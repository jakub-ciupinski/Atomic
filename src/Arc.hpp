// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "plugin.hpp"
#include <cmath>

/** The curve: a CRV's arc as numbers, depending on nothing but arithmetic, so
a CRV's jack and its screen read one formula. */

namespace rig {

/** The most arcs a block has, the longest block of CRVs: the working
arrays below are sized by it, and a knot more for the run's closing end. */
const int BLOCK_ARCS = 128;

/** The tension a spline arc's shape gives in an open run of two arcs or
more, signed: nought at nought, the elastic rod, the cubic spline; tight
below nought, pulled toward the straight line between its points, `p` running
from nought to infinity at minus ten, which is the straight line itself
(`TAUT`), half-way there at minus five; loose above nought, the rod let swing
wider, `q` from nought to two and a half at ten, short of pi where the loose
spline has no solution, and where it swings past random points in [-1, 1] by
at most about one and a half. The tension is `p * p` and the looseness
`-q * q`, one number, so one formula below covers both. */
inline double tensionOf(float c) {
	if (c < 0.f) {
		const double u = -(double)c / 10.0;
		const double p = 10.0 * u / (1.0 - u);
		return p * p;
	}
	const double q = (double)c / 4.0;
	return -q * q;
}
/** The tension where a sine is possible, centred on the sine's own
looseness `qs` rather than on the rod: a loose spline's arc is made of 1, x,
cos(qx) and sin(qx), so with `q` the phase an arc spans it is a sine exactly.
Below nought tightened from there as `tensionOf` tightens the rod, so minus
ten is still the straight line and the rod is passed on the way; above it
`q` runs on in a straight line to `qmax` at ten. */
inline double sineTensionOf(float c, double qs, double qmax) {
	if (c < 0.f)
		return tensionOf(c) - qs * qs;
	const double q = qs + (double)c / 10.0 * (qmax - qs);
	return -q * q;
}
/** A ring of `n` arcs, three or more: the sine's looseness, one turn over
the ring, and the end of its knob, which is the rod's two and a half as the
ring grows long. */
inline double ringSineQ(int n) { return 2.0 * M_PI / n; }
inline double ringLooseQ(int n) {
	const double qs = ringSineQ(n);
	return qs + 2.5 * (M_PI - qs) / M_PI;
}
/** An arc flat at both ends, a lone spline CRV or either arc of a ring of
two: its sine is the half cosine, `q` pi, and its knob runs on to 1.8 pi,
where it lingers at its points and steepens through its middle toward a
square, never leaving them below 2 pi. */
const double FLAT_SINE_Q = M_PI, FLAT_LOOSE_Q = 1.8 * M_PI;
/** How much of a joined bend's slope a spline run takes at the shared point,
a plain scale: one, the bend's slope whole, at either end of the run, for a bend
above its line or below it. No cap and no easing: no hard limit is set until
one proves excessive, and this is the one number to turn if it does. */
const double TAKEN_SLOPE = 1.0;
/** The shape at which a spline arc is the straight line exactly, the end of
its tension. */
const float TAUT = -10.f;
/** The two weights a knot's equation gives its arc's bends, the bend at the
far knot and the bend at the near one, six times the spline's own so the
cubic's are exactly one and two, for the signed tension `s`.
Near nought the closed forms lose themselves to cancellation, so below a
millionth they are the series, whose next term, of order `s * s`, is under a
float's last digit there: this is the one place in the curve that is not
exact by construction, and cannot be. */
inline void bendWeights(double s, double& A, double& B) {
	if (std::fabs(s) < 1e-6) {
		A = 1.0 - 7.0 * s / 60.0;
		B = 2.0 - 2.0 * s / 15.0;
		return;
	}
	if (s > 0.0) {
		const double p = std::sqrt(s), e = -std::expm1(-2.0 * p); // 1 - e^-2p, no overflow however tight
		const double csch = 2.0 * std::exp(-p) / e, coth = (2.0 - e) / e;
		A = 6.0 * (1.0 / s - csch / p);
		B = 6.0 * (coth / p - 1.0 / s);
		return;
	}
	const double q = std::sqrt(-s);
	A = 6.0 * (1.0 / (q * std::sin(q)) + 1.0 / s);
	B = 6.0 * (-1.0 / s - 1.0 / (q * std::tan(q)));
}
/** What a bend at a knot adds to its arc at `x` from it, nought to one,
measured from the far end: nought at both knots, and in the cubic
`(x^3 - x) / 6`. */
inline double bendShape(double s, double x) {
	if (std::fabs(s) < 1e-6)
		return (x * x * x - x) / 6.0 + s * x * (x * x - 1.0) * (3.0 * x * x - 7.0) / 360.0;
	if (s > 0.0) {
		const double p = std::sqrt(s);
		// sinh(px) / sinh(p), in exponentials that never overflow.
		const double r = std::exp(p * (x - 1.0)) * (-std::expm1(-2.0 * p * x)) / (-std::expm1(-2.0 * p));
		return (r - x) / s;
	}
	const double q = std::sqrt(-s);
	return (x - std::sin(q * x) / std::sin(q)) / -s;
}

/** The flat-ended arc of signed tension `s`, nought to one over nought to
one, in closed form: odd about its middle, 1/2 + a v + b sin(q v) with
v = x - 1/2 (sinh for a tension), its two weights fixed by reaching one half
flat at v = 1/2. At `q` pi it is the half cosine (1 - cos(pi x)) / 2, where
the run's system (`splineBends`) is singular; the straight line as the
tension grows without end; near nought the cubic's smoothstep through the
bends, as `bendWeights` takes it there. */
inline double flatStep(double s, double x) {
	if (std::fabs(s) < 1e-6) {
		double A, B;
		bendWeights(s, A, B);
		return x + 6.0 / (B - A) * (bendShape(s, 1.0 - x) - bendShape(s, x));
	}
	const double v = x - 0.5;
	if (s < 0.0) {
		const double q = std::sqrt(-s), h = q * std::cos(0.5 * q);
		return 0.5 + (std::sin(q * v) - h * v) / (2.0 * std::sin(0.5 * q) - h);
	}
	// Over cosh(p / 2), in exponentials that never overflow however tight.
	const double p = std::sqrt(s);
	const double S = (std::exp(p * (v - 0.5)) - std::exp(-p * (v + 0.5))) / (1.0 + std::exp(-p));
	return 0.5 + (S - p * v) / (2.0 * std::tanh(0.5 * p) - p);
}

/** The arc of signed tension `s` that is nought at both ends, leaves at a
slope of one and arrives flat, over nought to one, in closed form: what a
slope taken at an arc's start adds to it, and mirrored, `-leaveStep(s, 1 - x)`,
what a slope taken at its end adds. Even and odd about its middle, each part
of 1, v, cos(q v) and sin(q v) (cosh and sinh for a tension), so it stands at
`q` pi, where the bends below are singular, and only at 2 pi, past the knob's
1.8 pi, does it fail; near nought from the bends, as `flatStep` takes it. */
inline double leaveStep(double s, double x) {
	if (std::fabs(s) < 1e-6) {
		double A, B;
		bendWeights(s, A, B);
		return 6.0 * (A * bendShape(s, x) - B * bendShape(s, 1.0 - x)) / (B * B - A * A);
	}
	const double v = x - 0.5;
	if (s < 0.0) {
		const double q = std::sqrt(-s), sh = std::sin(0.5 * q), ch = std::cos(0.5 * q);
		return (std::cos(q * v) - ch) / (2.0 * q * sh) + (std::sin(q * v) - 2.0 * v * sh) / (2.0 * (q * ch - 2.0 * sh));
	}
	const double p = std::sqrt(s);
	if (p < 1.0) { // in sinh, whose differences near nought keep their digits
		const double sh = std::sinh(0.5 * p);
		return std::sinh(0.5 * p * (0.5 + v)) * std::sinh(0.5 * p * (0.5 - v)) / (p * sh) +
		       (std::sinh(p * v) - 2.0 * v * sh) / (2.0 * (p * std::cosh(0.5 * p) - 2.0 * sh));
	}
	// Over sinh and cosh of p / 2, in exponentials that never overflow.
	const double em = std::exp(-p), hi = std::exp(p * (v - 0.5)), lo = std::exp(-p * (v + 0.5));
	const double th = std::tanh(0.5 * p);
	return ((1.0 + em) - hi - lo) / (2.0 * p * -std::expm1(-p)) + ((hi - lo) / (1.0 + em) - 2.0 * v * th) / (2.0 * (p - 2.0 * th));
}

/** The bends of one run of spline arcs, `m` knots `k` and the weights of
each arc between them, `A` and `B`; a ring of `m` arcs when `ring`, else
`m - 1` arcs whose first end leaves at the slope `s0` and whose last arrives
at `s1`, flat where nothing gives them one. Solved outright, as a
three-banded system (a ring's corners by Sherman and Morrison), in double:
the loose arcs' weights come near each other where a sweep would take a
hundred passes. A straight arc weighs nothing and couples nothing, so a knot
between two has no bend and its arcs' sides of the system come apart there.
A ring is of three arcs or more: a ring of two is flat-ended (`flatStep`). */
inline void splineBends(const float* k, const double* A, const double* B, int m, bool ring, double* M, double s0 = 0.0,
                        double s1 = 0.0) {
	double lo[BLOCK_ARCS + 1], di[BLOCK_ARCS + 1], up[BLOCK_ARCS + 1], r[BLOCK_ARCS + 1];
	const int segs = ring ? m : m - 1;
	for (int i = 0; i < m; i++) {
		const bool hasL = ring || i > 0, hasR = ring || i < m - 1;
		const int L = (i + segs - 1) % segs, R = i % segs;
		const double dL = hasL ? (double)k[i] - (double)k[(i + m - 1) % m] : 0.0;
		const double dR = hasR ? (double)k[(i + 1) % m] - (double)k[i] : 0.0;
		lo[i] = hasL ? A[L] : 0.0;
		up[i] = hasR ? A[R] : 0.0;
		di[i] = (hasL ? B[L] : 0.0) + (hasR ? B[R] : 0.0);
		r[i] = 6.0 * ((hasR ? dR : s1) - (hasL ? dL : s0));
		M[i] = 0.0;
	}
	// Thomas's sweep over rows `from` to `to`, the corners left out; a row
	// weighing nothing, between two straight arcs, has no bend.
	auto band = [&](const double* d, const double* rr, double* x, int n) {
		double c2[BLOCK_ARCS + 1], r2[BLOCK_ARCS + 1];
		for (int i = 0; i < n; i++) {
			const double den = d[i] - (i > 0 ? lo[i] * c2[i - 1] : 0.0);
			if (den == 0.0) {
				c2[i] = 0.0;
				r2[i] = 0.0;
				continue;
			}
			c2[i] = (i < n - 1 ? up[i] : 0.0) / den;
			r2[i] = (rr[i] - (i > 0 ? lo[i] * r2[i - 1] : 0.0)) / den;
		}
		for (int i = n - 1; i >= 0; i--)
			x[i] = r2[i] - (i < n - 1 ? c2[i] * x[i + 1] : 0.0);
	};
	if (!ring) {
		band(di, r, M, m);
		return;
	}
	const double corner0 = lo[0], cornerN = up[m - 1]; // row 0's to the last knot, the last row's to the first
	if (corner0 == 0.0 && cornerN == 0.0) {
		band(di, r, M, m);
		return;
	}
	const double gamma = di[0] == 0.0 ? 1.0 : -di[0];
	double d2[BLOCK_ARCS + 1], u[BLOCK_ARCS + 1], z[BLOCK_ARCS + 1];
	for (int i = 0; i < m; i++) {
		d2[i] = di[i];
		u[i] = 0.0;
	}
	d2[0] -= gamma;
	d2[m - 1] -= corner0 * cornerN / gamma;
	u[0] = gamma;
	u[m - 1] = cornerN;
	band(d2, r, M, m);
	band(d2, u, z, m);
	const double f = (M[0] + corner0 * M[m - 1] / gamma) / (1.0 + z[0] + corner0 * z[m - 1] / gamma);
	for (int i = 0; i < m; i++)
		M[i] -= f * z[i];
}

/** The exponential the bend is made of, nought to one over nought to one,
`k` its steepness; the straight line at nought, exactly. */
inline float bendExp(float k, float x) { return k == 0.f ? x : std::expm1(k * x) / std::expm1(k); }

/** A CRV's arc: a start, an end, a shape and a curve type. Whether it is joined to
its neighbours is a link of its own, made by a double click on a point, and
its type, bend or spline, is turned by a double click on the curve
handle (`blockArcs`).

A bend, joined or free, is its own shape and nothing else: which side of its
straight line it bends, minus below and plus above whichever way it goes, an
exponential quick to leave and slow to settle on one side and its mirror on
the other, so the pointer dragged down bends the curve down and minus ten and
plus ten are mirror images, and at nought the straight line. A
bend takes nothing from a join but the point; a spline run joined to it takes
its slope there too.

A spline arc is a stretch of a spline in tension through the points of its
run, every spline arc joined to the next: the curve that leaves each point
bending exactly as hard as it arrived, so value, slope and bend all carry
through every join. Its shape is its tension (`tensionOf`): the elastic rod at
nought, the cubic spline, tight below toward the straight line between its
points at minus ten, loose above, swinging wider. A run that does not close on
itself leaves its first point and arrives at its last flat where nothing is
joined there, and where a bend is joined takes the bend's slope at the shared
point, whole (`TAKEN_SLOPE`, one); a run that does close, a whole block spline and
joined round, is a loop with no ends. Where a sine is possible, nought is the sine
rather than the rod (`sineTensionOf`): a ring of three or more, one turn
over the ring, exactly the sine through points sampled from one; a lone
spline arc, or a ring of two, the half cosine between its points
(`flatStep`). The spline passes
through every point and may swing a little past one, and past the window's
edge, where it is played as it is and left undrawn (`Crv::volts`, `Crv::held`
for the light alone). */
struct Arc {
	float a = 0.f, b = 0.f;         // start and end, minus one to one
	float c = 0.f;                  // the shape, minus ten to ten
	float k = 0.f;                  // the bend's steepness, nought for none
	bool up = false;                // bent above its line
	bool spline = false;            // a stretch of spline, not a bend
	bool jin = false, jout = false; // joined at its start, at its end
	float m0 = 0.f, m1 = 0.f;       // a spline arc's slopes, leaving and arriving, at nought
	float M0 = 0.f, M1 = 0.f;       // its bends at its start and end, at any other tension
	double ten = 0.0;               // a spline arc's signed tension in force, from its shape and its run
	bool flat = false;              // alone in closed form (`flatStep`): a run of one, or a ring of two
	float took0 = 0.f, took1 = 0.f; // a run of one's slopes taken from a joined bend, leaving and arriving
	bool operator==(const Arc& o) const {
		return a == o.a && b == o.b && c == o.c && spline == o.spline && jin == o.jin && jout == o.jout &&
		       m0 == o.m0 && m1 == o.m1 && M0 == o.M0 && M1 == o.M1 && ten == o.ten && flat == o.flat &&
		       took0 == o.took0 && took1 == o.took1;
	}
	bool operator!=(const Arc& o) const { return !(*this == o); }
};
/** Whether a bent arc is quick to leave its start: above its line rising,
below it falling. */
inline bool arcQuickFirst(const Arc& s) { return s.up == (s.b > s.a); }
/** A bend of steepness `k` over nought to one of the rise, quick first or slow. */
inline float bendAt(float k, bool quick, float x) {
	if (k <= 0.f)
		return x;
	return quick ? 1.f - bendExp(k, 1.f - x) : bendExp(k, x);
}
/** A bend's slope at `x`, analytic, in rises per arc: one for the straight
line, and at the steep end of a bend of steepness ten about ten. */
inline double bendSlopeAt(float k, bool quick, double x) {
	if (k <= 0.f)
		return 1.0;
	const double kk = k, u = quick ? 1.0 - x : x;
	return kk * std::exp(kk * u) / std::expm1(kk);
}
inline float hermiteAt(float a, float b, float m0, float m1, float x) {
	const float x2 = x * x, x3 = x2 * x;
	return a + (b - a) * (3.f * x2 - 2.f * x3) + m0 * (x3 - 2.f * x2 + x) + m1 * (x3 - x2);
}
inline float arcAt(const Arc& s, float x) {
	x = clamp(x, 0.f, 1.f);
	if (!s.spline)
		return s.a + (s.b - s.a) * bendAt(s.k, arcQuickFirst(s), x);
	if (s.c <= TAUT)
		return s.a + (s.b - s.a) * x;
	if (s.flat) {
		const double f = (double)s.a + ((double)s.b - (double)s.a) * flatStep(s.ten, x);
		if (s.took0 == 0.f && s.took1 == 0.f)
			return (float)f;
		return (float)(f + (double)s.took0 * leaveStep(s.ten, x) - (double)s.took1 * leaveStep(s.ten, 1.0 - x));
	}
	if (s.ten == 0.0)
		return hermiteAt(s.a, s.b, s.m0, s.m1, x);
	const double t = s.ten, u = x;
	return (float)((double)s.a * (1.0 - u) + (double)s.b * u + (double)s.M0 * bendShape(t, 1.0 - u) +
	               (double)s.M1 * bendShape(t, u));
}

/** A block's arcs from its members' starts `p`, own ends `e`, shapes `c`,
links `link` and types `spline`, `n` of them left to right, into `out`, and
whether each one's end is joined into `joined`. `link[i]` joins member i's
start to the end of the one before it, the first's to the last's, round the
block, so a joined end is the next start and its own end is out of force. */
inline void blockArcs(const float* p, const float* e, const float* c, const bool* link, const bool* spline, int n,
                      Arc* out, bool* joined) {
	// Up to BLOCK_ARCS, so a block of 128 has its 128th CRV's arcs written
	// for it to read.
	if (n < 1 || n > BLOCK_ARCS)
		return;
	auto nextOf = [&](int j) { return (j + 1) % n; };
	bool all = n > 1;
	for (int j = 0; j < n; j++) {
		joined[j] = n > 1 && link[nextOf(j)];
		all = all && joined[j] && spline[j];
	}
	for (int j = 0; j < n; j++) {
		Arc& s = out[j];
		s = Arc();
		s.a = p[j];
		s.b = joined[j] ? p[nextOf(j)] : e[j];
		s.c = clamp(c[j], -10.f, 10.f);
		s.spline = spline[j];
		s.k = s.spline ? 0.f : std::fabs(s.c);
		s.up = !s.spline && s.c > 0.f;
		s.jin = n > 1 && link[j];
		s.jout = joined[j];
	}
	// One run of spline arcs, `len` of them from `first`, round it where
	// `ring`: its knots, its bends, and each arc's share of them.
	// The slopes an open run takes at its ends from a bend joined there,
	// leaving its first point and arriving at its last, the bend's own slope
	// there (times `TAKEN_SLOPE`, one); nought, flat, where no bend is
	// joined.
	auto takenBy = [&](int first, int len, double& s0, double& s1) {
		s0 = s1 = 0.0;
		if (n < 2)
			return;
		const int before = (first + n - 1) % n, last = (first + len - 1) % n, after = nextOf(last);
		auto taken = [](double v) { return TAKEN_SLOPE * v; };
		if (link[first] && !spline[before]) {
			const Arc& w = out[before];
			s0 = taken(((double)w.b - (double)w.a) * bendSlopeAt(w.k, arcQuickFirst(w), 1.0));
		}
		if (joined[last] && !spline[after]) {
			const Arc& w = out[after];
			s1 = taken(((double)w.b - (double)w.a) * bendSlopeAt(w.k, arcQuickFirst(w), 0.0));
		}
	};
	auto runOver = [&](int first, int len, bool ring) {
		// A run of one, or a ring of two, is alone in closed form at every
		// arc: flat at its ends, the half cosine at nought, and a run of one
		// leaning into the slopes a joined bend gives it (`leaveStep`).
		if (ring ? len == 2 : len == 1) {
			for (int i = 0; i < len; i++) {
				Arc& s = out[(first + i) % n];
				s.flat = true;
				s.ten = s.c <= TAUT ? 0.0 : sineTensionOf(s.c, FLAT_SINE_Q, FLAT_LOOSE_Q);
				if (!ring && s.c > TAUT) {
					double s0, s1;
					takenBy(first, 1, s0, s1);
					s.took0 = (float)s0;
					s.took1 = (float)s1;
				}
			}
			return;
		}
		const int m = ring ? len : len + 1;
		const double qs = ring ? ringSineQ(len) : 0.0, qmax = ring ? ringLooseQ(len) : 0.0;
		float knots[BLOCK_ARCS + 1];
		double A[BLOCK_ARCS], B[BLOCK_ARCS], M[BLOCK_ARCS + 1];
		for (int i = 0; i < len; i++) {
			Arc& s = out[(first + i) % n];
			knots[i] = s.a;
			if (s.c <= TAUT)
				A[i] = B[i] = 0.0;
			else {
				s.ten = ring ? sineTensionOf(s.c, qs, qmax) : tensionOf(s.c);
				bendWeights(s.ten, A[i], B[i]);
			}
		}
		double s0 = 0.0, s1 = 0.0;
		if (!ring) {
			knots[len] = out[(first + len - 1) % n].b;
			takenBy(first, len, s0, s1);
		}
		splineBends(knots, A, B, m, ring, M, s0, s1);
		for (int i = 0; i < len; i++) {
			Arc& s = out[(first + i) % n];
			const float d = knots[(i + 1) % m] - knots[i], ma = (float)M[i], mb = (float)M[(i + 1) % m];
			s.M0 = ma;
			s.M1 = mb;
			// The rod's slopes, for an open run's arc at nought.
			const float t0 = d - (2.f * ma + mb) / 6.f, t1 = d + (ma + 2.f * mb) / 6.f;
			s.m0 = d + (t0 - d);
			s.m1 = d + (t1 - d);
		}
	};
	if (all) {
		runOver(0, n, true);
		return;
	}
	// The runs: a spline arc, and the next joined to it a spline too.
	auto carries = [&](int j) { return spline[j] && joined[j] && spline[nextOf(j)]; };
	int from = 0;
	if (n > 1)
		for (int i = 0; i < n && carries((from + n - 1) % n); i++)
			from = (from + 1) % n;
	for (int done = 0; done < n;) {
		const int first = (from + done) % n;
		if (!spline[first]) {
			done++;
			continue;
		}
		int len = 1;
		while (done + len < n && carries((first + len - 1) % n))
			len++;
		done += len;
		runOver(first, len, false);
	}
}

} // namespace rig
