// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "HeadBase.hpp"

/** Clock, CLK on the plate: where every temporal thing in a rig begins, sending
the Seqs of its rig pulses, resets and its tempo, in BPM or, in hertz, as an
oscillator. */

using namespace rig;

namespace {

/** The tempo's and the frequency's gestures (Words.hpp): shift lands on
tens of BPM or on octaves, command a cent at a time. */
const Gesture G_CLOCK_TENS = declare(On::Drag, Mod::Shift, Rank::Known, RACK_MOD_SHIFT_NAME "-drag: tempo by tens");
const Gesture G_CLOCK_OCTAVES = declare(On::Drag, Mod::Shift, Rank::Known, RACK_MOD_SHIFT_NAME "-drag: frequency by octaves");
const Gesture G_CLOCK_CENTS = declare(On::Drag, Mod::Command, Rank::Known, RACK_MOD_CTRL_NAME "-drag: frequency by cents");

// The multiplier as a signed step from x1 on the one ladder a Seq's rate
// is on too, RATE_MIN to RATE_MAX: the arithmetic, the ladder
// and the jack's sweep are in Rate.hpp, the rate held in millionths. A plain
// drag steps by a whole rate, Command held by a hundredth, Command and shift
// by a ten-thousandth, and shift held lands on the powers of two
// (`RateScreen`).


struct Clock;
/** The tempo as the hover and a typed value have it: the tempo in force, the
setting times what the BPM jack multiplies it by, so what is typed in is what
the readout will show and the setting is worked back from it. A whole number
of BPM unbent; bent, the setting is whatever the tempo shown over the gain
is, a continuous quantity. Following a cable there is one tempo and it is the
cable's: the hover names it exactly, its fraction and all, and says where it
comes from, and nothing the hand does reaches the setting under it, not a
typed value, a reset or Randomize (`Clock::follows`; the screen refuses its
strokes the same way, `TempoParam::idle`). */
struct TempoQuantity : HintQuantity {
	Clock* clock = nullptr;
	double gain();
	bool follows();
	double heard();
	float getDisplayValue() override;
	void setDisplayValue(float v) override;
	std::string getDisplayValueString() override;
	std::string getUnit() override;
	void reset() override;
	void randomize() override;
};

/** The frequency as the hover and a typed value have it: the pitch, the
setting times what the V/OCT jack multiplies it by, exact, so the hover is
the readout's value to the hundredth, and what is typed in is the pitch the
readout will show, the setting worked back from it. */
struct FreqQuantity : HintQuantity {
	Clock* clock = nullptr;
	double gain();
	bool follows();
	double heard();
	float getDisplayValue() override;
	void setDisplayValue(float v) override;
	std::string getDisplayValueString() override;
	std::string getUnit() override;
	void reset() override;
	void randomize() override;
};

struct Clock : Member, Probe, RateMode {
	enum ParamId {
		MODE_PARAM, // 0 CLK, 1 HZ, the title's arrows' alone
		TEMPO_PARAM,
		FREQ_PARAM, // HZ: the cycle's frequency, in octaves from C4, on the tempo's screen
		RATE_PARAM, // the multiplier as a signed step from x1, /255 to x255; BPM's (`rateParam`)
		RATE_HZ_PARAM, // HZ: the multiplier, as RATE_PARAM is BPM's
		RUN_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		BPM_INPUT,  // bends the tempo from the readout's setting; HZ: V/OCT
		CLOCK_INPUT, // an external clock to follow, its gap the tempo or, in HZ, the cycle
		RATE_INPUT,  // sweeps the division, as a Seq's RATE does
		RESET_INPUT,
		RUN_INPUT,   // a gate high for running; HZ: PHASE, the bank's turn
		INPUTS_LEN
	};
	enum OutputId {
		RUN_OUTPUT, // 10 V while running; HZ: PHASE, the cycle's ramp
		CLK_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		RUN_LIGHT,
		LIGHTS_LEN
	};

	/** A Clock sends what it has to send on the samples it says so in
	`everySample`, and is asked at every wake of the rig all the same, so
	what it hands on is never older than the wake. */
	bool atEveryWake() override { return true; }

	/** The colour it is drawn in: the reader's, or with nobody reading it
	the first colour, yellow, as a lone Seq is, rather than grey. */
	int drawnColor() const { return std::max(0, relaxed(groupColor)); }
	int drawnOut() override { return drawnColor(); }
	std::atomic<int> groupColor{0}; // the colour its group opens with, from `rankClock`
	/** Whether a reader takes my count, for the RESET tooltip: a parse fact,
	told me by the reader's clock slot (`TOLD_COUNTED`), never read off my
	parents or written into me by every reader every wake. */
	std::atomic<bool> readNow{false};
	/** Whether a reader takes my count: told by the parse that one holds me
	(`TOLD_COUNTED`), and then by each holder, every time it changes, whether
	it takes the count or a cable of its own has it (`countTaken`), so a Clock
	beside a Seq clocked by its own cable is dressed as that Seq's clock only
	while it lends it its tempo (`takePeriod`). Counted while any holder that has spoken takes it, or, before
	any has, while the parse says one holds me. The holders are kept by who
	they are, a few at most. */
	struct Taker {
		const void* by;
		bool on;
	};
	static constexpr int MAX_TAKERS = 16;
	Taker takers[MAX_TAKERS];
	int ntakers = 0;
	bool toldCounted = false, countedWork = false;
	bool counted() const {
		if (ntakers == 0)
			return toldCounted;
		for (int i = 0; i < ntakers; i++)
			if (takers[i].on)
				return true;
		return false;
	}
	void countTaken(const void* by, bool on) override {
		int i = 0;
		while (i < ntakers && takers[i].by != by)
			i++;
		if (i == ntakers) {
			if (ntakers == MAX_TAKERS)
				return;
			ntakers++;
		}
		takers[i] = {by, on};
		recount();
	}
	/** A reader starts or stops taking my count, told me in its wake: kept in
	the plain field, and handed the screens in my publish, the wake the poke
	makes, as everything a screen reads is, never stored here mid-wake. */
	void recount() {
		const bool c = counted();
		if (c == countedWork)
			return;
		countedWork = c;
		poke.store(true, std::memory_order_relaxed);
	}
	void publishReads() override {
		readNow.store(countedWork, std::memory_order_relaxed);
		groupColor.store(countedWork ? colorRank % MAX_COLORS : 0, std::memory_order_relaxed);
		countedShown.store(countedWork, std::memory_order_relaxed);
	}

	/** Run is run, and every Rack user knows it, so it has no lesson in any
	state. The one thing worth saying is when the switch has stopped
	deciding, said as its state where Running or Stopped would be
	(`restate`), which in hertz it never is, the jack there being PHASE. */
	void retipRun() {
		restate(this, RUN_PARAM, !hz() && inputs[RUN_INPUT].isConnected() ? "Set by the RUN jack" : "");
		retipParam(this, RUN_PARAM, {"Run", ""});
		if (!hz())
			retipIn(this, RUN_INPUT, {"Run", "A gate that takes over the switch: high runs, low stops."});
	}

	/** Whether it runs: the jack when a cable is in, else the latch; in
	hertz the latch alone, the jack being PHASE. */
	bool running() {
		if (!hz() && inputs[RUN_INPUT].isConnected())
			return inputs[RUN_INPUT].getVoltage() > TRIG_HIGH;
		return params[RUN_PARAM].getValue() > 0.5f;
	}
	/** The RUN light says so, and in BPM the RUN jack, steady: on or off.
	Written on the sample it moves and not again, which is exact and costs
	nothing between times; a steady level has no reason to be stored
	forty-eight thousand times a second. In hertz the jack is PHASE's ramp
	(`rampOut`). */
	int runShown = -1;
	void writeRun(bool run, bool hzMode) {
		if ((int)run == runShown)
			return;
		runShown = (int)run;
		lights[RUN_LIGHT].setBrightness(run ? 1.f : 0.f);
		if (!hzMode)
			outputs[RUN_OUTPUT].setVoltage(run ? 10.f : 0.f);
	}

	int channelsHeard(int input) const override { return 1; } // mono by design: every jack its first channel
	std::atomic<int> bpmNow{120}; // for the readout, three cells wide
	// The tempo heard on the clock input, exact, for the hover; minus one
	// with no cable or no tempo heard yet. Stored with the readout's.
	std::atomic<double> heardNow{-1.0};
	// What the BPM jack multiplies the setting by, for the readout's hand
	// (`TempoParam`): one unpatched, or following a cable. Stored where it
	// is worked out, at the control rate, and read only by the UI.
	std::atomic<double> bpmGain{1.0};
	// The pitch in hertz, the readout's in HZ. Stored with the others, at
	// the control rate.
	std::atomic<double> hzNow{261.6256};
	int tempoOut() override { return relaxed(bpmNow); }
	double hzOut() override { return relaxed(hzNow); }
	// The RATE jack, for the bar under the division: patched, and its volts
	// over ten. Stored at the control rate where they are read.
	std::atomic<bool> ratePatched{false};
	std::atomic<float> rateCv{0.f};
	// And the V/OCT jack's, following a cable: patched, and the whole
	// octaves it moves the ticks by over ten, the division it makes.
	std::atomic<bool> octPatched{false};
	std::atomic<float> octCv{0.f};

	// ---- What it sends (`Count`), exact: the serials of its ticks and its
	// resets, the ticks since the last reset, and the index of the next tick
	// it makes, counted for ever, which is the same number whether its own
	// tempo makes it or a cable's beats do.
	uint32_t pulses = 0, resets = 0;
	uint32_t resetAfter = 0; // the latest reset: the pulses sent before it,
	bool resetBinds = false; // and whether it belongs to the last of them (`Count::resetBinds`)
	int64_t next = 0;
	int64_t lastPulseFrame = -1; // the frame of the last tick it made, -1 for none
	double lastAfter = 0.0;      // and in hertz the part of that frame after the tick (`Count::after`)
	// The division the ticks are made at, which is not always the one asked
	// for: a change waits for the next beat, the tempo's own quarter note,
	// whatever the division, so a change is heard within half a second at
	// 120 however slow the ticks, and lands where a bar would put it however
	// fast they are. Made at once while stopped.
	int rateLive = 0; // in millionths (`RATE_ONE`)
	bool rateStarted = false;

	// ---- Its own tempo, an anchored oscillator. Time is counted in beats:
	// at `anchorTime` (a frame, fractional after a change of tempo) it stood
	// `anchorBeats` beats on, and a beat lasts `beatFrames` frames, so beat b
	// falls at anchorTime + (b - anchorBeats) beatFrames. The division is
	// based on a whole beat, `yBase`, where tick `tBase` stands, so tick j
	// falls at beat yBase + (j - tBase) den / num: worked out from the anchor
	// every time and never summed, so the k-th tick on a steady tempo is on
	// floor(anchor + k period) however many came before; a beat's fraction
	// added sample by sample put one tick in thirteen a sample late after a
	// few minutes at 97 BPM.
	double anchorTime = 0.0, anchorBeats = 0.0, beatFrames = 0.0;
	int64_t yBase = 0, tBase = 0;
	// A free rate's FM moves the base off the whole beat: the part of a beat
	// past `yBase` where tick `tBase` stands, nought on the grid.
	double yFrac = 0.0;
	int64_t nextBeat = 0;     // the next whole beat to fall, for a change of division
	int64_t lastBeatFrame = -1;
	bool ownRunning = false;  // the oscillator ran at the last sample
	bool armedStart = true;   // RUN makes a tick at once: fresh, or sent home while stopped

	// ---- Following a cable: the writer at the multiplier. Each trigger
	// is an edge, and the gap from the last is the tempo (`TrigCounter`).
	// The landed place is exact, whole ticks `fw` plus `fs` over the
	// division's den, a beat adding num; between beats a ramp runs from
	// `rampFrom` across `span` ticks over the last gap and makes a tick per
	// whole it crosses, never past `capTick`, the last tick before the next
	// beat's own.
	TrigCounter beatIn;      // its edges and its gaps, a gap a reset fell in not measured (`resetFell`)
	double beatPeriod = 0.0; // frames a beat, the last gap measured
	double beatPeriodBefore = 0.0; // and as it stood before the last beat (`restart`)
	bool gapSkipped = false; // the last reset left the gap it fell in unmeasured
	bool waiting = true;     // the next trigger is a start: the tick it lands is sounded as it stands
	bool halted = false;     // RUN off: triggers ignored, the ramp stood where it was
	double haltedAt = 0.0;
	bool beatInHalt = false; // a beat came while RUN was off: the cable ran on through the stop
	// Where a beat cuts a multiplied journey and the ticks the next one
	// pays back (`cutAt`).
	struct Cut {
		int64_t to = 0, owed = 0;
	};
	// The last beat heard while RUN was off, which RUN arriving inside its
	// window takes as its own (`follow`).
	struct HaltBeat {
		int64_t frame = -1;
		double after = 0.0;
		bool inHaltBefore = false;
		double stood = 0.0;
		Cut cut;
	} haltBeat;
	int64_t fw = 0, fs = 0;
	int64_t owed = 0; // ticks the journey in progress pays back (`cutAt`)
	double rampFrom = 0.0, span = 0.0;
	int64_t capTick = -1;
	int64_t beatFrame = -1;
	// Where in its frame the last beat fell, in hertz (`TrigCounter::after`),
	// nought in BPM, and so the time it stood at, `beatFrame` + 1 less it:
	// the ramp is timed from there, so a cycle that is no whole number of
	// frames keeps its length tick by tick. In BPM `beatTime` is `beatFrame`
	// + 1 and every sum below is a whole number of frames.
	double beatAfter = 0.0, beatTime = 0.0;
	// The beats landed since the start, in hertz the cycles, for the PHASE
	// jack's ramp (`followCycle`).
	int64_t cycWhole = 0;
	// The last beat's decision, open for the window: where the ticks stood
	// before it landed, so a change of division or a RESET inside the window
	// makes it again (`landBeat`, `restart`).
	struct BeatPre {
		int64_t fw = 0, fs = 0;
		int rate = 0;
		double stood = 0.0;
		Cut cut;
	} beatPre;
	bool beatOpen = false; // a beat landed by `landBeat`, so `beatPre` is its
	bool startOpen = false; // the last beat was a start, a fresh cable's or a RESET's (`follow`)
	bool wasFollowing = false;
	dsp::SchmittTrigger resetTrig;

	// ---- The right-click menu's two. Set from the UI, read once a sample into
	// the engine's own fields.
	/** Pulses a beat, in BPM alone: 1, 4 or 24. On the CLOCK jack that many
	pulses are one beat, the tempo and the division counted from the beat;
	on the CLK jack, past 1, that many pulses a beat at the tempo, whatever
	the division, so a Clock following a DAW's 24 sends 24 on. At 1 the CLK
	jack is the ticks. */
	static constexpr int PPQN_N = 3;
	static int ppqnOf(int i) { return i == 2 ? 24 : i == 1 ? 4 : 1; }
	std::atomic<int> ppqnSet{1};
	int ppqn = 1; // in force: the setting in BPM, 1 in hertz
	/** BLEP, in hertz alone: the CLK jack's square and the PHASE jack's ramp
	band-limited, to hear, the same edge a Seq's BLEP puts on its jack
	(`Blep`). Off by default, the two then sample-exact, as they always were. */
	std::atomic<bool> blep{false};
	bool blepWork = false; // on, and in hertz
	Blep clkBlep, rampBlep;
	/** The PHASE jack's ramp in hertz, its naive volts, and where the cycle
	stood when it was last worked out, its whole turns and all, whether it
	has been worked out since the mode came, and the resets it had seen, so its wrap is told from a
	jump (`phaseOut`). */
	float rampOut = 0.f;
	double rampPrev = 0.0;
	double rampFloor = 0.0; // the whole under it, one lower at a closed top
	bool rampFlowed = false; // worked out on the last sample it was asked, since the mode came
	uint32_t rampResets = 0;
	/** Following, a pulse in is a beat over `ppqn`: the division's ticks a
	pulse, `n` every `d`, in lowest terms, so the journeys between pulses
	are the writer's own at any setting. */
	void ratioIn(int& n, int& d) const {
		ratioOfRate(rateLive, n, d);
		// A denominator near /255 times 24 passes an int; in lowest terms it
		// is back under three times the magnitude (`ratioOfRate`).
		int64_t a = n, b = (int64_t)d * ppqn;
		const int64_t dd = b;
		while (b) {
			const int64_t t = a % b;
			a = b;
			b = t;
		}
		n = (int)(n / a);
		d = (int)(dd / a);
	}
	/** Where a pulse in stands in its beat, nought on the beat: a start is
	nought, and a change of division waits for a beat, as on the Clock's own
	tempo, never landing on a sixteenth. */
	int phaseIn = 0;
	bool beatOnGrid = true; // the last pulse landed was a beat's
	int wantOnGrid(int want) const { return phaseIn == 0 ? want : rateLive; }
	// The CLK jack past 1: the pulses it has made, the frame of the last, and
	// on its own tempo the index of the next, pulse q falling at beat q over
	// `ppqn` on the oscillator's anchor (`ppqAt`).
	uint32_t ppqPulses = 0;
	int64_t ppqFrame = -1, ppqNext = 0;
	double ppqAt(int64_t q) const {
		const double at = (double)yBase - anchorBeats + (double)(q - yBase * ppqn) / (double)ppqn;
		return anchorTime + at * beatFrames;
	}
	/** The next pulse after where the beats stand, after a change of setting. */
	void ppqFrom(int64_t f) {
		ppqNext = beatFrames > 0.0 ? (int64_t)std::floor(beatsAt(f) * ppqn) + 1 : 0;
	}
	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "ppqn", json_integer(relaxed(ppqnSet)));
		json_object_set_new(rootJ, "blep", json_boolean(relaxed(blep)));
		rateModeToJson(rootJ);
		return rootJ;
	}
	void dataFromJson(json_t* rootJ) override {
		Member::dataFromJson(rootJ);
		json_t* p = json_object_get(rootJ, "ppqn");
		const int n = p ? (int)json_integer_value(p) : 1;
		ppqnSet.store(n == 4 || n == 24 ? n : 1, std::memory_order_relaxed);
		json_t* b = json_object_get(rootJ, "blep");
		blep.store(b && json_boolean_value(b), std::memory_order_relaxed);
		rateModeFromJson(rootJ);
	}
	/** What a RIG's scene holds of my menu, past my params: the pulses per
	quarter, BLEP and the division's mode (`RateMode`), all whole. */
	enum SceneSetting { SCENE_PPQN, SCENE_BLEP, SCENE_FREE, SCENE_SETTINGS };
	int sceneSettings() override { return SCENE_SETTINGS; }
	float sceneSetting(int k) override {
		if (k == SCENE_FREE)
			return rateFreeSet() ? 1.f : 0.f;
		return k == SCENE_PPQN ? (float)relaxed(ppqnSet) : k == SCENE_BLEP && relaxed(blep) ? 1.f : 0.f;
	}
	void setSceneSetting(int k, float v) override {
		if (k == SCENE_PPQN) {
			const int n = (int)std::lround(v);
			ppqnSet.store(n == 4 || n == 24 ? n : 1, std::memory_order_relaxed);
		}
		else if (k == SCENE_BLEP)
			blep.store(v != 0.f, std::memory_order_relaxed);
		else if (k == SCENE_FREE) {
			setRateFree(v != 0.f);
			wake();
		}
	}
	/** Initialize leaves a Clock as one fresh from the browser, its menu's
	settings too. */
	void onReset(const ResetEvent& e) override {
		ppqnSet.store(1, std::memory_order_relaxed);
		blep.store(false, std::memory_order_relaxed);
		setRateFree(false);
		Member::onReset(e);
	}

	// ---- The CLK jack.
	bool jackArmed = false; // a tick has fallen since the Clock last ran, so its pulse may sound
	bool pulseHigh = false; // the jack at the last sample
	bool jackOwed = false;  // a tick came while it was high: it fell for a sample and rises now
	int64_t jackFrame = -1; // the frame the jack was last written on
	uint32_t jackPulses = 0; // the ticks the jack has seen
	double tickPeriod = 0.0; // the frames between my ticks as the tempo has it (`Count::period`)
	int64_t frameNow = 0;    // this sample's, for a reset made from the bracket
	int64_t window = 48;     // the Seqs' window in frames (window.json)
	WindowFrames windowNow;  // and kept, worked out when the rate moves
	/** The division in force. Following a clock the BPM jack has no tempo to
	bend, so it is the division instead: a volt a step from the screen. */
	int rateSet() { return rateParamOf(this, rateParam()); }
	/** The division the RATE jack leaves: a volt an octave around the
	screen's, nought volts leaving it, snapped to the nearest whole rung in
	the whole mode and continuous in the free one, its pie showing (`RateIn`,
	the reading a Seq's RATE jack shares). No stop, since RUN is what stops a
	Clock. */
	RateIn rateIn() { return rateInOf(this, rateParam(), RATE_INPUT); }
	/** The division in force, in millionths. Following, the V/OCT jack moves
	it a whole octave a volt from the whole rate it stands on, the fraction
	being the hand's alone, free or whole (`RateIn`). */
	int rateNow() { return rateNow(rateIn()); }
	/** The same from a reading of the jack already taken this sample. */
	int rateNow(const RateIn& in) {
		const int h = in.rate();
		if (follows() && inputs[BPM_INPUT].isConnected())
			return rateOfStep(clamp(pow2Rung(wholeStepOf(h), (int)std::round(clamp(inputs[BPM_INPUT].getVoltage(), -10.f, 10.f))),
			                        RATE_MIN, RATE_MAX));
		return h;
	}
	void settle() override {
		rankClock(at(slotNamed("before")));
	}
	/** Its colour, once the parse is whole: its rank's when a reader takes
	its count, and the first colour, yellow, when nobody does, as any working
	module nobody reads wears. Pass one, because the
	reader that tells me so stands to my right and has not been built when I
	settle. The rank itself stays, so nothing to my right is recoloured.
	Worked out here as plain fields and handed the screens in my publish:
	the first wake after a bond stirs the whole rig, and I am at every
	wake, so it publishes them on the sample the parse lands. */
	void parsed(const tree::Left& left, int pass) override {
		if (pass == 1) {
			ntakers = 0;
			toldCounted = (told() & tree::TOLD_COUNTED) != 0;
			countedWork = toldCounted;
		}
	}
	std::atomic<bool> countedShown{false};
	// What only the UI knows, handed over in the widget's step: the cable in
	// my clock jack's colour, and whether I stand in a bond.
	std::atomic<uint32_t> inRGB{0};
	std::atomic<uint32_t> bpmRGB{0}; // and the cable in my BPM jack's
	std::atomic<bool> bondedUi{false};
	/** And, for the words alone (`retipAll`), who stands in my run: a SEQ
	to my right before the next CLK (my RESET's reach), a CLK further right,
	a CLK to my left (whose resets an empty RESET takes, `hearLeft`), each up
	to a GAP, so a tooltip names only the neighbours there are. */
	std::atomic<bool> seqsRightUi{false}, clockRightUi{false}, clockLeftUi{false};
	/** The colour my readouts and RUN wear, by the scheme (`schemeColor`):
	my group's while a reader takes my count, else the white bonded with
	nothing taking my count and no cable leaving, idle but working, and
	yellow alone. The cable I follow colours the tempo alone (`tempoColor`).
	Under the DEV's OLD, my group's colour from the palette whatever the
	scheme says. */
	NVGcolor faceColor() {
		if (dev().oldColors || relaxed(countedShown))
			return playColor(drawnColor());
		return schemeColor(-1, 0u, relaxed(bondedUi),
		                    outputs[CLK_OUTPUT].isConnected() || outputs[RUN_OUTPUT].isConnected());
	}
	/** The tempo's readout: the cable in my clock jack's colour while one
	sets my tempo, so the one number set from outside says so; else the cable
	in my BPM jack's while one bends it; else my face's. With both patched the
	clock jack's wins, since the tempo is then the outside clock's and the BPM
	jack is the division. In hertz the same, the number the frequency. */
	NVGcolor tempoColor() {
		const uint32_t in = relaxed(inRGB);
		return modColor(in ? in : relaxed(bpmRGB), faceColor());
	}
	// What the three cells of the readout hold, decimal to 999 and hex past it
	// to FFF (`drawCount`).
	static constexpr int TEMPO_MIN = 1, TEMPO_MAX = 4095;
	/** What the BPM CV jack multiplies the setting by: two to the volts, one
	volt doubling and minus one halving, read from ten either way; one
	unpatched, and one following a cable, where the jack is the division. */
	double tempoGain() {
		if (!inputs[BPM_INPUT].isConnected() || follows())
			return 1.0;
		return std::exp2((double)clamp(inputs[BPM_INPUT].getVoltage(), -10.f, 10.f));
	}
	/** A setting times a gain, kept within what the readout holds: the
	tempo in force, the engine's and the readout's hand's one arithmetic. */
	static double tempoKept(double t) { return std::min(std::max(t, (double)TEMPO_MIN), (double)TEMPO_MAX); }
	static double bent(double set, double gain) { return tempoKept(set * gain); }
	/** Whether a cable in my clock jack sets the tempo, or in hertz the
	cycle: then it is the one tempo or frequency, and the setting is not the
	hand's (`TempoQuantity`, `FreqQuantity`). One law in both modes, a
	trigger a beat and the gap the period (`follow`); in hertz the gap is
	measured to the part of a frame (`TrigCounter::fine`), the pitch riding
	on it. */
	bool follows() { return inputs[CLOCK_INPUT].isConnected(); }
	/** The gap the cable's last two edges make: whole frames in BPM, where
	everything on the cable counts in them, a Seq's clock among them, and to
	the part of a frame in hertz, where the gap is the pitch. */
	double gapIn() const { return hzWas ? beatIn.fine : (double)beatIn.period; }
	/** Whether it is in hertz, an oscillator, rather than a clock. */
	bool hz() { return params[MODE_PARAM].getValue() > 0.5f; }
	/** The rate param in force: each mode keeps a rate of its own, BPM's
	x4 and HZ's x1 fresh, so a
	switch finds the other mode's rate where it was left and the engine sets
	nothing. Everything that reads or shows the rate asks this; the RATE
	jack sweeps whichever it names. */
	int rateParam() { return hz() ? RATE_HZ_PARAM : RATE_PARAM; }
	static constexpr double FREQ_MIN = 0.01, FREQ_MAX = 20000.0; // what the readout holds, 0.01 to 999 and in kHZ past it
	static constexpr double FREQ_C4 = 261.6256; // nought on FREQ, as nought volts is on a V/OCT
	static double freqKept(double f) { return std::min(std::max(f, FREQ_MIN), FREQ_MAX); }
	/** The pitch: the setting, bent by the V/OCT jack a volt a doubling
	(`tempoGain`, the BPM jack's own arithmetic), what the readout names. */
	double pitch() { return freqKept(FREQ_C4 * std::exp2(freqGlide.playedFine(kept(FREQ_PARAM))) * tempoGain()); }
	/** The setting the engine plays, in octaves, gliding to the param's
	(a stroke would otherwise be heard as steps) along every dragged setting's line (`rig::Glide`). A jump, a setting at
	least `JUMP_OCT` from the last one (a shift step's octave, a double
	click home, a typed value), is reached over the glide's `SNAP_SECONDS`
	instead, the softening kept and the swoop gone. A mode switched into hertz, the first sample or
	a RIG's recall stands on the setting at once. The engine's own. */
	static constexpr float JUMP_OCT = 0.5f;
	Glide freqGlide{JUMP_OCT};
	/** PHASE in, in hertz: the ticks go on
	at the pitch, forward only as a writer's pulses must, and the jack is a
	turn beside them, ten volts one whole cycle of the Clock, minus ten one
	back, which every Seq the Clock drives adds to its walk as its own PHASE
	adds its turn (`Seq::clockTurn`), and the PHASE jack's ramp is drawn from
	the cycle and the turn together. Read every sample, so a sine at audio
	rate is phase modulation, through nought by its nature. A cable pulled
	leaves the turn where it stood, its last volts kept in the base, so the
	cycle never jumps; a reset and a change of mode put the base at
	nought, the live volts still turning it. Kept as whole cycles and a
	fraction nought up to one, the whole an integer so it never runs out of
	bits. One law, running or not: running, it modulates the cycle; stopped, the cycle stands and
	PHASE is all that moves the bank, so a ramp from outside drives it, and
	after a RESET while stopped, the cycle at nought, its volts are the
	bank's place. Plain fields. */
	int64_t turnWhole = 0;
	double turnFrac = 0.0;
	int64_t phBaseWhole = 0;
	double phBaseFrac = 0.0;
	float phHeld = 0.f; // the jack's volts, last read
	bool phOn = false;  // and whether a cable was in then
	static void wholeAndFrac(double t, int64_t& whole, double& frac) {
		const double w = std::floor(t);
		whole += (int64_t)w;
		frac = t - w;
	}
	void turnSet() {
		turnWhole = phBaseWhole;
		wholeAndFrac(phBaseFrac + (phOn ? (double)phHeld / 10.0 : 0.0), turnWhole, turnFrac);
	}
	void phaseZero() {
		phBaseWhole = 0;
		phBaseFrac = 0.0;
		turnSet();
	}
	void phaseRead() {
		if (inputs[RUN_INPUT].isConnected()) {
			phHeld = clamp(inputs[RUN_INPUT].getVoltage(), -10.f, 10.f);
			phOn = true;
		}
		else if (phOn) {
			wholeAndFrac(phBaseFrac + (double)phHeld / 10.0, phBaseWhole, phBaseFrac);
			phHeld = 0.f;
			phOn = false;
		}
		turnSet();
	}
	/** The PHASE jack in hertz, each sample: nought to ten volts once a
	cycle, where the bank stands, PHASE in's turn included, so it is the cycle
	the Seqs walk. Stopped, the cycle stands where it stopped, or at nought
	after a reset, and the jack moves with PHASE in alone. Band-limited, a
	wrap stands where the place crossed its whole between the last sample and
	this, forwards or, under PHASE through nought, back, a fall of ten volts
	or a rise; a jump, a reset, is the difference on the frame. `cycle` is
	where the cycle stands at this sample's end (`cycleAt`), and `turning`
	whether it is moving on its own. */
	void phaseOut(double cycle, bool turning) {
		const bool home = resets != rampResets;
		const double ph = cycle + turnFrac; // the whole cycles, the turn's among them, change nothing
		const double total = ph + (double)turnWhole;
		// Stopped, PHASE in's turn of exactly whole cycles forward from a
		// cycle's start stands at the end of the cycle, ten volts, as the
		// Seqs of the bank stand at the end of theirs (`Seq::place`, PHASE's
		// closed top): the floor
		// one lower there, exactly.
		const bool closedTop = !turning && turnFrac == 0.0 && turnWhole > 0 && cycle == std::floor(cycle);
		const double floorAt = std::floor(total) - (closedTop ? 1.0 : 0.0);
		const float ramp = closedTop ? 10.f : (float)((ph - std::floor(ph)) * 10.0);
		if (blepWork) {
			const double wraps = floorAt - rampFloor;
			const double moved = total - rampPrev;
			const bool flowing = rampFlowed && !home && std::abs(moved) < 1.0;
			if (flowing && (wraps == 1.0 || wraps == -1.0)) {
				const double c = moved > 0.0 ? floorAt : rampFloor;
				const float p = (float)std::min(std::max((c - rampPrev) / moved - 1.0, -1.0), 0.0);
				rampBlep.insert(p, (float)(-10.0 * wraps));
			}
			else if (!flowing && ramp != rampOut)
				rampBlep.insert(0.f, ramp - rampOut);
		}
		rampOut = ramp;
		rampPrev = total;
		rampFloor = floorAt;
		rampFlowed = true;
		rampResets = resets;
		outputs[RUN_OUTPUT].setVoltage(rampOut + (blepWork ? rampBlep.process() : 0.f));
	}
	/** Where the cycle stands at `f`'s end for the PHASE jack: on its own,
	where it is running; stopped, where it stood, nought where a reset has
	armed the start; following a cable, where the cable's beats and the ramp
	between them have it (`followCycle`). */
	double cycleAt(int64_t f, bool following, bool turning) const {
		if (following)
			return followCycle(f);
		return turning ? beatsAt(f + 1) : armedStart ? 0.0 : anchorBeats;
	}
	bool hzWas = false;
	/** One tempo: a clock cable
	pulled leaves the Clock at the tempo it last heard, the setting that makes
	it under the BPM jack's gain posted into TEMPO (`postWrite`), which joins
	the cable's own undo step, so one undo puts the cable and the setting back
	together. Read in the param's place until the write lands; the param
	moving off where it stood at the pull, the write landing or a hand, ends
	that. Taken at the pull alone, never beat by beat while following, which
	would push an undo step a beat.
	In hertz the same law on FREQ: the cycle heard, in octaves from C4
	under the V/OCT jack's gain, the glide standing on it at once, so the
	pitch goes on unbroken. `keptId` names the param kept, minus one none. */
	int keptId = -1;
	float keptSet = 0.f, keptFrom = 0.f;
	void keepHeard(float sampleRate, bool hzMode) {
		if (beatPeriod <= 0.0)
			return;
		if (hzMode) {
			const double heard = freqKept(sampleRate / beatPeriod / tempoGain());
			keptId = FREQ_PARAM;
			keptSet = clamp((float)std::log2(heard / FREQ_C4), paramQuantities[FREQ_PARAM]->getMinValue(),
			                paramQuantities[FREQ_PARAM]->getMaxValue());
			freqGlide.jump(keptSet);
		}
		else {
			const double heard = 60.0 * sampleRate / (beatPeriod * ppqn);
			keptId = TEMPO_PARAM;
			keptSet = (float)tempoKept(heard / tempoGain());
		}
		keptFrom = params[keptId].getValue();
		postWrite(keptId, keptSet, hzMode ? "keep the frequency" : "keep the tempo");
	}
	/** The setting of `id` in force: the param, or what a pull kept until
	the write lands or a hand moves it. */
	float kept(int id) {
		const float p = params[id].getValue();
		if (keptId >= 0 && params[keptId].getValue() != keptFrom)
			keptId = -1;
		return keptId == id ? keptSet : p;
	}
	float setting() { return kept(TEMPO_PARAM); }
	/** The tempo in force: the readout's setting, bent by the BPM CV jack a
	volt a doubling (`tempoGain`). */
	double tempo() { return bent(setting(), tempoGain()); }
	Clock() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configSwitch<WithHint<SwitchQuantity>>(RUN_PARAM, 0.f, 1.f, 1.f, "Run", {"Stopped", "Running"});
		configInput(RUN_INPUT, "Run");
		configOutput(RUN_OUTPUT, "Run");
		configParam<TempoQuantity>(TEMPO_PARAM, (float)TEMPO_MIN, (float)TEMPO_MAX, 120.f, "Tempo", " bpm");
		static_cast<TempoQuantity*>(getParamQuantity(TEMPO_PARAM))->clock = this;
		retipParam(this, TEMPO_PARAM, {"Tempo", "", Gestures{&G_CLOCK_TENS}});
		configParam<StepRateQuantity>(RATE_PARAM, (float)RATE_MIN, (float)RATE_MAX, 4.f, "Rate");
		configParam<StepRateQuantity>(RATE_HZ_PARAM, (float)RATE_MIN, (float)RATE_MAX, 1.f, "Rate");
		configInput(RESET_INPUT, "Reset");
		configInput(BPM_INPUT, "Beats per minute");
		configInput(CLOCK_INPUT, "External clock");
		retipIn(this, CLOCK_INPUT, {"External clock", string::f(CLOCK_JACK_TIP, "").c_str()});
		configInput(RATE_INPUT, "Rate");
		retipIn(this, RATE_INPUT, {"Rate", RATE_JACK_TIP});
		configOutput(CLK_OUTPUT, "Clock");
		configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Variant", {"BPM", "Hz"});
		configParam<FreqQuantity>(FREQ_PARAM, (float)std::log2(FREQ_MIN / FREQ_C4),
		                          (float)std::log2(FREQ_MAX / FREQ_C4), 0.f, "Frequency", " Hz");
		static_cast<FreqQuantity*>(getParamQuantity(FREQ_PARAM))->clock = this;
		retipParam(this, FREQ_PARAM, {"Frequency", "", Gestures{&G_CLOCK_OCTAVES, &G_CLOCK_CENTS}});
	}

	/** Randomize rolls the tempo and the division, never RUN, which would
	stop the rig one try in two, nor the variant. */
	bool rolled(int p) override { return p != RUN_PARAM && p != MODE_PARAM; }

	/** The rate's words, `lesson`, on both modes' rate params, so the one out
	of force never keeps the other mode's; the rate a jack puts in force,
	`now` from `from`, is said on the one in force alone. */
	void retipRates(const std::string& lesson, int now, const std::string& from) {
		for (int p : {(int)RATE_PARAM, (int)RATE_HZ_PARAM}) {
			retipRateInForce(this, p, p == rateParam() ? now : rateParamOf(this, p), from);
			retipParam(this, p, {"Rate", lesson.c_str(), rateGestures()});
		}
	}

	/** What the panel is doing now: a clock of its own, or one following a
	cable. The tempo readout is set by hand in the first and worked out from
	the cable in the second, and the BPM jack has no tempo to bend in the
	second, so it is the division there instead. */
	void retipAll() override {
		// Who stands beside me, as my widget saw the run (`seqsRightUi`): a
		// tooltip names a SEQ only where one stands, the next CLK only where
		// there is one, and an empty RESET's neighbour only while it is empty.
		// A SEQ taking my count (`readNow`, the parse's word, which holds
		// with no widget too) stands in my reach too.
		const bool seqs = relaxed(seqsRightUi) || relaxed(readNow);
		const bool right = relaxed(clockRightUi);
		const std::string resetEmpty =
		    !inputs[RESET_INPUT].isConnected() && relaxed(clockLeftUi) ? " Empty, it resets with the CLK to its left." : "";
		if (hz()) {
			// A cable in CLK is the cycle, its pitch the gap between two
			// rising edges, as it is the tempo in BPM, and a pull leaves the
			// frequency it last brought (`keepHeard`).
			const bool following = follows();
			const std::string bentTip =
			    tempoGain() != 1.0
			        ? string::f("Set at %.7g Hz; the V/OCT jack moves it.", FREQ_C4 * std::exp2(params[FREQ_PARAM].getValue()))
			        : std::string();
			retipParam(this, FREQ_PARAM, {"Frequency", following ? "Set by the CLK input." : bentTip.c_str(),
			           following ? Gestures{} : Gestures{&G_CLOCK_OCTAVES, &G_CLOCK_CENTS}});
			// The rate in force, where the RATE jack or, following, the
			// V/OCT jack moves it, is said after the value
			// (`retipRateInForce`), the screen keeping the setting.
			const RateIn in = rateIn();
			const int swept = rateNow(in);
			const bool byRate = in.rate() != rateSet(), byOct = swept != in.rate();
			const std::string divTip = seqs ? "Ticks per cycle, each clocking the SEQs." : "Ticks per cycle.";
			retipRates(divTip + " " + rateModeLesson(rateFreeOf(this, rateSet()), "cycle"), swept,
			           byRate && byOct ? "the RATE and V/OCT jacks" : byOct ? "the V/OCT jack" : "the RATE jack");
			retipIn(this, RATE_INPUT, {rateJackName(rateFreeOf(this, rateSet())), rateFreeOf(this, rateSet()) ? RATE_JACK_FREE_TIP : RATE_JACK_TIP});
			retipIn(this, BPM_INPUT, {"Volts per octave",
			        following ? "+1 V doubles the ticks, -1 V halves them: the frequency is the cable's."
			                  : "+1 V an octave up from the frequency on the screen."});
			const char* const keptHz = following && relaxed(heardNow) > 0.0 ? ", which stays when the cable is pulled" : "";
			retipIn(this, CLOCK_INPUT, {"External clock",
			        string::f("Each cycle of an oscillator or clock here sets the frequency%s.", keptHz).c_str()});
			// The sync goes unsaid where the empty jack's neighbour must be
			// said beside the SEQs, the lesson kept to its budget: the
			// SEQs and the neighbour are this rig's, a reset at audio rate
			// being a sync anywhere.
			const bool sync = !(seqs && !resetEmpty.empty());
			const std::string resetTip =
			    std::string(seqs ? (right ? "Restarts the cycle and sends the SEQs to its right, up to the next CLK, back to their first step"
			                              : "Restarts the cycle and sends the SEQs to its right back to their first step")
			                     : "Restarts the cycle") +
			    (!sync ? "." : seqs ? "; at audio rate it is a sync." : "; at audio rate, a sync.") + resetEmpty;
			retipIn(this, RESET_INPUT, {"Reset", resetTip.c_str()});
			// PHASE in's law, then the one surprise in force: stopped, the
			// jack alone moves the cycle (`phaseOut`), so a ramp drives it;
			// running with a cable in, a pull leaves the shift where it
			// stood (`phaseRead`). One of the two, the stopped one first.
			const bool stopped = !running();
			const std::string phaseTip =
			    std::string(seqs ? "Shifts the cycle and the SEQs it clocks: +10 V is one whole cycle ahead, negative volts move it back."
			                     : "Shifts the cycle: +10 V is one whole cycle ahead, negative volts move it back.") +
			    (stopped ? (seqs ? " Stopped, a ramp here drives them alone." : " Stopped, a ramp here drives it alone.")
			     : inputs[RUN_INPUT].isConnected() ? " Pulled, the shift stays where it stood."
			                                       : "");
			retipIn(this, RUN_INPUT, {"Phase", phaseTip.c_str()});
			const bool phased = inputs[RUN_INPUT].isConnected();
			retipOut(this, RUN_OUTPUT, {"Phase",
			         relaxed(blep) ? (phased ? "A rising ramp once a cycle, moved by the PHASE input too; band-limited."
			                                 : "A rising ramp once a cycle; band-limited.")
			                       : (phased ? "A rising ramp once a cycle, moved by the PHASE input too."
			                                 : "A rising ramp once a cycle.")});
			retipOut(this, CLK_OUTPUT, {"Clock",
			         relaxed(blep) ? "High for the first half of each tick; band-limited."
			                       : "High for the first half of each tick."});
			retipRun();
			return;
		}
		const int pq = relaxed(ppqnSet);
		// A clock heard, a pull leaves the tempo it brought (`keepHeard`),
		// said while the cable is in and has been heard.
		const char* const kept = follows() && relaxed(heardNow) > 0.0 ? ", which stays when the cable is pulled" : "";
		if (pq > 1) {
			const std::string inTip = string::f("%d triggers a beat set the tempo%s.", pq, kept);
			const std::string outTip = string::f("%d pulses a beat at the tempo, whatever the rate.", pq);
			retipIn(this, CLOCK_INPUT, {"External clock", inTip.c_str()});
			retipOut(this, CLK_OUTPUT, {"Clock", outTip.c_str()});
		}
		else {
			const std::string inTip = string::f(CLOCK_JACK_TIP, kept);
			retipIn(this, CLOCK_INPUT, {"External clock", inTip.c_str()});
			retipOut(this, CLK_OUTPUT, {"Clock", "High for the first half of each tick."});
		}
		retipOut(this, RUN_OUTPUT, {"Run", "High while it runs."});
		const bool following = follows();
		// What is in force where a jack moves it off the setting the hover
		// names, as the screens show it. The hover's value is the tempo in
		// force, as the readout's is, and with the BPM jack bending it the
		// setting under it is named here.
		const float set = params[TEMPO_PARAM].getValue();
		// The shift stroke's hint stands whenever a stroke moves the tempo.
		const std::string bentTip = tempoGain() != 1.0
		                                ? string::f("Set at %.5g bpm; the BPM jack moves it.", set)
		                                : std::string();
		retipParam(this, TEMPO_PARAM, {"Tempo",
		           following ? "Set by the CLK input." : bentTip.c_str(),
		           following ? Gestures{} : Gestures{&G_CLOCK_TENS}});
		// The rate in force where a jack moves it, said after the value
		// with the jack that does (`retipRateInForce`): the RATE jack, and
		// following a cable the BPM jack too, in whole octaves (`rateNow`).
		const RateIn in = rateIn();
		const int swept = rateNow(in);
		const bool byRate = in.rate() != rateSet(), byBpm = swept != in.rate();
		const std::string divTip = seqs ? "Ticks per beat, each clocking the SEQs." : "Ticks per beat.";
		retipRates(divTip + " " + rateModeLesson(rateFreeOf(this, rateSet()), "beat"), swept,
		           byRate && byBpm ? "the RATE and BPM jacks" : byBpm ? "the BPM jack" : "the RATE jack");
		retipIn(this, RATE_INPUT, {rateJackName(rateFreeOf(this, rateSet())), rateFreeOf(this, rateSet()) ? RATE_JACK_FREE_TIP : RATE_JACK_TIP});
		// Following, the jack moves the ticks by octaves, the law it obeys
		// there said in its lesson; its name is its caption spelt out.
		retipIn(this, BPM_INPUT, {"Beats per minute",
		        following ? "+1 V doubles the ticks, -1 V halves them: the tempo is the cable's."
		                  : "+1 V doubles the tempo, -1 V halves it."});
		// A reset here is not a reset of the sequence, which is worth one
		// line once there is a sequence to be surprised by it; with none, what
		// it does to the beat (`restart`): following a cable the next trigger
		// is the first beat, running alone the beat starts again with a tick
		// at once, and stopped RUN starts it with one.
		const std::string resetTip =
		    std::string(seqs ? (right ? "Sends the SEQs to its right, up to the next CLK, back to their first step."
		                              : "Sends the SEQs to its right back to their first step.")
		                : following ? "The next trigger in CLK starts the beat afresh."
		                : running() ? "Starts the beat afresh, with a tick at once."
		                            : "Starts the beat afresh; RUN then begins it with a tick.") +
		    resetEmpty;
		retipIn(this, RESET_INPUT, {"Reset", (resetTip[0] == ' ' ? resetTip.substr(1) : resetTip).c_str()});
		retipRun();
	}

	/** A running Clock's oscillator moves on its own, and one following a
	cable watches every sample for its triggers. A stopped clock with nothing
	in its clock jack has nothing inside it that moves, so it leaves the list
	and costs the rig nothing at all. */
	bool wantsTick() override {
		return running() || follows() || clkBlep.left > 0 || rampBlep.left > 0 ||
		       (hz() && (inputs[RUN_INPUT].isConnected() || phOn));
	}

	/** Makes the ticks from the next up to tick `upTo`, all on frame `f`:
	one, or several where a change of division on an early beat jumped. */
	void make(int64_t upTo, int64_t f) {
		if (upTo < next)
			return;
		const int64_t n = upTo - next + 1;
		pulses += (uint32_t)n;
		next = upTo + 1;
		lastPulseFrame = f;
		lastAfter = 0.0;
	}

	// ---- The oscillator.
	double beatsAt(int64_t f) const { return anchorBeats + ((double)f - anchorTime) / beatFrames; }
	/** Whether beat `b`, or tick `j`, has fallen by frame `f`: each falls on
	the floor of its time, worked out from the anchor. */
	bool beatDue(int64_t b, int64_t f) const {
		return anchorTime + ((double)b - anchorBeats) * beatFrames < (double)f + 1.0;
	}
	bool tickDue(int64_t j, int64_t f) const { return tickAt(j) < (double)f + 1.0; }
	/** The time tick `j` falls at, a frame and a fraction. */
	double tickAt(int64_t j) const {
		int n, d;
		ratioOfRate(rateLive, n, d);
		// In 128 bits: a millionth's denominator is near a million, and ticks
		// at 20 kHz times x255 would pass 64 bits in about three weeks.
		const double at = (double)yBase + yFrac - anchorBeats + (double)((__int128)(j - tBase) * d) / (double)n;
		return anchorTime + at * beatFrames;
	}
	/** Starts the grid afresh with tick `first` on a new whole beat, at the
	anchor, which is `at`. */
	void regrid(double at, int64_t first) {
		const int64_t K = nextBeat;
		anchorTime = at;
		anchorBeats = (double)K;
		yBase = K;
		yFrac = 0.0;
		tBase = first;
		nextBeat = K + 1;
		// The CLK jack's pulses past 1 start on the same beat, the one there
		// made or not as the tick there is.
		ppqNext = K * ppqn + (first < next ? 1 : 0);
	}
	/** A new division based on beat `b`: the tick on the beat, or, with a
	slow tick part way through there, the next, which ends it on the beat. In
	integers, so whether the beat stands on a tick is decided exactly. */
	void rebaseOnBeat(int64_t b, int want) {
		int n, d;
		ratioOfRate(rateLive, n, d);
		if (yFrac != 0.0) // off the grid since a free rate's FM: the tick under way ends on the beat
			tBase = tBase + (int64_t)std::ceil(((double)(b - yBase) - yFrac) * n / d);
		else {
			const __int128 q = (__int128)(b - yBase) * n; // 128 bits, as `tickAt`
			tBase = tBase + (int64_t)(q / d) + (q % d != 0 ? 1 : 0);
		}
		yBase = b;
		yFrac = 0.0;
		rateLive = want;
	}
	/** A free rate is FM, nothing in it quantized any more:
	a change of division takes effect at once, the tick under way keeping the
	part of it already travelled and the rest going at the new rate, so the
	ticks are the integral of the rate the jack makes and a step's length is
	the rate in force while it plays, never a beat's remainder. The grid is
	left: the base is the beat where the last tick stood, fraction and all
	(`yFrac`), until a whole rate's change on a beat or a regrid puts it back. */
	void fmRebase(int64_t f, int want) {
		int n, d;
		ratioOfRate(rateLive, n, d);
		const double beat = beatsAt(f);
		const double pos = (double)tBase + (beat - (double)yBase - yFrac) * n / d;
		const double frac = std::min(std::max(pos - (double)(next - 1), 0.0), 1.0);
		rateLive = want;
		ratioOfRate(rateLive, n, d);
		const double y = beat - frac * d / n;
		yBase = (int64_t)std::floor(y);
		yFrac = y - (double)yBase;
		tBase = next - 1;
	}
	/** Its own tempo, this sample: stopped it stands, and a change of
	division is made at once, the tick part way through keeping its
	fraction; RUN makes a tick at once where a reset armed it and otherwise
	goes on from where it stood; a change of tempo re-anchors where it
	stands; a change of division waits for the beat, or takes one just gone
	inside the window; and every tick due by now is made. A free rate
	(`free`) takes its change on the sample it arrives (`fmRebase`). */
	void oscillate(int64_t f, bool run, double B, int want, bool free) {
		if (!run) {
			if (ownRunning)
				anchorBeats = beatsAt(f);
			ownRunning = false;
			if (want != rateLive) {
				if (armedStart)
					rateLive = want;
				else {
					int n, d;
					ratioOfRate(rateLive, n, d);
					const double t = (double)tBase + (anchorBeats - (double)yBase - yFrac) * n / d;
					const double frac = std::min(std::max(t - (double)(next - 1), 0.0), 1.0);
					rateLive = want;
					ratioOfRate(rateLive, n, d);
					const int64_t K = nextBeat;
					yBase = K;
					yFrac = 0.0;
					tBase = next - 1;
					anchorBeats = (double)K + frac * d / n;
					nextBeat = (int64_t)std::floor(anchorBeats) + 1;
					// The beats moved under a tick kept whole: the CLK
					// jack's pulses past 1 go on from there, none owed.
					ppqNext = std::max(ppqNext, (int64_t)std::ceil(anchorBeats * ppqn));
				}
			}
			beatFrames = B;
			return;
		}
		if (!ownRunning) {
			beatFrames = B;
			// In hertz as in BPM, RUN goes on from where the cycle stood,
			// a pause ended; RESET is the way to step one.
			if (armedStart) {
				rateLive = want;
				regrid((double)f, next);
				lastBeatFrame = f;
				armedStart = false;
			}
			else
				anchorTime = (double)f;
			ownRunning = true;
		}
		else if (B != beatFrames) {
			anchorBeats = beatsAt(f);
			anchorTime = (double)f;
			beatFrames = B;
		}
		if (free && want != rateLive)
			fmRebase(f, want);
		while (beatDue(nextBeat, f)) {
			if (want != rateLive)
				rebaseOnBeat(nextBeat, want);
			lastBeatFrame = f;
			nextBeat++;
		}
		// A change of division inside the window after a beat is that
		// beat's: the grid is rebased on it, and a tick the new division puts
		// on the beat is sent as the beat's, so the Seqs land it there (one
		// pulse, one decision).
		bool onBeat = false;
		if (want != rateLive && lastBeatFrame >= 0 && f - lastBeatFrame <= window) {
			rebaseOnBeat(nextBeat - 1, want);
			onBeat = true;
		}
		int64_t upTo = next - 1;
		while (tickDue(upTo + 1, f))
			upTo++;
		const int64_t was = next;
		make(upTo, onBeat ? lastBeatFrame : f);
		// In hertz a tick falls part way through the frame it is made on, and
		// a Seq's ramp and its band-limited edge start from where (`Count::after`).
		if (hz() && next > was && lastPulseFrame == f)
			lastAfter = std::min(std::max((double)f + 1.0 - tickAt(upTo), 0.0), 1.0);
	}

	// ---- Following a cable.
	/** Where the ramp stands at `f`'s end, in ticks: it runs over the last
	gap and stops at the last frame's end before the next trigger is due,
	which it never reaches, so a cable that slows holds the ramp short of
	the beat's whole until its trigger comes. In BPM that is a sample short,
	the gap being whole frames; in hertz the same rule to the part of a frame. */
	double rampAt(int64_t f) const {
		if (halted)
			return haltedAt;
		if (!(beatPeriod > 0.0) || beatFrame < 0)
			return rampFrom;
		const double since = (double)(f + 1) - beatTime;
		const double lastBefore = std::ceil(beatTime + beatPeriod) - 1.0 - beatTime;
		const double x = std::min(since, lastBefore) / beatPeriod;
		return rampFrom + span * std::max(x, 0.0);
	}
	/** The time tick `j` of the journey under way falls at, in frames, for
	where a tick made by the ramp stands in its frame in hertz
	(`Count::after`). */
	double rampTickTime(int64_t j) const {
		if (!(beatPeriod > 0.0) || !(span > 0.0))
			return beatTime;
		return beatTime + ((double)j - rampFrom) / span * beatPeriod;
	}
	/** Where the cycle stands following a cable, for the PHASE jack in
	hertz: the beats landed and the ramp's part of the journey to the next,
	nought while a start is awaited, as a Clock of its own stands at nought
	after a reset. */
	double followCycle(int64_t f) const {
		if (waiting)
			return 0.0;
		const double x = span > 0.0 ? (rampAt(f) - rampFrom) / span : 0.0;
		return (double)cycWhole + std::min(std::max(x, 0.0), 1.0);
	}
	/** Where a beat at frame `f` cuts a multiplied journey: the whole after
	the ramp, the ticks it had not reached dropped, as a Seq on the same cable
	has it. Worked out as that Seq works it, from the tick the last beat
	landed and the time since at the tempo it ran at, never from the ticks
	made: a pause RUN held while the cable shared it, or ticks still owed from
	one, leave the ticks made behind, and the Seq on the cable, which has no
	RUN, ran on through it; those ticks then come over the next beat
	(`landBeat`). With no tempo yet there is no ramp, and the whole after it
	is the next tick; the beat that brings the first tempo then owes the
	ticks that journey's ramp would have made, paid over the next beat, as
	the Seq on the cable pays them (`rig::owedWholes`). Asked before the
	beat's gap is taken as the tempo. */
	Cut cutAt(int64_t f) const {
		int n, d;
		ratioIn(n, d);
		if (d != 1 || beatFrame < 0)
			return {next, 0};
		int64_t k = 0;
		if (beatPeriod > 0.0)
			k = std::min((int64_t)n - 1,
			             (int64_t)std::floor(std::max((double)f - beatTime, 0.0) * n / beatPeriod));
		const bool untimed = !(beatPeriod > 0.0) && beatIn.period > 0;
		return {std::max(fw + k + 1, next), owedWholes(untimed, (int64_t)n - 1, k)};
	}
	/** The journey from the landing to the next beat's: its span from where
	the ramp starts, and the last tick it may make before that beat. */
	void journey() {
		int n, d;
		ratioIn(n, d);
		const int64_t u = fs + n + owed * d;
		const int64_t tw = fw + u / d, tr = u % d;
		span = (double)tw + (double)tr / d - rampFrom;
		capTick = tr == 0 ? tw - 1 : tw;
	}
	/** A beat's landing on the ticks: a change of division lands on a whole
	tick, a slow tick part way through ended on the beat, and is the journey
	from there; multiplying or at x1, the beat lands `cut`, the tick after
	the ramp (`cutAt`), the ticks the ramp had not reached dropped or, the
	ramp having had no tempo, owed; and
	any landing that would make more than one tick at once, ticks owed after a
	pause RUN held, goes on from where the ramp stood, so they come over the
	next beat, never burst all on this frame. The ticks it makes are sent as
	frame `at`'s. */
	void landBeat(int want, int64_t at, double stood, Cut cut) {
		int n, d;
		ratioIn(n, d);
		const bool change = want != rateLive;
		owed = 0;
		if (d == 1)
			fw = cut.to;
		else {
			fs += n;
			fw += fs / d;
			fs %= d;
		}
		if (change && fs != 0) {
			fw++;
			fs = 0;
		}
		if (fw > next)
			rampFrom = stood;
		else {
			make(fw, at);
			rampFrom = (double)fw + (double)fs / d;
		}
		if (change)
			rateLive = want;
		ratioIn(n, d);
		if (d == 1)
			owed = cut.owed;
	}
	/** Following, this sample. A trigger is a beat: the first after the cable
	came, a reset or nothing at all is a start, the tick it stands on sounded;
	the first after RUN came back, where the cable stopped with it and carried
	no beat while it was off, lands as any other does, from the grid at or
	below where the ramp stood, since that stop was a pause the cable shared
	and the beat is the end of the tick it stopped in, as a Clock on its own
	tempo finishes that tick and a Seq on the same cable lands it (else a rig
	following a Clock's CLK and RUN comes in a tick late after every restart);
	where the cable ran on through the stop, the first beat after RUN goes on
	from the grid at or below where the ramp stood, nothing made twice, as a
	Seq's HALT does, since those beats' ticks were never the Clock's to make;
	any beat lands its beat's ticks, a change of division taking effect there
	with a slow tick part way through ended on the beat, and a beat that came
	before the ramp had made them, the Clock multiplying, cuts the journey at
	the tick after the ramp (`landBeat`), a beat with no tempo yet to ramp at
	making one tick. Every gap is the tempo, however long, save the gap a
	reset fell in. */
	void follow(int64_t f, bool run, int want, bool edge) {
		if (!run && !halted) {
			haltedAt = rampAt(f);
			halted = true;
			beatInHalt = false;
			haltBeat.frame = -1;
		}
		// RUN back inside the window after a beat it missed: that beat is
		// RUN's, as if the two had come together (one pulse, one decision).
		// Taken as a beat while stopped, the first beat after RUN would begin
		// a journey with no tick, and a Clock following a DAW's clock and
		// RUN, its RUN a few samples late, would stand two steps behind a Seq
		// on the same clock for good.
		if (run && halted && !edge && haltBeat.frame >= 0 && f - haltBeat.frame <= window) {
			beatInHalt = haltBeat.inHaltBefore;
			beat(haltBeat.frame, haltBeat.after, want, haltBeat.stood, haltBeat.cut);
			// What the ramp has made since is that beat's, sent as its frame's.
			make(std::min((int64_t)std::floor(rampAt(f)), capTick), haltBeat.frame);
		}
		if (run)
			haltBeat.frame = -1;
		if (edge) {
			phaseIn = (phaseIn + 1) % ppqn; // a start puts it at nought (`beat`)
			const double stood = rampAt(f);
			const double gap = gapIn();
			const double part = hzWas ? beatIn.after : 0.0;
			const Cut cut = cutAt(f);
			beatPeriodBefore = beatPeriod;
			if (gap > 0.0)
				beatPeriod = gap;
			if (run)
				beat(f, part, want, stood, cut);
			else {
				haltBeat = {f, part, beatInHalt, stood, cut};
				beatInHalt = true;
			}
		}
		// A change of division inside the window after a beat is that
		// beat's decision made again, as a Seq's rate is: the beat lands
		// afresh from where it stood before it, at the new division, a tick
		// it then makes sent as the beat's (one pulse, one decision).
		else if (run && !halted && !waiting && beatOpen && beatOnGrid && want != rateLive && f - beatFrame <= window) {
			fw = beatPre.fw;
			fs = beatPre.fs;
			rateLive = beatPre.rate;
			landBeat(want, beatFrame, beatPre.stood, beatPre.cut);
			journey();
		}
		// A beat that was a start, a fresh cable's first or a RESET's, takes
		// a change of division inside its window as a start with it takes it:
		// the journey from the start's tick at the new division, so a
		// division a few samples after a take's first pulse does not wait a
		// beat.
		else if (run && !halted && !waiting && startOpen && want != rateLive && f - beatFrame <= window) {
			rateLive = want;
			journey();
		}
		if (run && !halted && !waiting)
			make(std::min((int64_t)std::floor(rampAt(f)), capTick), f);
	}
	/** A beat heard while running, at frame `at`, `part` of it after the
	edge in hertz (`follow`). */
	void beat(int64_t at, double part, int want, double stood, Cut cut) {
		int n, d;
		ratioIn(n, d);
		startOpen = false;
		owed = 0;
		cycWhole = waiting ? 0 : cycWhole + 1;
		if (waiting) {
			beatOpen = false;
			startOpen = true;
			phaseIn = 0;
			rateLive = want;
			fw = next;
			fs = 0;
			make(fw, at);
			rampFrom = (double)fw;
			waiting = false;
			halted = false;
		}
		else if (halted && beatInHalt) {
			beatOpen = false;
			const int64_t g = (int64_t)std::floor(haltedAt * d);
			fw = g / d;
			fs = g % d;
			if (fs == 0)
				rateLive = wantOnGrid(want);
			rampFrom = (double)fw + (double)fs / d;
			halted = false;
		}
		else {
			// The first beat after a pause the cable shared ends the beat the
			// stop was in, so it lands from that beat's own grid, never from
			// where the ramp stood: landed from the grid at or below the
			// ramp, a whole beat added to a beat part walked, a multiplying
			// follower would stand a tick or two ahead of a Seq on its cable
			// for good after every restart. The ticks the stop held back come
			// over the next beat, never all on this frame (`cutAt`,
			// `landBeat`).
			halted = false;
			beatPre = {fw, fs, rateLive, stood, cut};
			beatOpen = true;
			landBeat(wantOnGrid(want), at, stood, cut);
		}
		beatOnGrid = phaseIn == 0;
		beatFrame = at;
		beatAfter = part;
		beatTime = (double)at + 1.0 - part;
		journey();
	}

	bool runningWork = false; // what `Count::running` says: running, and following, not waiting on a start

	/** The CLK jack, each sample, once the tick is made. */
	void pulseOut(int64_t f, bool run, bool following, float sampleRate) {
		// High for the first half of every tick, the tick as it
		// is being made (a squeezed tick is shorter), and a trigger's
		// millisecond where a cable has carried one trigger and so no tempo.
		// A tick that begins while the pulse is already high, a RESET part
		// way through the high half, drops it for one sample and rises on the
		// next, so whatever is clocked from this jack still sees the edge.
		// And only from a tick made while it ran: a stop lets the jack fall,
		// and a run resumed part way through a tick waits for the next one.
		// Measured from the tick, so a cable that stops part way through a
		// slow tick lets it fall after half a tick, never held at 10 V for
		// good. The jack rises on a tick and on nothing else: the half is
		// worked out afresh every sample, so a tempo lowered after the jack
		// fell, or a beat squeezed, lengthens it, and without that rule it
		// would rise again with no tick, a phantom edge a Seq on this cable
		// counts as a step the rig never heard. Once fallen it stays down
		// until the next tick.
		// Past 1 pulse a beat it is the same jack on the PPQ pulses rather
		// than the ticks: on the cable's own pulses following one, else
		// `ppqn` a beat on the oscillator, high half the gap.
		const bool ppq = ppqn > 1;
		const uint32_t made = ppq ? ppqPulses : pulses;
		const int64_t from = ppq ? ppqFrame : lastPulseFrame;
		const bool pulsedNow = made != jackPulses;
		jackPulses = made;
		if (!run)
			jackArmed = false;
		else if (pulsedNow)
			jackArmed = true;
		double tickNow = following ? (beatPeriod > 0.0 && span > 0.0 ? beatPeriod / span : 0.0) : tickPeriod;
		if (ppq)
			tickNow = following ? beatPeriod : beatFrames / ppqn;
		const double highFor = tickNow > 0.0 ? tickNow / 2.0 : (double)pulseFrames(sampleRate);
		// Band-limited, in hertz, the half is measured from where the tick
		// fell inside its frame, so both edges stand where they fell.
		const double tickTime = (double)lastPulseFrame + 1.0 - lastAfter;
		const bool half = blepWork ? lastPulseFrame >= 0 && (double)(f + 1) - tickTime < highFor
		                           : from >= 0 && (double)(f - from) < highFor;
		bool high = false;
		if (pulsedNow) {
			high = !pulseHigh;
			jackOwed = pulseHigh;
		}
		else if (jackOwed) {
			high = true;
			jackOwed = false;
		}
		else
			high = pulseHigh;
		high = high && jackArmed && half;
		// A rise on a tick stands where the tick fell, a fall at the half's
		// end where that fell, and any other edge on the frame.
		if (blepWork && high != pulseHigh) {
			float p = 0.f;
			if (high && pulsedNow)
				p = -(float)lastAfter;
			else if (!high && !pulsedNow && jackArmed)
				p = (float)std::min(std::max(tickTime + highFor - (double)(f + 1), -1.0), 0.0);
			clkBlep.insert(p, high ? 10.f : -10.f);
		}
		pulseHigh = high;
		jackFrame = f;
		outputs[CLK_OUTPUT].setVoltage((high ? 10.f : 0.f) + (blepWork ? clkBlep.process() : 0.f));
	}

	/** The Clock, each sample: RESET first, so a trigger and a RESET together
	are a start, then its own tempo or the cable's beats, the CLK jack, and
	whether the rig must hear of it. */
	bool everySample(const ProcessArgs& args) override {
		const int64_t f = args.frame;
		frameNow = f;
		const bool hzMode = hz();
		// In hertz a tick is a fraction of a millisecond, so nothing is
		// decided again inside a window after it: a reset or a change of the
		// multiplier is its own sample's. Following a cable, though, the
		// window is the one a Seq on that cable keeps, whatever its rate, so
		// a RESET sent to both lands them on the same step.
		window = hzMode && !follows() ? 0 : windowNow.at(args.sampleRate);
		if (hzMode != hzWas) {
			hzWas = hzMode;
			runShown = -1;
			freqGlide.jump(params[FREQ_PARAM].getValue());
			phHeld = 0.f;
			phOn = false;
			phaseZero();
			rampOut = 0.f;
			rampFlowed = false;
			// Following, the cable's gap is counted afresh in the other
			// mode's measure, whole frames or their parts, so its next
			// trigger is a start, as a new count of pulses a beat makes it.
			if (wasFollowing) {
				waiting = true;
				halted = !running();
			}
		}
		if (hzMode)
			freqGlide.step(kept(FREQ_PARAM), args.sampleRate, args.frame, landing());
		const bool run = running();
		const bool following = follows();
		// The RATE jack read once this sample: the rate it leaves, and whether
		// it is free, which on its own tempo takes a change at once.
		const RateIn rateHere = rateIn();
		const int want = rateNow(rateHere);
		if (!rateStarted) {
			rateLive = want;
			rateStarted = true;
		}
		// The menu's two. A new count of pulses a beat, following, makes the
		// next pulse a start, since the journey under way was counted in the
		// old one's pulses.
		const int pq = hzMode ? 1 : relaxed(ppqnSet);
		if (pq != ppqn) {
			ppqn = pq;
			if (following && wasFollowing) {
				waiting = true;
				halted = !run;
			}
			ppqFrom(f);
			jackPulses = ppqn > 1 ? ppqPulses : pulses;
		}
		const bool bw = hzMode && relaxed(blep);
		if (bw != blepWork) {
			blepWork = bw;
			clkBlep.clear();
			rampBlep.clear();
		}
		const uint32_t pulsesBefore = pulses, resetsBefore = resets;
		// The cable leaving: the tempo it last brought is the Clock's own
		// from here (`keepHeard`).
		if (!following && wasFollowing)
			keepHeard(args.sampleRate, hzMode);
		double bpm = tempo();
		// A beat's frames on its own: a tempo's quarter note, or in hertz
		// the cycle.
		const double ownBeat = hzMode ? args.sampleRate / pitch() : 60.0 * args.sampleRate / bpm;
		// The cable arriving: the next trigger is a start, on the tick the
		// count stands at. Leaving: the Clock's own tempo from here, its next
		// tick a period on, nothing made at the pull.
		if (following && !wasFollowing) {
			waiting = true;
			halted = !run;
			beatPeriod = 0.0;
			beatFrame = -1;
			ownRunning = false;
		}
		else if (!following && wasFollowing) {
			rateLive = want;
			regrid((double)f, next - 1);
			lastBeatFrame = f;
			beatFrames = ownBeat;
			ownRunning = run;
			armedStart = false;
		}
		wasFollowing = following;
		const bool edge = beatIn.watch(inputs[CLOCK_INPUT], f);
		// RESET: the Clock's reset, to every Seq of its rig (`restart`).
		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), TRIG_LOW, TRIG_HIGH)) {
			restart();
			// Read before the beat, a RESET with a trigger is a start, so the
			// gap that trigger closed is the one it fell in.
			if (edge && gapSkipped)
				beatIn.resetWith();
		}
		if (following) {
			const uint32_t madeBefore = pulses;
			follow(f, run, want, edge);
			// In hertz a tick the cable's ramp made stands where its time
			// falls inside the frame, the beat's own where the edge crossed,
			// so the CLK jack's edges and every Seq's band-limited step land
			// on the cable's cycle and not on the frame's grid.
			if (hzMode && pulses != madeBefore)
				lastAfter = std::min(std::max((double)lastPulseFrame + 1.0 - rampTickTime(next - 1), 0.0), 1.0);
			if (beatPeriod > 0.0)
				bpm = 60.0 * args.sampleRate / (beatPeriod * ppqn);
			// Past 1 the CLK jack sends the cable's pulses on.
			if (ppqn > 1 && edge && run) {
				ppqPulses++;
				ppqFrame = f;
			}
		}
		else {
			oscillate(f, run, ownBeat, want, rateHere.isFree());
			// Past 1, every pulse due by now on the beats, one edge however
			// many a jump of the grid passed.
			if (ppqn > 1 && run && ownRunning) {
				int64_t q = ppqNext;
				while (ppqAt(q) < (double)f + 1.0)
					q++;
				if (q > ppqNext) {
					ppqNext = q;
					ppqPulses++;
					ppqFrame = f;
				}
			}
		}
		// PHASE's turn, this sample's, after a reset has put its base
		// at nought, so it stands where the cycle does at the sample's end;
		// running or stopped alike.
		if (hzMode)
			phaseRead();
		// The spacing of my ticks in frames, the tempo every Seq in my rig
		// ramps at: a beat's frames over the ticks a beat makes,
		// the beats followed or my own tempo, running or stopped; nought
		// following a cable not yet measured.
		{
			int tn, td;
			if (following)
				ratioIn(tn, td);
			else
				ratioOfRate(rateLive, tn, td);
			const double beatLen = following ? beatPeriod : ownBeat;
			// Following, a journey paying back owed ticks makes them closer.
			tickPeriod = beatLen > 0.0 ? beatLen * td / (tn + (following ? owed : 0) * td) : 0.0;
		}
		runningWork = run && !(following && waiting);
		// The readout is three cells wide, FFF in hex, and a clock in fed
		// sixteenths at speed can name a tempo far past that, so it stops at
		// TEMPO_MAX, the most it holds. Following
		// a cable that has not yet carried two triggers there is no tempo to
		// name, and minus one leaves the readout its ghosts alone. A cable
		// that has stopped carrying triggers names the tempo the Clock keeps,
		// the last gap, which every Seq of its rig still ramps at, so the
		// readout never says no tempo while one is in force. The readout's
		// alone: nothing in time reads it.
		if ((f % CONTROL_PERIOD) == 0) {
			const bool silent = following && (beatPeriod <= 0.0 || beatIn.lastFrame < 0);
			bpmNow.store(silent ? -1 : clamp((int)std::round(bpm), 0, TEMPO_MAX), std::memory_order_relaxed);
			// In hertz the same, the pitch the cable's cycle, unrounded.
			const double heard = hzMode ? args.sampleRate / beatPeriod : bpm;
			heardNow.store(following && !silent ? heard : -1.0, std::memory_order_relaxed);
			bpmGain.store(tempoGain(), std::memory_order_relaxed);
			hzNow.store(!following ? pitch() : silent ? -1.0 : heard, std::memory_order_relaxed);
			ratePatched.store(inputs[RATE_INPUT].isConnected(), std::memory_order_relaxed);
			rateCv.store(clamp(inputs[RATE_INPUT].getVoltage() / 10.f, -1.f, 1.f), std::memory_order_relaxed);
			octPatched.store(following && inputs[BPM_INPUT].isConnected(), std::memory_order_relaxed);
			octCv.store(std::round(clamp(inputs[BPM_INPUT].getVoltage(), -10.f, 10.f)) / 10.f,
			            std::memory_order_relaxed);
		}
		writeRun(run, hzMode);
		if (hzMode) {
			const bool turning = following ? run && !waiting && !halted : run && ownRunning;
			phaseOut(cycleAt(f, following, turning), turning);
		}
		pulseOut(f, run, following, args.sampleRate);
		// The rig hears of it when it sends something: a tick, a reset, a
		// start or a stop, and a change of tempo on the frame it changes.
		// Between, nothing it sends moves, and every Seq ramps for itself. A
		// change of tempo is sent on its frame, not at the control period: a
		// stopped Clock runs only on the frame a jack stirs it, so a BPM or
		// RATE jack stepped on any other frame would change the tempo and
		// never send it, and a Seq on its own cable at x4 would ramp at the
		// old spacing. It costs nothing: a jack that moves wakes the rig on
		// the frame it moves anyway.
		const bool due = pulses != pulsesBefore || resets != resetsBefore || runningWork != cnt.running ||
		                 tickPeriod != cnt.period || hzWas != cnt.hertz || (hzWas && following) != cnt.cabled ||
		                 (hzMode && (turnWhole != cnt.turnWhole || turnFrac != cnt.turnFrac || phOn != cnt.phased ||
		                             rateLive != cntRate));
		countDue = countDue || due;
		return due;
	}

	// What it sends, as announced, and whether it has something new.
	Count cnt;
	int cntRate = 0; // the rate the count last carried, `tickN` over `tickD`
	bool countDue = true;
	ROW_MANIFEST(M_CLOCK)
	ROW_SLOT_IS(M_CLOCK, 0, "before");
	void onRestart() override {
		restart();
		countDue = true;
		bracketTook = true;
	}
	ROW_SLOT_IS(M_CLOCK, 1, "clock");
	/** The Clock to my left's resets, heard while nothing is in my RESET:
	one trigger into the
	first Clock of a rig restarts every Clock after it, and so every Seq,
	as the bracket's SYNC does, in the same wake, since the nearest Clock to my
	left has sent its count before I am evaluated. The nearest one, so a
	Clock with a cable of its own starts a chain of its own for those after
	it; a GAP ends the rig and severs it. Its count is a child's face, read
	as a Seq reads its Clock's. The serial it last sent is kept with whose it
	was, so a Clock newly to my left is learned, never taken as a reset; and
	a press of the bracket's SYNC, which restarts the two of us already, is not
	taken a second time. Made from my wake, the bracket's way (`onRestart`):
	after this sample's jacks, its tick rising on the next sample. */
	const tree::Node* leftFrom = nullptr;
	uint32_t leftResets = 0;
	bool bracketTook = false;
	void hearLeft(const ProcessArgs& args) {
		const Count* left = countFrom(slotNamed("clock"));
		const bool bracket = bracketTook;
		bracketTook = false;
		if (!left) {
			leftFrom = nullptr;
			return;
		}
		const bool known = left->source == leftFrom;
		const bool moved = left->resets != leftResets;
		leftFrom = left->source;
		leftResets = left->resets;
		if (!known || !moved || bracket || inputs[RESET_INPUT].isConnected())
			return;
		frameNow = args.frame;
		restart();
	}
	void evaluate(const ProcessArgs& args) override {
		hearLeft(args);
		cnt.present = true;
		cnt.source = this;
		if (countDue) {
			cnt.pulses = pulses;
			cnt.resets = resets;
			cnt.period = tickPeriod;
			cnt.running = runningWork;
			cnt.lastFrame = lastPulseFrame;
			cnt.after = lastAfter;
			cnt.hertz = hzWas;
			cnt.cabled = hzWas && wasFollowing;
			cnt.resetAfter = resetAfter;
			cnt.resetBinds = resetBinds;
			cnt.turnWhole = turnWhole;
			cnt.turnFrac = turnFrac;
			cnt.phased = hzWas && phOn;
			if (hzWas)
				ratioOfRate(rateLive, cnt.tickN, cnt.tickD);
			else
				cnt.tickN = cnt.tickD = 1; // the turn is hertz's alone
			cntRate = rateLive;
		}
		countDue = false;
	}
	const Count* countOut() override { return &cnt; }

	/** A reset, from the RESET jack or the bracket's RESET: sent to every Seq of
	my rig. Running on my own tempo, I re-phase there and send a tick with
	it, one event, unless I made one inside the Seqs' window just now, which
	is then the reset's and the grid's (a reset a moment late, through a
	delay or a gate, is the tick's and not a second one). Stopped, the reset
	alone, and RUN makes a tick at once. Following a cable, the reset alone
	and the next trigger is a start, step one, as a DAW's reset then play
	is; inside the window after a beat, that beat is the start, its tick or
	one made now as the beat's, as a Seq on the cable takes it. A
	Seq's own RESET never comes here. Which it is, bound to the tick just
	made or before the next, is decided here alone and sent with the reset,
	so the Seqs of the rig and a Seq on the CLK cable, which hears every tick
	a sample later, take it alike (`Count::resetBinds`); a stopped Clock's
	reset is before RUN's tick, a tick inside the window or not. */
	void restart() {
		const int64_t f = frameNow;
		resets++;
		if (hz())
			phaseZero();
		countDue = true;
		resetAfter = pulses;
		resetBinds = false;
		gapSkipped = false;
		const bool run = running();
		const bool following = follows();
		const bool justMade = (!hz() || following) && lastPulseFrame >= 0 && f - lastPulseFrame <= window && jackArmed;
		if (following) {
			const bool live = run && !waiting && !halted;
			const bool beatJust = beatFrame >= 0 && f - beatFrame <= window;
			// A tick the ramp made after the beat, the beat and the tick both
			// inside the window (a high multiplier on a fast cable): that
			// tick is the reset's. A mid-beat tick alone binds nothing: a Seq
			// on the cable multiplies the beats itself and takes such a reset
			// before the next beat, so the Clock does too; binding it to the
			// tick puts the rig a step or two ahead of that Seq for good at
			// x2 to x4.
			if (live && justMade && lastPulseFrame > beatFrame && beatJust) {
				resetBinds = true;
				return;
			}
			// Inside the window after a beat, that beat is the reset's, a
			// start, its decision made again as a Seq on the same cable makes
			// its own: the tick it made is the start, or, a beat that made
			// none (a division between its ticks), a tick is made now and
			// sent as the beat's, so the Seqs land it on the beat and the
			// grid starts there (one pulse, one decision), and a Clock
			// following at /2 and a Seq at /2 on its cable never part at a
			// RESET after the other beat.
			if (live && beatJust) {
				// The beat is the reset's, so the gap it closed is the one
				// the reset fell in, a stop's, no tempo, as for a reset on
				// the beat's own sample (`TrigCounter::resetWith`). Measured,
				// a DAW's stop, reset, play with the reset a sample behind
				// the clock made no tick that beat and crammed eight into the
				// next.
				if (beatIn.lastFrame == beatFrame) {
					beatIn.resetWith();
					beatPeriod = gapIn() > 0.0 ? gapIn() : beatPeriodBefore;
				}
				if (lastPulseFrame < beatFrame) {
					fw = next;
					make(fw, beatFrame);
				}
				else
					fw = next - 1;
				fs = 0;
				rampFrom = (double)fw;
				rateLive = rateNow();
				phaseIn = 0;
				cycWhole = 0;
				owed = 0;
				journey();
				beatOpen = false;
				startOpen = true;
				resetAfter = pulses;
				resetBinds = true;
				return;
			}
			waiting = true;
			// Stopped or waiting, inside the window after a beat the Clock
			// did not take: the reset is that beat's, as on its own sample,
			// so the gap it closed is no tempo and the gap it opens is
			// measured, never taking the pause as the tempo when RUN comes
			// after a reset a sample behind the take's first pulse.
			if (beatIn.lastFrame >= 0 && f - beatIn.lastFrame <= window && beatFrame != beatIn.lastFrame) {
				beatIn.resetWith();
				beatPeriod = gapIn() > 0.0 ? gapIn() : beatPeriodBefore;
				gapSkipped = false;
				return;
			}
			beatIn.resetFell();
			gapSkipped = true;
			return;
		}
		if (!run || !ownRunning) {
			armedStart = true;
			return;
		}
		if (justMade) {
			regrid((double)lastPulseFrame, next - 1);
			lastBeatFrame = lastPulseFrame;
			resetBinds = true;
			return;
		}
		rateLive = rateNow();
		regrid((double)f, next);
		lastBeatFrame = f;
		make(next, f);
		// The bracket's RESET comes after this sample's jack was written: high,
		// it falls now, so the tick rises on the next sample as the RESET
		// jack's does, not a sample later still.
		if (jackFrame == f && pulseHigh) {
			if (blepWork)
				clkBlep.insert(0.f, -10.f);
			outputs[CLK_OUTPUT].setVoltage(0.f);
			pulseHigh = false;
		}
	}
};

double TempoQuantity::gain() { return clock ? relaxed(clock->bpmGain) : 1.0; }
// The cable sets the variant's own setting only: what is heard is in that
// variant's unit, and the other's setting stands as it was left.
bool TempoQuantity::follows() { return clock && clock->follows() && !clock->hz(); }
double TempoQuantity::heard() { return clock ? relaxed(clock->heardNow) : -1.0; }
float TempoQuantity::getDisplayValue() {
	if (follows())
		return (float)heard();
	return (float)std::round(Clock::bent(getValue(), gain()));
}
void TempoQuantity::setDisplayValue(float v) {
	if (follows())
		return;
	setValue((float)(Clock::tempoKept((double)v) / gain()));
}
/** Before two triggers there is no tempo, and the hover says so rather than name the setting under it. */
std::string TempoQuantity::getDisplayValueString() {
	if (follows() && heard() <= 0.0)
		return "waiting for pulses";
	return ParamQuantity::getDisplayValueString();
}
std::string TempoQuantity::getUnit() {
	if (!follows())
		return ParamQuantity::getUnit();
	return heard() > 0.0 ? " bpm (external clock)" : " (external clock)";
}
void TempoQuantity::reset() {
	if (!follows())
		ParamQuantity::reset();
}
void TempoQuantity::randomize() {
	if (follows())
		return;
	ParamQuantity::randomize();
	setValue(std::round(getValue()));
}

double FreqQuantity::gain() { return clock ? relaxed(clock->bpmGain) : 1.0; }
/** Following a cable the frequency is the cable's, as the tempo is in BPM
(`TempoQuantity`): the hover names it, and a typed value, a reset or a
randomize sets nothing. */
bool FreqQuantity::follows() { return clock && clock->follows() && clock->hz(); }
double FreqQuantity::heard() { return clock ? relaxed(clock->heardNow) : -1.0; }
float FreqQuantity::getDisplayValue() {
	if (follows())
		return (float)heard();
	return (float)Clock::freqKept(Clock::FREQ_C4 * std::exp2((double)getValue()) * gain());
}
void FreqQuantity::setDisplayValue(float v) {
	if (follows())
		return;
	setValue((float)std::log2(Clock::freqKept((double)v) / gain() / Clock::FREQ_C4));
}
std::string FreqQuantity::getUnit() {
	if (!follows())
		return ParamQuantity::getUnit();
	return heard() > 0.0 ? " Hz (external clock)" : " (external clock)";
}
void FreqQuantity::reset() {
	if (!follows())
		ParamQuantity::reset();
}
void FreqQuantity::randomize() {
	if (!follows())
		ParamQuantity::randomize();
}
/** Seven figures, so C4 reads 261.6256, a stock VCO's at nought volts, and
a frequency typed back from the field is the one shown: at two decimals C4
read 261.63, and 261.63 typed in stood 0.0044 Hz sharp of a VCO, a beat
every four minutes. */
std::string FreqQuantity::getDisplayValueString() {
	if (follows() && heard() <= 0.0)
		return "waiting for pulses";
	return string::f("%.7g", getDisplayValue());
}

/** The variants in the order the arrows walk them and the word the title
says for each as it comes. BPM and HZ are the
Clock's two ways of running and CLK the module's own name, so coming back from
hertz the title says BPM before it settles on CLK. */
inline const char* clockModeWord(int mode) { return mode ? "HZ" : "BPM"; }

/** The plate, its captions the mode's, and its title the mode's word for a
moment after the mode changes (`ModeFlash`). */
struct ClockPanel : Panel {
	Clock* clock = nullptr;
	ModeFlash flash;
	int mode() { return clock && clock->hz() ? 1 : 0; }
	/** The pulses a beat the CLK jacks are captioned with, 1 in hertz. */
	int ppqn() { return clock && !clock->hz() ? relaxed(clock->ppqnSet) : 1; }
	/** Whether the rate is free, its jack captioned FM. */
	bool rateFree() { return clock && rateFreeOf(clock, clock->rateSet()); }
	uint64_t version() override { return (flash.stamp() * 32 + (uint64_t)ppqn()) * 2 + (rateFree() ? 1 : 0); }
	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		const bool hz = mode() == 1;
		drawPlate(args, this, "CLK", -1.f, 0.f, flash.word, flash.weight());
		// BPM, the clock to follow, RATE, RESET and RUN, on a Seq's rows
		// nought to four, as a Seq's REC, CLK, RATE, RESET and PHASE, so
		// CLK, RATE and RESET stand level with a Seq's own and RUN, PHASE in
		// hertz, with its PHASE.
		// The BPM jack is a volt an octave either way, on the tempo or,
		// following a clock, on the ticks, so its caption is BPM in both;
		// the tooltip says which.
		// In hertz BPM is V/OCT and RUN is PHASE, in and out; RESET stays
		// RESET, a reset at audio rate being a sync anyway, and CLK stays
		// CLK, the cycle to follow as it is the beat in BPM. RATE
		// is FM while the rate is free, its pie showing, in either mode.
		drawCaption(vg, CX, seqCapY(0), hz ? "V/OCT" : "BPM", INK);
		drawCaption(vg, CX, seqCapY(2), rateJackCaption(rateFree()), INK);
		drawCaption(vg, CX, seqCapY(3), "RESET", INK);
		// Past 1 pulse a beat both CLK jacks say how many, in and out.
		const std::string clk = ppqn() > 1 ? string::f("%d%s", ppqn(), CLOCK_PPQ) : std::string("CLK");
		drawCaption(vg, CX, seqCapY(1), clk.c_str(), INK);
		drawCaption(vg, CX, seqCapY(4), hz ? "PHASE" : "RUN", INK);
		drawOutBlock(vg, HEAD_BLOCK2_Y, HEAD_BLOCK2_H);
		drawCaption(vg, CX, HEAD_OUT2_CAP_Y, hz ? "PHASE" : "RUN", PALE);
		drawCaption(vg, CX, HEAD_OUT3_CAP_Y, clk.c_str(), PALE);
	}
};

/** The tempo readout as its own control: drag up and down, a pixel and a half
per BPM. With a clock in patched it reads the tempo arriving and takes no
stroke and no reset (`idle`), the tempo being the cable's; with a BPM CV patched it reads the tempo in force, and the drag
moves that, a BPM a step of what is shown, the setting worked back from it, so
a drag to 200 under a volt sets 100 and pulling the cable shows 100; a double
click puts the tempo shown back to 120 the same way. Unbent this is the drag
every screen has, from a whole BPM to the next; with shift held it lands on
the tens instead, 107 to 110 and on to 120, at a screen's own pace. */
struct TempoParam : ScreenParam {
	TempoParam() { pxPerUnit = 1.5f; }
	/** In hertz the screen is the frequency's: the same widget on the other
	param, so the ground under it is one piece in both modes. */
	bool hzShown = false;
	void step() override {
		Clock* c = paramModule<Clock>(this);
		hzShown = c && c->hz();
		paramId = hzShown ? Clock::FREQ_PARAM : Clock::TEMPO_PARAM;
		ScreenParam::step();
	}
	bool idle() override {
		Clock* c = paramModule<Clock>(this);
		return c && c->follows();
	}
	/** A stroke on the frequency, as a stock VCO's FREQ knob turns:
	continuous, a semitone every
	`PX_PER_STEP` pixels and with command held a cent, moved from the
	setting as it stands each move, so a stroke past an end keeps nothing and
	turning back moves it at once; the engine glides between the moves
	(`Clock::freqGlide`). Shift alone steps an octave from where it stands. */
	void dragFreq(engine::ParamQuantity* pq, float dy) {
		const float lo = pq->getMinValue(), hi = pq->getMaxValue();
		if (Hand::held(G_CLOCK_OCTAVES)) {
			pxPerUnit = PX_PER_STEP;
			dragAccum += dy;
			const int took = (int)stepsTaken();
			if (took != 0)
				landStep(pq, pq->getValue() - (float)took, lo, hi);
			return;
		}
		dragAccum = 0.f;
		if (idle())
			return;
		const float perPx = (Hand::among(G_CLOCK_CENTS) ? 1.f / 1200.f : 1.f / 12.f) / PX_PER_STEP;
		landStep(pq, pq->getValue() - dy * perPx, lo, hi);
	}
	double gain() {
		Clock* c = paramModule<Clock>(this);
		return c ? relaxed(c->bpmGain) : 1.0;
	}
	void resetToDefault(engine::ParamQuantity* pq) override {
		if (hzShown)
			pq->setValue(pq->getDefaultValue());
		else
			pq->setValue((float)(pq->getDefaultValue() / gain()));
	}
	/** The frequency on three cells, as many decimals as they leave room
	for: under a hundred hertz, whole hertz to 999, and past that the same
	three cells in kilohertz, 2.51 and 12.5, the unit beside them saying kHZ
	rather than a k drawn on a cell of its own. */
	static std::string hzCells(double f, bool& kilo) {
		kilo = f >= 999.5;
		const double v = kilo ? f / 1000.0 : f;
		if (v < 9.995)
			return string::f("%.2f", v);
		if (v < 99.95)
			return string::f("%.1f", v);
		return string::f("%d", (int)std::round(v));
	}
	void strokeMove(const DragMoveEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		engine::ParamQuantity* pq = getParamQuantity();
		if (!pq)
			return;
		const float dy = strokeDelta(e);
		moved = moved || strayed();
		if (hzShown) {
			dragFreq(pq, dy);
			return;
		}
		const bool tens = Hand::held(G_CLOCK_TENS);
		pxPerUnit = tens ? PX_PER_STEP : 1.5f;
		dragAccum += dy;
		const int took = (int)stepsTaken();
		if (took == 0)
			return;
		// From the tempo shown as it stands, one rounding of the tempo in
		// force, to the next whole BPM along, and the setting that makes it.
		const double g = gain();
		const int now = (int)std::round(Clock::bent(pq->getValue(), g));
		const int to = tens ? gridStep(now, 10, -took, Clock::TEMPO_MIN, Clock::TEMPO_MAX)
		                    : clamp(now - took, Clock::TEMPO_MIN, Clock::TEMPO_MAX);
		const float set = clamp((float)(to / g), pq->getMinValue(), pq->getMaxValue());
		// An end, of the tempo shown or of the setting, keeps no travel.
		if (to == Clock::TEMPO_MIN || to == Clock::TEMPO_MAX || set == pq->getMinValue() ||
		    set == pq->getMaxValue())
			dragAccum = 0.f;
		pq->setValue(set);
	}
	void drawLit(const DrawArgs& args) override {
		NVGcolor col = SCHEME_YELLOW;
		Clock* c = paramModule<Clock>(this);
		if (c && hzShown) {
			col = c->tempoColor();
			// Waiting on a cable's second edge, minus one: the ghosts alone,
			// as in BPM.
			const double hzShownNow = relaxed(c->hzNow);
			if (hzShownNow < 0.0) {
				drawCount(args.vg, box.size, -1, col, 3);
				return;
			}
			bool kilo = false;
			const std::string text = hzCells(hzShownNow, kilo);
			const CellsLayout l = drawNumber(args.vg, box.size, text, col, 3);
			drawUnitMark(args.vg, l, kilo ? "kHZ" : "HZ", col);
			return;
		}
		// Decimal to 999 and hex past it with its mark, to FFF (`drawCount`);
		// waiting on a clock, minus one, the ghosts alone.
		const int bpm = c ? relaxed(c->bpmNow) : 120;
		if (c)
			col = c->tempoColor();
		drawCount(args.vg, box.size, bpm, col, 3);
	}
};

/** The multiplier, the mode's own (`Clock::rateParam`): drag through /4, /2, x1,
x2 and on up to x255; a double click is the mode's default again, x4 in BPM
and x1 in HZ, and a click alone nothing. The screen every
rate is drawn on (`RateScreen`): with the RATE or BPM jack patched it
still says the division set, the hover the one in force, and the jack's volts
run along its foot in its cable's colour as a Seq's do, the V/OCT jack's in
the whole octaves it moves the ticks by. With both patched the bar is the
RATE jack's, the screen's own, and the hover names the two together. A plain
drag moves by a whole rate, Command held by a hundredth, Command and shift by
a ten-thousandth, and shift held lands on the powers of two (`RateScreen`). */
struct MultParam : RateScreen {
	void step() override {
		Clock* c = paramModule<Clock>(this);
		barJack = c && !relaxed(c->ratePatched) && relaxed(c->octPatched) ? Clock::BPM_INPUT : Clock::RATE_INPUT;
		paramId = c ? c->rateParam() : Clock::RATE_PARAM; // the mode's own rate, as TempoParam shows its
		RateScreen::step();
	}
	void shown(NVGcolor& col, float& bar) override {
		Clock* c = paramModule<Clock>(this);
		if (!c)
			return;
		col = c->faceColor();
		if (relaxed(c->ratePatched))
			bar = jackBar(relaxed(c->rateCv));
		else if (relaxed(c->octPatched))
			bar = relaxed(c->octCv);
	}
};


struct ClockWidget : MemberWidget {
	Clock* clock = nullptr;
	ClockPanel* plate = nullptr;
	VariantArrows arrows;
	/** Variant `to`, BPM nought and HZ one, one undo. */
	void setMode(int to) {
		if (!clock)
			return;
		GestureChanges turn;
		turn.note(clock, Clock::MODE_PARAM);
		clock->params[Clock::MODE_PARAM].setValue((float)to);
		turn.push(std::string("CLK variant: ") + clockModeWord(to));
	}
	/** The right-click menu: in BPM the pulses a beat on the CLK jacks, in
	hertz BLEP on the CLK and PHASE jacks, each only where it means something,
	BLEP a plain switch with its words on the hover. */
	void appendContextMenu(ui::Menu* menu) override {
		if (!clock)
			return;
		menu->addChild(new ui::MenuSeparator);
		if (clock->hz()) {
			menu->addChild(tipBoolMenuItem(
			    "BLEP", "CLK and PHASE band-limited for audio; off, sample-exact edges for logic.",
			    whileStanding(this, [](ClockWidget* w) { return relaxed(w->clock->blep); }),
			    whileStanding(this, [](ClockWidget* w, bool on) {
				    dataChange(w->clock, "change CLK BLEP", [&] { w->clock->blep.store(on, std::memory_order_relaxed); });
			    }),
			    "Matters on jumps: CLK's pulses and PHASE's ramp at audio rate. At LFO rates it never does."));
			MemberWidget::appendContextMenu(menu);
			return;
		}
		std::vector<std::string> names;
		for (int i = 0; i < Clock::PPQN_N; i++)
			names.push_back(string::f("%d", Clock::ppqnOf(i)));
		menu->addChild(createIndexSubmenuItem(
		    CLOCK_PPQ, names,
		    whileStanding(this,
		                  [](ClockWidget* w) {
			                  const int n = relaxed(w->clock->ppqnSet);
			                  return (size_t)(n == 24 ? 2 : n == 4 ? 1 : 0);
		                  }),
		    whileStanding(this, [](ClockWidget* w, size_t i) {
			    dataChange(w->clock, "change CLK PPQN", [&] { w->clock->ppqnSet.store(Clock::ppqnOf((int)i), std::memory_order_relaxed); });
		    })));
		menu->addChild(createMenuLabel("For the CLK input and output"));
		MemberWidget::appendContextMenu(menu);
	}
	/** What only the UI knows of my colour: the cable I follow and the bond. */
	void step() override {
		if (clock) {
			clock->inRGB.store(cableColorOn(getInput(Clock::CLOCK_INPUT)), std::memory_order_relaxed);
			clock->bpmRGB.store(cableColorOn(getInput(Clock::BPM_INPUT)), std::memory_order_relaxed);
			const std::vector<ModuleWidget*>* run = runOf(this);
			clock->bondedUi.store(run && run->size() > 1, std::memory_order_relaxed);
			bool seqs = false, right = false, left = false;
			if (run) {
				const auto me = std::find(run->begin(), run->end(), this);
				for (auto w = me + 1; me != run->end() && w != run->end(); ++w) {
					const std::string& slug = (*w)->model->slug;
					if (slug == "GAP")
						break;
					if (slug == "CLK") {
						right = true;
						break;
					}
					seqs = seqs || slug == "SEQ";
				}
				for (auto w = me; me != run->end() && w != run->begin();) {
					const std::string& slug = (*--w)->model->slug;
					if (slug == "GAP")
						break;
					if (slug == "CLK") {
						left = true;
						break;
					}
				}
			}
			clock->seqsRightUi.store(seqs, std::memory_order_relaxed);
			clock->clockRightUi.store(right, std::memory_order_relaxed);
			clock->clockLeftUi.store(left, std::memory_order_relaxed);
		}
		plate->flash.see(plate->mode(), clockModeWord(plate->mode()));
		arrows.show(this);
		MemberWidget::step();
	}
	ClockWidget(Clock* module) {
		clock = module;
		ClockPanel* panel = new ClockPanel;
		panel->clock = module;
		plate = panel;
		open(module, panel);
		const std::string lesson =
		    "BPM is a clock, HZ an audio-rate oscillator that clocks the rig. Each keeps its own tempo or frequency, and its own rate.";
		arrows.add(this, 2, [=]() { return clock && clock->hz() ? 1 : 0; }, [=](int to) { setMode(to); }, lesson);

		addInput(createInputCentered<InPort>(mm2px(Vec(CX, seqRowY(1))), module, Clock::CLOCK_INPUT));

		addScreen<TempoParam>(this, module, Clock::TEMPO_PARAM, SCREEN_Y, HEAD_SCREEN_H);
		addScreen<MultParam>(this, module, Clock::RATE_PARAM, HEAD_SCREEN2_Y, HEAD_SCREEN_H);

		addInput(createInputCentered<InPort>(mm2px(Vec(CX, seqRowY(3))), module, Clock::RESET_INPUT));
		addInput(createInputCentered<InPort>(mm2px(Vec(CX, seqRowY(0))), module, Clock::BPM_INPUT));
		addInput(createInputCentered<InPort>(mm2px(Vec(CX, seqRowY(2))), module, Clock::RATE_INPUT));
		addInput(createInputCentered<InPort>(mm2px(Vec(CX, seqRowY(4))), module, Clock::RUN_INPUT));
		// The RUN switch, in the Clock's colour, lit by whether it runs, the
		// switch or the cable in RUN, as the RUN light and jack say, so a
		// Clock a cable holds stopped never reads RUN. Before the engine has
		// ever stepped, the switch, as every screen, shows its last published
		// state.
		ScreenButton* run = addStripButton<ScreenButton>(this, module, Clock::RUN_PARAM, "RUN");
		run->colorOf = [module]() { return module ? module->faceColor() : playColor(0); };
		run->inForce = [module]() {
			if (module->runShown < 0)
				return module->params[Clock::RUN_PARAM].getValue() > 0.5f;
			return module->lights[Clock::RUN_LIGHT].getBrightness() > 0.5f;
		};
		addOutput(createOutputCentered<OutPort>(mm2px(Vec(CX, HEAD_OUT2_Y)), module, Clock::RUN_OUTPUT));
		addOutput(createOutputCentered<OutPort>(mm2px(Vec(CX, FOOT_Y)), module, Clock::CLK_OUTPUT));
	}
};

} // namespace

Model* modelClock = createModel<Clock, ClockWidget>("CLK");
