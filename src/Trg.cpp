// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Panel.hpp"

/** Trg, TRG on the plate: a bar of one to sixteen trigger rings, latching gates
or pulse buttons alone and a drum's steps under a Seq. */

using namespace rig;

namespace {

/** A ring's gestures, the cable under command first, since it
is the one a hand would not guess, then
the stroke across the rings, shift's count, and the two few will need. A
plain click is learned by behaviour and said by nothing, which keeps the
lines few enough not to cover the rig. */
const Gesture G_TRG_CABLE =
    declare(On::Drag, Mod::Command, Rank::Invented, RACK_MOD_CTRL_NAME "-drag: patch from this trigger");
/** The same stroke on a ring with a cable, which lifts it (`TrgRing::jackToLift`):
said where it is true, read as `G_TRG_CABLE`. */
const Gesture G_TRG_CABLE_MOVE =
    declare(On::Drag, Mod::Command, Rank::Invented, RACK_MOD_CTRL_NAME "-drag: move this trigger's cable");
const Gesture G_TRG_PAINT = declare(On::Drag, Mod::None, Rank::Known, "Drag across: light or clear triggers");
const Gesture G_TRG_FIRE_ACROSS = declare(On::Drag, Mod::None, Rank::Known, "Drag across: fire triggers");
const Gesture G_TRG_COUNT = declare(On::Drag, Mod::Shift, Rank::Known, RACK_MOD_SHIFT_NAME "-drag: add or remove triggers");
/** The same stroke where a Seq walks its block across, which sizes every Trg
of the block it walks as one (`TrgRing::sizedWith`): a reach past the part,
said where it is true. */
const Gesture G_TRG_COUNT_BLOCK = declare(On::Drag, Mod::Shift, Rank::Block, RACK_MOD_SHIFT_NAME "-drag: size all the SEQ's TRGs");
const Gesture G_TRG_INVERT = declare(On::Click, Mod::ShiftCommand, Rank::Rare,
                                     RACK_MOD_SHIFT_NAME "+" RACK_MOD_CTRL_NAME "-click: invert all triggers");
const Gesture G_TRG_MENU = declare(On::Menu, Mod::None, Rank::Rare, "Right-click: its cables");
/** A cable dropped on a ring plugs into its own jack (`TrgRing::onDragDrop`),
a gesture with no face, so on the bracket rather than in the lesson;
said on a ring with no cable of its own, where the lesson no longer does. */
const Gesture G_TRG_DROP = declare(On::Cable, Mod::None, Rank::Rare, "Drop a cable: patch this trigger");
/** Pulse mode's click where turning it on clears the pattern, a one-way
door said at once rather than after the pointer rests; turning it off
clears nothing, and is `G_TOGGLE`. */
const Gesture G_TRG_PULSE_CLEARS = declare(On::Click, Mod::None, Rank::Invented, "Click: toggle, clears the pattern");
/** A Trg set by hand over a Poly's channels given back to them, said only
while there is a Poly to give it back to (`TrgRing::onDoubleClick`). */
const Gesture G_TRG_FOLLOW = declare(On::Double, Mod::None, Rank::Invented, "Double-click: triggers follow the POLY");

/** A ring's value as the tooltip gives it: off or on, or, while a stroke
with shift held is setting how many rings there are, that count as
it stands (`Trg::sizingShown`); a pulse button's none, its name alone,
never lit (`Trg::pulseShown`); and a ring the IN cable or a Poly holds, the
cable's gate, as the ring shows it (`TrgRing::lit`). */
struct RingQuantity : WithHint<SwitchQuantity> {
	std::string getDisplayValueString() override;
	std::string getString() override;
};

struct Trg : Cartridge, Probe {
	MEMBER_FOOTPRINT
	enum ParamId {
		PULSE_PARAM,                            // alone, the rings pulse rather than toggle
		RING_PARAM,                             // the rings' switches, sixteen of them, top to bottom
		LEN_PARAM = RING_PARAM + TRG_RINGS,     // how many rings, one to sixteen
		OWN_PARAM,                              // set by hand over a Poly's channels, no face of its own
		PARAMS_LEN
	};
	enum InputId {
		IN_INPUT,
		RING_INPUT,                            // a ring's own gate in, sixteen of them, top to bottom
		INPUTS_LEN = RING_INPUT + TRG_RINGS
	};
	// What only the UI knows, handed over in the widget's step, for the
	// scheme (`schemeColor`).
	std::atomic<uint32_t> inRGB{0};
	std::atomic<bool> bondedUi{false};
	enum OutputId {
		GATE_OUTPUT,
		RING_OUTPUT,                           // a ring's own trigger out, sixteen of them
		OUTPUTS_LEN = RING_OUTPUT + TRG_RINGS
	};
	enum LightId {
		LIGHTS_LEN
	};

	/** The ring count a sizing stroke has reached, for the tooltip of the
	ring it began on, nought with none under way: the UI's alone, written and
	read in the widgets' step and draw. */
	int sizingShown = 0;

	/** Ring `r`'s switch. */
	static int ringParam(int r) { return RING_PARAM + r; }
	/** How many rings this Trg has, one to sixteen, eight unless a stroke
	with shift held said otherwise. */
	int rings() { return clamp((int)std::round(params[LEN_PARAM].getValue()), 1, TRG_RINGS); }

	/** What this Trg is right now: a reader reading it makes it a member
	of that reader's block, and nothing reading it leaves it alone, the one
	place its rings may pulse. */
	enum Mode { MODE_MEMBER, MODE_ALONE };
	Mode mode = MODE_ALONE;               // this wake's, for the engine
	std::atomic<int> modeNow{MODE_ALONE}; // the last published, for the screens
	// Not `member()`: that is Member's own virtual, returning a MemberFace*,
	// and a pointer is always true, so a test by that name would compile and
	// read always true to any caller holding a base pointer.
	bool isMember() const { return mode == MODE_MEMBER; }
	bool memberShown() const { return relaxed(modeNow) == MODE_MEMBER; }
	/** Whether a Seq set to Across walks my block, a Trg a step: then the
	block is so many drums side by side, and a sizing stroke sizes every Trg
	of it together. Told at
	the parse (`addLane`), which a turn of the Seq's Poly makes
	(`Seq::parseStale`), published with the mode. */
	bool acrossWork = false;
	std::atomic<bool> acrossNow{false};
	bool acrossShown() const { return relaxed(acrossNow); }
	void settle() override {
		Cartridge::settle();
		acrossWork = false;
	}
	int addLane(int col, bool across) override {
		acrossWork = acrossWork || across;
		return Cartridge::addLane(col, across);
	}
	/** Whether the rings are pulse buttons: alone, and the corner's pulse
	lit. A member's rings always toggle, whatever the param says until the
	write the adoption posted has landed (`evaluate`). */
	bool pulsing() { return !isMember() && params[PULSE_PARAM].getValue() > 0.5f; }
	bool pulseShown() { return !memberShown() && params[PULSE_PARAM].getValue() > 0.5f; }
	/** Alone and pulsing with a cable in my own IN: a gate to trigger
	converter, each channel's rise a trigger on its channel of OUT, where
	pulse off passes the gates whole. A Poly's feed is not a cable in IN and passes whole either
	way. */
	bool converting() { return pulsing() && inputs[IN_INPUT].isConnected(); }
	bool convertingShown() { return pulseShown() && inputs[IN_INPUT].isConnected(); }
	/** The pulse corner, there while nothing reads me and nothing feeds me:
	a Seq's member always toggles, and a Poly's gates pass whole either way,
	so there it would turn nothing a hand could hear. */
	bool pulseCornerShown() { return !memberShown() && !followingNow(); }
	/** Whether the pattern may be turned over (`TrgRing::invertRings`):
	pulse buttons hold none, and one my IN cable holds is the cable's. A
	Poly's feed is taken over by the stroke instead. */
	bool invertible() { return !pulseShown() && !inputs[IN_INPUT].isConnected(); }

	/** The pulses, asked by the hand a ring at a time (`askPulse`), bit 0
	the top ring, and taken on the audio thread's next sample. */
	std::atomic<int> pulseAsk{0};
	int64_t pulseUntil[TRG_RINGS] = {}; // the frame each ring's pulse ends on
	int pulsingWork = 0;                // the rings whose pulse is high
	bool postedToggle = false;          // the adoption's write is on its way
	/** UI thread, or a headless test: a pulse on ring `r`, if the rings pulse. */
	void askPulse(int r) override {
		if (r < 0 || r >= TRG_RINGS)
			return;
		pulseAsk.fetch_or(1 << r, std::memory_order_relaxed);
		wake(); // no param moved, so the table would not see it
	}

	// The jack is asked, not copied: the watch table notices the gates have
	// moved, so a copy would have nothing to do but go stale between the
	// sample the rig starts reading the jack and the sample anything tells it
	// to look again.
	int maskWork = 0;               // the mask in force this wake, for the jack and a Seq
	// Every Seq whose playhead stands on this Trg, packed as heads (`Heads`):
	// a head's place is the ring, or ACROSS for a Seq standing on the whole
	// Trg. Lane 0 is the nearest Seq to the right, so several on one ring
	// cluster in rig order.
	static const int ACROSS = Heads::ACROSS;
	static const int HEADS = Heads::MAX;
	std::atomic<uint64_t> headsPacked{0};
	Heads headsWork; // the same, this wake, before it is published
	// The lane and the head its line was packed as this wake, so its halo
	// on the same ring joins it rather than standing as a second head.
	int lastReadLane = -1, lastReadHead = -1;
	std::atomic<float> flash[TRG_RINGS]; // a decaying bloom per ring the playhead lands on lit
	// The same, the audio thread's own: it reads these and stores them into
	// `flash` for the screen, never loading what it stores.
	float flashWork[TRG_RINGS] = {};
	int flashLit = 0; // the rings whose flashWork is above nought, so asking costs one test
	int bloomWork = 0; // the rings struck this wake, whose bloom the publish hands the screen

	// Randomize rolls the rhythm, not the bar, the pulse or who sets the rings.
	bool rolled(int p) override { return p != LEN_PARAM && p != PULSE_PARAM && p != OWN_PARAM; }

	Trg() : Cartridge(manifest::M_TRG) {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int c = 0; c < TRG_RINGS; c++) {
			configSwitch<RingQuantity>(ringParam(c), 0.f, 1.f, 0.f, string::f("Trigger %d", c + 1), {"Off", "On"});
			flash[c].store(0.f, std::memory_order_relaxed);
		}
		configParam(LEN_PARAM, 1.f, (float)TRG_RINGS, (float)GATE_CHANNELS, "Triggers");
		paramQuantities[LEN_PARAM]->snapEnabled = true;
		configSwitch<WithHint<SwitchQuantity>>(PULSE_PARAM, 0.f, 1.f, 1.f, "Pulse mode", {"Off", "On"});
		// Whether a stroke has taken the rings over from a Poly's channels,
		// for the whole Trg, its feed being one thing; a param so it is
		// saved, undone and held in a scene. A new cable in the Poly's IN
		// gives it back (`PolyWidget::giveBack`).
		configHandOrPoly(this, OWN_PARAM);
		configInput(IN_INPUT, "Gates");
		configOutput(GATE_OUTPUT, "Gates");
		for (int c = 0; c < TRG_RINGS; c++) {
			configInput(RING_INPUT + c, string::f("Trigger %d", c + 1));
			configOutput(RING_OUTPUT + c, string::f("Trigger %d", c + 1));
		}
		std::fill(tippedRing, tippedRing + TRG_RINGS, -1);
		retipAll();
	}
	/** A ring's own jack is one gate; IN is all of them. */
	int channelsHeard(int input) const override { return input == IN_INPUT ? PORT_MAX_CHANNELS : 1; }

	/** Every part says what it does now: a ring
	what a click, a stroke and a dropped cable do, alone, pulsing, under a
	Seq or held by IN or a Poly's feed; its own jacks, which a cable dragged
	over the ring shows, what they carry; the pulse, IN and OUT what the
	gates do. Written again only when what is said moves. */
	void retipAll() override {
		retipJacks();
		const bool member = memberShown(), pulse = pulseShown(), across = member && acrossShown();
		const bool inCable = inputs[IN_INPUT].isConnected(), fed = !inCable && relaxed(fedMaskNow) >= 0;
		const bool byHand = !inCable && !fed && offeredNow();
		for (int c = 0; c < TRG_RINGS; c++) {
			const bool ownIn = inputs[RING_INPUT + c].isConnected(), ownOut = outputs[RING_OUTPUT + c].isConnected();
			const int said = member | pulse << 1 | across << 2 | inCable << 3 | fed << 4 | ownIn << 5 | ownOut << 6 | byHand << 7;
			if (said == tippedRing[c])
				continue;
			tippedRing[c] = said;
			retipRing(c, member, pulse, across, inCable, fed, byHand, ownIn, ownOut);
		}
	}
	int tippedRing[TRG_RINGS]; // UI thread: what each ring's tips last said, a bit a state (`retipAll`)

	/** What the black says where the Trg has one ring, the black being that
	ring's button: the ring's one name and its state as the value, "Trigger
	1: On", as the ring's own tooltip gives them (`RingQuantity`); what a
	press does there is on the bracket, with the ring's strokes. */
	std::string blackTip() { return paramQuantities[ringParam(0)]->getString(); }

	void retipRing(int c, bool member, bool pulse, bool across, bool inCable, bool fed, bool byHand, bool ownIn, bool ownOut) {
		const std::string name = string::f("Trigger %d", c + 1);
		const bool locked = inCable && !ownIn;
		const bool converting = pulse && inCable;
		// A cable out of a ring: under a Seq a trigger at its step, or across
		// at its Trg's step while lit (`fireRings`); alone its channel of OUT.
		const char* outWhat = member ? across ? "a pulse each time the SEQ steps onto this TRG with it lit"
		                                      : "a pulse each time the SEQ reaches it, lit or not"
		                             : pulse ? "its pulses" : "its gate";
		const char* act = pulse ? "fires" : "flips";
		// Held by IN, the surprise is that the hand is refused
		// (`TrgRing::locked`), in the CV's and the CRV's form; by a Poly,
		// that the hand takes it over, and gives it back, as on a CV.
		std::string lesson;
		if (locked)
			lesson = converting ? "The cable in IN fires it as its gate rises; a click does nothing while it is in."
			                    : "The cable in IN sets it; a click does nothing while it is in.";
		else if (fed)
			lesson = "Follows the POLY to its left until a click or a stroke takes it over.";
		else if (byHand)
			lesson = "Set by hand in place of the POLY's channels.";
		else if (ownIn && ownOut)
			lesson = string::f("The cable into it %s it at each pulse; the cable out of it gets %s.", act, outWhat);
		else if (ownIn)
			lesson = string::f("The cable into it %s it at each pulse.", act);
		else if (ownOut)
			lesson = string::f("The cable out of it gets %s.", outWhat);
		else
			lesson = string::f("A cable into it %s it at each pulse; a cable out of it gets %s.", act, outWhat);
		Gestures gs = {ownIn || ownOut ? &G_TRG_CABLE_MOVE : &G_TRG_CABLE};
		if (byHand)
			gs.insert(gs.begin(), &G_TRG_FOLLOW);
		if (!locked)
			gs.push_back(pulse ? &G_TRG_FIRE_ACROSS : &G_TRG_PAINT);
		gs.push_back(across ? &G_TRG_COUNT_BLOCK : &G_TRG_COUNT);
		if (invertible())
			gs.push_back(&G_TRG_INVERT);
		if (ownIn || ownOut)
			gs.push_back(&G_TRG_MENU);
		else
			gs.push_back(&G_TRG_DROP);
		retipParam(this, ringParam(c), {name.c_str(), lesson.c_str(), gs});

		// Its own jacks, shown under a cable dragged over the ring.
		const std::string in = string::f("%s this trigger at each pulse it carries, as a click does%s.",
		                                 pulse ? "Fires" : "Flips",
		                                 inCable ? ", in place of its gate in IN"
		                                 : fed   ? ", in place of the POLY's channel"
		                                         : "");
		retipIn(this, RING_INPUT + c, {name.c_str(), in.c_str()});
		const char* out = member ? across ? "A pulse each time the SEQ steps onto this TRG with this trigger lit."
		                                  : "A pulse each time the SEQ reaches this step, lit or not."
		                  : ownIn      ? pulse ? "Its pulses, as on its channel of OUT."
		                                       : "Its gate, as on its channel of OUT."
		                  : converting ? "A pulse as its gate in IN rises, as on its channel of OUT."
		                  : inCable    ? "Its gate in IN, as on its channel of OUT."
		                  : fed        ? "Its gate from the POLY, as on its channel of OUT."
		                  : pulse      ? "Its pulses, as on its channel of OUT."
		                               : "Its gate, as on its channel of OUT.";
		retipOut(this, RING_OUTPUT + c, {name.c_str(), out});
	}

	/** The pulse, IN and OUT say what the gates do now: alone with a cable
	in IN the pulse is the switch between the gates passing whole and each
	rise a trigger, and a
	Poly's feed passes whole either way. */
	void retipJacks() {
		const bool member = memberShown(), pulse = pulseShown();
		const bool inCable = inputs[IN_INPUT].isConnected(), fed = !inCable && relaxed(fedMaskNow) >= 0;
		const bool ownIn = (inCable || fed) && ringsIn();
		const int said = member | pulse << 1 | inCable << 2 | fed << 3 | ownIn << 4;
		if (said == tippedJacks)
			return;
		tippedJacks = said;
		const bool converting = pulse && inCable;
		const char* pulseLesson = fed       ? "The POLY's gates pass through as they are either way."
		                          : inCable ? pulse ? "Off, the gates in IN pass through as they are."
		                                            : "On, each gate in IN becomes a pulse as it rises."
		                          : pulse   ? "Off, a trigger stays lit until put out."
		                                    : "On, a trigger fires once rather than staying lit.";
		retipParam(this, PULSE_PARAM, {"Pulse mode", pulseLesson, Gestures{pulse ? &G_TOGGLE : &G_TRG_PULSE_CLEARS}});
		const char* in = converting ? "Each gate fires its trigger as it rises, a channel each."
		                 : inCable  ? "Its gates light the triggers, a channel each; the pattern returns when the cable "
		                              "is pulled."
		                 : fed      ? pulse ? "A cable here takes the place of the POLY's gates, and each of its gates "
		                                      "fires its trigger as it rises."
		                                    : "A cable here takes the place of the POLY's gates."
		                 : pulse    ? "Poly gates here fire the triggers, a channel each, at each rise."
		                            : "Poly gates here light the triggers, a channel each, in place of the pattern.";
		retipIn(this, IN_INPUT, {"Gates", in});
		// A ring with a cable of its own keeps its own channel (`prepare`),
		// said as a sentence of its own after what passes.
		const char* own = !ownIn ? ""
		                  : converting ? " A trigger with its own cable in fires from that cable."
		                               : " A trigger with its own cable in keeps its own gate.";
		std::string out;
		if (member)
			out = inCable ? string::f("The gates in IN, passed through as they are; the SEQ to its right plays them in time.%s", own)
			      : fed   ? string::f("The POLY's gates, passed through as they are; the SEQ to its right plays them in time.%s", own)
			              : "The lit triggers as steady gates, a channel each; the SEQ to its right plays them in time.";
		else
			out = converting ? string::f("A pulse on each channel as its gate in IN rises.%s", own)
			      : inCable  ? string::f("The gates in IN, passed through as they are.%s", own)
			      : fed      ? string::f("The POLY's gates, passed through as they are.%s", own)
			      : pulse    ? "A channel per trigger, fired at each click."
			                 : "A channel per trigger, high while it is lit.";
		retipOut(this, GATE_OUTPUT, {member || !pulse || fed || (inCable && !converting) ? "Gates" : "Triggers", out.c_str()});
	}
	int tippedJacks = -1; // UI thread: what the pulse, IN and OUT last said, a bit a state

	/** The playheads standing on one ring, as colours, nearest Seq first,
	with whether each one's line stands here and whether its halo does (a
	CV's `playheadLanes`). Returns how many, so a ring with two readers
	clusters them the way a CV does rather than mixing them to a
	meaningless white. */
	int headsOn(int ring, int* cols, bool* line, bool* halo) {
		return Heads::on(relaxed(headsPacked), ring, cols, line, halo);
	}

	/** The rings, one bit each, bit 0 the top one. */
	int buttons() {
		int m = 0;
		for (int c = 0, n = rings(); c < n; c++)
			if (params[ringParam(c)].getValue() > 0.5f)
				m |= 1 << c;
		return m;
	}
	/** A cable in the IN jack, and the gates it carries: a channel a ring,
	high at a volt or more, the one knife-edge every Trg counts on. */
	bool cableIn() { return cableIn(fedMask); }
	int cableMask() { return cableMask(fedMask); }
	/** The same for the screen, with the feed as the last wake published it
	rather than the working `fedMask` the audio thread writes. */
	bool cableInNow() { return cableIn(relaxed(fedMaskNow)); }
	int cableMaskNow() { return cableMask(relaxed(fedMaskNow)); }
	bool cableIn(int fed) { return fed >= 0 || inputs[IN_INPUT].isConnected(); }
	/** My own cable wins over the Poly feeding me, as it does on a CV. */
	int cableMask(int fed) {
		if (fed >= 0 && !inputs[IN_INPUT].isConnected())
			return fed;
		const int n = std::min(inputs[IN_INPUT].getChannels(), rings());
		return gateMask(inputs[IN_INPUT].getVoltages(), n);
	}
	/** The channels at or above `TRIG_HIGH`, a bit each: my rings' gates,
	from my own cable or a Poly's channels alike. */
	static int gateMask(const float* v, int n) {
		int m = 0;
		for (int c = 0; c < n; c++)
			if (v[c] >= TRIG_HIGH)
				m |= 1 << c;
		return m;
	}
	/** A Poly's channels are my rings, as many as I have, a gate each, as a
	cable in the IN jack would be; minus one is nobody feeding me. */
	int fedMask = -1;
	std::atomic<int> fedMaskNow{-1};
	/** Set by hand, the channels are refused and the rings play, but
	still taken, so the members after me take the channels after mine. */
	int feed(const float* v, int n) override {
		const int k = std::min(n, rings());
		fedMask = own() ? -1 : gateMask(v, k);
		return k;
	}
	void unfeed() override { fedMask = -1; }

	// ---- A Poly's channels, followed or set aside. Fed, the rings show
	// the channels and a scene holds what they show; a click or a stroke on
	// a ring takes the Trg over (`takeOver`, `OWN_PARAM`) and the rings play
	// as the hand sets them until a double click gives it back to the Poly
	// (`followAgain`), or a new cable in the Poly's IN does. Ranked: a ring's
	// own cable, then my IN cable, then my hand, then the feed, then the
	// pattern under it.
	bool own() { return params[OWN_PARAM].getValue() > 0.5f; }
	int setAsideParam() override { return OWN_PARAM; }
	bool followingNow() override { return relaxed(fedMaskNow) >= 0 && !inputs[IN_INPUT].isConnected(); }
	/** Whether ring `c` is held by a cable, my IN's or a Poly's feed, as
	the last wake published it, and with `on` what it shows: the one
	definition the ring, its tooltip and a scene all ask. A ring with its
	own cable is its own switch and never held; converting, the IN cable's
	gates are triggers and hold every ring dark. */
	bool ringHeld(int c, bool& on) {
		on = false;
		if (!cableInNow() || ((relaxed(ringInNow) >> c) & 1))
			return false;
		on = !convertingShown() && ((cableMaskNow() >> c) & 1);
		return true;
	}
	/** A scene stores a held ring as it shows, and a recall leaves it alone.
	Pulse buttons hold no pattern, so a pulsing Trg's rings are the hand's
	whatever feeds them. */
	bool heldByCable(int param, float& shown) override {
		const int c = param - RING_PARAM;
		bool on = false;
		if (c < 0 || c >= rings() || pulseShown() || !ringHeld(c, on))
			return false;
		shown = on ? 1.f : 0.f;
		return true;
	}
	/** As many of a Poly's channels as I have rings, alone or in a block,
	since the rings are there to take them either way. */
	int feedWidth() override { return rings(); }
	/** The rings with a cable in their own jack, and those of them whose
	cable is at a volt or more, of the rings this Trg has: a ring's own cable
	is the ring, over the IN jack, a Poly's feed and the ring's switch alike,
	since it is the one that names it alone. */
	int ringsIn() {
		int m = 0;
		for (int c = 0, n = rings(); c < n; c++)
			if (inputs[RING_INPUT + c].isConnected())
				m |= 1 << c;
		return m;
	}
	/** The rings whose own cable has just sent a trigger, each ring's cable
	read as Rack reads a trigger, a Schmitt trigger: high at `TRIG_HIGH`, a
	volt, and low again only at `TRIG_LOW`, a tenth of one, a trigger being
	the rise from low to high. A cable plugged in, or there when the patch
	loads, counts as high until it has fallen, so plugging a cable or loading
	a patch never flips a saved pattern; its first sample is not read at all,
	since Rack steps the modules before the cables and a cable just added
	stands at nought for that one sample, which would read as a fall and
	make the next sample a trigger, so the ring is left unread and the next
	sample wakes the rig to read it (`everySample`). The latch moves once a
	wake, in `prepare`. A
	ring unplugged forgets. The latch is not the ring, lit while its cable
	stands at a volt; it is the edge that flips the ring (`prepare`). */
	int ringLatch = 0, ringInWas = 0, ringsUnread = 0;
	int triggeredRings(int in) {
		const int fresh = in & ~ringInWas;
		ringLatch |= fresh;
		ringInWas = in;
		ringsUnread = fresh;
		int rose = 0;
		for (int c = 0; in >> c; c++)
			if (((in & ~fresh) >> c) & 1) {
				const float v = inputs[RING_INPUT + c].getVoltage();
				if (v >= TRIG_HIGH) {
					if (!((ringLatch >> c) & 1))
						rose |= 1 << c;
					ringLatch |= 1 << c;
				}
				else if (v <= TRIG_LOW)
					ringLatch &= ~(1 << c);
			}
		ringLatch &= in;
		return rose;
	}
	int ringInWork = 0;
	std::atomic<int> ringInNow{0}; // the same, published for the screen

	/** The IN cable's channels read as triggers, each its own Schmitt
	trigger at `TRIG_HIGH` and `TRIG_LOW` as a ring's own cable is: the
	channels that have just risen, for the converter (`converting`). Kept in
	every mode, so turning pulse on over a gate held high fires nothing. A
	cable just plugged counts as high on every channel until it falls, and
	its first sample is left unread for the reason a ring's is
	(`triggeredRings`), so plugging a gate or loading a patch fires
	nothing. */
	int inLatch = 0;
	bool inPlugged = false, inUnread = false;
	int inRises() {
		if (!inputs[IN_INPUT].isConnected()) {
			inPlugged = inUnread = false;
			inLatch = 0;
			return 0;
		}
		if (!inPlugged) {
			inPlugged = inUnread = true;
			inLatch = (1 << TRG_RINGS) - 1;
			return 0;
		}
		inUnread = false;
		const int n = std::min(inputs[IN_INPUT].getChannels(), rings());
		int rose = 0;
		for (int c = 0; c < n; c++) {
			const float v = inputs[IN_INPUT].getVoltage(c);
			if (v >= TRIG_HIGH) {
				if (!((inLatch >> c) & 1))
					rose |= 1 << c;
				inLatch |= 1 << c;
			}
			else if (v <= TRIG_LOW)
				inLatch &= ~(1 << c);
		}
		inLatch &= (1 << n) - 1;
		return rose;
	}
	/** What is in force: the cable if there is one, else the rings, which
	are no gates at all while they pulse, and no gates either from a cable
	whose rises are triggers (`converting`); and over either, a ring with its
	own cable, which is its own switch, flipped by that cable. */
	int mask() { return mask(ringsIn()); }
	int mask(int in) {
		const int base = converting() ? 0 : cableIn() ? cableMask() : pulsing() ? 0 : buttons();
		return (base & ~in) | (buttons() & in);
	}

	/** A fresh bloom wherever a playhead strikes a lit ring, as its trigger
	fires, so each ring flashes on its own beat however many Seqs are walking,
	and a block of one ring on every step. Made in the wake, by the Seq
	striking, and handed the screen in `publishReads`, never worked out there
	from the playheads newly standing on a ring, which would bloom a ring the
	playhead never left only once.

	A ring's own jack fires whenever the walk lands on it, lit or not: the
	lit rings are what the Seq's own jack plays, and a cable from a ring says
	"at this step". Walked across, a POLY Seq standing on
	the whole Trg, a ring is a voice rather than a step, so there its jack
	fires only lit, as that ring's channel of the Seq's jack does. */
	void struck(int lane, int sub, int64_t until, uint32_t began, bool again) override {
		written(); // a write into me, said here too though `read` says so first
		const int at = sub < 0 ? (1 << TRG_RINGS) - 1 : 1 << clamp(sub, 0, TRG_RINGS - 1);
		const int m = maskWork & at;
		for (int c = 0; c < TRG_RINGS; c++)
			if ((m >> c) & 1)
				flashWork[c] = 1.f;
		flashLit |= m;
		bloomWork |= m;
		fireRings(lane, sub < 0 ? m : at, until, began, again);
	}
	/** The rings' own jacks fired as the reader fires its own: a pulse on
	each ring the walk lands on (`struck`), and on a landing re-aimed with no
	step begun none on a ring that lane already fired for that serial. Set on
	the jack where it happens, as a pulse is, and ended by the tick
	(`tickRings`). */
	int64_t ringUntil[TRG_RINGS] = {};
	int ringFiring = 0; // the rings whose own jack is high
	uint32_t ringBegan[MAX_LANES] = {};
	int ringFired[MAX_LANES] = {};
	/** A ring's own jack set, and `ringHigh` with it, so a wake writes only
	the jacks whose level moves rather than all sixteen. */
	int ringHigh = 0;
	void setRing(int c, bool on) {
		if (((ringHigh >> c) & 1) == (int)on)
			return;
		ringHigh ^= 1 << c;
		outputs[RING_OUTPUT + c].setVoltage(on ? 10.f : 0.f);
	}
	void fireRings(int lane, int m, int64_t until, uint32_t began, bool again) {
		// Only a jack with a cable in it has anyone to hear it, and a ring
		// fired keeps the tick on for its millisecond, at every step now that
		// a dark ring fires too.
		for (int c = 0; m >> c; c++)
			if (((m >> c) & 1) && !outputs[RING_OUTPUT + c].isConnected())
				m &= ~(1 << c);
		if (!m)
			return;
		const int l = clamp(lane, 0, MAX_LANES - 1);
		const bool same = ringBegan[l] == began;
		if (again && same)
			m &= ~ringFired[l];
		ringFired[l] = same ? ringFired[l] | m : m;
		ringBegan[l] = began;
		for (int c = 0; c < TRG_RINGS; c++)
			if ((m >> c) & 1) {
				ringUntil[c] = until;
				ringFiring |= 1 << c;
				setRing(c, true);
			}
	}
	void tickRings(const ProcessArgs& args) {
		if (!ringFiring)
			return;
		for (int c = 0; c < TRG_RINGS; c++)
			if (((ringFiring >> c) & 1) && args.frame >= ringUntil[c]) {
				ringFiring &= ~(1 << c);
				setRing(c, false);
			}
	}
	/** And the fade, which is the only part with anywhere to get to on its
	own: a screen value on the control period. */
	void fadeFlash(const ProcessArgs& args) {
		const float dt = (float)CONTROL_PERIOD * args.sampleTime;
		for (int c = 0; c < TRG_RINGS; c++) {
			const float f = flashWork[c];
			if (f > 0.f) {
				flashWork[c] = bloomFall(f, dt);
				flash[c].store(flashWork[c], std::memory_order_relaxed);
				if (flashWork[c] <= 0.f)
					flashLit &= ~(1 << c);
			}
		}
	}
	bool flashing() const { return flashLit != 0; }

	/** The pulses asked since the last sample fired, a millisecond each on
	their own channels, and those whose millisecond is up ended. Set on the
	jack where they happen, as a pulse is, and kept in `pulsingWork` so a
	wake's `publishJack` keeps them high. The bloom is a measurement made
	outside any wake, so it is stored where it is made. */
	void tickPulses(const ProcessArgs& args) {
		const int asked = pulseAsk.exchange(0, std::memory_order_relaxed);
		const int n = rings();
		Output& out = outputs[GATE_OUTPUT];
		if (asked && pulsing())
			for (int c = 0; c < n; c++)
				if ((asked >> c) & 1) {
					pulseUntil[c] = args.frame + pulseFrames(args.sampleRate);
					pulsingWork |= 1 << c;
					out.setVoltage(TRIG_VOLTS, c);
					setRing(c, true);
					flashWork[c] = 1.f;
					flashLit |= 1 << c;
					flash[c].store(1.f, std::memory_order_relaxed);
				}
		for (int c = 0; c < TRG_RINGS; c++)
			if (((pulsingWork >> c) & 1) && args.frame >= pulseUntil[c]) {
				pulsingWork &= ~(1 << c);
				const float v = c < n && (maskWork >> c) & 1 ? 10.f : 0.f;
				if (c < n)
					out.setVoltage(v, c);
				setRing(c, v > 0.f);
			}
	}
	/** The rings' flash, and a pulse to end; nothing else of its own. */
	bool wantsTick() override {
		return Cartridge::wantsTick() || flashing() || pulsingWork != 0 || ringFiring != 0 || ringsUnread != 0 ||
		       inUnread;
	}
	bool everySample(const ProcessArgs& args) override {
		tickPulses(args);
		tickRings(args);
		if ((args.frame % CONTROL_PERIOD) == 0)
			fadeFlash(args);
		return Cartridge::everySample(args) || ringsUnread != 0 || inUnread;
	}

	/** A ring written: the button takes the gate, so it is saved and shown
	as if pressed, and the rig wakes to publish it. Only the engine writes
	here, a Seq's REC take and a ring's own cable flipping it: the params the
	engine sets for itself, the patch performing on them rather than a hand's
	gesture, so neither is a step of undo. */
	void writeGate(int ring, bool on) {
		if (ring < 0 || ring >= TRG_RINGS)
			return;
		params[ringParam(ring)].setValue(on ? 1.f : 0.f);
		poke.store(true, std::memory_order_relaxed);
	}


	/** Which of the two this Trg is: a reader reading it makes it a
	member, and nothing reading it leaves it alone. */
	Mode modeOf() { return readByAny() ? MODE_MEMBER : MODE_ALONE; }
	void evaluate(const ProcessArgs& args) override {
		mode = modeOf();
		// Taken into a block while pulsing: the rings toggle from here on,
		// and the param is turned back to say so, by the hand's own undo
		// rather than the engine's write (`postWrite`), once.
		const bool pulseParam = params[PULSE_PARAM].getValue() > 0.5f;
		if (isMember() && pulseParam && !postedToggle) {
			postedToggle = true;
			postWrite(PULSE_PARAM, 0.f, "toggle triggers");
		}
		if (!pulseParam)
			postedToggle = false;
		if (isMember())
			pulsingWork = 0; // a member never pulses: what was high is let go
		else
			ringFiring = 0;  // nor does a Trg alone fire a ring as a Seq would
		Cartridge::evaluate(args);
	}

	/** The rings a cable lights, the IN jack's or a Poly's feed or a ring's
	own, as the last wake left them: a ring one of them lights afresh blooms
	as a struck one does, so a trigger arriving, however short, shows as a
	blink (`SHOW_FALL_SECONDS`, Pulse.hpp), where a ring lit only for the
	millisecond the cable is high would hardly be seen. */
	int cabledLit = 0;
	/** The rings this wake's cable bloomed, handed the screen in the
	publish and put down there, never in the clear: a field of my own wake
	that a clear zeroed would be moved by a wake that left me out. */
	int roseWork = 0;
	void prepare(const Mapping& win, const ProcessArgs& args) override {
		ringInWork = ringsIn();
		// A trigger on a ring's own cable is a press of the ring: toggling, it flips the ring,
		// written for itself as a REC take writes (`writeGate`), the pattern
		// saved and shown and no step of undo; pulsing, it fires the ring in
		// this same sample, its millisecond ended by `tickPulses` and its
		// bloom handed the screen in the publish.
		const int fired = triggeredRings(ringInWork);
		const bool pulses = pulsing();
		for (int c = 0; fired >> c; c++)
			if ((fired >> c) & 1) {
				if (!pulses)
					writeGate(c, !((buttons() >> c) & 1));
				else {
					pulseUntil[c] = args.frame + pulseFrames(args.sampleRate);
					pulsingWork |= 1 << c;
					flashWork[c] = 1.f;
					flashLit |= 1 << c;
					roseWork |= 1 << c;
				}
			}
		// The converter: a rise on a channel of IN fires that ring as a
		// pulse button's press does, a ring with its own cable keeping it.
		const int risen = inRises() & ~ringInWork;
		const bool converts = converting();
		if (converts)
			for (int c = 0; risen >> c; c++)
				if ((risen >> c) & 1) {
					pulseUntil[c] = args.frame + pulseFrames(args.sampleRate);
					pulsingWork |= 1 << c;
					flashWork[c] = 1.f;
					flashLit |= 1 << c;
					roseWork |= 1 << c;
				}
		maskWork = mask(ringInWork);
		// Converting, a gate's rise has bloomed as its trigger above, and its
		// hold shows nothing.
		const int lit = (cableIn() && !converts ? cableMask() : 0) & ~ringInWork;
		const int rose = lit & ~cabledLit;
		cabledLit = lit;
		for (int c = 0; rose >> c; c++)
			if ((rose >> c) & 1)
				flashWork[c] = 1.f;
		flashLit |= rose;
		roseWork |= rose;
	}
	/** OUT, and the rings' own jacks: alone, a ring's jack is its channel
	of OUT, gate or pulse; in a block it is high only while a reader's
	strike holds it (`fireRings`). A ring past the count is silent. */
	void publishJack() override {
		const int m = maskWork | pulsingWork;
		const int n = rings();
		outputs[GATE_OUTPUT].setChannels(n);
		jackWidth = n;
		for (int c = 0; c < n; c++)
			outputs[GATE_OUTPUT].setVoltage((m >> c) & 1 ? 10.f : 0.f, c);
		const int own = (isMember() ? ringFiring : m) & ((1 << n) - 1);
		for (int c = 0, moved = own ^ ringHigh; moved >> c; c++)
			if ((moved >> c) & 1)
				setRing(c, (own >> c) & 1);
	}
	// How wide the jack is meant to be, which an uncabled port does not
	// keep, for its glow (`channelsMeant`).
	int jackWidth = 1;
	int channelsMeant(int jack) override { return jack == GATE_OUTPUT ? jackWidth : 1; }
	// Only OUT is drawn as a jack; a ring's own shows on the ring.
	int glowingJacks(Module* m) override { return GATE_OUTPUT + 1; }
	/** A step a ring to a playhead, or one walked across. */
	int stepsOf(bool across) override { return across ? 1 : rings(); }
	int countParam() const override { return LEN_PARAM; }
	/** Standing on ring `sub`, or on the whole Trg when `sub` is -1: a
	trigger mask. */
	void sound(int sub, const Signal& sig, int col, Sound& o) override {
		o.type = Sound::TRIG;
		const int m = maskWork;
		o.mask = sub < 0 ? m : (m >> sub) & 1;
		o.value = sub;
		o.signal = sig;
		o.window = winIn;
	}
	/** A value taken into ring `sub`, or every ring from a channel each when
	`sub` is -1: lit when the channel is at the threshold or above, or turned
	over when it is and left alone when it is not, flipping. */
	void take(int sub, const Taken& t) override {
		const int from = sub < 0 ? 0 : sub, to = sub < 0 ? std::min(rings(), t.n) : sub + 1;
		const int was = mask();
		for (int c = from; c < to; c++) {
			const bool above = t.v[c - from] >= TRIG_HIGH;
			if (t.flip) {
				if (above)
					writeGate(c, !((was >> c) & 1));
			}
			else if (t.light) {
				if (above && !((was >> c) & 1))
					writeGate(c, true);
			}
			else
				writeGate(c, above);
		}
	}
	void clearReads() override {
		Cartridge::clearReads();
		headsWork = Heads();
		lastReadLane = lastReadHead = -1;
		bloomWork = 0;
	}
	void publishReads() override {
		Cartridge::publishReads();
		// The mode, the mask and the heads: all of what
		// the screens read, at once.
		modeNow.store(mode, std::memory_order_relaxed);
		acrossNow.store(acrossWork, std::memory_order_relaxed);
		headsPacked.store(headsWork.bits, std::memory_order_relaxed);
		fedMaskNow.store(fedMask, std::memory_order_relaxed);
		ringInNow.store(ringInWork, std::memory_order_relaxed);
		for (int c = 0; c < TRG_RINGS; c++)
			if (((bloomWork | roseWork) >> c) & 1)
				flash[c].store(flashWork[c], std::memory_order_relaxed);
		roseWork = 0;
	}
	/** A playhead standing on me: packed as a ring and colour pair, nearest
	Seq first since the nearest writes first. */
	void read(int lane, int col, int value) override {
		Cartridge::read(lane, col, value);
		lastReadLane = lane;
		lastReadHead = addHead(value, col, true, false);
	}
	/** The ring a Seq's walk stands on, for its halo: on the head its line
	was just packed as when the turn left it on that ring, which is always
	so with no turn, and a head of its own when the turn carried the line
	elsewhere. */
	void anchor(int lane, int sub) override {
		Cartridge::anchor(lane, sub);
		const int ring = sub < 0 ? ACROSS : clamp(sub, 0, TRG_RINGS - 1);
		if (lane >= 0 && lane == lastReadLane && lastReadHead >= 0 && Heads::placeOf(headsWork.bits, lastReadHead) == ring) {
			headsWork.addHalo(lastReadHead);
			return;
		}
		if (lane >= 0 && lane < nlanes)
			addHead(sub, laneColor[lane], false, true);
	}
	/** One head packed, returning its index, or -1 when five already stand. */
	int addHead(int sub, int col, bool line, bool halo) {
		return headsWork.add(sub < 0 ? ACROSS : clamp(sub, 0, TRG_RINGS - 1), clamp(col, 0, MAX_COLORS - 1), line, halo);
	}
	float flashOut(int ring) override { return ring >= 0 && ring < TRG_RINGS ? relaxed(flash[ring]) : -1.f; }
	int ringHeadsOut(int ring, int* cols, bool* line, bool* halo) override { return headsOn(ring, cols, line, halo); }
	int bundle(float* v) override { return 0; } // a Poly never asks (its row `gives` none, Manifest.hpp)
};

std::string RingQuantity::getDisplayValueString() {
	Trg* m = dynamic_cast<Trg*>(module);
	if (!m)
		return SwitchQuantity::getDisplayValueString();
	const int c = paramId - Trg::RING_PARAM;
	bool on = false;
	if (m->ringHeld(c, on) && !m->convertingShown())
		return on ? "On" : "Off";
	if (m->pulseShown())
		return "";
	return SwitchQuantity::getDisplayValueString();
}

std::string RingQuantity::getString() {
	Trg* m = dynamic_cast<Trg*>(module);
	if (m && m->sizingShown > 0)
		return string::f("%d triggers", m->sizingShown);
	const std::string v = getDisplayValueString();
	return v.empty() ? getLabel() : getLabel() + ": " + v;
}

struct TrgRing;

/** A ring's own jack, in or out: a port with no face of its own standing
under its ring, so the ring takes every click and the ring hands it what is
the jack's, a cable dragged onto the ring and dropped there
(`TrgRing::onDragDrop`) and a cable picked off it, by a press with command
held or from the ring's menu. A plain press never begins a cable: a cable
this jack would make new on one is taken away again as it is made, and only
one already in it may be picked up. A press with command held on a ring with
no cable begins one from the ring's output, Rack's own new cable, command
being the cabling key. Its plug stands
on the ring's centre and wears the ring's own face, on or off
(`RingPlugFace`), so the ring is read under any cable.

Rack's own jack reads command as "begin a new cable", which is what a
command press on a ring with no cable does, but on a ring with one a stroke
begun with command held lifts the top cable here the way Rack's jack lifts
one on a plain press: the plug off, to the top of the plugs, the cable's
removal noted, and on the drop either a move (the removal and the cable
where it now stands, one undo) or, dropped on nothing, a removal. */
struct RingJack : PortWidget {
	void draw(const DrawArgs& args) override {}
	CableWidget* lifted = nullptr;        // the cable a command stroke holds
	history::CableRemove* liftedAt = nullptr; // where it stood
	TrgRing* face = nullptr; // the ring I stand under, my plug's face
	void onDragStart(const DragStartEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && Hand::held(G_TRG_CABLE)) {
			if (cabled())
				lift();
			else
				PortWidget::onDragStart(e); // a new cable, Rack's own
			return;
		}
		const std::vector<CableWidget*> had = APP->scene->rack->getCompleteCablesOnPort(this);
		PortWidget::onDragStart(e);
		for (CableWidget* cw : APP->scene->rack->getIncompleteCables())
			if (std::find(had.begin(), had.end(), cw) == had.end()) {
				APP->scene->rack->removeCable(cw);
				delete cw;
			}
	}
	void lift() {
		CableWidget* cw = APP->scene->rack->getTopCable(this);
		if (!cw)
			return;
		liftedAt = new history::CableRemove;
		liftedAt->setCable(cw);
		lifted = cw;
		cw->getPort(type) = nullptr;
		cw->updateCable();
		PlugWidget* plug = cw->getPlug(type);
		Widget* plugs = APP->scene->rack->getPlugContainer();
		plugs->removeChild(plug);
		plugs->addChild(plug);
	}
	void onDragEnd(const DragEndEvent& e) override {
		if (!liftedAt) {
			PortWidget::onDragEnd(e);
			return;
		}
		// The cable's other end can be undone mid-stroke, a module taken
		// away and every cable on it freed with it: the drag ends on nothing
		// to move or to give back.
		const std::list<Widget*>& cables = APP->scene->rack->getCableContainer()->children;
		if (std::find(cables.begin(), cables.end(), lifted) == cables.end()) {
			delete liftedAt;
			liftedAt = nullptr;
			lifted = nullptr;
			PortWidget::onDragEnd(e);
			return;
		}
		// Asked before Rack's end takes away whatever is left incomplete.
		const bool whole = lifted->isComplete();
		const bool home = whole && lifted->getPort(type) == this;
		history::CableAdd* to = nullptr;
		if (whole && !home) {
			to = new history::CableAdd;
			to->setCable(lifted);
		}
		PortWidget::onDragEnd(e);
		if (home)
			delete liftedAt;
		else if (to) {
			history::ComplexAction* moved = new history::ComplexAction;
			moved->name = "move cable";
			moved->push(liftedAt);
			moved->push(to);
			APP->history->push(moved);
		}
		else
			APP->history->push(liftedAt);
		liftedAt = nullptr;
		lifted = nullptr;
	}
	bool cabled() { return module && APP->scene->rack->getTopCable(this); }
};

/** A cable on a ring picked up from the ring's menu: a press on the item is
a press on the jack, which picks its top cable up, Rack's own way of moving a
cable from a port's menu. */
struct RingCableItem : ui::MenuItem {
	WeakPtr<RingJack> jack;
	void onButton(const ButtonEvent& e) override {
		OpaqueWidget::onButton(e);
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && Hand::in(G_TRG_MENU, e.mods) && jack) {
			e.consume(jack.get());
			doAction(); // deletes this
		}
	}
};

/** One ring: an
off ring is a pale ring with a dark middle, an on ring is filled. A lit ring the
Seq's playhead is on takes that Seq's colour and blooms; an empty one stays
grey, the playhead's line alone marking it; the whole thing dims while a
cable holds the pattern. */
/** How much of a playhead on the rings fades at each end, down them in
poly and across a ring in mono: a fifth. */
const float RING_FEATHER = 0.2f;

struct TrgRing : ParamWidget, CellPaint<TrgRing>, Stroke, DarkFace {
	float startValue = 0.f;
	// A drag that began on a ring paints the rings it crosses, unless it
	// began with shift held, when it sets how many rings there are
	// (`sizing`, below), or with command held, when it is the ring's
	// cable's (`RingJack::lift`) or, with no cable to lift, a new cable from
	// the ring's output; `idle` is a command stroke on a ring with no
	// module, which does nothing.
	bool idle = false;
	// What my last stroke pushed as its undo, and what it noted, for a
	// double click to fold that stroke into its own.
	StepKey pushed;
	std::vector<GestureChanges::Was> lastWas;
	void endStroke() {
		lastWas = painted.was;
		pushed = StepKey::of(endPaint("change triggers"));
	}
	/** A fast second click toggles again, never Rack's reset; on a Trg a
	Poly offers channels, the two clicks give it back to them: every param
	the clicks moved stands as before the first, its undo taken back, and
	the Trg follows again, one undo. */
	void onDoubleClick(const DoubleClickEvent& e) override {
		e.consume(this);
		Trg* g = getTrg();
		if (!g || locked() || !g->offeredNow())
			return;
		std::vector<GestureChanges::Was> was = unpush(pushed) ? lastWas : std::vector<GestureChanges::Was>();
		pushed = StepKey();
		for (const GestureChanges::Was& w : painted.was)
			was.push_back(w);
		painted.was.clear();
		for (const GestureChanges::Was& w : was)
			painted.noteAt(w.module.get(), w.param, w.value); // the first click's value of each wins
		for (const GestureChanges::Was& w : painted.was)
			w.module.get()->params[w.param].setValue(w.value);
		followAgain(g, painted, g->params[Trg::OWN_PARAM].getValue());
		g->touch();
	}
	int ring = 0; // top to bottom, from nought

	bool shown = true; // one of the rings this Trg has
	void step() override {
		visible = shown;
		ParamWidget::step();
	}

	Trg* getTrg() { return paramModule<Trg>(this); }
	/** My own jacks, in and out, standing under me. */
	RingJack* jackIn = nullptr;
	RingJack* jackOut = nullptr;
	/** A cable in my own jack, as the last wake saw it. */
	bool ownCable() {
		Trg* m = getTrg();
		return m && (relaxed(m->ringInNow) >> ring) & 1;
	}
	/** Held by the IN cable, which the hand cannot press: a ring with its
	own cable is its own switch, flipped by the cable and pressed by the hand
	alike. A Poly's feed holds a ring too, but a press takes it over
	(`takeOver`). */
	bool locked() {
		Trg* m = getTrg();
		return m && m->inputs[Trg::IN_INPUT].isConnected() && !ownCable();
	}
	/** Held by a cable, my IN's or a Poly's feed (`Trg::ringHeld`), and
	with `on` what I show. */
	bool held(bool& on) {
		Trg* m = getTrg();
		on = false;
		return m && m->ringHeld(ring, on);
	}
	/** What this ring shows: the cable's channel while one holds it, else
	the param, which a ring's own cable flips. With no module at all,
	unlit. */
	bool lit() {
		engine::ParamQuantity* pq = getParamQuantity();
		if (!getTrg() || !pq)
			return false;
		bool on = false;
		if (held(on))
			return on;
		return pq->getValue() > 0.5f;
	}

	/** A cable being dragged over me is shown to the jack its loose end
	needs, the input for a cable from an output and the output for one from
	an input, so whichever end is dragged decides which of my jacks it is. */
	bool wantsIn(CableWidget* cw) { return cw->outputPort && !cw->inputPort; }
	bool wantsOut(CableWidget* cw) { return cw->inputPort && !cw->outputPort; }
	/** A cable whose fixed end is on one of this Trg's own rings, which no
	ring of it takes: a ring patched to a ring of the same bar is the bar
	talking to itself, so a
	ring's cable goes out of the Trg or comes in from elsewhere. */
	bool fromMyRings(CableWidget* cw) {
		PortWidget* fixed = cw->outputPort ? cw->outputPort : cw->inputPort;
		return dynamic_cast<RingJack*>(fixed) && module && fixed->module == module;
	}
	bool anyTaken() {
		for (CableWidget* cw : APP->scene->rack->getIncompleteCables())
			if (!fromMyRings(cw))
				return true;
		return false;
	}
	void onDragEnter(const DragEnterEvent& e) override {
		if (dynamic_cast<PortWidget*>(e.origin) && jackIn && jackOut) {
			bool in = false, out = false;
			for (CableWidget* cw : APP->scene->rack->getIncompleteCables()) {
				if (fromMyRings(cw))
					continue;
				in = in || wantsIn(cw);
				out = out || wantsOut(cw);
			}
			if (in)
				jackIn->onDragEnter(e);
			if (out)
				jackOut->onDragEnter(e);
		}
		ParamWidget::onDragEnter(e);
	}
	void onDragLeave(const DragLeaveEvent& e) override {
		if (jackIn && jackOut) {
			jackIn->onDragLeave(e);
			jackOut->onDragLeave(e);
		}
		ParamWidget::onDragLeave(e);
	}
	/** A cable dropped on me plugs into the jack its end needs; each jack
	takes only the cables it can (`PortWidget::onDragDrop`). Neither has a
	leave to come, being faceless, so the tooltip a drop gives it goes. */
	void onDragDrop(const DragDropEvent& e) override {
		if (dynamic_cast<PortWidget*>(e.origin) && jackIn && jackOut) {
			if (!anyTaken())
				return;
			jackIn->onDragDrop(e);
			jackOut->onDragDrop(e);
			jackIn->destroyTooltip();
			jackOut->destroyTooltip();
			return;
		}
		ParamWidget::onDragDrop(e);
	}
	/** A ring with a cable on it: a menu to pick each cable up or take it
	away, which Rack's port menu would do but for its items that begin a
	cable, which a ring never does. */
	void cableMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(string::f("Trigger %d", ring + 1)));
		for (RingJack* j : {jackIn, jackOut}) {
			if (!j || !j->cabled())
				continue;
			const bool in = j->type == engine::Port::INPUT;
			RingCableItem* pick = createMenuItem<RingCableItem>(in ? "Move the cable in" : "Move the cable out", "drag");
			pick->jack = j;
			menu->addChild(pick);
			WeakPtr<RingJack> weak = j;
			menu->addChild(createMenuItem(in ? "Delete the cable in" : "Delete the cable out", "", [=]() {
				if (weak)
					weak->deleteTopCableAction();
			}));
		}
	}
	/** The fewest rings a sizing stroke may leave the Trg whose rings stand
	under `w`: none of its rings with a cable on it put out of the bar, so
	every plug stays on its ring. */
	static int patchedFloor(Widget* w) {
		int f = 1;
		if (w)
			for (Widget* c : w->children) {
				if (TrgRing* r = dynamic_cast<TrgRing*>(c)) {
					if ((r->jackIn && r->jackIn->cabled()) || (r->jackOut && r->jackOut->cabled()))
						f = std::max(f, r->ring + 1);
				}
				else
					f = std::max(f, patchedFloor(c));
			}
		return f;
	}
	/** The Seq colours whose playhead stands on this exact ring right now,
	nearest Seq first, with which half of each stands here; how many come
	back is how many lines cluster here. */
	int heads(int* cols, bool* line, bool* halo) {
		Trg* m = getTrg();
		return m ? m->headsOn(ring, cols, line, halo) : 0;
	}
	/** And those standing on the whole Trg, walking it across. */
	int acrossHeads(int* cols, bool* line, bool* halo) {
		Trg* m = getTrg();
		return m ? m->headsOn(Trg::ACROSS, cols, line, halo) : 0;
	}

	/** A ring's radius, and smaller where more than eight share the screen. */
	float radius() const { return std::min(mm2px(4.f / 2), box.size.y * 0.42f); }
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		paintFace(args.vg, box.size.div(2), true);
	}
	/** Me, centred at `c`: the ring, on or off, and with `playheads` the
	Seqs' lines across my cell first. A plug standing on me paints the ring
	alone (`RingPlugFace`), the same colour and the same fill. */
	void paintFace(NVGcontext* vg, Vec c, bool playheads) {
		const float r = radius(), inner = r * 0.75f;
		Trg* m = getTrg();
		int hcol[Trg::HEADS], acol[Trg::HEADS];
		bool hline[Trg::HEADS], hhalo[Trg::HEADS], aline[Trg::HEADS], ahalo[Trg::HEADS];
		const int nh = heads(hcol, hline, hhalo);
		const int na = acrossHeads(acol, aline, ahalo);
		// The ring takes the colour of the first Seq whose line stands on it,
		// the step sounding; a halo alone, the walk's own step under a PHASE
		// turn, colours nothing but its glow.
		int lineCol = -1;
		for (int j = nh - 1; j >= 0; j--)
			if (hline[j])
				lineCol = hcol[j];
		if (lineCol < 0)
			for (int j = na - 1; j >= 0; j--)
				if (aline[j])
					lineCol = acol[j];
		const bool head = lineCol >= 0;
		NVGcolor seq = playColor(lineCol);
		// No Seq on me: by the scheme the cable in my IN, white standing idle
		// in a bond, the first colour alone or patched out; white, softened,
		// under the DEV's OLD.
		NVGcolor on = head                     ? seq
		              : !m || dev().oldColors ? color::alpha(color::WHITE, 0.85f)
		                                       : schemeColor(-1, relaxed(m->inRGB), relaxed(m->bondedUi),
		                                                      m->outputs[Trg::GATE_OUTPUT].isConnected());
		NVGcolor off = color::alpha(color::WHITE, 0.33f);
		// The IN cable holding every ring dims them; a ring's own cable does
		// not, its ring being that cable's gate, on or off, to be read.
		bool heldOn = false;
		const bool dimmed = held(heldOn);
		if (dimmed) {
			seq = color::alpha(seq, 0.4f);
			on = color::alpha(on, 0.4f);
			off = color::alpha(off, 0.4f);
		}
		const NVGcolor dark = RING_DARK;
		// A pulse button is lit for as long as its bloom lasts, which is
		// the only way a millisecond can be seen, and so is a ring a cable
		// lights: a trigger arriving holds it lit and it goes out. A ring a
		// cable holds is on or off and nothing between, no bloom toward
		// white and no swelling, so the hold is a blink.
		// Converting, the IN cable's gates are read as triggers, and a ring
		// shows each as a pulse button shows its press.
		const bool cabled = dimmed && !m->convertingShown();
		const bool pulse = m && m->pulseShown() && !cabled;
		const float held = (m && (head || pulse || cabled)) ? relaxed(m->flash[ring]) : 0.f;
		const bool high = lit() || ((pulse || cabled) && held > 0.f);
		const float fl = cabled ? 0.f : held;

		// The playheads: the CV's line and glow (`paintPlayhead`), across the
		// row for a Seq on this ring and down the cell, joining the cells
		// above and below, for a Seq walking the Trg across; a bloom when one
		// lands on a lit ring so the beat flashes. When two Seqs share a ring
		// they squeeze into two touching lines centred on it, just as the CV
		// clusters its lines, rather than mixing to a single white line.
		// Under a PHASE turn the halo stands on the walk's ring and the line
		// on the ring sounding, as on a CV.
		// A line down the rings fades in from either end of the column over
		// a fifth of its length, brightest about the rings, rather than
		// stopping hard partway down the glass: one length, from the first ring's top to
		// the last's foot, each cell drawing its piece of it. A line across
		// a ring feathers the same way at the screen's sides.
		auto lines = [&](const int* cols, const bool* ln, const bool* ha, int k, float centre, bool down, Feather f) {
			for (int j = 0; j < k; j++) {
				const NVGcolor lc = dimmed ? color::alpha(playColor(cols[j]), 0.4f) : playColor(cols[j]);
				paintPlayhead(vg, box.size, lc, centre + (j - (k - 1) / 2.f) * playheadStep(), down, ha[j],
				              ln[j], f);
			}
		};
		if (playheads) {
			lines(hcol, hline, hhalo, nh, c.y, false, Feather::over(0.f, box.size.x, RING_FEATHER));
			if (na > 0) {
				const int n = m ? m->rings() : GATE_CHANNELS;
				lines(acol, aline, ahalo, na, c.x, true,
				      Feather::over(-box.size.y * (float)ring, box.size.y * (float)(n - ring), RING_FEATHER));
			}
		}
		// On a flash the lit ring blooms toward white.
		if (high && fl > 0.f)
			on = nvgLerpRGBA(on, color::WHITE, 0.7f * fl);

		nvgBeginPath(vg);
		nvgCircle(vg, c.x, c.y, r * (high ? 1.f + 0.25f * fl : 1.f));
		nvgFillColor(vg, high ? on : off);
		nvgFill(vg);
		if (!high)
			paintHole(vg, c, inner, dark);
	}
	static void paintHole(NVGcontext* vg, Vec c, float inner, NVGcolor dark) {
		nvgBeginPath(vg);
		nvgCircle(vg, c.x, c.y, inner);
		nvgFillColor(vg, dark);
		nvgFill(vg);
	}
	/** Unpowered, every ring is out, whatever it holds, in the plain layer
	(`MemberWidget::unpowered`): the off ring alone, no playhead, no bloom. */
	void paintOut(NVGcontext* vg, Vec c) {
		const float r = radius();
		nvgBeginPath(vg);
		nvgCircle(vg, c.x, c.y, r);
		nvgFillColor(vg, color::alpha(color::WHITE, 0.33f));
		nvgFill(vg);
		paintHole(vg, c, r * 0.75f, RING_DARK);
	}
	void drawDark(const DrawArgs& args) override { paintOut(args.vg, box.size.div(2)); }

	void set(float v) {
		engine::ParamQuantity* pq = getParamQuantity();
		if (!pq)
			return;
		pq->setValue(v);
		if (Trg* m = getTrg()) {
			m->touch(); // a Seq previews the held step
		}
	}

	/** A stroke with shift held sets how many rings the Trg has, one to
	sixteen: up the screen for more, down for fewer, a ring every few millimetres of travel wherever
	the rings are standing, or on the black below them. The cursor is
	held still as a screen's drag holds it, and the travel counted the way a
	screen counts it (`Stroke`), so no edge of the window ends the stroke
	short of sixteen (a window's edge once ended one at fifteen).

	The whole stroke is this one thing: `begin` on the press, decides which
	Trgs it sizes (`sizedWith`) and what each began at; `move` on every
	move, the travel folded back at either end so turning back moves the
	count at once (the over-drag rule); `end` on the release, one undo. It
	is under way while it holds a Trg. */
	struct Sizing {
		/** A Trg the stroke sizes, the count it began with and its floor,
		the plugs on its rings, so a stroke that would put a plug out of one
		leaves that one at its plug. Held by id: an undo in the middle of the
		stroke may take one away. */
		struct Sized {
			HeldModule trg;
			float from;
			int floor;
		};
		std::vector<Sized> trgs; // left to right, as the block stands
		float from = 0.f;        // the count the pressed Trg began with
		int floor = 1;           // and its floor, which bounds the stroke
		float travel = 0.f;      // rack millimetres' pixels, up being more
		bool locked = false;     // the cursor held still for it
		bool on() const { return !trgs.empty(); }
		/** A ring every three millimetres. */
		static float per() { return mm2px(3.f); }
		void begin(TrgRing* owner, Trg* g) {
			trgs.clear();
			travel = 0.f;
			for (Trg* t : sizedWith(owner, g)) {
				const float f = t->params[Trg::LEN_PARAM].getValue();
				const int fl = std::min(patchedFloor(APP->scene->rack->getModule(t->id)), (int)f);
				trgs.push_back({HeldModule(t), f, fl});
				t->sizingShown = (int)f;
				if (t == g) {
					from = f;
					floor = fl;
				}
			}
		}
		/** The pointer moved `dy` rack pixels down. */
		void move(float dy) {
			travel -= dy;
			const int raw = (int)std::lround(from + travel / per());
			const int n = clamp(raw, floor, TRG_RINGS);
			if (raw != n)
				travel = (n - from) * per();
			for (const Sized& s : trgs) {
				Trg* t = s.trg.as<Trg>();
				if (!t)
					continue;
				const int k = std::max(n, s.floor);
				t->sizingShown = k;
				if ((float)k != t->params[Trg::LEN_PARAM].getValue()) {
					t->params[Trg::LEN_PARAM].setValue((float)k);
					t->touch();
				}
			}
		}
		void end() {
			GestureChanges sized; // one recorder for every gesture, so the block is one undo
			for (const Sized& s : trgs)
				if (Trg* t = s.trg.as<Trg>()) {
					t->sizingShown = 0;
					sized.noteAt(t, Trg::LEN_PARAM, s.from);
				}
			trgs.clear();
			sized.push("count triggers");
		}
	};
	Sizing sizing;
	/** Together or alone, decided here and nowhere else: the Trg pressed
	alone, or with a Seq set to Across walking its block, every Trg of the
	block that Seq walks across, which is then one count of drums; a block walked a ring a step is its
	Trgs sized one at a time. */
	static std::vector<Trg*> sizedWith(TrgRing* ring, Trg* g) {
		if (!g->acrossShown())
			return std::vector<Trg*>(1, g);
		std::vector<Trg*> out;
		for (Trg* t : touchingOf(ring->getAncestorOfType<ModuleWidget>(), g))
			if (t == g || t->acrossShown()) // past a Seq's cap: not its step, not its count
				out.push_back(t);
		return out;
	}
	/** The jack a press with command held lifts a cable from: the one with
	a cable, or with both the one whose plug is drawn on top, the plug the
	eye sees on the ring, as Rack's own jack takes the top of a stack. */
	RingJack* jackToLift() {
		const bool in = jackIn && jackIn->cabled(), out = jackOut && jackOut->cabled();
		if (in && out) {
			std::list<Widget*>& plugs = APP->scene->rack->getPlugContainer()->children;
			for (auto it = plugs.rbegin(); it != plugs.rend(); it++)
				if (PlugWidget* plug = dynamic_cast<PlugWidget*>(*it)) {
					PortWidget* p = plug->getCable() ? plug->getCable()->getPort(plug->getType()) : nullptr;
					if (p == jackIn || p == jackOut)
						return p == jackIn ? jackIn : jackOut;
				}
		}
		return in ? jackIn : out ? jackOut : nullptr;
	}
	/** Every ring the Trg has turned over, lit to dark and dark to lit, one
	undo: a press with command and shift held anywhere on the screen. Pulse buttons hold no pattern,
	so there it does nothing, nor under my IN cable, whose pattern it is (`Trg::invertible`). A stroke like a press or a paint, so a Trg a
	Poly's feed holds is taken over first and what its rings showed is
	turned over (`takeOver`), in the same undo. */
	static void invertRings(Trg* g) {
		if (!g || !g->invertible())
			return;
		GestureChanges turned;
		takeOver(g, turned);
		for (int c = 0, n = g->rings(); c < n; c++) {
			turned.note(g, Trg::ringParam(c));
			g->params[Trg::ringParam(c)].setValue(g->params[Trg::ringParam(c)].getValue() > 0.5f ? 0.f : 1.f);
		}
		g->touch();
		turned.push("invert triggers");
	}
	void onButton(const ButtonEvent& e) override {
		sizing.trgs.clear();
		idle = false;
		Trg* g = getTrg();
		if (g && e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && Hand::among(G_TRG_INVERT)) {
			invertRings(g);
			idle = true;
			e.consume(this);
			return;
		}
		// Command held is the cables': the press is handed to the jack, which lifts its cable, or on a
		// ring with none to the output, which begins one, a ring being a
		// source; a cable into a ring is another module's output dropped on
		// it.
		if (g && e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && Hand::held(G_TRG_CABLE)) {
			RingJack* j = jackToLift();
			if (!j)
				j = jackOut;
			if (j) {
				e.consume(j);
				return;
			}
			idle = true;
			e.consume(this);
			return;
		}
		if (g && e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && Hand::held(G_TRG_COUNT)) {
			sizing.begin(this, g);
			strokeBegins();
			ParamWidget::onButton(e);
			return;
		}
		// A plain press is always the ring's, a toggle or a pulse, whatever
		// cable is on it, so a Trg whose rings send gates or pulses out can be
		// played without lifting one; a cable is picked up with command held, or from the menu.
		if (g && e.button == GLFW_MOUSE_BUTTON_RIGHT && e.action == GLFW_PRESS &&
		    ((jackIn && jackIn->cabled()) || (jackOut && jackOut->cabled()))) {
			cableMenu();
			e.consume(this);
			return;
		}
		// A press on a ring a Poly's feed holds takes the Trg over first,
		// pulsing or not, so it starts from what was heard.
		pulseStroke = g && g->pulseShown();
		if (pulseStroke && !locked() && e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			lastRack = APP->scene->rack->getMousePos();
			painted.was.clear();
			takeOver(g, painted);
			g->askPulse(ring);
			e.consume(this);
			return;
		}
		if (!locked() && e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			startValue = lit() ? 1.f : 0.f;
			beginPaint(startValue);
			takeOver(g, painted);
			painted.note(this);
			set(paint);
		}
		ParamWidget::onButton(e);
	}
	void onDragStart(const DragStartEvent& e) override {
		sizing.locked = sizing.on() && e.button == GLFW_MOUSE_BUTTON_LEFT && settings::knobMode <= settings::KNOB_MODE_LINEAR;
		if (sizing.locked)
			APP->window->cursorLock();
		ParamWidget::onDragStart(e);
	}
	/** A stroke begun on a pulse button: a pulse on every pulse button it
	crosses, and no param touched but a Poly's Trg taken over. */
	bool pulseStroke = false;
	void onDragMove(const DragMoveEvent& e) override {
		Trg* g = getTrg();
		if (idle)
			return;
		if (pulseStroke) {
			if (e.button == GLFW_MOUSE_BUTTON_LEFT)
				paintCrossed(this, [&](TrgRing* r) {
					Trg* t = r->getTrg();
					if (t && t->pulseShown() && !r->locked()) {
						takeOver(t, painted);
						t->askPulse(r->ring);
					}
				});
			ParamWidget::onDragMove(e);
			return;
		}
		if (!sizing.on() && !locked() && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			paintCrossed(this, [&](TrgRing* r) {
				Trg* t = r->getTrg();
				if (!r->sizing.on() && !r->locked() && !(t && t->pulseShown())) {
					takeOver(t, painted);
					painted.note(r);
					r->set(paint);
				}
			});
		}
		if (sizing.on() && g && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			// In the rack's own millimetres, at any zoom; up is more rings.
			// Not an epsilon: a floor under a divisor, so a zoom Rack never
			// gives (nought) cannot make the travel infinite.
			const float zoom = std::max(APP->scene->rackScroll->getZoom(), 0.01f);
			sizing.move(strokeDelta(e) / zoom);
		}
		ParamWidget::onDragMove(e);
	}
	void onDragEnd(const DragEndEvent& e) override {
		Trg* g = getTrg();
		if (sizing.locked) {
			APP->window->cursorUnlock();
			sizing.locked = false;
		}
		if (sizing.on() && g && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			sizing.end();
			ParamWidget::onDragEnd(e);
			return;
		}
		if (idle) {
			pulseStroke = idle = false;
			ParamWidget::onDragEnd(e);
			return;
		}
		pulseStroke = false;
		if (!locked() && e.button == GLFW_MOUSE_BUTTON_LEFT)
			endStroke();
		ParamWidget::onDragEnd(e);
	}
};

/** A ring's face on a plug standing on it: Rack draws the plugs above every
panel, so a cable on a ring would hide it, and instead the plug's centre
shows the ring as the ring would draw itself, on in its own colour or off,
where a jack of ours shows its voltage (`OutPort`). A child of Rack's plug, laid on it by `TrgWidget::step`, and dies with
it. Which ring is asked every frame from the cable itself, so a plug lifted
off a ring shows nothing and one carried onto another ring shows that one;
a plug parked at the screen's foot, its ring past the count, shows nothing,
there being no ring on the screen for it to be. Drawn in the plugs' lit
layer, after Rack's own plug light, which it covers, so it stays lit when
the room darkens as the rings do. */
struct RingPlugFace : Widget {
	RingPlugFace() {
		// The plug's own origin is its centre, its pieces drawn about it.
		box = Rect(Vec(-8.f, -8.f), Vec(16.f, 16.f));
	}
	/** Unpowered, the ring out, in the plain layer, as the ring itself is. */
	void draw(const DrawArgs& args) override {
		if (engineSilent())
			paint(args.vg, true);
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1 && !engineSilent())
			paint(args.vg, false);
	}
	void paint(NVGcontext* vg, bool out) {
		PlugWidget* plug = dynamic_cast<PlugWidget*>(parent);
		CableWidget* cw = plug ? plug->getCable() : nullptr;
		RingJack* j = cw ? dynamic_cast<RingJack*>(cw->getPort(plug->getType())) : nullptr;
		if (!j || !j->face || !j->face->shown)
			return;
		// The screen's own dark first, a window onto the ring, so the off
		// ring's pale line reads on the plug as it does on the screen
		// rather than on the plug's grey.
		const Vec c = box.size.div(2);
		TrgRing::paintHole(vg, c, j->face->radius() * 1.25f, RING_DARK);
		if (out)
			j->face->paintOut(vg, c);
		else
			j->face->paintFace(vg, c, false);
	}
	/** Mine laid on every plug in `jack`, once. */
	static void lay(RingJack* jack) {
		for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(jack)) {
			PlugWidget* plug = cw->getPlug(jack->type);
			if (!plug)
				continue;
			bool has = false;
			for (Widget* w : plug->children)
				has = has || dynamic_cast<RingPlugFace*>(w);
			if (!has)
				plug->addChild(new RingPlugFace);
		}
	}
};

struct TrgPanel : Panel {
	void draw(const DrawArgs& args) override {
		drawPlate(args, this, "TRG");
		drawCaption(args.vg, CX, IN_CAP_Y, "IN", INK);
		drawOutBlock(args.vg, BLOCK_Y, BLOCK_H);
		drawCaption(args.vg, CX, FOOT_CAP_Y, "OUT", PALE);
	}
};

/** The black screen the rings stand on. With shift held a press on the
black no ring stands on sizes the bar as a press on a ring does, so a Trg of
one ring has the whole screen to take hold of, and with command and shift it turns the pattern over, as on
a ring (`TrgRing::invertRings`). A Trg of one ring is one button: a
press anywhere on the black is that ring's, plain, shift or command, the
corner's pulse keeping its own click, and under the pointer the black says what a press does there
(`Trg::blackTip`) and carries the ring's strokes on the bracket, but for the
right-click and the dropped cable, which stay the ring's. With more than one ring the black takes only
shift and command and shift, and has no tooltip. */
struct TrgScreen : Hovered<Screen>, HintSource, HoverTip {
	const std::vector<TrgRing*>* rings = nullptr;
	/** What the black says: its one ring's strokes, which a press on it
	makes, less the right-click and the dropped cable it does not pass on,
	or with several rings its own, a shift stroke sizing the last ring and,
	but for pulse buttons, the inversion. */
	Hinted own;
	const Hinted* hinted() override {
		if (!rings || rings->empty() || !rings->front()->module)
			return nullptr;
		Trg* t = paramModule<Trg>(rings->front());
		Gestures gs;
		if (TrgRing* one = onlyRing()) {
			if (const Hinted* h = dynamic_cast<const Hinted*>(one->getParamQuantity()))
				for (const Gesture* g : h->gestures)
					if (g != &G_TRG_MENU && g != &G_TRG_DROP)
						gs.push_back(g);
		}
		else {
			gs.push_back(t->acrossShown() ? &G_TRG_COUNT_BLOCK : &G_TRG_COUNT);
			if (t->invertible())
				gs.push_back(&G_TRG_INVERT);
		}
		if (own.gestures != gs) {
			own.gestures = gs;
			own.hint = hintOf(gs);
		}
		return &own;
	}
	/** The one ring, where the Trg has exactly one; with a module only. */
	TrgRing* onlyRing() {
		if (!rings || rings->empty())
			return nullptr;
		Trg* g = paramModule<Trg>(rings->front());
		return g && g->rings() == 1 ? rings->front() : nullptr;
	}
	void onButton(const ButtonEvent& e) override {
		Screen::onButton(e);
		if (e.isConsumed() || !rings || rings->empty() || e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS)
			return;
		if (Hand::among(G_TRG_INVERT)) {
			TrgRing::invertRings(paramModule<Trg>(rings->front()));
			e.consume(this);
			return;
		}
		if (TrgRing* one = onlyRing()) {
			one->onButton(e);
			return;
		}
		if (!Hand::held(G_TRG_COUNT))
			return;
		TrgRing* last = nullptr;
		for (TrgRing* b : *rings)
			if (b->shown)
				last = b;
		if (last)
			last->onButton(e);
	}
	/** One ring, its words, kept as the Trg changes under a resting pointer;
	several, none. */
	void step() override {
		Screen::step();
		Trg* t = rings && !rings->empty() ? paramModule<Trg>(rings->front()) : nullptr;
		// `hovered`: the pointer on the black itself, not on a ring or the corner.
		if (hovered && t && onlyRing())
			tip ? tipKeep(t->blackTip()) : tipShow(t->blackTip());
		else {
			tipHide();
			said.clear();
		}
	}
	void onHover(const HoverEvent& e) override {
		Screen::onHover(e);
		// Taken, so the black is what the pointer is over and says its
		// gestures (`hinted`), a press not needed.
		if (!e.isConsumed() && hinted())
			e.consume(this);
	}
};

/** The pulse in the screen's top left corner, there only while nothing
reads or feeds this Trg (`Trg::pulseCornerShown`): dim while the rings toggle, yellow while they pulse, and a
click turns one into the other. Turning to pulse clears every ring, the turn
and the clearing one undo. */
struct TrgPulse : Hovered<ParamWidget> {
	Trg* getTrg() { return paramModule<Trg>(this); }
	void step() override {
		Trg* m = getTrg();
		visible = !m || m->pulseCornerShown();
		ParamWidget::step();
	}
	/** A fast second click turns back again, never Rack's reset. */
	void onDoubleClick(const DoubleClickEvent& e) override { e.consume(this); }
	void onButton(const ButtonEvent& e) override {
		// Under a Seq or a Poly's feed the corner is empty, and a press
		// there is not mine either, whatever a frame's lag left visible.
		Trg* m = getTrg();
		if (m && !m->pulseCornerShown())
			return;
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			turn();
			e.consume(this);
			return;
		}
		ParamWidget::onButton(e);
	}
	void turn() {
		Trg* m = getTrg();
		if (!m)
			return;
		const bool toPulse = m->params[Trg::PULSE_PARAM].getValue() <= 0.5f;
		GestureChanges g;
		g.note(m, Trg::PULSE_PARAM);
		if (toPulse)
			for (int c = 0; c < TRG_RINGS; c++) {
				g.note(m, Trg::ringParam(c));
				m->params[Trg::ringParam(c)].setValue(0.f);
			}
		m->params[Trg::PULSE_PARAM].setValue(toPulse ? 1.f : 0.f);
		g.push(toPulse ? "pulse triggers" : "toggle triggers");
	}
	/** An impulse, drawn as the textbook draws one: a baseline with a single
	stem up from its middle and an arrowhead on it, a thing with no width,
	where a square would read as a gate. The plate's line weight, lit so it
	reads in a dark room. */
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		Trg* m = getTrg();
		const bool on = m && m->pulseShown();
		const NVGcolor ink = color::alpha(on ? SCHEME_YELLOW : color::WHITE, markInk(on, hovered));
		paintImpulse(args.vg, box.size, ink);
	}
};

struct TrgWidget : MemberWidget {
	Trg* gate = nullptr;
	std::vector<TrgRing*> ringsOf;
	std::vector<RingJack*> jacks; // in and out, a pair a ring
	float ringsTop = 0.f, ringsH = 0.f;
	int laidRings = -1;
	TrgScreen* screen = nullptr;
	TrgPulse* pulse = nullptr;
	/** The rings laid out for however many there are: on one pitch for
	eight or fewer, so a short bar stops partway down, and closer together past eight, filling
	the same height. */
	void layRings() {
		const int n = gate ? gate->rings() : GATE_CHANNELS;
		if (n == laidRings)
			return;
		laidRings = n;
		const float pitch = ringsH / (float)std::max(n, GATE_CHANNELS);
		for (TrgRing* b : ringsOf) {
			b->box.pos = Vec(0.f, ringsTop + pitch * (float)b->ring);
			b->box.size = Vec(b->box.size.x, pitch);
			b->shown = b->ring < n;
			// A ring's jacks on its centre, its plugs wearing its face
			// (`RingPlugFace`), and those of a ring past the count at the
			// screen's foot, where a plug left there says so.
			const Vec at = b->shown ? b->box.getCenter()
			                        : Vec(b->box.size.x / 2.f, screen->box.size.y - ringsTop / 2.f);
			for (RingJack* j : {b->jackIn, b->jackOut})
				j->box.pos = at.minus(j->box.size.div(2));
		}
	}
	void step() override {
		layRings();
		// Every plug on a ring wears the ring's face; a jack the engine says
		// is unconnected has none to look for.
		if (gate)
			for (RingJack* j : jacks) {
				const bool on = j->type == engine::Port::INPUT ? gate->inputs[j->portId].isConnected()
				                                               : gate->outputs[j->portId].isConnected();
				if (on)
					RingPlugFace::lay(j);
			}
		if (gate) {
			gate->inRGB.store(cableColorOn(getInput(Trg::IN_INPUT)), std::memory_order_relaxed);
			const std::vector<ModuleWidget*>* run = runOf(this);
			gate->bondedUi.store(run && run->size() > 1, std::memory_order_relaxed);
		}
		MemberWidget::step();
	}
	TrgWidget(Trg* module) {
		gate = module;
		TrgPanel* panel = new TrgPanel;
		open(module, panel);

		addInput(createInputCentered<InPort>(mm2px(Vec(CX, IN_Y)), module, Trg::IN_INPUT));

		screen = createWidget<TrgScreen>(mm2px(Vec(0.f, SCREEN_Y)));
		screen->box.size = mm2px(Vec(PANEL_W, SCREEN_H));
		screen->rings = &ringsOf;
		addChild(screen);
		// Sixteen cells down the screen, on the rings' margin and pitch.
		// Channel 1 is the top cell.
		const float margin = mm2px(2.f);
		const float h = screen->box.size.y - 2 * margin;
		ringsTop = margin;
		ringsH = h;
		// The rings' own jacks first, so the rings stand over them and take
		// every event.
		for (int c = 0; c < TRG_RINGS; c++)
			for (int k = 0; k < 2; k++) {
				RingJack* j = k == 0 ? createInput<RingJack>(Vec(), module, Trg::RING_INPUT + c)
				                     : createOutput<RingJack>(Vec(), module, Trg::RING_OUTPUT + c);
				j->box.size = mm2px(Vec(1.f, 1.f));
				j->visible = false;
				screen->addChild(j);
				jacks.push_back(j);
			}
		for (int c = 0; c < TRG_RINGS; c++) {
			TrgRing* b = createParam<TrgRing>(Vec(0.f, margin + h / GATE_CHANNELS * c),
			                                    module, Trg::ringParam(c));
			b->ring = c;
			b->jackIn = jacks[2 * c];
			b->jackOut = jacks[2 * c + 1];
			b->jackIn->face = b->jackOut->face = b;
			b->box.size = Vec(screen->box.size.x, h / GATE_CHANNELS);
			screen->addChild(b);
			ringsOf.push_back(b);
		}
		layRings();
		// The pulse in the corner, over the rings so it takes its own click,
		// standing above the first ring's cell and clear of every ring.
		// Lowered off the screen's edge, still left of the first ring at
		// any count.
		pulse = createParam<TrgPulse>(mm2px(Vec(0.6f, 1.7f)), module, Trg::PULSE_PARAM);
		pulse->box.size = mm2px(Vec(4.2f, 2.8f));
		screen->addChild(pulse);

		addOutput(createOutputCentered<OutPort>(mm2px(Vec(CX, FOOT_Y)), module,
		                                           Trg::GATE_OUTPUT));
	}
};

} // namespace

Model* modelTrg = createModel<Trg, TrgWidget>("TRG");
