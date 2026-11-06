// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

/** How long things last: a trigger on a cable, and a transient on a screen.

Two lengths, kept apart on purpose. What a cable carries is Rack's standard trigger, and every trigger
the plugin puts out is that one, wherever it is made or passed on: a longer
gate is a different thing and is made as one, never a trigger stretched by
the way it travelled. What a screen shows of a signal is another matter: a
millisecond is a sixteenth of a frame and an eye never sees it, so a screen
that watches a signal holds a short transient and lets it fall, and a
one-sample spike shows as a blink. Both are constants here and nowhere else,
so either is changed in one place. They are constants and not a settings
file on purpose: a constant read at build is one place and costs nothing on
the audio thread, where a setting would be a load on every sample.

Engine side, as everything a screen is handed is measured on the audio
thread, so this includes nothing of the look and the look nothing of it. */

namespace rig {

/** A trigger's pair of thresholds, Rack's own: low under a tenth of a volt,
high at a volt, and a gate counts as high over the volt. Every Schmitt
trigger in the rig and the RUN jack use these, and a ring, a Poly's feed and
a Seq's REC count a gate high over the volt too, so a ring lit by a Trg's IN
cable and one lit by its own cable (`Trg::latchRings`) light at the same
volt. */
const float TRIG_LOW = 0.1f;
const float TRIG_HIGH = 1.f;

/** A trigger on a cable: ten volts for a millisecond, the VCV voltage
standard's trigger and what Fundamental's dsp::PulseGenerator gives. */
const float TRIG_VOLTS = 10.f;
const int TRIG_MS = 1;

/** How many samples a trigger stands high: `TRIG_MS` counted in integers,
rounded up, since Rack's rates are whole hertz: 45 samples at 44.1 kHz,
never 44, which stops short of it. */
inline int64_t pulseFrames(float sampleRate) {
	const int64_t hz = (int64_t)std::lround(sampleRate);
	return (hz * TRIG_MS + 999) / 1000;
}

/** How long a trigger stands when the next may come sooner than a
millisecond after it: `pulse` samples (`pulseFrames`), or half of `gap`, the
samples to the next as far as the firer knows, whichever is shorter, and never
under one. A Seq stepping faster than two milliseconds, a Clock at 4095 BPM
times x255, still puts out a trigger a step, each parted from the next by a
fall, rather than a run of them merged into one gate. Under two samples a step there is no room
for a fall and they run together, which no cable can carry otherwise. */
inline int64_t triggerFrames(int64_t pulse, int64_t gap) {
	return std::max<int64_t>(1, std::min(pulse, gap / 2));
}

/** A transient on a screen: how long a mark takes to fall from full to
nothing, a bar from ten volts or a ring's bloom from its brightest. A
sixth of a second, sixty volts a second down a Poly's bar: long enough for a
spike to be seen, the one fall every screen shares. */
const float SHOW_FALL_SECONDS = 1.f / 6.f;

/** A jack's blink: how long the light in an output's centre, and in the
plug of a cable from it, takes to fall from full to nothing, the same hold
(`Held`) over a shorter fall. A tenth of a second, which stays snappy, and a
straight fall rather than one that loses most of its light in the first
frame: a trigger falling between two frames would be drawn at anything from
full to a third, depending on where in the frame it fell, and the dim ones
missed. Straight, the first frame after a trigger reads it at two thirds or
more at thirty frames a second and at half or more at twenty. It is not a
screen's
`SHOW_FALL_SECONDS` because a jack lights for every trigger, a whole row of
them at once, and a sixth of a second on each would smear a fast pattern. */
const float JACK_FALL_SECONDS = 0.1f;

/** A screen's hold on a signal: it jumps to anything further from nought
than it stands and falls back by `SHOW_FALL_SECONDS` a full scale, so a slow
signal reads as itself with a little lag on the way back and a trigger
stands long enough to be seen. `see` takes every value the owner has between
two looks, a sample's or a wake's, so a spike between looks is caught rather
than stepped over; `look` is made on the owner's control period and returns
the mark, signed. Audio thread only: the owner stores the mark where the
screen reads it. */
struct Held {
	float mark = 0.f;   // what the screen shows, signed
	float caught = 0.f; // the furthest from nought seen since the last look, signed
	bool any = false;
	void see(float v) {
		if (!any || std::fabs(v) > std::fabs(caught)) {
			caught = v;
			any = true;
		}
	}
	/** `now` the value at the look, `dt` the seconds since the last one,
	`span` the full scale in the mark's own unit; `seconds` the fall from
	full scale to nothing, a screen's unless a jack's (`JACK_FALL_SECONDS`). */
	float look(float now, float dt, float span, float seconds = SHOW_FALL_SECONDS) {
		see(now);
		const float v = caught;
		any = false;
		const float fall = span * dt / seconds;
		// A mark past the full scale is past what any screen shows, so it
		// falls from the full scale: a voltage off the rails is held no
		// longer than one on them.
		const float from = std::min(std::fabs(mark), span);
		if (std::fabs(v) >= from - fall)
			mark = v; // further out, or within a step of it: taken as it is, sign and all
		else
			mark = std::copysign(from - fall, mark); // falling, keeping its sign until the level below is reached
		return mark;
	}
	void rest() {
		mark = caught = 0.f;
		any = false;
	}
};

/** A screen's hold on a signal that swings both ways, a bar of a Poly or a
Mix: a `Held` on each side of nought, the one above taking what the signal
has above it and the one below what it has below, so the screen draws the
span between the two marks. A signal on one side of nought is one plain
hold, the other standing at nought, so a CV, a gate or a trigger reads as
itself; a signal crossing nought slowly is followed through it, the side it
leaves falling over `SHOW_FALL_SECONDS` as any mark does; and one crossing
faster than a mark falls holds both, the span of its swing. There is no
rate where it changes: the two marks are the same rule at every speed. One
signed hold would keep whichever side had the bigger peak, so an audio wave
higher on one side than the other would stand still on that side and a wave
equal both ways flip between them at whatever phase a frame caught. */
struct HeldSpan {
	Held up, down;
	void see(float v) {
		up.see(std::max(v, 0.f));
		down.see(std::min(v, 0.f));
	}
	void look(float now, float dt, float span) {
		up.look(std::max(now, 0.f), dt, span);
		down.look(std::min(now, 0.f), dt, span);
	}
	/** The span packed in one word, for the screen: below nought in the
	low half, above it in the high, so a frame never reads one look's top
	over another's foot. */
	uint64_t packed() const {
		uint32_t lo, hi;
		std::memcpy(&lo, &down.mark, sizeof lo);
		std::memcpy(&hi, &up.mark, sizeof hi);
		return (uint64_t)hi << 32 | lo;
	}
	void rest() {
		up.rest();
		down.rest();
	}
};
/** A `HeldSpan::packed` word unpacked: the foot, at or below nought, and
the top, at or above it. */
inline void unpackSpan(uint64_t w, float& foot, float& top) {
	const uint32_t lo = (uint32_t)w, hi = (uint32_t)(w >> 32);
	std::memcpy(&foot, &lo, sizeof foot);
	std::memcpy(&top, &hi, sizeof top);
}
/** The one mark of a span, the side further from nought, the bar as one
level: what a gatherer's bar is held against its jack by. */
inline float spanMark(float foot, float top) {
	return std::fabs(foot) > top ? foot : top;
}

/** A bloom, a mark with no level to it: struck at one and fallen to nothing
over `SHOW_FALL_SECONDS`, for a ring a trigger lands on. */
inline float bloomFall(float bloom, float dt) {
	return std::max(0.f, bloom - dt / SHOW_FALL_SECONDS);
}

} // namespace rig
