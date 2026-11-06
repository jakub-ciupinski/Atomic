// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/** A Seq's walk, the arithmetic that reads a place as a step: Dir, Turn,
wrapPlace, shorterWhole, reachesOne, cycleOf, stepAt, placeOf, goingDown and
alongBack. A part of Seq.cpp in a file of its own: Seq.cpp includes it once,
inside its anonymous namespace, and it is not a header to include anywhere
else. */

/** Which way a Seq walks its steps: the direction cell on the step screen, a
click going round the three. The walk itself only ever counts on, a place `k`
that the writer and the PHASE jack move, and the direction is how a place is
read as a step: forward; reverse the same places read from the other end, so a
reset is step one and the next is the last; and the pendulum one cycle of
there and back, each end once and never doubled, twice the steps less two.
Because it is only a reading, everything else keeps working without knowing of
it: a reset, a jump, a Sync, the grid. EOC is a pulse as the walk comes round,
its place crossing a whole cycle, so once a cycle in every direction, once a
there-and-back on a pendulum, an early pulse that skips over step one included;
and on a start on step one as well, a birth, RUN, a reset or a Sync, since that
too begins a cycle (`Seq`'s walk says what goes wrong without it). */
// In the order a click goes round them, forward, pendulum and reverse.
enum Dir { DIR_FORWARD, DIR_PENDULUM, DIR_REVERSE, DIRS, DIR_BOUNCE = DIRS };
/** DIR_BOUNCE is no setting of the cell's: it is the pendulum over a block of
CRVs, where every step is a stretch and the walk back plays each one back from
its end to its start, the last and the first included, so the light bounces
off the ends rather than leaving the last stretch for the one before it and
jumping. A cycle of it is every step there and every step back, twice the
steps. */
inline int64_t wrapPlace(int64_t k, int64_t m) { return ((k % m) + m) % m; }
/** The whole places walked from place `from`, `fromAlong` through it, to
place `to`, `toAlong` through it, the shorter way round a cycle of `cycle`
places: a place PHASE turned is known only round the cycle, so the two ways
are a whole cycle apart, and the fractions belong in the choice. Counting
wholes alone, a step back across an edge on a cycle of two was the step
forward, the same place, and a BLEP of the curves' far ends rang where they
met with no jump. Ties go forward. */
inline int64_t shorterWhole(int64_t from, double fromAlong, int64_t to, double toAlong, int64_t cycle) {
	const int64_t whole = wrapPlace(to - from, cycle);
	// Forward walks whole + d places and back whole - cycle + d, d the
	// fractions' difference: back where forward is the longer.
	return (double)(2 * whole - cycle) + 2.0 * (toAlong - fromAlong) > 0.0 ? whole - cycle : whole;
}
/** A turn of the sequence in places, split exactly into the whole places
under it and the fraction past them, nought up to below one: a double less
its own floor is exact. */
struct Turn {
	int64_t whole = 0;
	double frac = 0.0;
	static Turn of(double by) {
		Turn t;
		const double f = std::floor(by);
		t.whole = (int64_t)f;
		t.frac = by - f;
		return t;
	}
};
/** Whether `a + b` reaches one, both nought up to below one, exactly and
without summing them: one less whichever is at least a half is exact
(Sterbenz), and two below a half never reach it. */
inline bool reachesOne(double a, double b) {
	if (a >= 0.5)
		return b >= 1.0 - a;
	if (b >= 0.5)
		return a >= 1.0 - b;
	return false;
}
/** How many places make one cycle of `n` steps. */
inline int64_t cycleOf(int n, Dir d) {
	if (d == DIR_BOUNCE)
		return 2 * (int64_t)std::max(n, 1);
	return d == DIR_PENDULUM && n > 1 ? 2 * (int64_t)n - 2 : (int64_t)std::max(n, 1);
}
/** The step place `k` stands on, nought the first. */
inline int stepAt(int64_t k, int n, Dir d) {
	const int64_t c = cycleOf(n, d), m = wrapPlace(k, c);
	switch (d) {
		case DIR_REVERSE: return (int)wrapPlace(-m, c);
		case DIR_PENDULUM: return (int)(m < n ? m : c - m);
		case DIR_BOUNCE: return (int)(m < n ? m : c - 1 - m);
		default: return (int)m;
	}
}
/** And back: the place within the first cycle that stands on `step`, on a
pendulum the one on the way down when `down`. */
inline int64_t placeOf(int step, int n, Dir d, bool down = false) {
	switch (d) {
		case DIR_REVERSE: return wrapPlace(-(int64_t)step, cycleOf(n, d));
		case DIR_PENDULUM: return down && step > 0 && step < n - 1 ? cycleOf(n, d) - step : step;
		case DIR_BOUNCE: return down ? cycleOf(n, d) - 1 - step : step;
		default: return step;
	}
}
/** Whether place `k` is walking towards step one: always in reverse, on a
pendulum on the way back, never forward. */
inline bool goingDown(int64_t k, int n, Dir d) {
	return d == DIR_REVERSE || (d == DIR_PENDULUM && wrapPlace(k, cycleOf(n, d)) >= n - 1 && n > 1) ||
	       (d == DIR_BOUNCE && wrapPlace(k, cycleOf(n, d)) >= n);
}
/** Whether a CRV's curve is played from its end back to its start at place
`k`: walking towards step one, save a pendulum of one step, which stands
still and plays forward. */
inline bool alongBack(int64_t k, int n, Dir d) { return goingDown(k, n, d) && (d != DIR_PENDULUM || n > 1); }
