// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Panel.hpp"
#include "GatherFace.hpp"

/** CV, a true fader: one voltage, held, in the window what stands to its left
decides. */

using namespace rig;

namespace {

/** The fader's gestures (Words.hpp), by what stands to its left. A plain
drag that leaves the screen sideways sets every CV of the run the pointer
crosses to its height (`LevelParam::strokeMove`), a reach past the part
(`Rank::Block`, one invention to a hint being the double click's), said
wherever another CV stands in the run (`sweepUi`), named by the screen's own
thing, the value. Shift lands on the window's whole volts: exactly, in volts;
under a quantizer or a divider on the note or line nearest one, before the
quantizer's transpose, so "near". Over notes or a divider command takes every
CV touching it, the invented reach, said only where one touches (`peersUi`);
over volts command is fine, and with shift finer still. The value has no
caption, so each first piece names it. */
inline const Gesture G_CV_SWEEP = declare(On::Drag, Mod::None, Rank::Block, "Drag across: draw the values");
inline const Gesture G_CV_VOLTS = declare(On::Drag, Mod::Shift, Rank::Known, RACK_MOD_SHIFT_NAME "-drag: value on whole volts");
inline const Gesture G_CV_NEAR_VOLTS = declare(On::Drag, Mod::Shift, Rank::Known, RACK_MOD_SHIFT_NAME "-drag: value near whole volts");
/** Over notes the note nearest each whole volt is an octave from the next,
the scale repeating every volt, so the same stroke moves the value by
octaves; over a divider's lines it stays near whole volts. */
inline const Gesture G_CV_OCTAVES = declare(On::Drag, Mod::Shift, Rank::Known, RACK_MOD_SHIFT_NAME "-drag: value by octaves");
inline const Gesture G_CV_BLOCK_KEYS = declare(On::Drag, Mod::Command, Rank::Block, RACK_MOD_CTRL_NAME "-drag: move touching CVs");
inline const Gesture G_CV_BLOCK_ROLL = declare(On::Drag, Mod::Command, Rank::Block, RACK_MOD_CTRL_NAME "-drag: move touching CVs");
inline const Gesture G_CV_BLOCK_DIVIDER = declare(On::Drag, Mod::Command, Rank::Block, RACK_MOD_CTRL_NAME "-drag: move touching CVs");
/** Over a keyboard the block's stroke with shift too lands on octaves, a
piece of its own so the command's piece stays within the hint's width. */
inline const Gesture G_CV_BLOCK_OCTAVES =
    declare(On::Drag, Mod::ShiftCommand, Rank::Rare, RACK_MOD_SHIFT_NAME "+" RACK_MOD_CTRL_NAME "-drag: move CVs by octaves");
inline const Gesture G_CV_FINE = declare(On::Drag, Mod::Command, Rank::Known, RACK_MOD_CTRL_NAME "-drag: fine");
/** Set by hand over a Poly's channel, said only there: back to the channel. */
inline const Gesture G_CV_FOLLOW = declare(On::Double, Mod::None, Rank::Invented, "Double-click: value follows the POLY");
inline const Gesture G_CV_FINER = declare(On::Drag, Mod::ShiftCommand, Rank::Rare, RACK_MOD_SHIFT_NAME "+" RACK_MOD_CTRL_NAME "-drag: finer");
/** The jacks: IN takes the fader's place, whose bar follows the cable and
keeps where it was left (`keepWhatTheCableLeft`), said while a cable is in;
over a Poly's channel followed it replaces the channel, which takes over
again when the cable is pulled, the feed being still live. OUT is the
fader's voltage and says nothing more. */
#define CV_IN_REPLACES "Replaces the value set on the screen"
const char* const CV_IN_FEED_BACK = " Its channel takes over again when the cable is pulled.";
/** A cable in my own IN locks the fader (`FaderParam::locked`): the
surprise is not who sets it but that the hand is refused. */
const char* const CV_LOCKED = "The cable in IN sets it; a drag does nothing while it is in.";

struct CV;

/** The fader as a tooltip: the same note or voltage the screen has written
on it, not the fraction of the travel the param happens to hold. */
struct ValueQuantity : WithHint<PitchFieldQuantity> {
	CV* fader = nullptr;
	std::string getDisplayValueString() override;
	void setDisplayValue(float v) override;
	/** A value typed over a followed channel takes the fader over, noted
	here as the text is read and folded with Rack's own step for the value
	into one undo by the screen's next step (`foldTyped`). */
	GestureChanges typed;
	bool typedOver = false;
};

struct CV : Cartridge, Probe {
	MEMBER_FOOTPRINT
	enum ParamId {
		VALUE_PARAM,
		OWN_PARAM, // set by hand over a Poly's channel, no face of its own
		PARAMS_LEN
	};
	enum InputId {
		IN_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		CV_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	// The mapping and its slots, resolved only when the mapping changes, and
	// published under a generation counter: odd while being written, so the
	// screen copies and retries until it has a whole one.
	Mapping map;
	Slots slots;
	std::atomic<uint32_t> gen{0};
	float volts = 0.f;

	CV() : Cartridge(manifest::M_CV) {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam<ValueQuantity>(VALUE_PARAM, 0.f, 1.f, 0.5f, "Value")->fader = this;
		// Whether a stroke has taken the fader over from a Poly's channel,
		// a param so it is saved, undone and held in a scene; never rolled
		// (`rolled`), its randomize left on since off says a button. A new
		// cable in the Poly's IN gives it back (`PolyWidget::giveBack`).
		configHandOrPoly(this, OWN_PARAM);
		configInput(IN_INPUT, "Value");
		cv.bind(inputs[IN_INPUT]);
		retipIn(this, IN_INPUT, {"Value", CV_IN_REPLACES "."});
		configOutput(CV_OUTPUT, "Value");
		retipOut(this, CV_OUTPUT, {"Value"});
		slots.resolve(map);
	}

	/** What the fader means is decided to the left, and what the IN jack
	is doing depends on whether a cable is in it. The fader's strokes are
	said at once, each only where it does something: the
	block's command strokes only where another CV touches (`peersUi`), and
	none while a cable of my own holds the fader, which then refuses the
	hand (`FaderParam::locked`). Its lesson names only what stands to its
	left here, a QNT, an ATN or both. */
	void retipAll() override {
		Mapping m;
		Slots sl;
		const bool got = snapshot(m, sl);
		const bool keys = got && m.scale.any() && !m.scale.spread;
		const bool roll = got && m.scale.any() && m.scale.spread;
		const bool divider = got && m.scale.divides();
		const bool qnt = keys || roll || divider;
		const bool atn = got && m.atnNode;
		const bool peers = relaxed(peersUi);
		const bool stepped = (relaxed(lanesPacked) & 0xf) > 0; // a Seq walks the block
		const char* left = qnt && atn ? (keys ? "Plays a note of the QNT's scale, within the range of the ATN to its left."
		                                      : roll ? "Plays a note of the QNT's piano roll, within the range of the ATN to its left."
		                                             : "Stands on a line of the QNT's divider, within the range of the ATN to its left.")
		                   : keys   ? "Plays a note of the scale of the QNT to its left."
		                   : roll   ? "Plays a note of the piano roll of the QNT to its left."
		                   : divider ? "Stands on a line of the divider of the QNT to its left."
		                   : atn    ? "Its range is set by the ATN to its left."
		                            : "";
		// What takes it to its right with no cable: a SEQ as one of its
		// steps, or a gatherer, a POLY as one channel of its cable or a MIX
		// into its sum (`gatheredUi`).
		const int gathered = relaxed(gatheredUi);
		const std::string right = stepped        ? "The SEQ to its right plays it as one of its steps."
		                          : gathered > 0 ? string::f("The POLY to its right sends it as channel %d.", gathered)
		                          : gathered == -2 ? "The POLY to its right leaves it out."
		                          : gathered < 0 ? "The MIX to its right mixes it in."
		                                         : "";
		const std::string says = std::string(left) + (*left && !right.empty() ? " " : "") + right;
		// A Poly's channel, followed or set aside by hand: the double click
		// that gives it back is said only while there is one to give back.
		const bool plugged = cv.plugged();
		const bool offered = !plugged && offeredNow();
		const bool following = offered && followingNow();
		// The channel a Poly offers, counted from one, and whether my own
		// cable stands over it, the feed taking over again when it goes.
		const int channel = relaxed(feedChannelNow) + 1;
		const bool overFeed = plugged && offeredNow() && !own();
		Gestures gs = plugged   ? Gestures{}
		              : keys    ? (peers ? Gestures{&G_CV_BLOCK_KEYS, &G_CV_OCTAVES, &G_CV_BLOCK_OCTAVES} : Gestures{&G_CV_OCTAVES})
		              : roll    ? (peers ? Gestures{&G_CV_BLOCK_ROLL, &G_CV_OCTAVES} : Gestures{&G_CV_OCTAVES})
		              : divider ? (peers ? Gestures{&G_CV_BLOCK_DIVIDER, &G_CV_NEAR_VOLTS} : Gestures{&G_CV_NEAR_VOLTS})
		                        : Gestures{&G_CV_VOLTS, &G_CV_FINE, &G_CV_FINER};
		if (!plugged && relaxed(sweepUi))
			gs.insert(gs.begin(), &G_CV_SWEEP);
		if (offered && !following)
			gs.insert(gs.begin(), &G_CV_FOLLOW);
		const std::string fed = following ? string::f("Follows channel %d of the POLY until a drag takes it over.", channel)
		                        : offered ? string::f("Set by hand in place of channel %d of the POLY.", channel)
		                                  : std::string();
		retipParam(this, VALUE_PARAM, {"Value",
		           plugged   ? CV_LOCKED
		           : offered ? fed.c_str()
		                     : says.c_str(),
		           gs});
		// The jack takes the fader over in whatever window the fader is in,
		// placed there as `travelOf` places it.
		const std::string in = std::string(following || overFeed ? "Replaces its channel of the POLY" : CV_IN_REPLACES) +
		                       (divider     ? ", snapped to " + dividerSnap(atn) + ", held at the lowest and highest."
		                        : qnt && atn ? ", snapped to the nearest note of the QNT within the ATN's range."
		                        : qnt        ? ", snapped to the nearest note of the QNT to its left."
		                        : atn        ? ", held to the range of the ATN to its left."
		                                     : ".") +
		                       (overFeed ? CV_IN_FEED_BACK : plugged ? IN_KEEPS : "");
		retipIn(this, IN_INPUT, {"Value", in.c_str()});
		retipOut(this, CV_OUTPUT, {"Value"});
	}

	/** A whole copy of the mapping and slots, however busy the audio thread.
	Returns false, leaving `m` and `sl` alone, if it could not get one in a few
	tries, so a stalled engine never stalls the screen. */
	bool snapshot(Mapping& m, Slots& sl) {
		for (int attempt = 0; attempt < 4; attempt++) {
			uint32_t g0 = gen.load(std::memory_order_acquire);
			if (g0 & 1)
				continue;
			// Into copies of my own, handed over only once whole: a try the
			// audio thread tore leaves the caller's as they were.
			Mapping m1 = map;
			Slots sl1 = slots;
			std::atomic_thread_fence(std::memory_order_acquire);
			if (relaxed(gen) == g0) {
				m = m1;
				sl = sl1;
				return true;
			}
		}
		return false;
	}

	/** The travel a voltage means here, placed in this CV's window. */
	float travelOf(float v) { return slots.travelOf(map, v); }
	float unshifted(float outVolts) { return Slots::unshift(map, outVolts); }
	void writeTravel(float travel) {
		if (!cv.live()) { // the jack holds the fader while a cable is in
			params[VALUE_PARAM].setValue(clamp(travel, 0.f, 1.f));
			// A take is the patch performing, heard on its own sample.
			valueGlide.jump(params[VALUE_PARAM].getValue());
		}
	}
	/** The fader as the engine plays it, gliding to where the hand put it
	(`Glide`), so a stroke is no click on the jack. Only the hand's fader in
	volts: a cable is already continuous and is played as it comes, and
	under a quantizer or a divider the fader lands on a note or a band,
	which a glide would walk through the ones between, so that is played at
	once. The screens read the fader, never this. */
	Glide valueGlide;
	/** On its way, or owed a start: a feed set aside by hand (`own`) leaves
	the glide standing on the channel while the sample's step, taken before
	the wake that let the feed go, still saw it live, so the fader it must
	glide to is asked for here. */
	bool wantsTick() override {
		return valueGlide.moving() ||
		       (!cv.live() && !kept.owed() && valueGlide.left >= 0 && valueGlide.aim != params[VALUE_PARAM].getValue()) ||
		       Cartridge::wantsTick();
	}
	bool everySample(const ProcessArgs& args) override {
		// A cable plays its own line; the glide waits on what it played.
		const bool land = landing();
		const bool glided =
		    !cv.live() && !kept.owed() && valueGlide.step(params[VALUE_PARAM].getValue(), args.sampleRate, args.frame, land);
		return Cartridge::everySample(args) || glided;
	}
	float playedTravel(float travel) {
		if (cv.live() || kept.owed()) {
			// Stands where the cable is, so letting go, which writes the
			// cable's last travel into the fader, is no sweep from before.
			valueGlide.jump(travel);
			return travel;
		}
		if (map.scale.any() || map.scale.divides())
			return travel;
		return valueGlide.played(travel);
	}
	/** The travel, 0 to 1: the cable if there is one, placed in the window
	and held at its edges, else the knob. A voltage in the jack is what a CV's
	jack puts out, the shift after the quantizer included, so that shift is
	counted back before it is placed: otherwise one CV patched into the next
	in the same bond takes the transposition twice. */
	float value() {
		if (cv.live())
			return slots.travelOf(map, unshifted(cv.get()));
		if (kept.owed())
			return kept.letGo;
		return params[VALUE_PARAM].getValue();
	}
	/** A cable or a Poly's feed let go leaves the fader where it held it
	(`keepWhatTheCableLeft`). */
	CableLeft kept;


	// The window arriving is what the fader is read in.
	void prepare(const Mapping& win, const ProcessArgs& args) override {
		if (win != map) {
			gen.fetch_add(1, std::memory_order_release); // odd: writing
			std::atomic_thread_fence(std::memory_order_release);
			map = win;
			slots.resolve(map);
			gen.fetch_add(1, std::memory_order_release); // even: whole
		}
		keepWhatTheCableLeft(kept, VALUE_PARAM, cv.live());
		const float travel = value();
		if (cv.live())
			kept.liveTravel = travel;
		const float played = playedTravel(travel);
		volts = slots.map(map, played);
		tellUse(at(sWindow), this, true, useTold); // my window slot's shaper: I read its window, cable or not
		// And how I play it, which is the mode of whatever shapes it: the
		// quantizer in my reach's, or volts with none.
		if (Member* w = at(sWindow))
			w->heldMode(place, map.scale.divides() ? (map.scale.harmonic ? HELD_HARMONICS : HELD_DIVIDER)
			                   : !map.scale.any()  ? HELD_VOLTS
			                   : map.scale.spread  ? HELD_ROLL
			                                       : HELD_KEYS);
		noteWork = map.scale.any()  ? slots.noteOf(map, travel)
		           : played != travel ? (int)std::round(slots.map(map, travel) * 12.f) // the setting's, for the screen
		                              : (int)std::round(volts * 12.f);
		// Under a divider, the band my fader stands on, counted as an Atn
		// over it divides, once, from the voltage the jack gives.
		bandWork = map.scale.divides() ? map.divider().band(volts) : NO_BAND;
	}
	int bandWork = NO_BAND;
	int litBand(int sub) override { return bandWork; }
	/** The note this fader sounds, in semitones from C4: under a quantizer
	an integer from the one list, else the voltage rounded, which only a
	screen reads. */
	int noteWork = 0;
	/** A source: the mapped voltage, in its window, for my jack and for a
	reader standing on me. */
	float act(Signal& s, int color, int* value) override {
		s.setVolts(volts, color, map);
		return volts * 12.f;
	}
	/** The quantizer my window runs through, where there is one, gives me its
	colour, carried in the window (`Mapping::quantColor`); and with no
	playhead walking my block I light the note I sound on it, in its own
	colour, as a Seq lights the note of the step it stands on, so a Qnt bound
	to a block of faders shows what they are all playing, every member of the
	block, through the window rather than by reaching into the Qnt's node. */
	int quantHueWork = -1;
	/** The colour of the box on the note I sound: where my quantization came
	from, the Seq walking my quantizer, or white while nothing walks it; a
	playhead standing on me moves across the box and leaves its colour alone.
	*/
	std::atomic<int> boxColor{-1};
	/** The group I work for, for the scheme: my
	quantizer's block, else the reader that walks or gathers me, else none
	(`schemeColor`). */
	std::atomic<int> groupColor{-1};
	// What only the UI knows, handed over in the widget's step: the cable in
	// my jack's colour, and whether I stand idle in a bond.
	std::atomic<uint32_t> inRGB{0};
	std::atomic<bool> bondedUi{false};
	// Whether another CV touches me either side, as my widget saw the run
	// (`touchingOf`), for the hint to name the block's reach only where
	// there is one.
	std::atomic<bool> peersUi{false};
	// Whether another CV stands anywhere in my run, for the sweep's piece
	// (`sharesRunOf`), and what gathers me to my right, as the gatherer
	// published its rows (`srcIdNow`): nought for nothing, a POLY's channel
	// of OUT counted from one, minus two where its row leaves me out, minus
	// one for a MIX.
	std::atomic<bool> sweepUi{false};
	std::atomic<int> gatheredUi{0};
	/** The colour my note box wears, quantized. Under the DEV's OLD, the
	quantizer's block colour, or white with none. */
	NVGcolor faceColor() {
		const int q = relaxed(boxColor);
		if (dev().oldColors)
			return q >= 0 ? playColor(q) : color::WHITE;
		return schemeColor(relaxed(groupColor), relaxed(inRGB), relaxed(bondedUi), outputs[CV_OUTPUT].isConnected());
	}
	void publishJack() override {
		outputs[CV_OUTPUT].setVoltage(sane(volts));
		const tree::Node* q = quantOf();
		quantHueWork = q ? map.quantColor : -1;
		Member* qm = q ? const_cast<Member*>(static_cast<const Member*>(q)) : nullptr;
		if (qm && !claimedByReader())
			if (QuantFace* f = qm->quantFace()) {
				if (bandWork != NO_BAND) // a divider's band, finer than a semitone, counted as an Atn over it divides
					f->lightBand(bandWork, -1);
				else
					f->light(noteWork, -1);
			}
	}
	void publishReads() override {
		Cartridge::publishReads();
		// The quantizer's colour, which is the Seq walking it or white, and
		// never a playhead's standing on me: the playheads move across the
		// steps and leave their colour alone.
		boxColor.store(quantHueWork, std::memory_order_relaxed);
		groupColor.store(quantHueWork >= 0 ? quantHueWork : colorRead, std::memory_order_relaxed);
		feedRgbNow.store(feedSeen ? feedRgbWork : 0, std::memory_order_relaxed);
		feedChannelNow.store(feedSeen ? feedAt : -1, std::memory_order_relaxed);
	}
	/** Lighting a note on the quantizer is a write into another node, which
	is cleared at the top of every wake: so a CV that makes one is at every
	wake, and only one no playhead walks, with a window behind it, does. */
	bool atEveryWake() override { return !claimedByReader() && shaped; }
	/** Whether a shaper reaches my window, in my gap or my kin's lane: worked
	out at the parse, left to right, so a rig of bare faders, whose kin is
	only another bare fader, is at no wake it need not be. */
	bool shaped = false;

	// ---- Lanes. My window
	// slot looks no further than my own gap, the stretch between me and the
	// block of CVs or CRVs before me; a member of a block meets its peer
	// first and finds nothing there. With a Qnt in my gap I play what the gap
	// makes, a lane of my own. With none I take the window of my kin, the
	// block of CVs before me, past any block of CRVs, and an Atn in my gap
	// lays its range on it: I tell the shapers of my gap so (`TOLD_LANE`),
	// and the leftmost of them takes my kin's window as its own
	// (`Atn::windowSource`).
	enum { S_KIN = 3 };
	ROW_SLOT_IS(M_CV, S_KIN, "kin");
	/** Whose window I read: my gap's, else my kin's. */
	Member* windowSource() override {
		if (Member* w = at(sWindow))
			return w;
		return at(S_KIN);
	}
	/** What the next block of CVs takes from me: the window I read. */
	const Mapping* laneOut() override { return &winIn; }
	std::vector<Member*> gap, todo; // my parse's, kept for their room
	void parsed(const tree::Left& left, int pass) override {
		Cartridge::parsed(left, pass);
		if (pass != 0)
			return;
		{
			CV* kin = dynamic_cast<CV*>(at(S_KIN));
			shaped = at(sWindow) || (kin && kin->shaped);
		}
		if (!at(sWindow))
			return;
		at(sWindow)->heldModeOwed(); // I say how I play it at every wake (`prepare`)
		// My gap: my window chain, through the members a Seq standing in it
		// walks. A Qnt anywhere in it, walked or not, opens my own lane.
		// Each member waits once, so neither list outgrows the rig and the
		// room made for it stays (`gap`, `todo`), as does the walk's
		// (`behindScratch`): a parse on a hand's change allocates nothing.
		gap.clear();
		todo.clear();
		gap.reserve(left.rig.size());
		todo.reserve(left.rig.size());
		auto queue = [this](Member* n) {
			if (n && std::find(gap.begin(), gap.end(), n) == gap.end() && std::find(todo.begin(), todo.end(), n) == todo.end())
				todo.push_back(n);
		};
		queue(at(sWindow));
		bool quantizer = false;
		while (!todo.empty()) {
			Member* n = todo.back();
			todo.pop_back();
			gap.push_back(n);
			std::vector<Member*>& walked = behindScratch();
			walked.clear();
			n->behind(walked);
			if (!walked.empty()) {
				for (Member* w : walked)
					queue(w);
				continue;
			}
			quantizer |= (n->traits & tree::T_QNT) != 0;
			queue(n->windowChild());
		}
		// Every shaper of the gap hears which: with no Qnt it continues the
		// lane, and with one the Qnt decides by its mode at the wake.
		for (Member* n : gap)
			tell(n, quantizer ? tree::TOLD_LANE_Q : tree::TOLD_LANE);
	}
	int channelsHeard(int input) const override { return 1; } // a fader is one value
	/** A value taken: placed in the window, over what was there or added
	to it, the shift after the quantizer counted back. */
	void take(int sub, const Taken& t) override {
		writeTravel(travelOf((t.add ? unshifted(volts) : 0.f) + t.v[0]));
	}
	int bundle(float* v) override {
		v[0] = volts;
		return 1;
	}
	int litSemis(int sub) override { return noteWork; }
	bool windowIn(Mapping& win) override {
		win = winIn;
		return true;
	}
	/** A scene stores the fader as it shows: under a cable, mine or a
	Poly's, the travel the jack holds, as the screen draws it. */
	bool heldByCable(int param, float& shown) override {
		if (param != VALUE_PARAM || !jackLiveNow())
			return false;
		Mapping m;
		Slots sl;
		shown = snapshot(m, sl) ? clamp(sl.travelOf(m, Slots::unshift(m, jackNow())), 0.f, 1.f) : params[VALUE_PARAM].getValue();
		return true;
	}
	// ---- A Poly's channel, followed or set aside. Fed, I follow my channel and wear its cable's
	// colour; a stroke takes me over (`OWN_PARAM`), and I play my fader
	// and wear grey until a double click gives me back to the Poly, or a new
	// cable in the Poly's IN does: a different signal starts with every CV
	// following it, while a
	// patch loaded, a scene recalled or an undo keeps the hand where it was
	// (`PolyWidget::giveBack`). Ranked: my own cable, then my hand, then the
	// feed, then the fader under it.
	bool own() { return params[OWN_PARAM].getValue() > 0.5f; }
	int setAsideParam() override { return OWN_PARAM; }
	bool rolled(int param) override { return param != OWN_PARAM; } // a randomize moves the fader, never who sets it
	uint32_t feedRgbWork = 0;      // the cable's colour of the channel a Poly offers me (`feedSeen`)
	std::atomic<uint32_t> feedRgbNow{0};
	std::atomic<int> feedChannelNow{-1}; // the Poly's channel offered me, from nought, minus one for none
	int feed(const float* v, int n) override {
		if (n < 1)
			return 0;
		Member* p = at(sPoly);
		feedRgbWork = p ? p->feedRgb(feedAt) : 0;
		if (own())
			cv.unfeed();
		else
			cv.feed(v[0]);
		return 1; // the channel is mine, followed or not
	}
	uint32_t gatheredRgb() override {
		if (!feedSeen)
			return 0;
		return own() || cv.plugged() ? REPLACED_RGB : feedRgbWork;
	}
	bool faderOut(Mapping& win, float& travel) override {
		Slots sl;
		travel = value();
		return snapshot(win, sl);
	}
};

/** The screen is the fader. */
struct FaderParam : LevelParam {
	Mapping lastMap; // the last whole snapshot, shown again if the next is not ready
	Slots lastSlots;

	CV* getCV() { return paramModule<CV>(this); }

	// The working height: the face inside the bevel the screen carries, so a
	// level at the top of its travel stands on the top of the glass and not
	// on the shading above it. See `GLASS_BEVEL`.
	GlassZone zone() const { return GlassZone(box.size); }
	NVGcolor pane() const { return SCREEN_FACE; }

	/** The fader. */
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		drawFace(args);
	}

	/** One lit band of the fader, the width of the panel. The run's shared
	ground bonds the joins now, and a fader's colour is its own module's,
	which is the one thing a bond cannot reconcile. If a rig of faders at
	different levels ever shows its joins again, put `coatEdges` back around
	this and see: measured before it was taken out, six levels of 192 with
	it and thirty-four without. */
	void coat(const DrawArgs& args, const std::function<void()>& path, NVGcolor c) {
		nvgFillColor(args.vg, c);
		nvgBeginPath(args.vg);
		path();
		nvgFill(args.vg);
	}
	/** A reading floating on the screen, `ty` down it: the volts, or a
	note's name. */
	void label(const DrawArgs& args, float ty, const std::string& text, NVGcolor col) {
		std::shared_ptr<Font> font = plateFont();
		if (!font)
			return;
		drawWord(args.vg, font, LABEL_PX, 0.3f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, col, box.size.x / 2.f, ty, text.c_str());
	}
	static constexpr float LABEL_PX = 8.5f;
	/** Held off the screen's top and foot by two millimetres. */
	float inLabel(float ty) { return clamp(ty, mm2px(2.f), zone().foot - mm2px(2.f)); }
	/** What one frame of the face is drawn from, taken once at the top of
	the draw: the snapshot of the mapping (`lastMap`, `lastSlots`), the
	travel the fader stands at, and the two lines a bar is drawn between.
	The paint steps below read it and nothing else of the module. */
	struct View {
		bool following = false; // following a Poly's channel: drawn in its cable's colour, not dim
		uint32_t feedRgb = 0;   // that cable's colour, nought for white
		bool locked = false;    // a cable holds the value: the bar is drawn dim
		float h = 1.f, bottom = 0.f; // the travel, edge to edge
		float x = 0.5f;              // where the fader stands on it
		float top = 0.f, base = 0.f; // the level's edge and the line it grows from, on device pixels
		bool inside = false;         // nought is within the travel
		bool quantized = false;
		NVGcolor pane;
	};
	View takeView(engine::ParamQuantity* pq, CV* m) {
		View v;
		// A cable in the CV jack drives the fader and the bar is drawn dim to
		// say the hand is set aside; a Poly's channel followed is drawn in
		// its cable's colour, and one my hand set aside as any fader is.
		// Neither cable shows the setting it stands over.
		v.following = m && m->followingNow(); // my own cable wins over the feed
		v.feedRgb = v.following ? relaxed(m->feedRgbNow) : 0;
		v.locked = m && m->jackLiveNow();
		// The travel is the whole screen, edge to edge: the top of the range is
		// the top pixel and the bottom of it the bottom one, so a rig of these
		// has no black band above or below the values, and the lanes behind
		// divide exactly the same height.
		v.h = zone().h;
		v.bottom = zone().foot;
		if (m)
			m->snapshot(lastMap, lastSlots);
		// The jack's voltage with the shift after the quantizer counted back,
		// as the engine places it: see `centre`.
		v.x = v.locked ? lastSlots.travelOf(lastMap, Slots::unshift(lastMap, m->jackNow())) : (pq ? pq->getValue() : 0.5f);
		v.pane = pane(); // the screen behind it all
		// A divider's parts are cells as a pattern's notes are.
		v.quantized = lastMap.scale.any() || lastMap.scale.divides();
		return v;
	}
	/** A colour laid over what it stands on, dimmed while a cable holds the
	fader. The fader was a translucent veil over the pane, and a translucent
	fill cannot meet its neighbour's: at a join each panel lays down only
	part of its colour and the two together come to less than one, which is
	the flicker that survived the coats on the black behind it, measured at
	up to 24 levels of 192 in a rig of CVs. So the veil is worked out here
	and laid down solid instead, in coats like the plate. */
	static NVGcolor veil(const View& v, NVGcolor back, NVGcolor c, float a) {
		return rig::over(back, c, v.locked && !v.following ? a * 0.4f : a);
	}

	void drawFace(const DrawArgs& args) {
		engine::ParamQuantity* pq = getParamQuantity();
		CV* m = getCV();
		View v = takeView(pq, m);
		const Mapping& map = lastMap;
		const Slots& slots = lastSlots;
		// The playheads, behind everything else: a soft glowing vertical line
		// per Seq walking this block, lit in its Seq's colour only while that
		// playhead stands here. When several walk the block the lines take
		// their own column across the width, so two playheads read as two
		// lines rather than one. Nothing walking the block leaves it black.
		if (m) {
			// The playheads on this CV right now, gathered so they can be drawn
			// as one tight cluster centred on the pane: one line dead centre,
			// and several touching lines glowing together, still centred, when
			// more than one Seq is on this CV at once.
			int lit[8];
			bool line[8], halo[8];
			const int n = m->playheadLanes(lit, line, halo);
			paintPlayheadsOnGlass(args.vg, box.size, lit, n, line, halo);
		}

		// On the device's pixels, not the panel's units: a unit is two or
		// three pixels at the zooms a rack is worked at, and a level rounded
		// to units stood still under the hand for that many pixels of drag
		// and then jumped. `snapY` is the one rounding a screen's lines use.
		v.top = snapY(args.vg, v.bottom - slots.level(map, v.x) * v.h);
		// The reference the bar grows from: where the output is nought, C4,
		// wherever the window and its notes have been shifted to, turned over
		// or not; and when nought is out of the window, the edge it went past.
		v.base = snapY(args.vg, v.bottom - slots.nought(map, &v.inside) * v.h);
		if (v.quantized)
			paintNotes(args, v, m);
		else
			paintLevel(args, v);
	}
	/** Under a quantizer the screen stops being a bar and becomes the
	note itself: the notes of the pattern the window reaches divide the
	screen into equal cells, low at the foot, and the one the fader is on is
	a box filled across the whole width with its name in the middle of it.
	The cells are equal because a quantizer's picture is of its pattern, not
	of its volts: C D E and the C above are four notes and read as four
	steps of one stair, where in volts the gap after E is twice the others
	and the stair limps. The screen behind is black and nothing else, not
	even the line at nought, which in a picture of a pattern is one more
	thing to read past: C4 is a cell like any other and the box says where
	it is. A divider's parts are drawn the same way, its cells the parts and
	the box named by the volts of its line. */
	void paintNotes(const DrawArgs& args, const View& v, CV* m) {
		const Mapping& map = lastMap;
		const Slots& slots = lastSlots;
		const float h = v.h, bottom = v.bottom, x = v.x;
		auto coats = [&](const std::function<void()>& path, NVGcolor c) { coat(args, path, c); };
		{
			// The one list the fader walks and the jack lands on
			// (`Slots::windowNotes`): the screen draws it, and the lit cell
			// is the one the fader's travel is in (`Slots::cellOf`), so the
			// two never part near an end.
			const int n = slots.cellCount(map);
			if (n == 0)
				return;
			int lit = Slots::cellOf(map, x, n);
			// The ladder is laid out by the travel, not by the volts: with a
			// Atn turned over the window runs downward and the notes run down
			// the cells, so a staircase drawn going up sounds going down,
			// which is what an Atn below nought is for. The picture stays
			// where the hand put it and the values are what turn over.
			if (map.span() < 0.f)
				lit = n - 1 - lit;
			// Cell `i` counted up from the foot, on whole pixels so the
			// stair has no step a pixel taller than its neighbour.
			auto cellTop = [&](int i) { return std::round(bottom - (float)(i + 1) * h / (float)n); };
			auto cellFoot = [&](int i) { return std::round(bottom - (float)i * h / (float)n); };

			if (lit >= 0) {
				float y0 = cellTop(lit), y1 = cellFoot(lit);
				// Two pixels at the least: a note whose room is thinner than
				// that is still the note the fader is on.
				// A line of two, placed as every line on a screen is.
				if (y1 - y0 < 2.f) {
					const LineBand b = lineBand(args.vg, (y0 + y1) / 2.f, 2.f);
					y0 = b.top;
					y1 = b.top + b.h;
				}
				// In the colour the key is lit in on the quantizer the note came
				// through: the Seq standing here, else the Qnt's own, so a block
				// of faders reads as belonging to its Qnt and a stood fader as
				// the Seq's.
				// Following a Poly's channel, its cable's.
				const NVGcolor boxCol = veil(v, v.pane, v.feedRgb ? fromRGB(v.feedRgb) : m ? m->faceColor() : color::WHITE, 0.78f);
				coats([&] { nvgRect(args.vg, 0.f, y0, box.size.x, y1 - y0); }, boxCol);
				// The box carries its name: in the middle of the box in the
				// pane's own colour while the box is tall enough to hold the
				// letters, and floating just past it, the way the volts do
				// when the screen is not quantized, when it is not.
				const bool fits = (y1 - y0) >= LABEL_PX + 3.f;
				float ty = (y0 + y1) / 2.f;
				if (!fits) {
					ty = y0 - mm2px(2.6f);
					if (ty < mm2px(2.f))
						ty = y1 + mm2px(2.6f);
					ty = inLabel(ty);
				}
				const float sounds = sane(slots.map(map, x));
				const NVGcolor ink = fits ? v.pane : color::alpha(color::WHITE, 0.85f);
				if (map.scale.divides()) {
					label(args, ty, decimals(sounds, 2, 2, true), ink);
					return;
				}
				const Tuned t = slots.tunedAt(map, x, sounds);
				label(args, ty, noteName(t.semis), ink);
				// A note off equal temperament, a chord an Atn spreads, says
				// by how much: the cents on the black above the box, where
				// the volts float on a CV that is not quantized, above the
				// name when the name floats there itself; below when there is
				// no room above, and inside the box over its name when the
				// box fills the screen.
				if (t.cents != 0) {
					const float step = LABEL_PX + 1.f;
					const bool nameAbove = !fits && ty < y0;
					const bool nameBelow = !fits && ty > y1;
					float cy = nameAbove ? ty - step : y0 - mm2px(2.6f);
					bool inBox = false;
					if (cy < mm2px(2.f)) {
						cy = nameBelow ? ty + step : y1 + mm2px(2.6f);
						if (cy > zone().foot - mm2px(2.f)) {
							cy = ty - step;
							inBox = fits;
						}
					}
					label(args, inLabel(cy), centsName(t), inBox ? v.pane : color::alpha(color::WHITE, 0.85f));
				}
			}
		}
	}
	/** Not quantized: the fill, from the base to the level, and then the
	lines over it, each one solid over whatever it actually stands on, the
	fill where it crosses the fill and the bare pane everywhere else. */
	void paintLevel(const DrawArgs& args, const View& v) {
		const Mapping& map = lastMap;
		const Slots& slots = lastSlots;
		const float x = v.x, top = v.top, base = v.base;
		const NVGcolor pane = v.pane;
		auto over = [&](NVGcolor back, NVGcolor c, float a) { return veil(v, back, c, a); };
		auto coats = [&](const std::function<void()>& path, NVGcolor c) { coat(args, path, c); };
		const float fy0 = std::min(top, base), fy1 = std::max(top, base);
		// White: several Seqs' playheads stand on one CV and only their lines
		// can show them all, a fill only one, so the fader wears no scheme
		// colour. A Poly or a Mix gathering me has no colour to give.
		// Following a Poly's channel, the fill is its cable's colour, laid
		// on stronger than white's veil so a colour reads as one.
		const NVGcolor face = v.feedRgb ? fromRGB(v.feedRgb) : color::WHITE;
		const NVGcolor fillCol = over(pane, face, v.feedRgb ? FEED_FILL : 0.13f);
		coats([&] { nvgRect(args.vg, 0.f, fy0, box.size.x, fy1 - fy0); }, fillCol);
		auto backAt = [&](float y) { return (y >= fy0 && y <= fy1) ? fillCol : pane; };

		// Under the DEV's OLD, nought, or C4, when it is within the travel: a
		// thin white line, fainter than the level's edge and half its weight,
		// and not drawn at all while the edge stands on it. Otherwise the bar
		// from nought to the value and its number are all, so a CV at nought
		// is its number alone.
		if (dev().oldColors && v.inside && std::fabs(top - base) > 1.5f)
			coats([&] { const LineBand b = lineBand(args.vg, base, 1.f); nvgRect(args.vg, 0.f, b.top, box.size.x, b.h); },
			      over(backAt(base), face, 0.45f));

		// Under the DEV's OLD, the level's own edge, a brighter line riding
		// on top. Otherwise the fill's edge is the edge.
		if (dev().oldColors)
			coats([&] { const LineBand b = lineBand(args.vg, top, 2.f); nvgRect(args.vg, 0.f, b.top, box.size.x, b.h); },
			      over(backAt(top), face, 0.7f));

		// The reading, riding on the fill: the volts the CV gives, the
		// cable's included, held at the window's edge past it, as the jack
		// is. It floats just past the value, above it, or below when the bar
		// grows downward from nought.
		label(args, inLabel(top > base ? top + mm2px(2.6f) : top - mm2px(2.6f)),
		      decimals(sane(slots.map(map, x)), 2, 2, true), color::alpha(color::WHITE, 0.8f));
	}


	/** How strongly a followed channel's cable colour fills the bar. */
	static constexpr float FEED_FILL = 0.38f;

	/** True while a cable of my own holds the value: the pointer does
	nothing. A Poly's channel followed is no lock: a stroke takes it over
	(`takeOver`). */
	bool locked() override {
		CV* m = getCV();
		return m && m->cv.plugged();
	}

	/** A value typed over a followed channel, which took me over: one
	undo with the value (`foldTyped`). */
	void step() override {
		if (ValueQuantity* q = dynamic_cast<ValueQuantity*>(getParamQuantity()))
			if (q->typedOver) {
				q->typedOver = false;
				foldTyped(this, q->typed);
			}
		LevelParam::step();
	}
	/** Whether I stood set by hand at a press, for a double click to undo
	to what was there before its first press (`firstStart`). */
	float firstOwn = 0.f;
	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			strokeNo()++;
			CV* m = getCV();
			if (m && !locked() && firstPress(system::getTime()))
				firstOwn = m->params[CV::OWN_PARAM].getValue();
		}
		LevelParam::onButton(e);
	}
	/** A sweep crossing a CV that follows a Poly takes it over too, its
	param noted in my stroke's undo before the sweep moves it. */
	void strokeMove(const DragMoveEvent& e) override {
		if (!locked() && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			const float x = APP->scene->rack->getMousePos().x + sweepShift();
			std::vector<LevelParam*> passed;
			peersBetween(std::isnan(lastRackX) ? x : lastRackX, x, passed);
			for (LevelParam* f : passed)
				if (FaderParam* fp = dynamic_cast<FaderParam*>(f))
					if (!fp->locked())
						takeOver(fp->getCV(), swept);
		}
		LevelParam::strokeMove(e);
	}
	/** Two clicks on a CV a Poly offers a channel: back to following it, the
	fader left where it stood before the first click, one undo. With no Poly, the fader's default,
	and any hand set aside with it. */
	void onDoubleClick(const DoubleClickEvent& e) override {
		CV* m = getCV();
		if (!m || locked()) {
			e.consume(this);
			return;
		}
		if (m->offeredNow()) {
			doubleClicked();
			m->params[CV::VALUE_PARAM].setValue(firstStart);
			followAgain(m, swept, firstOwn);
			m->touch();
			spent = true;
			e.consume(this);
			return;
		}
		LevelParam::onDoubleClick(e);
		followAgain(m, swept, firstOwn);
	}

	/** Command held, on plain volts, is the finest setting (`FineStroke`): a
	thousandth of a volt is a pixel or two of hand, and with shift too a tenth
	of that. Over notes or a divider, where finer means nothing, command takes
	the block instead, in real time: over a keyboard or a roll every CV of it moves by
	the notes I move, whole degrees of the pattern, so a melody keeps its
	shape and a stroke back brings it back (by volts, C D E F G A B C nudged
	down and up again snapped to C E F F A B C C); over a divider every other CV moves
	by the volts I move; and over a keyboard shift with it moves us all by
	whole octaves, so each keeps its note. */
	FineStroke held;
	static FaderParam*& carrier() {
		static FaderParam* f = nullptr;
		return f;
	}
	static int& carrierStroke() {
		static int n = -1;
		return n;
	}
	/** The block a command stroke carries, each CV with the volts it gave
	at the stroke's start and, on notes, the cell of its own list it stood
	in (-1 off notes), and mine. */
	struct Carried {
		CV* cv;
		float volts;
		int cell;
	};
	std::vector<Carried> carried;
	float carriedFrom = 0.f;
	// By degrees: the cell I stood in at the stroke's start, and how many
	// the block can move down and up before one of it meets its window's end.
	int fromCell = 0, reachDown = 0, reachUp = 0;
	/** Whether a CV stands on notes, a keyboard's or a roll's, whose cells
	are its list's; and the cell of it a travel is in, -1 off notes. */
	static bool onNotes(const Mapping& m, const Slots& sl) {
		return !m.scale.divides() && m.scale.any() && sl.count > 0;
	}
	static int cellOn(const Mapping& m, const Slots& sl, float x) {
		return onNotes(m, sl) ? Slots::cellOf(m, x, sl.count) : -1;
	}
	static float voltsOf(CV* c) {
		Mapping cm;
		Slots cs;
		if (!c->snapshot(cm, cs))
			return NAN;
		return cs.map(cm, c->params[CV::VALUE_PARAM].getValue());
	}
	void gatherBlock(CV* m) {
		carried.clear();
		const float x0 = getParamQuantity()->getValue();
		carriedFrom = lastSlots.map(lastMap, x0);
		fromCell = cellOn(lastMap, lastSlots, x0);
		reachDown = -fromCell;
		reachUp = lastSlots.count - 1 - fromCell;
		for (CV* c : touchingOf(getAncestorOfType<ModuleWidget>(), m)) {
			if (c == m || c->cv.plugged())
				continue;
			// A CV following a Poly's channel is carried from what it
			// shows, taken over in the stroke's undo as a sweep takes it.
			takeOver(c, swept);
			Mapping cm;
			Slots cs;
			if (!c->snapshot(cm, cs))
				continue;
			const float x = c->params[CV::VALUE_PARAM].getValue();
			const float v = cs.map(cm, x);
			if (!std::isfinite(v))
				continue;
			const int cell = cellOn(cm, cs, x);
			if (cell >= 0) {
				reachDown = std::max(reachDown, -cell);
				reachUp = std::min(reachUp, cs.count - 1 - cell);
			}
			swept.note(c, CV::VALUE_PARAM);
			carried.push_back({c, v, cell});
		}
	}
	/** Every CV carried moved from where it stood: with `degrees`, `deg`
	cells along its own list where it stands on notes, whose neighbouring
	cells are neighbouring notes of the pattern, so that is `deg` degrees of
	it; `by` volts otherwise, placed in its own window as a voltage at its
	jack is, so the ends hold it. Turning back brings it back, nothing kept
	past them. */
	void carryBlock(float by, bool degrees, int deg) {
		for (const Carried& c : carried) {
			Mapping cm;
			Slots cs;
			if (!c.cv->snapshot(cm, cs))
				continue;
			const bool cells = degrees && c.cell >= 0 && onNotes(cm, cs);
			c.cv->params[CV::VALUE_PARAM].setValue(
			    cells ? Slots::cellTravel(cm, clamp(c.cell + deg, 0, cs.count - 1), cs.count)
			          : cs.travelOf(cm, Slots::unshift(cm, c.volts + by)));
			c.cv->touch();
		}
	}
	/** The travel of the whole volt nearest travel `x`, as shift places
	it: in notes the C nearest the note the fader sounds, counted in
	semitones from the one integer the fader lights (`noteOf`) and taken back
	through the shift after the quantizer to the fader's own notes, so it
	lands on the Cs as heard; a C the pattern lacks lands on its nearest
	note. Off notes the window's own whole volt. */
	float nearWholeVolt(float x) const {
		if (lastMap.scale.any()) {
			const int k = lastSlots.noteOf(lastMap, x);
			const int up = k + 6; // the nearest C, the upper at a tie: twelve times up / 12 rounded down
			const int c = 12 * ((up < 0 ? up - 11 : up) / 12);
			return lastSlots.travelOf(lastMap, (float)shiftAfterSnap(lastMap.scale, c, lastMap.postShift, true) / 12.f);
		}
		return lastSlots.travelOf(lastMap, std::round(lastMap.bottom() + x * lastMap.span()));
	}
	/** The level is wherever the mouse is on the screen; with shift held it
	is the whole volt of the window nearest, in a divider the line nearest
	that volt, and in notes the C nearest the note heard, after every
	transpose, an Atn's offset after the Qnt included (`nearWholeVolt`). */
	void setFromY(float y) override {
		engine::ParamQuantity* pq = getParamQuantity();
		if (!pq)
			return;
		float x = zone().travel(y);
		CV* m = getCV();
		takeOver(m, swept);
		if (m)
			m->snapshot(lastMap, lastSlots);
		const bool divider = lastMap.scale.divides(), notes = !divider && lastMap.scale.any();
		const Gesture& reach = divider ? G_CV_BLOCK_DIVIDER : lastMap.scale.spread ? G_CV_BLOCK_ROLL : G_CV_BLOCK_KEYS;
		const bool block = (notes || divider) && Hand::among(reach);
		// Over a roll or a divider shift adds nothing to command: an octave
		// is a keyboard's.
		const bool octaves = block && notes && !lastMap.scale.spread && Hand::among(G_CV_BLOCK_OCTAVES);
		// A command stroke carries its block from the CV it began on; one it
		// sweeps across sideways is carried, never a carrier of its own.
		if (block && carrierStroke() == strokeNo() && carrier() != this)
			return;
		const bool fine = !notes && !divider && Hand::among(G_CV_FINE);
		const float pace = block ? 1.f : Hand::among(G_CV_FINER) ? ULTRA_PACE : FINE_PACE;
		if (held.take(strokeNo(), y, pq->getValue(), fine || block, pace)) {
			carried.clear();
			if (block && m) {
				gatherBlock(m);
				carrier() = this;
				carrierStroke() = strokeNo();
			}
		}
		if (held.relative) {
			x = held.travel(y, zone().h);
			if (fine) {
				pq->setValue(x);
				if (m)
					m->touch();
				return;
			}
		}
		if (block && m && !carried.empty()) {
			// Mine moves by distance and the rest by the notes it moved, an
			// integer from the cell my travel is in, held where the first of
			// the block meets its window's end; the stroke folds there, so
			// turning back moves us at once. Off notes the rest go by the
			// volts I moved, and by octaves mine too goes in whole ones from
			// where it stood.
			float by = lastSlots.map(lastMap, x) - carriedFrom;
			int deg = 0;
			const bool degrees = !octaves && fromCell >= 0;
			if (octaves) {
				by = std::round(by);
				x = lastSlots.travelOf(lastMap, Slots::unshift(lastMap, carriedFrom + by));
			}
			else if (degrees) {
				const int n = lastSlots.count;
				const int want = Slots::cellOf(lastMap, x, n) - fromCell;
				deg = clamp(want, reachDown, reachUp);
				if (deg != want) {
					x = Slots::cellTravel(lastMap, clamp(fromCell + deg, 0, n - 1), n);
					held.value = x;
					held.y = y;
				}
			}
			pq->setValue(x);
			m->touch();
			carryBlock(by, degrees, deg);
			return;
		}
		if (Hand::held(notes ? G_CV_OCTAVES : divider ? G_CV_NEAR_VOLTS : G_CV_VOLTS))
			x = nearWholeVolt(x);
		pq->setValue(x);
		if (m)
			m->touch();
	}
	// A stroke that leaves the panel sideways carries on into the fader of
	// the CV under the pointer: LevelParam's sweep, shared with an Atn's
	// lanes.
};

std::string ValueQuantity::getDisplayValueString() {
	Mapping m;
	Slots sl;
	if (!fader || !fader->snapshot(m, sl))
		return decimals(getValue() * 2.f * CV_VOLTS - CV_VOLTS, 2, 2, true) + " V";
	// What the jack gives, as `publishJack` gives it: an offset can carry the
	// window past twelve volts, and the reading says what the jack carries.
	const float volts = sane(sl.map(m, fader->value()));
	if (!sl.named)
		return decimals(volts, 2, 3, true) + " V";
	const Tuned t = sl.tunedAt(m, fader->value(), volts);
	return t.cents == 0 ? noteName(t.semis) : noteName(t.semis) + " " + centsName(t);
}

/** A value typed into the field is the volts it shows, placed in the window
as a voltage at the jack is, a note on the nearest note, the shift after the
quantizer counted back, as the CRV's points take theirs. Rack hands a typed
number to `setDisplayValue`, which for a plain param is the travel, so without
this 3 would be +10 V. */
void ValueQuantity::setDisplayValue(float v) {
	if (fader && fader->followingNow()) {
		takeOver(fader, typed);
		typedOver = true;
	}
	Mapping m;
	Slots sl;
	if (!fader || !fader->snapshot(m, sl))
		setValue(cvTravel(v));
	else
		setValue(sl.travelOf(m, Slots::unshift(m, v)));
}

struct CVPanel : Panel {
	void draw(const DrawArgs& args) override {
		drawPlate(args, this, "CV", mm2px(SCREEN_Y), mm2px(SCREEN_H));
		drawCaption(args.vg, CX, IN_CAP_Y, "IN", INK);
		drawOutBlock(args.vg, BLOCK_Y, BLOCK_H);
		drawCaption(args.vg, CX, FOOT_CAP_Y, "OUT", PALE);
	}
};

struct CVWidget : MemberWidget {
	CV* fader = nullptr;
	/** What gathers me (`gatheredBy`, GatherFace.hpp): a POLY's channel of
	OUT counted from one, the count its row's value reads, or minus two
	where my row is left out; minus one for a MIX, nought for none. */
	int gatheredCode() {
		if (!fader)
			return 0;
		const GatheredBy by = gatheredBy(this);
		return by.kind == manifest::M_MIX ? -1 : by.kind == manifest::M_POLY ? (by.channel > 0 ? by.channel : -2) : 0;
	}
	/** What only the UI knows of my colour: the cable arriving and the bond. */
	void step() override {
		if (fader) {
			fader->inRGB.store(cableColorOn(getInput(CV::IN_INPUT)), std::memory_order_relaxed);
			const std::vector<ModuleWidget*>* run = runOf(this);
			fader->bondedUi.store(run && run->size() > 1, std::memory_order_relaxed);
			fader->peersUi.store(touchingOf(this, fader).size() > 1, std::memory_order_relaxed);
			fader->sweepUi.store(sharesRunOf(this, fader), std::memory_order_relaxed);
			fader->gatheredUi.store(gatheredCode(), std::memory_order_relaxed);
		}
		MemberWidget::step();
	}
	CVWidget(CV* module) {
		fader = module;
		CVPanel* panel = new CVPanel;
		open(module, panel);

		addInput(createInputCentered<InPort>(mm2px(Vec(CX, IN_Y)), module, CV::IN_INPUT));

		FlatScreen* screen = createWidget<FlatScreen>(mm2px(Vec(0.f, SCREEN_Y)));
		screen->box.size = mm2px(Vec(PANEL_W, SCREEN_H));
		addChild(screen);
		FaderParam* p = createParam<FaderParam>(Vec(0, 0), module, CV::VALUE_PARAM);
		p->box.size = screen->box.size;
		screen->addChild(p);

		addOutput(createOutputCentered<OutPort>(mm2px(Vec(CX, FOOT_Y)), module, CV::CV_OUTPUT));
	}
};

} // namespace

Model* modelCV = createModel<CV, CVWidget>("CV");
