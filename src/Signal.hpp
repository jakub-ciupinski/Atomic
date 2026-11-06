// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <climits>
#include "Pitch.hpp"
#include "Pulse.hpp"
#include "Tree.hpp"
#include <cstdint>
#include <string>

/** The signal: the vocabulary nodes hand each other along the rig, and the
rules on it. The constants a rig is sized by, what reads a block
 (`Reading`), the word a Qnt hands its shapers, the window a CV
is resolved in (`Mapping`, `Slots`), the signal and the count themselves, the
steps and shapers as free functions over them, and the faces a playhead asks
of a member. A signal is what a reader hands the member it stands on, its own
cable, and what that member hands back; the rig itself carries none between
modules. All of it plain data, compared by value, which is
what lets the gate hold a wake against the last one mechanically. */

namespace rig {

struct Cartridge; // Cartridge.hpp: what a Mapping's Atn is (`atnNode`)

/** The longest walk a Seq makes, its own memory included, and so how far its
block claim reaches: a block longer than this is walked for this many steps,
the ones nearest the Seq, and the members past them are not claimed at all.
Every limit of the Seq's derives from it, the param ranges, the arrays, the
packing of its screen word, so it changes here alone. 255, the most two hex
cells say. */
const int MAX_SEQ_STEPS = 255;

/** A Trg cartridge holds this many rings. Each is one step of a gate sequence
when a Seq walks the block, and one polyphonic channel when the Trg stands
alone. Eight, so a Trg is a bar of eight and a rig of them chains into
sixteen, twenty-four and so on. That is the default; a stroke with shift or
command held sets a Trg anywhere from one ring to `TRG_RINGS`, and a Seq walks
each Trg for as many as it has. */
const int GATE_CHANNELS = 8;
const int TRG_RINGS = 16;
// A trigger's thresholds, its length and a screen's hold on it: Pulse.hpp.


/** No rails: a source gives exactly what
its screen shows, a CV its window and a CRV its curve, the overshoot of a
spline past its window played as it is and left undrawn; and a processor, an Atn,
a MIX, a Poly, a Qnt quantizing a cable, a Seq sounding its members, passes
the real number, thousands of volts if that is what was asked for. The CV,
held to its window, is the one clamp in the plugin a signal can be patched
through. Only a number that is no number is stopped, so nothing in the rig is
ever NaN or infinite; a cable looped back into the rig may climb until it
would be. */
inline float sane(float v) { return std::isfinite(v) ? v : 0.f; }
/** No divider's band: what a band is where nothing divided. */
constexpr int NO_BAND = INT_MIN;

/** How a CV's inner value, 0 to 1, becomes volts. Remappers to the left
of a CV build it up: Atn sets the reach, Qnt a pattern of notes within the
window, an offset a shift, before or after the quantizer depending on where
it stands. The CV resolves it and shapes its screen. */
struct Mapping {
	float reach = CV_VOLTS;    // the window is -reach to reach, in volts, and runs downward when reach is below nought
	float preShift = 0.f;      // volts added to the window: an offset with no quantizer in reach
	int postShift = 0;         // semitones added after snapping: an offset after the quantizer, a count
	// Of `preShift`, the whole semitones an Atn in notes put there in front
	// of a quantizer it cannot see yet: the Qnt that lays its pattern on the
	// window lands them on it (`Qnt::shape`), as an offset after it is landed
	// (`shiftOnScale`), so a black key before a Qnt and after one moves the
	// pattern's root to the same note.
	int landSemis = 0;
	Scale scale;               // a Qnt's pattern; none for none
	bool spanned = false;      // the reach is a span of notes already, laid by an Atn after a quantizer (`shapeAtnNotes`)
	const tree::Node* quantNode = nullptr; // the quantizer that set the scale, empty or not
	int quantColor = -1;       // and the colour it wears, for the CVs quantized through it
	// An Atn its CV holds over a roll (`HELD_ROLL`): the chord scaled about
	// its lowest note by `rollGain`, the Atn's x, and moved by `rollCents`,
	// its offset, a count of cents. Nought and one leave the roll as it is.
	float rollGain = 1.f;
	int rollCents = 0;
	// An Atn its CV holds over a divider (`HELD_DIVIDER`): equal parts start
	// at `divOrigin`, the foot of its window, and divide `divGain` of ten
	// volts, its x; the harmonic series stands on `divCentre`, its offset,
	// scaled by `divScale`, its x over ten (`Divider`).
	float divOrigin = 0.f;
	float divGain = 1.f;
	float divCentre = 0.f;
	float divScale = 1.f;
	/** The divider in force here: the scale's, under the Atn's window. */
	Divider divider() const {
		Divider d = Divider::of(scale);
		d.origin = divOrigin;
		d.gain = divGain;
		d.centre = divCentre;
		d.scale = divScale;
		return d;
	}
	// The lane: the mode of the Qnt that laid the
	// scale, 0 unquantized (or no Qnt), 1 a keyboard, 2 a roll, 3 a divider,
	// so a block whose own Qnt is of the same mode keeps the range; and the
	// Atn that set the range, so a keyboard of its own can lay that Atn's
	// range again on its own keys (`Qnt::shape`).
	int8_t laneMode = 0;
	Cartridge* atnNode = nullptr;

	bool quantized() const { return scale.any(); }
	/** Whether the rig here is in pitch: a quantizer is in reach AND has a
	pattern on it, so an offset is semitones and a Atn's top is a note.

	An empty Qnt does not count: it snaps nothing and passes what it is given
	straight through, and counting it left an Atn in front of it with its top
	a plain multiplier and its offset in semitones, two lanes of one module
	disagreeing about what rig they are in. Nothing is quantized until a note
	is picked, so nothing is musical either. */
	bool musical() const { return quantized(); }
	bool operator==(const Mapping& o) const {
		return reach == o.reach && preShift == o.preShift && postShift == o.postShift && landSemis == o.landSemis &&
		       scale == o.scale &&
		       spanned == o.spanned && quantNode == o.quantNode && quantColor == o.quantColor &&
		       rollGain == o.rollGain && rollCents == o.rollCents && divOrigin == o.divOrigin && divGain == o.divGain &&
		       divCentre == o.divCentre && divScale == o.divScale &&
		       laneMode == o.laneMode && atnNode == o.atnNode;
	}
	bool operator!=(const Mapping& o) const { return !(*this == o); }
	/** The window runs from `bottom`, where the travel starts, to `top`, and
	`top` is below `bottom` when a Atn has turned it over. It is symmetric
	about nought until an offset shifts it, so a sequence sits around C4 and a
	Atn scales it about C4 either way.

	Under a keyboard the reach is a span of notes, laid here wherever no Atn
	after the quantizer has laid it already (`noteSpan`): ten volts are ten
	octaves, C-1 to C9 from a lowest key of C, with no Atn, with one before
	the Qnt, and with one after it alike, so a fresh Atn changes nothing
	wherever it stands. A roll shares the whole travel among
	its notes and stays outside the span. */
	bool laid() const { return scale.any() && !scale.spread && !spanned; }
	float span() const {
		if (!laid())
			return 2.f * reach;
		float lo, hi;
		noteSpan(reach, scale, lo, hi);
		return reach < 0.f ? lo - hi : hi - lo;
	}
	float bottom() const {
		if (!laid())
			return -reach + preShift;
		float lo, hi;
		noteSpan(reach, scale, lo, hi);
		return (reach < 0.f ? hi : lo) + preShift;
	}
	float top() const {
		if (!laid())
			return reach + preShift;
		float lo, hi;
		noteSpan(reach, scale, lo, hi);
		return (reach < 0.f ? lo : hi) + preShift;
	}
	/** The volts a wave is put out between in this window, unsnapped: its
	ends, and an offset under a scale, a shift after the snap in semitones,
	as the same interval in volts. */
	void waveEnds(float& lo, float& hi) const {
		lo = bottom();
		hi = top();
		if (postShift != 0) {
			lo += (float)postShift / 12.f;
			hi += (float)postShift / 12.f;
		}
	}
	/** The ends as plain volts, the reach either side of the shift, a scale
	in reach laying no span of notes: what a CRV plays between, which is
	never quantized. The same as `waveEnds` wherever no span is laid. */
	void plainEnds(float& lo, float& hi) const {
		if (laid()) {
			lo = -reach + preShift + (float)postShift / 12.f;
			hi = reach + preShift + (float)postShift / 12.f;
		}
		else
			waveEnds(lo, hi);
	}
	/** A value, minus one to one, placed between those ends. */
	static float placed(float v, float lo, float hi) { return lo + (clamp(v, -1.f, 1.f) + 1.f) * 0.5f * (hi - lo); }
};

/** The travel a voltage means to a CV with nothing to its left: minus
CV_VOLTS to CV_VOLTS is the whole of it, and past either end it is held there.
A CV with a window made for it places a voltage with `Slots::travelOf`. */
inline float cvTravel(float volts) { return clamp((volts + CV_VOLTS) / (2.f * CV_VOLTS), 0.f, 1.f); }

/** The mapping made concrete: a voltage across the window, snapped to a
Qnt's pattern when one is in reach. Under a Qnt the travel stays a voltage,
as a quantizer proper takes one: the middle of the fader is C4 whatever the
pattern holds, a fader all the way down is the nearest note to the bottom of
the window, and a pattern with uneven octaves has no say over where any note
sits. */
struct Slots {
	bool named = false; // the output is a note, worth naming

	/** The one list, `windowNotes`, worked out when the mapping changes and
	kept: it depends on the mapping alone, never on the travel or a volt, so a
	CV whose jack moves every sample reads it rather than making it again
	four times a wake (a Qnt over a CV with an LFO in its jack once cost up to
	a whole core). A Slots is only ever read
	beside the mapping it was resolved for, both published together. */
	void resolve(const Mapping& m) {
		named = m.scale.any();
		count = windowNotes(m, notes);
	}
	/** The notes a quantized CV is made of, low to high, into `out`; how
	many. A roll's are its own notes, the whole of what it quantizes to. A
	keyboard's are the notes of its pattern between the window's ends, which
	are notes by construction (an Atn after a Qnt grows its span by whole
	notes, and with none the window is ten volts either side), so they are
	taken as whole semitones once, by rounding. A window shut to nothing is
	the one note nearest it. This is the one list: the CV's screen draws it
	as its cells, the fader walks it, and the jack lands on it. */
	static const int MAX_NOTES = 24 * MAX_OCTAVES + 1;
	static int windowNotes(const Mapping& m, int* out) {
		if (!m.scale.any())
			return 0;
		if (m.scale.spread)
			return spreadNotes(m.scale, out);
		const int lo = (int)std::round(std::min(m.bottom(), m.top()) * 12.f);
		const int hi = (int)std::round(std::max(m.bottom(), m.top()) * 12.f);
		int n = 0;
		for (int k = lo; k <= hi && n < MAX_NOTES; k++)
			if (m.scale.has(k))
				out[n++] = k;
		if (n == 0)
			out[n++] = snapToScale(lo, m.scale);
		// A keyboard transposed by degrees moves every note of the window
		// along its pattern (`degreeMoved`): the same count of cells, the
		// ladder the screen names and the jack sounds.
		if (m.scale.degrees != 0)
			for (int i = 0; i < n; i++)
				out[i] = degreeMoved(m.scale, out[i]);
		return n;
	}
	int notes[MAX_NOTES];
	int count = 0;
	/** How many cells a CV's fader walks: a divider's parts, the notes of
	the one list otherwise, nought with neither. */
	int cellCount(const Mapping& m) const { return m.scale.divides() ? m.scale.divs : count; }
	/** A divider's part under a fader: the travel shared among the parts as
	a roll shares it among its notes, the bottom the first line and the top
	the last, so eight parts are eight equal cells of the whole fader and ten
	volts, the next turn, is never reached. An integer from the one place the travel meets the parts. */
	static int partOf(const Mapping& m, float x) { return cellOf(m, x, m.scale.divs); }
	/** Which of `n` cells a travel falls in, counted along the notes low to
	high: the travel's equal share each, the way the screen draws them, and
	turned over with the window. */
	static int cellOf(const Mapping& m, float x, int n) {
		const float t = m.span() < 0.f ? 1.f - clamp(x, 0.f, 1.f) : clamp(x, 0.f, 1.f);
		return clamp((int)std::floor(t * (float)n), 0, n - 1);
	}
	/** The travel at the middle of cell `i` of `n`, counted along the notes
	low to high as `cellOf` counts them: where a fader placed on a cell
	stands, and turned over with the window. */
	static float cellTravel(const Mapping& m, int i, int n) {
		const float t = ((float)i + 0.5f) / (float)n;
		return m.span() < 0.f ? 1.f - t : t;
	}
	/** Volts for an inner value, before the shift after the quantizer. A
	quantized fader goes by its cells, one note to an equal share of the
	travel, as its screen draws them, so the pointer is always inside the note
	it plays; snapping by volts to the nearest note gave the end notes half a
	cell and uneven patterns uneven ones. */
	float raw(const Mapping& m, float x) const {
		// A volt divider's fader walks its parts, an equal share of the
		// travel each, and gives the line of the one it is in.
		if (m.scale.divides())
			return m.divider().line(partOf(m, x));
		if (!m.scale.any())
			return m.bottom() + clamp(x, 0.f, 1.f) * m.span();
		return (float)rawNote(m, x) / 12.f;
	}
	/** The note under a quantized fader, in semitones from C4, before the
	shift after the quantizer: an integer from the one list, never a rounded
	voltage. */
	int rawNote(const Mapping& m, float x) const { return notes[cellOf(m, x, count)]; }
	/** The note a quantized fader sounds, in semitones from C4: the note
	under it moved by the shift after the quantizer along the pattern
	(`shiftAfterSnap`). The one integer a CV lights on its quantizer. */
	int noteOf(const Mapping& m, float x) const {
		return shiftAfterSnap(m.scale, rawNote(m, x), m.postShift);
	}
	/** A quantized fader's voltage named: on a keyboard the key it was
	snapped to, the integer source, and the cents of the voltage from that
	key, rounded once, so a key detuned +60 reads E3 +60c rather than the F3
	-40c the nearest note would make of it. Where no key is known, a roll's
	chord an Atn spreads off its notes, or a jack standing more than a key's
	cents off it, the nearest note (`tunedOf`). The one rule the screen
	and the tooltip read a quantized fader by. */
	Tuned tunedAt(const Mapping& m, float x, float volts) const {
		if (m.scale.any() && !m.scale.spread) {
			const int k = noteOf(m, x);
			const long c = std::lround((double)volts * 1200.0) - (long)k * 100;
			if (c >= -99 && c <= 99)
				return {k, (int)c};
		}
		return tunedOf(volts);
	}
	/** Volts for an inner value: the note, moved by the shift after the
	quantizer along the pattern (see `shiftAfterSnap`). */
	float map(const Mapping& m, float x) const {
		if (m.scale.any() && m.scale.spread)
			return rollVolts(m, noteOf(m, x));
		if (m.scale.any())
			return m.scale.tunedVolts(noteOf(m, x)); // detuned by the key's cents, a microtonal keyboard's
		return raw(m, x);
	}
	/** A roll's note as an Atn held over it plays it: the chord scaled about
	its lowest note, the anchor a musician hears a chord from, so x nought
	collapses it onto that note and x two doubles every interval above it;
	then moved by the cents. The roll's own note where no Atn is. */
	float rollVolts(const Mapping& m, int note) const {
		if (m.rollGain == 1.f && m.rollCents == 0)
			return (float)note / 12.f;
		const int lo = count > 0 ? notes[0] : note;
		return ((float)lo + m.rollGain * (float)(note - lo)) / 12.f + (float)m.rollCents / 1200.f;
	}
	/** The voltage whose note, shifted, sounds `volts`: the way back, for a
	voltage arriving that has to be placed on the fader. */
	static float unshift(const Mapping& m, float volts) {
		return shiftAfterSnap(m.scale, volts * 12.f, m.postShift, true) / 12.f;
	}
	/** Where in the travel the output sits, nought to one: the middle of
	the cell the fader is in, or the value itself. */
	float level(const Mapping& m, float x) const {
		if ((m.scale.divides() || m.scale.any()) && m.span() != 0.f) {
			const int n = cellCount(m);
			const int i = clamp((int)std::floor(clamp(x, 0.f, 1.f) * (float)n), 0, n - 1);
			return ((float)i + 0.5f) / (float)n; // a cell is a cell either way up
		}
		return clamp(x, 0.f, 1.f);
	}
	/** Where the output is nought, C4, as a fraction of the travel: the one
	reference a CV's screen marks, wherever the window and the notes have
	been shifted to, the voltage that sounds C4 with the shift after the
	quantizer counted back. `inside` says whether it is within the travel at
	all; when it is not the fraction is the end it lies beyond, nought or
	one, so a bar grows from the edge nearest to where nought went. */
	float nought(const Mapping& m, bool* inside) const {
		const float span = m.top() - m.bottom();
		// The voltage that sounds C4: under a transpose by degrees, the
		// window's note moved onto it, taken back along the pattern.
		float target = unshift(m, 0.f);
		if (m.scale.degrees != 0 && m.scale.any() && !m.scale.spread)
			target = (float)degreeMoved(m.scale, (int)std::round(target * 12.f), true) / 12.f;
		float f = 0.5f;
		if (span != 0.f)
			f = (target - m.bottom()) / span;
		else if (m.bottom() != target)
			f = m.bottom() > target ? 0.f : 1.f;
		*inside = f >= 0.f && f <= 1.f;
		return clamp(f, 0.f, 1.f);
	}
	/** The travel a voltage arriving at the CV means, placed in the window
	the CV is in: the window's bottom is nought and its top one, and past
	either end the travel is held there, so what an Atn's top or offset made
	of the window is what the jack takes, a Qnt snaps it to its notes as
	it does the fader, and what the screen shows is what the jack gives. */
	float travelOf(const Mapping& m, float volts) const {
		if (m.span() == 0.f)
			return 0.5f;
		// Over a keyboard the jack goes by pitch: the note of the window
		// nearest the voltage, the boundary exactly halfway between two
		// neighbours and the upper one at a dead tie, and the travel is the
		// middle of that note's cell; placed by volts and rounded to a
		// semitone first, C4 caught a semitone and D4 two. A
		// roll goes by pitch as a keyboard does, its fader sharing the
		// travel among its notes but its jack taking the note nearest the
		// volts, so a CV
		// cabled from another on the same roll plays the same note.
		if (m.scale.any()) {
			// The list runs low to high, so the nearest is the last note at
			// or under the volts or the first over them, found by halving,
			// the upper at a tie and the last of any notes the same.
			const int n = count;
			const float want = volts * 12.f;
			int lo = 0, hi = n; // the first note over the volts, by halving
			while (lo < hi) {
				const int mid = (lo + hi) / 2;
				if ((float)notes[mid] > want)
					hi = mid;
				else
					lo = mid + 1;
			}
			int best = lo - 1;
			// A jack carrying no number takes the first note and an endless
			// one the last, as a walk up the notes would leave them.
			if (!std::isfinite(want))
				best = std::isnan(want) ? 0 : n - 1;
			else if (lo < n && (best < 0 || (float)notes[lo] - want <= want - (float)notes[best])) {
				best = lo;
				while (best + 1 < n && notes[best + 1] == notes[best])
					best++;
			}
			return cellTravel(m, best, n);
		}
		// A divider takes a voltage arriving at the cell of the line it
		// stands on, held at the first part and the last past either end.
		if (m.scale.divides()) {
			const int n = m.scale.divs;
			return cellTravel(m, clamp(m.divider().band(volts), 0, n - 1), n);
		}
		return clamp((volts - m.bottom()) / m.span(), 0.f, 1.f);
	}
};

/** The signal, as one node hands it on to its right: a pitch in semitones,
or a CV's voltage still carrying the window it was made in, and the
colour of whoever put it there. */
struct Signal {
	bool present = false;
	float semis = 0.f;
	int color = -1;
	bool mapped = false; // it came from a CV and `map` describes it
	Mapping map;
	// The voltage it was set from, kept exactly: twelve times a voltage over
	// twelve is not always that voltage, and a divider's line read back from
	// semitones fell a float under the line and into the band below. Good
	// while `semis` is still twelve times it, which a step that moves `semis`
	// itself breaks, so it is asked by `voltsNow` and never read alone.
	float volts = 0.f;
	bool inVolts = false;
	float voltsNow() const { return inVolts && semis == volts * 12.f ? volts : semis / 12.f; }

	/** Whether it is a pitch: anything but a CV's voltage that no
	quantizer has touched. */
	bool musical() const { return present && (!mapped || map.quantized()); }
	/** Whether it is a CV's plain voltage, still carrying its window. */
	bool voltage() const { return present && mapped && !map.quantized(); }
	/** Where a plain voltage sits in its window, nought to one. */
	float travel() const {
		const float span = map.span();
		return span != 0.f ? clamp((semis / 12.f - map.bottom()) / span, 0.f, 1.f) : 0.f;
	}
	void set(float s, int c) {
		semis = s;
		present = true;
		color = c;
		mapped = false;
		inVolts = false;
	}
	/** A voltage that is a pitch no longer carrying a window, a divider's
	line, kept exactly as `setVolts` keeps its own. */
	void setLine(float v, int c) {
		set(v * 12.f, c);
		volts = v;
		inVolts = true;
	}
	void setVolts(float v, int c, const Mapping& m) {
		set(v * 12.f, c);
		map = m;
		mapped = true;
		volts = v;
		inVolts = true;
	}
	bool operator==(const Signal& o) const {
		return present == o.present && semis == o.semis && color == o.color && mapped == o.mapped && map == o.map &&
		       inVolts == o.inVolts && (!inVolts || volts == o.volts);
	}
	bool operator!=(const Signal& o) const { return !(*this == o); }
};

/** What a driver sends the Seqs of its rig, as if each were cabled to it:
pulses, resets and the tempo. A Clock is a dispatch centre of the three; a Seq's
clock jack is read into the same shape (`TrigCounter`). The pulses and the
resets are serials, so a reader counts what came since it last looked and
never asks when; nothing to the right of a driver keeps time of its own beyond
the ramp a Seq runs between pulses at `period`. */
struct Count {
	bool present = false;
	const tree::Node* source = nullptr; // the driver's identity: a Seq tells a new one by the pointer
	/** Serial of the pulses sent, one a tick, several on one frame when a
	jump crossed several (a change of division on an early beat). */
	uint32_t pulses = 0;
	/** Serial of the resets sent: the Clock's RESET jack and the bracket's RESET.
	A Seq takes the rig's reset whatever clocks it, its own cable included. */
	uint32_t resets = 0;
	/** The expected spacing of the pulses in frames, nought for not known: a
	Clock's from its tempo and its multiplier, or from the beats it follows; a
	Seq's clock jack the frames between its last two edges. The tempo a Seq
	ramps at, the rig Clock's ruling every Seq in its rig, one on its own
	cable included. */
	double period = 0.0;
	/** The driver is running: its pulses are coming, so a Seq ramps toward
	the next and one born under it sounds step one at once. A Clock stopped,
	or following a cable that has brought no beat since it arrived or since
	a reset, is not; a Seq's clock jack always is. */
	bool running = false;
	/** The frame the latest pulse was sent on, -1 for none: what lands a
	Seq's catching up on the frame it belongs to, one back from a bypass. It
	moves only with `pulses`, so it is no part of a change. */
	int64_t lastFrame = -1;
	/** How the latest reset meets the pulses, decided by the Clock once for
	every Seq it reaches, its own cable's included: `resetAfter` is the serial
	of the pulses sent before it, and `resetBinds` says it belongs to the last
	of them, made within the window before it, so a Seq whose own pulse's
	decision is still open makes that landing step one; otherwise it comes
	before the next pulse, and a Seq arms whatever its own window says
	(`Writer::arm`). Decided once rather than by each Seq's own window,
	because a Seq on the CLK cable hears every pulse a sample after the rig,
	so a RESET a sample past the window would bind for it alone and part the
	two for good. */
	uint32_t resetAfter = 0;
	bool resetBinds = false;
	/** A Clock in hertz: the part of the latest pulse's frame that falls
	after the tick it was due at, nought to one, the pulse made on the frame
	its tick falls in. A Seq starts its ramp that far on and puts its edge
	there, so a cycle that is no whole number of frames keeps its length
	sample by sample rather than on average. Nought for every other driver.
	It moves only with `pulses`, so it is no part of a change. */
	double after = 0.0;
	/** And whether it is in hertz, where a Seq it drives band-limits its
	edges unless its own BLEP is off. */
	bool hertz = false;
	/** And, in hertz, whether its ticks are a cable's edges it follows: a
	Seq it drives then keeps the window a Seq on that cable keeps, so a
	reset sent to both lands them together, rather than an oscillator's one
	frame. */
	bool cabled = false;
	/** A Clock in hertz: how far its PHASE jack has turned the cycle, in
	cycles, a whole and a fraction nought up to one, beside the ticks the
	frequency makes and never one of them. Every Seq it drives adds it to its
	walk as a cable in its own PHASE turns it, so a turn run backwards walks
	the rig back, through nought (`Seq::clockTurn`). The ticks of a cycle are
	`tickN` over `tickD`, the Clock's rate. Nought with nothing in PHASE; no
	part of any other driver. */
	int64_t turnWhole = 0;
	double turnFrac = 0.0;
	/** And whether a cable is in that PHASE: a turn, nought volts included,
	so a Seq it drives measures an armed walk's turn from step one, as a
	cable in its own PHASE does (`Seq::evaluate`, the arm). */
	bool phased = false;
	int tickN = 1, tickD = 1;

	bool operator==(const Count& o) const {
		return present == o.present && source == o.source && pulses == o.pulses && resets == o.resets &&
		       period == o.period && running == o.running && hertz == o.hertz && cabled == o.cabled &&
		       resetAfter == o.resetAfter && resetBinds == o.resetBinds && turnWhole == o.turnWhole &&
		       turnFrac == o.turnFrac && phased == o.phased && tickN == o.tickN && tickD == o.tickD;
	}
	bool operator!=(const Count& o) const { return !(*this == o); }
};

// ---- The rules on a Signal and a Mapping ------------------------------------

/** An offset's shift in the unit the rig is in: semitones on a pitch, and
with no signal semitones when a quantizer is in reach, else volts. */
inline float shiftOf(const Signal& s, const Mapping& win, int semis, float volts) {
	const bool musical = s.present ? s.musical() : win.musical();
	return musical ? (float)semis : volts * 12.f;
}
/** An offset as a reader's step on its cable: shifted, keeping a plain
voltage's window shifted with it; with nothing arriving the signal stays
absent and the shift of nought is returned for the jack. */
inline float transStage(Signal& s, const Mapping& win, int semis, float volts) {
	const float shift = shiftOf(s, win, semis, volts);
	if (!s.present)
		return shift;
	// A pitch past a quantizer moves along its pattern, as a CV's note does,
	// by the whole semitones the offset is.
	if (s.musical() && !s.voltage() && win.quantized()) {
		s.semis = shiftAfterSnap(win.scale, s.semis, semis);
		return s.semis;
	}
	s.semis += shift;
	if (s.voltage())
		s.map.preShift += volts;
	return s.semis;
}
/** An Atn as a reader's step on its cable: a plain voltage and the
window it carries are multiplied by the top over ten volts, about nought; a
pitch passes untouched. Returns what the jack shows, in semitones. */
inline float rangeStage(Signal& s, float top) {
	if (!s.present)
		return 0.f;
	if (s.voltage()) {
		const float k = top / CV_VOLTS;
		s.semis *= k;
		s.map.reach *= k;
		s.map.preShift *= k;
	}
	return s.semis;
}
/** A quantizer as a reader's step on its cable: the arriving pitch
snapped to the scale, or nought snapped with nothing arriving, which leaves
the signal absent. `value` gets the key sounded, -1 with no signal. */
inline float quantStage(Signal& s, const Scale& scale, int* value, int* bandOut = nullptr, int* noteOut = nullptr) {
	// A volt divider snaps the voltage to its line, a whole band each, no
	// note at all: the band is handed out as the integer it was snapped by
	// (`bandOut`), for a reader to light, never named again from the
	// semitones, which at seven parts put 1.4286 V, 17.14 semitones, on 17
	// and the band under it.
	if (scale.divides()) {
		const Divider d = Divider::of(scale);
		const int band = d.band(s.present ? s.voltsNow() : 0.f);
		const float line = d.line(band);
		if (value)
			*value = s.present ? 0 : -1;
		if (bandOut)
			*bandOut = s.present ? band : NO_BAND;
		if (s.present)
			s.setLine(line, s.color);
		return 12.f * line;
	}
	// A keyboard transposed by degrees moves the note it snapped to along
	// the pattern (`degreeMoved`).
	const int semis = scale.spread && scale.any() ? nearestSpread(scale, s.present ? s.semis : 0.f)
	                  : scale.any()               ? degreeMoved(scale, nearestInScale(s.present ? s.semis : 0.f, scale))
	                                              : (int)std::round(s.present ? s.semis : 0.f);
	if (value)
		*value = s.present ? scale.rowOf(semis) : -1;
	// The note snapped to as a whole semitone, for whoever lights it, since
	// a note detuned by its key's cents may stand nearer the next one.
	if (noteOut)
		*noteOut = semis;
	const float tuned = scale.tunedSemis(semis);
	if (s.present)
		s.set(tuned, s.color);
	return tuned;
}
/** An offset on the window: volts and a pending turn with no
quantizer in reach, semitones after the snap with one. */
inline void shapeOffset(Mapping& m, int semis, float volts) {
	if (m.quantized())
		m.postShift += semis;
	else if (m.quantNode)
		m.preShift += (float)semis / 12.f;
	else
		m.preShift += volts;
}
/** An Atn on the window: the reach is its top, either side of nought, and
whatever reach and shift an Atn before it left are replaced. An Atn shapes
everything to its right until the next Atn replaces it, through a Qnt as
through anything; multiplying what arrived instead compounds the Atns before
it, and nothing could see past a Qnt to stop that. */
inline void shapeAtn(Mapping& m, float top) {
	m.reach = top;
	m.preShift = 0.f;
	m.postShift = 0;
	m.landSemis = 0;
	m.spanned = false;
	m.rollGain = 1.f;
	m.rollCents = 0;
	m.divOrigin = 0.f;
	m.divGain = 1.f;
	m.divCentre = 0.f;
	m.divScale = 1.f;
}

/** An Atn on a window with a scale in reach: the window laid onto the
span, its foot on the span's foot and its top on the span's top, or the
other way up below the line. The same replacement, in notes. */
inline void shapeAtnNotes(Mapping& m, float top) {
	float lo, hi;
	noteSpan(top, m.scale, lo, hi);
	shapeAtn(m, (top < 0.f ? -1.f : 1.f) * (hi - lo) / 2.f);
	m.preShift = (hi + lo) / 2.f;
	m.spanned = true;
}

// ---- The faces ---------------------------------------------------------------

/** A value taken into the step a playhead stands on, from the Seq's IN jack:
up to as many channels as a Trg has rings, whether to add to what the step
held (a fader) or turn it over (a ring), or only light a ring and leave the
rest alone (a REC take latching through its step), and the volts a ring counts
as lit from. */
struct Taken {
	float v[TRG_RINGS] = {};
	int n = 1;
	bool add = false;
	bool flip = false;
	bool light = false;
};

/** What a playhead standing on a member sounds: a voltage, a trigger mask
over the member's rings, or a chord; the signal it makes of the reader's
cable and the window it hands on. */
struct Sound {
	enum Type { VOLTS, TRIG, CHORD };
	// VOLTS under a volt divider: the band sounded, the integer the divider
	// snapped by (`Divider::band`), which a reader lights rather than `note`;
	// `NO_BAND` for anything else.
	int band = NO_BAND;
	float volts = 0.f; // and its voltage exactly, which the jack gives while `semis` is still twelve times it
	Type type = VOLTS;
	float semis = 0.f; // VOLTS: the jack, in semitones
	int mask = 0;      // TRIG: the rings lit under the playhead
	Scale chord;       // CHORD: the keyboard to lay out
	int note = 0;      // VOLTS: the note sounded, in semitones from C4, an integer at its source (`MemberFace::litSemis`)
	int value = -1;    // what the member sounded, for its screen: a Qnt's key, a Trg's ring
	// VOLTS: the voices past the first, in volts and a divider's line as the
	// line itself, as `volts` is the first's, where the member carries a
	// chord on a cable (`semis` is the first); one voice else.
	int voices = 1;
	float voice[PORT_MAX_CHANNELS] = {};
	Signal signal;
	Mapping window;
};

/** The face of a cartridge: everything a playhead or a Poly asks of a
member, with no kind named. */
struct MemberFace {
	virtual ~MemberFace() {}
	/** How many steps this member is to a playhead: one, or a Trg's rings. */
	virtual int stepsOf(bool across) { return 1; }
	/** The param that sets how many steps this member is, a Trg's ring
	count, as a whole number of them; -1 for a member that is always one. */
	virtual int countParam() const { return -1; }
	/** Standing on sub-step `sub` with `in` arriving: what is sounded. */
	virtual void sound(int sub, const Signal& in, int color, Sound& out) = 0;
	// `out` arrives fresh, a Sound as it is made, and a member writes only
	// what it sounds.
	/** Where in the step the reader in `lane` stands, nought as it lands
	and one as it leaves, said just before `sound`: a member that is a
	stretch rather than a point, a CRV, sounds the place it is told, and
	every other member is a point and has no use for it. */
	virtual void standsAt(int lane, float along) {}
	/** The volts a stretch sounds at `along`, asked without standing there
	and touching nothing: a Seq at audio rate places its edges and its
	oversampled points between two samples by it (`Seq::bandLimit`). False
	for a point, whose step holds the one value it sounded. */
	virtual bool voltsAlong(float along, float& v) const { return false; }
	/** A value taken into sub-step `sub`. */
	virtual void take(int sub, const Taken& t) {}
	/** A Seq's ROUTE, routed into this member while its playhead stands on
	it, `on` false once it has moved off: `n` voices, a mono cable one, from
	the Seq walking it in `lane` (`Routed`, every Seq's word its own). An
	Atn adds them to its own cable at its jack, a Qnt quantizes them there,
	so the member's jack and the Seq's agree; a member with no jack of that
	kind ignores it. */
	virtual void route(int lane, bool on, const float* volts, int n) {}
	/** Whether the mouse is held on this member, for a preview. */
	virtual bool holding() = 0;
	/** Whether a Seq's preview sounds this member: held, or a Qnt the
	pointer rests on. */
	virtual bool previewing() { return holding(); }
	/** At build: a playhead walking this member's block takes a lane;
	`across` when it walks the block a member a step, as a Seq set to Across
	walks Trgs, the one way a block is walked as a whole. */
	virtual int addLane(int color, bool across) = 0;
	/** At evaluate: a playhead stands here, in this lane and colour, and
	`value` is what it sounded through this member. */
	virtual void read(int lane, int color, int value) = 0;
	/** At evaluate: the playhead standing on sub-step `sub` here, the whole
	member when -1, struck it: a step began there and its trigger fired,
	which a Trg's bloom answers, and its rings' own jacks, a pulse each
	ending on frame `until`. `began` is the reader's serial of steps begun
	and `again` says the landing was re-aimed inside its window with no step
	begun, so a ring already fired for that serial in this lane is not fired
	twice, as the reader's own jack does not (one pulse, one decision). */
	virtual void struck(int lane, int sub, int64_t until, uint32_t began, bool again) {}
	/** At evaluate: the walk's own step stands here, before a PHASE turn
	carried the playhead on, in this lane: where the halo goes while the line
	goes where `read` says. A Seq with no turn anchors where it reads. `sub`
	is the step within me the walk stands on, as `sound` takes it: a Trg's
	ring, or -1 walked across; a member that is one step whole ignores it. */
	virtual void anchor(int lane, int sub) {}
	/** At evaluate, after `anchor`: how far through me, nought to one, the
	walk stands under a PHASE turn, -1 with no turn; for a member that is a
	stretch rather than a point, a CRV, whose halo stands on its curve there.
	A point ignores it. */
	virtual void anchorAt(int lane, float along) {}
	/** At evaluate: a Poly reads this member, in this colour, standing on
	no one step. */
	virtual void readBy(int color) = 0;
	/** The member's channels for a Poly, in the volts its own jack would
	carry. Returns how many. */
	virtual int bundle(float* v) = 0;
	/** The quantizer this member resolves through, to light a key on. */
	virtual const tree::Node* quantOf() = 0;
	/** The note this member sounds for sub-step `sub`, in semitones from
	C4, an integer at its source: a quantized CV's note from its list, a
	Qnt's snapped note. */
	virtual int litSemis(int sub) = 0;
	/** The divider's band this member sounds, the integer it was snapped by,
	or `NO_BAND` where no divider snapped it. */
	virtual int litBand(int sub) { return NO_BAND; }
};

/** The face of a quantizer: the one way a key is lit on it. Whoever sounds a
note through it, a playhead standing on a member, a source standing alone, a
Poly reading a block, the quantizer itself for its own cable, calls this with
the note as an integer and the colour to light it in, -1 for the quantizer's
own; the quantizer keeps one sounding set, cleared at the top of every wake
and published at its end, and its screen reads nothing else. */
struct QuantFace {
	virtual ~QuantFace() {}
	virtual void light(int semis, int color) = 0;
	/** The same for a voltage that is not a note: a volt divider's band,
	which a semitone is too coarse to name past a hundred and twenty parts. */
	virtual void lightVolts(float volts, int color) { light((int)std::round(volts * 12.f), color); }
	/** And a divider's band by its own integer, the one route a reader
	lights a band by, so nothing names it twice. */
	virtual void lightBand(int band, int color) {}
};

} // namespace rig
