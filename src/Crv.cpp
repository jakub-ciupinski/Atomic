// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "HeadBase.hpp"

/** CRV: a curve, a start, an end and the shape between them.

One screen from the head of the panel to the jacks, no bend screen, no LINK
button and no playhead lines: what is being read is a light crossing the
curve, and there is nothing on the curve but the curve until the pointer is
over it.

The screen is three controls side by side: the left third the start point,
the right third the end point, both set as a CV's fader is, shift landing on whole volts,
command scaling every point of that kind in the block about nought, and the
two together setting every point of the kind to the one landed; and the middle
third the handle, a ring on the curve's middle under the pointer, dragged up
smoother and down spikier, shift landing it on whole volts (nought the
straight line), command moving every shape of the block, and
the two together bringing them all to the one landed. A double click on a
point joins it to its neighbour's, the neighbour coming to it: an end brings
the next CRV's start, a start brings the end before; the first's start and the
last's end join round the block, which is how a block loops. A joined point
is one point, held whole wherever it is pressed or dragged, the stroke moving
the shared point (the next start), and only a double click on it parts it,
anywhere in its column, the shared point staying. Under the pointer a joined
start is a diamond, the end it is joined to lit with it, and a joined end a triangle alone. A start that
jumps from the end before has a dashed riser under it.

The shape is how the arc bends and nothing else, by its curve type, bend or
spline (TYPE). A bend, joined or free, curves minus below its straight line and plus
above it whichever way it goes, mirror images. A spline arc is a stretch of
the spline through the points of its run of spline arcs joined to each other,
its shape the spline's tension: the elastic rod at nought, pulled straight at
minus ten, loose at ten; where its run meets a bend or stops, it is flat. The
arithmetic is `rig::Arc` in Arc.hpp. The last CRV of a block works the arcs out for all of
it and tells each member its own.

IN and OUT: alone, IN is the place along the arc, minus ten volts its start
and ten its end, and OUT the curve there, its start with nothing in IN; bonded
to other CRVs or walked by a Seq, IN is the start and OUT the start. SHAPE is
the shape in volts, in place of the screen's.
*/

using namespace rig;

namespace {

struct Crv;

/** A start or an end as its tooltip, in the volts of the window. */
struct ArcPointQuantity : WithHint<PitchFieldQuantity> {
	Crv* crv = nullptr; // set by the constructor that configures the param, before anything asks

	bool end = false;
	std::string getDisplayValueString() override;
	void setDisplayValue(float v) override;
};
struct Crv;
/** The shape as its tooltip: its volts and what they make of the arc, a bend's
or a spline stretch's (after Crv). */
struct ArcBendQuantity : HintQuantity {
	Crv* crv = nullptr;
	std::string getDisplayValueString() override;
};

/** The width of the dashed riser under a start that a jump arrives at. */
const float RISER_W = 2.f;
/** A point's and the shape's gestures (Words.hpp), each a verb and its
object and the first naming its part, which has no caption. The double click
is the invented one, the one a hand most needs: it joins or splits a point,
and toggles a stretch between bend and spline. Command takes every CRV
touching it, for the double click and the drag alike, said only where one
touches (`placeUi`), since alone it reaches nothing past the part; shift
lands a drag on whole volts, and both together land every one on the same. A
point alone has no join to make, so it says shift alone. */
inline const Gesture G_CRV_JOIN_START = declare(On::Double, Mod::None, Rank::Invented, "Double-click: join the start");
inline const Gesture G_CRV_SPLIT_START = declare(On::Double, Mod::None, Rank::Invented, "Double-click: split the start");
inline const Gesture G_CRV_JOIN_END = declare(On::Double, Mod::None, Rank::Invented, "Double-click: join the end");
inline const Gesture G_CRV_SPLIT_END = declare(On::Double, Mod::None, Rank::Invented, "Double-click: split the end");
inline const Gesture G_CRV_SCALE_STARTS = declare(On::Drag, Mod::Command, Rank::Block, RACK_MOD_CTRL_NAME "-drag: scale touching starts");
inline const Gesture G_CRV_SCALE_ENDS = declare(On::Drag, Mod::Command, Rank::Block, RACK_MOD_CTRL_NAME "-drag: scale touching ends");
inline const Gesture G_CRV_JOIN_ALL = declare(On::Double, Mod::Command, Rank::Block, RACK_MOD_CTRL_NAME "-double-click: join touching CRVs");
inline const Gesture G_CRV_SPLIT_ALL = declare(On::Double, Mod::Command, Rank::Block, RACK_MOD_CTRL_NAME "-double-click: split touching CRVs");
inline const Gesture G_CRV_START_VOLTS = declare(On::Drag, Mod::Shift, Rank::Known, RACK_MOD_SHIFT_NAME "-drag: start on whole volts");
inline const Gesture G_CRV_END_VOLTS = declare(On::Drag, Mod::Shift, Rank::Known, RACK_MOD_SHIFT_NAME "-drag: end on whole volts");
inline const Gesture G_CRV_STARTS_ALIKE =
    declare(On::Drag, Mod::ShiftCommand, Rank::Rare, RACK_MOD_SHIFT_NAME "+" RACK_MOD_CTRL_NAME "-drag: match touching starts");
inline const Gesture G_CRV_ENDS_ALIKE =
    declare(On::Drag, Mod::ShiftCommand, Rank::Rare, RACK_MOD_SHIFT_NAME "+" RACK_MOD_CTRL_NAME "-drag: match touching ends");
inline const Gesture G_CRV_TYPE = declare(On::Double, Mod::None, Rank::Invented, "Double-click: toggle bend or spline");
inline const Gesture G_CRV_MOVE_SHAPES = declare(On::Drag, Mod::Command, Rank::Block, RACK_MOD_CTRL_NAME "-drag: move touching shapes");
inline const Gesture G_CRV_ALL_SPLINES = declare(On::Double, Mod::Command, Rank::Block, RACK_MOD_CTRL_NAME "-double-click: make touching CRVs splines");
inline const Gesture G_CRV_ALL_BENDS = declare(On::Double, Mod::Command, Rank::Block, RACK_MOD_CTRL_NAME "-double-click: make touching CRVs bends");
inline const Gesture G_CRV_BEND_VOLTS = declare(On::Drag, Mod::Shift, Rank::Known, RACK_MOD_SHIFT_NAME "-drag: bend on whole volts");
inline const Gesture G_CRV_SPLINE_VOLTS = declare(On::Drag, Mod::Shift, Rank::Known, RACK_MOD_SHIFT_NAME "-drag: spline on whole volts");
inline const Gesture G_CRV_SHAPES_ALIKE =
    declare(On::Drag, Mod::ShiftCommand, Rank::Rare, RACK_MOD_SHIFT_NAME "+" RACK_MOD_CTRL_NAME "-drag: match touching shapes");
/** The one surprise of a spline stretch, said where it is in force: joined to
another spline, the points of both are one curve. */
const char* const SPLINE_RUN_LESSON = "One smooth curve through the points of the spline CRVs joined to it.";

struct Crv : Cartridge, Probe {
	MEMBER_FOOTPRINT
	enum ParamId {
		START_PARAM, // the start, a travel nought to one across the window
		SHAPE_PARAM, // the bend, minus ten to ten volts
		END_PARAM,   // its own end, the same, which the bend leaves behind from nought to five
		LINK_PARAM,  // my start joined to the end of the CRV before me, round the block; set by a double click
		TYPE_PARAM,  // the curve type, bend or spline, turned by a double click on the handle
		PARAMS_LEN
	};
	enum InputId {
		SHAPE_INPUT,
		IN_INPUT, // the place along the arc alone, the start bonded
		INPUTS_LEN
	};
	enum OutputId {
		OUT_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};
	enum { S_KIN = 2 };
	ROW_SLOT_IS(M_CRV, S_KIN, "kin");
	/** Whose window I play: the Atn in my own gap, a
	Qnt there being nothing to me, a curve never being quantized; else my
	kin's, the block of CRVs before me, past any block of CVs; else the full
	range, so a CRV after QNT ATN CV.. takes nothing of that Atn. */
	Member* windowSource() override {
		Member* m = at(sWindow);
		while (m && m->quantFace())
			m = m->windowSource();
		return m ? m : at(S_KIN);
	}
	/** What the next block of CRVs borrows from me: the window I play. */
	const Mapping* laneOut() override { return &winIn; }
	/** My jack is volts, whatever scale my window carries: a CRV is never
	quantized, so an OUT Atn reading me scales and offsets volts. */
	const Mapping* playedOut() override { return nullptr; }
	static const int MAX_BLOCK = 128;
	static_assert(MAX_BLOCK <= BLOCK_ARCS, "blockArcs works out a block this long");

	// My face, which the last of my block reads: the start (the IN jack's,
	// bonded), my own end and my bend, minus one to one and in volts, as the
	// engine plays them; and the same as the hand set them, for the screens.
	float p = 0.f, e = 1.f, c = 0.f;
	float pSet = 0.f, eSet = 1.f, cSet = 0.f;
	bool link = false, spline = false;
	/** The start, the end and the bend as the engine plays them, gliding to
	where the hand put them (`Glide`), so a stroke on a point or on the shape is no click on the
	jack or on a Seq playing me. A cable is played as it comes, and a REC
	take is heard on its own sample. The screen draws the arc the settings
	make, never the glided one (`arcSeen`). */
	Glide startGlide, endGlide, bendGlide;
	CableLeft kept; // the start IN held in a bond, kept when it is pulled
	bool wantsTick() override {
		return startGlide.moving() || endGlide.moving() || bendGlide.moving() || Cartridge::wantsTick();
	}
	bool everySample(const ProcessArgs& args) override {
		const bool land = landing();
		bool glided = !kept.holding() && startGlide.step(params[START_PARAM].getValue(), args.sampleRate, args.frame, land);
		glided |= endGlide.step(params[END_PARAM].getValue(), args.sampleRate, args.frame, land);
		glided |= bendGlide.step(params[SHAPE_PARAM].getValue(), args.sampleRate, args.frame, land);
		return Cartridge::everySample(args) || glided;
	}
	bool glidesNow() const { return p != pSet || e != eSet || c != cSet; }
	// The arc I play: worked out by the last of my block and told me, or by
	// me where I am the last; and the arc my settings make, which my screen
	// draws, the same one but while a glide is under way.
	Arc arc, arcSeen;
	bool toldArc = false; // the last of my block has said, since the parse
	bool joinedIn = false, joinedOut = false; // my start, my end joined to a neighbour, as the last says
	// Where the arcs either side of me stand at the points I share with
	// them, for the jump a screen draws dashed where they are not joined:
	// the next one's start and the one before's end, NAN with none; from
	// the arcs the settings make, as the screen's own arc, so a dash never
	// trails a glide.
	float nextA = NAN, prevB = NAN;
	void arcTold(const Arc& a, const Arc& seen, bool in, bool out, float next, float prev) {
		if (toldArc && a == arc && seen == arcSeen && in == joinedIn && out == joinedOut && next == nextA && prev == prevB)
			return;
		arc = a;
		arcSeen = seen;
		joinedIn = in;
		joinedOut = out;
		nextA = next;
		prevB = prev;
		toldArc = true;
		written();
		poke.store(true, std::memory_order_relaxed);
	}
	float lo = -CV_VOLTS, hi = CV_VOLTS;
	bool bonded = false;
	float alongIn = 0.f;
	float alongWork[MAX_LANES] = {};
	float ownAlong = -1.f;
	float outVolts = 0.f;
	/** Alone with a polyphonic cable in IN, every channel read along the one
	arc, as many out as came in; `outVolts` is the first. */
	float outPoly[PORT_MAX_CHANNELS] = {};
	int nOut = 1;

	Arc arcShown;
	std::atomic<uint32_t> arcGen{0};
	std::atomic<float> loNow{-CV_VOLTS}, hiNow{CV_VOLTS};
	std::atomic<bool> bondedNow{false}, joinedInNow{false}, joinedOutNow{false};
	std::atomic<float> prevBNow{NAN};
	std::atomic<float> ownAlongNow{-1.f}, shapeNow{0.f};
	std::atomic<float> alongNow[MAX_LANES];
	// Where each lane's walk stands along me under a PHASE turn that parts it
	// from the light, for the halo; below nought where it does not stand
	// here, or stands on the light itself.
	float haloWork[MAX_LANES] = {-1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f};
	std::atomic<float> haloNow[MAX_LANES];

	/** A fresh CRV falls from ten to minus ten, straight: an envelope the
	moment a Seq reads it. The defaults its params are configured with, and
	what the browser's picture of one draws (`freshArc`). */
	static constexpr float START_FRESH = 1.f, END_FRESH = 0.f, SHAPE_FRESH = 0.f;
	static constexpr bool LINK_FRESH = false, SPLINE_FRESH = false;
	/** A point's param, nought to one, as the arc's minus one to one. */
	static float pointOf(float v) { return clamp(v, 0.f, 1.f) * 2.f - 1.f; }
	/** The arc a fresh CRV standing alone plays, worked out as a block of one
	works its own out, so the browser shows what dropping one in gives. */
	static Arc freshArc() {
		const float p = pointOf(START_FRESH), e = pointOf(END_FRESH), c = SHAPE_FRESH;
		const bool l = LINK_FRESH, k = SPLINE_FRESH;
		Arc a;
		bool j = false;
		blockArcs(&p, &e, &c, &l, &k, 1, &a, &j);
		return a;
	}
	Crv() : Cartridge(manifest::M_CRV) {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		ArcPointQuantity* s = configParam<ArcPointQuantity>(START_PARAM, 0.f, 1.f, START_FRESH, "Start");
		s->crv = this;
		ArcPointQuantity* en = configParam<ArcPointQuantity>(END_PARAM, 0.f, 1.f, END_FRESH, "End");
		en->crv = this;
		en->end = true;
		configParam<ArcBendQuantity>(SHAPE_PARAM, -10.f, 10.f, SHAPE_FRESH, "Bend")->crv = this;
		configInput(SHAPE_INPUT, "Shape");
		configSwitch(LINK_PARAM, 0.f, 1.f, LINK_FRESH ? 1.f : 0.f, "Start joined", {"Off", "On"});
		configSwitch(TYPE_PARAM, 0.f, 1.f, SPLINE_FRESH ? 1.f : 0.f, "Curve type", {"Bend", "Spline"});
		configInput(IN_INPUT, "Phase");
		cv.bind(inputs[IN_INPUT]);
		configOutput(OUT_OUTPUT, "Curve");
		for (int i = 0; i < MAX_LANES; i++) {
			alongNow[i].store(-1.f, std::memory_order_relaxed);
			haloNow[i].store(-1.f, std::memory_order_relaxed);
		}
	}

	/** The volts a place on my arc means, the window's bottom at minus one and
	its top at one, and past them as far as the arc goes: not `Mapping::placed`,
	which holds a place to the window. In the lerp form, so both ends are
	exact: at the top `hi` itself, where `lo` plus the span rounded off it,
	and a phase standing at a curve's end gave a hair past nought. */
	float volts(float u) const { return voltsIn(u, lo, hi); }
	static float voltsIn(float u, float lo, float hi) {
		const float t = (u + 1.f) * 0.5f;
		return lo * (1.f - t) + hi * t;
	}
	/** Where a joined spline swings past its points beyond the window's edge,
	the jack gives the overshoot as it is, for an Atn to scale back, and the
	screen draws no line out there; only the light and the halo are held at
	the edge, so a read past it still shows where it stands. */
	static float held(float u) { return clamp(u, -1.f, 1.f); }
	float travelOf(float v) const { return hi == lo ? 0.5f : clamp((v - lo) / (hi - lo), 0.f, 1.f); }
	float bendNow() { return bendOf(params[SHAPE_PARAM].getValue()); }
	/** The bend, the cable's where one is in, else `knob`'s. */
	float bendOf(float knob) {
		if (inputs[SHAPE_INPUT].isConnected())
			return clamp(inputs[SHAPE_INPUT].getVoltage(), -10.f, 10.f);
		return clamp(knob, -10.f, 10.f);
	}
	Crv* left() { return static_cast<Crv*>(peer()); }
	/** Whether a CRV stands on my right: then it is the one that says. */
	bool chained() const { return (told() & tree::TOLD_CHAINED) != 0; }

	int useTold = -1;
	void settle() override {
		Cartridge::settle();
		useTold = -1;
		toldArc = false;
	}

	void prepare(const Mapping& win, const ProcessArgs& args) override {
		win.plainEnds(lo, hi); // volts, a scale in reach or not: a CRV is never quantized
		// I use what my window slot holds only where an Atn lies through it.
		{
			Member* src = windowSource();
			tellUse(at(sWindow), this, src && src != at(S_KIN), useTold);
		}
		bonded = claimed() || left() != nullptr;
		// IN holds my start in a bond, and pulled leaves it where it held it.
		const bool startCabled = bonded && cv.live();
		keepWhatTheCableLeft(kept, START_PARAM, startCabled);
		if (startCabled)
			kept.liveTravel = travelOf(cv.get());
		const float startSet = startCabled ? kept.liveTravel : kept.owed() ? kept.letGo : params[START_PARAM].getValue();
		const float endSet = params[END_PARAM].getValue();
		if (startCabled || kept.owed())
			startGlide.jump(startSet); // the cable's line, so letting go is no sweep from before
		pSet = pointOf(startSet);
		p = pointOf(startGlide.played(startSet));
		eSet = pointOf(endSet);
		e = pointOf(endGlide.played(endSet));
		cSet = bendNow();
		c = bendOf(bendGlide.played(params[SHAPE_PARAM].getValue()));
		link = params[LINK_PARAM].getValue() > 0.5f;
		spline = params[TYPE_PARAM].getValue() > 0.5f;
		if (!chained()) {
			// The last of my block: every member's arc, mine among them.
			Crv* m[MAX_BLOCK];
			int n = 0;
			for (Crv* q = this; q && n < MAX_BLOCK; q = q->left())
				m[n++] = q;
			std::reverse(m, m + n);
			float ps[MAX_BLOCK], es[MAX_BLOCK], cs[MAX_BLOCK];
			bool ls[MAX_BLOCK], ks[MAX_BLOCK], js[MAX_BLOCK];
			for (int i = 0; i < n; i++) {
				ps[i] = m[i]->p;
				es[i] = m[i]->e;
				cs[i] = m[i]->c;
				ls[i] = m[i]->link;
				ks[i] = m[i]->spline;
			}
			Arc arcs[MAX_BLOCK];
			blockArcs(ps, es, cs, ls, ks, n, arcs, js);
			// While any of us glides, the arcs our settings make as well,
			// for the screens; standing still they are the same arcs.
			Arc seen[MAX_BLOCK];
			bool gliding = false;
			for (int i = 0; i < n && !gliding; i++)
				gliding = m[i]->glidesNow();
			if (gliding) {
				bool sj[MAX_BLOCK];
				for (int i = 0; i < n; i++) {
					ps[i] = m[i]->pSet;
					es[i] = m[i]->eSet;
					cs[i] = m[i]->cSet;
				}
				blockArcs(ps, es, cs, ls, ks, n, seen, sj);
			}
			const Arc* shown = gliding ? seen : arcs;
			for (int i = 0; i < n - 1; i++)
				m[i]->arcTold(arcs[i], shown[i], n > 1 && ls[i], js[i], shown[i + 1].a, n > 1 ? shown[(i + n - 1) % n].b : NAN);
			arc = arcs[n - 1];
			arcSeen = shown[n - 1];
			joinedIn = n > 1 && ls[n - 1];
			joinedOut = js[n - 1];
			nextA = n > 1 ? shown[0].a : NAN;
			prevB = n > 1 ? shown[n - 2].b : NAN;
		}
		else if (!toldArc) {
			// Before the last has spoken, my own ramp.
			arc = Arc();
			arc.a = p;
			arc.b = e;
			arcSeen = Arc();
			arcSeen.a = pSet;
			arcSeen.b = eSet;
			joinedIn = joinedOut = false;
			nextA = prevB = NAN;
		}
		const bool reading = !bonded && cv.live();
		// The place along the arc read in my window, as a CV reads a cable in
		// its own jack: the window's bottom the start, its top the end, so an
		// Atn making the rig nought to ten makes a CV's nought to ten sweep
		// the whole curve.
		const float x = reading ? travelOf(cv.get()) : 0.f;
		// Bonded the jack is the start, whatever the arc has been told.
		outVolts = sane(volts(reading ? arcAt(arc, x) : p));
		ownAlong = reading ? x : -1.f;
		nOut = reading && cv.plugged() ? std::max(inputs[IN_INPUT].getChannels(), 1) : 1;
		outPoly[0] = outVolts;
		for (int ch = 1; ch < nOut; ch++)
			outPoly[ch] = sane(volts(arcAt(arc, travelOf(inputs[IN_INPUT].getVoltage(ch)))));
	}
	float act(Signal& s, int color, int* value) override { return outVolts * 12.f; }
	void publishJack() override {
		outputs[OUT_OUTPUT].setChannels(nOut);
		for (int ch = 0; ch < nOut; ch++)
			outputs[OUT_OUTPUT].setVoltage(outPoly[ch], ch);
	}
	int channelsMeant(int jack) override { return jack == OUT_OUTPUT ? nOut : 1; }

	void standsAt(int lane, float along) override {
		written();
		alongIn = clamp(along, 0.f, 1.f);
		if (lane >= 0 && lane < MAX_LANES)
			alongWork[lane] = alongIn;
	}
	void anchorAt(int lane, float along) override {
		written(); // a write into me, said here too though `anchor` says so first
		if (lane >= 0 && lane < MAX_LANES)
			haloWork[lane] = along < 0.f ? -1.f : clamp(along, 0.f, 1.f);
	}
	/** `arcAt(arc, x)`, kept for the last place asked of the arc as it
	stands: the Seqs walking me in step ask the same place on the same
	sample, and a bend is two exponentials. The place is compared bit for
	bit, so the answer is the one worked out, never a neighbour's. */
	float arcMemoV = 0.f;
	uint32_t arcMemoX = 0;
	Arc arcMemo;
	bool arcMemoKnown = false;
	float curveAt(float x) {
		uint32_t bits;
		std::memcpy(&bits, &x, sizeof bits);
		const Arc& a = arc;
		if (!(arcMemoKnown && bits == arcMemoX && a == arcMemo && a.k == arcMemo.k && a.up == arcMemo.up)) {
			arcMemoV = arcAt(a, x);
			arcMemoX = bits;
			arcMemo = a;
			arcMemoKnown = true;
		}
		return arcMemoV;
	}
	void sound(int sub, const Signal& sig, int col, Sound& o) override {
		o.window = winIn;
		const float v = sane(volts(curveAt(alongIn)));
		o.semis = v * 12.f;
		o.note = (int)std::round(o.semis);
		o.signal.set(v * 12.f, col); // a plain voltage in no window: `setVolts` with the fresh Mapping it holds
		o.signal.mapped = true;
	}
	bool voltsAlong(float along, float& v) const override {
		v = sane(volts(arcAt(arc, along)));
		return true;
	}
	const tree::Node* quantOf() override { return nullptr; }
	int litSemis(int sub) override { return (int)std::round(outVolts * 12.f); }
	void take(int sub, const Taken& t) override {
		if (cv.live())
			return;
		params[START_PARAM].setValue(travelOf((t.add ? volts(p) : 0.f) + t.v[0]));
		startGlide.jump(params[START_PARAM].getValue()); // the patch performing, heard on its own sample
	}
	/** Every channel my jack carries: a lone CRV on a poly cable gives a Poly
	each. */
	int bundle(float* v) override {
		for (int ch = 0; ch < nOut; ch++)
			v[ch] = outPoly[ch];
		return nOut;
	}
	int channelsHeard(int input) const override { return input == IN_INPUT ? PORT_MAX_CHANNELS : 1; }

	/** What the last of my block reads of me, the arc it told me, and my jack. */
	float goneP = 0.f, goneE = 0.f, goneC = 0.f, goneOut = 0.f;
	Arc goneArc;
	bool goneLink = false, goneType = false, goneIn = false, goneOutJ = false;
	bool published() override {
		bool any = Member::published();
		any |= moved(goneP, p);
		any |= moved(goneE, e);
		any |= moved(goneC, c);
		any |= moved(goneLink, link);
		any |= moved(goneType, spline);
		any |= moved(goneArc, arc);
		any |= moved(goneIn, joinedIn);
		any |= moved(goneOutJ, joinedOut);
		any |= moved(goneOut, outVolts);
		return any;
	}

	void publishReads() override {
		Cartridge::publishReads();
		if (arcShown != arcSeen) {
			arcGen.fetch_add(1, std::memory_order_release);
			std::atomic_thread_fence(std::memory_order_release);
			arcShown = arcSeen;
			arcGen.fetch_add(1, std::memory_order_release);
		}
		loNow.store(lo, std::memory_order_relaxed);
		hiNow.store(hi, std::memory_order_relaxed);
		bondedNow.store(bonded, std::memory_order_relaxed);
		joinedInNow.store(joinedIn, std::memory_order_relaxed);
		joinedOutNow.store(joinedOut, std::memory_order_relaxed);
		prevBNow.store(prevB, std::memory_order_relaxed);
		ownAlongNow.store(ownAlong, std::memory_order_relaxed);
		shapeNow.store(cSet, std::memory_order_relaxed);
		for (int i = 0; i < nlanes && i < MAX_LANES; i++)
			alongNow[i].store(laneLit[i] ? alongWork[i] : -1.f, std::memory_order_relaxed);
		// The halo, where the walk stands apart from the light: nothing
		// where the two coincide, which with no turn they always do.
		for (int i = 0; i < nlanes && i < MAX_LANES; i++) {
			const bool apart = laneBase[i] && haloWork[i] >= 0.f && !(laneLit[i] && haloWork[i] == alongWork[i]);
			haloNow[i].store(apart ? haloWork[i] : -1.f, std::memory_order_relaxed);
		}
	}
	/** The lights my screen shows, nought to one along me: where each Seq
	standing on me is, or alone where IN reads me. */
	int lightsNow(float* along, NVGcolor* cols) {
		int n = 0;
		const uint64_t q = relaxed(lanesPacked);
		const int lanes = std::min((int)(q & 0xf), MAX_LANES);
		// A lane is lit by its own place along me, which is below nought
		// while it is not: the lit bits are published apart from the places,
		// and a frame read between the two would take a lane just gone out as
		// lit at nought, a ball at my start for a frame as the light leaves
		// me.
		for (int i = 0; i < lanes; i++) {
			const float a = relaxed(alongNow[i]);
			if (a >= 0.f) {
				along[n] = std::min(a, 1.f);
				cols[n++] = playColor((int)((q >> (11 + 4 * i)) & 0xf));
			}
		}
		const float own = relaxed(ownAlongNow);
		if (n == 0 && own >= 0.f) {
			along[n] = own;
			cols[n++] = curveColor();
		}
		return n;
	}
	/** The halos my screen shows, nought to one along me: where the walk of
	each Seq standing on me stands, under a PHASE turn that carried its
	light elsewhere, along me or onto another CRV. */
	int halosNow(float* along, int* cols) {
		int n = 0;
		const uint64_t q = relaxed(lanesPacked);
		const int lanes = std::min((int)(q & 0xf), MAX_LANES);
		for (int i = 0; i < lanes; i++) {
			const float a = relaxed(haloNow[i]);
			if (a >= 0.f) {
				along[n] = std::min(a, 1.f);
				cols[n++] = (int)((q >> (11 + 4 * i)) & 0xf);
			}
		}
		return n;
	}
	int curveHalosOut(float* along, int* cols) override { return halosNow(along, cols); }
	int curveLightsOut(float* along) override {
		NVGcolor cs[8];
		return lightsNow(along, cs);
	}
	/** What my screen shows my jack giving, from what the last wake
	published alone: the curve at my own light where IN reads me, else the
	start my arc is drawn from. */
	bool curveJackOut(float& v) override {
		Arc a;
		if (!snapshot(a))
			return false;
		const float own = relaxed(ownAlongNow);
		v = sane(voltsIn(own >= 0.f ? arcAt(a, own) : a.a, relaxed(loNow), relaxed(hiNow)));
		return true;
	}
	bool snapshot(Arc& out) {
		for (int attempt = 0; attempt < 4; attempt++) {
			const uint32_t g0 = arcGen.load(std::memory_order_acquire);
			if (g0 & 1)
				continue;
			const Arc a = arcShown;
			std::atomic_thread_fence(std::memory_order_acquire);
			if (relaxed(arcGen) == g0) {
				out = a;
				return true;
			}
		}
		return false;
	}
	/** My own end is out of force while it is joined to the next start. */
	bool endUsed() { return !relaxed(joinedOutNow); }
	/** Whether my IN holds my start: bonded, with a cable in. The one test
	of a held start, read by a stroke's lock on either half of the point
	(`ArcPoint::locked`, through `endHeldUi` for an end joined to it), by the
	words and by the value shown. */
	bool startHeld() { return relaxed(bondedNow) && cv.plugged(); }
	/** My start as it stands in force, as a param: the param, or while IN
	holds it the point IN puts it at, as the engine published it (the
	snapshot's `a`), so an end parted from it stays where the point was seen
	rather than jumping to a param the cable hides. */
	float startInForce() {
		Arc a;
		if (startHeld() && snapshot(a))
			return clamp((a.a + 1.f) * 0.5f, 0.f, 1.f);
		return params[START_PARAM].getValue();
	}
	/** The end I draw, written beside my params while it is the next start's
	rather than my own, so a copy of me alone, which joins nothing, ends
	where I was seen to end (`CrvWidget::born`). Nought to one, as the param;
	NAN while my own end is in force, and in a copy once it is used. */
	float copiedEnd = NAN;
	bool dataIsSetting() override { return false; }
	json_t* dataToJson() override {
		Arc a;
		if (endUsed() || !snapshot(a))
			return nullptr;
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "shownEnd", json_real((a.b + 1.f) / 2.f));
		return rootJ;
	}
	void dataFromJson(json_t* rootJ) override {
		Member::dataFromJson(rootJ);
		json_t* endJ = json_object_get(rootJ, "shownEnd");
		copiedEnd = endJ ? clamp((float)json_number_value(endJ), 0.f, 1.f) : NAN;
	}
	// What only the UI knows, handed over in the widget's step.
	std::atomic<uint32_t> inRGB{0};
	std::atomic<bool> bondedUi{false};
	/** The curve's colour: the Seq walking my block; else, by the scheme
	(`schemeColor`), the cable in my IN while I read it alone, white standing
	idle in a bond, and the first colour alone or patched out. In a block IN is
	my start, and a block is one curve in one colour, so its cable colours
	nothing. */
	NVGcolor curveColor() {
		const uint64_t q = relaxed(lanesPacked);
		const int walked = (q & 0xf) > 0 ? (int)((q >> 11) & 0xf) : -1;
		if (dev().oldColors)
			return playColor(walked);
		return schemeColor(walked, relaxed(bondedNow) ? 0u : relaxed(inRGB), relaxed(bondedUi),
		                    outputs[OUT_OUTPUT].isConnected());
	}

	/** What only the UI knows of my block, for the words (`retipAll`),
	handed over in the widget's step: where I stand in it, nought alone (a
	block of one), one first, two between, three last; and whether I am a
	spline joined to another spline. */
	std::atomic<int> placeUi{0};
	std::atomic<bool> splineRunUi{false};
	/** Whether my end is joined to a start that its own IN holds (the next
	CRV's, or the first's round the block), which a stroke on my end cannot
	move: one first, two another. */
	std::atomic<int> endHeldUi{0};
	void retipAll() override {
		const bool w = relaxed(bondedNow);
		const std::string loWords = decimals(relaxed(loNow), 0, 2, relaxed(loNow) > 0.f),
		                  hiWords = decimals(relaxed(hiNow), 0, 2, relaxed(hiNow) > 0.f);
		if (w) {
			// In a rig IN and OUT are the start's, and a SEQ walking the
			// block plays the curve over one step.
			const bool stepped = (relaxed(lanesPacked) & 0xf) > 0;
			retipIn(this, IN_INPUT, {"Start point",
			        (string::f("Its volts set the start point, kept between %s and %s V.", loWords.c_str(), hiWords.c_str()) +
			         (cv.plugged() ? IN_KEEPS : "")).c_str()});
			retipOut(this, OUT_OUTPUT, {"Start point",
			         stepped ? "A steady voltage; the curve itself plays at the SEQ's OUT, start to end in one step."
			                 : "A steady voltage: the curve itself does not play here."});
		}
		else {
			// A window even about nought: a ramp from nought plays its half.
			const float l = relaxed(loNow), h = relaxed(hiNow);
			const std::string half =
			    l == -h && h > 0.f ? string::f(" A 0 to %s V ramp plays only its second half.", decimals(h, 0, 2).c_str()) : "";
			retipIn(this, IN_INPUT, {"Phase",
			        string::f("%s V reads the start, %s V the end: a ramp here plays the curve at OUT.%s",
			                  loWords.c_str(), hiWords.c_str(), half.c_str()).c_str()});
			retipOut(this, OUT_OUTPUT, {"Curve", cv.plugged() ? "" : "Holds the start point while IN is empty."});
		}
		const int place = relaxed(placeUi);
		const bool block = place > 0;
		const bool locked = startHeld();
		const bool inJoined = relaxed(joinedInNow), outJoined = relaxed(joinedOutNow);
		const int endHeld = outJoined ? relaxed(endHeldUi) : 0;
		// A point alone has no join to make and no block to reach; a start
		// held by IN refuses every stroke, its double click too, and an end
		// joined to such a start is that start, which a drag on it cannot
		// move, while its double click still parts it (crv-tips, ends-dragged).
		Gestures start, end;
		if (block) {
			end = {outJoined ? &G_CRV_SPLIT_END : &G_CRV_JOIN_END, outJoined ? &G_CRV_SPLIT_ALL : &G_CRV_JOIN_ALL};
			if (!endHeld)
				end.insert(end.end(), {&G_CRV_SCALE_ENDS, &G_CRV_ENDS_ALIKE});
			if (!locked)
				start = {inJoined ? &G_CRV_SPLIT_START : &G_CRV_JOIN_START, inJoined ? &G_CRV_SPLIT_ALL : &G_CRV_JOIN_ALL,
				         &G_CRV_SCALE_STARTS, &G_CRV_STARTS_ALIKE};
		}
		if (!locked)
			start.push_back(&G_CRV_START_VOLTS);
		if (!endHeld)
			end.push_back(&G_CRV_END_VOLTS);
		// The first start and the last end join round the block, the one
		// join a hand could not guess from where the screens stand.
		retipParam(this, START_PARAM, {"Start",
		           locked ? "The cable in IN sets it; a drag does nothing while it is in."
		           : place == 1 ? (inJoined ? "Joined round to the last CRV's end."
		                                    : "Joining it closes the loop round to the last CRV's end.")
		                        : "",
		           start});
		retipParam(this, END_PARAM, {"End",
		           endHeld == 1 ? "The cable in the first CRV's IN sets it; a drag does nothing while it is in."
		           : endHeld == 2 ? "The cable in the next CRV's IN sets it; a drag does nothing while it is in."
		           : place == 3 ? (outJoined ? "Joined round to the first CRV's start."
		                                     : "Joining it closes the loop round to the first CRV's start.")
		                        : "",
		           end});
		const bool k = params[TYPE_PARAM].getValue() >= 0.5f;
		const bool held = inputs[SHAPE_INPUT].isConnected();
		Gestures shape{&G_CRV_TYPE};
		if (block)
			shape.insert(shape.end(), {&G_CRV_MOVE_SHAPES, k ? &G_CRV_ALL_BENDS : &G_CRV_ALL_SPLINES});
		if (!held) {
			shape.push_back(k ? &G_CRV_SPLINE_VOLTS : &G_CRV_BEND_VOLTS);
			if (block)
				shape.push_back(&G_CRV_SHAPES_ALIKE);
		}
		// A tooltip names no gesture, so the double click's reset of the
		// shape is left to the hint line, whose piece has no room for it at
		// the bracket's width; a spline joined to a spline says its run, and
		// under SHAPE the cable rules.
		retipParam(this, SHAPE_PARAM, {k ? "Spline" : "Bend",
		           held ? "Set by the SHAPE input."
		           : k && relaxed(splineRunUi) ? SPLINE_RUN_LESSON
		           : "",
		           shape});
		retipIn(this, SHAPE_INPUT, {"Shape", k ? "Replaces the shape set on the screen: -10 V pulls the curve straight, +10 V lets it swing widest."
		                                          : "Replaces the bend set on the screen: -10 V bends the curve fully down, +10 V fully up."});
	}};

std::string ArcBendQuantity::getDisplayValueString() {
	// The shape in force: the SHAPE cable's where one is in, as the screen
	// draws it, else the handle's.
	Arc a;
	const bool shown = crv && crv->snapshot(a);
	const float c = shown && crv->inputs[Crv::SHAPE_INPUT].isConnected() ? a.c : getValue();
	const char* what;
	if (crv && crv->params[Crv::TYPE_PARAM].getValue() >= 0.5f) {
		// Nought is the sine where a sine can be, a ring or a lone spline
		// CRV, whose tension there is the sine's rather than the rod's none,
		// and which takes no slope from a bend joined to it.
		const bool sine = c == 0.f && shown && a.spline && a.ten != 0.0 && a.took0 == 0.f && a.took1 == 0.f;
		what = c <= TAUT ? "straight" : c < 0.f ? "tight" : c == 0.f ? (sine ? "a sine" : "smooth") : "loose";
	}
	else
		what = c < 0.f ? "down" : c == 0.f ? "straight" : "up";
	return decimals(c, 2, 3, true) + " V, " + what;
}

std::string ArcPointQuantity::getDisplayValueString() {
	const float lo = relaxed(crv->loNow), hi = relaxed(crv->hiNow);
	float u = clamp(getValue(), 0.f, 1.f) * 2.f - 1.f;
	Arc a;
	if (((end && !crv->endUsed()) || (!end && crv->startHeld())) && crv->snapshot(a))
		u = end ? a.b : a.a; // the point in force: the next start, or the IN jack's
	return decimals(sane(Mapping::placed(u, lo, hi)), 2, 3, true) + " V";
}
void ArcPointQuantity::setDisplayValue(float v) {
	const float lo = relaxed(crv->loNow), hi = relaxed(crv->hiNow);
	if (hi != lo)
		setValue(clamp((v - lo) / (hi - lo), 0.f, 1.f));
}

// ---- The panel ----------------------------------------------------------

// One screen from the stock panels' screen top to the air over SHAPE.
const float ARC_Y = SCREEN_Y;
const float ARC_H = seqCapY(4) - SCREEN_AIR - ARC_Y;

struct ArcZone : GlassZone {
	using GlassZone::GlassZone;
	float y(float u) const { return foot - (clamp(u, -1.5f, 1.5f) + 1.f) * 0.5f * h; }
};

struct CrvPanel : Panel {
	void draw(const DrawArgs& args) override {
		drawPlate(args, this, "CRV", mm2px(ARC_Y), mm2px(ARC_H));
		drawCaption(args.vg, CX, seqCapY(4), "SHAPE", INK);
		drawCaption(args.vg, CX, IN_CAP_Y, "IN", INK);
		drawOutBlock(args.vg, BLOCK_Y, BLOCK_H);
		drawCaption(args.vg, CX, FOOT_CAP_Y, "OUT", PALE);
	}
};

struct ArcPoint;
/** The point under the pointer, any CRV's: a joined start's neighbour
draws its end's half of the diamond while it is hovered. */
/** The point a stroke is setting just now, which a sweep hands on from module
to module: the one point lit while a stroke is under way. */
inline ArcPoint*& activePoint() {
	static ArcPoint* p = nullptr;
	return p;
}
inline ArcPoint*& hoveredPoint() {
	static ArcPoint* p = nullptr;
	return p;
}

/** The arc, its line and the lights on it: where each Seq standing here is,
or alone where IN reads it; a dashed riser under its start where it jumps from
the end before; and while the handle or a point is held, what it sets
in words at its head. Nothing else: a curve left alone carries no marks, the
handle and the points' triangles showing only under the pointer. */
struct ArcFace : Widget {
	Crv* crv = nullptr;
	Arc last;
	bool showBend = false; // set by the handle while hovered or held
	int showPoint = 0;     // set by a point while hovered or held: one the start, two the end
	ArcPoint* startPt = nullptr; // the two points, whose triangles this face paints under the curve
	ArcPoint* endPt = nullptr;

	/** One CRV standing in the drawn run, placed against this screen. */
	struct Link {
		ArcFace* face;
		Crv* crv;
		float dx;    // its screen's left edge, from mine
		float x0, x1; // where nought and one along its arc stand on its screen
		Arc arc;
		NVGcolor col;
	};
	/** The curve is drawn as one path through every arc of a chain, the CRVs
	standing side by side and joined each to the next in one colour, so the
	stroker's own round join keeps the line one width through every bend: two
	strokes, one a screen, each cut at the edge they shared and carried past
	it on its own heading, would widen the join wherever the two headings
	differed. The chain's last screen draws it, and the lights on and beside
	it, since a screen drawn after the line would lay its nought line over it;
	the others draw neither. A chain's two ends stand inside their screens by
	half the line, so the ends are drawn whole. */
	void drawChain(NVGcontext* vg, const ArcZone& z, const Arc& mine, NVGcolor myCol) {
		std::vector<Link> chain;
		Link lone{this, crv, 0.f, 0.f, box.size.x, mine, myCol};
		ModuleWidget* mw = getAncestorOfType<ModuleWidget>();
		const std::vector<ModuleWidget*>* run = mw ? runOf(mw) : nullptr;
		int at = -1;
		if (run && crv)
			at = (int)(std::find(run->begin(), run->end(), mw) - run->begin());
		auto crvAt = [&](int i) -> Crv* {
			return run && i >= 0 && i < (int)run->size() ? dynamic_cast<Crv*>((*run)[i]->module) : nullptr;
		};
		auto linkAt = [&](int i) {
			Link m;
			m.face = (*run)[i]->getFirstDescendantOfType<ArcFace>();
			m.crv = crvAt(i);
			m.dx = (*run)[i]->box.pos.x - mw->box.pos.x;
			m.x0 = 0.f;
			m.x1 = box.size.x;
			m.crv->snapshot(m.arc);
			m.col = m.crv->curveColor();
			return m;
		};
		// Joined to the next on the screen beside it, in the same colour.
		auto chained = [&](int l, int r) {
			Crv* L = crvAt(l);
			Crv* R = crvAt(r);
			if (!L || !R || !relaxed(L->joinedOutNow) || !relaxed(R->joinedInNow))
				return false;
			const NVGcolor a = L->curveColor(), b = R->curveColor();
			return a.r == b.r && a.g == b.g && a.b == b.b;
		};
		if (at >= 0 && at < (int)run->size()) {
			if (chained(at, at + 1))
				return; // the chain's last screen draws it
			int first = at;
			while (chained(first - 1, first))
				first--;
			for (int i = first; i <= at; i++)
				chain.push_back(linkAt(i));
		}
		else
			chain.push_back(lone);
		// The chain's ends inside their screens: by half the line, and past
		// the dashed riser of a jump arriving at the first start.
		const float half = CURVE_W / 2.f;
		Link& head = chain.front();
		head.x0 = head.crv && !relaxed(head.crv->joinedInNow) && !std::isnan(relaxed(head.crv->prevBNow))
		              ? RISER_W + half
		              : half;
		chain.back().x1 = box.size.x - half;
		std::vector<Vec> pts;
		const int steps = 64;
		for (size_t m = 0; m < chain.size(); m++) {
			const Link& c = chain[m];
			for (int i = m == 0 ? 0 : 1; i <= steps; i++) {
				const float u = (float)i / (float)steps;
				pts.push_back(Vec(c.dx + c.x0 + u * (c.x1 - c.x0), z.y(arcAt(c.arc, u))));
			}
		}
		const Rect clip(Vec(chain.front().dx, 0.f), Vec(chain.back().dx + box.size.x - chain.front().dx, box.size.y));
		// The line inside the window and half its width over, so a point on
		// the edge is drawn whole and a bend past it is not drawn at all.
		const float top = z.y(1.f) - half, bottom = z.y(-1.f) + half;
		const Rect band(Vec(clip.pos.x, top), Vec(clip.size.x, bottom - top));
		// The lights on the chain and on the screens either side of it whose
		// glow reaches in, each clipped to the chain as the line is, and the
		// halos the same way: where a walk stands under a PHASE turn that
		// carried its light elsewhere, a glow with no ball.
		Vec lights[32], halos[32];
		NVGcolor cols[32], hcols[32];
		int n = 0, nh = 0;
		auto lightsOf = [&](const Link& c) {
			float hal[8];
			int hc[8];
			const int kh = c.crv->halosNow(hal, hc);
			const float reach = haloReach();
			for (int i = 0; i < kh && nh < 32; i++) {
				const float x = c.dx + c.x0 + hal[i] * (c.x1 - c.x0);
				if (x < clip.pos.x - reach || x > clip.pos.x + clip.size.x + reach)
					continue;
				halos[nh] = Vec(x, z.y(Crv::held(arcAt(c.arc, hal[i]))));
				hcols[nh++] = playColor(hc[i]);
			}
			float along[8];
			NVGcolor cs[8];
			const int k = c.crv->lightsNow(along, cs);
			const float glow = lightGlow();
			for (int i = 0; i < k && n < 32; i++) {
				const float x = c.dx + c.x0 + along[i] * (c.x1 - c.x0);
				if (x < clip.pos.x - glow || x > clip.pos.x + clip.size.x + glow)
					continue;
				lights[n] = Vec(x, z.y(Crv::held(arcAt(c.arc, along[i]))));
				cols[n++] = cs[i];
			}
		};
		for (const Link& c : chain)
			if (c.crv)
				lightsOf(c);
		if (at >= 0) {
			const int lo = at - (int)chain.size(), hi = at + 1;
			for (int i : {lo, hi})
				if (crvAt(i)) {
					Link c = linkAt(i);
					// Its own span, as its own chain draws it.
					const bool end0 = !chained(i - 1, i), end1 = !chained(i, i + 1);
					if (end0)
						c.x0 = !relaxed(c.crv->joinedInNow) && !std::isnan(relaxed(c.crv->prevBNow)) ? RISER_W + half : half;
					if (end1)
						c.x1 = box.size.x - half;
					lightsOf(c);
				}
		}
		for (const Link& c : chain)
			if (c.face) {
				if (c.face != this && c.crv)
					c.crv->snapshot(c.face->last);
				nvgSave(vg);
				nvgTranslate(vg, c.dx, 0.f);
				c.face->paintUnder(vg);
				nvgRestore(vg);
			}
		drawCurvePath(vg, clip, band, pts, chain.back().col, lights, cols, n, halos, hcols, nh);
	}

	void paintPointTriangles(NVGcontext* vg);
	/** What stands under my stretch of the line: the nought line, the dashed
	riser of a jump arriving at my start, and the points' triangles. The painter
	of my chain lays these down for every screen of it before the line,
	whatever order Rack draws the screens in: Rack draws modules in the order
	they were made, so a screen made after its chain's last would draw its
	nought line over the line, and a triangle over it. */
	void paintUnder(NVGcontext* vg) {
		const ArcZone z(box.size);
		const Arc a = last;
		const NVGcolor col = crv ? crv->curveColor() : playColor(-1);
		const float lo = crv ? relaxed(crv->loNow) : -CV_VOLTS, hi = crv ? relaxed(crv->hiNow) : CV_VOLTS;
		if (hi != lo) {
			const float f = (0.f - lo) / (hi - lo);
			if (f >= 0.f && f <= 1.f) {
				// Solid over the pane, so two screens' lines meet at a join
				// without the shared column coming out brighter.
				paintLine(vg, 0.f, box.size.x, z.y(f * 2.f - 1.f), 1.f, rig::over(SCREEN_FACE, color::WHITE, 0.16f));
			}
		}
		// A start not joined where the end before stands elsewhere is a
		// jump, drawn as a dashed riser, on this screen
		// alone and just inside its edge, under the start it arrives at.
		if (crv && !relaxed(crv->joinedInNow)) {
			const float u0 = relaxed(crv->prevBNow);
			if (!std::isnan(u0)) {
				// Whole dashes with a gap at both ends, so neither where the
				// solid line lands nor where the end before left off has a
				// dash over it; the gaps are stretched to fit the length,
				// since a pattern run from one end leaves a sliver of a dash
				// across the other line's end.
				const float from = z.y(a.a), to = z.y(u0), dir = to > from ? 1.f : -1.f;
				const float len = std::fabs(to - from);
				const float w = RISER_W, x = w / 2.f;
				const float on = 2.2f, off = 2.2f;
				if (len >= on + 1.5f) {
					const int n = std::max(1, (int)std::floor((len - off) / (on + off)));
					const float gap = (len - n * on) / (n + 1);
					nvgBeginPath(vg);
					for (int i = 0; i < n; i++) {
						const float t = gap + i * (on + gap);
						nvgMoveTo(vg, x, from + dir * t);
						nvgLineTo(vg, x, from + dir * (t + on));
					}
					nvgLineCap(vg, NVG_BUTT);
					nvgStrokeWidth(vg, w);
					nvgStrokeColor(vg, color::alpha(col, 0.7f));
					nvgStroke(vg);
				}
			}
		}
		paintPointTriangles(vg);
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		Widget::drawLayer(args, layer);
		if (layer != 1)
			return;
		const ArcZone z(box.size);
		if (!crv)
			last = Crv::freshArc();
		else
			crv->snapshot(last);
		NVGcontext* vg = args.vg;
		const NVGcolor col = crv ? crv->curveColor() : playColor(-1);
		const Arc a = last;
		drawChain(vg, z, a, col);
		// While the handle or a point is held or under the pointer, what
		// it sets in words at the screen's head: the shape, or the point in
		// volts.
		std::string words;
		if (crv && showBend)
			words = decimals(relaxed(crv->shapeNow), 1, 1, true) + " V";
		else if (crv && showPoint) {
			const float lo = relaxed(crv->loNow), hi = relaxed(crv->hiNow);
			const float u = showPoint == 2 ? a.b : a.a;
			words = decimals(lo + (u + 1.f) / 2.f * (hi - lo), 1, 2, true) + " V";
		}
		if (!words.empty()) {
			std::shared_ptr<Font> font = plateFont();
			if (font) {
				nvgFontFaceId(vg, font->handle);
				nvgFontSize(vg, 8.5f);
				nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
				nvgFillColor(vg, color::alpha(color::WHITE, 0.85f));
				nvgText(vg, box.size.x / 2.f, GLASS_BEVEL + mm2px(1.f), words.c_str(), nullptr);
			}
		}
	}
};

/** The CRVs of a module's block, left to right, as the engine has it
(`touchingOf`). */
inline std::vector<Crv*> blockOf(Widget* w, Crv* me) {
	return touchingOf(w->getAncestorOfType<ModuleWidget>(), me);
}
/** The CRV after or before `me` in its block, round it; null alone. */
inline Crv* besideIn(Widget* w, Crv* me, int side) {
	const std::vector<Crv*> b = blockOf(w, me);
	const int n = (int)b.size(), i = (int)(std::find(b.begin(), b.end(), me) - b.begin());
	if (n < 2 || i >= n)
		return nullptr;
	return b[(i + (side > 0 ? 1 : n - 1)) % n];
}

/** The screen's thirds: the start's, the shape's and the end's. */
const float ARC_THIRD = 1.f / 3.f;

#include "CrvPoint.hpp"

/** The handle, the middle third of the screen: under the pointer a
small ring on the curve's middle, a spline stretch's with an arm either
side along the curve, as a spline's point is drawn, and a drag up or down
sets the shape, a bend from below its line to above it, a spline stretch from
pulled straight to loose, by distance rather than to the pointer, so it feels
like taking hold of the curve, with a notch at nought a plain stroke lands
in and must travel out of; shift lands it on whole volts, one a notch,
nought being straight or the spline; command moves every shape of the block
together, and with shift brings every one to the volt landed. A double click
turns the stretch between bend and spline, its shape put to nought, and with
command the whole block turns with it.
The SHAPE jack holds the shape while a cable is in, never the curve type. */
/** Where along the face the shape's ring stands, nought to one: the curve's
middle, or where a curve run off the screen there comes back onto it, the
nearest such place either side where the curve stands `inset` (in volts of
the screen, its window being -1 to 1) inside the edge, so the ring and its
arms stay on the glass with the slope uncapped. A curve off the screen across the whole face keeps the middle,
the ring on its edge. */
inline float ringAt(const Arc& a, float inset) {
	if (std::fabs(arcAt(a, 0.5f)) <= 1.f)
		return 0.5f;
	const float lim = 1.f - inset;
	auto on = [&](float x) { return std::fabs(arcAt(a, x)) <= lim; };
	// Out from the middle in steps of a 256th of the face, then halved down to
	// the edge between the last step off and the first on: a place found by
	// search, which no closed form gives for a spline run.
	const int STEPS = 128;
	for (int k = 1; k <= STEPS; k++) {
		for (int side = -1; side <= 1; side += 2) {
			const float in = 0.5f + side * k / (2.f * STEPS);
			if (!on(in))
				continue;
			float out = 0.5f + side * (k - 1) / (2.f * STEPS);
			float hit = in;
			for (int i = 0; i < 20; i++) {
				const float mid = 0.5f * (out + hit);
				(on(mid) ? hit : out) = mid;
			}
			return hit;
		}
	}
	return 0.5f;
}

struct ArcShape : Hovered<ScreenParam> {
	ArcFace* face = nullptr;
	ArcShape() { pxPerUnit = 0.f; }
	Crv* crv() { return paramModule<Crv>(this); }
	// ScreenParam paints ground; this one stands on the screen's own.
	void draw(const DrawArgs& args) override { ParamWidget::draw(args); }
	void drawLayer(const DrawArgs& args, int layer) override {
		ParamWidget::drawLayer(args, layer);
		Crv* m = crv();
		const bool held = APP->event->getDraggedWidget() == this;
		if (face)
			face->showBend = hovered || held;
		if (layer != 1 || !m || !(hovered || held))
			return;
		Arc a;
		if (!m->snapshot(a))
			return;
		const ArcZone z(box.size);
		const float r = mm2px(1.1f), reach = 2.6f * r;
		// Off the screen, the ring stands in from the edge by what it draws,
		// its arms' reach for a spline stretch, its own radius for a bend.
		const float at = ringAt(a, 2.f * (a.spline ? reach + 1.3f : r) / z.h);
		const float cx = box.size.x / 2.f + (at - 0.5f) * 3.f * box.size.x;
		const float cy = z.y(clamp(arcAt(a, at), -1.f, 1.f));
		const NVGcolor ink = color::alpha(color::WHITE, 0.9f);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, cx, cy, r);
		nvgStrokeWidth(args.vg, 1.4f);
		nvgStrokeColor(args.vg, ink);
		nvgStroke(args.vg);
		if (!a.spline)
			return;
		// The arms lie along the curve as drawn: the face is the screen's
		// width, three of this third, so a step along the arc is that wide.
		const float h = 1.f / 64.f;
		const float dx = 2.f * h * 3.f * box.size.x, dy = z.y(arcAt(a, at + h)) - z.y(arcAt(a, at - h));
		const float len = std::hypot(dx, dy); // never nought: dx is a width
		const float ux = dx / len, uy = dy / len;
		nvgBeginPath(args.vg);
		for (int side = -1; side <= 1; side += 2) {
			nvgMoveTo(args.vg, cx + side * ux * r, cy + side * uy * r);
			nvgLineTo(args.vg, cx + side * ux * reach, cy + side * uy * reach);
		}
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);
		nvgBeginPath(args.vg);
		for (int side = -1; side <= 1; side += 2)
			nvgCircle(args.vg, cx + side * ux * reach, cy + side * uy * reach, 1.3f);
		nvgFillColor(args.vg, ink);
		nvgFill(args.vg);
	}
	void onLeave(const LeaveEvent& e) override {
		if (face)
			face->showBend = false;
		Hovered::onLeave(e);
	}
	// Every shape a command stroke moved, with what it was, for one undo.
	GestureChanges block;
	// Mouse travel not yet spent on a notch, a shift stroke's running total,
	// folded back the moment a notch is past an end.
	float notchTravel = 0.f;
	// A plain stroke's travel inside the notch at nought, never wider than it.
	static constexpr float DETENT_PX = 12.f;
	float detentTravel = 0.f;
	// Whether a double click turned a curve type in this stroke, which names its undo.
	bool turned = false;
	void onDragStart(const DragStartEvent& e) override {
		ScreenParam::onDragStart(e);
		notchTravel = 0.f;
		detentTravel = 0.f;
		turned = false;
		block.was.clear();
		block.noteAt(this, startValue);
	}
	/** A drag steps the shape from where it stands, so past an end turning
	back moves it at once (the over-drag rule, `crv-shape-overdrag`). With
	command the step moves every shape of the block by the same volts, each
	held at the ends on its own, so a block can be pushed together to ten or
	nudged as one. Shift lands on whole volts instead, the next one up or down
	from where the shape stands, and with command too every shape of the block
	is set to the volt landed (`crv-shape-notch-overdrag`). */
	static float notched(float v, int n) {
		const int k = n > 0 ? (int)std::floor(v) + n : (int)std::ceil(v) + n;
		return clamp((float)k, -10.f, 10.f);
	}
	void strokeMove(const DragMoveEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		engine::ParamQuantity* pq = getParamQuantity();
		Crv* m = crv();
		if (!pq || !m)
			return;
		const float dy = strokeDelta(e);
		moved = moved || strayed();
		// Twenty volts over the screen's height.
		const float per = 20.f / std::max(box.size.y, 1.f);
		std::vector<Crv*> members;
		if (Hand::among(G_CRV_MOVE_SHAPES))
			members = blockOf(this, m);
		else
			members.push_back(m);
		const bool spline = m->params[Crv::TYPE_PARAM].getValue() >= 0.5f;
		if (spline ? Hand::among(G_CRV_SPLINE_VOLTS) : Hand::among(G_CRV_BEND_VOLTS)) {
			// A notch is a volt of travel; a stroke past an end keeps
			// none of it, so turning back lands on the next volt at once.
			const float px = 1.f / per;
			const float now = pq->getValue();
			notchTravel -= dy;
			if ((now >= 10.f && notchTravel > 0.f) || (now <= -10.f && notchTravel < 0.f))
				notchTravel = 0.f;
			const int n = (int)std::round(notchTravel / px);
			if (n == 0)
				return;
			notchTravel -= n * px;
			const float v = notched(now, n);
			if (v == -10.f || v == 10.f)
				notchTravel = 0.f;
			for (Crv* c : members) {
				if (c->inputs[Crv::SHAPE_INPUT].isConnected())
					continue;
				block.note(c, Crv::SHAPE_PARAM);
				c->paramQuantities[Crv::SHAPE_PARAM]->setValue(v);
				c->touch();
			}
			return;
		}
		const float d = -dy * per;
		if (members.size() == 1 && !members[0]->inputs[Crv::SHAPE_INPUT].isConnected()) {
			// A plain stroke has a notch at nought, straight for a bend and the
			// plain spline for a spline stretch: crossing it lands on it, the rest of
			// that move dropped, and leaving it takes DETENT_PX of travel, whose
			// remainder carries on. Only the travel inside the notch is kept,
			// never more than its width, so turning back moves at once.
			engine::ParamQuantity* q = m->paramQuantities[Crv::SHAPE_PARAM];
			const float v = q->getValue(), w = DETENT_PX * per;
			float to = v + d;
			if (v == 0.f) {
				detentTravel += d;
				if (std::fabs(detentTravel) < w)
					return;
				to = detentTravel - std::copysign(w, detentTravel);
				detentTravel = 0.f;
			}
			else if ((v < 0.f) != (to < 0.f) || to == 0.f) {
				to = 0.f;
				detentTravel = 0.f;
			}
			block.note(m, Crv::SHAPE_PARAM);
			q->setValue(clamp(to, -10.f, 10.f));
			m->touch();
			return;
		}
		for (Crv* c : members) {
			if (c->inputs[Crv::SHAPE_INPUT].isConnected())
				continue;
			block.note(c, Crv::SHAPE_PARAM);
			engine::ParamQuantity* q = c->paramQuantities[Crv::SHAPE_PARAM];
			q->setValue(clamp(q->getValue() + d, -10.f, 10.f));
			c->touch();
		}
	}
	/** A double click turns the stretch between bend and spline and puts its
	shape to nought, a known place to land each time: the straight line coming
	back to a bend, the sine (or the spline) coming to a spline. With command every CRV of the block turns to
	the type this one turns to, so a block of both types becomes one, each
	shape to nought. The SHAPE jack holds the shape, not the type, so a CRV
	with a cable in turns too and its param goes to nought under the cable.
	One undo with the stroke the second press began. */
	void resetToDefault(engine::ParamQuantity* pq) override {
		Crv* m = crv();
		if (!m)
			return;
		const float to = m->params[Crv::TYPE_PARAM].getValue() >= 0.5f ? 0.f : 1.f;
		std::vector<Crv*> members;
		if (to > 0.5f ? Hand::among(G_CRV_ALL_SPLINES) : Hand::among(G_CRV_ALL_BENDS))
			members = blockOf(this, m);
		else
			members.push_back(m);
		for (Crv* c : members) {
			block.note(c, Crv::TYPE_PARAM);
			block.note(c, Crv::SHAPE_PARAM);
			c->paramQuantities[Crv::TYPE_PARAM]->setValue(to);
			c->paramQuantities[Crv::SHAPE_PARAM]->setValue(0.f);
			c->touch();
		}
		turned = true;
	}
	void onDragEnd(const DragEndEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			block.push(turned ? "change curve type" : "change shape");
			turned = false;
			block.was.clear();
			// The gesture is pushed whole; the screen's own record sees no change.
			if (engine::ParamQuantity* pq = getParamQuantity())
				startValue = pq->getValue();
		}
		ScreenParam::onDragEnd(e);
	}
};

struct CrvWidget : MemberWidget {
	ArcFace* face;
	CrvWidget(Crv* module) {
		CrvPanel* panel = new CrvPanel;
		open(module, panel);

		FlatScreen* screen = createWidget<FlatScreen>(mm2px(Vec(0.f, ARC_Y)));
		screen->box.size = mm2px(Vec(PANEL_W, ARC_H));
		addChild(screen);
		face = createWidget<ArcFace>(Vec(0, 0));
		face->box.size = screen->box.size;
		face->crv = module;
		screen->addChild(face);
		const float third = screen->box.size.x * ARC_THIRD;
		ArcPoint* start = createParam<ArcPoint>(Vec(0, 0), module, Crv::START_PARAM);
		start->box.size = Vec(third, screen->box.size.y);
		start->face = face;
		face->startPt = start;
		screen->addChild(start);
		ArcShape* shape = createParam<ArcShape>(Vec(third, 0), module, Crv::SHAPE_PARAM);
		shape->box.size = Vec(screen->box.size.x - 2.f * third, screen->box.size.y);
		shape->face = face;
		screen->addChild(shape);
		ArcPoint* end = createParam<ArcPoint>(Vec(screen->box.size.x - third, 0), module, Crv::END_PARAM);
		end->end = true;
		end->face = face;
		face->endPt = end;
		end->box.size = Vec(third, screen->box.size.y);
		screen->addChild(end);

		addInput(createInputCentered<InPort>(mm2px(Vec(CX, seqRowY(4))), module, Crv::SHAPE_INPUT));
		addInput(createInputCentered<InPort>(mm2px(Vec(CX, IN_Y)), module, Crv::IN_INPUT));
		addOutput(createOutputCentered<OutPort>(mm2px(Vec(CX, FOOT_Y)), module, Crv::OUT_OUTPUT));
	}
	/** A CRV new to the rack on its own, from the browser, the swap menu or a
	duplicate of one CRV, joins nothing, in the undo step that added it: its
	LINK goes off. A duplicate keeps
	the curve it was copied from, its start, its shape and the end it was seen
	to end at, which was the next CRV's start if that one was joined to it, so
	one shape can be repeated; from
	the browser or the swap menu it is the fresh falling saw its defaults are.
	One that came with other modules in the same step, a selection or a block
	duplicated whole, keeps what it was copied with, its joins among them
	included, and so does one a RIG builds, since that step takes modules away
	too. It is told by the last undo step adding it, so a patch loaded or a
	delete undone is left as it was, and it is looked at once, the first step
	it is not being dragged in from the browser. */
	bool born = false;
	void step() override {
		// What only the UI knows of my colour: the cable arriving and the bond.
		if (Crv* c = dynamic_cast<Crv*>(module)) {
			c->inRGB.store(cableColorOn(getInput(Crv::IN_INPUT)), std::memory_order_relaxed);
			const std::vector<ModuleWidget*>* run = runOf(this);
			c->bondedUi.store(run && run->size() > 1, std::memory_order_relaxed);
			// My place in my block and a spline joined to a spline, for the
			// words: the block as the hand reaches it (`touchingOf`).
			const std::vector<Crv*> b = APP->scene && APP->scene->rack ? touchingOf(this, c) : std::vector<Crv*>(1, c);
			const int n = (int)b.size(), i = (int)(std::find(b.begin(), b.end(), c) - b.begin());
			c->placeUi.store(n < 2 || i >= n ? 0 : i == 0 ? 1 : i == n - 1 ? 3 : 2, std::memory_order_relaxed);
			bool run2 = false;
			if (n >= 2 && i < n && c->params[Crv::TYPE_PARAM].getValue() >= 0.5f) {
				const auto spline = [](Crv* x) { return x->params[Crv::TYPE_PARAM].getValue() >= 0.5f; };
				run2 = (relaxed(c->joinedInNow) && spline(b[(i + n - 1) % n])) ||
				       (relaxed(c->joinedOutNow) && spline(b[(i + 1) % n]));
			}
			c->splineRunUi.store(run2, std::memory_order_relaxed);
			// My end joined to a start its IN holds: the first's round the
			// block, else the next's (`locked` on the start's point).
			int held = 0;
			if (n >= 2 && i < n && relaxed(c->joinedOutNow)) {
				Crv* next = b[(i + 1) % n];
				if (next->startHeld())
					held = i == n - 1 ? 1 : 2;
			}
			c->endHeldUi.store(held, std::memory_order_relaxed);
		}
		MemberWidget::step();
		Crv* m = static_cast<Crv*>(module);
		if (born || !m || APP->event->getDraggedWidget() == this)
			return;
		born = true;
		double age = 0.0;
		history::Action* fresh = freshStep(age);
		const float end = m->copiedEnd;
		m->copiedEnd = NAN;
		if (!fresh || !addsOnly(fresh, m->id))
			return;
		history::ComplexAction* all = new history::ComplexAction;
		all->name = "split curves";
		auto set = [&](Crv* c, int param, float value) {
			const float was = c->params[param].getValue();
			if (was == value)
				return;
			APP->engine->setParamValue(c, param, value);
			history::ParamChange* h = new history::ParamChange;
			h->moduleId = c->id;
			h->paramId = param;
			h->oldValue = was;
			h->newValue = value;
			all->push(h);
			c->wake();
		};
		set(m, Crv::LINK_PARAM, 0.f);
		if (!std::isnan(end))
			set(m, Crv::END_PARAM, end);
		// The CRV I came in after ended where the one after it started, if
		// that one was joined to it, round the block; I join nothing, so its
		// own end comes into force and is given the end it was seen to end at.
		if (const std::vector<ModuleWidget*>* run = runOf(this)) {
			auto crvAt = [&](int i) {
				return i >= 0 && i < (int)run->size() ? dynamic_cast<Crv*>((*run)[i]->module) : nullptr;
			};
			int me = 0;
			while (me < (int)run->size() && (*run)[me] != this)
				me++;
			int first = me, last = me;
			while (crvAt(first - 1))
				first--;
			while (crvAt(last + 1))
				last++;
			Crv* before = crvAt(me - 1) ? crvAt(me - 1) : crvAt(last);
			Crv* after = crvAt(me + 1) ? crvAt(me + 1) : crvAt(first);
			if (before && after && before != m && after != m && before != after &&
			    after->params[Crv::LINK_PARAM].getValue() > 0.5f)
				set(before, Crv::END_PARAM, after->params[Crv::START_PARAM].getValue());
		}
		pushJoined(all, fresh, true);
		m->wake();
	}
};

} // namespace

Model* modelCrv = createModel<Crv, CrvWidget>("CRV");
