// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "HeadBase.hpp"

/** Seq, the playhead: walks the block of cartridges to its left on a clock and
sounds the step it stands on at OUT, and alone is an arpeggiator. */

using namespace rig;

namespace {

#include "Walk.hpp"

/** The mode's tooltip: one name, Mode, and the mode in force as its value,
"Mode: Halt" (`Seq::modeShown`), what each mode does being its lesson. */
struct ModeQuantity : WithHint<SwitchQuantity> {
	std::string getDisplayValueString() override;
};

/** The step screen read as a tooltip: the step a person would count, one
the first, and typing one in picks it. */
struct StepQuantity : HintQuantity {
	Head* head = nullptr;
	std::string getString() override;
	std::string getDisplayValueString() override;
	float getDisplayValue() override {
		if (!head)
			return 0.f;
		return (float)((relaxed(head->shownPhased) ? relaxed(head->shownBase) : head->shownPos()) + 1);
	}
	void setDisplayValue(float v) override {
		const int n = head ? head->shownCount() : 0;
		if (n > 1)
			setValue(clamp(v - 1.f, 0.f, (float)(n - 1)));
	}
};

/** What moves the playhead, settled every scan: the nearest driver in the
rig, else the screen. */
enum Drive : uint8_t {
	DRIVE_TIME,   // a driver to the left or a clock cable: pulses into the writer
	DRIVE_SCREEN, // nothing: the lower screen picks the step
};

/** The rate's value: the step rate, or, no clock driving the walk, idle
(`Seq::rateIdle`). */
struct SeqRateQuantity : StepRateQuantity {
	std::string getDisplayValueString() override;
};

/** The lamp's strokes, which its face does not say, on the hint line, a line
a modifier, each naming the lamp, since with Rack's tooltips off the line is
all that says what it is: shift-click puts it out and back, a plain drag sets
the time a rung at a time, off below the shortest and lit again from it
(`EdgeLampParam::strokeStep`), said because a lamp does not look like a
control one drags, ranked Known as Rack's own drag on a control is, and
command makes the drag fine, said as a rate's fine is. Slew or Interpolate as
the lamp is named, one key each way. */
inline const Gesture G_SLEW_TOGGLE = declare(On::Click, Mod::Shift, Rank::Invented, RACK_MOD_SHIFT_NAME "-click: toggle slew");
inline const Gesture G_INTERP_TOGGLE =
    declare(On::Click, Mod::Shift, Rank::Invented, RACK_MOD_SHIFT_NAME "-click: toggle interpolation");
inline const Gesture G_SLEW_DRAG = declare(On::Drag, Mod::None, Rank::Known, "Drag: slew time");
inline const Gesture G_INTERP_DRAG = declare(On::Drag, Mod::None, Rank::Known, "Drag: interpolation time");
inline const Gesture G_LAMP_FINE = declare(On::Drag, Mod::Command, Rank::Known, RACK_MOD_CTRL_NAME "-drag: fine");
/** BLEP's and Oversampling's lessons, on their diodes either side of OUT,
under a Clock in hertz: what each is for. Both say "off for" a phasor into
another SEQ's PHASE, since the decimator undershoots a phasor's drop by more
than a step of eight. Their four clicks are said at once, on the hint line. */
const char* const BLEP_HZ_LESSON = "Band-limits OUT's jumps, for less aliasing at audio rate. Off for a phasor into "
                                   "another SEQ's PHASE.";
const char* const OVER_HZ_LESSON = "Oversamples OUT, for less aliasing at audio rate. Off for a phasor into another "
                                   "SEQ's PHASE.";
/** The diodes and the slew's lamp are one choice in every clock context, and
each says what lighting it puts out where the other stands beside it. Outside
hertz the phasor's warning goes, the diodes being off there until a hand
lights them. */
const char* const BLEP_LESSON = "Band-limits OUT's jumps, for less aliasing at audio rate.";
const char* const OVER_LESSON = "Oversamples OUT, for less aliasing at audio rate.";
const char* const PUTS_SLEW_OUT = " Turning it on turns the slew off.";
const char* const PUTS_INTERP_OUT = " Turning it on turns interpolation off.";
const char* const PUTS_BAND_OUT = " Turning it on turns BLEP and oversampling off.";
const char* const PUTS_BLEP_OUT = " Turning it on turns BLEP off.";
/** The Poly square's, over TRGs: the state on screen alone. */
const char* const ACROSS_OFF_LESSON = "Each trigger is a step.";
const char* const ACROSS_ON_LESSON = "Each TRG is a step, its triggers out as poly channels.";
/** The crossfade square's, over ATNs, crossfade being the musician's word. */
const char* const FADE_LESSON = "On: OUT crossfades from each ATN to the next across its step.";
const char* const FADE_OFFSETS_LESSON = "On: OUT crossfades from each ATN's offset to the next across its step.";
const char* const FADE_ROUTE_LESSON = "On: OUT crossfades from each ATN to the next across its step, and ROUTE pans across their jacks.";
/** The two diodes' one name, the plain click on either setting both, and
their value which of the two are on (`bandOn`). */
const char* const ANTI_ALIASING = "Anti-aliasing";
inline std::string bandOn(bool blep, bool over) {
	return blep && over ? "BLEP and oversampling" : blep ? "BLEP" : over ? "Oversampling" : "Off";
}
/** A click toggles both, and command takes it rig-wide, as command takes a
stroke to the touching modules everywhere else; shift and command together,
the rare one, is this diode alone on this SEQ. Each line names what it
toggles, and where, so none leans on the line above it. */
inline const Gesture G_DIODE_BOTH = declare(On::Click, Mod::None, Rank::Invented, "Click: toggle BLEP and oversampling");
inline const Gesture G_DIODE_ROW =
    declare(On::Click, Mod::Command, Rank::Block, RACK_MOD_CTRL_NAME "-click: both, rig-wide");
inline const Gesture G_DIODE_ONE =
    declare(On::Click, Mod::ShiftCommand, Rank::Rare, RACK_MOD_SHIFT_NAME "+" RACK_MOD_CTRL_NAME "-click: this diode alone");
/** BLEP's click where it stands alone, over ATNs, Oversampling hidden
there (`Seq::overMeans`): one diode, so neither "both" nor "alone". */
inline const Gesture G_DIODE_BLEP = declare(On::Click, Mod::None, Rank::Invented, "Click: toggle BLEP");
/** And its command-click's words, the key being `G_DIODE_ROW`'s: one
diode, so not "both". */
inline const Gesture G_DIODE_BLEP_ROW =
    declare(On::Click, Mod::Command, Rank::Block, RACK_MOD_CTRL_NAME "-click: BLEP, rig-wide");
/** A diode's clicks, the pair's, or BLEP's where it stands alone. */
inline Gestures diodeGestures(bool pair) {
	return pair ? Gestures{&G_DIODE_BOTH, &G_DIODE_ROW, &G_DIODE_ONE} : Gestures{&G_DIODE_BLEP, &G_DIODE_BLEP_ROW};
}
/** The mode button's clicks, said at once (`ModeButton::onButton`), in one
fixed form, a piece a key, each naming where it goes from the mode in force
(`Seq::modeToward`): to its aim, or from the aim to the other, so in PLAY
shift goes to HALT and in HALT command goes to PLAY. A plain click goes on
to the next mode. `G_MODE_PLAY` and `G_MODE_HALT` are the keys the button
reads; the two after them are their words where they go the other way. */
inline const Gesture G_MODE_NEXT = declare(On::Click, Mod::None, Rank::Known, "Click: next mode");
inline const Gesture G_MODE_PLAY = declare(On::Click, Mod::Shift, Rank::Invented, RACK_MOD_SHIFT_NAME "-click: PLAY");
inline const Gesture G_MODE_SHIFT_HALT = declare(On::Click, Mod::Shift, Rank::Invented, RACK_MOD_SHIFT_NAME "-click: HALT");
inline const Gesture G_MODE_CMD_PLAY = declare(On::Click, Mod::Command, Rank::Invented, RACK_MOD_CTRL_NAME "-click: PLAY");
/** The step screen's: a drag under a clock holds the step it reaches,
playing it, and goes on from there when let go; with nothing clocking, a
drag picks the step; a double click sends the walk to step one. */
inline const Gesture G_STEP_HOLD = declare(On::Drag, Mod::None, Rank::Invented, "Drag: hold a step");
inline const Gesture G_STEP_PICK = declare(On::Drag, Mod::None, Rank::Known, "Drag: pick a step");
inline const Gesture G_STEP_ONE = declare(On::Double, Mod::None, Rank::Known, "Double-click: step one");
inline const Gesture G_MODE_HALT = declare(On::Click, Mod::Command, Rank::Invented, RACK_MOD_CTRL_NAME "-click: HALT");

/** The slew's place read as its time, in samples or milliseconds, "Off"
before it where its switch is off, and a time typed in kept as typed
(`Seq::slewLength`). */
struct SlewQuantity : HintQuantity {
	std::string getDisplayValueString() override;
	void setDisplayValueString(std::string s) override;
};

struct Seq : Member, Head, Probe, RateMode {
	enum ParamId {
		DIR_PARAM,          // forward, pendulum or reverse (`Dir`), the cell over the step screen's left
		STEP_PARAM,          // the step the step screen picks, unclocked: a step number, nought the first, so adding a CV never moves it
		RATE_PARAM,         // the rate on the count, a step rate as a Clock's division is, x1 the driver's own
		ACROSS_PARAM,       // over Trgs, each Trg is a step and its rings are channels: P, the square on the strip's left
		FADE_PARAM,         // over Atns, how the walk passes from one to the next: Step, or Fade (`fadeOn`), the square in the same place
		MODE_PARAM,         // PLAY, PREVIEW, HALT or ONCE (`Mode`), the strip's button
		SLEW_PARAM,         // the slew's time, a place along its ladder (`slewLength`), the lamp at the OUT dragged
		// The one choice at the OUT, slew or BLEP and Oversampling, a set of
		// three for each clock context, so each keeps its own (`Choice`), the
		// one in hertz beside its twin:
		SLEW_ON_PARAM,      // outside hertz, the slew on or off, its time kept, the lamp lit or dim (shift-click)
		SLEW_ON_HZ_PARAM,   // under a Clock in hertz, the slew's switch there, off by default
		BLEP_PARAM,         // outside hertz, the step edges band-limited: the diode on the left under OUT, off by default
		BLEP_HZ_PARAM,      // under a Clock in hertz, BLEP's diode there, on by default
		OVER_PARAM,         // outside hertz, the jack worked out four times over: the diode on the right under OUT, the same
		OVER_HZ_PARAM,      // under a Clock in hertz, Oversampling's diode there, on by default
		PARAMS_LEN
	};
	enum InputId {
		IN_INPUT,    // the IN jack (captioned REC, ROUTE, QNT IN or POLY IN): the signal, normalled to the member stood on; a clock in once, the index kept
		CLOCK_INPUT, // the Seq's own clock, ahead of the rig's (`countIn`)
		RATE_INPUT,
		RESET_INPUT,
		PHASE_INPUT, // the position of the playhead, as a voltage
		INPUTS_LEN
	};
	enum OutputId {
		STEP_OUTPUT, // STEP on the plate: a pulse each time the step changes
		CV_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};
	/** What the button over the jacks says, a click going round the four.
	PREVIEW: a member held under the mouse is heard at once and the walk goes
	on underneath. HALT: the clock no longer walks the Seq; the step screen
	alone sets the step it holds, a drag or a click there, and a member
	touched changes nothing about it. ONCE: one pass of the cycle and
	no more, armed on step one, started by the next pulse and spent by the
	pulse that would carry it round, on the last step until a reset arms it
	again. PLAY: none of
	them. */
	enum Mode { MODE_PLAY, MODE_PREVIEW, MODE_HALT, MODE_ONCE, MODES };

	int count = 0;
	int pos = 0;
	Dir dir = DIR_FORWARD;     // this wake's direction
	Dir lastDir = DIR_FORWARD; // and the last wake's, to turn on a change
	int64_t cycle = 1;         // places in one cycle: the steps, or there and back
	int color = 0;
	int gateMask = -1; // walking a block of Trgs: the rings under the playhead, one bit a channel, -1 for none
	bool gateWide = false; // and whether they are the rings of one Trg, across
	int gateRings = GATE_CHANNELS; // and how many rings that Trg has
	int firedMask = 0;      // the rings lit as the step landed: the channels pulsing
	int64_t firedUntil = -1; // and until when
	// The last sample a step of mine fired, re-aimed landings aside, the one
	// before it, and the trigger length worked out for the sample
	// `fireLenAt` (`fireFrames`), re-aimed or not.
	int64_t lastFireAt = -1, prevFireAt = -1, fireLenAt = -1, fireLen = 0;
	bool fireLenReaimed = false;
	// What the plate and the screens read: worked out through the wake into
	// the plain fields below, and stored into the atomics at the end of it
	// (`publishReads`) with the lanes and lit keys the members publish, so a
	// frame never draws the new step over the members' old lanes.
	bool headPhased = false;
	// A REC take over Trgs latching through its step: the member and ring
	// the landing took into, and which channels have been heard high since.
	bool latchOn = false;
	Cartridge* latchMember = nullptr;
	int latchSub = 0, latchN = 0;
	bool latchHigh[TRG_RINGS] = {};
	// And a pulse played in the second half of a step, which is the next
	// step's: the member and ring it lit ahead, and which channels, kept lit
	// when that step lands whatever the jack then says.
	Cartridge* aheadMember = nullptr;
	int aheadSub = 0, aheadMask = 0;
	bool recWork = false, routeWork = false, overAtnWork = false, overQntWork = false, gatesWork = false, overCVWork = false,
	     aloneWork = false, ratePatchedWork = false, phasedWork = false;
	bool emptyWork = false; // alone with nothing in ARP: nothing to play
	bool polyArpWork = false; // alone, ARP empty, and the Poly my reader feeding the steps
	const float* arpV = nullptr; // that Poly's channels, nullptr when the steps are ARP's cable
	Member* arpPoly = nullptr; // and that Poly, which my playhead stands on, for this wake alone
	bool curvesWork = false; // walking a block of CRVs, played through the step
	bool outPolyWork = false;
	// How wide the jack is meant to be, which an uncabled port does not keep:
	// for its glow (`channelsMeant`) and for the triggers' end.
	int jackWidth = 1;
	int channelsMeant(int jack) override { return jack == CV_OUTPUT ? jackWidth : 1; }
	int rateWork = RATE_ONE, headCount = 0, headPos = 0, headColor = 0;
	// Under a running count with a cable in PHASE, the one filter against the
	// flam (a square LFO in PHASE for a swing struck every other sixteenth
	// twice, 0.4 of a tick apart): a step is struck once per step the walk
	// travels. Each step keeps where the walk stood when it was last struck,
	// its serial of steps begun, the writer's (`Writer::Seen:: began`)
	// carried by a free rate's whole places, and how far through that step,
	// so "less than a step of travel ago" is a comparison of integers and an
	// along, never a sum of fractions: the same serial, or the next one not
	// yet as far through as the strike was. A strike the walk made, carrying the turned place on, stands
	// exactly where the walk met the turn, so a one-step block is struck
	// every step, where a sum of the travel summed to a hair under one.
	struct Struck {
		uint32_t walk = 0;
		double along = 0.0;
		uint32_t gen = 0;
	};
	Struck struck[MAX_SEQ_STEPS];
	uint32_t strikeGen = 1; // a slot of an older generation is empty
	bool guarding = false;
	// The walk's own place, the writer's carried by a free rate's turn, a
	// whole place and how far through it, and the turn added on top, in
	// places, at the last wake: what says whether a move of the turned place
	// was the walk carrying it.
	int64_t lastFromStep = 0;
	double lastFromAlong = 0.0, lastBy = 0.0;
	/** The turn a Clock in hertz's PHASE adds to the walk (`clockTurn`), in
	places: what it had come to at the last rebase, `ctP0`, and the Clock's
	turn there, `ctB*`, so the turn is that plus the cycles since times
	`ctR`, the places a cycle, which are the Clock's ticks a cycle times my
	rate's places a tick. A product of exact differences rather than a sum,
	rebased when the rate changes, a reset or a start sends the walk home,
	or HALT holds it. Plain fields, the engine's. */
	double ctP0 = 0.0, ctBFrac = 0.0, ctR = 0.0, ctPlaces = 0.0;
	int64_t ctBWhole = 0;
	const tree::Node* ctFrom = nullptr;
	uint32_t ctResets = 0, ctStarts = 0;
	/** The turn a free rate adds to the walk (`rateTurn`), in places: the
integral, sample by sample, of how much faster than the writer the rate in
force walks, the screen's rate times two to the jack's volts less the rate
of the writer's journey, so the walk's speed is the free rate's at every
sample, exponential FM at audio rate and a drag of the screen alike, and its
place never re-derived from a rate. Nought from a reset, a start or a new
driver; held in HALT, with the driver stopped, and when the cable or the
free mode goes, so the walk never jumps. `rateTurnOn` says it is moving, a
wake every sample. Plain fields, the engine's. */
	double rtPlaces = 0.0;
	int64_t rtFrame = -1;
	const tree::Node* rtFrom = nullptr;
	uint32_t rtResets = 0, rtStarts = 0;
	bool rateTurnOn = false;
	double rtSpeedWas = 0.0; // the turn's places a frame at the last wake, nought where it stood
	// In ONCE, whether the walk under the turn stood at the pass's end at the
	// last wake, held there (`place`): spent there, the turn has nothing left
	// to carry and stands.
	bool rtAtEnd = false;
	int rateWalkWork = RATE_ONE; // the rate the writer walks at: free, the last it took smoothly (`rtAsk`)
	int rateSetWork = RATE_ONE;  // the screen's rate, which a free rate's turn walks at
	float rateVoltsWork = 0.f;   // and the jack's volts, where they turn it
	/** The rate a free Seq has asked its writer for: the screen's, taken
	only where the writer takes it with nothing moved (`takesRateSmoothly`),
	so a free rate dragged, typed, undone or recalled never waits for a
	landing to be heard and never jumps when it is; the turn carries the walk at the screen's rate
	meanwhile. Whole, the rate in force, waiting for the landing. */
	int rtAsk = RATE_ONE;
	int rtWNum = 1, rtWDen = 1; // the writer's journey before this wake's walk
	bool rtBehind = false;      // and it is not the free rate's: the turn carries the difference
	int64_t frameWork = 0;
	float rateCvWork = 0.f, phaseWork = 0.f;
	int basePos = 0; // the walk's step before a PHASE turn, for the step screen
	float baseAlongWork = 0.f; // and how far through it, played the way a stretch is, for a CRV's halo
	// The writer's arm is a reset's (the Seq's own jack, the rig's, a Sync, a
	// Seq just born) rather than a step a hand let go of: the screen then
	// names the step still playing and lights the arm mark, where a hand's
	// step is shown as picked.
	bool resetArm = true;
	int baseShownWork = 0;  // the step the screen names under a PHASE cable
	bool armedShownWork = false;
	Drive headDrive = DRIVE_SCREEN;
	Cartridge* stoodWork = nullptr; // the member under the playhead this wake, for the forwards below
	Cartridge* fadeTo = nullptr; // in Fade, the Atn OUT fades toward this wake, the next in the walk, and how far
	float fadeBy = 0.f;
	std::atomic<bool> recNow{false}; // the IN jack is being taken into a step, for the plate
	std::atomic<bool> routeNow{false}; // over Atns, the IN jack routed to the one stood on
	std::atomic<bool> overAtnNow{false}; // walking Atns: the IN jack is ROUTE
	std::atomic<bool> overRoutersNow{false}; // and they are routers, so Fade stands (`overRouters`)
	std::atomic<int> atnCarryNow{0}; // and what they give OUT (`AtnCarry`), for OUT's tooltip
	std::atomic<int> slewNow{0}; // what the slew does to the jack (`SlewMode`), the lamp at the OUT shown unless nothing
	std::atomic<bool> overQntNow{false}; // walking Qnts: the IN jack is QNT IN
	std::atomic<bool> outPoly{false}; // POLY under the jack, not OUT
	std::atomic<bool> gatesNow{false}; // walking Trgs, for the switch
	std::atomic<bool> overCVNow{false}; // walking CVs, Atns or Qnts: the blocks PREVIEW means something over
	std::atomic<bool> aloneNow{false}; // nothing to walk: an arpeggiator, its own clock jack
	std::atomic<bool> polyArpNow{false}; // alone, its steps the channels of the Poly to its left
	std::atomic<bool> emptyNow{false}; // and nothing in ARP either: nothing to play (`stepIdle`)
	// ONCE, for the button: the places dark in the high half, the cycle in
	// the low, one word so the two are never torn apart (`onceDark`).
	static constexpr int32_t ONCE_OFF = -0x10000; // not in ONCE: -1 dark
	std::atomic<int32_t> onceShown{ONCE_OFF};
	int32_t onceWork = ONCE_OFF;
	bool onceWas = false; // ONCE held the walk last wake, so entering it arms
	TrigCounter clockIn;               // that clock, counted
	Count ownCnt;
	// The walk: the writer (Writer.hpp), the one thing that moves the
	// position, every event from outside handed to it and the position read
	// back. Its place counts on for ever; the direction reads a place as a
	// step (`stepAt`).
	Writer wr;
	RateIn rate;
	std::atomic<int> rateShown{RATE_ONE}; // the rate in force, in millionths, for the hover
	std::atomic<bool> ratePatched{false}; // and whether the jack is sweeping it
	std::atomic<float> rateCv{0.f};       // and what it stands at, minus one to one
	// What the driver sent as I last heard it (`hear`): whose it was and its
	// serial of pulses, so the pulses since are counted; and the period last
	// in force. And the rig Clock's resets, heard whatever clocks me.
	bool countHeard = false;
	const tree::Node* heardFrom = nullptr;
	uint32_t heardPulses = 0;
	double knownPeriod = 0.0;
	const tree::Node* resetFrom = nullptr;
	uint32_t heardResets = 0;
	uint32_t rigResetAfter = 0; // the rig reset heard: the pulses before it, and whether it binds (`Count::resetBinds`)
	bool rigResetBinds = false;
	bool cableWaiting = false; // a cable just patched in, its first trigger not yet come: no ramp
	// What the readers saw last wake: the serial of steps begun, whether it
	// was armed, and the frame the writer next moves by itself, -1 for none,
	// which is what the Seq wakes for (`nextEvent`).
	uint32_t lastBegan = 0, lastStarts = 0, lastAims = 0;
	bool lastArmed = true;
	// The serial of steps begun the STEP jack and the triggers over Trgs last
	// fired on, and which of those triggers: a landing re-aimed inside the
	// window fires only what it makes due that had not fired (one pulse, one
	// decision).
	uint32_t trigBegan = 0, gatesBegan = 0;
	bool trigFired = false;
	int gatesFired = 0;
	// The step the last pulse landed from and its member, kept at the wake
	// its decision opened, so a block reshaped inside the window reads that
	// step anew (`Writer::keepPulse`): the frame it is for, -1 for none.
	int64_t preFor = -1, prePlace = 0;
	Cartridge* preMember = nullptr;
	int preWithin = 0;
	bool silentWork = false; // armed and playing on the step last sounded, for `rampsFine`
	int64_t nextDue = -1;
	bool rampsFine = false; // the writer's ramp is moving and a reader plays it every sample
	// The band-limited edges (`bandLimit`): one a channel, the channels they
	// were for, the frame last written, the walk as it stood there, each
	// channel's naive value and slope, and the samples still owed a
	// correction, which keep the Seq waking every sample after its ramp
	// has stopped.
	Blep bleps[PORT_MAX_CHANNELS];
	int blepN = 0;
	int64_t blepFrame = -1;
	uint32_t blepBegan = 0, blepStarts = 0;
	double blepAlong = 0.0, blepOwnAlong = 0.0;
	int64_t blepPlace = 0; // under a turn, the turned place the last frame, and how far through
	double blepTAlong = 0.0;
	float blepWas[PORT_MAX_CHANNELS] = {}, blepSlope[PORT_MAX_CHANNELS] = {}, blepOut[PORT_MAX_CHANNELS] = {};
	int blepTail = 0;
	// Oversampling's own: the edges at four times the rate and the way back
	// down, a channel each; the curve the last frame stood on, the way it was
	// read and the parse it belonged to; and whether the last frame was
	// oversampled.
	Blep bleps4[PORT_MAX_CHANNELS];
	Decimate4 down[PORT_MAX_CHANNELS];
	const Cartridge* blepCurve = nullptr;
	bool blepFlip = false, blepOver = false;
	// Oversampled under PHASE: the jack's volts at four times the rate
	// (`PhaseUp4`), heard every sample; the place the walk stood on before
	// the turn the last whole wake, so the points between move it straight;
	// and the last point the jack sounded from them, for an edge between it
	// and the next.
	PhaseUp4 phaseUp;
	int64_t subFromStep = 0, subFromFrame = -2, subStarts = -1;
	double subFromAlong = 0.0;
	int64_t blepSubPlace = 0;
	double blepSubAlong = 0.0;
	bool blepSubOk = false;
	/** The one choice at the OUT, slew or BLEP and Oversampling, made in
	every clock context, and kept apart for each: under
	a Clock in hertz the HZ three, the diodes on by default and the slew
	off, so a Clock turned to HZ sounds band-limited at once, and a hand
	that wants an envelope in hertz lights the slew there; anywhere else,
	a Clock in BPM, a cable or nothing, the plain three, all off. Which
	three are in force is this and nothing else, the engine's by
	`hertzWas` and the screens' by `hertzNow`; the slew's time is one. */
	struct Choice {
		int slewOn, blep, over;
	};
	static Choice choiceIn(bool hertz) {
		return hertz ? Choice{SLEW_ON_HZ_PARAM, BLEP_HZ_PARAM, OVER_HZ_PARAM} : Choice{SLEW_ON_PARAM, BLEP_PARAM, OVER_PARAM};
	}
	/** The choice in force this wake, read at the census: the slew's switch,
	BLEP (the diode on the left of OUT: the edges band-limited; off for a
	phasor into another's PHASE, which must stay exact, or to save the CPU)
	and Oversampling (the diode on the right: the jack worked out at four
	times the rate, its curves and edges there, and brought back down,
	`Decimate4`; off to save the CPU, at the price of a curve's folding). */
	bool slewOnWork = false;
	bool blepWork = true;
	bool overWork = true;
	/** Under a Clock in hertz (`choiceIn`). Measured at the census, outside
	any wake, and stored there on a change. */
	std::atomic<bool> hertzNow{false};
	bool hertzWas = false;
	/** Whether the edges are band-limited this wake: BLEP or Oversampling
	on, unless the slew acts, the choice made, a scene or a patch having lit
	both (`slewInForce`). */
	bool bandHere() { return !(slewOnWork && slewModeWork != SLEW_NONE) && (blepWork || overWork); }
	/** Slew, on the lamp at the OUT: a micro slew that takes the click off a
	step's jump, for an envelope built of steps or a CV whose sharp corner
	another module would hear, not a musical glide. Two params, so both are
	undone, held in a scene and set back by Initialize: its time, a place on
	the ladder below, and its switch, the time kept while it is off, as a
	Mix's mute keeps a level. It acts only where it means something
	(`SlewMode`): over CVs and CRVs and standing alone, an arpeggiator, a
	glide; over Atns in Step a crossfade between them, the Seq a
	sequential switch and the lamp named Interpolate there (the crossfade of their live outputs rather than a glide between
	values, which would freeze a moving signal for the length of it); and nowhere
	else. In every clock context it is one choice with BLEP and Oversampling
	(`Choice`), whichever is lit owning the edge.

	The ladder's rungs begin in samples, a count whatever the sample rate, by
	half octaves from two (one sample is the step's own and slews nothing), to
	32, the last such count under a millisecond at every rate Rack runs at,
	44.1 kHz and up, so the ladder never turns back on itself; then
	milliseconds from one to 500, ten rungs a decade on round numbers. At 48
	kHz the seam is 32 samples to 48, a rung like any other; at 96 kHz and
	above it is wider, 32 to 96 and more, the price of a sample rung meaning
	samples everywhere. The param is a place along the ladder, a whole number
	a rung, which a plain stroke steps along; a command stroke stops between
	two rungs, and there the time is between theirs by ratio, the fraction of
	the way in octaves (`slewLength`), so a fine stroke moves the same share
	of the time wherever it is, between 4 and 6 samples as between 400 and 500
	ms. */
	static constexpr int SLEW_SAMPLE_RUNGS = 9;
	static constexpr int SLEW_RUNGS = SLEW_SAMPLE_RUNGS + 28; // how many rungs; the top is half a second
	static constexpr int SLEW_RUNG_1MS = SLEW_SAMPLE_RUNGS;   // the first in milliseconds, the default time
	/** Rung `k` as a count of samples, under the seam, or else as tenths of
	a millisecond: both integers, so a rung is exact. */
	static int slewSampleCount(int k) {
		static const int counts[SLEW_SAMPLE_RUNGS] = {2, 3, 4, 6, 8, 12, 16, 24, 32};
		return counts[clamp(k, 0, SLEW_SAMPLE_RUNGS - 1)];
	}
	static int slewTenths(int k) {
		static const int decade[10] = {10, 12, 15, 20, 25, 30, 40, 50, 60, 80};
		const int i = clamp(k, SLEW_SAMPLE_RUNGS, SLEW_RUNGS - 1) - SLEW_SAMPLE_RUNGS;
		return decade[i % 10] * (i < 10 ? 1 : i < 20 ? 10 : 100);
	}
	/** Rung `k`'s length in samples at `sampleRate`: a sample rung its whole
	count, a millisecond rung its time at the rate. */
	static double slewRungLength(int k, float sampleRate) {
		if (k < SLEW_SAMPLE_RUNGS)
			return (double)slewSampleCount(k);
		return (double)slewTenths(k) * (double)sampleRate / 10000.0;
	}
	/** The slew at place `p` of the ladder, in samples at `sampleRate`: on a
	rung its length, exactly, and between two the length between theirs by
	ratio. A time and not a count: a glide is measured against it sample by
	sample and lands on the first sample at or past it (`slew`), so 4.5
	samples eases differently from 4 and from 5. */
	static double slewLength(float p, float sampleRate) {
		const float q = clamp(p, 0.f, (float)(SLEW_RUNGS - 1));
		const int k = (int)std::floor(q);
		const double f = (double)q - (double)k;
		const double a = slewRungLength(k, sampleRate);
		if (f == 0.0 || k >= SLEW_RUNGS - 1)
			return a;
		return a * std::pow(slewRungLength(k + 1, sampleRate) / a, f);
	}
	/** Where `samples` stands on the ladder at `sampleRate`, the inverse of
	`slewLength`: a time typed in, kept as typed, inside the ladder's ends. */
	static float slewPlaceOf(double samples, float sampleRate) {
		if (!(samples > slewRungLength(0, sampleRate)))
			return 0.f;
		for (int k = 0; k + 1 < SLEW_RUNGS; k++) {
			const double a = slewRungLength(k, sampleRate), b = slewRungLength(k + 1, sampleRate);
			if (samples <= b)
				return (float)k + (float)(std::log(samples / a) / std::log(b / a));
		}
		return (float)(SLEW_RUNGS - 1);
	}
	float slewPlace() { return clamp(params[SLEW_PARAM].getValue(), 0.f, (float)(SLEW_RUNGS - 1)); }
	/** The slew's switch in force where the screens stand (`choiceIn`), and
	whether it is on. */
	int slewSwitch() { return choiceIn(relaxed(hertzNow)).slewOn; }
	bool slewOn() { return params[slewSwitch()].getValue() > 0.5f; }
	/** What the slew does where the Seq stands, worked out at every wake:
	nothing (over Trgs or Qnts, or in Fade); a glide over CVs and CRVs and
	standing alone, the jack easing from where it stood onto the new step's
	value; or over Atns in Step a crossfade, the last step's signal handed
	over to the next's, both running. */
	enum SlewMode { SLEW_NONE, SLEW_GLIDE, SLEW_FADE };
	SlewMode slewModeWork = SLEW_NONE;
	// The glide: the jack's last frame, a channel each, and how many; the
	// step it eased from, the member and the step sounded, so a new one
	// sets it going; the error it eases away, a channel each, the member
	// it fades from, its length in samples, a time, and how far along it
	// stands, a count, and the frame last written. `slewLeft` is the samples
	// it still owes, which keep the Seq waking every sample until it lands and
	// not one after.
	float slewOut[PORT_MAX_CHANNELS] = {}, slewErr[PORT_MAX_CHANNELS] = {};
	int slewN = 0;
	const Cartridge* slewMember = nullptr;
	int slewStep = -1;
	Cartridge* fadeFrom = nullptr;
	int slewErrN = 0;
	double slewLen = 0.0;
	int slewAt = 0;
	int64_t slewFrame = -1;
	int slewLeft = 0;
	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		rateModeToJson(rootJ);
		return rootJ;
	}
	void dataFromJson(json_t* rootJ) override {
		Member::dataFromJson(rootJ);
		rateModeFromJson(rootJ);
	}
	/** What a RIG's scene holds of me past my params: the rate's mode, whole
	(`RateMode`); the slew, BLEP and Oversampling are params, held with the
	rest. */
	enum SceneSetting { SCENE_FREE, SCENE_SETTINGS };
	int sceneSettings() override { return SCENE_SETTINGS; }
	float sceneSetting(int k) override {
		switch (k) {
			case SCENE_FREE: return rateFreeSet() ? 1.f : 0.f;
			default: return 0.f;
		}
	}
	void setSceneSetting(int k, float v) override {
		switch (k) {
			case SCENE_FREE:
				setRateFree(v != 0.f);
				wake();
				break;
			default: break;
		}
	}
	/** Initialize leaves a Seq as one fresh from the browser, the rate's
	mode too. */
	void onReset(const ResetEvent& e) override {
		setRateFree(false);
		Member::onReset(e);
	}
	// The place the playhead stood on at the last wake, turned by PHASE,
	// and the walk's own place before the turn: the readers' memory, for a
	// landing under a phase and its guard, a driver taken on, and the block
	// reshaped under the walk.
	int64_t shownPlace = 0, walkPlace = 0;
	int soundedPos = 0; // the step the jack last sounded, which it plays on while armed
	int64_t soundedPlace = 0; // and its place, for the way a curve is played on it
	int64_t heardPlace = 0;   // the turned place the jack sounded at the last wake, under a phase
	// PHASE as the readers hear it, the jack's volts on time, as the walk's
	// landing is.
	float phaseHeard = 0.f;
	WindowFrames windowNow; // the window in frames, kept
	bool woke = false; // a wake has been made since the Seq was born
	Drive lastDrive = DRIVE_SCREEN;
	// The events of this sample, from the jacks and the hand, for the wake
	// to hand the writer: a reset (the RESET jack, a double click on the step
	// screen), a step let go after a drag, a Sync.
	bool resetAsked = false;
	bool resetByHand = false; // and the reset was the double click, not the jack
	int jumpAsked = -1;
	bool synced = false;
	Pulse trig; // the STEP jack's pulse, a step begun or EOC
	JackWatch in; // the IN jack, read every sample so an audio-rate signal gets through
	dsp::SchmittTrigger rst;
	Drive drive = DRIVE_SCREEN; // what moves the playhead, settled every scan
	std::atomic<bool> resetClick{false}; // the lower screen was clicked, driven
	std::atomic<int> jumpClick{-1};      // the lower screen was dragged and let go, driven: the step
	std::atomic<bool> holding{false};    // the mouse is down on the lower screen, driven
	std::atomic<int> holdPos{0};         // and this is the step under it
	const Module* stoodOut() override { return relaxed(stoodOn); }
	bool holdStep(int step, bool down) override {
		if (down)
			holdPos.store(step, std::memory_order_relaxed);
		else
			jumpClick.store(step, std::memory_order_relaxed);
		holding.store(down, std::memory_order_relaxed);
		poke.store(true, std::memory_order_relaxed);
		return true;
	}
	// HALT, driven: the writer ignores pulses and holds where it stands, and
	// a drag on the step screen or a reset arms it on the step picked, shown
	// and sounded by the hand; leaving HALT the next pulse lands it, or,
	// untouched, goes on from where it held (the writer's rule).
	// The block reshaped under a running walk: the member the walk's own
	// step stood on at the last wake, and the step within it (a ring of a
	// Trg), so the walk stays on it when members come or go or a Trg's rings
	// change; for a Seq on its own memory, no member and the step itself.
	int lastWalkPos = 0, lastCount = 0;
	Cartridge* walkMember = nullptr;
	int walkWithin = 0;
	uint32_t chainSerial = 0; // a parse, which may have changed the block
	uint32_t identSerial = UINT32_MAX;              // and the block and length the members above were found in
	int identCount = -1;
	int keptStep = -1;     // undriven, a step the wake moved to: posted into POS and stood on until the write lands
	float keptFrom = 0.f;  // and what POS held when it was posted
	/** Undriven, the wake puts the playhead on `to`: posted into POS, as one
	undo named `name` (or the patch performing), and stood on from this wake
	until the write lands or a hand moves POS first, so the step is the
	engine's at once and the param the UI's, written on its own thread. */
	void keepStep(int to, const char* name, bool performed) {
		const float now = params[STEP_PARAM].getValue();
		if (to == (keptStep >= 0 ? keptStep : (int)std::round(now)))
			return;
		keptFrom = now;
		keptStep = to;
		postWrite(STEP_PARAM, (float)to, name, performed);
	}

	/** The rate the screen is set to, on the one ladder, in millionths. */
	int rateSet() { return rateParamOf(this, RATE_PARAM); }
	/** The rate, told what this module's own controls say. */
	RateIn& rateCore() {
		rate = rateInOf(this, RATE_PARAM, RATE_INPUT);
		return rate;
	}
	/** The rate in force: the screen's, moved by the RATE jack as a Clock's
	is, a volt an octave around it, nought volts leaving it; whole, snapped to
	the nearest whole rung; free, its point lit, continuous, which the walk
	takes as a turn instead (`rateTurn`) and the hover as this (`RateIn`).
	There is no stop and no backwards on it: backwards is the direction
	cell. */
	int rateNow() { return rateCore().rate(); }

	Drive shownDrive() { return (Drive)shownBits(); }
	/** Driven by the rig's count: a click on the lower screen restarts. */
	bool slavedShown() { return shownDrive() == DRIVE_TIME; }
	/** A cable in PHASE: the sequence is turned under the playhead, so the
	step screen carries the bar that says how far and the pulse jack is STEP
	rather than the end of the cycle. */
	bool phasedShown() { return relaxed(shownPhased); }
	/** Walking Trgs a step each: the jack at the foot fires on each lit ring. */
	bool triggersShown() { return relaxed(gatesNow) && params[ACROSS_PARAM].getValue() <= 0.5f; }
	/** Nothing drives it: the lower screen picks the step. */
	bool picksShown() { return shownDrive() == DRIVE_SCREEN; }
	/** The rate moving nothing: no clock drives the walk, nothing reaching
	it or HALT ignoring it, so the rate screen stands dark, its cells' ghosts
	alone, and takes no stroke and no hover, as a part with nothing to do is
	hidden (an Atn's arrows while a cable is in IN). A phase does not wake
	it, the rate never scaling the phase. From
	what the last wake published and the mode's param. */
	bool rateIdle() { return picksShown() || modeShown() == MODE_HALT; }
	/** Nothing to play: no block to my left and no cable in POLY IN, so the one
	step there is is nought volts whatever drives it, and the step screen
	stands idle as the rate's does, only the cells' ghosts, taking no stroke.
	A clock or a phase alone
	does not wake it, there being nothing for them to walk; a mono cable in
	ARP does, a step of one note. */
	bool stepIdle() { return relaxed(emptyNow); }
	/** Whether the slew acts where I stand, for the lamp at the OUT. */
	bool slewShown() { return relaxed(slewNow) != SLEW_NONE; }
	/** Whether BLEP and oversampling act on my jack, in every clock
	context: where it is volts and not a block of Trgs' gates, nor a
	crossfade (`bandLimit`), shown beside the slew's lamp wherever each
	acts, one choice with it. */
	bool bandShown() { return !relaxed(gatesNow) && !fadeShown(); }
	/** Whether Oversampling means something over what I walk, `kind` the
	traits walked: not over Atns. Their outputs are live
	signals already sampled at the rate, with no curve between two samples
	to read, so four times over is only straight lines between the samples
	and a lowpass back down: 4.3 dB off a sine at 18 kHz, 1.7 at 12 kHz,
	and a frame and a half late, with nothing at a switch's jump that BLEP,
	measuring both Atns at the frame, does not do. Hidden there, and off
	whatever its param holds. */
	static bool overMeans(uint32_t kind) { return !(kind & tree::T_ATN); }
	/** Oversampling's diode shown: where BLEP's is, and not over Atns
	(`overMeans`, as the last wake published what I walk). */
	bool overShown() { return bandShown() && !relaxed(overAtnNow); }
	/** The slew acting where I stand, lit and shown: the choice made, and
	the diodes out whatever their params hold (`bandHere`), a scene or a
	patch having lit both. */
	bool slewInForce() { return slewOn() && slewShown(); }
	/** What turning BLEP (`blep`) and Oversampling (`over`) to `to` writes on
	me: the pair in force where I stand (`choiceIn`), and lighting them puts
	the slew there out, the one choice being the hand's to keep (the engine
	never sets a param). */
	void bandWrites(bool blep, bool over, float to, std::vector<std::pair<int, float>>& out) {
		const Choice c = choiceIn(relaxed(hertzNow));
		if (blep)
			out.push_back({c.blep, to});
		if (over && !relaxed(overAtnNow)) // never over Atns, where 4X means nothing (`overMeans`)
			out.push_back({c.over, to});
		if (to > 0.5f)
			out.push_back({c.slewOn, 0.f});
	}
	/** Fade (`FADE_PARAM`, the square on the strip's left over Atns): over a
	block of Atns OUT is not the Atn under the playhead switched in as each
	step lands, but a crossfade between the two either side of the place,
	equal gain, linear in the place, each Atn exact at the start of its step
	and fading toward the next across it, so a stepped PHASE lands exactly on
	an Atn and a ramp morphs. Off, OUT steps, the Interpolate lamp with it.
	The param is kept wherever the Seq stands, and means something only over
	Atns that route (`overRouters`). */
	bool fadeOn() { return params[FADE_PARAM].getValue() > 0.5f; }
	/** Fade in force: over Atns, and chosen. From what the last wake
	published and the param. */
	bool fadeShown() { return relaxed(overRoutersNow) && fadeOn(); }
	/** Walking Atns that route, the block Fade crossfades between: not
	shaping Atns whose window something past me reads, configuring the CVs
	there, where a crossfade means nothing and the square is not there,
	unless a cable in
	ROUTE hands them a signal to carry, the sequential router whose jacks
	Fade pans across. Shaping Atns nothing reads
	play what comes in at ROUTE, and fade as routers do. */
	/** What the Atns I walk give OUT: each its offset alone, each a signal
	(a cable in its own jack, what its lit arrow takes, or ROUTE handed to
	it), or some of each. */
	enum AtnCarry { CARRY_OFFSETS, CARRY_SIGNALS, CARRY_MIXED };
	AtnCarry atnCarry() {
		if (routeWork)
			return CARRY_SIGNALS;
		int signals = 0;
		for (Cartridge* c : chain)
			signals += c->carriesSignal() ? 1 : 0;
		return signals == 0 ? CARRY_OFFSETS : signals == (int)chain.size() ? CARRY_SIGNALS : CARRY_MIXED;
	}
	/** Over Atns that give their offsets alone, as the last wake published. */
	bool offsetsOnly() { return relaxed(overAtnNow) && relaxed(atnCarryNow) == CARRY_OFFSETS; }
	bool overRouters() const {
		return (walked() & tree::T_ATN) &&
		       (in.live() || !((traits & tree::T_SHAPER) && (told() & (tree::TOLD_WINDOW | tree::TOLD_ATN_WINDOW))));
	}
	/** A square on the strip's left: P over Trgs, Step or Fade over Atns. */
	bool squareShown() { return relaxed(gatesNow) || relaxed(overRoutersNow); }
	int idleOut() override { return (stepIdle() ? 1 : 0) | (rateIdle() ? 2 : 0); }

	Seq() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam<StepQuantity>(STEP_PARAM, 0.f, (float)(MAX_SEQ_STEPS - 1), 0.f, "Step")->head = this;
		getParamQuantity(STEP_PARAM)->snapEnabled = true;
		// PREVIEW by default. The tooltip is the mode in force as the value,
		// a word, and what that mode does as the lesson (`retipMode`): a
		// paragraph naming all three covered the view. The clicks are said
		// under it, a line each.
		configSwitch<ModeQuantity>(MODE_PARAM, 0.f, (float)(MODES - 1), (float)MODE_PREVIEW, "Mode",
		                           {"Play", "Preview", "Halt", "Once"});
		retipParam(this, MODE_PARAM, {"Mode", "", Gestures{&G_MODE_NEXT, &G_MODE_PLAY, &G_MODE_HALT}});
		configSwitch(DIR_PARAM, 0.f, (float)(DIRS - 1), (float)DIR_FORWARD, "Direction", {"Forward", "Pendulum", "Reverse"});
		retipParam(this, DIR_PARAM, {"Direction", "Forward, pendulum and reverse in turn."});
		// Named for what it does, so it is never one of three "Poly"s with POLY
		// IN and the poly OUT. Its lesson is said over TRGs alone (`retipAll`).
		configSwitch<WithHint<SwitchQuantity>>(ACROSS_PARAM, 0.f, 1.f, 0.f, "Poly steps", {"Off", "On"});
		retipParam(this, ACROSS_PARAM, {"Poly steps", ""});
		configParam<SeqRateQuantity>(RATE_PARAM, (float)RATE_MIN, (float)RATE_MAX, 1.f, "Rate");
		// A helper, not a voice: off by default at a millisecond, and never
		// rolled (`rolled`).
		configParam<SlewQuantity>(SLEW_PARAM, 0.f, (float)(SLEW_RUNGS - 1), (float)SLEW_RUNG_1MS, "Slew");
		configSwitch(SLEW_ON_PARAM, 0.f, 1.f, 0.f, "Slew on", {"Off", "On"});
		configSwitch(SLEW_ON_HZ_PARAM, 0.f, 1.f, 0.f, "Slew on", {"Off", "On"});
		// The edges in hertz, both on by default, as diodes either side of OUT.
		// One name for both, since a plain click on either sets both; the
		// value says which are on (`retipLamp`).
		configSwitch<WithHint<SwitchQuantity>>(BLEP_HZ_PARAM, 0.f, 1.f, 1.f, ANTI_ALIASING, {"Off", "On"});
		configSwitch<WithHint<SwitchQuantity>>(OVER_HZ_PARAM, 0.f, 1.f, 1.f, ANTI_ALIASING, {"Off", "On"});
		// The same two outside hertz, off by default, so a phasor passes
		// exact; the lessons of all four are said where they stand
		// (`retipAll`).
		configSwitch<WithHint<SwitchQuantity>>(BLEP_PARAM, 0.f, 1.f, 0.f, ANTI_ALIASING, {"Off", "On"});
		configSwitch<WithHint<SwitchQuantity>>(OVER_PARAM, 0.f, 1.f, 0.f, ANTI_ALIASING, {"Off", "On"});
		configSwitch<WithHint<SwitchQuantity>>(FADE_PARAM, 0.f, 1.f, 0.f, "Crossfade", {"Off", "On"});
		retipParam(this, FADE_PARAM, {"Crossfade", ""});
		configInput(RATE_INPUT, "Rate");
		retipIn(this, RATE_INPUT, {"Rate", RATE_JACK_TIP});
		configInput(IN_INPUT, "Record");
		in.bind(inputs[IN_INPUT]);
		configInput(RESET_INPUT, "Reset");
		retipIn(this, RESET_INPUT, {"Reset", ""});
		configInput(PHASE_INPUT, "Phase");
		retipIn(this, PHASE_INPUT, {"Phase", SEQ_PHASE_LESSON});
		// CLK said out, and nothing more: a clock input needs no lesson.
		configInput(CLOCK_INPUT, "External clock");
		retipIn(this, CLOCK_INPUT, {"External clock", ""});
		configOutput(STEP_OUTPUT, "Step");
		configOutput(CV_OUTPUT, "Sequence");
	}

	/** A Seq is two modules: a playhead over the block to its left, and, with
	nothing to walk, an arpeggiator with a clock jack of its own.
	Nothing here describes the one it is not. */
	void retipAll() override {
		const bool alone = relaxed(aloneNow);
		const bool gates = relaxed(gatesNow);
		const bool poly = relaxed(outPoly);
		// Nothing to play, the step screen says how to wake the Seq. The
		// rate moving nothing has no hover (`SeqRateParam`), so its value
		// alone says so, for anywhere else that asks.
		const bool empty = stepIdle();
		retipSquares(gates);
		// The direction cell turns nothing on an idle screen (`DirCell`), so
		// it offers no click there.
		retipParam(this, DIR_PARAM, {"Direction", empty ? "" : "Forward, pendulum and reverse in turn."});
		retipRate();
		const Mode shownMode = modeShown();
		retipMode(shownMode);
		retipStep(empty, shownMode == MODE_HALT);
		retipRec(alone, gates);
		retipLamp();
		retipOuts(alone, gates, poly);
	}

	/** The Poly square and Crossfade. */
	void retipSquares(bool gates) {
		// The two squares speak only where they stand, the Poly square over
		// TRGs and Crossfade over ATNs: hidden, a lesson naming a module not
		// in the rig is untrue.
		if (gates)
			retipParam(this, ACROSS_PARAM, {"Poly steps", params[ACROSS_PARAM].getValue() > 0.5f ? ACROSS_ON_LESSON : ACROSS_OFF_LESSON,
			                                Gestures{&G_TOGGLE}});
		else
			retipParam(this, ACROSS_PARAM, {"Poly steps", "", Gestures{}});
		if (relaxed(overRoutersNow))
			retipParam(this, FADE_PARAM, {"Crossfade", relaxed(routeNow) ? FADE_ROUTE_LESSON : offsetsOnly() ? FADE_OFFSETS_LESSON : FADE_LESSON,
			                              Gestures{&G_TOGGLE}});
		else
			retipParam(this, FADE_PARAM, {"Crossfade", "", Gestures{}});
	}

	/** The rate, its jack and PHASE. */
	void retipRate() {
		// The rate in force where the RATE jack sweeps it, as the screen
		// shows it.
		const int set = rateSet();
		// The rate the last wake published: `rateNow` works it out through
		// `rateCore`, which stores into the RateIn the audio thread reads,
		// and this is the UI thread.
		const int now = inputs[RATE_INPUT].isConnected() ? relaxed(rateShown) : set;
		if (rateIdle()) {
			retipRateInForce(this, RATE_PARAM, set, "");
			retipParam(this, RATE_PARAM, {"Rate", ""});
		}
		else {
			// The rate the RATE jack puts in force, said after the value
			// (`retipRateInForce`), as a Clock's is; then what the mode
			// (`RateMode`) does to the rate, its switch said at once.
			retipRateInForce(this, RATE_PARAM, now, "the RATE jack");
			// Only when a change is heard, the one thing a musician cannot
			// guess; a change
			// waits for the next pulse of whatever clocks the SEQ
			// (`Writer::want`).
			const bool free = rateFreeOf(this, set);
			const std::string lesson = rateModeLesson(free, "clock pulse");
			retipParam(this, RATE_PARAM, {"Rate", lesson.c_str(), rateGestures()});
		}
		// The jack's law alone, and only where it is in force: with the rate
		// idle, nothing clocking the walk or HALT, the jack moves nothing, so
		// it says nothing, as the rate's screen stands dark.
		const bool rateFreeNow = rateFreeOf(this, set);
		retipIn(this, RATE_INPUT, {rateJackName(rateFreeNow), rateIdle() ? "" : rateFreeNow ? RATE_JACK_FREE_TIP : RATE_JACK_TIP});
		retipIn(this, PHASE_INPUT, {"Phase", SEQ_PHASE_LESSON});
	}

	/** The mode button and RESET. */
	void retipMode(Mode shownMode) {
		const bool halted = shownMode == MODE_HALT;
		// What the mode in force does, where a musician could not guess it,
		// and never by a gesture, which the tooltip does not name: PREVIEW
		// plays on as PLAY does, a member in hand heard at once; standing alone, an arpeggiator, HALT stops
		// the arpeggio; a spent ONCE stands on its last step, which OUT goes
		// on giving (Writer.hpp, `spent`). Then the clicks, by where each goes
		// from here (`modeToward`).
		const char* lesson = shownMode == MODE_PREVIEW ? "Plays on, and a step in hand sounds at once."
		                     : halted ? (relaxed(aloneNow) ? "The arpeggio stops on its step." : "Stays on the step the screen shows.")
		                     : shownMode == MODE_ONCE ? "One pass, then stays on the last step until reset."
		                                              : "";
		retipParam(this, MODE_PARAM, {"Mode", lesson,
		                              Gestures{&G_MODE_NEXT, shownMode == MODE_PLAY ? &G_MODE_SHIFT_HALT : &G_MODE_PLAY,
		                                       halted ? &G_MODE_CMD_PLAY : &G_MODE_HALT}});
		// RESET by what it does where the SEQ stands (Writer.hpp, `reset`,
		// `arm`, `sound`): clocked, an arm, step one waiting for the next
		// pulse, and in ONCE that pulse starts a pass; in HALT step one at
		// once, sounded; with nothing clocking it, step one, which needs no
		// lesson.
		if (halted)
			retipIn(this, RESET_INPUT, {"Reset", "In HALT, a trigger plays step one at once."});
		else if (slavedShown())
			retipIn(this, RESET_INPUT, {"Reset", shownMode == MODE_ONCE
			                                          ? "A trigger arms step one, and the next clock pulse starts a pass."
			                                          : "A trigger arms step one, to play on the next clock pulse."});
		else
			retipIn(this, RESET_INPUT, {"Reset", ""});
	}

	/** The step screen. */
	void retipStep(bool empty, bool halted) {
		// What the step screen shows and what a hand does to it, by what
		// moves it (`StepParam`): nothing to play, the way to give it
		// something, the value itself saying so (`StepQuantity::getString`);
		// under PHASE, the step
		// PHASE counts from, which the screen shows with the bar under it;
		// under a clock, the hold; in HALT or with nothing clocking it, a
		// drag picks the step, said on the hint line alone.
		// Under PHASE the screen still holds and picks, so its strokes are
		// said there too.
		const bool holds = slavedShown() && !halted;
		const Gestures stepStrokes = holds ? Gestures{&G_STEP_HOLD, &G_STEP_ONE} : Gestures{&G_STEP_PICK, &G_STEP_ONE};
		if (empty)
			retipParam(this, STEP_PARAM, {"Step", relaxed(polyArpNow) ? SEQ_POLY_EMPTY_LESSON : SEQ_WAKE_LESSON, Gestures{}});
		else if (phasedShown())
			retipParam(this, STEP_PARAM, {"Step", "PHASE counts from this step; the bar under it shows how far.", stepStrokes});
		else if (holds)
			retipParam(this, STEP_PARAM, {"Step", "A held step plays until you let go, then the sequence carries on from it.",
			                              stepStrokes});
		else
			retipParam(this, STEP_PARAM, {"Step", "", stepStrokes});
	}

	/** The jack on top. */
	void retipRec(bool alone, bool gates) {
		// Named and said by what it does here alone: no unit on
		// a jack that passes what it is given.
		if (relaxed(overAtnNow))
			retipIn(this, IN_INPUT, {"Route", "Any signal, sent into the ATN under the playhead."});
		else if (relaxed(overQntNow))
			retipIn(this, IN_INPUT, {"Quantizer", "A voltage here is quantized by the QNT under the playhead."});
		else if (alone) {
			retipIn(this, IN_INPUT, {"Poly", relaxed(polyArpNow) ? SEQ_ARP_POLY_LESSON : SEQ_ARP_LESSON});
		}
		else if (gates)
			retipIn(this, IN_INPUT, {"Record", "Patch triggers here to record them, quantized to the nearest step."});
		else
			retipIn(this, IN_INPUT, {"Record", "What arrives here is written into the step under the playhead."});
	}

	/** The lamp and the diodes. */
	void retipLamp() {
		// The lamp says what it is in a sentence, the same over CVs and
		// alone, and then its strokes; hidden, it says nothing (`slewShown`), and
		// it is hidden under a crossfade (`slewModeOf`). Over Atns in Step
		// the Seq is a sequential switch and the lamp is
		// Interpolate, a glide between the two Atns' live outputs,
		// mirroring the slew. OUT is never poly where
		// the lamp shows, so no "each channel".
		// Where the diodes stand beside the lamp, each says that lighting it
		// puts the other out (`bandShown`), in either clock context; over
		// Atns BLEP stands alone (`overShown`).
		const bool oneChoice = slewShown() && bandShown();
		const bool pair = !relaxed(overAtnNow); // 4X stands beside BLEP wherever it means something (`overMeans`)
		const std::string slewTail = !oneChoice ? "" : pair ? PUTS_BAND_OUT : PUTS_BLEP_OUT;
		// What it glides between said as the Atns give it (`AtnCarry`).
		const int carry = relaxed(atnCarryNow);
		const char* glides = carry == CARRY_OFFSETS ? "Glides OUT from one ATN's offset to the next over this time."
		                     : carry == CARRY_MIXED ? "Glides OUT from one ATN to the next over this time."
		                                            : "Glides OUT from one ATN's live signal to the next over this time.";
		if (relaxed(slewNow) == SLEW_FADE)
			retipParam(this, SLEW_PARAM, {"Interpolate", (glides + slewTail).c_str(),
			                              Gestures{&G_INTERP_TOGGLE, &G_INTERP_DRAG, &G_LAMP_FINE}});
		else
			retipParam(this, SLEW_PARAM, {"Slew", ("Slews OUT from each step to the next." + slewTail).c_str(),
			                              Gestures{&G_SLEW_TOGGLE, &G_SLEW_DRAG, &G_LAMP_FINE}});
		const std::string diodeTail = !oneChoice ? "" : relaxed(slewNow) == SLEW_FADE ? PUTS_INTERP_OUT : PUTS_SLEW_OUT;
		retipParam(this, BLEP_PARAM, {ANTI_ALIASING, (BLEP_LESSON + diodeTail).c_str(), diodeGestures(pair)});
		retipParam(this, OVER_PARAM, {ANTI_ALIASING, (OVER_LESSON + diodeTail).c_str(), diodeGestures(true)}); // hidden where BLEP stands alone
		retipParam(this, BLEP_HZ_PARAM, {ANTI_ALIASING, (BLEP_HZ_LESSON + diodeTail).c_str(), diodeGestures(pair)});
		retipParam(this, OVER_HZ_PARAM, {ANTI_ALIASING, (OVER_HZ_LESSON + diodeTail).c_str(), diodeGestures(true)}); // hidden where BLEP stands alone
		// Both diodes of a pair say which of the two are on, the plain click
		// setting both; BLEP alone says itself.
		for (const bool hz : {false, true}) {
			const int b = hz ? BLEP_HZ_PARAM : BLEP_PARAM, o = hz ? OVER_HZ_PARAM : OVER_PARAM;
			const std::string on = bandOn(params[b].getValue() > 0.5f, pair && params[o].getValue() > 0.5f);
			restate(this, b, on);
			restate(this, o, on);
		}
	}

	/** The two jacks below: the pulse jack and OUT. */
	void retipOuts(bool alone, bool gates, bool poly) {
		const bool across = gates && params[ACROSS_PARAM].getValue() > 0.5f;
		// The pulse jack: STEP under PHASE, else EOC, which fires as a cycle
		// begins, the first included (`startOne`), the surprise in its name;
		// a
		// trigger's length goes unsaid.
		if (phasedShown())
			retipOut(this, STEP_OUTPUT, {"Step", "Triggers each time the step changes."});
		else
			retipOut(this, STEP_OUTPUT, {"End of cycle", "Named by convention: it fires as each cycle begins, the first one included."});
		// Named as the caption under it reads (`SeqPanel`), TRIG, POLY or
		// OUT, and said by what it carries, with no unit.
		const char* outName = triggersShown() ? "Trigger" : poly ? "Poly" : "Sequence";
		if (across)
			retipOut(this, CV_OUTPUT, {outName, "Fires the lit triggers of the step under the playhead, each on its own channel."});
		else if (gates)
			retipOut(this, CV_OUTPUT, {outName, "Fires as the playhead lands on a lit step."});
		else if (poly)
			retipOut(this, CV_OUTPUT, {outName, "The step's lit keys as a chord, each on its own channel."});
		// Over Atns, what each gives (`AtnCarry`): a block carrying nothing
		// is a switch of offsets, and says so.
		else if (fadeShown())
			retipOut(this, CV_OUTPUT, {outName, offsetsOnly() ? "The offset of the ATN under the playhead, crossfading into the next across its step."
			                                                  : "The ATN under the playhead, crossfading into the next across its step."});
		else if (relaxed(overAtnNow))
			retipOut(this, CV_OUTPUT, {outName, offsetsOnly() ? "The offset of the ATN under the playhead."
			                                    : relaxed(atnCarryNow) == CARRY_MIXED
			                                        ? "The signal of the ATN under the playhead, or its offset where it has none."
			                                        : "The signal of the ATN under the playhead."});
		else if (relaxed(overQntNow))
			retipOut(this, CV_OUTPUT, {outName, "QNT IN, quantized by the QNT under the playhead."});
		else if (alone)
			retipOut(this, CV_OUTPUT, {outName, "The arpeggio's note."});
		else
			retipOut(this, CV_OUTPUT, {outName, "The step under the playhead."});
	}

	Mode mode() { return (Mode)clamp((int)std::round(params[MODE_PARAM].getValue()), 0, MODES - 1); }
	/** Whether there is nothing to preview: standing alone, an
	arpeggiator, and over Trgs, where a step is a trigger lit or not and a
	hand has nothing to hear. */
	bool previewless() { return relaxed(aloneNow) || relaxed(gatesNow); }
	/** The mode in force, as the button and the tooltips say it: PLAY,
	PREVIEW, HALT or ONCE, and where there is nothing to preview the same
	without PREVIEW, so a PREVIEW kept from a block is PLAY there. */
	Mode modeShown() {
		const Mode m = mode();
		return previewless() && m == MODE_PREVIEW ? MODE_PLAY : m;
	}
	/** The mode a click goes on to: PLAY, PREVIEW, HALT, ONCE and round,
	and with nothing to preview the same with no PREVIEW. */
	Mode modeAfter() {
		const Mode next = (Mode)stepRound((int)modeShown(), 1, MODES);
		return previewless() && next == MODE_PREVIEW ? MODE_HALT : next;
	}
	/** The mode a modified click goes to: straight to `aim`, or to `other` where `aim` is already
	in force, so shift-click is PLAY, or HALT from PLAY, and
	command-click is HALT, or PLAY from HALT, from whatever mode, alone
	or over a block. */
	Mode modeToward(Mode aim, Mode other) { return modeShown() == aim ? other : aim; }
	/** In ONCE, how many places of the cycle from its first are dark on the
	button, the cycle's whole when it is armed or spent; -1 in any other
	mode. And the places in the cycle, the button being divided into them. */
	int onceDark() { return relaxed(onceShown) >> 16; }
	int onceCycle() { return relaxed(onceShown) & 0xffff; }
	int onceOut() override { return onceDark(); }
	/** A member held under the mouse is heard: PREVIEW, and ONCE. Never
	HALT, whose step is the step screen's alone, so a hand editing a member
	neither sounds it nor moves the step. */
	bool preview() { return mode() != MODE_PLAY && mode() != MODE_HALT; }
	/** The kind of the block I walk, its first member's kind bit
	(`tree::KIND_BITS`); walking nothing, none. */
	uint32_t walked() const { return chain.empty() ? 0 : chain[0]->traits & tree::KIND_BITS; }
	/** A block a hand can hold a member of and a preview hear: CVs, Atns,
	Qnts and CRVs. The one block a hand cannot hold a member of is the Trgs,
	whose rings are lit by a click. */
	bool previews() const { return !chain.empty() && !(chain[0]->traits & tree::T_TRG); }
	bool across() { return params[ACROSS_PARAM].getValue() > 0.5f; }

	/** The STEP jack's pulse and the triggers laid over a block of Trgs both have
	to end on time, and the writer moves by itself between pulses: a ramp
	entering a step (`nextDue`), and, for a
	reader that plays the ramp every sample, a block of CRVs or a cable in
	PHASE, the ramp itself (`rampsFine`). Everything else a Seq watches is a
	jack, a param or a click, and the rig sees all three without being told. */
	bool wantsTick() override {
		return trig.until >= 0 || firedUntil >= 0 || nextDue >= 0 || rampsFine || blepTail > 0 || slewLeft > 0 ||
		       inputs[PHASE_INPUT].isConnected() || rateTurnOn;
	}
	/** The reset in is a trigger, so it is watched every sample, and so are
	the IN jack and the click the lower screen leaves behind; and a
	cable in PHASE, the wake made when its volts move. */
	bool everySample(const ProcessArgs& args) override {
		bool wake = false;
		fineTick = false;
		// The trigger is watched whether or not a cable is in, as a driver's
		// are, so a cable that arrives already high is an edge.
		if (rst.process(inputs[RESET_INPUT].getVoltage(), TRIG_LOW, TRIG_HIGH)) {
			resetAsked = true;
			wake = true;
		}
		if (resetClick.exchange(false, std::memory_order_relaxed)) {
			resetAsked = true;
			resetByHand = true;
			wake = true;
		}
		const int j = jumpClick.exchange(-1, std::memory_order_relaxed);
		if (j >= 0) {
			jumpAsked = j;
			wake = true;
		}
		if (clockIn.watch(inputs[CLOCK_INPUT], args.frame))
			wake = true;
		if (inputs[PHASE_INPUT].isConnected()) {
			const float v = inputs[PHASE_INPUT].getVoltage();
			if (v != phaseHeard) {
				phaseHeard = v;
				wake = true;
			}
			// Oversampled, the volts go up to four times the rate every
			// sample, and until the filter has settled on volts standing
			// still every sample is a whole wake, the place moving after the
			// jack has stopped.
			if (overWork && bandHere()) {
				phaseUp.push((double)sane(v), args.frame);
				if (!phaseUp.settled())
					wake = true;
			}
			else
				phaseUp.forget();
		}
		// A free rate's jack turning the walk: its volts are read every
		// sample, as PHASE's are (`rateTurn`).
		if (rateTurnOn)
			wake = true;
		// The writer moving by itself: a ramp entering a step is a step, a
		// wake for everybody; the ramp between, every sample, only for a
		// reader that plays it.
		if (nextDue >= 0 && args.frame >= nextDue)
			wake = true;
		else if (!wake && (rampsFine || blepTail > 0 || slewLeft > 0)) {
			fineTick = true;
			wake = true;
		}
		trig.tick(outputs[STEP_OUTPUT], args);
		// The triggers over Trgs end here, every channel at once, and the
		// end is a wake: a Poly bundling me reads my jack at a wake and
		// nowhere else, so an end the rig never heard left its channel high
		// until the next wake, a step later or whenever anything else moved,
		// and a millisecond's trigger came out of it a gate of any length.
		if (firedUntil >= 0 && args.frame >= firedUntil) {
			firedUntil = -1;
			for (int c = 0; c < jackWidth; c++)
				outputs[CV_OUTPUT].setVoltage(0.f, c);
			wake = true;
		}
		return wake;
	}

	// The slots: the rightmost cartridge, claimed or not, the block being its
	// peer chain; the nearest Clock, shared; the reader before me, for my
	// colour; and the Poly whose channels are my steps when I have no block.
	// No slot takes a signal from the left: the rig carries none.
	enum { S_BLOCK, S_CLOCK, S_READER, S_ARP };
	ROW_SLOT_IS(M_SEQ, S_BLOCK, "block");
	ROW_SLOT_IS(M_SEQ, S_CLOCK, "clock");
	ROW_SLOT_IS(M_SEQ, S_READER, "reader");
	ROW_SLOT_IS(M_SEQ, S_ARP, "arp");
	std::vector<Cartridge*> chain; // the block, leftmost first
	/** Poly turned on over Trgs: every Trg of the block takes the ring
	count of the one with the most, so no beat set while each was its own
	length is lost, and the block is one length from then on, as a
	shift-drag sizes it. Posted as writes that join the turn's own undo step, since
	the engine never sets a param. Only on the turn: a block across whose
	members are of several lengths, loaded so or sized by hand, is left. */
	void conform() {
		int most = 0;
		for (Cartridge* c : chain)
			if ((c->traits & tree::T_TRG) && c->countParam() >= 0)
				most = std::max(most, c->stepsOf(false));
		bool posted = false;
		for (Cartridge* c : chain)
			if ((c->traits & tree::T_TRG) && c->countParam() >= 0 && c->stepsOf(false) < most) {
				if (!posted)
					postFollowing(ACROSS_PARAM);
				posted = true;
				postWrite(c, c->countParam(), (float)most, "conform triggers");
			}
	}
	int lane = -1;                 // my lane on every member of it
	Mapping winOut;
	// The member under the playhead this wake, for forwarding: asked by the
	// nodes to my right in the pass, never by a screen. Atomic and dropped at
	// every parse all the same, since a pointer valid for one wake is a thing
	// to keep short-lived.
	std::atomic<Cartridge*> stoodOn{nullptr};

	/** A Seq writes into everything it reads (a colour, a lane, a lit key on
	the quantizer a member resolves through, the colour its driver wears),
	and all of that is cleared for the whole rig at the top of every wake. */
	bool atEveryWake() override { return true; }
	ROW_MANIFEST(M_SEQ)
	/** A Poly's channels of me: my jack, every channel of it, a chord's notes
	or a bar of gates across Trgs one after the other as a Poly's own are. */
	/** A setting of the rig, the rate, the reset, the phase and the clock,
	reads the first channel of a cable; the IN jack takes
	every channel, a polyphonic one being the steps. */
	int channelsHeard(int input) const override { return input == IN_INPUT ? PORT_MAX_CHANNELS : 1; }
	/** I wear what I walk, my block's `walked`, read off my block slot's
	child; standing alone, an arpeggiator, I am my own product. */
	manifest::Face face() const override {
		const tree::Node* c = child(0);
		const int id = c ? c->manifestId() : -1;
		return id < 0 ? Member::face() : manifest::spec((manifest::Id)id).walked;
	}
	int channelsOut(float* v) override {
		// The width I meant, which an uncabled jack does not keep.
		const int n = outPolyWork ? clamp(jackWidth, 1, PORT_MAX_CHANNELS) : 1;
		for (int c = 0; c < n; c++)
			v[c] = outputs[CV_OUTPUT].getVoltage(c);
		return n;
	}
	void publishReads() override {
		store(headCount, headPos, headColor, headDrive, headPhased);
		shownDir.store((int)(dir == DIR_BOUNCE ? DIR_PENDULUM : dir), std::memory_order_relaxed);
		shownPhase.store(phaseWork, std::memory_order_relaxed);
		shownBase.store(baseShownWork, std::memory_order_relaxed);
		shownArmed.store(armedShownWork, std::memory_order_relaxed);
		rateShown.store(rateWork, std::memory_order_relaxed);
		ratePatched.store(ratePatchedWork, std::memory_order_relaxed);
		rateCv.store(rateCvWork, std::memory_order_relaxed);
		aloneNow.store(aloneWork, std::memory_order_relaxed);
		emptyNow.store(emptyWork, std::memory_order_relaxed);
		polyArpNow.store(polyArpWork, std::memory_order_relaxed);
		recNow.store(recWork, std::memory_order_relaxed);
		routeNow.store(routeWork, std::memory_order_relaxed);
		overAtnNow.store(overAtnWork, std::memory_order_relaxed);
		overRoutersNow.store(overAtnWork && overRouters(), std::memory_order_relaxed);
		atnCarryNow.store(overAtnWork ? atnCarry() : CARRY_SIGNALS, std::memory_order_relaxed);
		slewNow.store(slewModeWork, std::memory_order_relaxed);
		overQntNow.store(overQntWork, std::memory_order_relaxed);
		overCVNow.store(overCVWork, std::memory_order_relaxed);
		gatesNow.store(gatesWork, std::memory_order_relaxed);
		outPoly.store(outPolyWork, std::memory_order_relaxed);
		stoodOn.store(stoodWork, std::memory_order_relaxed);
		onceShown.store(onceWork, std::memory_order_relaxed);
	}
	int tookTold = -1; // what my Clock was last told of its count, -1 since the parse
	int acrossSeen = -1; // whether I walked Trgs across last wake, -1 for not walking Trgs
	bool fineWork = false; // a reader of mine plays the writer's ramp, or the turn, every sample
	void settle() override {
		tookTold = -1;
		stoodOn.store(nullptr, std::memory_order_relaxed);
		stoodWork = nullptr;
		fadeTo = nullptr;
		// No further than the cap: the members past it, furthest from me,
		// are not claimed, so they wear no lane and no colour of mine.
		Cartridge::blockFrom(at(S_BLOCK), MAX_SEQ_STEPS, chain);
		chainSerial++;
		rankColor(at(S_READER), at(S_CLOCK));
		color = colorRank % MAX_COLORS;
		lane = -1;
		const bool wide = across();
		acrossParsed = wide;
		for (Cartridge* c : chain) {
			const int l = c->addLane(color, wide);
			if (lane < 0)
				lane = l;
		}
		// What I walk decides what I am to my right: a shaper where the
		// members are, the window of the member under the playhead handed on,
		// and nothing else.
		if (!chain.empty())
			traits |= chain[0]->traits & tree::T_SHAPER;
	}
	/** Poly is told my block at the parse (`addLane`), a Trg sizing its
	whole block by it, so a turn of Poly is a change of the parse, asked by
	the host. Without it a Trg would keep the size the last parse's Poly gave
	it: the block's members would fall apart when Poly turned on, and two Trgs
	of 8 and 7 steps would stay forced to 14 or 16 when it turned off. */
	bool acrossParsed = false;
	bool parseStale() override { return across() != acrossParsed; }
	int readerColor() override { return color; }
	void onRestart() override {
		synced = true;
		poke.store(true, std::memory_order_relaxed);
	}
	/** My walk as the bracket's mark compares it with the other Seqs of my count
	(`rig::inLine`): where I stand, never what was done to get there. The mark
	asks whether two or more Seqs walking one count stand as no Sync could
	leave them, so a lone Seq, whatever its rate does, never breathes the
	bracket. Left out as deliberate, so no line: a step screen driving me, HALT,
	ONCE, and a cable in my RESET, whose resets are mine. My count, by identity,
	says which Seqs I am compared with: one on its own clock cable is alone in
	its. */
	bool lineOf(WalkLine& l) override {
		if (drive != DRIVE_TIME || wr.halted || wr.stop != Walk::NO_STOP || !heardFrom || inputs[RESET_INPUT].isConnected() ||
		    rtPlaces != 0.0 || rateTurnOn)
			return false;
		l = wr.line();
		l.count = heardFrom;
		l.cycle = cycle;
		return true;
	}
	const Mapping* windowOut() override { return (traits & tree::T_SHAPER) ? &winOut : nullptr; }
	/** The window my jack's note was played in, for an OUT Atn reading me:
	the step's, while I sound a keyboard's notes of a block of CVs; nothing
	while my jack is volts, a chord, triggers or a divider's lines. */
	bool playedNotes = false;
	const Mapping* playedOut() override { return playedNotes ? &winOut : nullptr; }
	// Forwarded to the member under the playhead: a quantizer's face and the
	// window chain.
	QuantFace* quantFace() override { return stoodWork ? stoodWork->quantFace() : nullptr; }
	Member* windowChild() override { return stoodWork ? stoodWork->windowChild() : nullptr; }
	Member* windowSource() override { return stoodWork ? stoodWork->windowSource() : nullptr; }
	/** A CV's word on how it plays the window I hand on, passed to every
	shaper I walk, as a Qnt passes it to what shapes it: each of them is the
	CV's window while the playhead stands there, so the CVs past me tell the
	block notes as they would tell one Atn before them. Walking routers I
	shape nothing, and the word stops here. */
	void heldMode(int by, int mode) override {
		if (traits & tree::T_SHAPER)
			for (Cartridge* c : chain)
				c->heldMode(by, mode);
	}
	void heldModeOwed() override {
		if (traits & tree::T_SHAPER)
			for (Cartridge* c : chain)
				c->heldModeOwed();
	}
	/** Read past by a window slot, a CV's, a CRV's or a Qnt's, I hand on the
	window of whatever I stand on, so every member I walk is read past me
	while the playhead stands there, and is told so as if the slot had found
	it: a walked Atn draws its right arrow by it. Only a Seq walking shapers
	is found by a window slot, so a block of routers is never told it. */
	void parsed(const tree::Left& left, int pass) override {
		Member::parsed(left, pass);
		if (pass == 0 && (told() & tree::TOLD_WINDOW))
			for (Cartridge* c : chain)
				tell(c, tree::TOLD_WINDOW);
	}
	/** What I stand in front of: the whole block, not only the member under
	the playhead. Every downward write travelling on a trait I forward walks
	this, so a Qnt to my right reaches all of what I walk without knowing I
	am here. */
	void behind(std::vector<Member*>& out) override {
		for (Cartridge* c : chain)
			out.push_back(c);
	}

	/** The count in force: the CLK jack, else the Clock, else none. */
	const Count* countIn(int64_t frame) {
		// The CLK jack is a clock of this Seq's own, a trigger a
		// step, and it is asked first. A cable in it is an act, where the
		// count the rig offers is an accident of where the modules were
		// dropped: CLK SEQ is a Clock with a sequencer beside it as readily
		// as it is a Clock driving one, and a Seq walking a block of CVs has
		// whatever Clock happens to stand to the left of that block. So the
		// cable wins, the way a cable in PHASE wins over a count, and with
		// the jack empty the rig drives it as ever. It is this Seq's clock
		// and nobody else's, which is why it is not a slot.
		if (inputs[CLOCK_INPUT].isConnected()) {
			// Each edge a pulse and the period the last two edges' gap
			// (`TrigCounter`): an early trigger is the writer's to land at
			// its journey's end, and a reset arms for the next trigger
			// whatever the cable is doing, as the rig's does for its tick.
			clockIn.count(ownCnt);
			ownCnt.source = this;
			return &ownCnt;
		}
		return countFrom(S_CLOCK);
	}

	/** The block as steps, the one place a step of the walk is turned into
	the member it stands on and back. Each member has its steps, one for a
	CV, one per ring for a Trg, which can be anywhere from one ring to
	sixteen, so the walk is counted member by member rather than as the
	first one's times the block; `census` works out where each member's
	steps begin once a wake, since a Trg's rings can change them. `skip` is
	the steps past the cap, left out from the left: a block of Trgs can hold
	more rings than its members number, and the cap's worth nearest me is
	walked. */
	struct Block {
		const std::vector<Cartridge*>* chain = nullptr;
		int start[MAX_SEQ_STEPS + 1] = {}; // member i's first step, and past the last member the total
		int skip = 0;

		void build(const std::vector<Cartridge*>& members, bool across) {
			chain = &members;
			start[0] = 0;
			for (size_t i = 0; i < members.size(); i++)
				start[i + 1] = start[i] + members[i]->stepsOf(across);
		}
		int size() const { return (int)chain->size(); }
		/** Every step there is to walk, before the cap. */
		int total() const { return start[size()]; }
		/** The member step `step` falls in, by its index, or `size()` past
		the last. */
		int indexAt(int step) const {
			const int at = step + skip;
			int i = 0;
			while (i < size() && at >= start[i + 1])
				i++;
			return i;
		}
		/** The member step `step` stands on, and the step within it; past
		the block, and standing alone, none, and the step itself. */
		void memberAt(int step, Cartridge*& member, int& within) const {
			const int i = indexAt(step);
			member = i < size() ? (*chain)[i] : nullptr;
			within = i < size() ? step + skip - start[i] : step;
		}
		/** And back: the step that member and the step within it stand on
		now, the last of the member's if it has grown shorter, or -1 where
		it is no longer among the `count` walked. Pointers compared, never
		followed: one may be gone. */
		int stepOf(const Cartridge* member, int within, int count) const {
			if (!member)
				return -1;
			for (int i = 0; i < size(); i++)
				if ((*chain)[i] == member) {
					const int step = start[i] - skip + std::min(within, start[i + 1] - start[i] - 1);
					return step >= 0 && step < count ? step : -1;
				}
			return -1;
		}
	};
	Block block;

	/** What one wake of the walk works out, phase by phase: the census of
	what there is to walk, the halt and the direction, the place the
	playhead lands on, and what it sounds there. `evaluate` runs them in that
	order; each phase reads what the ones before it found. */
	struct ThisWake {
		const Count* cnt = nullptr;
		bool phased = false, turning = false, alone = true, gates = false, wide = false, poly = false;
		int n = 0, chans = 0;
		bool overCV = false, halt = false; // a block that previews, and HALT holding the walk
		bool once = false;                 // ONCE: one pass and no more
		bool curves = false, overAtn = false, overQnt = false; // the block is CRVs, Atns, Qnts
		bool fade = false;    // over Atns, Fade chosen: OUT crossfades from one to the next (`fadeOn`)
		bool holding = false; // the mouse down on the step screen, driven, taken once a wake
		int holdPos = 0;      // and the step under it
		int64_t place = 0;   // the walk's own place, a whole one, before a PHASE turn
		double wAlong = 0.0; // and how far through it the writer stands, nought up to just below one
		double wEnded = 1.0; // and how far through it the step before stood as it ended (`Seen::ended`)
		double wOwnAlong = 0.0; // `wAlong` before a sync's lag is taken off: the writer's own, which a cut is timed on
		bool armed = false;  // the writer waits on a pulse: shown, silent
		bool parked = false; // its ramp stands at its journey's end, no landing held: it travels nowhere
		uint32_t began = 0;  // its serial of steps begun
		uint32_t starts = 0; // and of starts, a landing out of an arm or bound to a reset
		double liveAlong = 0.0; // armed, how far the step last sounded has played on
		bool handMoved = false; // in HALT a hand, or a reset, moved the held step this wake
		bool silent = false;    // armed: the jack plays on the step it sounded until the pulse lands the next
		int sounding = 0;       // the step the jack sounds: the step stood on, or, armed, the last one sounded
		uint32_t aims = 0;        // and of its landings re-aimed inside the window (`Seen::aims`)
		bool reaimed = false;     // the landing re-aimed this wake, no step begun: its trigger already sent
		bool open = false;        // the writer's last pulse's decision is open, so a turn moved now is its
		bool rebound = false;     // the block reshaped inside that window, its landing made again (`keepPulse`)
		bool landedFirst = false; // a reset the Clock says came after a landing on this very sample
		int64_t landedPlace = 0;  // and the place it landed on, which is then the step last sounded
		float along = 0.f;
		bool landed = false, fires = false;
		bool heard = false; // the landing strikes: under a phase, a place not struck within a step
		bool takes = false; // the walk landed it, so a REC take lands with it
		bool start = false; // the landing is a start, not the walk arriving
		bool roundCame = false; // the walk came round to step one: EOC, unless a start
		bool startOne = false;  // a start on the cycle's first place: EOC too
		int64_t tPlace = 0;     // the place the jack sounds, turned, and how far through it
		double tAlong = 0.0;
		// Oversampled under PHASE, the same at the four points of the frame
		// the jack's volts were brought up to (`PhaseUp4`), oldest first.
		bool subOk = false;
		int64_t subPlace[4] = {};
		double subAlong[4] = {};
	};

	/** What the driver sent since I last heard it: the pulses, counted from
	its serial, and the period, the rig Clock's ruling a Seq on its own cable
	too, and a stopped driver stopping the ramp where it stands. A driver
	taken on anew, a new Clock or my own cable, sends no pulse: the walk
	stands on the step it was on (`Writer::LANDED`), and the bracket says where I
	stand against that driver's other Seqs. At my birth a running driver
	sounds step one at once, and so does a trigger at my clock jack on this
	very sample. Returns the pulses heard. */
	int64_t hear(const Count* cnt, const ProcessArgs& args) {
		const int64_t f = args.frame;
		const bool own = cnt == &ownCnt;
		const bool fresh = !countHeard || lastDrive != DRIVE_TIME || cnt->source != heardFrom;
		int64_t pulses = 0;
		if (fresh) {
			if (!woke)
				pulses = (own ? clockIn.lastFrame == f : cnt->running) ? 1 : 0;
			else {
				// From one driver to another the walk is kept exactly, the
				// journey in progress and the part of a step a divider has
				// walked; put on its step afresh, a dividing Seq pulled from
				// its cable would stay a tick behind its twin for good. From
				// the step screen it stands on its step.
				if (lastDrive != DRIVE_TIME)
					wr.relocate(placeOf(basePos, count, dir), Writer::LANDED, f);
			}
			// A cable patched in starts no journey; its first trigger does,
			// else it walks steps at the rig's tempo first.
			cableWaiting = own && woke;
		}
		else
			pulses = (int64_t)(uint32_t)(cnt->pulses - heardPulses);
		if (pulses > 0) {
			cableWaiting = false;
			wr.lead = cnt->after;
			// Heard after the frame they were sent on, a Seq back from a
			// bypass or a tick a Clock sends as a beat's, they land on that
			// frame, so the ramp comes back exactly.
			if (cnt->lastFrame >= 0 && cnt->lastFrame < f) {
				wr.pulse(cnt->lastFrame, pulses);
				pulses = 0;
			}
		}
		countHeard = true;
		heardFrom = cnt->source;
		heardPulses = cnt->pulses;
		takePeriod(cnt, f);
		return pulses;
	}
	/** The period this wake hands the writer (`hear`, and a reset that binds
	to my cable's trigger inside the window). */
	void takePeriod(const Count* cnt, int64_t f) {
		const bool own = cnt == &ownCnt;
		// The period. On my own cable the tempo is the rig's when the rig has
		// a Clock, running or stopped, and mine, the cable's last gap, only
		// when it has none, the Seq deciding nothing of its own. No tempo
		// before two edges.
		const bool unknown = !(knownPeriod > 0.0);
		if (own) {
			// A rig Clock with no period to give, following a cable that has
			// sent nothing, gives none: nought is unknown, not a tempo, and
			// the cable's own measure stands (else x4 plays 1 5 1 5).
			const Count* rig = countFrom(S_CLOCK);
			knownPeriod = rig && rig->period > 0.0 ? rig->period : cnt->period;
		}
		else
			knownPeriod = cnt->period;
		// The first period heard places the ramp as the driver has it, the
		// tempo having been there all along (`Writer::setPeriod`). Not on a
		// cable: its first period is measured by the pulse that ends the
		// first journey, and placed there it flashed the step between for a
		// window before that pulse landed. A cut walk, on my cable while
		// multiplying, hears it all the same: there it is the debt of the
		// steps the first journey could not walk, paid over the next.
		wr.setPeriod(cnt->running && !cableWaiting ? knownPeriod : 0.0, f, unknown && knownPeriod > 0.0 && (!own || wr.cut));
	}
	/** Whether the rig's Clock sent a reset since I last looked, whatever
	clocks me: its RESET jack and the bracket's RESET (`Count::resets`). */
	bool rigReset() {
		const Count* rig = countFrom(S_CLOCK);
		const tree::Node* from = rig ? rig->source : nullptr;
		const bool fresh = from != resetFrom;
		const bool heard = rig && !fresh && rig->resets != heardResets;
		resetFrom = from;
		heardResets = rig ? rig->resets : 0;
		if (heard) {
			rigResetAfter = rig->resetAfter;
			rigResetBinds = rig->resetBinds;
		}
		return heard;
	}

	/** Everything this wake hands the writer, and the position read back,
	in places: `place` the walk's own whole place and `wAlong` how far
	through it, `armed` whether it waits on a pulse. Driven, the events go to the writer: the
	rate, the period and the pulses, a reset or a Sync (an arm on step one),
	a step let go after a drag (an arm there), HALT and a hand's step in it,
	and the block reshaped or the direction turned (the step kept, read
	anew). Undriven, the step the lower screen picks. */
	void walk(ThisWake& w, const ProcessArgs& args, int reshapedTo, int reshapedPre, int wasCount) {
		const Asks a = takeAsks();
		// ONCE with nothing driving the walk is dark: there is no pass.
		onceWork = w.once ? (int32_t)((cycle << 16) | std::min(cycle, (int64_t)0xffff)) : ONCE_OFF;
		if (drive == DRIVE_SCREEN) {
			standUndriven(w, a);
			return;
		}
		const int64_t f = args.frame;
		paceWriter(w, args);
		if (reshapedTo >= 0 && lastDrive == DRIVE_TIME)
			keepMember(w, f, reshapedTo, reshapedPre, wasCount);
		if (dir != lastDir && woke && lastDrive == DRIVE_TIME)
			keepTurned(f);
		armOnce(w, f);
		hearPulses(w, args, a);
		pickStep(w, f, a);
		readBack(w, f);
	}

	/** What a wake was asked for before the walk, each ask taken once. */
	struct Asks {
		bool sent = false;      // the rig Clock's reset (`rigReset`)
		bool reset = false;     // a reset of any kind, a Sync among them
		bool hardSync = false;  // my own reset under a Clock in hertz
		bool handReset = false; // the reset was a hand's on the step screen
		int jumpTo = -1;        // a step let go after a drag
	};
	Asks takeAsks() {
		Asks a;
		// A Sync: the bracket's, or the rig Clock's reset, sent whatever clocks
		// me.
		a.sent = rigReset();
		const bool sync = synced || a.sent;
		synced = false;
		a.reset = resetAsked || sync;
		// My own, the RESET jack's or the screen's, under a Clock in hertz:
		// a hard sync (`Writer::restart`). A Sync is the rig's, and arms.
		a.hardSync = resetAsked && !sync && hertzWas;
		a.handReset = resetByHand;
		resetAsked = false;
		resetByHand = false;
		a.jumpTo = jumpAsked;
		jumpAsked = -1;
		return a;
	}

	/** Undriven, the step is POS, and every move the wake makes on
	it is a write (`keepStep`): a driver taken away, a clock cable
	pulled or a Clock deleted or bypassed, leaves the Seq on the
	step it stood on, since changing the nearest driver does not
	jump a running sequence, never on whatever POS last held, a
	step picked by hand days before; a reset or a Sync puts it on
	step one, the patch performing unless it was a double click on
	the step screen; and a driven drag let go after the driver
	went lands where it was let go, a hand's jump. */
	void standUndriven(ThisWake& w, const Asks& a) {
		w.armed = false;
		if (keptStep >= 0 && params[STEP_PARAM].getValue() != keptFrom)
			keptStep = -1; // the write has landed, or a hand was first
		if (lastDrive == DRIVE_TIME && woke)
			keepStep(basePos, "keep the step", false);
		if (a.reset)
			keepStep(0, "restart", !a.handReset);
		else if (a.jumpTo >= 0)
			keepStep(a.jumpTo, "jump to step", false);
		// The step picked, an integer at its source; past the block's end it stands on the last step.
		w.place = placeOf(
		    clamp(keptStep >= 0 ? keptStep : (int)std::round(params[STEP_PARAM].getValue()), 0, std::max(count - 1, 0)),
		    count, dir);
		w.wAlong = w.wOwnAlong = 0.0;
	}

	/** The rate and the window the writer walks by this wake. */
	void paceWriter(const ThisWake& w, const ProcessArgs& args) {
		const int64_t f = args.frame;
		int n, d;
		ratioOfRate(rateWalkWork, n, d);
		// Under a Clock in hertz the window is one frame, a cable's sample:
		// an EOC sent into RESET on the same Clock binds to its pulse, where
		// with none it was a hard sync a sample behind the grid. A millisecond
		// re-decided every period change that far back, heard as exponential
		// FM. Under one following a cable, the window a Seq on that cable
		// keeps, as the Clock keeps it, so the two land a reset together.
		wr.window = w.cnt && w.cnt->hertz && !w.cnt->cabled ? 1 : windowNow.at(args.sampleRate);
		// Division is one law on every driver: at /n I take every n-th
		// trigger on my own cable as the step, as a divided Clock takes every
		// n-th beat it follows, the ramp playing the step whole over the n
		// between. And there, multiplying, a trigger cuts the journey at the
		// whole after the ramp, so at xN an early trigger starts the next
		// step rather than skipping to the journey's end.
		wr.want(n, d, f, w.cnt == &ownCnt);
	}

	/** The block reshaped under the walk, a member bypassed, deleted or
	bonded on, a Trg's rings changed: the walk stays on the member it
	stood on, its time in the step kept, and carries on from there,
	never re-phased mid-step as the count modulo a new length would.
	Where the member is gone the walk keeps its step number, wrapped
	into the new length. In HALT the step held is the walk's, and it
	keeps its member the same way. Inside the window after a pulse the
	reshape is part of that pulse's decision: the step it landed from
	is found in the new block and the landing made again from there
	(one pulse, one decision). */
	void keepMember(ThisWake& w, int64_t f, int reshapedTo, int reshapedPre, int wasCount) {
		const bool bound = wr.landing(f) && preFor == wr.openFrame && reshapedPre >= 0;
		const int64_t L = bound ? prePlace : walkPlace;
		const int64_t to = placeOf(bound ? reshapedPre : reshapedTo, count, lastDir, goingDown(L, std::max(wasCount, 1), lastDir));
		const int64_t at = L + to - wrapPlace(L, cycleOf(count, lastDir));
		if (bound)
			wr.keepPulse(at, f);
		else
			wr.relocate(at, Writer::KEEP, f);
		w.rebound = bound;
		guarding = false; // the steps struck were in the old length
	}

	/** A change of direction turns where the Seq stands: the step it is
	on is kept, the step in progress keeps its time, and the next one
	is the new direction's. Into a pendulum it goes on the way it was
	going. Inside the window after a pulse it is that pulse's: the step
	it landed from is turned, and the landing made again from there. */
	void keepTurned(int64_t f) {
		const bool bound = wr.landing(f);
		const int64_t k = bound ? wr.preStood() : wr.at(f).step;
		const int on = stepAt(k, count, lastDir);
		const int64_t to = placeOf(on, count, dir, goingDown(k, count, lastDir));
		if (bound)
			wr.keepPulse(k + to - wrapPlace(k, cycle), f);
		else
			wr.relocate(k + to - wrapPlace(k, cycle), Writer::KEEP, f);
	}

	/** ONCE taken up arms the walk on step one, dark until the next pulse
	starts its pass, and every wake in it the walk stops short of the
	cycle's end (`Writer::stopAt`); a reset or a step let go after a
	drag arms it again. */
	void armOnce(const ThisWake& w, int64_t f) {
		if (w.once && !onceWas)
			wr.arm(0, f);
		onceWas = w.once;
		wr.stopAt(w.once ? cycle : Walk::NO_STOP);
	}

	/** The pulses first, then what a hand or a reset does on the same
	sample, which binds to the landing they made. A reset on the very
	sample of my cable's trigger is a start, as a Clock's RESET with
	its trigger is (`TrigCounter::resetWith`): the gap that trigger
	closed is the one the reset fell in, so it is no tempo, and the gap
	it opens is measured; measured, a stop's two seconds would be the
	tempo and the Seq part from the rig's for a beat at x2. */
	void hearPulses(ThisWake& w, const ProcessArgs& args, const Asks& a) {
		const int64_t f = args.frame;
		const bool resetWithEdge = a.reset && w.cnt == &ownCnt && clockIn.lastFrame == f;
		if (resetWithEdge) {
			clockIn.resetWith();
			clockIn.count(ownCnt);
		}
		const uint32_t heardBefore = heardPulses;
		const int64_t pulses = hear(w.cnt, args);
		wr.halt(w.halt, f);
		// The rig Clock's reset is bound as the Clock decided, not by my own
		// window: to the pulse it made just before, which a landing I hold
		// takes, or before its next pulse, what I hold landing first. Under
		// the Clock's own count the pulses it sent before the reset go before
		// it and any sent with it after. Mine, or a Sync with no Clock in the
		// rig, binds by my window.
		int64_t before = pulses;
		if (a.sent && w.cnt != &ownCnt && w.cnt == countFrom(S_CLOCK))
			before = std::min(std::max((int64_t)(int32_t)(rigResetAfter - heardBefore), (int64_t)0), pulses);
		wr.pulse(f, before);
		if (a.reset)
			landReset(w, f, a, resetWithEdge);
		wr.pulse(f, pulses - before);
	}

	/** A reset, bound to a pulse, an arm, or a hard sync. */
	void landReset(ThisWake& w, int64_t f, const Asks& a, bool resetWithEdge) {
		// The rig Clock's reset that the Clock says comes after its last
		// tick is an arm, whatever my own window says; any other binds to
		// my last pulse inside the window (`Writer::relocate`). On my own
		// cable the gap the reset fell in is no tempo, unless it binds to
		// the trigger before it.
		const bool after = a.sent && !rigResetBinds;
		const bool binds = !w.halt && !after && wr.open(f);
		if (w.cnt == &ownCnt && !resetWithEdge && !binds)
			clockIn.resetFell();
		// Bound to my cable's trigger inside the window, the reset is
		// that trigger's, as on its own sample: the gap the trigger
		// closed is the one the reset fell in, a stop's, no tempo.
		// Measured, a DAW's stop, reset, play with the reset a sample
		// behind the clock held step one through the take's first beat at
		// x4 and jumped to five.
		else if (w.cnt == &ownCnt && !resetWithEdge && binds && clockIn.lastFrame == wr.openFrame) {
			clockIn.resetWith();
			clockIn.count(ownCnt);
			takePeriod(w.cnt, f);
		}
		// In HALT a reset is an arm that sounds: step one there, a start
		// with no EOC, and the first pulse after HALT starts its journey
		// from there without striking it again.
		resetArm = !w.halt;
		if (w.halt)
			wr.sound(0, f, true);
		else if (after) {
			// A landing my own pulse made on this very sample came before
			// the reset, as the Clock has it: the step it landed is the
			// one played on under the arm.
			const Writer::Seen landed = wr.at(f);
			w.landedFirst = wr.openFrame == f && !landed.armed;
			w.landedPlace = landed.step;
			wr.arm(0, f);
		}
		// Under a Clock in hertz, a reset no pulse of this frame took
		// lands now: step one struck on its sample and walked on from
		// there, an oscillator's hard sync. Armed until the next tick, a reset
		// at audio rate synced nothing.
		else if (a.hardSync && !binds)
			wr.restart(f);
		else
			wr.reset(f);
	}

	/** A step let go after a drag on the step screen is the step: the
	writer arms there, shown until the next pulse lands it. In HALT a
	step held on the step screen is the held step at once, and sounds,
	and the first pulse after HALT moves on from it; a member touched
	is no step. */
	void pickStep(ThisWake& w, int64_t f, const Asks& a) {
		int picked = a.jumpTo;
		if (w.halt && w.holding)
			picked = clamp(w.holdPos, 0, count - 1);
		w.handMoved = false;
		if (picked >= 0) {
			const int64_t at = placeOf(clamp(picked, 0, count - 1), count, dir);
			const Writer::Seen now = wr.at(f);
			if (w.halt) {
				// Heard as it is picked, and the first pulse after HALT moves
				// on from it, rather than striking it twice.
				if (now.step != at || a.jumpTo >= 0) {
					wr.pick(at, f);
					resetArm = false;
				}
			}
			else if (!(now.armed && now.step == at) || a.jumpTo >= 0) {
				wr.relocate(at, Writer::NOW, f);
				resetArm = false;
			}
			w.handMoved = w.halt;
		}
		w.handMoved = w.handMoved || (w.halt && a.reset);
	}

	/** The position read back from the writer, the step and how far through
	it, and what the next wake compares with. */
	void readBack(ThisWake& w, int64_t f) {
		const Writer::Seen seen = wr.at(f);
		w.armed = seen.armed;
		resetArm = resetArm && seen.armed;
		w.began = seen.began;
		w.starts = seen.starts;
		w.liveAlong = seen.liveAlong;
		w.aims = seen.aims;
		w.wEnded = seen.ended;
		// In HALT the step stands at its start, as a picked step does. The
		// place and how far through it are two numbers, an integer and a
		// fraction below one, and stay two: summed into one double, a step a
		// hair from its end reads as the next place.
		// Synced, the walk stands `lag` behind the writer (`Writer::restart`),
		// taken off as two numbers by one exact comparison, and is a turned
		// walk: its steps land where it crosses them, not where the writer
		// does, every sample.
		w.place = seen.step;
		double along = seen.along;
		w.wOwnAlong = w.halt ? 0.0 : along;
		if (wr.lag != 0.0) {
			if (along < wr.lag) {
				w.place--;
				along = std::min(along - wr.lag + 1.0, Walk::BELOW_ONE);
			}
			else
				along -= wr.lag;
			if (!w.halt) {
				w.turning = true;
				fineWork = true;
			}
		}
		w.wAlong = w.halt ? 0.0 : along;
		onceWork = !w.once ? ONCE_OFF
		           : (int32_t)(((seen.armed || wr.spent ? cycle : std::min(std::max(w.place, (int64_t)0), cycle - 1)) << 16) |
		                       std::min(cycle, (int64_t)0xffff));
		w.parked = !wr.ramping(f);
		w.open = wr.landing(f) && !w.halt;
		// The step the open pulse landed from, and its member, for a block
		// reshaped inside its window (`keepPulse`).
		if (wr.open(f)) {
			preFor = wr.openFrame;
			prePlace = wr.preStood();
			block.memberAt(stepAt(prePlace, count, dir), preMember, preWithin);
		}
		else
			preFor = -1;
	}

	/** Under a Clock in hertz (`Count::hertz`) the jack is an oscillator,
	and two options keep what it folds back from past the Nyquist down; with
	no Clock over me they are a hand's choice, the slew's alternative
	(`bandHere`). With BLEP on, a step's edge is a band-limited step put where
	it fell between two samples (`Blep`); where it fell is where the walk
	crossed the whole, which a pulse lands part way through its frame
	(`Count::after`). Its height is what the step sounds as it begins less
	what the step before sounded as it ended: on a CRV, its curve read at
	nought and the last one's at one (`MemberFace::voltsAlong`), so a curve that
	meets the next one with no jump gets no edge at all, and a bend's steep
	end meets its drop exactly; on a point, the jump less the slope the
	channel was on. With oversampling on, the frame is worked out as four
	points between the last frame and this one, the walk moving straight
	between the two, a CRV read at each point's place and a point holding its
	value until the edge, the edges band-limited at that rate, and the four
	brought down by a lowpass (`Decimate4`), which is what a curve bending
	faster than a sample needs; a value held comes out exactly itself. A
	corner, a CRV's change of slope, is left to the oversampling. Anything
	else, and every channel when the count of them changes, is the naive
	value. `curve` is the CRV the jack sounds, if that is all it sounds, read
	backward when `flip`. `left`, `leftN` channels, is the step the jack has
	just left still sounding at this frame, where it runs on (an Atn the
	playhead has switched away from, `leftBehind`): the jump is the new
	step less it at the same frame, measured, where a value that runs on
	unseen is guessed from its last slope, which a signal bending within a
	sample, audio far above a kilohertz, misses by more than its own swing. */
	void bandLimit(float* v, int n, const ThisWake& w, int64_t frame, const Cartridge* curve = nullptr,
	               bool flip = false, const float* left = nullptr, int leftN = 0) {
		const bool over = overWork;
		if (!bandHere() || n == 0) {
			if (blepN > 0)
				for (int c = 0; c < blepN; c++) {
					bleps[c].clear();
					bleps4[c].clear();
				}
			blepN = 0;
			blepTail = 0;
			blepCurve = nullptr;
			blepSubOk = false;
			return;
		}
		// Written again on a frame already written: that frame's correction.
		if (n == blepN && frame == blepFrame) {
			for (int c = 0; c < n; c++)
				v[c] += blepOut[c];
			return;
		}
		// A wake after frames I slept through with the jack settled (`blepTail`
		// nought: no correction left and the decimator holding the last value
		// as itself, which is why I could sleep) follows those frames as a
		// wake every sample would have: they held that value, so the state is
		// already theirs, the slope nought. A HALTed Seq passing its IN
		// through sleeps until the IN moves, and its edge was handled as a
		// first frame, the decimator filled with the new value and the frame
		// drawn from the old: the edge out unfiltered and a ring of a quarter
		// of it after.
		const bool resumes = n == blepN && over == blepOver && frame > blepFrame + 1 && blepTail == 0;
		const bool follows = n == blepN && over == blepOver && (frame == blepFrame + 1 || resumes);
		if (resumes)
			for (int c = 0; c < n; c++)
				blepSlope[c] = 0.f;
		// Anything else has no frames before it to follow (birth, a parse, the
		// channels or Oversampling changed): it starts as the value held for
		// ever, every point of the frame the value itself, so the jack is
		// exactly that value and nothing rings.
		if (!follows)
			for (int c = 0; c < PORT_MAX_CHANNELS; c++) {
				bleps[c].clear();
				bleps4[c].clear();
				if (c < n) {
					down[c].fill(v[c]);
					blepWas[c] = v[c];
					blepSlope[c] = 0.f;
				}
			}
		// Under a turn, PHASE's or the Clock's, the edge is where the turned
		// place crosses a whole, which may be backwards, and not where the
		// writer does.
		const bool turned = w.turning && w.starts == blepStarts;
		const int64_t dt = w.tPlace - blepPlace;
		const int32_t d = turned ? (int32_t)std::min<int64_t>(std::max<int64_t>(dt, -2), 2) : (int32_t)(w.began - blepBegan);
		// A frame that crossed two wholes or more has no one edge to put
		// anywhere: what the jack passed between the two samples is several
		// steps, not one jump, and a point's jump less the slope it was on,
		// carried on to the end of the frame, is neither. It is left as it
		// is, straight between the two samples where oversampled, so the jack
		// never leaves the volts its steps sound (otherwise a HALTed Seq under a
		// fast PHASE turn rings out to seventy volts with BLEP and 4X on), unless it is all curves,
		// which oversampled are read where the turn passed them (`across`).
		const bool several = d > 1 || d < -1;
		// A step a change of rate ended early under a turn: the writer jumped
		// what the cut skipped, so the turned place did too, over a turned
		// whole or inside the step it stood on; an edge either way. Timed on
		// the writer's own walk, which the turn rides on, as a plain cut is.
		const bool cutTurned = turned && follows && w.wEnded < 1.0 && w.began - blepBegan == 1;
		const double jump = cutTurned ? 1.0 - w.wEnded : 0.0;
		const double ownSpan = w.wOwnAlong + w.wEnded - blepOwnAlong;
		const bool edge = follows && !several && (d != 0 || w.starts != blepStarts || cutTurned);
		// One whole crossed: the fraction of the frame since, from where
		// the walk stood the frame before and stands now. Otherwise, a
		// jump, it falls on the frame. `tc` is
		// when, nought the last frame and one this.
		// Under a turn the walk is the turned place, which may go back, `dw`
		// below nought: a whole crossed backwards falls where the place left
		// the one it was on.
		// The step left ends at one, but where a change of rate ended it
		// early (`Walk::land`), where it was ended: the edge is the new step
		// less the old one read there, and the walk goes from where it stood
		// to there and on into the new step.
		const double aNow = turned ? w.tAlong : w.wAlong, aWas = turned ? blepTAlong : blepAlong;
		const double aEnd = turned ? 1.0 : w.wEnded;
		const double span = aNow + aEnd - aWas;
		double tc = edge && d == 1 && span > 0.0 ? 1.0 - std::min(std::max(aNow / span, 0.0), 1.0) : 1.0;
		if (cutTurned)
			tc = ownSpan > 0.0 ? 1.0 - std::min(std::max(w.wOwnAlong / ownSpan, 0.0), 1.0) : 1.0;
		else if (turned && edge && d == -1) {
			const double back = aWas + 1.0 - aNow;
			tc = back > 0.0 ? std::min(std::max(aWas / back, 0.0), 1.0) : 1.0;
		}
		// The walk between the two frames, where it went straight: along its
		// step, or over one whole into the next, `dw` steps.
		// Cut under a turn, less the jump, which is the edge and no walking.
		const double dw = cutTurned ? aNow + d - aWas - jump : d == 0 ? aNow - aWas : d == 1 ? span : aNow - 1.0 - aWas;
		const bool straight = follows && w.starts == blepStarts &&
		                      (turned ? d >= -1 && d <= 1 && dw != 0.0 : (d == 0 || d == 1) && dw > 0.0);
		const Cartridge* now = straight ? curve : nullptr;
		// The curve the last frame sounded, as long as it is still in the
		// block: a parse deals the block again and leaves a member standing
		// where it stood, so it carries no news of the curve. Read off the
		// curve under the walk now instead, an edge on the frame of a reparse
		// would take the new step's curve for the old one's end, a wrap of a
		// CRV at audio rate twenty volts off (CLK CRV CRV SEQ, a reparse at
		// audio rate). A member gone, the block's new curve stands in.
		const Cartridge* was = straight && d != 0 ? blepCurve : now;
		if (was && was != now && std::find(chain.begin(), chain.end(), was) == chain.end())
			was = now;
		auto read = [](const Cartridge* m, bool back, double x, float& o) {
			const float a = (float)std::min(std::max(x, 0.0), 1.0);
			return m && m->voltsAlong(back ? 1.f - a : a, o);
		};
		const bool wasFlip = d != 0 && (turned || d == 1) ? blepFlip : flip;
		// Oversampled, a turn that crossed several wholes in one frame is
		// still read along its curves, if every place it passed is a CRV's:
		// each of the three points between the samples stands on the place the
		// turn reached there, moving straight from the last frame's turned
		// place to this one's, and reads the member under it, forward or back
		// as the walk goes there. Under PHASE's wrap a place may come round a
		// whole cycle in one frame, the same place, so the way taken is the
		// shorter way round. Any point on something with no curve, and the
		// frame is the straight line between the samples. The members are
		// looked up in the block as it stands, never the last frame's, so a
		// parse between changes nothing.
		float across[3];
		bool acrossOk = over && several && turned && follows && curve && count > 0 && !w.subOk; // PHASE's own points do it better, below
		if (acrossOk) {
			Cartridge* here = nullptr;
			int within = 0;
			block.memberAt(stepAt(w.tPlace, count, dir), here, within);
			acrossOk = here == curve;
		}
		if (acrossOk) {
			const int64_t cycle = cycleOf(count, dir);
			const int64_t whole = shorterWhole(blepPlace, blepTAlong, w.tPlace, w.tAlong, cycle);
			const double moved = (double)whole + w.tAlong - blepTAlong;
			for (int j = 0; j < 3 && acrossOk; j++) {
				const double at = blepTAlong + (j + 1) / 4.0 * moved, below = std::floor(at);
				const int64_t k = blepPlace + (int64_t)below;
				Cartridge* m = nullptr;
				int within = 0;
				block.memberAt(stepAt(k, count, dir), m, within);
				acrossOk = read(m, alongBack(k, count, dir), at - below, across[j]);
			}
		}
		// Oversampled under PHASE, the four points of the frame stand where
		// the jack's volts brought up to four times the rate put them
		// (`ThisWake::subPlace`), each reading the CRV under it, a point
		// standing on the frame's own place sounding what the jack sounds; a
		// step from one member to the next between two points, a whole
		// crossed, is a band-limited edge at four times the rate where the
		// place crossed it, its height the new curve at its edge less the old
		// at its own. Any point on something with no curve, and the frame is
		// worked out as above.
		float subV[4], subH[4] = {}, subP[4] = {};
		bool subs = over && w.subOk && curve && follows && w.starts == blepStarts && count > 0;
		if (subs) {
			Cartridge* here = nullptr;
			int within = 0;
			block.memberAt(stepAt(w.tPlace, count, dir), here, within);
			subs = here == curve;
		}
		const int64_t subCycle = count > 0 ? cycleOf(count, dir) : 1;
		for (int j = 0; j < 4 && subs; j++) {
			const int64_t k = w.subPlace[j];
			const double a = w.subAlong[j];
			Cartridge* m = nullptr;
			int within = 0;
			block.memberAt(stepAt(k, count, dir), m, within);
			const bool back = alongBack(k, count, dir);
			if (k == w.tPlace && a == w.tAlong)
				subV[j] = v[0];
			else if (!read(m, back, a, subV[j])) {
				subs = false;
				break;
			}
			if (!blepWork || (j == 0 && !blepSubOk))
				continue;
			const int64_t pk = j == 0 ? blepSubPlace : w.subPlace[j - 1];
			const double pa = j == 0 ? blepSubAlong : w.subAlong[j - 1];
			const int64_t dk = shorterWhole(pk, pa, k, a, subCycle);
			if (dk != 1 && dk != -1)
				continue;
			const double span = dk == 1 ? 1.0 - pa + a : pa + 1.0 - a;
			if (!(span > 0.0))
				continue;
			const double since = (dk == 1 ? a : 1.0 - a) / span; // of the step between the two points
			Cartridge* was = nullptr;
			block.memberAt(stepAt(pk, count, dir), was, within);
			float to, from;
			if (read(m, back, dk == 1 ? 0.0 : 1.0, to) && read(was, alongBack(pk, count, dir), dk == 1 ? 1.0 : 0.0, from)) {
				subH[j] = to - from;
				subP[j] = -(float)std::min(std::max(since, 0.0), 1.0);
			}
		}
		int tail = 0;
		for (int c = 0; c < n; c++) {
			const float naive = v[c];
			const float slope = blepSlope[c];
			// The step sounding at `t` of the frame, and the one before it
			// carried on to `t`: a CRV read along its curve, a point its
			// value.
			auto newAt = [&](double t, float& o) {
				if (c == 0 && read(now, flip, aNow - (1.0 - t) * dw, o))
					return true;
				o = naive;
				return false;
			};
			auto oldAt = [&](double t, float& o) {
				if (c == 0 && read(was, wasFlip, aWas + t * dw, o))
					return true;
				o = blepWas[c] + (float)t * slope;
				return false;
			};
			float h = 0.f;
			if (edge) {
				float a, b;
				const bool ca = newAt(tc, a), cb = oldAt(tc, b);
				// A point's jump: the step left, measured at this frame where
				// it runs on, else carried on along its slope.
				h = ca || cb ? a - b : naive - (c < leftN ? left[c] : blepWas[c] + slope);
			}
			float out;
			if (over) {
				float x[4];
				for (int j = 0; j < 4; j++) {
					if (subs) {
						if (subH[j] != 0.f)
							bleps4[c].insert(subP[j], subH[j]);
						x[j] = subV[j] + (blepWork ? bleps4[c].process() : 0.f);
						continue;
					}
					const double t = (j + 1) / 4.0;
					float o;
					if (j == 3)
						o = naive;
					else if (acrossOk)
						o = across[j];
					else if (edge ? t < tc : false)
						oldAt(t, o);
					else if (!newAt(t, o) && !edge)
						o = blepWas[c] + (float)t * (naive - blepWas[c]); // a point moving: straight between
					if (edge && blepWork && h != 0.f && t >= tc && (j == 0 || (j) / 4.0 < tc))
						bleps4[c].insert(-(float)((t - tc) * 4.0), h);
					x[j] = o + (blepWork ? bleps4[c].process() : 0.f);
				}
				out = down[c].process(x);
				tail = std::max(tail, std::max(down[c].settling(), (bleps4[c].left + 3) / 4));
			}
			else {
				if (edge && blepWork && h != 0.f)
					bleps[c].insert((float)(tc - 1.0), h);
				const float corr = bleps[c].process();
				out = naive + corr;
				// The last correction is never quite nought, the step's table cut
				// at its length, so one wake more writes the naive value itself.
				tail = std::max(tail, bleps[c].left > 0 ? bleps[c].left : corr != 0.f ? 1 : 0);
			}
			if (!edge)
				blepSlope[c] = follows ? naive - blepWas[c] : 0.f;
			blepWas[c] = naive;
			blepOut[c] = out - naive;
			v[c] = out;
		}
		blepN = n;
		blepFrame = frame;
		blepBegan = w.began;
		blepStarts = w.starts;
		blepAlong = w.wAlong;
		blepOwnAlong = w.wOwnAlong;
		// The place the edges were read from, turned or not, so a turn
		// beginning reads its first frame from where the walk stood: a wake
		// for the ramp alone (`fineAgain`) leaves the turned place as the
		// last whole wake's.
		blepPlace = turned ? w.tPlace : w.place;
		blepTAlong = turned ? w.tAlong : w.wAlong;
		blepTail = tail;
		blepSubOk = subs;
		blepSubPlace = w.subPlace[3];
		blepSubAlong = w.subAlong[3];
		blepCurve = curve;
		blepFlip = flip;
		blepOver = over;
	}
	/** How far along a glide stands at `x` of its length: a raised cosine,
	nought to one with no slope at either end, so the jack leaves the old value
	and lands on the new one without a corner, which a straight ramp has at
	both and an RC's curve at its start, each a click of its own on a fast
	slew. */
	static float slewShape(float x) { return 0.5f - 0.5f * std::cos(float(M_PI) * x); }
	/** The volts of a sound on the jack, a channel each; how many. */
	static int jackVolts(const Sound& o, float* v) {
		const int n = std::max(o.voices, 1);
		// A divider's line as the line itself, not twelve times it over twelve.
		v[0] = sane(o.band != NO_BAND && o.semis == 12.f * o.volts ? o.volts : o.semis / 12.f);
		for (int c = 1; c < n; c++)
			v[c] = sane(o.voice[c]);
		return n;
	}
	/** What the slew does where I stand this wake (`SlewMode`): over Atns
	in Step a crossfade, whatever they carry, a routed signal, an Atn's own
	or an offset alone (the interpolation is its own feature wherever the crossfade is off, a couple
	of milliseconds taking the click off a switched signal); a glide over
	CVs and CRVs, and standing alone, an arpeggiator gliding from note to
	note; nothing anywhere else. Not in Fade, which has no jump to take off,
	nor over Trgs, whose jack is gates, nor over a Qnt, whose jack is a
	chord or, with a cable, notes that change mid-step, so a slew would ease
	only the step's edge and not the quantizer's. */
	SlewMode slewModeOf(const ThisWake& w) {
		if (w.gates || w.overQnt || w.fade)
			return SLEW_NONE;
		if (w.alone)
			return SLEW_GLIDE;
		if (chain.empty())
			return SLEW_NONE;
		if (w.overAtn)
			return SLEW_FADE;
		return (chain[0]->traits & (tree::T_CV | tree::T_CRV)) ? SLEW_GLIDE : SLEW_NONE;
	}
	/** The Atn the jack sounded the frame before, where the playhead has
	left it this frame and it still stands in my block, or none: what a
	switch leaves behind, still running (`soundOf`), which the crossfade
	fades from and BLEP measures the jump from. */
	Cartridge* leftBehind() const {
		Cartridge* was = const_cast<Cartridge*>(slewMember);
		return was && was != stoodWork && std::find(chain.begin(), chain.end(), was) != chain.end() ? was : nullptr;
	}
	/** What Atn `m` sounds as a step, as though the playhead stood on it:
	ROUTE's signal handed to it with its own cable where ROUTE carries one,
	else its own, through the same act the step it was made with. What a
	crossfade fades from, still running. Returns how many channels. */
	int soundOf(Cartridge* m, float* v) {
		Signal sig;
		if (routeWork)
			sig.setVolts(in.get() + (m->cv.live() ? m->cv.get() : 0.f), color, Mapping());
		Sound o;
		m->sound(0, sig, color, o);
		if (routeWork)
			o.semis = 12.f * sane(o.semis / 12.f);
		soundVoices(m, 0, sig, routeWork, (m->traits & tree::T_STAGE) != 0, color, o, false);
		return jackVolts(o, v);
	}
	/** The jack's volts in `v`, `n` channels, slewed (`SLEW_PARAM`). A new
	step, the playhead on another step or a held fader chosen, sets a glide
	going from what the jack read last: the error between that and where it
	goes is eased away over the slew's time on the raised cosine, so a value
	still moving under it, a CRV's curve or a cable's voltage, is followed
	all the way and arrived on, not chased. A crossfade does the same with
	the last step's signal, still running, under the error in place of the
	new step's value, so the two are weighed against each other, one less
	the cosine and the cosine, summing to one. The glide counts in samples,
	the step's own sample the first, against a length that is a time
	(`slewLength`), and lands on the first sample at or past it with
	nothing to snap; a channel the jack did not hold arrives on its value.
	Standing alone the step is ARP's channel sounding, and a new one glides
	as a new member's step does. */
	void slew(float* v, int n, int step, int64_t frame, float sampleRate) {
		const bool on = slewOnWork && slewModeWork != SLEW_NONE;
		const int64_t gone = slewFrame < 0 ? 0 : std::max<int64_t>(frame - slewFrame, 0);
		slewFrame = frame;
		const bool fresh = stoodWork != slewMember || step != slewStep;
		Cartridge* was = slewModeWork == SLEW_FADE ? leftBehind() : nullptr;
		slewMember = stoodWork;
		slewStep = step;
		if (!on) {
			slewAt = slewLeft = 0;
			slewLen = 0.0;
			fadeFrom = nullptr;
		}
		else if (fresh && slewN > 0 && (slewMember || aloneWork)) {
			const bool fade = was != nullptr;
			float base[PORT_MAX_CHANNELS];
			const int bn = fade ? soundOf(was, base) : 0;
			for (int c = bn; c < n; c++)
				base[c] = v[c];
			bool any = fade;
			for (int c = 0; c < n; c++) {
				slewErr[c] = c < slewN ? slewOut[c] - base[c] : 0.f;
				any = any || slewErr[c] != 0.f;
			}
			fadeFrom = fade ? was : nullptr;
			slewErrN = n;
			slewLen = any ? slewLength(slewPlace(), sampleRate) : 0.0;
			slewAt = any ? 1 : 0;
		}
		else if (slewAt < slewLen)
			// Past the length is landed: the count stops at the first whole
			// sample at or beyond it.
			slewAt = (int)std::min<int64_t>(slewAt + gone, (int64_t)std::ceil(slewLen));
		if (on && slewAt < slewLen) {
			// The faded-from Atn gone from the block mid-fade: the error
			// alone eases out onto the new step.
			if (fadeFrom && std::find(chain.begin(), chain.end(), fadeFrom) == chain.end())
				fadeFrom = nullptr;
			float base[PORT_MAX_CHANNELS];
			const int bn = fadeFrom ? soundOf(fadeFrom, base) : 0;
			const float keep = 1.f - slewShape((float)((double)slewAt / slewLen));
			for (int c = 0; c < n; c++) {
				const float from = (c < bn ? base[c] : v[c]) + (c < slewErrN ? slewErr[c] : 0.f);
				v[c] += keep * (from - v[c]);
			}
			// Where the glide stops being a time and becomes a count: the
			// samples still owed, to the first at or past its length.
			slewLeft = (int)std::ceil(slewLen) - slewAt;
		}
		else {
			slewAt = slewLeft = 0;
			slewLen = 0.0;
			fadeFrom = nullptr;
		}
		for (int c = 0; c < n; c++)
			slewOut[c] = v[c];
		slewN = n;
	}
	/** No slew where the jack is not volts: gates, or a Qnt's chord; the
	volts after them arrive on their value. */
	void slewStops() {
		slewN = slewAt = slewLeft = 0;
		slewLen = 0.0;
		slewMember = nullptr;
		slewStep = -1;
		fadeFrom = nullptr;
	}
	/** How long a trigger fired on this sample stands, on every jack I fire
	one, mine, my gates' and a Trg's rings': a millisecond, or half a step
	where steps come faster than two (`triggerFrames`), a step's length being
	the shorter of the one the walk expects, its period over its journey's
	length, and
	the samples since the last step fired. A landing re-aimed inside its
	window (`reaimed`) is that step's, so its gap is from the step before:
	measured from the step it re-aims, a reset arriving a sample after the
	pulse cut the trigger to one sample, which no light caught (a Trg's rings cabled
	into two Seqs' RESET). Worked out once a sample. */
	int64_t fireFrames(const ProcessArgs& args, bool reaimed) {
		if (fireLenAt != args.frame || fireLenReaimed != reaimed) {
			fireLenAt = args.frame;
			fireLenReaimed = reaimed;
			int64_t gap = INT64_MAX;
			if (wr.period > 0.0 && wr.num > 0)
				gap = (int64_t)std::floor(wr.period * wr.den / wr.length());
			const int64_t from = reaimed ? prevFireAt : lastFireAt;
			if (from >= 0 && args.frame > from)
				gap = std::min(gap, args.frame - from);
			fireLen = triggerFrames(pulseFrames(args.sampleRate), gap);
		}
		return fireLen;
	}
	/** The jack: the gates of the step over a block of Trgs (`writeGates`),
	a chord over a Qnt with nothing to snap, or the step's volts
	(`writeVolts`). */
	void writeJack(const Sound& snd, bool landed, const ThisWake& w, const ProcessArgs& args) {
		if (snd.type == Sound::TRIG)
			writeGates(landed, w, args);
		else if (snd.type == Sound::CHORD) {
			jackWidth = writeChord(outputs[CV_OUTPUT], snd.chord);
			slewStops();
			bandLimit(nullptr, 0, w, args.frame);
			outPolyWork = true;
		}
		else
			writeVolts(snd, w, args);
	}
	/** Which gates fire on this sample, and until when: a landing's rings.
	A landing re-aimed inside its window fires the rings it makes due that
	its pulse had not fired, and nothing it had: a hit already sent is not
	taken back, nor sent twice (one pulse, one decision). */
	void fireGates(bool landed, const ThisWake& w, const ProcessArgs& args) {
		int m = landed ? gateMask : 0;
		if (w.reaimed && gatesBegan == w.began)
			m &= ~gatesFired;
		if (m == 0)
			return;
		const bool high = args.frame < firedUntil;
		firedMask = w.reaimed && high ? firedMask | m : m;
		firedUntil = args.frame + fireFrames(args, w.reaimed);
		gatesFired = gatesBegan == w.began ? gatesFired | m : m;
		gatesBegan = w.began;
	}
	/** The gates on the jack, a channel a ring across one Trg, else one:
	high while a fired gate stands. No slew and no edge to band-limit. */
	void writeGates(bool landed, const ThisWake& w, const ProcessArgs& args) {
		fireGates(landed, w, args);
		const bool high = args.frame < firedUntil;
		const int channels = gateWide ? std::max(gateRings, 1) : 1;
		outputs[CV_OUTPUT].setChannels(channels);
		jackWidth = channels;
		for (int c = 0; c < channels; c++)
			outputs[CV_OUTPUT].setVoltage(high && (firedMask >> c) & 1 ? TRIG_VOLTS : 0.f, c);
		slewStops();
		bandLimit(nullptr, 0, w, args.frame);
		outPolyWork = gateWide;
	}
	/** Fade: the next Atn weighed into the `n` volts of `v`, a channel
	against its channel, a mono one standing for every channel as Rack's
	mono does and a channel one lacks nought, each weight exact
	(`soundStep`). At a step's start the weight is nought, one times this
	Atn and nought times the next, so the Atn is exactly itself, only as
	wide as the wider of the two. The width after. */
	int fadeInto(float* v, int n) {
		float u[PORT_MAX_CHANNELS];
		const int m = soundOf(fadeTo, u);
		const int both = std::max(n, m);
		const float a = fadeBy, b = 1.f - fadeBy;
		// From the top down, so a mono channel nought is read before
		// it is written.
		for (int c = both - 1; c >= 0; c--) {
			const float x = c < n ? v[c] : n == 1 ? v[0] : 0.f;
			const float y = c < m ? u[c] : m == 1 ? u[0] : 0.f;
			v[c] = b * x + a * y;
		}
		return both;
	}
	/** The edges of the volts band-limited (`bandLimit`), with what a CRV's
	curve sounds between two samples where the jack is that curve and
	nothing more. HALT too: a HALTed Seq under a turn is a waveshaper, the
	turn walking the curves, and a step stood on with no turn moves nowhere
	along it, so `bandLimit` reads no curve there anyway (HALT was left out
	with PHASE before a turn's edges were read where the turned place
	crosses). */
	void limitVolts(float* v, int n, const ThisWake& w, const ProcessArgs& args) {
		const bool alongs = n == 1 && !routeWork && !w.silent && !w.holding;
		const bool back = alongBack(w.turning ? w.tPlace : w.place, count, dir);
		// A fade has no edge to band-limit: its mix arrives on the next
		// Atn as that Atn's step begins.
		if (w.fade) {
			bandLimit(nullptr, 0, w, args.frame);
			return;
		}
		// A switch between Atns: the one left behind, still running.
		float u[PORT_MAX_CHANNELS];
		Cartridge* was = w.overAtn ? leftBehind() : nullptr;
		const int un = was ? soundOf(was, u) : 0;
		bandLimit(v, n, w, args.frame, alongs ? stoodWork : nullptr, back, u, un);
	}
	/** The step's volts on the jack: a chord the member carries on a cable,
	a voice a channel, faded, band-limited and slewed. */
	void writeVolts(const Sound& snd, const ThisWake& w, const ProcessArgs& args) {
		float v[PORT_MAX_CHANNELS];
		int n = jackVolts(snd, v);
		if (fadeTo)
			n = fadeInto(v, n);
		outputs[CV_OUTPUT].setChannels(n);
		jackWidth = n;
		limitVolts(v, n, w, args);
		slew(v, n, w.sounding, args.frame, args.sampleRate);
		for (int c = 0; c < n; c++)
			outputs[CV_OUTPUT].setVoltage(v[c], c);
		outPolyWork = jackWidth > 1;
	}

	/** The census: whose count I take, what there is to walk and how many
	steps of it. */
	ThisWake census(const ProcessArgs& args) {
		ThisWake w;
		// The Clock holding me hears whether I take its count or my own cable
		// has it (`countTaken`). A cable in the PHASE jack turns the sequence
		// under the playhead, ten volts a whole turn, added to the walk
		// (`place`).
		const bool phased = inputs[PHASE_INPUT].isConnected();
		// And whether a reader of mine plays the writer's ramp, so the wake
		// wants making every sample the ramp moves: a block of CRVs played
		// through each step, or a cable in PHASE, whose turn is read every
		// sample and would rock the playhead between two steps against a
		// count in 32-sample stairs; not in HALT, where the ramp stands.
		const bool overCV = previews();
		// HALT holds the walk and ONCE stops it after one pass over every
		// block and standing alone, over Trgs too, so a
		// trigger Seq can be stopped without stopping its Clock.
		const bool halt = mode() == MODE_HALT;
		const bool once = mode() == MODE_ONCE;
		// What drives me: my cable, or what my Clock sends. The writer ramps
		// for itself, so it is wanted when the Clock sends something and no
		// more often.
		const Count* cnt = countIn(args.frame);
		const bool hz = cnt && cnt->hertz;
		if (hz != hertzWas) {
			hertzWas = hz;
			hertzNow.store(hz, std::memory_order_relaxed);
		}
		// The choice in force where I stand (`choiceIn`). BLEP or
		// oversampling on wants every sample too: the correction after an
		// edge, and the way back down, are a sample's at a time (`bandLimit`).
		const Choice choice = choiceIn(hz);
		slewOnWork = params[choice.slewOn].getValue() > 0.5f;
		blepWork = params[choice.blep].getValue() > 0.5f;
		// And a Clock in hertz whose PHASE has turned the cycle, whose turn
		// moves every sample as my own PHASE's does (`clockTurn`).
		const bool clockTurning =
		    cnt && cnt->hertz &&
		    (cnt->phased || cnt->turnWhole != 0 || cnt->turnFrac != 0.0 || (ctFrom && ctPlaces != 0.0));
		// And a free rate's jack, which turns the walk every sample, its
		// volts heard at the sample (`rateTurn`); while the turn it left
		// stands, the place is turned, as under a Clock's PHASE.
		// A free rate's screen is the same: its change is heard on the
		// sample, the writer asked for it only where it takes it smoothly
		// and the turn carrying the walk at the screen's rate until it has.
		const RateIn& ri = rateCore();
		const bool rateFree = ri.isFree();
		const bool rateFm = ri.patched && rateFree;
		if (!rateFree)
			rtAsk = ri.rate();
		else if (rtAsk != ri.set && wr.takesRateSmoothly(args.frame))
			rtAsk = ri.set;
		rateWalkWork = rtAsk;
		rateSetWork = ri.set;
		rateVoltsWork = rateFm ? clamp(ri.cv, -10.f, 10.f) : 0.f;
		int sn, sd;
		ratioOfRate(ri.set, sn, sd);
		rtWNum = wr.num;
		rtWDen = wr.den;
		rtBehind = rateFree && !wr.armed && (wr.num != sn || wr.den != sd);
		rateTurnOn = rateFree && cnt && cnt->running && cnt->period > 0.0 && (rateVoltsWork != 0.f || rtBehind) && !halt &&
		             !(once && wr.spent && rtAtEnd);
		const bool rateTurning = rateTurnOn || rtPlaces != 0.0;
		frameWork = args.frame;
		const uint32_t kind = walked();
		// Oversampling where it means something (`overMeans`).
		overWork = params[choice.over].getValue() > 0.5f && overMeans(kind);
		// And Fade over Atns, whose mix moves with the ramp every sample.
		const bool fade = overRouters() && fadeOn();
		fineWork = ((kind & tree::T_CRV) || phased || clockTurning || rateTurning || fade || blepWork || overWork) && !halt;
		// On my own cable the Clock is still mine while it lends me its tempo
		// and rate (`takePeriod`), and wears its colour so it does not look
		// idle when it sets how fast a CRV moves.
		const Count* lender = inputs[CLOCK_INPUT].isConnected() ? countFrom(S_CLOCK) : nullptr;
		const int took = !inputs[CLOCK_INPUT].isConnected() || (lender && lender->period > 0.0) ? 1 : 0;
		if (took != tookTold) {
			tookTold = took;
			if (Member* c = at(S_CLOCK))
				c->countTaken(this, took != 0);
		}
		drive = cnt ? DRIVE_TIME : DRIVE_SCREEN;
		const bool alone = chain.empty();
		const bool gates = !alone && (chain[0]->traits & tree::T_TRG);
		const bool wide = gates && across();
		if (wide && acrossSeen == 0)
			conform();
		acrossSeen = gates ? (int)wide : -1;
		block.build(chain, across());
		const int walkable = block.total();
		// The whole of what there is to walk: a Seq walks its block entire
		// and a shorter sequence is fewer modules rather than a number. No
		// more than the cap, and the cap's worth nearest me: a block of Trgs
		// can hold more rings than its members number, so the steps past the
		// cap are skipped from the left, as the members past it were left
		// unclaimed. Nothing to walk is an arpeggiator however it is clocked,
		// by the rig's count or a cable in the clock jack alike: the channels
		// of ARP are the steps, one with a mono cable or none.
		// With ARP empty, the Poly that ended my block's search is the
		// cable: SEQ POLY SEQ arpeggiates the chord the Poly bundles, its
		// channels my steps, and every Seq to its right with nothing nearer to walk does the
		// same, POLY SEQ SEQ two playheads over it. A cable in ARP is an act and wins.
		arpV = nullptr;
		arpPoly = nullptr;
		Member* left = at(S_ARP);
		int chans = inputs[IN_INPUT].isConnected() ? inputs[IN_INPUT].getChannels() : 0;
		if (alone && !inputs[IN_INPUT].isConnected() && left)
			chans = std::max(left->jackOut(arpV), 0);
		if (arpV)
			arpPoly = left;
		polyArpWork = arpV != nullptr;
		const int n = alone ? std::max(chans, 1) : std::min(std::max(walkable, 1), MAX_SEQ_STEPS);
		block.skip = alone ? 0 : std::max(walkable - MAX_SEQ_STEPS, 0);
		count = n;
		const bool poly = alone && chans > 1;
		aloneWork = alone;
		emptyWork = alone && chans == 0;
		rateWork = rate.rate();
		ratePatchedWork = inputs[RATE_INPUT].isConnected();
		rateCvWork = clamp(inputs[RATE_INPUT].getVoltage() / 10.f, -1.f, 1.f);
		w.cnt = cnt;
		w.holding = relaxed(holding);
		w.holdPos = relaxed(holdPos);
		w.phased = phased;
		w.turning = phased || clockTurning || rateTurning;
		w.alone = alone;
		w.gates = gates;
		w.wide = wide;
		w.n = n;
		w.chans = chans;
		w.poly = poly;
		w.overCV = overCV;
		w.halt = halt;
		w.once = once;
		w.curves = kind & tree::T_CRV;
		w.overAtn = kind & tree::T_ATN;
		w.overQnt = kind & tree::T_QNT;
		w.fade = fade;
		return w;
	}

	/** HALT and the direction: whether the walk is held, the step a hand
	is on, the direction, and the walk (`walk`). */
	void haltAndTurn(ThisWake& w, const ProcessArgs& args) {
		curvesWork = w.curves;
		// The block reshaped under the walk: the step the walk's member
		// stands on now (`walk` keeps the walk on it).
		int reshapedTo = -1, reshapedPre = -1;
		const int wasCount = lastCount;
		// An unchanged length is no reshape: a parse that changed nothing
		// under the walk, a Seq back from bypass, leaves the landing and the
		// ramp alone, so a bypassed Seq never comes back behind.
		if (woke && count != lastCount) {
			const int s = block.stepOf(walkMember, walkWithin, count);
			reshapedTo = s >= 0 ? s : (int)wrapPlace(lastWalkPos, count);
			if (preFor >= 0) {
				const int sp = block.stepOf(preMember, preWithin, count);
				reshapedPre = sp >= 0 ? sp : (int)wrapPlace(stepAt(prePlace, std::max(wasCount, 1), lastDir), count);
			}
		}
		lastCount = count;
		dir = (Dir)clamp((int)std::round(params[DIR_PARAM].getValue()), 0, DIRS - 1);
		if (dir == DIR_PENDULUM && curvesWork)
			dir = DIR_BOUNCE;
		cycle = cycleOf(count, dir);
		walk(w, args, reshapedTo, reshapedPre, wasCount);
		lastDir = dir;
	}

	/** A Clock in hertz with a cable in its PHASE makes its ticks at the pitch alone and sends how far PHASE has
	turned its cycle (`Count::turnWhole`), and I add it to my walk in places,
	as a cable in my own PHASE adds its turn, ten volts there one cycle of the
	Clock: so a sine in it at audio rate is phase modulation of the whole
	bank, through nought and back. A reset, a start or a new driver rebases
	it on the Clock's turn entire, so a bank walked home stands where PHASE
	puts it, as every Seq of the bank does; HALT holds it where it is; a
	driver that is not a Clock in hertz has none. */
	double clockTurn(const ThisWake& w) {
		const Count* c = w.cnt;
		if (!c || !c->hertz || drive != DRIVE_TIME) {
			ctFrom = nullptr;
			ctPlaces = 0.0;
			return 0.0;
		}
		int n, d;
		ratioOfRate(rateWalkWork, n, d);
		const double r = (double)c->tickN * (double)n / ((double)c->tickD * (double)d);
		const double since = (double)(c->turnWhole - ctBWhole) + (c->turnFrac - ctBFrac);
		const bool fresh = c->source != ctFrom || c->resets != ctResets || w.starts != ctStarts;
		if (fresh || w.halt || r != ctR) {
			ctP0 = fresh ? ((double)c->turnWhole + c->turnFrac) * r : w.halt ? ctPlaces : ctP0 + since * ctR;
			ctBWhole = c->turnWhole;
			ctBFrac = c->turnFrac;
			ctR = r;
			ctFrom = c->source;
			ctResets = c->resets;
			ctStarts = w.starts;
			ctPlaces = ctP0;
		}
		else
			ctPlaces = ctP0 + since * ctR;
		return ctPlaces;
	}

	/** A free rate, its jack exponential FM at audio rate (a volt an octave)
	and its screen heard on the sample: the writer walks at a rate it took
	smoothly, its ratio exact, and the rest is a turn added to the walk as a
	Clock's PHASE is, the places the screen's rate times two to the volts
	gains on the writer's journey, worked out every sample from the volts and
	the screen at that sample and the pulses' spacing, and summed: the walk's
	speed follows the jack and the screen at audio rate, so a sine in the jack
	makes sidebands rather than a rate stepped at the pulses, and the place is
	the integral, continuous through every change. Rebased to nought by a
	reset, a start or a new driver, as the walk goes home; never by a change
	of the rate, since the turn is places already walked. The samples since
	the last wake but this one moved at the speed that wake left, since
	nothing woke the rig between. */
	double rateTurn(const ThisWake& w, bool& rebased) {
		const Count* c = w.cnt;
		rebased = false;
		if (!c || drive != DRIVE_TIME) {
			rtFrom = nullptr;
			rtPlaces = 0.0;
			rtFrame = -1;
			rtSpeedWas = 0.0;
			return 0.0;
		}
		const bool fresh = c->source != rtFrom || c->resets != rtResets || w.starts != rtStarts;
		double speed = 0.0;
		if (rateTurnOn) {
			int n, d;
			ratioOfRate(rateSetWork, n, d);
			const double gain = std::exp2((double)rateVoltsWork) - 1.0;
			speed = gain * (double)n / ((double)d * c->period);
			// The screen's rate ahead of the writer's journey, or behind it.
			if (rtBehind)
				speed += ((double)n / (double)d - (double)rtWNum / (double)rtWDen) / c->period;
			// A ONCE spent by the writer short of the end under the turn, a
			// rate below the writer's: the writer stands, so the turn alone
			// walks the rest of the pass, at the whole rate in force, the
			// screen's times two to the volts (`place` holds it at the end).
			if (w.once && wr.spent)
				speed = std::exp2((double)rateVoltsWork) * (double)n / ((double)d * c->period);
		}
		if (fresh) {
			rebased = rtPlaces != 0.0;
			rtPlaces = 0.0;
			rtFrom = c->source;
			rtResets = c->resets;
			rtStarts = w.starts;
		}
		else if (rtFrame >= 0 && frameWork > rtFrame) {
			if (rateTurnOn)
				rtPlaces += speed;
			if (frameWork - rtFrame > 1)
				rtPlaces += (double)(frameWork - rtFrame - 1) * rtSpeedWas;
		}
		rtFrame = frameWork;
		rtSpeedWas = speed;
		return rtPlaces;
	}

	/** PHASE's volts wrapped to a turn each way, minus ten to ten as they
	stand, past them a turn every ten volts (`place`). */
	static double wrapTurn(double v) {
		if (v >= -10.0 && v <= 10.0)
			return v;
		const double r = std::fmod(v, 10.0);
		return r != 0.0 ? r : v > 0.0 ? 10.0 : -10.0;
	}

	/** How far through the place `tick` the place `step` and `along`
	stands, turned by `tb`: the one expression, so a wake for the ramp
	alone (`fineAgain`) works the same bits out as a whole wake. */
	static double throughOf(int64_t tick, int64_t step, double along, const Turn& tb) {
		return along + tb.frac - (double)(tick - step - tb.whole);
	}

	/** One wake's placing (`place`), carried from stage to stage: the turn
	and the rule it is added by, where the walk stands before it, and the
	playhead after it. */
	struct Placing {
		double rt = 0.0;      // a free rate's turn, the walk's speed (`rateTurn`)
		double phaseBy = 0.0; // PHASE's turn in places
		double ct = 0.0;      // a Clock in hertz's PHASE turn in places (`clockTurn`)
		double by = 0.0;      // the two summed, the turn added to the walk
		Turn tb;              // and split into whole places and a fraction
		int64_t cycle = 1;
		bool walked = false;    // pulses are coming: a whole place is a step arrived at
		bool startsNow = false; // a walk's start lands on its step at once
		int64_t walkedTo = 0;   // where the writer alone has the walk
		int64_t baseStep = 0;   // the walk's own place, before the turn
		double baseAlong = 0.0;
		int64_t fromStep = 0; // the place the turn is added to
		double fromAlong = 0.0;
		int64_t carriedTo = 0; // the writer's place carried by the rate's turn, held or halted alike: the walk the strikes count
		double carriedAlong = 0.0;
		bool held = false; // a step held under the mouse, driven
		int64_t tick = 0;  // the place turned: the playhead's
		float along = 0.f; // how far through its step the jack sounds
		int64_t sounded = 0; // the turned place the jack sounds
		bool beganMoved = false, aimed = false; // the writer began a step, or re-aimed a landing
		int64_t carried = 0; // under a phase, how far the walk carried the turned place
		bool landed = false;
		bool rebased = false; // a free rate's turn sent home with the walk (`rateTurn`)
		// The place a turn makes of the place `step` and `along`: the same
		// rule for the walk and for a held step, so a phase moves a halted
		// Seq as it moves a running one. PHASE adds to where the Seq stands,
		// minus ten to ten volts a turn each way, and ten volts is the end of
		// the turn, never the next start, +10 being the same as 9.9999: a turn of exactly whole cycles forward from the start of
		// a place stands at the end of the place before it, through one,
		// where 9.999 V was heading, walked or stood alike. Integers and an
		// exact nought, no tolerance. A spent ONCE gives exactly ten volts,
		// and wrapped to step one it would put a kick's envelope back at its
		// top. Walked, a landing at exactly ten
		// volts is a sample late, the place's start reached on the next.
		// A walk's start, its birth, a reset, a Sync, a step let go, lands on
		// its step at once, ten volts or nought: held back a sample it struck
		// the last step and then its own, a trigger two samples long.
		bool closesBy(const Turn& t, double along) const {
			return !startsNow && cycle > 0 && along == 0.0 && t.frac == 0.0 && t.whole > 0 && t.whole % cycle == 0;
		}
		int64_t turnedBy(const Turn& t, int64_t step, double along) const {
			return step + t.whole + (reachesOne(along, t.frac) ? 1 : 0) - (closesBy(t, along) ? 1 : 0);
		}
		int64_t turned(int64_t step, double along) const { return turnedBy(tb, step, along); }
		// How far through the place `tick` that place stands, turned.
		double through(int64_t tick, int64_t step, double along) const { return throughOf(tick, step, along, tb); }
	};

	/** The place: the turn a cable in PHASE adds, a held step,
	the step the playhead lands on, how far through it, and whether the
	landing fires. */
	void place(ThisWake& w) {
		Placing p;
		turnOf(w, p);
		walkFrom(w, p);
		pointsOf(w, p);
		barOf(w, p);
		alongOf(p);
		soundedOf(w, p);
		roundOf(w, p);
		landedOf(w, p);
		const bool heard = struckOf(w, p);
		keepPlace(w, p, heard);
	}

	/** The turn added to the walk: PHASE's volts in places, a Clock in hertz's
	PHASE and a free rate's, and the rule a turn is added by. */
	void turnOf(ThisWake& w, Placing& p) {
		p.cycle = cycle;
		// The turn is added last so nothing else knows about it: plain
		// continuous addition to the writer's position, after the writer and
		// before the readers, ten volts one whole cycle of the sequence,
		// whatever shapes the block, minus ten to ten a cycle each way,
		// negative backward, nothing unwrapped and nothing quantised, so a
		// small voltage rushes or delays the walk. The rate never scales it, under a clock or
		// with none, running or in HALT: the rate is the clock's. With no
		// pulse source the position is the turn on top of the step the walk
		// stands on, the step picked. A CV's or a CRV's range is never the
		// phase's. The volts are the jack's now (`phaseHeard`), as the walk's
		// landing is published on its pulse, so the two reach the readers
		// together.
		//
		// The top stands on the last place before the turn comes round, not
		// on the first again (a fader pushed all the way up would give step one,
		// which no player expects), walked or stood, a Clock stopped or never run among
		// them (`turned`). Past a turn either way it wraps on, a turn every ten
		// volts, so a CRV wavefolder driven past ten volts goes on folding,
		// as the negative range wraps too. Wrapped in volts, before the
		// places, so fifteen volts is five volts' place bit for bit: `fmod` is
		// exact, a whole turn past the top stands on the top as ten volts does
		// (twenty volts is ten's place, minus twenty minus ten's), and minus
		// ten to ten is untouched. The remainder of a voltage however large
		// is exact and inside a turn, so no place is ever an int64
		// overflowed, and a jack not finite reads nought (`sane`). The places
		// are the volts times the cycle over ten, worked out in that order in
		// double, which is exact wherever the answer is (a float tenth of the
		// volts first, -0.8f times five, came to a hair past minus four, and
		// the floor took the hair for a step: minus eight volts on five steps
		// stood a step short, the relation phase-whole-turn). The turn is
		// split into whole places and a fraction, and the walk's place stays
		// a whole place and a fraction, so the place a turn makes of it is
		// integers and one exact comparison of the two fractions (`Turn`,
		// `reachesOne`): summed in one double, ten volts on a walk a hair
		// short of its next step came to the next step.
		const double volts = w.phased ? wrapTurn((double)sane(phaseHeard)) : 0.0;
		p.phaseBy = volts * (double)cycle / 10.0; // in places: a whole turn is a whole cycle, there and back on a pendulum
		p.ct = clockTurn(w);
		p.rt = rateTurn(w, p.rebased);
		// A free rate's turn is the walk's speed, not a place added as
		// PHASE's is: the writer's place is carried by it first, the walk,
		// and PHASE and a Clock's turn are added on top as everywhere. So a
		// step the rate carries the walk onto is a step the walk travelled,
		// struck as the writer's own are, a one-step block's STEP on every
		// place (a free x1 SEQ of one CRV under a Clock in hertz, 1.3 V in
		// its FM, struck only where the writer's own move crossed, four
		// places in eleven, in a hard-sync rig). In ONCE it is held inside the
		// pass the writer is held in (otherwise any volts in FM other than
		// nought walked a ONCE round for ever, the turn
		// carrying the playhead past the end of the pass and on round the
		// cycle after the writer was spent), standing on the last step's end
		// where it reaches the stop.
		p.by = p.phaseBy + p.ct;
		p.tb = Turn::of(p.by);
		// Walked: pulses are coming, so a whole place is a step arrived at,
		// and the turn floors on it.
		p.walked = drive == DRIVE_TIME && w.cnt && w.cnt->running;
		p.startsNow = p.walked && (!woke || w.starts != lastStarts);
	}

	/** Where the walk stands before the turn: the writer's place carried by a
	free rate's turn, or a step held under the mouse, or HALT's held step. */
	void walkFrom(const ThisWake& w, Placing& p) {
		// The walk's own place, before the turn: what the step screen shows
		// under a phase, the bar showing what the turn adds to it.
		p.walkedTo = w.place; // where the writer alone has the walk
		p.baseStep = p.walkedTo;
		p.baseAlong = w.wAlong;
		// The place the turn is added to, the walk's: the writer's carried by
		// the rate's turn, in ONCE held short of the stop.
		p.fromStep = p.baseStep;
		p.fromAlong = p.baseAlong;
		bool atEnd = false;
		if (p.rt != 0.0) {
			const Turn tr = Turn::of(p.rt);
			p.fromStep = p.baseStep + tr.whole + (reachesOne(p.baseAlong, tr.frac) ? 1 : 0);
			p.fromAlong = throughOf(p.fromStep, p.baseStep, p.baseAlong, tr);
			if (w.once && p.fromStep >= cycle) {
				p.fromStep = cycle - 1;
				p.fromAlong = Walk::BELOW_ONE;
				atEnd = true;
			}
		}
		rtAtEnd = atEnd;
		p.carriedTo = p.fromStep;
		p.carriedAlong = p.fromAlong;
		p.tick = p.turned(p.fromStep, p.fromAlong);
		p.held = w.holding && drive == DRIVE_TIME && !w.halt;
		if (p.held) {
			// The step held under the mouse is the walk's own, and the
			// playhead stands on it turned, as the walk let go there will,
			// rather than sounding the picked step bare until the jump lands
			// and then moving by the turn.
			p.baseStep = placeOf(clamp(w.holdPos, 0, count - 1), count, dir);
			p.baseAlong = 0.0;
			p.fromStep = p.baseStep;
			p.fromAlong = 0.0;
			p.tick = p.turned(p.baseStep, 0.0);
		}
		if (drive == DRIVE_TIME && w.halt) {
			// The held step, the writer's, is nought volts at the PHASE jack
			// and the jack turns the sequence from there, the clock having no
			// say in HALT, so a halted Seq is driven by a phase. Where the
			// phase has turned it to, fraction and all, so a block of CRVs
			// under a phase alone is played through continuously rather than
			// step by step; a point member hears only the step.
			p.baseStep = p.walkedTo;
			p.baseAlong = 0.0;
			p.fromStep = p.baseStep;
			p.fromAlong = 0.0;
			p.tick = p.turned(p.baseStep, 0.0);
		}
	}

	/** Oversampled under PHASE, the place at each of the frame's four points. */
	void pointsOf(ThisWake& w, const Placing& p) {
		// Oversampled, the place at each of the frame's four points, PHASE's
		// volts brought up to four times the rate (`PhaseUp4`) and wrapped
		// there, each point turned by the same rule as the frame, integers
		// and a fraction: the walk under the turn moving straight from where
		// it stood the last whole wake to where it stands now, the last
		// point standing on it. A Clock's turn and a free rate's are this
		// frame's, as the walk is.
		w.subOk = w.phased && overWork && phaseUp.frame == frameWork;
		if (w.subOk) {
			double sub[4];
			phaseUp.points(sub);
			const bool moves = subFromFrame == frameWork - 1 && subStarts == w.starts;
			const double moved = moves ? (double)(p.fromStep - subFromStep) + (p.fromAlong - subFromAlong) : 0.0;
			for (int j = 0; j < 4; j++) {
				int64_t step = p.fromStep;
				double along = p.fromAlong;
				if (j < 3 && moved != 0.0) {
					const Turn m = Turn::of((double)(j + 1) / 4.0 * moved);
					const bool over = reachesOne(subFromAlong, m.frac);
					step = subFromStep + m.whole + (over ? 1 : 0);
					along = over ? subFromAlong - (1.0 - m.frac) : subFromAlong + m.frac;
				}
				const double by = wrapTurn(sub[j]) * (double)cycle / 10.0 + p.ct;
				const Turn t = Turn::of(by);
				w.subPlace[j] = p.turnedBy(t, step, along);
				w.subAlong[j] = throughOf(w.subPlace[j], step, along, t);
			}
		}
		subFromStep = p.fromStep;
		subFromAlong = p.fromAlong;
		subFromFrame = frameWork;
		subStarts = w.starts;
	}

	/** The step the playhead and the walk stand on, and PHASE's bar. */
	void barOf(const ThisWake& w, const Placing& p) {
		pos = stepAt(p.tick, count, dir);
		basePos = stepAt(p.baseStep, count, dir);
		// The bar: where the turn has carried the playhead. Where the Seq
		// plays a block of CRVs through, whose turn is not in steps, it flows
		// with the voltage as a share of the cycle. Stepped, it counts the
		// places the playhead moved on, a whole cycle folded away since the
		// playhead is back where it began, over the cycle's last place, so
		// the first step is empty and the last full, either way. Integers until the share.
		if (w.phased && curvesWork && cycle > 0)
			phaseWork = clamp((float)(p.phaseBy / (double)cycle), -1.f, 1.f);
		else if (w.phased && cycle > 1)
			phaseWork = (float)((p.tick - p.baseStep) % (int64_t)cycle) / (float)(cycle - 1);
		else
			phaseWork = 0.f;
	}

	/** How far through its step the playhead stands, and the walk's own. */
	void alongOf(Placing& p) {
		// How far through the step the walk stands, nought as it lands and
		// one as it leaves, for a member that is a stretch rather than a
		// point, a CRV, played through the step. The walk goes on between the
		// ticks wherever it is driven by a count that does, the Clock's or a
		// measured cable's; a picked or a held step stands at its start.
		// Walking towards step one the stretch is played from its end back to
		// its start, so a sequence reversed is the same curve run backwards
		// rather than its pieces turned round.
		p.along = clamp((float)p.through(p.tick, p.fromStep, p.fromAlong), 0.f, 1.f);
		if (alongBack(p.tick, count, dir))
			p.along = 1.f - p.along;
		// The same of the walk's own place, before the turn, for a CRV's
		// halo: with no turn it is `along` exactly, bit for bit.
		baseAlongWork = clamp((float)p.baseAlong, 0.f, 1.f);
		if (alongBack(p.baseStep, count, dir))
			baseAlongWork = 1.f - baseAlongWork;
	}

	/** What the jack sounds: whether the writer began or re-aimed a step,
	and, armed, the step it last sounded played on. */
	void soundedOf(ThisWake& w, Placing& p) {
		// Driven, a step begins when the writer says so, its serial of steps
		// begun moving: a ramp entering a step, a landing, a landing out of
		// an arm (a start: the Seq's birth, a reset, a Sync, a step let go;
		// on step one it gives EOC, `startOne`). An arm is shown and silent. In HALT the step
		// is the hand's and sounds as the hand moves it; under a phase a
		// step lands where the turned place moves to another step, or the
		// walk carries it on. Undriven, where the picked place moves.
		p.beganMoved = drive == DRIVE_TIME && w.began != lastBegan;
		// A landing re-aimed inside its window, a reset or a rate bound to
		// its pulse, moves the step with no step begun: heard as a landing,
		// its trigger already sent the one trigger (`reaimed`).
		// A block reshaped inside the window is the same: the place the
		// writer stands on may be the same number, but in the new block it
		// is another step (`rebound`).
		p.aimed = drive == DRIVE_TIME && (w.aims != lastAims || w.rebound);
		w.reaimed = p.aimed && !p.beganMoved;
		// And a turn moved at PHASE inside the window after the writer's last
		// pulse is that pulse's turn: the landing heard under the old turn is
		// re-aimed, its STEP or trigger already sent the one, as a reset or a
		// rate inside the window is (else a sample and hold on one Seq's CV
		// into another's PHASE moves the playhead twice a beat). Between pulses
		// the turn still adds continuously; only a move inside a pulse's
		// window is bound to it.
		w.reaimed = w.reaimed || (w.phased && w.open && p.by != lastBy && !p.beganMoved && !w.armed);
		// Armed, step one waiting is shown and silent: the step screen shows
		// it, and the jack plays on from the step it last sounded, live, its
		// member still reading the IN jack, until the pulse lands the step,
		// so a reset mid-note lets the note finish and a step still lands on
		// a tick. Under a turn (my PHASE, a Clock in hertz's PHASE, a free
		// rate's FM) the turn is measured from the step the reset armed, step
		// one, not from the step left: the jack moves there with no STEP as the reset arms, a turn
		// moving it to another step while armed strikes that step, as a
		// stopped bank is walked by PHASE alone, and the pulse strikes step
		// one turned once. So an armed Seq under a turn is not silent. A Seq
		// just born has sounded nothing to go on with.
		w.silent = woke && drive == DRIVE_TIME && w.armed && !w.halt && !p.held && !w.turning;
		// A reset the Clock says came after a landing on this very sample
		// arms after it, so that landing is the step last sounded though no
		// wake stood on it.
		if (w.silent && w.landedFirst) {
			soundedPos = stepAt(w.landedPlace, count, dir);
			soundedPlace = w.landedPlace;
		}
		// The place the jack sounds: the step stood on, or, armed, the step
		// last sounded, played on as it was playing: a CRV's curve goes on
		// along the journey the arm interrupted, live like a CV's fader,
		// rather than snapped to its start and held flat.
		p.sounded = p.tick;
		if (w.silent) {
			w.sounding = clamp(soundedPos, 0, std::max(count - 1, 0));
			p.along = clamp((float)w.liveAlong, 0.f, 1.f);
		}
		else {
			w.sounding = pos;
			soundedPlace = p.tick;
		}
		if (w.silent && alongBack(soundedPlace, count, dir))
			p.along = 1.f - p.along;
		soundedPos = w.sounding;
		// The turned place the jack sounds, whole and through, for the
		// edges under a turn (`bandLimit`).
		w.tPlace = p.sounded;
		w.tAlong = w.silent ? w.liveAlong : p.through(p.tick, p.fromStep, p.fromAlong);
		silentWork = w.silent;
	}

	/** A start, and the cycle coming round: what gives EOC. */
	void roundOf(ThisWake& w, const Placing& p) {
		// A start is the writer's to say, a landing out of an arm or bound to
		// a reset inside the window alike, so the second is never read as the
		// walk arriving and given an EOC on step one.
		w.start = drive == DRIVE_TIME && w.starts != lastStarts;
		lastStarts = w.starts;
		// EOC is the cycle coming round, the walk crossing a multiple of its
		// length, not the place being step one: an early pulse at x4 that
		// skips over step one still comes round. The walk's places only go
		// on, so a reset, which arms on nought, never comes round. Where the
		// step is a hand's or a phase's, step one is the round.
		const bool walking = drive == DRIVE_TIME && woke && !w.halt && !p.held && !w.turning;
		auto round = [&](int64_t k) { return k >= 0 ? k / cycle : -((-k + cycle - 1) / cycle); };
		// A landing made again inside its window, the block reshaped or a
		// rate bound to its pulse, comes round or not from where the pulse
		// landed from, read in the new block, not from where the old
		// landing put it, else an arpeggiator's chord shrunk a sample after a
		// pulse makes a round of the landing and gives no EOC. One the old
		// landing already sent stays the one (below).
		const int64_t roundFrom = p.aimed && w.open ? prePlace : walkPlace;
		w.roundCame = walking ? round(p.walkedTo) > round(roundFrom) : pos == 0;
		// A start on step one is the beginning of a cycle too, a start, a
		// reset, a Sync, a Seq born on a running driver, and gives EOC like a
		// wrap (without it, a Seq clocked by another's EOC and reset by
		// the same cable runs a bar late, the first bar's EOC never sent). A
		// step let go elsewhere is a start but begins no cycle.
		w.startOne = w.start && (walking ? p.walkedTo == round(p.walkedTo) * cycle : pos == 0);
	}

	/** Whether the step landed, by what drives it, and the writer's serials
	kept for the next wake to compare with. */
	void landedOf(const ThisWake& w, Placing& p) {
		const bool justArmed = drive == DRIVE_TIME && w.armed && !lastArmed;
		// Under a phase, whether the walk carried the turned place on: the
		// place the walk makes under the turn as it is now, moved, exactly,
		// its closed top and all (`turned`), so a pulse met by a rise to ten
		// volts on the same sample, the playhead standing on at the end of
		// the step it leaves, is no landing, and the step a sample on is.
		p.carried = p.turned(p.fromStep, p.fromAlong) - p.turned(lastFromStep, lastFromAlong);
		if (!woke)
			p.landed = true;
		else if (drive == DRIVE_SCREEN)
			p.landed = p.tick != shownPlace || drive != lastDrive;
		else if (w.halt)
			// A hand's move, or the turn moving the held step to another: in
			// HALT the walk travels nowhere and the phase is the drive, so
			// every such move is heard, as with no clock, or a sine in PHASE
			// moves the jack and never STEP.
			p.landed = p.tick != shownPlace && (w.handMoved || (w.turning && pos != stepAt(shownPlace, count, dir)));
		else if (p.held)
			p.landed = p.tick != shownPlace;
		else if (w.turning) {
			// A move of the place the jack sounds is a landing when it is a
			// step: the walk carried it on (`carried`, the turn held as it
			// was), or it stands on another step. A whole turn of the phase,
			// a saw's wrap or a square of nought and ten volts, moves the
			// place a cycle and the step not at all, and is no landing, while
			// a one-step block lands each step the walk carries it. Armed,
			// the step played on, turned, moving to another is one.
			p.landed = (p.sounded != heardPlace && !justArmed &&
			            (w.sounding != stepAt(heardPlace, count, dir) || p.carried != 0)) ||
			           w.start;
		}
		else
			p.landed = p.beganMoved || p.aimed;
		lastBegan = w.began;
		lastAims = w.aims;
		lastArmed = w.armed;
	}

	/** Under pulses with a cable in PHASE, whether a landing strikes: a step
	is struck once per step the walk travels. Returns whether it is heard. */
	bool struckOf(const ThisWake& w, const Placing& p) {
		// Under pulses coming, with a cable in PHASE swinging the steps the
		// walk takes, a step is struck once per step the walk travels, the
		// writer's travel carried by a free rate's turn: a landing on a step
		// struck less than a step of that travel ago is not struck again
		// (`Struck`). So a step the phase pushes to and lets go is not
		// struck again as the walk arrives on it, nor the step let back to
		// (the flam), while a step pulled back to, struck a whole step ago,
		// is, and a falling phase walking the sequence backwards strikes each
		// step it lands on once. Where the writer travels nowhere, a Clock
		// stopped, an arm waiting on its pulse, HALT, no driver at all, or a
		// ramp standing at its journey's end with no landing held, a cable
		// gone quiet, every move of the turn is struck. A REC take is made on
		// a strike and only then, so a step is written once. The filter is the
		// one place the plain rule, a step fires when the turned whole
		// changes, is bent.
		bool heard = p.landed;
		if (w.phased && p.walked && !w.halt && !w.armed && woke) {
			// The walk's serial: the writer's carried by the rate's whole
			// places, unsigned so it wraps as the writer's does. Where the
			// turn is sent home it drops back by those places, so the filter
			// starts afresh then, as on a new drive, rather than meet an old
			// strike's serial again.
			const uint32_t walk = w.began + (uint32_t)(p.carriedTo - p.walkedTo);
			// Where the walk carried the place on, the strike stands where
			// the walk met the turn, the turn's own fraction below its place
			// (exactly that, not the sample's along after it), so a step the
			// walk carries onto again under a steady turn is a whole step on,
			// exactly, at any rate: the sampled along fell a hair either side
			// of the last strike's and a one-step block at x2 struck every
			// other step.
			const double at = p.carried != 0 ? -lastBy - std::floor(-lastBy) : p.carriedAlong;
			// Parked, the ramp at its journey's end, every move of the turn
			// is struck, and the filter keeps its strikes, so the landing
			// just after a late pulse is held to where the step was really
			// struck: a whole step on from the last strike at the same along
			// is heard. Starting afresh on resuming stood the step at the
			// head of the resumed step and silenced that landing.
			if (w.parked) {
				if (drive != lastDrive || p.rebased)
					guarding = false;
				else if (guarding && p.landed)
					struck[pos] = {walk, at, strikeGen};
				return heard;
			}
			if (!guarding || drive != lastDrive || p.rebased) {
				// Afresh, with the step the playhead stands on struck at the
				// head of the walk's step: when it was struck is not known,
				// only that it was within this step of travel, so only a
				// landing in this same step reads as recent. Its along where
				// it stands, a wake after a start, silenced the next landing
				// on a one-step block, less than a step on from that along
				// though a whole step on from the start's strike.
				strikeGen++;
				guarding = true;
				struck[stepAt(heardPlace, count, dir)] = {walk, 0.0, strikeGen};
			}
			Struck& s = struck[pos];
			// A serial that went back reads as no recent strike.
			const int32_t d = (int32_t)(walk - s.walk);
			const bool within = s.gen == strikeGen && (d == 0 || (d == 1 && at < s.along));
			// A start is the downbeat and always heard, though the filter's
			// fresh memory of the step stood on would silence it.
			heard = p.landed && (w.start || !within);
			if (p.landed)
				s = {walk, at, strikeGen};
		}
		else
			guarding = false;
		return heard;
	}

	/** Whether the landing fires and takes, and what the next wake compares
	with: the places, the member stood on, the drive. */
	void keepPlace(ThisWake& w, const Placing& p, bool heard) {
		lastFromStep = p.fromStep;
		lastFromAlong = p.fromAlong;
		lastBy = p.by;
		// Born, only a step the writer began sounds: a running count's
		// landing on step one, with no window to hold it.
		const bool fires = heard && (woke || p.beganMoved);
		// A take is the patch performing, so it lands as the walk lands a
		// step and only then, which is when the step sounds, and never as the
		// drive changes under the step the Seq stands on: else born on a
		// stopped Clock or none, a cable in REC that had not yet delivered
		// writes nought into the step stood on, and a clock pulled writes
		// what the jack held then over what the step took as it landed. Nor
		// as the Seq is born: a running count's landing on step one is heard
		// on that very sample, when no cable has delivered anything yet, so a
		// take there would write nought into step one as a patch loads.
		const bool takes = fires && woke && drive == lastDrive;
		shownPlace = p.tick;
		heardPlace = p.sounded;
		walkPlace = p.walkedTo;
		// Who the steps stand on, looked up only when a step or the block has
		// moved since, which a wake on every sample over a block of CRVs
		// mostly has not.
		const int walkPos = stepAt(p.walkedTo, count, dir);
		if (walkPos != lastWalkPos || chainSerial != identSerial || count != identCount)
			block.memberAt(walkPos, walkMember, walkWithin);
		lastWalkPos = walkPos;
		identSerial = chainSerial;
		identCount = count;
		lastDrive = drive;
		woke = true;
		w.along = p.along;
		w.landed = p.landed;
		w.heard = heard;
		w.takes = takes;
		w.fires = fires;
	}

#include "SeqSound.hpp"

	/** A wake the host made for the ramp alone (`Node::fineWake`): a reader's
	fine tick, nothing else in the rig moved, no pulse, no reset, no jack and
	no knob swept. Then only how far through its step the walk stands can have
	changed since the last whole wake, so the census, the writer's events and
	the place are that wake's, and the walk's along is read afresh and turned
	into the member's along exactly as `place` does. Taken only where the
	place is the walk's own, unturned and unheld, and only while the params a
	wake reads stand as they did; anything else, or a writer that has moved
	past what a fine wake can move, is a whole wake. A whole wake every sample
	made a block of CRVs played through by nine Seqs on two Clocks cost a
	quarter more. */
	ThisWake fineWas;
	bool fineReady = false;
	float fineParams[6] = {};
	void wakeParams(float* p) {
		p[0] = params[RATE_PARAM].getValue();
		p[1] = params[MODE_PARAM].getValue();
		p[2] = params[DIR_PARAM].getValue();
		p[3] = params[ACROSS_PARAM].getValue();
		// ARP's channels are a lone Seq's steps, so a change of them is a whole wake.
		p[4] = inputs[IN_INPUT].isConnected() ? (float)inputs[IN_INPUT].getChannels() : 0.f;
		p[5] = params[FADE_PARAM].getValue();
	}
	void keepForFine(const ThisWake& w) {
		fineReady = woke && drive == DRIVE_TIME && !w.turning && !w.halt && !w.holding;
		// The free rate's turn is read before this wake's pulse lands
		// (`rtBehind`), so a landing that leaves the writer behind the
		// screen's rate turns the walk from the next sample: a turn, so no
		// ramp alone. A Seq in ONCE on a free rate split the gated rig from
		// the whole one there, an EOC and a step apart.
		if (fineReady && rateCore().isFree()) {
			int sn, sd;
			ratioOfRate(rateSetWork, sn, sd);
			fineReady = wr.armed || (wr.num == sn && wr.den == sd);
		}
		if (!fineReady)
			return;
		fineWas = w;
		wakeParams(fineParams);
	}
	bool fineAgain(ThisWake& w, const ProcessArgs& args) {
		if (!fineWake || !fineReady || relaxed(holding))
			return false;
		float p[6];
		wakeParams(p);
		for (int i = 0; i < 6; i++)
			if (p[i] != fineParams[i])
				return false;
		const Writer::Seen seen = wr.at(args.frame);
		if (seen.step != fineWas.place || seen.armed != fineWas.armed || seen.began != fineWas.began ||
		    seen.starts != fineWas.starts || seen.aims != fineWas.aims)
			return false;
		w = fineWas;
		w.wAlong = w.wOwnAlong = seen.along;
		w.liveAlong = seen.liveAlong;
		// As `place` turns it, the turn nought: the place is the walk's own.
		const Turn none = Turn::of(0.0);
		const int64_t tick = w.place;
		float along = clamp((float)throughOf(tick, w.place, w.wAlong, none), 0.f, 1.f);
		if (alongBack(tick, count, dir))
			along = 1.f - along;
		if (w.silent) {
			along = clamp((float)w.liveAlong, 0.f, 1.f);
			if (alongBack(soundedPlace, count, dir))
				along = 1.f - along;
		}
		w.along = along;
		// And the place the jack sounds, the walk's own, for a fade.
		w.tAlong = w.silent ? w.liveAlong : w.wAlong;
		w.landed = w.heard = w.takes = w.fires = w.start = w.roundCame = w.startOne = w.landedFirst = w.reaimed = false;
		lastFromAlong = w.wAlong;
		return true;
	}

	void evaluate(const ProcessArgs& args) override {
		ThisWake w;
		if (!fineAgain(w, args)) {
			w = census(args);
			haltAndTurn(w, args);
			place(w);
			keepForFine(w);
		}
		const Sound snd = soundStep(w, args);
		const bool gates = w.gates, overCV = w.overCV, phased = w.phased, fires = w.fires;
		winOut = snd.window;
		playedNotes = snd.type == Sound::VOLTS && snd.band == NO_BAND && snd.voices <= 1 && !chain.empty() &&
		              (chain[0]->traits & tree::T_CV) && snd.window.quantized() && !snd.window.scale.spread;

		slewModeWork = slewModeOf(w);
		writeJack(snd, fires, w, args);
		// A Seq whose jack is gliding is fine while it glides, so a wake
		// asked for the glide alone runs it and its cluster (`fineGroup`),
		// and is not once it has landed.
		setFine(fineWork || slewLeft > 0);
		gatesWork = gates;
		// CVs and Atns both preview: a lane held on an Atn the playhead is
		// not on is heard at once, as a fader held on a CV is.
		overCVWork = overCV;
		overAtnWork = w.overAtn;
		overQntWork = w.overQnt;
		// The pulse jack is EOC: one pulse as the sequence comes round to its
		// first step again, which is what a sequencer's neighbours want to
		// hear and the one thing nothing else on the rig says. A pulse a step
		// is not, since the count that moves the playhead is already there to
		// be counted. The exception is a cable in PHASE: a turn moves
		// the step without the count moving, so nothing else on the rig knows
		// a step has landed, and there alone the jack is STEP.
		// A landing re-aimed inside its window fires the jack only where its
		// pulse had not: an EOC already sent stays the one.
		if (fires && (phased || (w.roundCame && !w.start) || w.startOne) && !(w.reaimed && trigFired && trigBegan == w.began)) {
			trig.fire(outputs[STEP_OUTPUT], args, fireFrames(args, w.reaimed));
			trigFired = true;
			trigBegan = w.began;
		}
		if (fires && !w.reaimed && lastFireAt != args.frame) {
			prevFireAt = lastFireAt;
			lastFireAt = args.frame;
		}
		// When the writer next moves by itself, and whether a reader plays
		// its ramp every sample: what the Seq wakes for (`everySample`).
		nextDue = drive == DRIVE_TIME ? wr.nextEvent(args.frame) : -1;
		rampsFine = drive == DRIVE_TIME && fineWork && (wr.ramping(args.frame) || (silentWork && wr.liveRamping(args.frame)));
		headCount = count;
		// A reset armed: the cells name the step still playing, the arm mark
		// says step one waits on the next pulse.
		const bool armShown = drive == DRIVE_TIME && w.armed && resetArm && !w.halt;
		armedShownWork = armShown;
		walkPos = pos;
		headPos = armShown && w.silent ? w.sounding : pos;
		baseShownWork = basePos;
		headColor = color;
		headDrive = drive;
		headPhased = phased;
	}

	/** Randomize rolls the knobs, not the transport: the mode stays, HALT
	stopping a Seq over a block, and so do the slew, BLEP and Oversampling,
	helpers and no voice. */
	bool rolled(int p) override {
		return p != MODE_PARAM && p != SLEW_PARAM && p != SLEW_ON_PARAM && p != BLEP_HZ_PARAM && p != OVER_HZ_PARAM &&
		       p != BLEP_PARAM && p != OVER_PARAM && p != SLEW_ON_HZ_PARAM;
	}
};

#include "SeqParts.hpp"

} // namespace

Model* modelSeq = createModel<Seq, SeqWidget>("SEQ");
