// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/** An Atn's engine, its quantities, its meter and the module itself. A part
of Atn.cpp in a file of its own: Atn.cpp includes it once, inside its
anonymous namespace, and it is not a header to include anywhere else. */

/** How far past the fader's end a gain can be boosted: ten times, a hundred
volts' worth of gain. The fader stops at its end; a stroke begun with the gain
already at an end goes on past it, and one let go back inside is capped
again. */
const float GAIN_BOOST = 10.f;
const float GAIN_MAX = 10.f * GAIN_BOOST;

/** The gain as the bar reads it: how far it reaches in a scale when there
is one, and volts otherwise. */
struct GainQuantity : HintQuantity {
	Atn* atn = nullptr;
	std::string getDisplayValueString() override;
	void setDisplayValue(float v) override;
	void setDisplayValueString(std::string s) override;
	/** Randomized on the fader, never into the boost. */
	void randomize() override {
		if (isBounded())
			setValue(math::rescale(random::uniform(), 0.f, 1.f, -10.f, 10.f));
	}
};

/** The red line reads exactly as an Ofs's bar does, because it is one. */
struct OfsQuantity : HintQuantity {
	Atn* atn = nullptr;
	std::string getDisplayValueString() override;
	void setDisplayValue(float v) override;
	void setDisplayValueString(std::string s) override;
};

// What the two jacks on the glass do, which is not the same thing: the gain's
// scales the fader and stops there, the offset's adds to it, at the
// volt-per-octave standard where a scale is in reach and a volt a volt where
// none is. The gain's is said in the constructor and both again whenever the
// tooltip is rewritten. The gain's jack names its lane and says what 0 V
// leaves of it (`gained`: the knob times the jack over ten, held to ±1), which
// is not nothing: the offset alone in volts, one note in notes or over a roll,
// the lines or the series all on the offset over a divider.
const char* const GAIN_JACK = "Scales the gain: 10 V keeps it as set, 0 V leaves only the offset, and below 0 V inverts it.";
const char* const GAIN_JACK_RANGE = "Scales the range: 10 V keeps it as set, 0 V closes it to one note, and below 0 V turns it over.";
const char* const GAIN_JACK_SPREAD = "Scales the spread: 10 V keeps it as set, 0 V plays one note, and below 0 V turns the chord over.";
const char* const GAIN_JACK_SPAN = "Scales the span: 10 V keeps it as set, 0 V closes it onto the offset, and below 0 V turns it over.";
const char* const OFS_JACK = "1V/oct, added to the transpose.";
const char* const OFS_JACK_KEYS = "1V/oct, added to the transpose and snapped to the scale.";
/** The lanes' gestures (Words.hpp): in volts, shift lands on whole volts
and command slows the stroke, with shift slower still; in notes, shift lands
on octaves and command is the plain stroke; over a roll, command is cents.
Each shift piece names its lane and says what the handle then reads:
the gain's whole volts are its handle's steps of 0.1, the offset's are
whole volts on its own handle. */
inline const Gesture G_LANE_GAIN_STEPS = declare(On::Drag, Mod::Shift, Rank::Known, RACK_MOD_SHIFT_NAME "-drag: gain by 0.1");
inline const Gesture G_LANE_SPREAD_STEPS = declare(On::Drag, Mod::Shift, Rank::Known, RACK_MOD_SHIFT_NAME "-drag: spread by 0.1");
inline const Gesture G_LANE_SPAN_STEPS = declare(On::Drag, Mod::Shift, Rank::Known, RACK_MOD_SHIFT_NAME "-drag: span by 0.1");
inline const Gesture G_LANE_OFFSET_VOLTS = declare(On::Drag, Mod::Shift, Rank::Known, RACK_MOD_SHIFT_NAME "-drag: offset on whole volts");
/** In notes the shift piece names its lane's job, the range's or the
transpose's, never the unit alone, as a QNT's transpose says the same. */
inline const Gesture G_LANE_RANGE_OCTAVES = declare(On::Drag, Mod::Shift, Rank::Known, RACK_MOD_SHIFT_NAME "-drag: range by octaves");
inline const Gesture G_LANE_OCTAVES = declare(On::Drag, Mod::Shift, Rank::Known, RACK_MOD_SHIFT_NAME "-drag: transpose by octaves");
inline const Gesture G_LANE_FINE = declare(On::Drag, Mod::Command, Rank::Known, RACK_MOD_CTRL_NAME "-drag: fine");
inline const Gesture G_LANE_CENTS = declare(On::Drag, Mod::Command, Rank::Known, RACK_MOD_CTRL_NAME "-drag: transpose by cents");
inline const Gesture G_LANE_FINER = declare(On::Drag, Mod::ShiftCommand, Rank::Rare, RACK_MOD_SHIFT_NAME "+" RACK_MOD_CTRL_NAME "-drag: finer");
/** The same keys on a transpose in cents, words only: command there is
already cents, and with shift the stroke is the same cents at a tenth of
the pace (`AtnLane::paceOf`), so "finer" would promise a unit there is not. */
inline const Gesture G_LANE_CENTS_SLOWER =
    declare(On::Drag, Mod::ShiftCommand, Rank::Rare, RACK_MOD_SHIFT_NAME "+" RACK_MOD_CTRL_NAME "-drag: cents, slower");
/** The boost: a gain stroke begun at either end of its fader and carried
past it goes on to x10 (`AtnLane::setFromY`). Invented, so it is the line's
first piece, what is hard to guess said first: "the stop" names the
fader's end without saying which, true at both ends, from -1 boosting
inverted, as a tap's stop is pushed past, and "boost" is the musician's
word. */
inline const Gesture G_LANE_BOOST = declare(On::Drag, Mod::None, Rank::Invented, "Drag past the stop: boost");
/** A gain lane in volts, by its name's shift piece: whole volts, the boost,
fine and finer. */
inline Gestures laneGain(const Gesture& steps) { return {&steps, &G_LANE_BOOST, &G_LANE_FINE, &G_LANE_FINER}; }
inline Gestures laneOffset() { return {&G_LANE_OFFSET_VOLTS, &G_LANE_FINE, &G_LANE_FINER}; }
/** A plain drag leaving the glass sideways sets the same lane of every ATN
of the run the pointer crosses (`LevelParam::strokeMove`), a reach past
the part (`Rank::Block`), said wherever another ATN stands in the run (`sweepUi`), named by the lane's own
word. */
inline const Gesture G_LANE_SWEEP_GAIN = declare(On::Drag, Mod::None, Rank::Block, "Drag across: draw the gains");
inline const Gesture G_LANE_SWEEP_RANGE = declare(On::Drag, Mod::None, Rank::Block, "Drag across: draw the ranges");
inline const Gesture G_LANE_SWEEP_SPREAD = declare(On::Drag, Mod::None, Rank::Block, "Drag across: draw the spreads");
inline const Gesture G_LANE_SWEEP_SPAN = declare(On::Drag, Mod::None, Rank::Block, "Drag across: draw the spans");
inline const Gesture G_LANE_SWEEP_OFFSET = declare(On::Drag, Mod::None, Rank::Block, "Drag across: draw the offsets");
inline const Gesture G_LANE_SWEEP_TRANSPOSE = declare(On::Drag, Mod::None, Rank::Block, "Drag across: draw the transposes");
/** `gs` with the sweep in front where it reaches another ATN. */
inline Gestures laneSwept(Gestures gs, bool sweeps, const Gesture& sweep) {
	if (sweeps)
		gs.insert(gs.begin(), &sweep);
	return gs;
}
const char* const OFS_JACK_VOLTS = "Added to the offset, volt for volt.";

/** The Atn's meter, in a form a module can hold rather than be: what its
own jack carries, as a screen's hold on it (`Held`, Pulse.hpp) that follows
what arrives and falls back over `SHOW_FALL_SECONDS`, so a CV reads as a
level, an audio signal as its swing, and a trigger, a millisecond or a
single sample, as a blink, where the value at the look would never show a
trigger between two looks. Written every sample, read by the screen; it wakes
nothing, being a screen value and no part of what the rig computes.

And what it is a meter OF, which is not one thing. A CV wants its own value
read off the lane the way a fader is: a control voltage is a thing to
read, not a thing to watch flicker. An audio signal has no value worth
reading at any one instant and wants a level. So the meter asks which it is
looking at and answers accordingly, and it asks by counting: a signal that
crosses its own mean more than a few dozen times a second is audio and
nothing else is. A CV, however fast it is swept by hand or by an envelope,
does not cross its own mean twenty times a second and keep doing it.

The owner feeds it the value every sample (`follow`), and on the control
period the anchor, where the bar stands rather than where nought is: the
offset alone at the gain in force, so a channel lifted three
volts shows its signal running from three and not from nothing, the zero
crossing of what actually arrives (`mark`). The atomics are the screen's
and a headless test's; a measurement made at the sample rate, stored where it is
measured. */
struct AtnMeter {
	/** One channel's meter: how far from the anchor the signal reaches,
	signed, held (the level is the anchor plus it, the peak its size, every
	sample seen from the anchor of the last look), and whether it is audio,
	each channel asked on its own, so a poly jack carrying a CV beside an
	audio voice reads each as what it is. */
	struct Lane {
		Held held;
		float dc = 0.f;       // the signal's own slow mean
		int crossings = 0;    // of that mean, in the window
		bool audio = false;   // what the last window made of it
		float wasAbove = 0.f; // which side of the mean we were on
		void rest() {
			held.rest();
			dc = 0.f;
			wasAbove = 0.f;
			crossings = 0;
			audio = false;
		}
	};
	Lane lane[PORT_MAX_CHANNELS];
	/** How many channels are followed, one per lane of the screen's meter:
	the audio thread's,
	published as `lanesNow` at the look, after the lanes it counts. */
	int n = 1;
	float anchorWas = 0.f;
	int crossWindow = 0;  // samples into the window, every lane's alike
	std::atomic<float> peakNow[PORT_MAX_CHANNELS] = {};
	std::atomic<bool> audioNow[PORT_MAX_CHANNELS] = {};
	std::atomic<float> levelNow[PORT_MAX_CHANNELS] = {}; // the signal itself, for a CV
	std::atomic<int> lanesNow{1};
	// Whether there is anything to meter at all, for the screens: an Atn
	// with nothing passing drew a bar from its offset to nought, since
	// that is where a meter at rest reads. Published at the end of the
	// wake by the owner, with the rest of what the screens read.
	std::atomic<bool> meteringNow{false};
	std::atomic<float> anchorNow{0.f};

	/** Every sample, `count` channels of `v`: the mean, followed slowly, a
	fiftieth of a second, which is under every audio frequency and over
	every control one, so a CV takes its own mean with it and an audio
	signal swings about a mean that stands still; and the crossings of it,
	counted over a quarter of a second. Forty crossings a second is twenty
	hertz, which is where hearing begins and where a control voltage has
	long since stopped. A channel that arrives starts from nothing, so a
	lane that was last used minutes ago brings none of it back. */
	void follow(const float* v, int count, const Module::ProcessArgs& args) {
		count = clamp(count, 1, (int)PORT_MAX_CHANNELS);
		for (int c = n; c < count; c++)
			lane[c].rest();
		n = count;
		const float k = std::min(1.f, 50.f * args.sampleTime);
		for (int c = 0; c < n; c++) {
			Lane& l = lane[c];
			l.held.see(v[c] - anchorWas);
			l.dc += (v[c] - l.dc) * k;
			const float side = v[c] - l.dc;
			if (side > 0.02f || side < -0.02f) {
				const float now = side > 0.f ? 1.f : -1.f;
				if (l.wasAbove != 0.f && now != l.wasAbove)
					l.crossings++;
				l.wasAbove = now;
			}
		}
		if (++crossWindow >= (int)(args.sampleRate / 4.f)) {
			for (int c = 0; c < n; c++) {
				lane[c].audio = lane[c].crossings >= 10;
				audioNow[c].store(lane[c].audio, std::memory_order_relaxed);
				lane[c].crossings = 0;
			}
			crossWindow = 0;
		}
	}
	/** On the control period: every lane's level and peak, the peak of the
	swing about the anchor, not of the voltage, since what an audio meter
	says is how far the signal goes from where it sits, and where it sits
	is the offset; then the anchor and the count, the count last, so a
	screen that sees a new count sees its lanes already filled. */
	void mark(const float* v, float anchor, const Module::ProcessArgs& args) {
		anchorNow.store(anchor, std::memory_order_relaxed);
		// What was held was measured from the anchor it stood on: an
		// anchor that has moved, an offset turned or a rig formed, starts
		// the hold afresh rather than letting a swing about the old one
		// fall across the new.
		const bool moved = anchor != anchorWas;
		const float dt = (float)Cartridge::CONTROL_PERIOD * args.sampleTime;
		for (int c = 0; c < n; c++) {
			if (moved)
				lane[c].held.rest();
			const float m = lane[c].held.look(v[c] - anchor, dt, CV_VOLTS);
			levelNow[c].store(anchor + m, std::memory_order_relaxed);
			peakNow[c].store(std::fabs(m), std::memory_order_relaxed);
		}
		anchorWas = anchor;
		lanesNow.store(n, std::memory_order_release);
	}
	/** Whether the meter still has a mark to let fall. */
	bool falling() const {
		for (int c = 0; c < n; c++)
			if (lane[c].held.mark != 0.f)
				return true;
		return false;
	}
	/** Everything the meter shows, put down: a module that has stopped
	processing shows a still, empty column rather than the last thing it
	saw. */
	void rest() {
		for (int c = 0; c < PORT_MAX_CHANNELS; c++) {
			lane[c].rest();
			levelNow[c].store(0.f, std::memory_order_relaxed);
			peakNow[c].store(0.f, std::memory_order_relaxed);
			audioNow[c].store(false, std::memory_order_relaxed);
		}
		n = 1;
		anchorWas = 0.f;
		crossWindow = 0;
		anchorNow.store(0.f, std::memory_order_relaxed);
		lanesNow.store(1, std::memory_order_release);
	}
};

struct Atn : Cartridge, Probe {
	MEMBER_FOOTPRINT
	enum ParamId {
		GAIN_PARAM,
		OFS_PARAM, // the red line's own height, minus one to one
		GRAB_PARAM, // the arrow into IN, unlit nought and lit one (`grabSet`)
		CLAMP_PARAM, // the diode: my jack held to my window, off by default (`held`)
		PARAMS_LEN
	};
	enum InputId {
		VCA_INPUT,    // a CV that drives the gain, for a VCA / four-quadrant multiply
		OFS_CV_INPUT, // a CV that drives the offset, for sequenced transposition
		IN_INPUT,     // the signal this Atn scales, a cable in which makes it a VCA and takes it out of the window
		INPUTS_LEN
	};
	enum OutputId {
		CV_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	/** One job at a time, each said by a claim or by my own cable, never
	guessed from what stands beside me, and never by
	anything to my right: claimed by a reader, a Seq's step, I am a step; with
	a cable in my own jack I am a VCA;
	with none of those I am a shaper and nothing else. The job says what my
	own jack carries; the window of everything to my right until the next Atn
	I shape as a shaper alone, a cable in IN taking me out of the window
	(`M_VCA`). No
	job depends on
	what stands to my right, which is why the Atns are no mixer.
	A cable in the gain or offset jack of an idle Atn is not a
	job: it modulates the window, which is the one control of the rig from
	outside the rig. */
	enum Job { JOB_SHAPER, JOB_VCA, JOB_STEP };
	int jobWork = JOB_SHAPER;
	std::atomic<int> jobNow{JOB_SHAPER};
	int jobOf() {
		if (claimedByReader())
			return JOB_STEP;
		return cv.live() ? JOB_VCA : JOB_SHAPER;
	}
	// A shaper shapes the window to my right; a VCA is no shaper at all, its
	// rig having no such trait for a window slot to find (`M_VCA`). A step
	// shapes nothing, standing down for the reader that claimed it
	// (`Cartridge::evaluate`).
	int jobOut() override { return jobWork; }
	int arrowOut() override {
		return (relaxed(shapesRightNow) ? 1 : 0) | (relaxed(windowHeldNow) ? 2 : 0) | (relaxed(heldThroughNow) ? 4 : 0);
	}
	/** What my jack carries, channel by channel, before `sane`. */
	float jackV[PORT_MAX_CHANNELS] = {};
	/** My jack, channel `c`, a number that is no number stopped (`sane`). */
	void put(int c, float v) {
		jackV[c] = v;
		outputs[CV_OUTPUT].setVoltage(sane(v), c);
	}
	/** The diode: lit, my
	jack is held to my window, the offset `centre` and the reach of a gain of
	`g` either side of it as the glass lays it (`AtnLane::spanOf`), so offset 0
	at x0.5 gives -5 V to 5 V and offset 9 at x0.2 gives 7 V to 11 V, past the
	glass or not. A plain min and max against the window's own ends: the edge
	is the edge, with no tolerance. Read at the wake (`prepare`). */
	bool clampWork = false;
	bool clampSet() { return params[CLAMP_PARAM].getValue() > 0.5f; }
	void windowOf(float g, float& lo, float& hi) {
		if (notesWork)
			noteSpan(g, snapWork, lo, hi);
		else {
			lo = -std::fabs(g);
			hi = std::fabs(g);
		}
	}
	float held(float v, float g, float centre) {
		if (!clampWork)
			return v;
		float lo, hi;
		windowOf(g, lo, hi);
		return clamp(v, centre + lo, centre + hi);
	}
	// How wide my own jack is. Rack's setChannels does nothing on a port
	// with no cable in it, so the count cannot be read back off the port
	// and is kept here instead, for a Poly bundling me and for the glow.
	int jackN = 1;
	int channelsMeant(int jack) override { return jack == CV_OUTPUT ? jackN : 1; } // and the glow's
	std::atomic<bool> claimedNow{false}; // a reader walks the block I stand in
	// The meter, a machine of its own (`AtnMeter`), held rather than been.
	AtnMeter meter;
	// The colour of the cable in my own jack, packed RGBA, written by my
	// widget and read by every Atn of the block to draw the routing. Nought
	// is no cable.
	std::atomic<uint32_t> cableRGB{0};
	// The colour of the Poly's channel I take as an OUT strip, the module
	// the channel came from: the cable's colour's stand-in on my screen.
	int fedColWork = -1;
	std::atomic<int> fedColNow{-1};
	// And of the cables in the two jacks on the glass, for their own hint
	// lines to light up in: a line that is grey while the jack is empty and
	// the cable's own colour while it is not says, without a caption, which
	// fader that cable has hold of.
	std::atomic<uint32_t> vcaRGB{0};
	std::atomic<uint32_t> ofsRGB{0};

	// The jacks, read every sample so an audio-rate signal gets through.
	JackWatch gainCv;   // the CV that drives the gain
	JackWatch ofsCv; // the CV that drives the offset
	Scale snapWork;                     // the scale in force this wake, for the engine's own snapping
	ScaleAtomic snapScale;              // the same for the screens' drags, published at the wake's end
	// Whether I am in notes: a scale arriving from my left, or the CV whose
	// window I shape saying it plays it on notes (`heldMode`).
	bool notesWork = false;
	std::atomic<bool> notesNow{false};
	/** Whether I play the window I hand my holders as volts: unless the CV
	whose window I shape says it plays it on notes (`heldMode`), or, before
	its first word since the parse, a CV holds me (`TOLD_PITCH`), I do,
	whatever scale arrives from my left.
	Then I am a gain in volts and my offset is volts on the window, and I
	leave the scale on it for whatever stands past me, so a block of CVs
	further right is quantized still. My own jack is not my holders' business:
	it goes by my left alone (`ofsVolts`), so nothing put down to my right
	changes it. */
	bool plainHeld() const {
		// A CRV holding me nearest says volts, and a CV reaching past it is
		// not heard on me (the nearest holder's word, `Node::hear`).
		if (told() & tree::TOLD_PLAIN)
			return true;
		return heldYet ? !heldOnNotes() : !(told() & tree::TOLD_PITCH);
	}
	/** The window as it arrived, for my own jack. */
	Mapping winLeft;
	/** The window as I read it this wake: the one arriving, with the scale
	taken off where I am held plain. */
	Mapping winSeen;
	// How the CV whose window I shape plays it (`heldMode`), a wake late,
	// passed to me through a Qnt standing between us, which I know nothing of.
	// Before the first word since the parse, the parse's `TOLD_PITCH` alone.
	int heldWork = HELD_UNSAID;
	int heldBy = 0; // the place of the CV whose word it is, the nearest that spoke
	/** A keyboard's notes or a roll's: a divider's steps are volts. */
	bool heldOnNotes() const { return heldWork == HELD_KEYS || heldWork == HELD_ROLL; }
	/** A divider's either way, equal lines or the harmonic series: the
	engine shapes both alike, and only the tooltips tell them apart. */
	static bool divides(int mode) { return mode == HELD_DIVIDER || mode == HELD_HARMONICS; }
	bool heldYet = false;
	// The mode I am played in as this wake finds it, and as the screens see it.
	int modeWork = HELD_UNSAID;
	std::atomic<int> modeNow{HELD_UNSAID};
	/** A parse keeps the last word, on trust: the CV that said it may be
	gone, or farther than another now. It is forgotten over the finished
	parse where no CV owes me a word any more (`heldModeOwed`, `parsed`),
	and otherwise stands, kept, until the first word since the parse
	replaces it, whoever says it, at the first wake, which every node of a
	fresh parse is at. So a reparse that moved nothing, another rig's
	module added or a Seq's ACROSS flipped, plays the word it played before
	and is not heard; were it forgotten, the first wake after it would play
	me as though no CV held me, a click of one sample on my jack and the CVs
	I shape. An OUT Atn is a colourless reader besides, as a Mix is: it leaves the
	colours' count where the reader before it left it. */
	void settle() override {
		Cartridge::settle();
		heldKept = heldYet;
		heldOwed = false;
		if (outParse)
			rankNone(at(S_OUT_READER));
	}
	/** The word as the last parse left it, not said since; and whether a
	CV promised one at this parse. */
	bool heldKept = false, heldOwed = false;
	void heldModeOwed() override { heldOwed = true; }
	/** Walked by a Seq whose window something past it reads, a CV, a CRV or
	a Qnt, which the Seq tells me over the finished parse (`Seq::parsed`):
	a step that still shapes, its right arrow its handshake with what reads
	past the Seq. */
	std::atomic<bool> heldThroughNow{false};

	// ---- Three things, one at a time. With IN empty and the arrow unlit an
	// Atn shapes the window of what stands to its right, the manifest's ATN
	// row. With a cable in IN it is a VCA of that cable and nothing else,
	// shaping nothing to its right, which goes on as though it were not there
	// (`M_VCA`): the cable always wins. With IN empty and the arrow lit it is
	// a VCA of the nearest signal to its left, as though that were cabled into
	// IN, a reader and no cartridge (`M_OUT`), so the grammar holds it as it
	// holds any reader and no cartridge's slot is widened for it
	// (CARTRIDGE_MAY_FIND); it too shapes nothing to its right.
	enum { S_SIGNAL = 1, S_OUT_READER = 2 };
	ROW_SLOT_IS(M_OUT, S_SIGNAL, "signal");
	ROW_SLOT_IS(M_OUT, S_OUT_READER, "reader");
	/** Which of the three the parse took, which the manifest row follows:
	the arrow and the IN jack, read at the top of every parse and nowhere
	else, so a change of either is a change of the parse (`parseStale`).
	`outParse` is the lit arrow with IN empty, a reader; `vcaParse` a cable
	in IN, whatever the arrow says. */
	bool outParse = false, vcaParse = false;
	std::atomic<bool> outNow{false}, vcaNow{false}; // the same, for the screens and the tooltips
	bool grabSet() { return params[GRAB_PARAM].getValue() > 0.5f; }
	bool cabledSet() { return inputs[IN_INPUT].isConnected(); }
	/** Standalone in a rig: IN empty, the arrow unlit, nothing to my left a
	lit arrow could take and nothing to my right reading my window, so neither
	arrow is drawn and the switch chooses nothing. The one job left is a VCA
	of IN, which reads nought, so a Seq's step on me is my offset, ROUTE added
	to it as to any routed Atn: each Atn a Seq walks decides its job by what
	it can reach, never by the block, and CLK ATN ATN SEQ is a sequential
	switch of offsets. Parsed as the cabled row (`M_VCA`), a router, so a
	cabled Atn beside me stands in my block. Decided over the last parse
	(`standaloneWanted`), the arrows being what a parse finds, and a change
	asks for the next (`parseStale`); a lone Atn, in no rig, stays as it
	was. */
	bool standaloneParse = false, standaloneWanted = false;
	/** Standalone, I carry no signal, so a gatherer passes me over as it
	passes over an unlit Atn (`unseen`, Gather.hpp): a Mix and a Poly take
	only the Atns carrying a signal. */
	manifest::Face face() const override { return standaloneParse ? manifest::F_NONE : Cartridge::face(); }
	void beforeParse() override {
		vcaParse = cabledSet();
		outParse = grabSet() && !vcaParse;
		standaloneParse = standaloneWanted && !vcaParse && !outParse;
		row = vcaParse || standaloneParse ? manifest::M_VCA : outParse ? manifest::M_OUT : manifest::M_ATN;
		// The slots found by name are the rig's, and the rig may just have changed.
		sWindow = slotNamed("window");
	}
	/** An Atn lit or cabled is no shaper, and would be one unlit and
	unpatched: the window slots to my right that pass me over say so
	(`wouldHear`), which is how the right arrow knows it has something to
	shape while it is out. */
	uint32_t latentTraits() override { return row == manifest::M_ATN ? 0 : tree::T_SHAPER; }
	/** Whether something to my right reads, or would read, my window: a
	window slot of a Qnt, a CV or a CRV holding me, or one that would were
	my arrow out and IN empty. The next Atn's window slot is not counted,
	since that Atn takes my window's place. The right arrow is drawn only
	where it is. */
	std::atomic<bool> shapesRightNow{false};
	/** Whether my left gives what a lit arrow takes: lit, whether my signal
	slot found it, a signal or a Poly to take a strip of (`grab`,
	`stripPoly`); unlit, whether that slot would, the nearest node to my left
	playing what it searches for, a lit Atn that is no strip passed over for
	its own (`parsed`). The IN arrow lights only where it is (an arrow
	lights only where what it points at is real), and is drawn only where it is, or where it is lit. */
	bool canGrabWork = false;
	std::atomic<bool> canGrabNow{false};
	/** And whether what it would take is a Poly or a lit strip of one, which
	the guess lights whatever holds me: where the context makes it obvious,
	the signal is the one to take. */
	bool grabsPolyWork = false;
	std::atomic<bool> grabsPolyNow{false};
	/** What a lit arrow takes, worked out over the finished parse: my signal
	slot's, and where that is a lit Atn that is no strip, what that Atn takes,
	so SEQ ATN ATN ATN, every arrow lit, is three VCAs of the one Seq, each at
	its own gain. An Atn between with its arrow
	unlit takes nothing and is passed by the slot, being no reader: it shapes
	windows, and a lit Atn takes no window. */
	Member* grab = nullptr;
	/** What the hand's guess at the arrow and its turn at a new bond go by
	(`AtnWidget::bond`), published at the end of the wake: whether a window slot holds
	me, and the ids of the neighbours the parse saw, minus one for none. */
	std::atomic<bool> windowHeldNow{false};
	std::atomic<int64_t> leftSeenNow{-2}, rightSeenNow{-2};
	/** Who stands beside me as the expanders say, whether each neighbour is
	ours and its id, minus one for none. Asking whether a module is ours is a
	`dynamic_cast`, which on this platform compares type names and cost a
	quarter of a VCA's sample when it ran three times a wake; an expander
	moves only with an ExpanderChangeEvent, which bumps the topology epoch
	(`Member::onExpanderChange`), so the answer is read afresh only when the
	epoch has moved. Plain fields of the wake, on my rig's thread. */
	struct Sides {
		bool left = false, right = false;
		int64_t leftId = -1, rightId = -1;
	};
	Sides sidesWork;
	uint32_t sidesAt = 0; // the epoch starts at one, so nought is never current
	const Sides& sides() {
		const uint32_t e = topology();
		if (e != sidesAt) {
			sidesAt = e;
			Module* l = leftExpander.module;
			Module* r = rightExpander.module;
			sidesWork.left = inRig(l);
			sidesWork.right = inRig(r);
			sidesWork.leftId = sidesWork.left ? l->id : -1;
			sidesWork.rightId = sidesWork.right && bondsToLeft(r) ? r->id : -1;
		}
		return sidesWork;
	}
	/** Whether the arrow has had its guess: false on an Atn fresh from the
	browser and true on any loaded from a patch or a duplicate's copy, unless
	what was saved says it never had (`fromJson`). The hand's alone, on the
	UI thread. */
	bool arrowGuessed = false;
	void fromJson(json_t* rootJ) override {
		arrowGuessed = true;
		Cartridge::fromJson(rootJ);
	}
	json_t* dataToJson() override {
		json_t* j = json_object();
		json_object_set_new(j, "arrowGuessed", json_boolean(arrowGuessed));
		return j;
	}
	void dataFromJson(json_t* rootJ) override {
		Cartridge::dataFromJson(rootJ);
		if (json_t* g = json_object_get(rootJ, "arrowGuessed"))
			arrowGuessed = json_is_true(g);
	}
	/** A strip, a mixer's channel made explicit as OUT Atns after a Poly with
	a MIX at the end of the chain: a lit Atn
	whose signal is a Poly takes its first channel, and one whose signal is a
	lit strip of a Poly the channel after that strip's, so POLY ATN ATN ATN,
	every arrow lit, is three channel strips and never a master, and a MIX
	after them sums them. Worked out over the finished parse from my signal
	slot, the strip I read having worked out its own the moment before, as it
	stands to my left. An Atn with a cable in IN is no strip, being no reader
	(`M_VCA`): the slot passes it, and the strip after it takes the channel it
	would have. */
	Member* stripPoly = nullptr;
	int stripAt = -1;
	void parsed(const tree::Left& left, int pass) override {
		Cartridge::parsed(left, pass);
		// Every CV has promised its word in the first pass; nobody owing one,
		// the kept word is nobody's.
		if (pass == 1 && !heldOwed) {
			heldYet = heldKept = false;
			heldWork = HELD_UNSAID;
		}
		// Over the finished parse, a Seq having told what it walks in its
		// first pass: whether I stand in a rig with neither arrow drawn. An
		// Atn to my right whose window slot finds me, or would, has taken my
		// window's place: I am overridden, not standalone, and keep that look
		// (`markUnread`).
		if (pass == 2) {
			const Sides& n = sides();
			standaloneWanted = !grabSet() && !vcaParse && (n.left || n.right) && !canGrabWork &&
			                   !((told() | wouldHear()) & (tree::TOLD_WINDOW | tree::TOLD_ATN_WINDOW));
		}
		if (pass != 0)
			return;
		stripPoly = nullptr;
		stripAt = -1;
		grab = nullptr;
		canGrabWork = grabsPolyWork = false;
		// What the lit row's signal slot searches for, read from the manifest
		// rather than listed again here.
		const uint32_t signal = manifest::spec(manifest::M_OUT).slots[S_SIGNAL].trait;
		for (int i = left.size() - 1; i >= 0 && !canGrabWork; i--) {
			Atn* a = dynamic_cast<Atn*>(left[i]);
			canGrabWork = (left[i]->traits & signal) && !(a && a->outParse && !a->stripPoly);
			grabsPolyWork = canGrabWork && ((left[i]->traits & tree::T_BUNDLE) || (a && a->outParse));
		}
		Member* src = outParse ? at(S_SIGNAL) : nullptr;
		// Lit, the slot's own answer is the word: what follows sets `grab`
		// or `stripPoly`, and it is real only where one is set.
		if (outParse)
			canGrabWork = false;
		if (!src)
			return;
		if (src->traits & tree::T_BUNDLE) {
			stripPoly = src;
			stripAt = 0;
		}
		else if (Atn* a = dynamic_cast<Atn*>(src)) {
			if (a->outParse && a->stripPoly) {
				stripPoly = a->stripPoly;
				stripAt = a->stripAt + 1;
				alsoReads(stripPoly); // past my slot, as a Poly reads its bundle
			}
			else if (a->outParse) {
				// Past a lit Atn to what it takes, read as it is.
				src = a->grab;
				if (src)
					alsoReads(src);
			}
		}
		grab = src;
		if (outParse)
			canGrabWork = grab || stripPoly;
	}
	std::atomic<int> stripNow{-1};        // the channel I take as a strip, for the tooltip
	std::atomic<bool> stripEmptyNow{false}; // and whether the Poly has it
	/** A reader of everything to my left, so at every wake, as a Mix is: what
	I read may move without its node waking me, a Seq's jack between its
	steps and a CRV's under its own IN. */
	bool atEveryWake() override { return outParse; }
	void heldMode(int by, int mode) override {
		// The first word since the parse: whoever says it is the nearest
		// (every node is at the first wake, and nearer stands farther left),
		// and the same word as the one kept moves nothing.
		if (heldKept) {
			heldKept = false;
			if (mode == heldWork) {
				heldBy = by;
				return;
			}
		}
		else if (heldYet && (by > heldBy || (by == heldBy && mode == heldWork)))
			return;
		heldBy = by;
		heldWork = mode;
		heldYet = true;
		written();
		poke.store(true, std::memory_order_relaxed);
	}
	/** Whose window I shape: my window slot's, the shaper before me in my
	gap; and where it finds nothing and the block of CVs holding my gap has no
	Qnt in it (`TOLD_LANE`), the window of the block of CVs before that, so
	CV.. ATN CV.. lays my range on the first block's scheme. */
	enum { S_LANE = 2 };
	ROW_SLOT_IS(M_ATN, S_LANE, "lane");
	Member* windowSource() override {
		if (outParse)
			return nullptr; // a reader takes no window: what it reads says how it was played
		if (Member* w = at(sWindow))
			return w;
		return (told() & tree::TOLD_LANE) ? at(S_LANE) : nullptr;
	}
	/** The scale a span of mine is laid on and named from: the one arriving
	from my left, none for none. */
	Scale spanScale() { return snapScale.load(); }
	// Whether I affect nothing: a shaper with no cable of its own, out or in,
	// that nothing reads through its window, so held by the Atn on my right
	// alone, whose window takes the place of mine before anything reads it,
	// or by nothing at all, a lone Atn. An Atn whose cabled output carries
	// its offset is in play. `byAtnWork` says which, for the tooltip.
	bool overriddenWork = false, byAtnWork = false, outOnlyWork = false, aloneWork = true;
	std::atomic<bool> overriddenNow{false}, byAtnNow{false}, outOnlyNow{false}, aloneNow{true};
	/** Which of my lanes reaches nothing past me, a bit a lane by `AtnLane::lane`:
	both where I affect nothing, overridden or an OUT strip the Poly has no
	channel for, the grey of `AtnLane::colour`; the gain as well where there is
	no signal for it to scale, alone with IN empty or OUT giving the offset
	alone. Its cable still moves the glass, so only the handle's own light
	goes (`AtnLane::drawHandle`). One word, so the two bits never tear. */
	std::atomic<int> idleLanesNow{0};
	// Whether what I shape is a block of CRVs (`TOLD_PLAIN`) rather than of
	// CVs, for the tooltip, which names them.
	bool crvWork = false;
	std::atomic<bool> crvNow{false};
	// The same for what a window slot to my right holds me for or would
	// (`wouldHear`), so a cable in IN, which takes me out of the window, can
	// still say what it stops me shaping.
	std::atomic<bool> crvRightNow{false};
	// Whether another ATN stands anywhere in my run, as my widget saw it
	// (`sharesRunOf`), for the sweep's piece.
	std::atomic<bool> sweepUi{false};

	Atn() : Cartridge(manifest::M_ATN) {
		// `publishReads` works the offset out again from the knob and the CV
		// as it stands, which the engine then plays: so it is never left out
		// of a wake that reaches me, or a quiet Atn would publish an offset
		// the engine is not playing.
		publishesLive = true;
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		// The gain is bipolar so an Atn is an attenuverter: full up passes
		// the signal, centre kills it, full down inverts it.
		configParam<GainQuantity>(GAIN_PARAM, -GAIN_MAX, GAIN_MAX, CV_VOLTS, "Gain")->atn = this;
		// The same parameter an Ofs has, with the same range and the same
		// meaning, because it is the same thing: minus one to one of whatever
		// the rig is in.
		configParam<OfsQuantity>(OFS_PARAM, -1.f, 1.f, 0.f, "Offset")->atn = this;
		// The arrow into IN, chosen by a click, guessed once by the hand and
		// turned by it to the one job a new place offers (`AtnWidget::bond`),
		// never at the parse (`beforeParse`).
		// Two arrows show the one param, so its values say what the Atn does rather than whether
		// one arrow is lit: the right arrow lit at nought, the IN arrow at one.
		// One name for both, and the job as it stands said as the value
		// (`restate` in `retipAll`), since what the arrows do turns on what
		// stands beside the Atn as well as on the param.
		configSwitch<WithHint<SwitchQuantity>>(GRAB_PARAM, 0.f, 1.f, 0.f, "Reach", {"", ""});
		// The diode, off until lit: OUT held to the window (`held`).
		configSwitch<WithHint<SwitchQuantity>>(CLAMP_PARAM, 0.f, 1.f, 0.f, "Clamp", {"Off", "On"});
		configInput(IN_INPUT, "Signal");
		configInput(VCA_INPUT, "Gain");
		configInput(OFS_CV_INPUT, "Offset");
		cv.bind(inputs[IN_INPUT]);
		gainCv.bind(inputs[VCA_INPUT]);
		ofsCv.bind(inputs[OFS_CV_INPUT]);
		// The two jacks do not obey the same law, and that is the one thing
		// about this panel nobody can guess, so both say which they are: the
		// gain's here, the offset's once a scale comes into reach.
		retipIn(this, VCA_INPUT, {"Gain", GAIN_JACK});
		configOutput(CV_OUTPUT, "Signal");
	}

	/** An Atn is two things by where it stands: how far a CV's travel
	reaches, or the gain on a signal passing through. Only the one it is now
	is described. */
	void retipAll() override {
		// One control, one name, the word on its own lane (Gain, or Range
		// where a keyboard is in reach) so a user reading the tooltip and
		// the glass sees one control; what changes with the role is the
		// sentence under it.
		const bool notes = relaxed(notesNow);
		// Kept to a word or two, so the tooltip never covers the glass it
		// describes. The line under the name says the job: an Atn shapes the
		// rig only when nothing feeds it.
		const int job = relaxed(jobNow);
		// Shorter again: a step of a block says nothing a glance does not.
		const bool out = relaxed(outNow);
		// An OUT strip of a Poly says which channel, or that the Poly has
		// none for it.
		const int strip = relaxed(stripNow);
		const std::string stripSays = strip < 0 ? std::string()
		                              : relaxed(stripEmptyNow)
		                                  ? std::string("The POLY has no channel left for it, so OUT is silent.")
		                                  : string::f("Scales channel %d of the POLY onto OUT.", strip + 1);
		const bool vca = relaxed(vcaNow);
		// What it shapes, named: a block of CRVs or of CVs.
		const bool crv = relaxed(crvNow);
		const bool alone = relaxed(aloneNow), overridden = relaxed(overriddenNow), outOnly = relaxed(outOnlyNow);
		// Taking the left with nothing there to take, standalone, says what
		// any Atn taking nothing says. Alone, an attenuverter is what it looks
		// like and says nothing; where it affects nothing, it says so on
		// both lanes, and where OUT is all it gives, both lanes say that,
		// in the overriding ATN's words where one overrides it, one cause said
		// one way wherever it is met.
		const bool byAtn = relaxed(byAtnNow);
		const char* unused = outOnly ? (byAtn ? "The ATN to its right overrides it, so OUT gives the offset alone."
		                                      : "Nothing to its right reads it, so OUT gives the offset alone.")
		                     : !overridden ? ""
		                     : byAtn ? "The ATN to its right overrides it, so it shapes nothing."
		                             : "Nothing to its right reads it.";
		// A step of a Seq's block plays what the Seq routes to it while the
		// playhead stands on it, at its gain and lifted by its offset
		// (`ownJack`), which nothing on the panel says.
		const bool step = job == JOB_STEP;
		// Unless what reads past the Seq, CVs, CRVs or a Qnt, reads the window
		// it hands on, so the step shapes it while the playhead stands on it
		// (`heldThroughNow`); which of them it is the step is not told, so the
		// words name them all.
		const bool stepShapes = step && relaxed(heldThroughNow);
		// With a cable in IN the gain does what any VCA's does, and the
		// surprise, that it stops shaping, is the cable's, said on IN.
		const char* does = out && (strip >= 0 || relaxed(canGrabNow))
		                       ? (strip >= 0 ? stripSays.c_str() : "Scales the module to its left onto OUT.")
		                   : vca && !step ? ""
		                   : overridden || outOnly ? unused
		                   : stepShapes ? "Sets the range of the modules past the SEQ while its playhead stands here."
		                   : step ? "A step of the SEQ: while the playhead stands here, it scales what comes in at the SEQ's ROUTE."
		                   : alone && job == JOB_SHAPER ? ""
		                   : job == JOB_SHAPER ? (crv ? (outputs[CV_OUTPUT].isConnected() ? "Sets the range of the CRVs to its right, and gives the offset at OUT."
		                                                                                    : "Sets the range of the CRVs to its right.")
		                                              : (outputs[CV_OUTPUT].isConnected() ? "Sets the range of the CVs to its right, and gives the offset at OUT."
		                                                                                    : "Sets the range of the CVs to its right."))
		                                       : "";
		const int mode = relaxed(modeNow);
		// Over a span of notes the number on the handle is semitones, which
		// nothing else says: a bare "120" reads as nothing.
		const std::string rangeSays = std::string(does) + (*does ? " " : "") + "The number on its handle is the range in semitones.";
		const bool lines = mode == HELD_DIVIDER, series = mode == HELD_HARMONICS, roll = mode == HELD_ROLL;
		const bool sweeps = relaxed(sweepUi);
		// Each lane's name, the jack under it named the same.
		const char* gainName = roll ? "Spread" : lines || series ? "Span" : notes ? "Range" : "Gain";
		const char* ofsName = roll || notes ? "Transpose" : "Offset";
		retipParam(this, GAIN_PARAM, {gainName,
		           roll ? "Spreads the chord of the QNT's piano roll about its lowest note: x0 plays one note, x1 the chord as it is."
		           : lines ? "How far the divider's range reaches either side of the offset: x1 reaches 10 V each way."
		           : series ? "Stretches the harmonic series: x1 leaves it as it stands."
		           : notes ? rangeSays.c_str() : does,
		           laneSwept(notes ? Gestures{&G_LANE_RANGE_OCTAVES}
		                     : laneGain(roll ? G_LANE_SPREAD_STEPS : lines || series ? G_LANE_SPAN_STEPS : G_LANE_GAIN_STEPS),
		                     sweeps,
		                     roll ? G_LANE_SWEEP_SPREAD : lines || series ? G_LANE_SWEEP_SPAN
		                     : notes ? G_LANE_SWEEP_RANGE : G_LANE_SWEEP_GAIN)});
		// And the offset lane follows its own caption. In notes a plain
		// stroke lands on the intervals the scale holds (`shiftOnScale`,
		// `AtnLane::setFromY`), which no player would guess. Over a roll
		// shift with command is the cents' stroke slower, said as the
		// volts' finer is.
		retipParam(this, OFS_PARAM, {ofsName,
		           lines ? "The centre of the divider's range."
		           : series ? "Where the harmonic series starts, its fundamental."
		           : overridden || outOnly ? unused
		           : stepShapes && !(notes && !roll) ? "Moves the modules past the SEQ while its playhead stands here."
		           : step && !stepShapes ? "A step of the SEQ: while the playhead stands here, it is added to what comes in at the SEQ's ROUTE."
		           : notes && !roll ? "A drag moves the notes only by intervals the scale holds, so they stay on it."
		           : "",
		           laneSwept(roll ? Gestures{&G_LANE_OCTAVES, &G_LANE_CENTS, &G_LANE_CENTS_SLOWER}
		                     : notes ? Gestures{&G_LANE_OCTAVES}
		                             : laneOffset(), sweeps,
		                     roll || notes ? G_LANE_SWEEP_TRANSPOSE : G_LANE_SWEEP_OFFSET)});
		// The jacks follow their lanes too, and keep the one sentence each
		// that says which law it obeys: scaling on the left, adding on the
		// right. `retip` writes both or neither, so the sentence is repeated
		// here rather than dropped.
		retipIn(this, VCA_INPUT, {roll ? "Spread" : lines || series ? "Span" : notes ? "Range" : "Gain",
		        roll ? GAIN_JACK_SPREAD : lines || series ? GAIN_JACK_SPAN : notes ? GAIN_JACK_RANGE : GAIN_JACK});
		retipIn(this, OFS_CV_INPUT, {roll || notes ? "Transpose" : "Offset",
		        roll ? OFS_JACK : notes ? OFS_JACK_KEYS : OFS_JACK_VOLTS});
		// IN, alone and empty, by what OUT gives without it, the one thing an
		// attenuverter's IN can surprise with; elsewhere by what a cable
		// there would take the place of: the module to
		// its left it takes, or the shaping of what stands to its right; and
		// once a cable is in, what that cable is doing, the same fact kept on
		// the part that causes it. A cable in
		// IN wins whatever the arrow says (`beforeParse`), so the param is
		// read here as the arrow would have it once the cable goes.
		const bool canGrab = relaxed(canGrabNow), shapes = relaxed(shapesRightNow);
		const bool crvRight = crv || relaxed(crvRightNow);
		retipIn(this, IN_INPUT, {"Signal",
		        alone && !vca ? "With nothing in, OUT gives the offset alone."
		        : vca ? (step ? ""
		               : grabSet() && canGrab ? "Its cable takes the place of the module to its left."
		               : shapes ? (crvRight ? "Its cable stops it shaping the CRVs to its right." : "Its cable stops it shaping the CVs to its right.")
		                        : "")
		        : out ? (canGrab ? "A cable here takes the place of the module to its left." : "")
		        : (job == JOB_SHAPER || stepShapes) && !alone && !overridden && !outOnly
		            ? (stepShapes ? "A cable here stops it shaping the modules past the SEQ."
		               : crv      ? "A cable here stops it shaping the CRVs to its right."
		                          : "A cable here stops it shaping the CVs to its right.")
		            : ""});
		// Both arrows are one control, Reach, and say the job as it stands
		// as its value, never the widget, and where what the job points at
		// is not there, the value says so. Taking the left with nothing
		// there to take, the Atn is standalone, the IN arrow undrawn and the
		// right arrow alone saying so. The two arrows are one param, so
		// either's click flips it, and the toggle is left off where no arrow
		// is drawn (`AtnArrow::shown`). With a cable in IN neither arrow is
		// drawn, so the first branch is what a capture reads and no hand
		// hovers.
		restate(this, GRAB_PARAM, vca ? "Cable in IN" : out ? (canGrab ? "Takes the left" : "Standalone")
		                                : shapes ? "Shapes the right" : "Nothing to shape on the right");
		retipParam(this, GRAB_PARAM,
		           {"Reach",
		           vca ? "The arrows come back when the cable goes."
		           : out ? (canGrab ? "Reads the module to its left as though it were cabled into IN."
		                            : "It shapes nothing, and OUT gives the offset.")
		           : canGrab ? (shapes ? "Taking the left reads the module there as though cabled into IN, and shapes nothing to its right."
		                               : "Taking the left reads the module there as though cabled into IN.")
		           : shapes ? "Stopped, it is standalone: it shapes nothing, and OUT gives the offset."
		                    : "",
		           !vca && (shapes || canGrab) ? Gestures{&G_TOGGLE} : Gestures{}});
		// The diode says what lighting it does in every job, OUT held to the
		// offset lane either side by the gain lane (`held`), each named as
		// its caption names it now.
		const std::string clampSays = string::f("Lit, OUT is held between the %s minus and plus the %s, even past the screen's edge.",
		                                        string::lowercase(ofsName).c_str(), string::lowercase(gainName).c_str());
		retipParam(this, CLAMP_PARAM, {"Clamp", clampSays.c_str(), Gestures{&G_TOGGLE}});
		retipOut(this, CV_OUTPUT, {"Signal"});
	}

	/** The knob alone. */
	float gainSet() { return params[GAIN_PARAM].getValue(); }
	/** The two faders as the engine plays them, gliding to where the hand
	put them (`Glide`), so a stroke on either is no click on what I scale.
	Glided only where what they move is continuous: the gain over volts or
	as a VCA, the offset in volts. Where I am in notes, a roll or a divider,
	the setting lands on a pattern and a glide would walk through the notes
	between, so the engine plays the setting itself. The screens read the
	settings, never these. */
	Glide gainGlide, ofsGlide;
	float gainKnobWake = CV_VOLTS; // the knob this wake plays, glided or not
	bool gainGlides(bool notes, int mode) const {
		return jobWork == JOB_VCA || (!notes && mode != HELD_ROLL && !divides(mode));
	}
	/** The offset fader as the engine plays it in unit `u`. */
	float ofsKnobFor(int u) {
		const float set = params[OFS_PARAM].getValue();
		const bool glides = u == OfsCore::UNIT_VOLTS && !divides(modeWork);
		return clamp(glides ? ofsGlide.played(set) : set, -1.f, 1.f);
	}
	bool windowIn(Mapping& win) override {
		win = winIn;
		return true;
	}
	/** The top a cable at the VCA jack makes of the knob: the knob scaled by
	the jack, ten volts there being the whole of it; the knob itself with no
	cable in. */
	float driven(float knob) { return gainCv.live() ? gained(knob, gainCv.get()) : knob; }
	/** A knob scaled by so many volts at the VCA jack, ten being all of it. */
	static float gained(float knob, float volts) { return knob * clamp(volts / CV_VOLTS, -1.f, 1.f); }
	/** The offset lane: an Ofs, whole, inside this module (`OfsCore`,
	Offset.hpp), the retired Ofs module's own code lifted into a struct so
	that the two cannot drift apart: the same unit, the same shift of a
	window and of a signal. The bar is minus one to one as an Ofs's is, and
	the flow is gain first, then offset (`act`). */
	OfsCore ofs;
	/** The offset fader alone, where the mouse left it. */
	float ofsKnob() { return clamp(params[OFS_PARAM].getValue(), -1.f, 1.f); }
	/** The offset in force: the fader, plus whatever its jack brings, at a
	volt the octave. The two lanes do not obey the same law and should not:
	the gain scales, so a CV in the gain's jack reaches the fader and no
	further, while an offset adds, so a CV in the offset's jack adds to it.
	One volt is twelve semitones on a pitch and one volt in volts, which is
	the standard every other module here follows: set the offset to +12 and
	send a volt and the transposition is +24. The sum is held inside the
	lane's own range, and on a keyboard it lands on the pattern like the
	fader's own drag, so a sequence stays on its own notes however it is
	transposed and by whom. */
	float ofsAmountIn(int u, const Scale& scale) { return ofsAmountOf(ofsKnob(), u, scale); }
	/** The same as the engine plays it, the fader glided where it may be. */
	float ofsPlayedIn(int u, const Scale& scale, int voice = 0) { return ofsAmountOf(ofsKnobFor(u), u, scale, voice); }
	/** `voice` the channel of a polyphonic cable in the offset's jack, so a
	poly offset shifts each channel by its own voice as a poly gain scales
	each by its own; a mono cable shifts them all alike. */
	float ofsAmountOf(float a, int u, const Scale& scale, int voice = 0) {
		if (ofsCv.live()) {
			// One volt, as this unit counts: the octave, whatever it is
			// called here, which is a tenth of the bar in volts and twelve
			// of its hundred and twenty semitones alike. The same step shift
			// lands a drag on.
			const float octave = 1.f / (float)OfsCore::MAX_VOLTS;
			a = clamp(a + ofsCv.getVoice(voice) * octave, -1.f, 1.f);
		}
		return onKeys(a, u, scale);
	}
	/** A bar landed on the keyboard in reach. A keyboard's notes only: a roll
	is a chord drawn on the piano and is transposed as one, a semitone a step,
	so its offset lands on every semitone; snapped to the roll's own notes,
	+12 on a roll would be +7 or whatever interval the roll happened to hold.
	*/
	static float onKeys(float a, int u, const Scale& scale) {
		if (u == OfsCore::UNIT_SEMIS && scale.any() && !scale.spread)
			a = clamp((float)shiftOnScale((int)std::round(a * (float)OfsCore::SEMI_REACH), scale) /
			              (float)OfsCore::SEMI_REACH,
			          -1.f, 1.f);
		return a;
	}
	/** The bar as the screens read it: the interval that actually sounds. A
	bar held at +1 over the white keys transposes by nothing, and reads so: a
	drag never lands there, but keys changed under a held offset, a MIDI map
	and randomize all do. */
	float ofsHeard(float a) { return onKeys(clamp(a, -1.f, 1.f), relaxed(ofs.unit), spanScale()); }
	/** The offset as the screens should read it: off the published unit, a
	wake behind, which is what a screen sees of everything else here too. */
	// In front of a Qnt the pattern it hands me, which its own window lands
	// the bar on (`Qnt::shape`), so the screens read the interval that sounds
	// there too.
	float ofsAmount() { return ofsAmountIn(relaxed(ofs.unit), spanScale()); }
	/** The notes a span of notes really ends on, worked out as the CV works
	them out: the span's own ends taken as whole semitones once, the lowest
	note of the pattern at or over the foot and the highest at or under the
	top, then moved by the offset along the pattern, as a note the CV plays is
	moved (`shiftAfterSnap`). A span with none of the pattern's notes in it is
	the one note the CV sounds there, the nearest to its foot
	(`Slots::windowNotes`), at both ends. `lo` and `hi` are volts from C4, as
	`noteSpan` gives them. The glass and the tooltip both name the ends from
	here, never by rounding the raw span, which says Gb4 where the CVs reach
	F4. The CVs' jacks give what their windows say, so the span's ends are
	named as they are. */
	void spanEnds(float lo, float hi, int& foot, int& top) {
		const Scale scale = snapScale.load();
		const int a = (int)std::round(lo * 12.f), b = (int)std::round(hi * 12.f);
		const int ofsSemis = (int)std::round(ofsAmount() * (float)OfsCore::SEMI_REACH);
		// Over a roll, which does not repeat and shares the whole travel
		// among its notes whatever the span, the ends are the roll's own
		// lowest and highest notes, moved by the offset, never the span's
		// ends, C-1 to C9 over CVs playing C4 E4 G4.
		if (scale.spread && scale.any()) {
			int notes[12 * MAX_OCTAVES + 1];
			const int n = spreadNotes(scale, notes);
			foot = notes[0] + ofsSemis;
			top = notes[n - 1] + ofsSemis;
			return;
		}
		if (!scale.any()) {
			foot = a + ofsSemis;
			top = b + ofsSemis;
			return;
		}
		foot = noteAtOrAbove(a, scale);
		top = noteAtOrBelow(b, scale);
		if (top < foot)
			foot = top = snapToScale(a, scale);
		// A keyboard transposed by degrees moves them first, as it moves
		// every note a CV plays over it (`Slots::noteOf`).
		foot = shiftAfterSnap(scale, degreeMoved(scale, foot), ofsSemis);
		top = shiftAfterSnap(scale, degreeMoved(scale, top), ofsSemis);
	}
	/** And as this wake should work it out: off the unit this wake settled,
	not the one the last wake published. The atomics move at the end of the
	wake, so on the wake a Qnt arrives they still
	hold the old unit while `snapScale` already holds the new pattern: a
	volt at the jack was scaled by one unit and then snapped by another, for
	one wake, on the signal rather than on a screen. */
	float ofsAmountWork(int voice = 0) { return ofsPlayedIn(ofs.unitWork, snapWork, voice); }
	/** What the offset adds to a voltage on a jack, in volts. Not the bar's
	own ±10 V: that is what it reads in volts, and on a pitch the same bar is
	sixty semitones, which is five volts and not ten; asking the bar directly
	would be twice the transposition the lane says. */
	float ofsVolts(int voice = 0) {
		// In the unit of the block that claims me, wherever I stand against a
		// Qnt: whole semitones on a keyboard, whole
		// cents on a roll, volts otherwise, so an Atn before a Qnt gives at
		// its jack what it shapes the CVs in. An Atn no block claims, walked
		// by a Seq or standing alone, goes by its left.
		if (!heldYet && !(told() & (tree::TOLD_PITCH | tree::TOLD_PLAIN))) {
			return ofs.shift(winLeft, ofsAmountLeft(voice)) / 12.f;
		}
		const float a = ofsAmountWork(voice);
		return ofs.unitWork == OfsCore::UNIT_SEMIS  ? (float)OfsCore::semis(a) / 12.f
		       : ofs.unitWork == OfsCore::UNIT_ROLL ? (float)OfsCore::cents(a) / 1200.f
		                                            : OfsCore::volts(a);
	}
	/** The bar in the unit my left gives me, whatever my holders say: for my
	own jack and for a reader's cable I sound, both mine to make. */
	float ofsAmountLeft(int voice = 0) {
		return ofsPlayedIn(winLeft.musical() ? OfsCore::UNIT_SEMIS : OfsCore::UNIT_VOLTS,
		                   winLeft.scale.any() ? winLeft.scale : Scale(), voice);
	}
	/** The top in force: the knob driven by the VCA jack, either side of
	nought, so a Atn inverts a plain voltage and a scale alike. */
	float gainInForce() { return clamp(driven(gainSet()), -GAIN_MAX, GAIN_MAX); }

	/** The meter: straight up to whatever arrives and back down over
	`SHOW_FALL_SECONDS`, so a slow CV sits where it stands and an audio
	signal reads as a level rather than as a flicker. It is a screen value,
	so it is looked at on the control period, every sample between two
	looks seen so a spike is never stepped over, and it is the one thing
	here that falls on its own, so it is the one thing an Atn asks the tick
	for. */
	bool wantsTick() override {
		if (gainGlide.moving() || ofsGlide.moving())
			return true;
		if (!running())
			return Cartridge::wantsTick();
		// The router's signal moves as a cable of my own does, and the Seq
		// routing it never wakes me, so it asks for the tick by itself, or a
		// routed Atn with nothing of its own would show nothing. And an OUT
		// Atn's jack is what it reads, which moves without waking it as a
		// cable does: without the tick its meter was fed only by a sample a
		// touch or a stir happened to land on the control period, so a fresh
		// OUT Atn showed nothing until a knob was nudged (atn-out-meter).
		return Cartridge::wantsTick() || meter.falling() || cv.live() || routed.any() || outParse;
	}
	/** Bypassed, my meter follows what OUT now carries, as the jack's light
	does (`Member::processBypass`), falling to it at the screen's pace from
	where it stood rather than freezing on a signal no longer there. Its
	anchor and its lanes stay as the last wake left them, so the fall is the
	meter's own, as when a signal goes. */
	void processBypass(const ProcessArgs& args) override {
		Cartridge::processBypass(args);
		float v[PORT_MAX_CHANNELS];
		const int n = meter.n;
		for (int c = 0; c < n; c++)
			v[c] = sane(outputs[CV_OUTPUT].getVoltage(c));
		meter.follow(v, n, args);
		if ((args.frame % CONTROL_PERIOD) == 0)
			meter.mark(v, meter.anchorWas, args);
	}
	bool everySample(const ProcessArgs& args) override {
		// The faders' glides first, so the meter's anchor is this sample's.
		const bool land = landing();
		bool glided = gainGlide.step(params[GAIN_PARAM].getValue(), args.sampleRate, args.frame, land);
		glided |= ofsGlide.step(params[OFS_PARAM].getValue(), args.sampleRate, args.frame, land);
		float v[PORT_MAX_CHANNELS];
		const int n = metered(v);
		meter.follow(v, n, args);
		// The offset lands on the output whole now, the gain having had its
		// say before it, so the anchor is the offset itself.
		if ((args.frame % CONTROL_PERIOD) == 0)
			meter.mark(v, running() ? ofsVolts() : 0.f, args);
		return Cartridge::everySample(args) || glided;
	}


	float gainWake = CV_VOLTS; // the top in force this wake
	void prepare(const Mapping& win, const ProcessArgs& args) override {
		// The unit and the pattern first, and only then the offset: what a
		// volt at the offset's jack is worth, and whether the sum lands on a
		// keyboard, are both things this wake's window says.
		const bool toldNotes = !(told() & tree::TOLD_PLAIN) && heldYet && heldOnNotes();
		fedColWork = -1;
		// A reader's cable I sound goes by my window slot alone, never by
		// the lane I am told I continue (`TOLD_LANE`).
		{
			Member* w = at(sWindow);
			const Mapping* m = w ? w->laneOut() : nullptr;
			winLeft = m ? *m : Mapping();
		}
		winSeen = win;
		if (plainHeld()) {
			winSeen.scale = Scale();
			winSeen.quantNode = nullptr;
		}
		ofs.prepared(winSeen, toldNotes);
		snapWork = winSeen.scale.any() ? winSeen.scale : Scale();
		notesWork = snapWork.any() || toldNotes;
		// Over a roll or a divider, told by the CV that holds me (`heldMode`),
		// a role of its own: the chord scaled and moved, or the lines' span
		// and start.
		const bool heard = !(told() & tree::TOLD_PLAIN) && heldYet;
		modeWork = heard ? heldWork : HELD_UNSAID;
		if (modeWork == HELD_ROLL) {
			notesWork = false; // x is a multiplier on the chord, not a span of notes
			ofs.unitWork = OfsCore::UNIT_ROLL;
		}
		// The job, once the left has had this wake: what
		// `shape` and `publishJack` go by, and whether the gain glides.
		jobWork = jobOf();
		gainKnobWake = gainGlides(notesWork, modeWork) ? gainGlide.played(gainSet()) : gainSet();
		gainWake = clamp(driven(gainKnobWake), -GAIN_MAX, GAIN_MAX);
		clampWork = clampSet();
		const uint32_t t = told();
		byAtnWork = (t & tree::TOLD_ATN_WINDOW) != 0;
		crvWork = (t & tree::TOLD_PLAIN) != 0;
		// Nothing that holds me uses me, as they say it (`usedBy`): a Qnt
		// whose window nothing past it reads, a Trg that took no threshold in
		// its mode, are not in play by the parse's word alone.
		aloneWork = standsAlone();
		markUnread(jobWork == JOB_SHAPER && !usedNow() && !aloneWork);
	}
	/** Alone: no module of ours stands beside me, nothing holds me and I
	hold nothing, so I am a standalone module and look it, in the yellow
	every lone module shares. Grey is for an Atn bonded into a rig that
	nothing of it uses, ATN TRG as much as ATN QNT, shaping or taking the left alike. */
	bool standsAlone() {
		const Sides& n = sides();
		bool alone = told() == 0 && !n.left && !n.right;
		for (int i = 0; alone && i < (int)slots.size(); i++)
			alone = !slot(i).filled();
		return alone;
	}
	/** Nothing of me used: OUT giving the offset alone where it is cabled,
	the faded white where it is not (`AtnLane::colour`). */
	void markUnread(bool unread) {
		outOnlyWork = unread && outputs[CV_OUTPUT].isConnected();
		overriddenWork = unread && !outOnlyWork;
	}
	void publishReads() override {
		Cartridge::publishReads();
		ofs.publish();
		snapScale.store(snapWork);
		notesNow.store(notesWork, std::memory_order_relaxed);
		modeNow.store(modeWork, std::memory_order_relaxed);
		takenColNow.store(outParse ? outColWork : -1, std::memory_order_relaxed);
		fedColNow.store(fedColWork, std::memory_order_relaxed);
		overriddenNow.store(overriddenWork, std::memory_order_relaxed);
		byAtnNow.store(byAtnWork, std::memory_order_relaxed);
		outOnlyNow.store(outOnlyWork, std::memory_order_relaxed);
		aloneNow.store(aloneWork, std::memory_order_relaxed);
		crvNow.store(crvWork, std::memory_order_relaxed);
		// Whether there is anything to meter, worked out here and not in my
		// own wake: the router writes into me at the Seq's wake, which
		// publishes me without running me, so a field my `publishJack` filled
		// would latch whichever Atn was routed when the rig last woke whole.
		// Put down at once when I
		// stop.
		const bool metering = running();
		if (!metering)
			meter.rest();
		meter.meteringNow.store(metering, std::memory_order_relaxed);
		// Read by the screens, so they are handed over together at the end of
		// the wake rather than as they are worked out: see the three-pass
		// rule in Engine.cpp.
		jobNow.store(jobWork, std::memory_order_relaxed);
		claimedNow.store(claimedByReader(), std::memory_order_relaxed);
		outNow.store(outParse, std::memory_order_relaxed);
		vcaNow.store(vcaParse, std::memory_order_relaxed);
		canGrabNow.store(canGrabWork, std::memory_order_relaxed);
		grabsPolyNow.store(grabsPolyWork, std::memory_order_relaxed);
		// What the hand's guess and new bonds go by (`AtnWidget::bond`): a
		// window slot holding me, which only an Atn shaping can be held by,
		// and the neighbours this wake's parse saw, as the hand's run counts
		// them (`runOf`: ours, and bonding to its left), so the switch is only
		// ever turned on the parse of where I stand now. A step whose window
		// is read past its Seq is told so by the Seq (`Seq::parsed`), so a
		// fresh Atn set into that block stays a shaper.
		windowHeldNow.store((told() & (tree::TOLD_WINDOW | tree::TOLD_ATN_WINDOW)) != 0, std::memory_order_relaxed);
		shapesRightNow.store(((told() | wouldHear()) & tree::TOLD_WINDOW) != 0, std::memory_order_relaxed);
		heldThroughNow.store(claimedByReader() && (told() & tree::TOLD_WINDOW), std::memory_order_relaxed);
		crvRightNow.store(((told() | wouldHear()) & tree::TOLD_PLAIN) != 0, std::memory_order_relaxed);
		const Sides& n = sides();
		leftSeenNow.store(n.leftId, std::memory_order_relaxed);
		rightSeenNow.store(n.rightId, std::memory_order_release);
		colorlessNow.store(outParse && colorlessWork, std::memory_order_relaxed);
		stripNow.store(outParse ? stripAt : -1, std::memory_order_relaxed);
		stripEmptyNow.store(stripEmptyWork, std::memory_order_relaxed);
		const bool idle = overriddenWork || (outParse && stripAt >= 0 && stripEmptyWork);
		const bool gainIdle = idle || outOnlyWork || (aloneWork && !vcaParse);
		idleLanesNow.store((gainIdle ? 1 : 0) | (idle ? 2 : 0), std::memory_order_relaxed);
	}
	/** A VCA: a cable in my own jack, and nobody having claimed the block I
	stand in.

	The claim is the third of those and was the last to be added. A reader
	walking a block uses it as a sequential switch: the Seq has the block
	whole and chooses one member a step, and what the chosen one is worth is
	its settings, not its jack. Without the clause every claimed Atn with a
	cable in its input went on being a VCA of its own, so a block of eight
	under a Seq was eight VCAs as well as a switch: they all showed as
	acting, which put the playhead's colour on every one of them at every
	step instead of on the one the playhead was standing on, and they went
	on mixing sideways underneath it. A claimed Atn is a configurator and
	nothing else. */
	bool isVca() { return cv.live() && !claimedByReader(); }
	/** My signal: the cable in my own jack, every channel of it. A Poly's
	channels are the OUT strips' after it, never an ordinary Atn's. */
	int inN() { return cv.plugged() ? std::max(1, inputs[IN_INPUT].getChannels()) : 0; }
	float inV(int c) { return cv.plugged() ? inputs[IN_INPUT].getVoltage(c) : 0.f; }
	/** Lit, only where my arrow takes something: a lit arrow with nothing
	to its left plays my offset, as an unlit one does. */
	bool carriesSignal() override { return cv.live() || (outParse && (grab || stripPoly)); }
	/** Whether I move voltage at all.

	An Atn with nothing in its own two jacks is a configurator: it tells the
	rig what its top and its offset are and produces nothing itself. It is
	only a cable in the input, in the output, or a mix arriving from the left
	that switches the processing on. With the input alone, it scales what
	arrives; with the output alone, the input is presumed nought and what
	comes out is the offset, which is worth having and worth showing; with
	neither, there is nothing to produce and nothing to meter.

	The CV jacks over the two lanes do not count. They drive the settings,
	which is the configurator's own work, and a rig of Atns setting up a Seq
	should not start putting voltage on jacks because one of them has a
	cable on its offset. The symptom was the meter: an offset with no signal
	under it made `peak` the whole of the offset, so a module carrying
	nothing showed a full swing and asked for a tick every sample to show
	it. */
	bool running() {
		// A reader walking the block does not stand me down: the Seq takes my
		// setting as a step, and my own jacks go on being an attenuator of my
		// own cable.
		return outParse || cv.live() || outputs[CV_OUTPUT].isConnected() || routed.any();
	}
	/** A Seq's ROUTE while its playhead stands on me: added to my own cable
	at my jack, and my jack rewritten at once, since the router moves at
	the Seq's wake and not at mine. */
	void route(int lane, bool on, const float* volts, int n) override {
		if (outParse)
			return; // lit, my jack is what my arrow takes: nothing is routed into me (`sound`)
		if (!routed.take(lane, on, volts, n))
			return;
		written();
		if (!claimedByReader())
			return;
		if (cv.live() || routed.any())
			ownJack();
		else {
			// Left by the router with nothing of my own: my jack as a step
			// with nothing to process, the offset only while a cable waits on
			// it, as `publishJack` would have it, not the routed offset after
			// the playhead has left.
			jackN = 1;
			outputs[CV_OUTPUT].setChannels(1);
			put(0, running() ? ofsVolts() : 0.f);
		}
	}
	/** My jack as a step of a Seq's block: my own cable, and the router where
	it is routed to me, at my top, every voice of either, a mono one held
	across the other's voices as the Seq holds it (`soundVoices`). */
	void ownJack() {
		const float shift = ofsVolts();
		const int nc = std::max({1, inN(), routed.any() ? routed.n : 0});
		jackN = nc;
		outputs[CV_OUTPUT].setChannels(nc);
		for (int c = 0; c < nc; c++) {
			const float r = !routed.any() ? 0.f : routed.n == 1 ? routed.v[0] : c < routed.n ? routed.v[c] : 0.f;
			const float own = cv.live() ? cv.getVoice(c) : 0.f;
			// No rails (Signal.hpp): a cable from my jack back into the Seq's
			// ROUTE is a loop, and a loop at a gain over one climbs until `sane`.
			put(c, held((own + r) * gainWake / CV_VOLTS + shift, gainWake, shift));
		}
	}
	/** What my own jack carries as a VCA, channel one: my cable taken at my
	top and then lifted by the offset. This is what the Atn on the right
	takes. */
	float vcaOut() {
		const float shift = ofsVolts();
		return held(cv.get() * gainWake / CV_VOLTS + shift, gainWake, shift);
	}
	/** The window I hand my right, boost and all: an Atn at x1.84 makes the
	CVs past it reach 18.4 V, since nothing holds a jack to rails. */
	void shape(Mapping& m) override { shapeWith(m, ofsAmountWork()); }
	/** The same, as though no scale arrived from my left, the bar not yet
	landed on any keyboard: what a keyboard of the same lane further right
	lays again on its own keys (`Qnt::shape`). */
	void shapeAfresh(Mapping& m) override { shapeWith(m, ofsPlayedIn(ofs.unitWork, Scale())); }
	void shapeWith(Mapping& m, float amount) {
		// The Atn that set the range, for a keyboard of the same lane further
		// right to lay it again on its own keys (`Qnt::shape`).
		m.atnNode = this;
		if (modeWork == HELD_ROLL) {
			// The window as an Atn at x1 leaves it, and the chord scaled
			// about its lowest note and moved by the cents (`Slots::rollVolts`).
			shapeAtn(m, CV_VOLTS);
			m.rollGain = gainWake / CV_VOLTS;
			m.rollCents = OfsCore::cents(amount);
			return;
		}
		if (divides(modeWork)) {
			// The lines divide the window the lanes show, x either side of
			// the offset, so a fresh Atn divides minus ten to ten as it shows;
			// and the harmonic
			// series starts at the offset, scaled by x over ten, so a fresh
			// Atn at x10 leaves it as it stands and x5 brings its octaves to
			// half a volt; `Divider`.
			shapeAtn(m, gainWake);
			m.preShift += OfsCore::volts(amount);
			m.divOrigin = OfsCore::volts(amount) - std::fabs(gainWake);
			m.divGain = 2.f * gainWake / CV_VOLTS;
			m.divCentre = OfsCore::volts(amount);
			m.divScale = gainWake / CV_VOLTS;
			return;
		}
		if (plainHeld()) {
			// Volts, the scale left on for what stands further right.
			shapeAtn(m, gainWake);
			m.preShift += OfsCore::volts(amount);
			return;
		}
		if (m.quantized())
			shapeAtnNotes(m, gainWake);
		else
			shapeAtn(m, gainWake);
		ofs.shape(m, amount);
	}
	/** A reader's step, handed the reader's cable: multiply, then add, the
	gain first and the offset after it, which is an OFS standing immediately
	BEHIND an ATN. This is what a fader and an offset mean everywhere else,
	and it is what makes the offset's own reading true. Add first and the gain
	scales the offset too, so a lane saying +10 V puts two volts on the jack
	at a gain of a fifth, and a lane saying +24 semitones transposes by rather
	fewer. Multiply first and the offset is the displacement of the output, in
	the unit the lane says, whatever the gain is doing; the order is chosen on
	its own merits. */
	float act(Signal& s, int color, int* value) override {
		const float amount = ofsAmountLeft();
		// An Ofs with nothing arriving is a source and puts its offset on its
		// jack; an Atn is not, and never was, so what the offset does to an
		// absent signal is not allowed to become one.
		const bool arrived = s.present;
		rangeStage(s, gainWake);
		const float out = ofs.act(s, winLeft, amount);
		return arrived ? out : 0.f;
	}
	/** The jack, which a Poly or a MIX gathering me takes, so the gate has
	to see every channel of it move, voices trading volts under a still
	first channel included. */
	int goneN = -1;
	float goneJack[PORT_MAX_CHANNELS] = {};
	bool published() override {
		bool any = Member::published();
		bool moved = jackN != goneN;
		for (int c = 0; c < jackN; c++)
			moved |= jackV[c] != goneJack[c];
		if (moved) {
			goneN = jackN;
			for (int c = 0; c < jackN; c++)
				goneJack[c] = jackV[c];
		}
		return any || moved;
	}
	/** The jack: my own cable at my top where I am a VCA, and what the rig
	makes of me otherwise. */
	void publishJack() override {
		const float shift = ofsVolts();
		// A VCA wherever a cable of my own is in play.
		if (jobWork == JOB_VCA) {
			colorWork = 0;
			actingWork = true;
			// A VCA takes whatever arrives: a poly cable comes out poly, every
			// channel scaled by the top. A poly cable in the VCA jack gives
			// every channel its own gain, which is a poly VCA proper; a mono
			// one drives them all together. The offset's jack the same.
			const int nc = std::max(1, inN());
			const int gn = inputs[VCA_INPUT].getChannels();
			const bool polyOfs = ofsCv.voices() > 1;
			jackN = nc;
			outputs[CV_OUTPUT].setChannels(nc);
			for (int c = 0; c < nc; c++) {
				// A poly cable in the gain jack gives every channel its own
				// gain.
				const float g = gainCv.live() && gn > 1
				                    ? clamp(gained(gainKnobWake, inputs[VCA_INPUT].getVoltage(std::min(c, gn - 1))),
				                            -GAIN_MAX, GAIN_MAX)
				                    : gainWake;
				const float s = polyOfs && c > 0 ? ofsVolts(c) : shift;
				put(c, held(inV(c) * g / CV_VOLTS + s, g, s));
			}
			return;
		}
		if (!claimedByReader())
			routed.clear(); // no Seq walking me, so nobody routing to me
		if (claimedByReader() && (cv.live() || routed.any())) {
			// A step of a Seq's block with a cable of my own, or the Seq's
			// router: my jack is that at my top, as it would be standing
			// alone.
			ownJack();
			return;
		}
		jackN = 1;
		outputs[CV_OUTPUT].setChannels(1);
		// The offset alone where a cable is waiting on the output, the input
		// being presumed nought; and nought where I am nobody's processor,
		// which is what `running` says.
		put(0, running() ? shift : 0.f);
	}
	/** What my meter shows: what my jack carries, a lane a channel of it,
	not a poly jack's loudest voice alone. Returns how many, one at the least.
	What my jack carries is `sane` of what I worked out (`put`), so the
	meter takes the same stop: a loop at a gain over one climbs until the
	number is no number, the jack gives nought, and the meter read minus
	infinity beside it (two Atns cabled into each other). */
	int metered(float* v) {
		v[0] = 0.f;
		if (!running())
			return 1;
		const bool signal = outParse || (claimedByReader() && (cv.live() || routed.any())) || isVca();
		if (!signal) {
			v[0] = ofsVolts();
			return 1;
		}
		if (jackN <= 1) {
			v[0] = sane(outParse || claimedByReader() ? jackV[0] : vcaOut());
			return 1;
		}
		const int n = std::min(jackN, (int)PORT_MAX_CHANNELS);
		for (int c = 0; c < n; c++)
			v[c] = sane(jackV[c]);
		return n;
	}
	/** A Poly's channels of me: what my jack carries, every channel of it,
	whether a cable is in my output or not, which is why they are read from
	`jackV` and not off the port, where Rack keeps no count without a cable.
	*/
	int bundle(float* v) override {
		for (int c = 0; c < jackN; c++)
			v[c] = sane(jackV[c]);
		return jackN;
	}
	/** A change of the arrow or of the cable in IN is a change of the parse,
	which the host asks of me on the control period, as it sweeps the knobs. */
	/** A VCA's or a lit Atn's claim on me as its peer leaves me acting,
	since it shapes nothing in my place (`TOLD_PEER_UNSHAPED`): ATN VCA CV is
	ATN CV to the CV.
	A Seq walking the block still makes me its step. */
	bool acts() override {
		const uint32_t t = told();
		return !claimed() || ((t & tree::TOLD_PEER_UNSHAPED) && !tree::walked(t));
	}
	bool parseStale() override {
		return cabledSet() != vcaParse || (grabSet() && !vcaParse) != outParse ||
		       (standaloneWanted && !vcaParse && !outParse) != standaloneParse;
	}
	/** A Seq's step. Lit, a step of my own: what my jack carries, my arrow's
	signal scaled and offset, every channel of it, whatever the Seq hands me,
	so SEQ ATN ATN ATN SEQ, the arrows lit, steps through three ranges of the
	one Seq, and POLY ATN ATN SEQ through the strips. Worked out at my own
	wake, which comes before the Seq's, standing to its left. Unlit, a step
	as any member's (`Cartridge::sound`). */
	void sound(int sub, const Signal& sig, int col, Sound& o) override {
		if (!outParse) {
			// Standalone, my IN reads nought where nothing is routed to me.
			if (standaloneParse && !sig.present && !cv.live()) {
				Signal none;
				none.setVolts(0.f, col, Mapping());
				Cartridge::sound(sub, none, col, o);
				return;
			}
			Cartridge::sound(sub, sig, col, o);
			return;
		}
		o.window = Mapping();
		o.signal.setVolts(jackV[0], col, Mapping());
		o.semis = 12.f * jackV[0];
		o.note = (int)std::round(o.semis);
		o.voices = std::max(1, std::min(jackN, (int)PORT_MAX_CHANNELS));
		for (int c = 1; c < o.voices; c++)
			o.voice[c] = jackV[c];
	}
	void evaluate(const ProcessArgs& args) override {
		if (outParse)
			outEvaluate();
		else
			Cartridge::evaluate(args);
	}
	/** The colour of what I read, for my screen and for a gatherer of me, a
	Seq's own; minus one for none. */
	int outColWork = -1;
	/** The same for my arrow, which fills with the colour of what it takes
	whoever stands on me: a Seq walking me lights my glass in its own
	colour, never my arrow. */
	std::atomic<int> takenColNow{-1};
	/** Whether what I read in OUT is colourless, for my screen to be white. */
	bool colorlessWork = false;
	std::atomic<bool> colorlessNow{false};
	int readerColor() override { return outParse ? outColWork : -1; }
	/** The window my jack's notes were played in, for an OUT Atn reading me:
	the one I read them in, while I read notes. */
	const Mapping* playedOut() override { return outParse && notesWork ? &winLeft : Cartridge::playedOut(); }
	/** A note a jack carries, if it is exactly one of the pattern's as the
	pattern sounds it, its cents and all: `k`, true. A voltage between them is
	not a note, and is moved as volts (`outEvaluate`). Read from the whole
	cents once (`tunedOf`), so the test is of integers. */
	static bool noteOfJack(float v, const Scale& scale, int& k) {
		const long c = std::lround((double)v * 1200.0);
		const Tuned t = tunedOf(v);
		// A key detuned by up to 99 cents sounds within a semitone of itself.
		for (int d : {0, -1, 1}) {
			const int n = t.semis + d;
			if (scale.has(n) && (long)n * 100 + scale.centsOf(n) == c) {
				k = n;
				return true;
			}
		}
		return false;
	}
	/** A lit Atn's wake: the signal to my left (`grab`), every channel
	scaled by the gain and moved by the offset onto my jack, gain first and offset after as everywhere (`act`). Where
	what I read carries the notes of a keyboard (`playedOut`), I am in notes:
	the gain scales the note about C4 and lands it on the pattern, and the
	offset transposes it along the pattern as an offset after a Qnt does
	(`shiftAfterSnap`), so x1 and nought give the very note that came in. */
	bool stripEmptyWork = false;
	void outEvaluate() {
		// A strip reads its Poly, whatever strip stands between. A cable in
		// IN is no business of this wake: it takes me out of this rig
		// (`beforeParse`), and until the parse it asks for has run I read
		// what the parse gave me.
		Member* src = stripPoly ? stripPoly : grab;
		const Mapping* played = src ? src->playedOut() : nullptr;
		const bool notes = played && played->scale.any() && !played->scale.spread;
		winLeft = notes ? *played : Mapping();
		winSeen = winLeft;
		winIn = winLeft;
		winOut = Mapping();
		snapWork = notes ? winLeft.scale : Scale();
		ofs.unitWork = notes ? OfsCore::UNIT_SEMIS : OfsCore::UNIT_VOLTS;
		notesWork = notes;
		modeWork = HELD_UNSAID;
		jobWork = JOB_VCA;
		// In notes the gain lands each note on the pattern: the setting
		// itself, never a glide through the notes between.
		gainKnobWake = notes ? gainSet() : gainGlide.played(gainSet());
		gainWake = clamp(driven(gainKnobWake), -GAIN_MAX, GAIN_MAX);
		clampWork = clampSet();
		fedColWork = -1;
		byAtnWork = false;
		crvWork = false;
		// Taking the left with nothing there to take I am standalone, and
		// bonded with nothing of me used I am the faded white as a shaper
		// nothing reads is, never the yellow of a lone module.
		aloneWork = standsAlone();
		markUnread(!src && !usedNow() && !claimedByReader() && !aloneWork);
		heldNow = hoveredNow = false;
		touched.store(false, std::memory_order_relaxed);
		hovered.store(false, std::memory_order_relaxed);
		holdUntil = hoverUntil = -1;
		float in[PORT_MAX_CHANNELS] = {};
		int n = 1;
		stripEmptyWork = false;
		if (stripPoly) {
			// My channel of the Poly, one; past its last, idle (below).
			float all[PORT_MAX_CHANNELS] = {};
			const int m = std::min(stripPoly->channelsOut(all), (int)PORT_MAX_CHANNELS);
			stripEmptyWork = stripAt >= m;
			in[0] = stripEmptyWork ? 0.f : all[stripAt];
		}
		else if (src) {
			// Nothing, a Poly with every row off, is one channel of
			// nought: the offset alone, as an Atn with nothing in it gives.
			n = src->channelsOut(in);
			if (n <= 0) {
				n = 1;
				in[0] = 0.f;
			}
			n = std::min(n, (int)PORT_MAX_CHANNELS);
		}
		const float amount = ofsAmountLeft();
		const int semis = OfsCore::semis(amount);
		const float shift = OfsCore::volts(amount);
		const float centre = notes ? (float)semis / 12.f : shift;
		jackN = n;
		outputs[CV_OUTPUT].setChannels(n);
		// A strip past the Poly's last channel is idle, rather than
		// giving its offset alone as a hidden feature: one channel of nought, as an Atn that is nobody's
		// processor gives (`publishJack`), and looking it, as an overridden
		// Atn does (AtnLane's `colour`).
		for (int c = 0; c < n && stripEmptyWork; c++)
			put(c, 0.f);
		for (int c = 0; c < n && !stripEmptyWork; c++) {
			const float v = in[c];
			int k = 0;
			if (notes && noteOfJack(v, winLeft.scale, k)) {
				// The gain about C4, landed on the pattern; x1 is the note
				// itself, an integer untouched.
				if (gainWake != CV_VOLTS)
					k = nearestInScale((float)k * gainWake / CV_VOLTS, winLeft.scale);
				const int moved = shiftAfterSnap(winLeft.scale, k, semis);
				put(c, held(winLeft.scale.tunedVolts(moved), gainWake, centre));
			}
			else
				put(c, held(v * gainWake / CV_VOLTS + centre, gainWake, centre));
		}
		outColWork = !src ? -1
		             : stripPoly ? (stripEmptyWork ? -1 : stripPoly->feedColor(stripAt))
		                         : src->readerColor();
		fedColWork = stripPoly ? outColWork : -1;
		colorWork = src ? std::max(outColWork, 0) : -1;
		// What I read has no colour of its own, a Mix's sum, a Poly's cable,
		// a CV: white, as a gatherer shows a colourless row
		// (`gatheredColor`), never the unread yellow.
		colorlessWork = src && outColWork < 0 && !stripEmptyWork;
		actingWork = true;
	}
	int dimmedOut() override { return relaxed(claimedNow) && !liveNow() ? 1 : 0; }
	bool spanOut(bool& notes, int& foot, int& top) override {
		notes = relaxed(notesNow);
		float lo, hi;
		noteSpan(gainInForce(), spanScale(), lo, hi);
		spanEnds(lo, hi, foot, top);
		return true;
	}
	bool meterOut(float& level, float& anchor, int lane) override {
		level = lane < relaxed(meter.lanesNow) ? relaxed(meter.levelNow[lane]) : 0.f;
		anchor = relaxed(meter.anchorNow);
		return relaxed(meter.meteringNow);
	}
	int meterLanes() override { return relaxed(meter.lanesNow); }
};
