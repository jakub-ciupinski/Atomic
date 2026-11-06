// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "plugin.hpp"
#include "Signal.hpp"
#include <cstdint>

/** The probes: what a module has to show the headless tests that its screens
read and its jacks do not carry. A module with something to show derives from
Probe beside its Member base and answers the questions it can; a test reaches it
through `probeOf`, and a module that is not a Probe answers every question with
the default. Nothing in the plugin asks these. */

namespace rig {

struct Probe {
	virtual ~Probe() {}
	/** A quantizer's key as its screen reads it, for the tests: the colour a
	reader lit it in plus one, nought for unlit. */
	virtual int keyLit(int key) { return 0; }
	/** A Qnt's volt divider: the colour + 1 its band is lit in, nought when dark. */
	virtual int bandLit(int band) { return 0; }
	/** And the readers sounding a band, for the tests: the colour of each, into
	`cols`, up to four, as `keyHeads` gives a key's. Returns how many. */
	virtual int bandHeads(int band, int* cols) { return 0; }
	/** The playheads standing on a quantizer's key, for the tests: the colour
	of every reader sounding it, into `cols`, up to four. Returns how many. */
	virtual int keyHeads(int key, int* cols) { return 0; }
	/** Whether a member's screen dims itself, for the tests: one where it
	does, nought where it stands bright, minus one for a module with no such
	rule. The screens ask `claimedNow` and `liveNow` for it. */
	virtual int dimmedOut() { return -1; }
	/** The group a face wears by the colour scheme, for the tests: a palette
	index, minus one for none (the cable's, white or yellow then), minus two
	for a module with no such face. A Qnt's divider count asks it. */
	virtual int groupOut() { return -2; }
	/** An Atn's meter as its screen reads it, for the tests: what lane `lane`
	reads and where the bar stands; false while it has nothing to meter. */
	virtual bool meterOut(float& level, float& anchor, int lane = 0) { return false; }
	/** How many lanes an Atn's meter is divided into, a channel each of
	what its jack carries; nought for a module with no meter. */
	virtual int meterLanes() { return 0; }
	/** What a fader's screen is drawn from, for the tests: the window it is in
	and its travel, nought to one. */
	virtual bool faderOut(Mapping& win, float& travel) { return false; }
	/** The window this member read this wake, for the tests: what arrived
	through its window slot before it did anything to it. */
	virtual bool windowIn(Mapping& win) { return false; }
	/** An Atn's job this wake, for the tests (`Atn::Job`); minus one from
	anything that is not an Atn. */
	virtual int jobOut() { return -1; } // an Atn's: 0 a shaper, 1 a VCA, 2 a step
	/** An Atn's right arrow as its widget reads it, for the tests: one where
	it shapes something to its right, two where a window slot holds it, four
	where it is a step that shapes; minus one from anything not an Atn. */
	virtual int arrowOut() { return -1; }
	/** A Poly's bar, for the tests: the held level of channel `c`, or nought past its count. */
	virtual float barOut(int c) { return 0.f; }
	/** The same bar as its screen draws it, the span from `foot` to `top`
	(`HeldSpan`); false from anything with no bars. */
	virtual bool barSpanOut(int c, float& foot, float& top) { return false; }
	/** A Clock's tempo readout, for the tests: the tempo it names, minus one
	while it waits for a clock to measure, minus two from anything else. */
	virtual int tempoOut() { return -2; }
	/** And in hertz its frequency readout, unrounded: minus one while it
	waits for a cable's cycle, minus two from anything else. */
	virtual double hzOut() { return -2.0; }
	/** A Clock's colour as it is drawn, for the tests: a palette index, minus
	one from anything else. */
	virtual int drawnOut() { return -1; }
	/** A hand on a driven Seq's step screen, as its widget makes one, for
	the tests: held down on `step`, or let go there after a drag. False from
	anything that is not a Seq. */
	virtual bool holdStep(int step, bool down) { return false; }
	/** The member a Seq's playhead stood on at its last wake, for the tests:
	the one whose setting it sounds. Null from anything else. */
	virtual const Module* stoodOut() { return nullptr; }
	/** An Atn's span as its Range tooltip and glass name it, for the tests:
	whether it reads in notes, and the notes the span ends on. */
	virtual bool spanOut(bool& notes, int& foot, int& top) { return false; }
	/** A Trg's bloom on ring `ring` as its screen reads it, for the tests:
	one as a playhead strikes the ring lit, fading to nought; minus one
	from anything that is not a Trg. */
	virtual float flashOut(int ring) { return -1.f; }
	/** The playheads a Trg's ring `ring` draws, as its screen reads them,
	for the tests: their colours, and whether each one's line and halo stand
	there; minus one from anything that is not a Trg. */
	virtual int ringHeadsOut(int ring, int* cols, bool* line, bool* halo) { return -1; }
	/** And the playheads a Poly's row `r` draws, the Seqs arpeggiating it
	standing on the channel that row gives; minus one from anything that is
	not a Poly. */
	virtual int rowHeadsOut(int r, int* cols, bool* line, bool* halo) { return -1; }
	/** The halos a CRV's screen draws on its curve, nought to one along it,
	and their colours: where a Seq's walk stands under a PHASE turn that
	carried the light elsewhere; minus one from anything not a CRV. */
	virtual int curveHalosOut(float* along, int* cols) { return -1; }
	/** And its lights, where the curve is being read, nought to one along
	it; minus one from anything not a CRV. */
	virtual int curveLightsOut(float* along) { return -1; }
	/** And what its screen shows its jack giving, the curve where its own
	light stands or else its start; false from anything not a CRV. */
	virtual bool curveJackOut(float& v) { return false; }
	/** A click on a Trg's pulse button `ring`, as the hand makes it, for
	the tests: nothing from anything else, or from a Trg whose rings toggle. */
	virtual void askPulse(int ring) {}
	/** A Seq's screens standing idle, as they draw and refuse a stroke, for
	the tests: one for the step screen, two for the rate's, minus one from
	anything that is not a Seq. */
	virtual int idleOut() { return -1; }
	/** A Seq's mode button in ONCE, as it draws, for the tests: the places of
	the cycle dark, the whole cycle armed or spent, minus one in any other
	mode or from anything that is not a Seq. */
	virtual int onceOut() { return -1; }
};

/** The probe a module answers, or one that answers every question with its
default. */
inline Probe& probeOf(Module* m) {
	static Probe none;
	Probe* p = dynamic_cast<Probe*>(m);
	return p ? *p : none;
}

} // namespace rig
