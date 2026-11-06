// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "plugin.hpp"
#include <cctype>
#include <string>
#include "Writer.hpp"
#include "Words.hpp"

/** A rate as a signed step from x1, and the ladder a drag lands on: the one
arithmetic a Clock's division and a Seq's rate share, written once so that a
sequence and a phase set alike read the same number and come round together.

A step rate: one and up is x1, x2, x3, and nought and down is /2, /3, /4, so
the whole range either side of x1 is one number a drag can cross and a jack
can sweep, and every whole number in it is reachable. There is no stop and no
backwards on it; backwards is the Seq's direction cell.

Between the whole rates stand the fractions, x3.11 or /3.000125, which the
hand sets with command held, a hundredth a step, or command and shift, a
ten-thousandth, or types to the millionth (fine enough for a tenth of a hertz of
detune): a rate is carried as a whole count of millionths of a
step rate (`RATE_ONE` to a step), x3.11 being 3110000 and /3.11, the step
rate 2 - 3.11, minus 1110000, so that a millionth is a millionth of the
number written on either side of x1. The param holds the step rate and is
made whole here, once, by rounding (`rateOfValue`); from there on it is an
integer, and a ratio of two (`ratioOfRate`), so x3.11 is 311 steps every
100 pulses with no drift.

The param is a float, whose 24 bits hold a millionth exactly only below a
step rate of 16: from x16, and from /18, whose step rate is -16, the grid is
ten millionths, five decimals, and from x128 and /130, a step rate of 128
either way, a hundred, four decimals (`rateQuantum`), each still finer
against the rate itself than a millionth of x1. */

namespace rig {

/** Millionths to a step rate: a rate is a whole count of them. */
const int RATE_ONE = 1000000;
/** The decimals a rate is written and typed to, six, a millionth. */
const int RATE_DECIMALS = 6;
/** Where the param's float stops holding a millionth: a step rate of 16. Below
it a float's step is under half a millionth (2^-20 from 8 to 16), so the
nearest millionth to the float is the millionth set; from it a float's step
reaches 2^-17, nearly eight millionths below 128, and only ten millionths are
kept apart. */
const int RATE_FINE_UNDER = 16 * RATE_ONE;
/** And where it stops holding ten: a step rate of 128, from which a float's
step is 2^-16, over fifteen millionths at 255, so a hundred are kept apart,
the nearest float to each within eight of it. */
const int RATE_COARSE_FROM = 128 * RATE_ONE;
/** The grid a rate stands on, in millionths: one under a step rate of 16
either way, ten from there, a hundred from 128. */
inline int rateQuantum(int h) {
	const int a = std::abs(h);
	return a < RATE_FINE_UNDER ? 1 : a < RATE_COARSE_FROM ? 10 : 100;
}
/** The one ladder every rate stands on, a Clock's division and a Seq's rate
alike: /255 to x255, every whole rate between them a rung (a count runs on
past its cells' decimal in hex, x100 to xFF, up to the most two cells hold). The step rate
of /255 is 2 - 255. */
const int RATE_MAX = 255, RATE_MIN = 2 - RATE_MAX;
/** A rate set onto its grid, the nearest point of it, a half going up the
step rate: what typing a millionth the float cannot hold leaves. Integers. */
inline int snapRate(int h) {
	const int q = rateQuantum(h);
	if (q == 1)
		return h;
	const int r = ((h % q) + q) % q;
	return r * 2 >= q ? h - r + q : h - r;
}
/** A whole step rate, x3 or /3, as a rate in millionths. */
inline int rateOfStep(int n) { return n * RATE_ONE; }
/** The number written after the sign, in millionths: 3110000 for x3.11 and
for /3.11 alike. */
inline int rateMagnitude(int h) { return h >= RATE_ONE ? h : 2 * RATE_ONE - h; }
/** The whole step rate a rate stands on, its fraction dropped toward x1:
x3.11 stands on x3, /3.11 on /3, and /1.5 on x1. */
inline int wholeStepOf(int h) {
	const int whole = rateMagnitude(h) / RATE_ONE;
	return h >= RATE_ONE ? whole : 2 - whole;
}
/** Whether a rate has a fraction, and so is free whatever its mode says
(`RateMode`). */
inline bool rateHasFraction(int h) { return rateMagnitude(h) % RATE_ONE != 0; }
/** The whole rate nearest a rate, by the number written after its sign, a
half going away from x1: x2.05 is x2, x2.5 x3, /3.6 /4, /1.5 /2. Integers. */
inline int nearestWholeRate(int h) {
	const int w = (rateMagnitude(h) + RATE_ONE / 2) / RATE_ONE;
	return rateOfStep(h >= RATE_ONE ? w : 2 - w);
}

/** How a rate is written: x4, /3, x1, and with its fraction x3.11, /3.1,
x1.000123, every decimal it has and no trailing nought; a whole rate that is
free (`RateMode`) with its point and nothing after it, x2., which is also how
it is typed back to stay free (`typedFree`). */
inline std::string rateText(int h, bool free = false) {
	const int m = rateMagnitude(h);
	std::string t = string::f("%s%d", h >= RATE_ONE ? "x" : "/", m / RATE_ONE);
	int frac = m % RATE_ONE, digits = RATE_DECIMALS;
	if (frac != 0) {
		while (frac % 10 == 0) {
			frac /= 10;
			digits--;
		}
		t += string::f(".%0*d", digits, frac);
	}
	else if (free)
		t += ".";
	return t;
}
/** How a whole step rate is written: x4, /3, x1. */
inline std::string stepText(int n) { return rateText(rateOfStep(n)); }
/** A rate typed as `rateText` writes it, read back into millionths: "x3",
"/3", "x3.11", "/3.5", "x1.000123", and "1/3" for /3, in either case and with
any spacing, never below one and held at the ladder's ends, x255 and /255.
Decimals past the sixth round it to the nearest millionth, half up, and a
millionth the param cannot hold to its grid (`snapRate`), ten from x16 and
/18, a hundred from x128 and /130; `rounded` says
whether either happened. False, leaving `h` alone, for anything else: a bare
number goes on to Rack's parse as the step rate itself ("3" is x3, "-1" /3),
and any other text leaves the rate where it was. Read as digits, never through
a float, so x3.11 is 3110000 exactly. */
inline bool readRateText(const std::string& text, int& h, bool* rounded = nullptr) {
	std::string t;
	for (char c : text)
		if (!std::isspace((unsigned char)c))
			t += (char)std::tolower((unsigned char)c);
	size_t i;
	bool divide;
	if (t.compare(0, 1, "x") == 0)
		i = 1, divide = false;
	else if (t.compare(0, 1, "/") == 0)
		i = 1, divide = true;
	else if (t.compare(0, 2, "1/") == 0)
		i = 2, divide = true;
	else
		return false;
	int64_t whole = 0, frac = 0;
	int wholeDigits = 0, fracDigits = 0;
	bool point = false, past = false, up = false; // digits past the sixth, and whether they round up
	int pastFirst = -1;
	for (; i < t.size(); i++) {
		const char c = t[i];
		if (c == '.' && !point) {
			point = true;
			continue;
		}
		if (!std::isdigit((unsigned char)c))
			return false;
		if (point) {
			if (fracDigits < RATE_DECIMALS) {
				fracDigits++;
				frac = frac * 10 + (c - '0');
			}
			else {
				if (pastFirst < 0)
					pastFirst = c - '0';
				past = past || c != '0';
			}
		}
		else {
			if (++wholeDigits > 4)
				return false;
			whole = whole * 10 + (c - '0');
		}
	}
	if (wholeDigits == 0)
		return false;
	for (int k = fracDigits; k < RATE_DECIMALS; k++)
		frac *= 10;
	up = pastFirst >= 5;
	int64_t m = whole * RATE_ONE + frac + (up ? 1 : 0);
	if (m < RATE_ONE)
		return false;
	m = std::min(m, (int64_t)rateOfStep(RATE_MAX));
	const int set = divide ? 2 * RATE_ONE - (int)m : (int)m;
	h = snapRate(set);
	if (rounded)
		*rounded = past || h != set;
	return true;
}

/** Whether text typed into a rate leaves it free (`RateMode`): the text
decides, a point in it being free, "x2." or "2.0", and none whole, "x2" or
"2", whatever the rate it reads as; a rate read with a fraction is free
however it was written: typing a number that is not whole into a rate is
what switches its modulation law. */
inline bool typedFree(const std::string& text, int h) {
	return text.find('.') != std::string::npos || rateHasFraction(h);
}

/** One rung of a ladder from where `n` stands, `k` rungs along, either way.
Standing between rungs, a step takes the next one along rather than the one
after it, so a drag from x63 goes to x64 and not past it. */
inline int rung(const int* rungs, int count, int n, int k) {
	int i = 0;
	while (i < count - 1 && rungs[i] < n)
		i++;
	if (k > 0)
		i += (rungs[i] == n) ? k : (k - 1);
	else if (k < 0)
		i += k;
	return rungs[clamp(i, 0, count - 1)];
}

/** The step rates a beat division nearly always wants: the powers of two
from /128 to x128, and the ladder's ends past them, /255 and x255, so a
sweep reaches the end and comes back by the same rungs: a following Clock's
V/OCT jack steps its division along them, a volt a rung. A shift stroke on a
rate's screen does not land on them: it doubles and halves the rate as it
stands (`rateScaled`). */
const int POW2_RUNGS[] = {RATE_MIN, -126, -62, -30, -14, -6, -2, 0, 1, 2, 4, 8, 16, 32, 64, 128, RATE_MAX};
const int NPOW2_RUNGS = 17;
inline int pow2Rung(int n, int k) { return rung(POW2_RUNGS, NPOW2_RUNGS, n, k); }
/** A rate in millionths doubled, or halved, as it stands: x3 to x6 and x1.5, /3 to /1.5 and /6, x1 to
x2 and /2, x3.11 to x6.22. The rate is a ratio of two whole numbers, p over
q, so the doubling is p times two or q times two, and the result is put back
in millionths on the side of x1 it falls, rounded half up, then onto the
param's grid (`snapRate`). A doubling or halving that would pass the
ladder's end is not made, the rate standing where it is: a stroke past the
end keeps no travel (`landStep`) and stays on its own chain, so x96 goes up
to x192 and no further, and comes back down by x96 and x48, never from a
clamped x255 to x127.5. Exact wherever the result is on the grid, every
doubling of a whole rate and every halving of an even one among them;
rounded only where it is not, x1.5 halved being /1.333333, so a halving and
a doubling may not quite come back. */
inline int rateScaled(int h, bool up) {
	const int64_t one = RATE_ONE;
	const int64_t m = rateMagnitude(h);
	int64_t p = h >= RATE_ONE ? m : one, q = h >= RATE_ONE ? one : m;
	if (up)
		p *= 2;
	else
		q *= 2;
	// The nearest millionth of p/q, or of q/p on the divider's side.
	const int64_t a = p >= q ? p : q, b = p >= q ? q : p;
	const int64_t mag = (2 * a * one + b) / (2 * b);
	const int64_t set = p >= q ? mag : 2 * one - mag;
	if (set < (int64_t)RATE_MIN * one || set > (int64_t)RATE_MAX * one)
		return h;
	return snapRate((int)set);
}

/** Reads the window, how long after a pulse its decision stays open for a
late reset or rate to bind to it (Writer.hpp), from `path`, the window.json beside plugin.json, which says in
plain words what it is: once at the plugin's init and once by a headless test. A
file missing, unreadable or without the number leaves the default, and the
number is clamped where it is read (`clampWindowMs`, Writer.hpp). */
void loadWindowMs(const std::string& path);

/** A rate in millionths as a ratio of whole numbers in lowest terms, `n`
steps every `d` pulses: the writer's journey (Writer.hpp) and a Clock's ticks
a beat. x2 is 2 over 1, /3 1 over 3, x3.11 311 over 100 and /3.11 100 over
311, x1.000001 1000001 over 1000000, so a fractional rate is a ratio like any
other and nothing drifts. Each stays under 256 million, the most a rate's
magnitude holds; a Clock's `ratioIn` takes the denominator times 24 pulses a
beat in 64 bits and is back under an int once in lowest terms (at most three
times the magnitude, since a millionth's six twos meet 24's three), and every
product of a ratio with a count of ticks, beats or places is made in 64 bits
or wider (`inLine`, Writer.hpp, in 128). */
inline void ratioOfRate(int h, int& n, int& d) {
	n = h >= RATE_ONE ? h : RATE_ONE;
	d = h >= RATE_ONE ? RATE_ONE : rateMagnitude(h);
	int a = n, b = d;
	while (b) {
		const int t = a % b;
		a = b;
		b = t;
	}
	n /= a;
	d /= a;
}
/** A whole step rate, x2 or /3, as the same ratio. */
inline void ratioOf(int stepRate, int& n, int& d) { ratioOfRate(rateOfStep(stepRate), n, d); }

/** What a RATE jack says on its hover in a whole rate's mode, a Clock's and
a Seq's alike: a volt an octave around the screen's rate, snapped to the
nearest whole rate (`wholeSweep`). */
const char* const RATE_JACK_TIP =
    "+1 V doubles the rate, -1 V halves it, landing on the nearest step.";

/** And what it says in a free rate's mode (`RateMode`), its pie showing, where
it is named FM, on the panel and in its hover, as the Seq's IN is named by
what it walks. */
const char* const RATE_JACK_FREE_TIP =
    "+1 V doubles the rate, -1 V halves it, smoothly.";
/** The jack's name in its hover and its caption on the panel, by the mode:
the hover's the caption said out, never the caption again. */
inline const char* rateJackName(bool free) { return free ? "Frequency modulation" : "Rate"; }
inline const char* rateJackCaption(bool free) { return free ? "FM" : "RATE"; }
/** What a hand cannot guess about a rate's LCD (Words.hpp, `Gesture`): the
pie beside its cells first, a click on it turning the mode over and a drag
on it moving the fraction between whole rates; then the shift stroke's
octaves and command's finer steps, on the cells and on the pie alike; what
each mode does to the rate itself is the lesson, and what the jack does in
each is the jack's own hover, never this one. */
inline const Gesture G_RATE_MODE = declare(On::Click, Mod::None, Rank::Invented, "Click the pie: free rate");
inline const Gesture G_RATE_PIE = declare(On::Drag, Mod::None, Rank::Invented, "Drag the pie: between whole rates");
inline const Gesture G_RATE_OCTAVES = declare(On::Drag, Mod::Shift, Rank::Known, RACK_MOD_SHIFT_NAME "-drag: rate by octaves");
inline const Gesture G_RATE_FINE = declare(On::Drag, Mod::Command, Rank::Known, RACK_MOD_CTRL_NAME "-drag: fine");
inline const Gesture G_RATE_FINER =
    declare(On::Drag, Mod::ShiftCommand, Rank::Rare, RACK_MOD_SHIFT_NAME "+" RACK_MOD_CTRL_NAME "-drag: finer");
/** The gestures a rate's screen says. */
inline Gestures rateGestures() { return {&G_RATE_MODE, &G_RATE_PIE, &G_RATE_OCTAVES, &G_RATE_FINE, &G_RATE_FINER}; }
/** When a change of the rate is heard, the one thing its screen cannot
say: the mode itself the screen shows, by its pie (`pieMark`), whose click
turns it over (`G_RATE_MODE`); a whole rate's change lands on the next
`beat`, a Clock in hertz naming its cycle. */
inline std::string rateModeLesson(bool free, const char* beat = "beat") {
	return free ? std::string("Each change is heard at once.")
	            : std::string("Each change lands on the next ") + beat + ".";
}

/** A rate as a ratio, the speed it multiplies by: x3 is 3, /4 a quarter. A
double, for the one law that is continuous (`octaveSweep`). */
inline double rateRatio(int h) {
	return h >= RATE_ONE ? (double)h / RATE_ONE : (double)RATE_ONE / (double)rateMagnitude(h);
}
/** A ratio back as a rate in millionths, held to the ladder's ends, x255 and
/255: the nearest millionth of the number written after the sign, rounded
once, here, so what the writer and a Clock's ticks are given is a ratio of
whole numbers as every rate is (`ratioOfRate`). A millionth of a rate is
under a five-hundredth of a cent, far finer than the ear, so the rounding
is where a continuous quantity meets the integer it is carried as, not a
tolerance. */
inline int rateOfRatio(double r) {
	const double top = (double)RATE_MAX;
	r = std::min(std::max(r, 1.0 / top), top);
	if (r >= 1.0)
		return (int)std::llround(r * RATE_ONE);
	const int m = (int)std::llround((double)RATE_ONE / r);
	return 2 * RATE_ONE - m;
}
/** A free rate's jack: one volt an octave around the screen's rate, the
rate times two to the volts, continuous both ways, so +1 V doubles it, -1 V
halves it and a twelfth of a volt is a semitone; held to x255 and /255. */
inline int octaveSweep(int h, float volts) {
	return rateOfRatio(rateRatio(h) * std::exp2((double)clamp(volts, -10.f, 10.f)));
}

/** A whole rate's jack: one volt an octave around the screen's whole step
rate `from`, the rate times two to the volts snapped to the nearest rung of
the ladder, every whole rung from /255 to x255, nearest in octaves; held at the
ladder's ends; the step rate it reaches. From x1 one volt is x2, log2 3, about
1.585 volts, x3, and the boundary between them log2 of root six, about 1.292
volts; whole volts from a power of two are whole octaves.

The boundary between rungs a and b stands at their geometric mean, so the
target t = r 2^V is past it when t^2 > ab, every ratio a fraction of whole
numbers under 256: compared as 2^2V p0^2 qa qb against pa pb q0^2, products
under 2^32 times a power of two, exact in a double wherever a tie can fall,
which is only where 2V is whole. A tie, exactly half-way in octaves (x1 at
+0.5 V, between x1 and x2; x6 at +0.5 V, between x8 and x9), goes away from
the screen's rate, the way the volts point, as the doubling's rounding of a
half volt did before it; no epsilon. The first guess is the rung nearest by
the ratio, and the walk from it a rung at most. */
inline int wholeSweep(int from, float volts) {
	const double v = (double)clamp(volts, -10.f, 10.f);
	if (v == 0.0)
		return from;
	int p0, q0;
	ratioOf(from, p0, q0);
	const double e = std::exp2(2.0 * v);
	const double t = (double)p0 / (double)q0 * std::exp2(v);
	int s = t >= 1.0 ? (int)std::lround(std::min(t, (double)RATE_MAX))
	                 : 2 - (int)std::lround(std::min(1.0 / t, (double)RATE_MAX));
	// Past the boundary between rung `lo` and the one above it; standing on
	// it counts as past when the volts point up.
	const auto past = [&](int lo) {
		int pa, qa, pb, qb;
		ratioOf(lo, pa, qa);
		ratioOf(lo + 1, pb, qb);
		const double l = e * (double)(p0 * p0) * (double)(qa * qb);
		const double r = (double)(pa * pb) * (double)(q0 * q0);
		return v > 0.0 ? l >= r : l > r;
	};
	while (s < RATE_MAX && past(s))
		s++;
	while (s > RATE_MIN && !past(s - 1))
		s--;
	return s;
}

struct RateIn {
	int set = RATE_ONE;   // the screen's rate, in millionths
	bool free = false;    // its mode (`RateMode`): free, the jack a volt an octave
	bool patched = false; // a cable in the jack that sweeps it
	float cv = 0.f;       // and the volts at that jack
	/** Whether the jack follows the free law: the mode free, or a rate with
	a fraction, which is free whatever its mode says, since its pie shows. */
	bool isFree() const { return free || rateHasFraction(set); }
	/** The rate in force, in millionths. The jack is a volt an octave
	around the screen's rate in both modes, so nought volts is the screen's
	rate: free, continuous (`octaveSweep`), fraction and all; whole, snapped
	to the nearest whole rung (`wholeSweep`). */
	int rate() const {
		if (!patched)
			return set;
		if (isFree())
			return octaveSweep(set, cv);
		return rateOfStep(wholeSweep(wholeStepOf(set), cv));
	}
};

/** A rate's mode, whole or free, a Clock's division and a Seq's rate
alike, each module holding one beside its rate param: the pie beside the number
says the rate is modulated as a continuous value, and how far it stands past
the whole rate written (`pieMark`). Whole,
the RATE jack sweeps whole rates (`RateIn`); free, the screen's pie lit, it
is a volt an octave around the rate (`octaveSweep`). A rate with a fraction
is free whatever this says (`RateIn::isFree`), so the flag decides only for
a whole rate, x2 against x2.; it is set by the hand, typed (`typedFree`),
a click on its pie, or a stroke that lands off a whole rate (`RateScreen`),
saved with the module (`rateModeToJson`) and held by a RIG's scene as a whole
setting. An atomic, since the UI sets it and a scene's recall on the audio
thread does too, and the engine reads it where it reads the rate. */
struct RateMode {
	std::atomic<bool> rateFree{false};
	virtual ~RateMode() {}
	bool rateFreeSet() const { return rateFree.load(std::memory_order_relaxed); }
	void setRateFree(bool f) { rateFree.store(f, std::memory_order_relaxed); }
	void rateModeToJson(json_t* rootJ) { json_object_set_new(rootJ, "rateFree", json_boolean(rateFreeSet())); }
	void rateModeFromJson(json_t* rootJ) {
		json_t* f = json_object_get(rootJ, "rateFree");
		setRateFree(f && json_boolean_value(f));
	}
};
/** The rate mode a module holds, or none. */
inline RateMode* rateModeOf(Module* m) { return dynamic_cast<RateMode*>(m); }
/** Whether rate `h` of module `m` is free: its mode, or a fraction. */
inline bool rateFreeOf(Module* m, int h) {
	RateMode* rm = m ? rateModeOf(m) : nullptr;
	return rateHasFraction(h) || (rm && rm->rateFreeSet());
}

/** A rate param's value as the ladder holds it, in millionths: the step
rate's float made whole once, here, by rounding it to the nearest point of
its grid (`rateQuantum`), and held to the ladder's ends. A float set from a
rate on the grid (`valueOfRate`) comes back as that rate, every one of them:
under 16 the float is within a quarter millionth of it, from 16 within four
millionths of a grid ten wide, from 128 within eight of a grid a hundred
wide. */
inline int rateOfValue(float v) {
	// Held to the ladder before rounding, which is the same since both ends
	// stand on the grid, so the millionths always fit an int.
	const double x = std::min(std::max((double)v, (double)RATE_MIN), (double)RATE_MAX);
	const int q = rateQuantum((int)std::lround(x * RATE_ONE));
	return (int)std::lround(x * (RATE_ONE / q)) * q;
}
/** And a rate in millionths as the param holds it, a step rate: the nearest
float, divided in double so it is rounded once. */
inline float valueOfRate(int h) { return (float)((double)h / (double)RATE_ONE); }
/** A rate param as the ladder holds it, in millionths: a Clock's division,
a Seq's rate. */
inline int rateParamOf(Module* m, int paramId) { return rateOfValue(m->params[paramId].getValue()); }
/** A module's rate as its own controls say: the screen's rate and the RATE
jack that sweeps it, a Clock's and a Seq's alike. Read on every sample, so the
module is its own type, a Module that is a RateMode, and its mode is reached
with no cast at run time (`rateModeOf`'s cross-cast cost 60 ns a call). */
template <class M>
inline RateIn rateInOf(M* m, int paramId, int inputId) {
	RateIn r;
	r.set = rateParamOf(m, paramId);
	r.free = static_cast<RateMode*>(m)->rateFreeSet();
	r.patched = m->inputs[inputId].isConnected();
	r.cv = m->inputs[inputId].getVoltage();
	return r;
}

/** A rate's param as its tooltip reads it, x4, /3 or x1.000123, where "-2"
would say nothing, and as it is typed back: a Clock's division, a Seq's rate.
Not snapped, since the fractions stand between the whole rates; a value is
made whole in millionths where it is read (`rateOfValue`), and set the same
way, so the param never holds a rate off its grid. A rate typed finer than
the grid is rounded and shown as it now stands, unsaid otherwise: a typed
number's rounding is Rack's convention. */
struct StepRateQuantity : HintQuantity {
	/** A typed change of mode (`RateMode`), for the screen to record as
	one step of undo with the change Rack records of the value
	(`RateScreen::step`): what the mode was before, while one is owed. */
	int typedModeWas = -1;
	/** The rate a jack has put in force where it differs from the setting,
	and which jack: said after the value, ", x8 from the RATE jack", since
	no screen draws it (the screen keeps the setting, the jack's bar beside
	it) and a value finer than the screen shows is the instant tier's. Written by the module's retip on the
	UI thread, empty while no jack moves the rate. */
	std::string inForce;
	std::string getString() override { return HintQuantity::getString() + inForce; }
	std::string getDisplayValueString() override {
		const int h = rateOfValue(getValue());
		return rateText(h, rateFreeOf(module, h));
	}
	void setDisplayValueString(std::string text) override {
		int h;
		RateMode* mode = module ? rateModeOf(module) : nullptr;
		const bool was = mode && mode->rateFreeSet();
		if (readRateText(text, h)) {
			h = clamp(h, rateOfStep(RATE_MIN), rateOfStep(RATE_MAX));
			setValue(valueOfRate(h));
		}
		else if (text.find_first_of("xX/") == std::string::npos) { // a bare number, the step rate
			ParamQuantity::setDisplayValueString(text);
			setValue(valueOfRate(rateOfValue(getValue())));
			if (text.find_first_of("0123456789") == std::string::npos)
				return; // no number in it: nothing typed that names a mode
		}
		else
			return; // written as a rate and not one, x0 or 2/3: the rate stays where it
			        // was, where Rack would read 2/3 as a number and round it to x1
		// The text decides the mode: a point in it free, none whole.
		if (mode) {
			const bool free = typedFree(text, rateOfValue(getValue()));
			if (free != was) {
				mode->setRateFree(free);
				if (typedModeWas < 0)
					typedModeWas = was ? 1 : 0;
			}
		}
	}
	/** Rack's randomize lands on the whole rates, as it did while the param
	was snapped: the hundredths are a hand's choice. */
	void randomize() override {
		if (!isBounded())
			return;
		setValue((float)clamp((int)std::lround(getMinValue() + random::uniform() * (getMaxValue() - getMinValue())),
		                      RATE_MIN, RATE_MAX));
	}
};
/** What a rate's hover says after its value (`StepRateQuantity::inForce`):
the rate `now` in force and the jack that put it there, `from` ("the RATE
jack"), where it is not the setting; nothing where it is. */
inline void retipRateInForce(Module* m, int paramId, int now, const std::string& from) {
	StepRateQuantity* q = m ? dynamic_cast<StepRateQuantity*>(m->paramQuantities[paramId]) : nullptr;
	if (!q)
		return;
	const std::string f = now != rateParamOf(m, paramId) ? ", " + rateText(now) + " from " + from : std::string();
	if (q->inForce != f)
		q->inForce = f;
}

/** Where a RATE jack's bar stands on its screen, `cv` its volts over ten and
`set` the whole step rate the screen stands on (`wholeStepOf`): how many
octaves the jack moves the rate, on a scale of six to either end, x64 from
x1. Free (`free`), the volts themselves, moving as smoothly as the rate does;
whole, the octaves to the rung the jack has reached (`wholeSweep`), so the bar
moves in the jumps the rate does, a whole volt from a power of two standing it
where the free bar would. Drawn, never counted, so a log in floats. */
inline float rateBar(int set, float cv, bool free = false) {
	const float volts = clamp(cv, -1.f, 1.f) * 10.f;
	if (free)
		return clamp(volts / 6.f, -1.f, 1.f);
	const int reached = wholeSweep(set, volts);
	const float octaves = (float)std::log2(rateRatio(rateOfStep(reached)) / rateRatio(rateOfStep(set)));
	return clamp(octaves / 6.f, -1.f, 1.f);
}

} // namespace rig
