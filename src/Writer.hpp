// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

/** The writer: the one thing that moves a Seq's position, every pulse, reset,
rate and HALT going in and the readers reading only what comes out. */

namespace rig {

/** The window in milliseconds when nothing says otherwise: how long after a
pulse its decision stays open for a reset, a rate or anything else that
decides what it means to belong to it. */
const double WINDOW_MS = 1.0;
/** And the most it may be: a reset that long after a pulse still takes that
pulse back to step one, so anything past a tenth of a second is a mistake in
the file rather than a choice, and an endless one would overflow the frames it
is compared over. Nought is the least, a decision open on its own frame alone,
and something that is not a number is the default. */
const double WINDOW_MS_MAX = 100.0;
inline double clampWindowMs(double ms) {
	if (!(ms == ms))
		return WINDOW_MS;
	return std::min(std::max(ms, 0.0), WINDOW_MS_MAX);
}

/** The window in force, in milliseconds: `WINDOW_MS` until the plugin, or
a headless test, has read window.json beside plugin.json (`loadWindowMs`, Rate.cpp). */
inline double& windowMs() {
	static double ms = WINDOW_MS;
	return ms;
}

struct Writer;

/** The wholes a cut journey owes. A pulse on a multiplied cable lands the
whole after the ramp (`Walk::land`, the Clock's `cutAt`), and a journey walked
with no tempo has no ramp, so the pulse that brings the first tempo, the
second on a fresh cable, would land one whole where the journey was the rate's
and leave the walk a step behind a walk that knew the tempo all along, for
good. `untimed` says the journey that pulse ends is such a one; `most` is the
wholes its ramp could have crossed and `crossed` those it did. The rest are
owed, and the journey the pulse begins runs that many wholes longer at the
tempo just measured, so its next pulse lands where the other walk stands. The
Seq's writer and a Clock following a cable both ask here, so the two never
part. */
inline int64_t owedWholes(bool untimed, int64_t most, int64_t crossed) {
	return untimed ? std::max(most - crossed, (int64_t)0) : 0;
}

/** A walk as the bracket's mark compares it: where it stands as a line,
x + q n after q more pulses in d-ths of a step (`Writer::line`), the
places in one cycle of it (the steps, or there and back), and the count it
walks, by identity. */
struct WalkLine {
	const void* count = nullptr;
	int64_t x = 0;
	int64_t n = 1, d = 1;
	int64_t cycle = 1;
};

inline int64_t gcdOf(int64_t a, int64_t b) {
	a = a < 0 ? -a : a;
	b = b < 0 ? -b : b;
	while (b) {
		const int64_t t = a % b;
		a = b;
		b = t;
	}
	return a;
}

/** Whether the walks of one count stand against each other as some Sync would
have left them: the bracket's mark, asked of the rig. A Sync arms every walk of a
count on place nought, the next pulse lands them all there, and each walks on
at its rate, so t pulses on walk i stands on t n_i modulo its cycle, in
d_i-ths of a step, its ramp carrying it between landings. The walks are in
line when one t puts every walk of a count where it stands: t n_i = x_i modulo
M_i = cycle_i d_i for every i. Then a press would put them where they already
stand against each other, the whole of them a time shift of what it leaves,
and nothing heard between them would change; otherwise it would.

The t is a time, not a count of pulses: it may fall between pulses. Asked for
a whole number of pulses, two Seqs at x2 standing together on step two were
off, since a Sync puts an x2 walk on odd places only, though a press would
change nothing between them, only where both stand against the Clock's bar,
which a lone Seq is free to do. A
t that solves walk i is a whole number over n_i, so every solution is a whole
number of L-ths of a pulse, L the least common multiple of the rates' n, and
in those units walk i asks t = x_i L/n_i modulo M_i L/n_i: congruences in
integers, which have a common solution exactly when every two agree modulo the
gcd of their moduli (the Chinese remainder theorem for moduli that need not be
coprime). Where any walk is at x1 or under division, n = 1, t is a whole
number of pulses.

A count walked by one walk is never off, nor is a walk compared with walks of
another count: there is nothing for a Sync to put right between them. Integers
all through, no float.

Asked two at a time, each pair in the L of its own two rates: congruences
that agree two by two have a common solution (the same theorem), and a pair's
agreement does not hang on the unit it is counted in, so the answer is the
whole rig's L's. A rig-wide L was the product of every rate's n, and with
rates to the millionth three Seqs on x1.000001, x1.000003 and x1.000007 put
it past 64 bits; a pair's L is at most two n's, under 2^52, and the moduli
it makes, a cycle times a d times the other's n, are worked out in 128 bits,
so no rig of any length overflows. */
inline bool inLine(std::vector<WalkLine>& w) {
	for (size_t i = 0; i < w.size(); i++)
		for (size_t j = i + 1; j < w.size(); j++) {
			if (w[i].count != w[j].count)
				continue;
			const int64_t ni = std::max(w[i].n, (int64_t)1), nj = std::max(w[j].n, (int64_t)1);
			const int64_t L = ni / gcdOf(ni, nj) * nj;
			__int128 a[2], b[2];
			const WalkLine* p[2] = {&w[i], &w[j]};
			const int64_t ns[2] = {ni, nj};
			for (int s = 0; s < 2; s++) {
				const __int128 k = L / ns[s];
				b[s] = (__int128)std::max(p[s]->cycle, (int64_t)1) * std::max(p[s]->d, (int64_t)1) * k;
				a[s] = (((__int128)p[s]->x * k) % b[s] + b[s]) % b[s];
			}
			// gcd of the two moduli, in 128 bits.
			__int128 g = b[0], h = b[1];
			while (h) {
				const __int128 t = g % h;
				g = h;
				h = t;
			}
			if ((a[0] - a[1]) % g != 0)
				return false;
		}
	return true;
}

/** Whether two lists of walks are the same walks, as `inLine` reads them. */
inline bool sameWalks(const std::vector<WalkLine>& u, const std::vector<WalkLine>& v) {
	if (u.size() != v.size())
		return false;
	for (size_t i = 0; i < u.size(); i++)
		if (u[i].count != v[i].count || u[i].x != v[i].x || u[i].n != v[i].n || u[i].d != v[i].d || u[i].cycle != v[i].cycle)
			return false;
	return true;
}

/** The window in frames at a sample rate, kept, and worked out again only
when the rate or the milliseconds move. */
struct WindowFrames {
	double rate = -1.0, ms = -1.0;
	int64_t frames = 0;
	inline int64_t at(double sampleRate);
};

/** One decision's walk: everything a landing is made from and makes. The
writer is one of these, the walk as published, and keeps a second, the walk
as it stood just before the last pulse with every event bound to that pulse
applied, which it lands again whenever such an event comes (`Writer::pre`,
`Writer::redecide`). */
struct Walk {
	// The landed position, exactly whole + sub/den, and the rate of the
	// journey it started, with the rate asked for beside it.
	int64_t whole = 0, sub = 0;
	int num = 1, den = 1;
	int wantNum = 1, wantDen = 1;
	// A pulse cuts the journey rather than landing its end: on a Seq's own
	// clock cable, multiplying, a pulse lands the whole after the ramp (under
	// division it lands its n-th as every driver's does), so at xN a pulse's
	// ramp crosses up to N-1 wholes and an early pulse starts the next step
	// rather than skipping the ones the ramp had not begun; with no period
	// there is no ramp, and a pulse is one step, the wholes it could not
	// reach paid back over the next journey (`owed`). A Clock following a
	// cable cuts its ticks the same way (Clock.cpp, `cutAt`), so the two never
	// part. Taken at a landing, as the rate is.
	bool cut = false, wantCut = false;
	// A cut journey walked with no tempo that has heard its first
	// (`periodAt`): the pulse that ends it owes what its ramp never crossed
	// (`owedWholes`). And the wholes the journey in progress pays back, which
	// make it that much longer than the rate (`length`).
	bool untimed = false;
	int64_t owed = 0;
	// How far through it the step the last landing left stood as it ended:
	// one, its end, but for a step a change of rate ended part way through
	// (`land`), which ends where its last journey did. What the step's edge
	// is measured from (`Seen::ended`).
	double ended = 1.0;
	bool armed = true; // standing on `whole`, shown and silent: a fresh writer is reset
	uint32_t began = 0; // the serial at the landing; the ramp's crossings add to it
	uint32_t starts = 0; // landings that were starts (`Seen::starts`)
	int64_t pulseFrame = -1; // where the journey began, -1 before the first landing
	// The part of the next landing's frame after the tick it lands, set by
	// the reader before the pulse (`Count::after`): its ramp starts that far
	// on. Nought but under a Clock in hertz.
	double lead = 0.0;

	// The ramp: the expected spacing of pulses in frames (nought for not
	// known), and the anchor progress is worked out from, in steps.
	double period = 0.0;
	int64_t anchorFrame = 0;
	double anchorProgress = 0.0;
	// And where the ramp stood the frame before the anchor, by the line it
	// was on then: what a landing or an arm on the anchor's own frame counts
	// as crossed (`land`, `armAt`), which the new line, run backwards, cannot
	// say. Run backwards at a fast tempo, a step or more a frame, it stood
	// below nought and the serial fell.
	double beforeProgress = 0.0;

	// Armed by a reset or a jump made now, the journey it interrupted, which
	// the ramp's anchor goes on describing until the landing: its landed
	// position and the step it stood in, so the step last sounded plays on
	// (`Seen::liveAlong`). `live` says there is one.
	bool live = false;
	int64_t liveWhole = 0, liveSub = 0, liveStep = 0;

	// Synced, how far the walk stands behind its landed position, in steps,
	// nought up to below one (`Writer::restart`): the readers take it off
	// what `at` says, so step one began on the reset's frame and the walk
	// goes on from there at its rate, the pulses landing its journeys as
	// ever. Kept by a landing and a KEEP, which leave the journey's grid
	// where it was; an arm or a LANDED, a journey begun afresh, clears it,
	// and so does a ONCE spent, standing at its end.
	double lag = 0.0;

	// HALT: pulses ignored, and the ramp stopped where it stood until the
	// next landing, which goes on from there.
	bool halted = false;
	bool frozen = false;
	double frozenProgress = 0.0;
	// A step a hand picked in HALT, sounded there: the first landing after
	// HALT moves on from it rather than starting its journey on it, since it
	// was heard when it was picked (`Writer::pick`).
	bool picked = false;

	// ONCE: one pass, the walk standing strictly below `stop`, the end of
	// the cycle it started in, the ramp capped short of it, and the pulse
	// that would land there spending the walk instead: on the last step, at
	// the end of it, pulses ignored until an arm or a landing elsewhere
	// (`Writer::stopAt`). `NO_STOP` for none.
	static constexpr int64_t NO_STOP = INT64_MAX;
	int64_t stop = NO_STOP;
	bool spent = false;

	/** A spent walk let go of, ONCE left: as a step picked in HALT, the next
	pulse moves on to the next whole and strikes it. */
	void letGo() {
		spent = false;
		sub = 0;
		frozen = picked = true;
		frozenProgress = 0.0;
	}

	/** Nought up to just below one, the most an `along` may be. */
	static constexpr double BELOW_ONE = 0x1.fffffffffffffp-1;
	/** The journey's length in den-ths of a step: the rate's num and the
	wholes it pays back (`owed`). Whatever measures the journey reads this;
	whatever asks the rate reads `num`. */
	int64_t length() const { return (int64_t)num + owed * den; }
	/** The journey's length less a hair, just below: what the ramp never
	reaches. Worked out when the length changes, not every sample. */
	double endBelow() const {
		if (length() != endNum || den != endDen) {
			endNum = length();
			endDen = den;
			endAt = std::nextafter((double)endNum / den, 0.0);
		}
		return endAt;
	}
	mutable int64_t endNum = 0;
	mutable int endDen = 0;
	mutable double endAt = 0.0;

	/** How far the ramp has gone into the journey at `frame`, in steps,
	strictly below its length. */
	double progress(int64_t frame) const { return armed ? 0.0 : rawProgress(frame); }
	/** The same whether armed or not: armed, the interrupted journey's. */
	double rawProgress(int64_t frame) const {
		if (pulseFrame < 0)
			return 0.0;
		double p = frozen                ? frozenProgress
		           : frame < anchorFrame ? beforeProgress
		           : period > 0.0        ? anchorProgress + (double)(frame - anchorFrame) * length() / (den * period)
		                                 : anchorProgress;
		return std::min(p, endBelow());
	}

	/** The most wholes the ramp may cross from `whole`: the journey's last
	whole short of its end, and in ONCE the last step short of the stop. */
	int64_t cap() const {
		const int64_t c = (sub + length() - 1) / den;
		return stop == NO_STOP ? c : std::max(std::min(c, stop - 1 - whole), (int64_t)0);
	}
	/** The wholes the ramp has crossed from `whole`, as an integer, capped
	(`cap`). */
	int64_t offset(double p) const {
		const int64_t k = (int64_t)std::floor(((double)sub + p * den) / den);
		return std::min(std::max(k, (int64_t)0), cap());
	}
	/** The step the walk stands in at `frame`, as a landing on it reads it:
	the ramp's crossings made strictly before it. */
	int64_t stoodAt(int64_t frame) const { return whole + offset(progress(frame - 1)); }

	/** The period from `frame` on; the ramp bends there without a jump. With
	`first`, the first period heard for a journey that had none, the ramp
	stands where it would have stood had the period been known from the
	landing: the tempo was there all along and only now measured. A cut
	walk lands the whole after its ramp whatever the ramp would have been, so
	there the tempo there all along is a debt instead: the pulse that ends
	this journey owes the wholes it could not reach (`untimed`, `land`). */
	void periodAt(double samples, int64_t frame, bool first) {
		if (samples == period)
			return;
		const bool measured = first && !(period > 0.0) && samples > 0.0 && !armed && pulseFrame >= 0 && !frozen;
		if (measured && cut)
			untimed = true;
		else if (measured) {
			anchorFrame = pulseFrame;
			anchorProgress = 0.0;
			beforeProgress = 0.0;
			period = samples;
			return;
		}
		beforeProgress = rawProgress(frame - 1);
		anchorProgress = rawProgress(frame); // armed, the interrupted journey's, which plays on
		anchorFrame = frame;
		period = samples > 0.0 ? samples : 0.0;
	}

	/** An arm at `place` at `frame`: shown, silent, sounded by the next
	landing. The steps the ramp has begun are kept in the serial before the
	arm takes the ramp away, so it never goes back, and the journey it
	interrupts goes on being described by the ramp's anchor, so the step it
	stood in plays on. */
	void armAt(int64_t place, int64_t frame) {
		const int64_t k = offset(progress(frame - 1));
		began += (uint32_t)k;
		if (!armed && pulseFrame >= 0) {
			live = true;
			liveWhole = whole;
			liveSub = sub;
			liveStep = whole + k;
		}
		whole = place;
		sub = 0;
		armed = true;
		untimed = false;
		picked = false;
		spent = false;
		lag = 0.0;
	}
	/** The step stood in at `frame` moved to `place`, its journey, its time
	and its arm kept and nothing begun: the same step read another way. */
	void keepAt(int64_t place, int64_t frame) { whole = place - offset(progress(frame)); }
	/** Standing on `place` as a landing does, a fresh journey from `frame`
	with nothing sounded. */
	void landedAt(int64_t place, int64_t frame) {
		began += (uint32_t)offset(progress(frame - 1));
		whole = place;
		sub = 0;
		armed = false;
		num = wantNum;
		den = wantDen;
		cut = wantCut;
		untimed = false;
		owed = 0;
		pulseFrame = anchorFrame = frame;
		anchorProgress = beforeProgress = 0.0;
		ended = 1.0;
		frozen = halted;
		frozenProgress = 0.0;
		live = false;
		picked = false;
		spent = false;
		lag = 0.0;
	}

	/** A landing at `frame`, of one journey and `extra` more (pulses on its
	own frame: one move, the journeys summed by arithmetic, however many). An
	armed walk lands where it stands, a start, and so takes no more journeys
	when `placed` (a reset or a jump bound to the pulse); the first pulse
	after HALT goes on from the point of the journey's grid at or below where
	it stood, nothing replayed, or on from a step picked in HALT to the next;
	any other lands its journey's end. The ramp's crossings are those made
	strictly before `frame`: a whole the ramp would enter on the landing's own
	frame is never shown, so it is not counted. */
	void land(int64_t frame, int64_t extra, bool placed) {
		const double p = progress(frame - 1);
		const int64_t k = offset(p);
		const int64_t shown = whole + k;
		const uint32_t serial = began + (uint32_t)k;
		bool start = true; // a landing that begins a step whatever the whole
		int64_t pay = 0;   // the wholes the next journey pays back
		ended = 1.0;
		if (armed) {
			// lands where it stands armed, sub already nought
		}
		else if (frozen) {
			const int64_t g = std::min(sub + (int64_t)std::floor(p * den), sub + length() - 1);
			whole += g / den;
			sub = g % den;
			if (picked) {
				// Heard when it was picked: on to the next whole, struck.
				whole++;
				sub = 0;
			}
			start = false;
		}
		else if (cut && den == 1) {
			whole = shown + 1;
			pay = owedWholes(untimed, cap(), k);
			start = false;
		}
		else {
			sub += num;
			whole += sub / den;
			sub %= den;
			start = false;
		}
		// A change of rate is the pulse's: landed part way through a divided
		// step, the pulse lands on the next whole instead, the step in
		// progress ending there early and the next one sounding on the pulse,
		// and the new rate's journey starts from that whole. A multiplied
		// rate always lands on a whole, so there it changes nothing; so the
		// one denominator in force is the new one from here, with no wait.
		if (sub != 0 && (num != wantNum || den != wantDen)) {
			ended = (double)sub / den;
			whole++;
			sub = 0;
		}
		num = wantNum;
		den = wantDen;
		cut = wantCut;
		owed = cut && den == 1 ? pay : 0;
		untimed = false;
		// The rest in one sum at the rate in force: exact, and no loop
		// however many; cut, a pulse is a step.
		if (!placed && extra > 0) {
			const int64_t t = sub + extra * (cut && den == 1 ? 1 : num);
			whole += t / den;
			sub = t % den;
		}
		// In ONCE a landing at the stop or past it spends the walk: it stays
		// on the last step, at its end, and nothing begins.
		const bool spends = whole >= stop;
		if (spends) {
			whole = stop - 1;
			sub = den - 1;
		}
		began = serial + ((start || whole != shown) ? 1u : 0u);
		if (start)
			starts++;
		armed = false;
		live = false;
		picked = false;
		pulseFrame = frame;
		anchorFrame = frame;
		anchorProgress = period > 0.0 ? lead * length() / (den * period) : 0.0;
		beforeProgress = 0.0;
		spent = spends;
		frozen = halted || spent;
		frozenProgress = spent ? endBelow() : 0.0;
		// Spent, a synced pass stands at its end as any other does: its
		// last step was cut short by the sync, the stop on the pulses' grid.
		if (spent)
			lag = 0.0;
	}
};

struct Writer : Walk {
	/** How a relocation is made. NOW arms at the place: shown, silent, and
	sounded by the next pulse (a reset, a step let go after a drag, a step a
	hand picks in HALT), or, inside the window after a pulse, that pulse's
	landing put there, a start. KEEP moves the step stood on to the place as
	it stands, its journey, its time and its arm kept and nothing begun: the
	same step read another way, a change of direction or a block reshaped
	under the walk, made on the walk as published (`keepPulse` is the same
	inside the window, on the walk the pulse landed from). LANDED stands on
	the place as a landing does, a fresh journey from now with nothing
	sounded: a new count taken on, the walk going on from the step it stood
	on. */
	enum When { NOW, KEEP, LANDED };

	/** What the readers see at a frame: the step the position stands in, how
	far through it (nought up to just below one), whether it is armed, and
	the serial of steps begun. */
	struct Seen {
		int64_t step = 0;
		double along = 0.0;
		bool armed = true;
		uint32_t began = 0;
		/** The serial of starts: a landing out of an arm, a reset bound to
		its pulse, a jump. A start is the downbeat; on step one the Seq gives
		it an EOC, a cycle begun. */
		uint32_t starts = 0;
		/** Armed, how far through the step it last sounded the walk would
		stand had the arm not come: that step plays on, live, until the
		landing, its curve going on rather than snapped to its start. */
		double liveAlong = 0.0;
		/** The serial of re-decisions that moved what a landing stands on: a
		reader seeing it move with `began` still is the same landing
		re-aimed, its trigger already sent the one trigger. */
		uint32_t aims = 0;
		/** How far through it the step before stood as it ended, on the
		frame a landing began this one (`Walk::ended`): one, but for a step
		a change of rate ended early. A whole the ramp crossed ends at one. */
		double ended = 1.0;
	};

	/** The window, in frames; the caller converts (`windowFrames`). */
	int64_t window = 0;

	// The last pulse's decision, open for the window: its frame (-1 for
	// none), the pulses that came on it, whether a reset or a jump bound to
	// it put it on a place, and the walk as it stood just before it with
	// every event bound to it applied, which the landing is made from.
	int64_t openFrame = -1;
	int64_t openPulses = 0;
	bool openPlaced = false;
	Walk pre;
	uint32_t aims = 0;

	/** The window in frames at a sample rate: a count of frames, so whole
	from here on. */
	static int64_t windowFrames(double sampleRate, double ms) {
		const double f = clampWindowMs(ms) * sampleRate / 1000.0;
		// Not an epsilon: a bound on a count, far past any window (1e15 frames is
		// years at any rate) and well inside int64_t, so llround never overflows.
		return f > 0.0 && f < 1e15 ? (int64_t)std::llround(f) : 0;
	}

	/** Whether an event at `frame` belongs to the last pulse: inside its
	window, the due frame included. Compared as a distance, so no window
	overflows the frame. */
	bool open(int64_t frame) const {
		return openFrame >= 0 && frame >= openFrame && frame - openFrame <= window;
	}
	/** Whether the last pulse's decision is open at `frame` with a landing
	in it to re-aim: a pulse that found a ONCE spent opens a decision a reset
	takes as a start, but lands nothing a turn, a reshape or a direction
	turned could re-aim, so those go on the walk as published, as between
	pulses. */
	bool landing(int64_t frame) const { return open(frame) && !pre.spent; }
	/** The step the last pulse landed from, inside its window: what a KEEP
	bound to that pulse reads (`keepPulse`). */
	int64_t preStood() const { return pre.stoodAt(openFrame); }

	/** Asks for a rate at `frame`, `n` steps every `d` pulses (`ratioOf`): it
	takes effect at the next pulse, landed on the next whole if it would land
	part way through a step (`land`), or, inside the window, at the landing
	just made, made again by the same rule, the journey it began taking the
	rate from the pulse's own frame. `cutShort` is `cut`, taken the same way. */
	void want(int n, int d, int64_t frame, bool cutShort = false) {
		if (open(frame)) {
			if (pre.wantNum != n || pre.wantDen != d || pre.wantCut != cutShort) {
				pre.wantNum = n;
				pre.wantDen = d;
				pre.wantCut = cutShort;
				redecide(frame);
			}
			return;
		}
		wantNum = n;
		wantDen = d;
		wantCut = cutShort;
	}

	/** Whether a rate asked at `frame` would be taken with nothing moved:
	outside the window, where it would make the last landing again from the
	pulse's frame, and with the next landing on a whole, where a change of
	rate ends no divided step early (`land`); armed, the landing is on a
	whole already. A free rate asks only then, and the turn it gains on the
	writer carries the walk between (Seq::rateTurn). */
	bool takesRateSmoothly(int64_t frame) const {
		return !open(frame) && !frozen && (armed || (sub + num) % den == 0);
	}

	/** The period from `frame` on (`Walk::periodAt`); inside the window, from
	the pulse's own frame, the ramp the pulse began running at it. There the
	first period places no ramp, the pulse having landed already, but a cut
	walk still hears it as its debt. */
	void setPeriod(double samples, int64_t frame, bool first = false) {
		if (open(frame)) {
			if (samples != pre.period) {
				pre.periodAt(samples, openFrame, first && pre.cut);
				redecide(frame);
			}
			return;
		}
		periodAt(samples, frame, first);
	}

	/** `n` pulses at `frame`, one by default. More than one pulse on one
	frame, a count crossing two wholes at once or a hundred a bypassed Seq
	slept through, is one landing of as many journeys, and a pulse on the
	frame of the last one is journeys more on it. Ignored in HALT. A spent
	ONCE lands nothing, but its pulse still opens a decision, so a reset
	inside the window, a trigger the same pulse sent into RESET, is a start
	on that pulse (were it only armed, step one would wait a pulse); anything else bound to it leaves
	the walk spent (`redecide`). */
	void pulse(int64_t frame, int64_t n = 1) {
		if (n <= 0 || halted)
			return;
		if (frame == openFrame) {
			openPulses += n;
			redecide(frame);
			return;
		}
		pre = *this;
		openFrame = frame;
		openPulses = n;
		openPlaced = false;
		if (!spent)
			land(frame, n - 1, false);
	}

	/** A reset: step one, by the window's rule (`relocate`, NOW). A Sync's
	and a Seq's own are the same to the walk. */
	void reset(int64_t frame) { relocate(0, NOW, frame); }
	/** A reset the Clock has said comes after its last tick, not with it
	(`Count::resetBinds`): an arm, whatever this Seq's own window says, so
	a Seq hearing the tick a cable sample late never binds a reset the rig
	did not. The landing stands. */
	void arm(int64_t place, int64_t frame) {
		openFrame = -1;
		armAt(place, frame);
	}

	void relocate(int64_t place, When when, int64_t frame) {
		if (when == KEEP) {
			openFrame = -1;
			keepAt(place, frame);
			return;
		}
		if (when == LANDED) {
			openFrame = -1;
			landedAt(place, frame);
			return;
		}
		if (open(frame)) {
			pre.armAt(place, openFrame);
			openPlaced = true;
			redecide(frame);
			return;
		}
		armAt(place, frame);
	}
	/** A KEEP bound to the last pulse, inside its window: the step it landed
	from (`preStood`) read as `place`, and the landing made again from
	there. */
	void keepPulse(int64_t place, int64_t frame) {
		if (!open(frame))
			return relocate(place, KEEP, frame);
		pre.keepAt(place, openFrame - 1); // the step it stood in as the pulse came (`stoodAt`)
		redecide(frame);
	}

	/** An arm that sounds, HALT's: the walk stands on `place` now, sounded
	and begun, a fresh journey from here that goes nowhere while HALT holds
	it, so the first pulse after HALT starts that journey from there and
	strikes nothing again (a reset in HALT is step one there, sounded). With
	`start` it is a start, the downbeat, and on step one the Seq gives it an
	EOC. */
	void sound(int64_t place, int64_t frame, bool start) {
		relocate(place, LANDED, frame);
		began++;
		if (start)
			starts++;
	}

	/** A reset under a Clock in hertz, the hard sync of an oscillator: step
	one now, a start, struck on this frame, and the walk going on from it at
	its rate. The step stood in is read as place nought (`keepAt`), its
	journey, its time and the pulses' grid kept, and the walk stands behind
	the writer by how far through that step it was (`lag`), so the next
	pulse lands the journey as ever and the walk carries on a step's
	fraction behind the grid until the next reset moves it again. Where the
	walk is on no journey that moves, armed, spent, halted, never landed or
	under a stopped Clock (no period), there is no grid to move it on, and
	the reset arms as every other does (`reset`): the next pulse sounds step
	one. */
	void restart(int64_t frame) {
		if (armed || frozen || pulseFrame < 0 || !(period > 0.0))
			return reset(frame);
		const double a = at(frame).along;
		openFrame = -1;
		keepAt(0, frame);
		lag = a;
		starts++;
	}

	/** A step a hand picks in HALT: stood on now and sounded, as HALT's reset
	is (`sound`), and the first pulse after HALT moves on from it to the next
	step rather than starting its journey on it, since it was heard when it
	was picked (an arm there would strike it a second time). */
	void pick(int64_t place, int64_t frame) {
		sound(place, frame, false);
		picked = true;
	}

	/** HALT on or off at `frame`. On, pulses are ignored and the ramp stops;
	off, it stays stopped, and the first pulse after it starts a journey from
	where it stood. Either way the last pulse's decision is closed: HALT is
	the hand's, and a landing heard is not taken back by it. */
	void halt(bool on, int64_t frame) {
		if (on == halted)
			return;
		openFrame = -1;
		halted = on;
		if (on && !frozen) {
			frozenProgress = rawProgress(frame);
			frozen = true;
		}
	}

	/** ONCE on or off: the walk stops short of `place`, the end of the
	cycle, or `NO_STOP` for none, on the walk as published and on the one the
	open pulse landed from alike, so a reset bound to the pulse that spent it
	re-decides it as a start. Off, a spent walk is let go as a step picked in
	HALT is: the next pulse moves on to the next whole and strikes it, and,
	the hand's as HALT is, the last pulse's decision is closed, so nothing
	bound to it lands it again as if ONCE had never held it. */
	void stopAt(int64_t place) {
		pre.stop = place;
		if (place == stop)
			return;
		stop = place;
		if (place == NO_STOP && spent) {
			openFrame = -1;
			letGo();
		}
		else if (!armed && whole >= place) {
			// The cycle shortened under a walk that stood past its new end.
			whole = place - 1;
			sub = den - 1;
			spent = frozen = true;
			frozenProgress = endBelow();
			lag = 0.0;
		}
	}

	/** Whether the ramp is moving at `frame`: landed, a period known, not
	stopped by HALT, and not yet standing just short of its journey's end. */
	bool ramping(int64_t frame) const {
		return !armed && pulseFrame >= 0 && !frozen && period > 0.0 &&
		       progress(frame) < endBelow();
	}

	/** Whether, armed, the step last sounded is still playing on along the
	interrupted journey, which a reader of its curve hears every sample. */
	bool liveRamping(int64_t frame) const {
		return armed && live && !frozen && period > 0.0 && rawProgress(frame) < endBelow();
	}

	/** The first frame after `frame` at which what the readers see moves by
	itself, with no event: the ramp entering a whole it was not in at `frame`;
	-1 for none, armed, stopped, with no period, or on the journey's last
	whole. What a gated host wakes the writer for. Worked out from the same
	arithmetic `at` uses, then walked to the exact frame, so the two cannot
	disagree. A decision's window closing moves nothing, so it is no event. */
	int64_t nextEvent(int64_t frame) const {
		// Asked every wake and moving only with the ramp: the frame found is
		// the answer for every frame from where it was asked up to it, while
		// nothing the ramp is worked out from has changed.
		CrossMemo now = {armed, frozen, num, den, owed, pulseFrame, anchorFrame, sub, whole, stop, period, anchorProgress, frozenProgress};
		if (crossKnown && now == crossState && frame >= crossFrom && (crossAt < 0 || frame < crossAt))
			return crossAt;
		crossKnown = true;
		crossState = now;
		crossFrom = frame;
		crossAt = crossingAfter(frame);
		return crossAt;
	}
	/** What the ramp is worked out from, for `nextEvent`'s memory. */
	struct CrossMemo {
		bool armed, frozen;
		int num, den;
		int64_t owed, pulseFrame, anchorFrame, sub, whole, stop;
		double period, anchorProgress, frozenProgress;
		bool operator==(const CrossMemo& o) const {
			return armed == o.armed && frozen == o.frozen && num == o.num && den == o.den && owed == o.owed && pulseFrame == o.pulseFrame &&
			       anchorFrame == o.anchorFrame && sub == o.sub && whole == o.whole && stop == o.stop && period == o.period &&
			       anchorProgress == o.anchorProgress && frozenProgress == o.frozenProgress;
		}
	};
	mutable bool crossKnown = false;
	mutable CrossMemo crossState = {};
	mutable int64_t crossFrom = 0, crossAt = -1;
	int64_t crossingAfter(int64_t frame) const {
		if (armed || pulseFrame < 0 || frozen || !(period > 0.0))
			return -1;
		const int64_t k = offset(progress(frame));
		if (k >= cap())
			return -1;
		// progress reaching the (k+1)th whole: (sub + p den) / den = k + 1
		const double target = ((double)(k + 1) * den - (double)sub) / den;
		const double guess = (double)anchorFrame + (target - anchorProgress) * den * period / length();
		int64_t f = guess > (double)frame ? (int64_t)std::ceil(guess) : frame + 1;
		// Not an epsilon: a crossing past int64_t's reach (9.2e18) has no frame
		// to name, so it is none, and the ceil above never meets an overflow.
		if (guess > 4e18)
			return -1;
		while (f > frame + 1 && offset(progress(f - 1)) > k)
			f--;
		while (offset(progress(f)) <= k)
			f++;
		return f;
	}

	/** The walk as the bracket's mark compares it (`inLine`): `x`, in d-ths of a
	step, and the rate `n` over `d` it walks at for good, such that q pulses
	from now, q far enough on, it has landed on x + q n. What the next few
	pulses will do is taken as done, each exactly as `land` does it, on a
	copy: an arm lands on its place, the first pulse after HALT lands where
	the ramp stood, and a change of rate waiting for its pulse lands on the
	next whole; the pulses that took are then taken back off at the rate kept.
	An armed walk is so treated as its landing will be, on its place a pulse
	from now. Integers all through. HALT is the caller's to leave out: halted,
	the walk hears no pulse and has no line. Comparing the pulses since the
	last Sync, walked at the rate in force, with where the walk stood put a
	lone Seq whose rate was turned mid-run off the line for good. */
	WalkLine line() const {
		WalkLine l;
		if (!armed && !frozen && num == wantNum && den == wantDen) {
			l.x = whole * den + sub + owed * den;
			l.n = num;
			l.d = den;
			return l;
		}
		Walk c = *this;
		c.halted = false;
		c.stop = NO_STOP;
		c.spent = false;
		// One landing takes an arm, HALT or a waiting rate (`land`); the
		// bound only keeps a walk of another shape from looping for ever.
		int64_t k = 0;
		for (int i = 0; i <= c.den + 1 && (c.armed || c.frozen || c.num != c.wantNum || c.den != c.wantDen); i++) {
			c.land(c.anchorFrame, 0, false);
			k++;
		}
		l.x = c.whole * c.den + c.sub + c.owed * c.den - k * c.num;
		l.n = c.num;
		l.d = c.den;
		return l;
	}

	/** What the readers see at `frame`. */
	Seen at(int64_t frame) const {
		Seen s;
		const double p = progress(frame);
		const int64_t k = offset(p);
		s.step = whole + k;
		s.armed = armed;
		s.began = began + (uint32_t)k;
		const double a = ((double)sub + p * den) / den - (double)k;
		s.along = std::min(std::max(a, 0.0), BELOW_ONE);
		s.starts = starts;
		s.aims = aims;
		s.ended = k == 0 ? ended : 1.0;
		if (armed && live) {
			const double r = rawProgress(frame);
			const double l = (double)(liveWhole - liveStep) + ((double)liveSub + r * den) / den;
			s.liveAlong = std::min(std::max(l, 0.0), BELOW_ONE);
		}
		return s;
	}

private:
	/** The last pulse's landing made again from `pre`, every event bound to
	it applied, at `frame`, the event's. What the readers had seen stays
	seen: the serial of steps begun never goes back, and moves only for a
	step the new landing begins that the old did not, so a trigger already
	sent is the one trigger; starts likewise. Where the step stood on, its
	arm or its starts moved, `aims` moves, and a reader takes that as the
	landing re-aimed. A walk the pulse found spent, and nothing has placed,
	lands nothing: a rate, a period, a reshape bound to it leave it spent,
	where it stands, silent. */
	void redecide(int64_t frame) {
		const uint32_t seen = began + (uint32_t)offset(progress(frame - 1));
		const uint32_t landedBegan = began, landedStarts = starts;
		const int64_t was = whole + offset(progress(frame));
		const bool wasArmed = armed;
		static_cast<Walk&>(*this) = pre;
		if (!spent)
			land(openFrame, openPulses - 1, openPlaced);
		if ((int32_t)(landedBegan - began) > 0)
			began = landedBegan;
		const uint32_t now = began + (uint32_t)offset(progress(frame));
		if ((int32_t)(seen - now) > 0)
			began += seen - now;
		if ((int32_t)(landedStarts - starts) > 0)
			starts = landedStarts;
		if (whole + offset(progress(frame)) != was || armed != wasArmed || starts != landedStarts)
			aims++;
	}
};

inline int64_t WindowFrames::at(double sampleRate) {
	const double m = windowMs();
	if (sampleRate != rate || m != ms) {
		rate = sampleRate;
		ms = m;
		frames = Writer::windowFrames(sampleRate, m);
	}
	return frames;
}

} // namespace rig
