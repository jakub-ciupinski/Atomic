// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "plugin.hpp"
#include "Arc.hpp"
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string>

/** Pitch: a quantizer's pattern and the arithmetic of it, all pure
functions of a Scale and nothing else: the key names, the turn and the
mirror of a keyboard, the snap, the one pitch space an offset moves notes in,
a roll's notes shared along a CV's travel, the chord a pattern sounds, and
the span of notes an Atn's gain reaches. Nothing here knows of a node or a
module, so most of it can be tested as relations between functions. */

namespace rig {

/** A CV's reach: its window is minus this to this, in volts, and a Atn's
top runs the same way. Ten volts either side of nought is the whole of what
a Rack cable carries, so a CV on its own passes anything as it is. */
const float CV_VOLTS = 10.f;

const int NOTES = 16; // notes in a chord at most, a channel each

/** A quantizer's pattern: which semitones above its `base` are in it, over
a period of whole octaves, and the same again every period up and down. One
octave is a scale, twelve pitch classes; more is a keyboard of that many
octaves, each its own, or a piano roll, which does not repeat at all. A Qnt
is one octave until its own stroke says how many. */
const int MAX_OCTAVES = 10;
struct Scale {
	int8_t octaves = 0;                 // 0: no scale at all
	uint16_t oct[MAX_OCTAVES] = {};     // twelve bits an octave, bit 0 = C
	// A piano roll's notes rather than a pattern: the one period from `base`
	// semitones up is all there is, nothing repeats, and the whole of a
	// CV's travel is shared out equally among the notes in it. See
	// `spreadNotes` and `Slots::windowNotes`.
	bool spread = false;
	// Where the pattern starts, in semitones from C4: a roll's foot, or a
	// keyboard's C moved by its transpose, so the white keys at +3 are E flat
	// major from E flat 4, its first note, its chord and its span laid from
	// there.
	int8_t base = 0;
	// A keyboard's transpose by degrees, its default: every note it gives
	// out is moved this many of the pattern's own notes along it, by count, so the white keys' C D E G at +1
	// are D E F A. The keys stay as set and `base` at nought; the transpose in
	// semitones moves the keys instead and leaves this at nought. A count, an
	// integer from the param on (`degreeMoved`).
	int8_t degrees = 0;
	// And a roll's one note past its period: a roll of two octaves from C4
	// runs to C6, the octave closed at the top, which a pattern that
	// repeats could not say, the top C being the bottom one again.
	bool extra = false;
	// A volt divider instead of notes, the Qnt's third mode: ten volts in
	// this many equal parts, nought for none. It holds no notes, so `any` is false and the rig reads
	// it as volts; only `divides` says it snaps.
	int16_t divs = 0;
	// And its law: equal
	// volts, the parts' edges `bend` hundredths of the full bend toward a log
	// spacing (plus) or an exponential one (minus), nought straight; or the
	// harmonic series, line k at log2(k + 1) V, which no bend touches. Counts
	// from the one place they are read, the Qnt's params.
	bool harmonic = false;
	int8_t bend = 0;
	// A keyboard's microtonal detunes: whole cents off equal temperament, -99
	// to +99, a key of the pattern each, as it stands in force (moved by the
	// transpose with its key). A count from the one place it is read, the
	// Qnt's param; nought everywhere is equal temperament. A roll is never
	// detuned.
	int8_t cents[12 * MAX_OCTAVES] = {};
	bool detuned = false; // any cents at all: an equal-tempered scale never reads them
	/** The cents a note of the pattern is detuned by, nought off it. */
	int centsOf(int semis) const { return !detuned || spread || octaves <= 0 ? 0 : cents[keyOf(semis)]; }
	/** A note snapped to the pattern as it sounds, in semitones and in
	volts: the whole semitone and its cents, made a float once. A note in
	equal temperament is the whole semitone's own float, exactly. */
	float tunedSemis(int semis) const {
		const int c = centsOf(semis);
		return c == 0 ? (float)semis : (float)(semis * 100 + c) / 100.f;
	}
	float tunedVolts(int semis) const {
		const int c = centsOf(semis);
		return c == 0 ? (float)semis / 12.f : (float)(semis * 100 + c) / 1200.f;
	}
	/** Whether it is a volt divider at all: one part or more. One part is the
	whole ten volts one band, every voltage snapped to the ten volts at or
	under it, which with a boosted signal or an Atn's range of a volt gives
	integers; nought parts is unquantized, the volts passed as they come. */
	bool divides() const { return divs >= 1; }
	/** The key a pitch lights: its place in the pattern, or for a roll's
	note past the period the slot kept for it. */
	int rowOf(int semis) const {
		if (spread && extra && semis == base + period())
			return 12 * MAX_OCTAVES;
		return keyOf(semis);
	}

	int period() const { return 12 * octaves; }
	/** A key of the pattern, 0 to period - 1, for a pitch in semitones from C4. */
	int keyOf(int semis) const {
		const int p = period();
		return p > 0 ? eucMod(semis, p) : 0;
	}
	bool has(int semis) const {
		if (octaves <= 0)
			return false;
		const int k = keyOf(semis);
		return (oct[k / 12] >> (k % 12)) & 1;
	}
	/** Whether a note is one of mine: in the pattern, or a roll's own top C
	over it. The one test a key is lit by: a note that passed through
	unquantized is nobody's key, where folding it onto the period (`keyOf`)
	would light C4 on a Qnt with nothing in it. */
	bool holds(int semis) const {
		if (spread) { // a roll does not repeat: its notes stand where they stand
			if (semis == base + period())
				return extra;
			if (semis < base || semis > base + period())
				return false;
		}
		return has(semis);
	}
	void set(int key, bool on) {
		if (key < 0 || key >= period())
			return;
		if (on)
			oct[key / 12] |= 1 << (key % 12);
		else
			oct[key / 12] &= ~(1 << (key % 12));
	}
	/** Whether any note is in it: a Qnt with every key off is no quantizer. */
	bool any() const {
		for (int o = 0; o < octaves; o++)
			if (oct[o])
				return true;
		// A roll whose one note is its top C is a roll with a note, not an
		// empty one passing every voltage through, which an inversion running
		// a chord up the roll meets the moment only that C is left in it.
		return spread && extra;
	}
	/** How many notes it has, a roll's top C among them. */
	int count() const {
		int c = extra ? 1 : 0;
		for (int o = 0; o < octaves; o++)
			for (int k = 0; k < 12; k++)
				c += (oct[o] >> k) & 1;
		return c;
	}
	bool operator==(const Scale& o) const {
		if (octaves != o.octaves || spread != o.spread || base != o.base || degrees != o.degrees || extra != o.extra ||
		    divs != o.divs || harmonic != o.harmonic || bend != o.bend)
			return false;
		for (int i = 0; i < octaves; i++)
			if (oct[i] != o.oct[i])
				return false;
		if (detuned != o.detuned)
			return false;
		if (detuned)
			for (int k = 0; k < 12 * octaves && k < 12 * MAX_OCTAVES; k++)
				if (cents[k] != o.cents[k])
					return false;
		return true;
	}
	bool operator!=(const Scale& o) const { return !(*this == o); }
};

/** A Scale a widget can read while the audio thread writes it: its words
published under a generation counter, odd while being written, as a CV's
mapping is, so a screen copies and retries until it has a whole one and keeps
its last whole copy if the engine is mid-write every try. The words alone once
went without it, and a read could take new keys with an old degree count. */
struct ScaleAtomic {
	// The octave count in the low byte of the first word, then twelve bits
	// an octave in sixteen: three octaves in the first word and four in
	// each after it, so no octave straddles two.
	static const int WORDS = 3;
	std::atomic<uint64_t> w[WORDS] = {};
	std::atomic<int8_t> degrees{0}; // a keyboard's transpose by degrees, beside the words
	std::atomic<uint16_t> divLaw{0}; // a divider's law and bend, beside them too (`lawWord`)
	std::atomic<uint32_t> gen{0};
	mutable Scale last; // the UI's last whole copy, written only by load()
	uint64_t sent[WORDS] = {}; // the writer's copy of what it last stored
	int8_t sentDegrees = 0;
	uint16_t sentLaw = 0;
	/** A divider's law over its bend's byte, one word. */
	static uint16_t lawWord(const Scale& s) { return (uint16_t)((s.harmonic ? 1u : 0u) << 8 | (uint8_t)s.bend); }
	static_assert(3 + 4 * (WORDS - 1) >= MAX_OCTAVES, "ScaleAtomic has no room for every octave");
	static void slot(int i, int& word, int& shift) {
		word = i < 3 ? 0 : 1 + (i - 3) / 4;
		shift = i < 3 ? 8 + 16 * i : 16 * ((i - 3) % 4);
	}
	void store(const Scale& s) {
		uint64_t x[WORDS] = {(uint64_t)(uint8_t)s.octaves, 0, 0};
		for (int i = 0; i < MAX_OCTAVES; i++) {
			int word, shift;
			slot(i, word, shift);
			x[word] |= (uint64_t)s.oct[i] << shift;
		}
		// The roll's two, above the last octave in the last word.
		x[WORDS - 1] |= (uint64_t)(s.spread ? 1 : 0) << 56 | (uint64_t)(s.extra ? 1 : 0) << 57 |
		                (uint64_t)(uint8_t)s.base << 48;
		// A divider's parts, up to 255 (ten bits held): the low eight bits over the octave
		// count in the first word, the two above them over the roll's two.
		const uint16_t d = (uint16_t)clamp((int)s.divs, 0, 1023);
		x[0] |= (uint64_t)(d & 0xff) << 56;
		x[WORDS - 1] |= (uint64_t)(d >> 8) << 58;
		// Nothing moves for a scale already published, the counter included,
		// so a publish the host leaves out of a wake is the publish it made;
		// asked of the writer's own copy, never of the atomics it stores.
		const uint16_t law = lawWord(s);
		bool same = sentDegrees == s.degrees && sentLaw == law;
		for (int k = 0; k < WORDS && same; k++)
			same = sent[k] == x[k];
		if (same)
			return;
		for (int k = 0; k < WORDS; k++)
			sent[k] = x[k];
		sentDegrees = s.degrees;
		sentLaw = law;
		gen.fetch_add(1, std::memory_order_release); // odd: writing
		std::atomic_thread_fence(std::memory_order_release);
		for (int k = 0; k < WORDS; k++)
			w[k].store(x[k], std::memory_order_relaxed);
		degrees.store(s.degrees, std::memory_order_relaxed);
		divLaw.store(law, std::memory_order_relaxed);
		gen.fetch_add(1, std::memory_order_release); // even: whole
	}
	Scale load() const {
		uint64_t x[WORDS];
		int8_t deg = 0;
		uint16_t law = 0;
		bool whole = false;
		for (int attempt = 0; attempt < 4 && !whole; attempt++) {
			const uint32_t g0 = gen.load(std::memory_order_acquire);
			if (g0 & 1)
				continue;
			for (int k = 0; k < WORDS; k++)
				x[k] = relaxed(w[k]);
			deg = relaxed(degrees);
			law = relaxed(divLaw);
			std::atomic_thread_fence(std::memory_order_acquire);
			whole = relaxed(gen) == g0;
		}
		if (!whole)
			return last;
		Scale s;
		s.octaves = (int8_t)clamp((int)(x[0] & 0xff), 0, MAX_OCTAVES);
		for (int i = 0; i < MAX_OCTAVES; i++) {
			int word, shift;
			slot(i, word, shift);
			s.oct[i] = (uint16_t)((x[word] >> shift) & 0xfff);
		}
		s.spread = (x[WORDS - 1] >> 56) & 1;
		s.extra = (x[WORDS - 1] >> 57) & 1;
		s.base = (int8_t)(uint8_t)((x[WORDS - 1] >> 48) & 0xff);
		s.divs = (int16_t)(((x[0] >> 56) & 0xff) | ((x[WORDS - 1] >> 58) & 0x3) << 8);
		s.degrees = deg;
		s.harmonic = (law >> 8) & 1;
		s.bend = (int8_t)(uint8_t)(law & 0xff);
		last = s;
		return s;
	}
};

/** The bend's whole reach: the steepness of `bendExp`, the CRV's own exponential, at a bend of a
hundred, ln 16, so a fully bent divider's widest part is sixteen times its
narrowest, four octaves end to end, as the harmonic series of sixteen parts
runs four octaves. Past it the narrowest parts of a divider of a few dozen
fall under the pixel and a half a face draws a line at. */
const float DIV_BEND_FULL = 2.7725887f; // ln 16
/** The bend's count at full, either way. */
const int DIV_BEND_MAX = 100;

/** A volt divider: its lines, the band a voltage stands in, and where both
stand on its face, for both laws. One component, every reader of the lines asking it, so a Qnt, a CV's
cells and fader and a Seq's light never disagree about where a line is.

Equal volts: `n` parts of ten volts, an Atn's window moving where they start
(`origin`, the foot of its window, its offset less its x) and how much of ten
volts they divide (`gain`, its window's height over ten, either sign dividing
alike; nought lays every line on the origin), every ten volts the same parts
again. Straight, line k is exactly the `origin + k * 10 |gain| / n` it always
was; bent, the parts' edges within each ten volts stand on `bendExp` of their
straight place, line q n + r at q + bendExp(s, r / n) tens of volts, so the
ten volts' ends stay where they were and only what lies between them moves.

Harmonic: line k at log2(k + 1) volts, k from nought to n - 1, so a divider
of eight parts into a RATE jack, a volt an octave, lands on x1 to x8; the
highest line holds every voltage above it, the series being no period. Under
an Atn the series is moved by its offset (`centre`) and scaled by its x over
ten (`scale`), a fresh Atn at x10 leaving it as it stands.

Below nought, or below an Atn's origin, the picture above is mirrored, and
the band is the nearest line toward nought from the voltage: -3 V of one part on nought as 3 V is. The band
is an integer from the one place the voltage meets the divider, found by comparing with the very lines
`line` gives, so a value one divider put out lands on the same line in the
next, where a floor of the voltage times `n` over ten put 3.75 V of an eighth
divider on the line under it. */
struct Divider {
	int n = 0;
	bool harmonic = false;
	int bend = 0;                    // hundredths of the full bend: plus toward log, minus toward exp
	float origin = 0.f, gain = 1.f;  // equal volts under an Atn
	float centre = 0.f, scale = 1.f; // the harmonic series under an Atn
	Divider() {}
	explicit Divider(int parts, float origin_ = 0.f, float gain_ = 1.f) : n(parts), origin(origin_), gain(gain_) {}
	/** A Qnt's divider as its scale holds it, alone. */
	static Divider of(const Scale& s) {
		Divider d(s.divs);
		d.harmonic = s.harmonic;
		d.bend = s.bend;
		return d;
	}
	bool divides() const { return n >= 1; }
	/** `bendExp`'s steepness: a log bend crowds the parts toward the top, a
	concave curve, and so a steepness below nought. */
	float steepness() const { return -(float)bend * DIV_BEND_FULL / (float)DIV_BEND_MAX; }
	/** The line of band `k`, in volts. */
	float line(int k) const {
		if (n < 1)
			return 0.f;
		const int a = k >= 0 ? k : -k;
		if (harmonic) {
			const float h = std::fabs(scale) * std::log2((float)(std::min(a, n - 1) + 1));
			return k >= 0 ? centre + h : centre - h;
		}
		if (bend == 0)
			return origin + (float)k * CV_VOLTS * std::fabs(gain) / (float)n;
		const float u = CV_VOLTS * std::fabs(gain) * ((float)(a / n) + bendExp(steepness(), (float)(a % n) / (float)n));
		return k >= 0 ? origin + u : origin - u;
	}
	/** The band a voltage stands in: an estimate from the law's inverse, then
	the lines themselves asked which side of them it stands. */
	int band(float volts) const {
		const float g = std::fabs(harmonic ? scale : gain);
		if (n < 1 || !std::isfinite(volts) || g == 0.f)
			return 0;
		const float from = harmonic ? centre : origin;
		const float x = std::fabs(clamp((volts - from) / g, -1e6f, 1e6f));
		float est;
		if (harmonic)
			est = std::min(std::exp2(std::min(x, 30.f)) - 1.f, (float)(n - 1));
		else if (bend == 0)
			est = x * (float)n / CV_VOLTS;
		else {
			const float u = x / CV_VOLTS, q = std::floor(u), s = steepness();
			est = (q + std::log1p((u - q) * std::expm1(s)) / s) * (float)n;
		}
		const int far = harmonic ? n - 1 : INT_MAX;
		int k = (int)std::floor(est);
		if (volts >= from) {
			for (int i = 0; i < 8 && k < far && line(k + 1) <= volts; i++)
				k++;
			for (int i = 0; i < 8 && k > 0 && line(k) > volts; i++)
				k--;
			return k;
		}
		k = -k;
		for (int i = 0; i < 8 && k > -far && line(k - 1) >= volts; i++)
			k--;
		for (int i = 0; i < 8 && k < 0 && line(k) < volts; i++)
			k++;
		return k;
	}
	/** A voltage snapped: its band's line. */
	float snap(float volts) const { return line(band(volts)); }
	/** Where a band stands on the divider's screen, which shows one period
	of it, nought to ten volts of equal parts or the whole series: the
	screen shows where a voltage lands whatever ten volts it is in, as a
	keyboard shows the pitch class whatever the octave, so ten volts is the
	first band again; and below nought the picture is mirrored about it, the
	band either side of nought the first, so -0.01 to -1.99 V of five parts
	shows as nought to two and -2 V as two to four, as 2 V does. Integers in
	and out: the band is counted once, by `band`. */
	int shown(int b) const {
		if (n < 1)
			return 0;
		const int a = b >= 0 ? b : -b;
		return harmonic ? std::min(a, n - 1) : a % n;
	}
	/** Where edge `j` of the face's bands stands, nought at its foot and one
	at its top, j from nought to n: equal parts straight or bent as the
	lines are, the series on a scale of octaves, its top band reaching to
	log2(n + 1). */
	float faceAt(int j) const {
		if (n < 1)
			return 0.f;
		const float x = (float)j / (float)n;
		if (harmonic)
			return std::log2((float)(j + 1)) / std::log2((float)(n + 1));
		return bendExp(steepness(), x);
	}
};

/** The names of the twelve pitch classes from C, the black keys as flats,
as every screen and tooltip names them. */
inline const char* pcName(int pc) {
	static const char* const n[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
	return n[eucMod(pc, 12)];
}
/** The black keys of one octave, bit 0 being C. */
const uint16_t BLACK_KEYS = 0x54a;
/** Whether a pitch class is one of the five black keys. */
inline bool isBlackKey(int pc) { return (BLACK_KEYS >> eucMod(pc, 12)) & 1; }

/** Twelve keys of one octave, bit 0 being C, as seen from root `r`, 0 to
11: the key r semitones up is bit 0. */
inline uint16_t fromRoot(uint16_t bits, int r) {
	return (uint16_t)(((bits >> r) | (bits << (12 - r))) & 0xfff);
}

/** A name for the twelve keys of one octave, bit 0 being C: the mode or
scale it is, on the root it is that on, the one on C first; empty where it
is none of those. */
inline std::string patternName(uint16_t bits) {
	struct Named {
		uint16_t mask;
		const char* name;
	};
	auto m = [](std::initializer_list<int> ks) {
		uint16_t b = 0;
		for (int k : ks)
			b |= (uint16_t)(1 << k);
		return b;
	};
	static const Named table[] = {
	    {m({0, 2, 4, 5, 7, 9, 11}), "ionian"},     {m({0, 2, 3, 5, 7, 9, 10}), "dorian"},
	    {m({0, 1, 3, 5, 7, 8, 10}), "phrygian"},   {m({0, 2, 4, 6, 7, 9, 11}), "lydian"},
	    {m({0, 2, 4, 5, 7, 9, 10}), "mixolydian"}, {m({0, 2, 3, 5, 7, 8, 10}), "aeolian"},
	    {m({0, 1, 3, 5, 6, 8, 10}), "locrian"},    {m({0, 2, 3, 5, 7, 8, 11}), "harm minor"},
	    {m({0, 2, 3, 5, 7, 9, 11}), "mel minor"},  {m({0, 2, 4, 7, 9}), "maj pent"},
	    {m({0, 3, 5, 7, 10}), "min pent"},         {m({0, 2, 5, 7, 10}), "suspended"},
	    {m({0, 3, 5, 8, 10}), "man gong"},         {m({0, 2, 5, 7, 9}), "ritusen"},
	    {m({0, 3, 5, 6, 7, 10}), "blues"},         {m({0, 2, 4, 6, 8, 10}), "whole tone"},
	    {m({0, 4, 7}), "major"},                   {m({0, 3, 7}), "minor"},
	};
	bits &= 0xfff;
	if (bits == 0xfff)
		return "chromatic";
	// The pattern as seen from root r, which has to be one of its keys.
	auto from = [&](int r) { return fromRoot(bits, r); };
	// On C, whatever it is there: the white keys turned by three are C
	// aeolian, not E flat ionian.
	if (bits & 1)
		for (const Named& n : table)
			if (n.mask == from(0))
				return std::string("C ") + n.name;
	// Otherwise the plainest name it has on any root: a major scale before
	// its modes, so the black keys are G flat's major pentatonic.
	for (const Named& n : table)
		for (int r = 1; r < 12; r++)
			if (((bits >> r) & 1) && n.mask == from(r))
				return std::string(pcName(r)) + " " + n.name;
	return "";
}



/** The nearest semitone in `scale`, `semis` itself if it is, the higher of
two at the same distance. */
inline int snapToScale(int semis, const Scale& scale) {
	if (!scale.any())
		return semis;
	for (int d = 0; d <= scale.period(); d++) {
		for (int sgn : {+1, -1}) {
			int k = semis + sgn * d;
			if (scale.has(k))
				return k;
		}
	}
	return semis;
}

// ---- One pitch space. Every path that moves a note by an Atn's offset asks
// these, and nothing else: the Atn's bar lands on `shiftOnScale`, a CV's
// window and a pitch on the rig are moved by `shiftAfterSnap`, which counts
// `scaleDegrees` from the same root the bar measured from, so the number on
// the Atn is the interval the pattern's root moves by, whatever path the note
// took.

/** The highest note of a scale at or below semitone `k`, in semitones from
C4. Whole semitones in and out: a pitch that is a count is an integer before
it gets here, a float meant to be whole being no count (-12.000001 for a
-12 has been met here). */
inline int noteAtOrBelow(int k, const Scale& scale) {
	for (int d = 0; d < scale.period(); d++, k--)
		if (scale.has(k))
			break;
	return k;
}
/** And the lowest at or above. */
inline int noteAtOrAbove(int k, const Scale& scale) {
	for (int d = 0; d < scale.period(); d++, k++)
		if (scale.has(k))
			break;
	return k;
}

/** The note of `scale` nearest a pitch, in semitones from C4: the boundary
exactly halfway between two neighbouring notes and the upper one at a dead
tie, which is how a CV's jack places a voltage (`Slots::travelOf`). The pitch
is continuous, so it is not rounded first: rounded to a semitone and then
snapped, 0.6 of a semitone over the white keys goes to C sharp and up to D, a
note further away than C. Fundamental's Quantizer rounds first, and takes the
lower at that tie. */
inline int nearestInScale(float semis, const Scale& scale) {
	const int below = noteAtOrBelow((int)std::floor(semis), scale);
	const int above = noteAtOrAbove(below + 1, scale);
	return semis - (float)below < (float)above - semis ? below : above;
}

/** The pattern's own root: the first note of it from C4 up, which is what a
transposition is measured from. A pattern of C and G has its root on C and
transposes by 0, 7, 12, 19; a pattern of D alone has its root on D and
transposes by octaves, since those are the only intervals it holds. Without
it a transposition of nought would snap to whatever note lay below C4. */
inline int scaleRoot(const Scale& scale) {
	for (int k = scale.base; k < scale.base + scale.period(); k++)
		if (scale.has(k))
			return k;
	return 0;
}

/** A transposition in semitones landed on the pattern: the intervals the
pattern holds from its own root, and the one nearest what was asked for, the
upper at a tie, the Qnt's own rule for a pitch (`snapToScale`), so a black key
at an Atn's offset rounds the same way before a Qnt and after one (relation
offset-nearest). Nought is always one of them. */
inline int shiftOnScale(int semis, const Scale& scale) {
	const int root = scaleRoot(scale);
	return snapToScale(root + semis, scale) - root;
}

/** The pattern's notes counted, as integers and never walked: a note of the
pattern is its period's count times the periods below it plus its place in
its own period, so moving a note any number of degrees costs what moving it
one does (a walk of one semitone at a time cost a whole core under a CV
with a moving jack). The pattern
repeats every `period`, its keys the low twelve bits of each octave's word. */
inline int keysIn(const Scale& sc) {
	int n = 0;
	for (int o = 0; o < sc.octaves; o++)
		n += __builtin_popcount(sc.oct[o] & 0xfff);
	return n;
}
/** How many keys of the period are at or below key `r`, 0 to period - 1. */
inline int keysUpTo(const Scale& sc, int r) {
	int n = 0;
	for (int o = 0; o < r / 12; o++)
		n += __builtin_popcount(sc.oct[o] & 0xfff);
	return n + __builtin_popcount(sc.oct[r / 12] & (0xfffu >> (11 - r % 12)));
}
/** The key, 0 to period - 1, holding the `j`th note of the period from 0. */
inline int keyAt(const Scale& sc, int j) {
	for (int o = 0; o < sc.octaves; o++) {
		const unsigned bits = sc.oct[o] & 0xfff;
		const int c = __builtin_popcount(bits);
		if (j < c) {
			unsigned b = bits;
			for (int i = 0; i < j; i++)
				b &= b - 1; // the lowest set bit off, j times: at most eleven
			return 12 * o + __builtin_ctz(b);
		}
		j -= c;
	}
	return 0;
}
/** How many of the pattern's notes stand at or below semitone `k`, counted
from the period that starts at nought: a difference of two is the count
between them, which is all a degree is. In 64 bits, since a pitch that came
from a voltage of no meaning, a cable looped back into the rig, can be
any integer at all. */
inline int64_t notesUpTo(const Scale& sc, int64_t k, int perPeriod) {
	const int64_t p = sc.period();
	const int64_t q = k >= 0 ? k / p : -((-k + p - 1) / p);
	return q * perPeriod + keysUpTo(sc, (int)(k - q * p));
}
/** A shift after the quantizer, counted in the pattern's own notes: the shift
names an interval of the pattern from its root, the one `shiftOnScale` lands
the bar on, and the distance is how many of the pattern's notes that interval
spans. Over the white keys +2 is one note and +4 two, so C goes to D and then
E, and A to B and then C: the pattern is kept, which is what an offset after a
quantizer is for, where a plain shift in semitones would move A four semitones
to C sharp, out of the pattern it had just been snapped to. */
inline int scaleDegrees(const Scale& sc, int semis) {
	const int from = scaleRoot(sc), to = from + shiftOnScale(semis, sc);
	if (to == from || sc.octaves <= 0)
		return 0;
	const int n = keysIn(sc);
	// The notes in from + 1 to `to` going up, in `to` to from - 1 going down.
	return (int)(to > from ? notesUpTo(sc, to, n) - notesUpTo(sc, from, n)
	                       : notesUpTo(sc, (int64_t)to - 1, n) - notesUpTo(sc, (int64_t)from - 1, n));
}
/** Note `note` of the pattern moved `deg` of the pattern's notes along: the
note `deg` places past the last one at or below it, or before the first one
under it, by count. A move held to `24 * MAX_OCTAVES` semitones, a pattern
with no notes at all stopping there too. */
inline int stepInScale(const Scale& sc, int note, int deg) {
	if (deg == 0)
		return note;
	const int reach = 24 * MAX_OCTAVES;
	const int n = sc.octaves > 0 ? keysIn(sc) : 0;
	if (n == 0)
		return deg > 0 ? note + reach : note - reach;
	const int64_t atOrBelow = notesUpTo(sc, note, n);
	const int64_t idx = deg > 0 ? atOrBelow - 1 + deg : atOrBelow - (sc.has(note) ? 1 : 0) + deg;
	const int64_t q = idx >= 0 ? idx / n : -((-idx + n - 1) / n);
	const int64_t k = q * sc.period() + keyAt(sc, (int)(idx - q * n));
	const int64_t held = deg > 0 ? std::min(k, (int64_t)note + reach) : std::max(k, (int64_t)note - reach);
	// Past the ends of an int only from a pitch with no meaning: held at the
	// end rather than wrapped.
	return (int)std::min<int64_t>(std::max<int64_t>(held, INT_MIN), INT_MAX);
}
/** A note moved by a keyboard's transpose by degrees (`Scale::degrees`),
landed on the pattern first, the way back with `back`: nothing on a roll, a
divider or a keyboard at nought. Applied where a note leaves the Qnt, its own
cable, its chord and a CV's fader over it, and to the ends of an Atn's window
over it, and nowhere else, so an Atn's offset after it (`shiftAfterSnap`)
walks the pattern on from where the degrees put the note. */
inline int degreeMoved(const Scale& sc, int note, bool back = false) {
	if (sc.degrees == 0 || !sc.any() || sc.spread)
		return note;
	return stepInScale(sc, snapToScale(note, sc), back ? -sc.degrees : sc.degrees);
}
/** A transpose by degrees as far as it reaches: the pattern's root moved
at most `semis` semitones either way, so a count asked past it holds at the
last note of the pattern within them. Counted on the keys as set, `base` at
nought. The engine clamps by it and the stroke folds by it, so a drag past the
end turns back at once. */
/** The notes of a pattern in one period, which is how many degrees move
every note it gives out by that period exactly: seven on the white keys,
five on a pentatonic, twelve on two octaves of the two. Nought where there is
no pattern to count, or a roll, which never repeats. */
inline int notesInPeriod(const Scale& sc) {
	if (!sc.any() || sc.spread)
		return 0;
	int n = 0;
	for (int k = 0; k < sc.period(); k++)
		n += sc.has(k) ? 1 : 0;
	return n;
}
inline int degreeReach(const Scale& sc, int deg, int semis) {
	if (!sc.any() || sc.spread)
		return 0;
	const int root = scaleRoot(sc);
	int up = 0, down = 0;
	for (int k = root + 1; k <= root + semis; k++)
		up += sc.has(k) ? 1 : 0;
	for (int k = root - semis; k < root; k++)
		down += sc.has(k) ? 1 : 0;
	return clamp(deg, -down, up);
}
/** The shift after the quantizer applied to a note, in semitones: in the
pattern where there is one to keep, as it is on a roll or with none. */
inline float shiftAfterSnap(const Scale& sc, float note, int semis, bool back = false) {
	if (semis == 0)
		return note;
	if (!sc.any() || sc.spread)
		return note + (float)(back ? -semis : semis);
	const int deg = scaleDegrees(sc, semis);
	return (float)stepInScale(sc, snapToScale((int)std::round(note), sc), back ? -deg : deg);
}
/** The same of a whole note, which stays whole: a note that is a count from
the start is shifted as one, and never rounded again. */
inline int shiftAfterSnap(const Scale& sc, int note, int semis, bool back = false) {
	if (semis == 0)
		return note;
	if (!sc.any() || sc.spread)
		return note + (back ? -semis : semis);
	const int deg = scaleDegrees(sc, semis);
	return stepInScale(sc, snapToScale(note, sc), back ? -deg : deg);
}

/** A roll's notes, low to high, into `notes`; how many. */
inline int spreadNotes(const Scale& s, int* notes) {
	int n = 0;
	for (int k = 0; k < s.period(); k++)
		if (s.has(s.base + k))
			notes[n++] = s.base + k;
	if (s.extra)
		notes[n++] = s.base + s.period();
	return n;
}

/** A roll's note nearest a pitch, in semitones from C4, the upper on a tie as
a keyboard takes it: how a roll quantizes a cable, which carries a pitch and
not a fader's travel, so a C4 in on a roll holding C4 E4 G4 is C4, where
sharing the travel among the notes would make it E4. */
inline int nearestSpread(const Scale& s, float semis) {
	int notes[12 * MAX_OCTAVES + 1];
	const int n = spreadNotes(s, notes);
	if (n == 0)
		return (int)std::round(semis);
	int best = notes[0];
	for (int i = 1; i < n; i++)
		if (std::fabs((float)notes[i] - semis) <= std::fabs((float)best - semis))
			best = notes[i];
	return best;
}

/** The same, preferring `root` as the note it is named on: the white keys
from F are F lydian rather than C ionian. */
inline std::string patternNameFrom(uint16_t bits, int root) {
	root = eucMod(root, 12);
	const uint16_t onC = fromRoot(bits, root);
	const std::string c = patternName(onC);
	if (c.size() > 2 && c[0] == 'C' && c[1] == ' ')
		return std::string(pcName(root)) + c.substr(1);
	return patternName(bits);
}

/** A scale as a chord: its notes over its octaves from C4 up, in volts, one
a channel, the lowest sixteen of them. What a Qnt's jack carries with no
pitch to snap, and a Seq's over a block of Qnt with no signal: the notes
laid out on the keyboard, sounded at once. Returns how many. */
inline int chordOf(const Scale& scale, float* volts) {
	int n = 0;
	// A divider's first lines from nought up, one a channel, as many as it
	// has parts or sixteen: an unpatched divider matches the other two modes,
	// which give a chord.
	if (scale.divides()) {
		for (; n < scale.divs && n < NOTES; n++)
			volts[n] = Divider::of(scale).line(n);
		return n;
	}
	for (int k = scale.base; k < scale.base + scale.period() && n < NOTES; k++)
		if (scale.has(k))
			volts[n++] = scale.tunedVolts(k);
	// Transposed by degrees, the same count of the pattern's notes from the
	// root moved along it, each sounding its own key's cents.
	if (scale.degrees != 0 && n > 0 && !scale.spread) {
		int k = degreeMoved(scale, scaleRoot(scale));
		for (int i = 0; i < n; i++, k = stepInScale(scale, k, 1))
			volts[i] = scale.tunedVolts(k);
	}
	// A roll's top C, and its notes where they stand, never counted from C4
	// round the period, which would put a roll's notes under C4 an octave or
	// more too high.
	if (scale.spread && scale.extra && n < NOTES)
		volts[n++] = (scale.base + scale.period()) / 12.f;
	return n;
}

/** An Atn's gain where a scale is in reach, as a span of notes rather than a
reach either side of C4. The gain in volts is the span in octaves, twelve
semitones a volt, and it grows upward first: one octave is C4 to C5, two is C3
to C5, three C3 to C6, and so on, each octave going on top when it is an odd
one and underneath when it is an even one, and a part octave growing the way
the next whole one would. Ten volts is ten octaves, C-1 to C9. Below the line
it is the same span where it stands, turned over, so the window runs downward
over the same notes: x-0.1 is C5 down to C4, the octave x0.1 spans, since an
inverted Atn keeps the CVs' notes where they stand. `lo` and `hi` are volts
from C4. */
inline void atnSpan(float gain, float& lo, float& hi) {
	const float s = std::fabs(gain) * 12.f;
	const int n = (int)std::floor(s / 12.f);
	const float part = s - 12.f * (float)n;
	float up = 12.f * (float)((n + 1) / 2), down = 12.f * (float)(n / 2);
	if (n % 2 == 0)
		up += part;
	else
		down += part;
	hi = up / 12.f;
	lo = -down / 12.f;
}

/** The same span for a scale: laid from the keyboard's C, which is C4 moved
by its transpose (`base`), so a keyboard transposed by three lays every window
from E flat 4 and the whole of what a CV plays over it moves by the three
(relation transpose-moves). A roll's is laid from C4, and shares its travel
among its notes whatever the span. */
inline void noteSpan(float gain, const Scale& scale, float& lo, float& hi) {
	atnSpan(gain, lo, hi);
	if (scale.any() && !scale.spread) {
		lo += (float)scale.base / 12.f;
		hi += (float)scale.base / 12.f;
	}
}

/** Whether a span `semis` semitones wide ends on a note of the scale at the
end it grows from, an offset of `ofsSemis` moving it along the pattern as a
CV's note is moved: the widths a plain stroke on an Atn's span lands on. The
note is asked of the whole pattern, every octave of it, never folded onto one
octave, so a keyboard wider than an octave answers for its second octave with
its own notes. */
inline bool spanEndsOnScale(int semis, const Scale& scale, int ofsSemis) {
	float lo, hi;
	noteSpan((float)semis / 12.f, scale, lo, hi);
	// Whole octaves of a width that is whole semitones, by integer
	// division, where a tolerance before a floor was.
	// Turned over, the span stands where it did (`atnSpan`), so it grows
	// from the same end either side of the line.
	const int n = std::abs(semis) / 12;
	const bool growsUp = n % 2 == 0;
	const int base = (int)std::round((growsUp ? hi : lo) * 12.f);
	const int on = snapToScale(base, scale);
	const int pitch = ofsSemis == 0 ? base : base + shiftAfterSnap(scale, on, ofsSemis) - on;
	return scale.has(pitch);
}

inline std::string noteName(int semis) { // relative to C4, the black keys as flats, as the Qnt names them
	const int v = semis + 48; // C0 = 0
	const int oct = v >= 0 ? v / 12 : -((-v + 11) / 12);
	return string::f("%s%d", pcName(v), oct);
}

/** A note written as `noteName` and `centsName` write it, read back into
volts at one volt an octave from C4: a letter, a sharp or a flat, an octave,
and the cents off it if any ("E4", "C#3", "Bb2", "E4 +27c"). The letter takes
either case, and its octave is the letter's, so B#3 is C4. False, leaving
`volts` alone, for anything else, so a plain number goes on to Rack's own
parse. The semitones and cents are whole from where they are read, and the
volts one division of them. */
inline bool readNoteName(const std::string& text, float& volts) {
	static const int fromA[7] = {9, 11, 0, 2, 4, 5, 7}; // A B C D E F G
	const size_t n = text.size();
	size_t i = 0;
	auto skip = [&] {
		while (i < n && std::isspace((unsigned char) text[i]))
			i++;
	};
	// A whole number of at most four digits; `sign` 1 lets a sign stand
	// before it, 2 asks for one.
	auto whole = [&](int sign, int& out) {
		int s = 1;
		if (i < n && (text[i] == '-' || text[i] == '+') && sign != 0) {
			s = text[i] == '-' ? -1 : 1;
			i++;
		}
		else if (sign > 1)
			return false;
		int v = 0, d = 0;
		while (i < n && d < 4 && std::isdigit((unsigned char) text[i])) {
			v = v * 10 + (text[i] - '0');
			i++, d++;
		}
		out = s * v;
		return d > 0 && !(i < n && std::isdigit((unsigned char) text[i]));
	};
	skip();
	if (i >= n)
		return false;
	const char letter = (char)std::toupper((unsigned char) text[i]);
	if (letter < 'A' || letter > 'G')
		return false;
	int semis = fromA[letter - 'A'];
	i++;
	if (i < n && (text[i] == '#' || text[i] == 'b')) {
		semis += text[i] == '#' ? 1 : -1;
		i++;
	}
	int oct = 0;
	if (!whole(1, oct)) // the octave may be below nought, C-1
		return false;
	semis += 12 * (oct - 4);
	skip();
	int cents = 0;
	if (i < n) {
		if (!whole(2, cents) || i >= n || (text[i] != 'c' && text[i] != 'C'))
			return false;
		i++;
		skip();
		if (i < n)
			return false;
	}
	volts = (float)(semis * 100 + cents) / 1200.f;
	return true;
}

/** A field's words handed back as Rack's parser can read them: the x an
Atn's gain is written with and the unit a reading ends in (V, semitones)
taken off, the number left to Rack. A field opens on the reading itself,
x0.229 or +0.0 V, and the parser takes a letter for a variable it does not
know and refuses the whole, so a hand editing the number it was shown changed
nothing (0.25 typed into an Atn's X left it at .23). Every field whose reading carries a letter hands its
text through this: the Atn's two, a CV's value, a CRV's points. */
inline std::string typedBack(std::string s) {
	s = string::trim(s);
	if (!s.empty() && (s[0] == 'x' || s[0] == 'X'))
		s.erase(0, 1);
	const std::string low = string::lowercase(s);
	for (const std::string unit : {"semitones", "semitone", "v"})
		if (low.size() > unit.size() && low.compare(low.size() - unit.size(), unit.size(), unit) == 0) {
			s.erase(s.size() - unit.size());
			break;
		}
	return s;
}
/** A span of notes written as an Atn's gain reads it in notes, read back as
its width in semitones: "C4 to A4" is nine, "A4 down to C4" minus nine, the
inverted span the gain below the line reads. False, leaving `semis` alone,
for anything else, a lone note among it, which names no width. The width is
whole cents from where each note is read, so the difference is exact. */
inline bool readNoteSpan(const std::string& text, float& semis) {
	// Either way the width is the second note less the first: the inverted
	// span is read from its top down to its foot.
	const std::string low = string::lowercase(text);
	size_t at = low.find(" down to "), len = 9;
	if (at == std::string::npos)
		at = low.find(" to "), len = 4;
	if (at == std::string::npos)
		return false;
	float a, b;
	if (!readNoteName(text.substr(0, at), a) || !readNoteName(text.substr(at + len), b))
		return false;
	const long cents = std::lround((double)b * 1200.0) - std::lround((double)a * 1200.0);
	semis = (float)cents / 100.f;
	return true;
}
/** A field in volts at one volt an octave, which takes a note typed by its
name as that note rather than as Rack's hertz: a CV's value, a CRV's points. */
struct PitchFieldQuantity : ParamQuantity {
	void setDisplayValueString(std::string text) override {
		float v;
		if (readNoteName(text, v))
			setDisplayValue(v);
		else
			ParamQuantity::setDisplayValueString(typedBack(text));
	}
};

/** A voltage read as a note: the nearest semitone, and how far off it the
volt stands in whole cents, -50 to +49, the breakpoint at fifty, for a chord
an Atn spreads off the grid. The volt is continuous, so it becomes a count
once, whole cents, and the semitone and the remainder are integer division of
that one count: the name and the cents can never disagree about which note the
volt is nearest. */
struct Tuned {
	int semis = 0;
	int cents = 0;
};
inline Tuned tunedOf(float volts) {
	const long c = std::lround((double)volts * 1200.0);
	const long up = c + 50;
	const long s = up >= 0 ? up / 100 : -((-up + 99) / 100);
	return {(int)s, (int)(c - s * 100)};
}
/** The cents off its note, "+27c" or "-45c"; nothing on the note itself. */
inline std::string centsName(const Tuned& t) {
	return t.cents == 0 ? std::string() : string::f("%+dc", t.cents);
}

} // namespace rig
