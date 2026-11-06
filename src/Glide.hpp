// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>

namespace rig {

/** How many glides in the plugin are on their way, every rig's together: a
count kept by the glides themselves as they set off and land, never read by
the engine, for the test harness's "settle", which asks whether everything a hand moved has landed. Several rigs' hosts
may step at once, each on its own thread, so it is an atomic, touched only
when a glide sets off or lands, never on a sample it stands or travels.
The release form counts too, unread, so a glide is one code path in both
forms. */
inline std::atomic<int>& glidesUnderway() {
	static std::atomic<int> n{0};
	return n;
}

/** A setting the engine plays, gliding to where the hand put it: one
interpolation for every control a mouse drags. A stroke lands on a param a frame of the window at
a time, and a gain or a point jumping sixty times a second is a click on
whatever it scales, so each new setting is reached by a straight line from
wherever the last one had got to, and stood on exactly at its end: the
frames left are a whole number, counted down, and the last one is the
setting itself, never an asymptote and never a tolerance.

The line lasts as long as the hand took to send it, or a slow drag on an
Atn's gain is heard as steps: a slow stroke's moves arrive 50 to 150 ms apart, and a line of a fixed 20 ms
finished and stood between them, a staircase with smoothed risers. So a new
setting is reached over the frames since the one before it arrived, held
between `SECONDS` and `LONGEST`, and a line ends about when the next move
is due, the slow stroke one unbroken line. A setting arriving more than
`LONGEST` after the last is a fresh touch, a double click or a typed value
or a stroke's first move, and is reached over `SECONDS`, never dragged out
by the pause before it. A caller may name a distance at which a move is a
jump rather than a stroke's (`jumpAt`, the Clock's half octave in hertz),
reached over `SNAP_SECONDS` whatever the pause: softened, no swoop. The
frame each setting arrived on is kept, so the pause is one subtraction when
a setting arrives and nothing is counted while it stands.

The first setting it is handed is stood on at once, so a patch loading, a
module dropped in or a rig bonded starts where it stands with no sweep from
nought; a RIG's recall lands at once too, a scene change being no stroke and
resetting the molecule on the sample it is heard; and a change of the sample
rate stands on the setting at once rather than glide a stale count of frames. Standing still it costs one comparison
and one test.

It knows nothing of what it glides: the caller hands it a value in the
domain the thing should move linearly in (a gain, a travel, a bend; the
Clock's frequency in octaves), and glides only what is continuous. A count,
a step, a key or a note snapped to a pattern is a jump, and its caller
reads the setting itself (`played` with the setting as it stands). The state
is plain fields of the module that owns it, stepped once a sample on the
engine's one thread for its rig in that module's
`everySample`, and read by its wake: never by a screen, which shows the
setting in force at once. */
struct Glide {
	static constexpr double SECONDS = 0.02, LONGEST = 0.1, SNAP_SECONDS = 0.005;
	double now = 0.0, slope = 0.0;
	float aim = 0.f, rate = 0.f;
	int left = -1; // frames still to glide; below nought, nothing set yet
	/** The frame `aim` arrived on, by a stroke; a setting stood on at once
	(the first, a recall, a new rate, a jump) is long ago, so the next is a
	fresh touch rather than a stroke going on. */
	static constexpr int64_t LONG_AGO = INT64_MIN / 2;
	int64_t aimedAt = LONG_AGO;
	/** A move from the last setting at least this far is a jump, reached
	over `SNAP_SECONDS`; by default none is. */
	float jumpAt = std::numeric_limits<float>::infinity();
	Glide() = default;
	explicit Glide(float jumpAt) : jumpAt(jumpAt) {}
	Glide(const Glide&) = delete; // a copy would be counted twice in `glidesUnderway`
	Glide& operator=(const Glide&) = delete;
	~Glide() { setLeft(-1); }
	/** One sample, `frame` (the engine's `args.frame`), toward `setting`,
	or onto it at once where `land` says a RIG has just recalled it
	(`Member::landing`). Returns whether what it plays moved. */
	bool step(float setting, float sampleRate, int64_t frame, bool land = false) {
		if (left == 0 && setting == aim)
			return false;
		if (left < 0 || sampleRate != rate || land) {
			// The first setting, a new sample rate or a recall: stood on at
			// once.
			const bool moved = left < 0 || now != (double)setting;
			now = aim = setting;
			aimedAt = LONG_AGO;
			rate = sampleRate;
			setLeft(0);
			return moved;
		}
		if (setting != aim) {
			setLeft(framesFor(setting, frame - aimedAt, sampleRate));
			aim = setting;
			aimedAt = frame;
			slope = ((double)setting - now) / left;
		}
		setLeft(left - 1);
		now = left == 0 ? (double)aim : now + slope;
		return true;
	}
	/** Whether it is on its way and wants the next sample. */
	bool moving() const { return left > 0; }
	/** What the engine plays: where the glide stands, or `setting` itself
	before the first step, so a wake ahead of the first sample still plays
	the setting and never nought. */
	float played(float setting) const { return (float)playedFine(setting); }
	/** The same in the double it travels in, for a caller that plays it
	through a curve, the Clock's octaves through `exp2`. */
	double playedFine(float setting) const { return left < 0 ? (double)setting : now; }
	/** Stand on `setting` at once: what the patch performs on a param, a
	Seq's REC take, is heard on its own sample, never glided. */
	void jump(float setting) {
		now = aim = setting;
		aimedAt = LONG_AGO;
		setLeft(rate > 0.f ? 0 : -1);
	}

private:
	/** The frames of the line to `setting`, `pause` frames after the last
	setting arrived: the pause held between `SECONDS` and `LONGEST`, a
	longer one a fresh touch over `SECONDS`, a jump over `SNAP_SECONDS`. */
	int framesFor(float setting, int64_t pause, float sampleRate) const {
		const double sr = sampleRate;
		if (std::fabs((double)setting - (double)aim) >= (double)jumpAt)
			return std::max(1, (int)std::lround(SNAP_SECONDS * sr));
		const int64_t shortest = std::max(1, (int)std::lround(SECONDS * sr));
		const int64_t longest = std::max(shortest, (int64_t)std::lround(LONGEST * sr));
		return (int)(pause > longest ? shortest : std::max(shortest, pause));
	}
	/** The frames left, `glidesUnderway` told when the glide sets off or lands. */
	void setLeft(int frames) {
		if ((frames > 0) != (left > 0))
			glidesUnderway().fetch_add(frames > 0 ? 1 : -1, std::memory_order_relaxed);
		left = frames;
	}
};

} // namespace rig
