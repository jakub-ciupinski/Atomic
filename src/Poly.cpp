// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Panel.hpp"
#include "GatherFace.hpp"
#include "Stack.hpp"

/** Poly: mono outputs to its left, packed into one polyphonic cable.

It hunts for single outputs to its left and bundles every one of a kind. The
kind is that of the nearest module to its left that a Poly can bundle and that
no Poly to its left has bundled already; the members are every module of that
kind to its left, at any distance, that no Poly to its left has taken, the
sixteen nearest, put on the jack at the foot in rig order. The kinds are the
mono outputs, a Seq's jack, a CV's volts, an Atn's processed signal, a CRV's
curve, and a Mix's sum and a Poly's channels for a submix (Gather.hpp). A Trg is
never bundled, since gathering triggers makes no sense. A Qnt is passed over,
its chord being polyphonic already; a Seq's chord and a Seq's bar of gates
across Trgs give every channel, one after the other. So the Polys of a rig share
it out: in SEQ ... SEQ ATN POLY POLY the first takes the Atns, its nearest, and
the second passes them, taken, and takes the Seqs. Who takes first is the
parse's order, left to right, as everything in the rig is: a Poly further left
has told its members they are bundled (`TOLD_BUNDLED`) before the next Poly
looks. Nothing is walked, so the members wear the Poly's colour all at once, the
body of the panel shows every channel as a bar, and the strip over it names the
kind, so what the cable carries can be read off the plate.

The IN jack is a polyphonic in, and it runs the other way, to the right: with a
cable in it, every CV and Trg to my right whose nearest Poly I am takes its
channels of the cable, in rig order, as though they had arrived in its own jack
(a CV's fader held at the volts, placed in its window so a Qnt snaps it and an
Atn scales it, a Trg's rings gated, and no Atn taking one) and a cable in a
member's own jack wins, so one voice of a bundle can be replaced there. A CV
moved by hand sets its channel aside and plays its fader until a double click
gives it back; a new cable in IN, plugged where there was none or in place of
another, gives every CV I feed back at once, since a different signal is no
reason to keep the old one's hand, while a patch loaded, a scene recalled or an
undo keeps it (`PolyWidget::giveBack`). The cable is what is shown, and its rows
left on are what is passed on, to my jack and to the members alike (`feedOut`),
so a row left out gives its channel to the row after it. So a bundle made by one
Poly is unpacked by the next: POLY CV CV POLY is a cable taken apart, worked on
member by member, and put together again. On its own, with nothing to its left,
it is a meter for any polyphonic cable, sixteen bars for sixteen channels. With
several cables stacked in IN, each cable is a channel, its first, in the cable's
own colour (`Stack`), so a Poly merges mono cables into one polyphonic one. And
a cable in IN releases what the Poly would bundle, so a Poly or a Mix to its
right takes it instead. */

using namespace rig;

namespace {

/** A row's stroke: the press flips it and the drag lays the same over every
row it crosses (`RowPaint`), which no hand guesses of a bar; one piece, since
the click and the paint are one press, said by what it does to the row
pressed. */
inline const Gesture G_POLY_OUT = declare(On::Click, Mod::None, Rank::Invented, "Click or drag: leave rows out");
inline const Gesture G_POLY_BACK = declare(On::Click, Mod::None, Rank::Invented, "Click or drag: put rows back");

const int CHANNELS = GATHER_MAX;


struct Poly;

/** A row's value: its channel of OUT, counted over the rows left on before
it, or left out; nothing on a row with no channel in it. */
struct PolyRowQuantity : WithHint<SwitchQuantity> {
	std::string getDisplayValueString() override;
};

struct Poly : Gatherer<Poly, GATHER_MAX, GATHER_MAX> {
	enum ParamId {
		GATHER_PARAM,              // what I gather, the strip's choice (`Choice`)
		ENUMS(ON_PARAM, CHANNELS), // each row of the body in the jack or left out, a click on its bar
		PARAMS_LEN
	};
	enum InputId {
		IN_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		POLY_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	int n = 0;             // channels this scan
	float v[CHANNELS] = {}; // and their volts
	int rowOf[CHANNELS] = {}; // the row of the body each channel of the jack came from
	// Every Seq arpeggiating my jack, a playhead on the row its channel came
	// from (`Heads`), packed through the wake and published at its end, as a
	// Trg's rings carry the Seqs walking them.
	Heads headsWork;
	std::atomic<uint64_t> headsPacked{0};
	void arpStands(int c, int col, bool line, bool halo) override {
		if (c < 0 || c >= n)
			return;
		headsWork.add(rowOf[c], clamp(col, 0, MAX_COLORS - 1), line, halo);
		written();
	}
	int rowHeadsOut(int r, int* cols, bool* line, bool* halo) override {
		return Heads::on(relaxed(headsPacked), r, cols, line, halo);
	}
	void clearReads() override {
		Gatherer::clearReads();
		headsWork = Heads();
	}
	void publishReads() override {
		Gatherer::publishReads();
		headsPacked.store(headsWork.bits, std::memory_order_relaxed);
	}
	// The body shows every source, a row each (`srcV`), and the jack carries
	// the ones left on, closed up with no gap.
	bool rowOn(int c) { return params[ON_PARAM + c].getValue() > 0.5f; }

	Poly() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int c = 0; c < CHANNELS; c++)
			configSwitch<PolyRowQuantity>(ON_PARAM + c, 0.f, 1.f, 1.f, string::f("Row %d", c + 1), {"Off", "On"});
		configChoice(this, GATHER_PARAM, false);
		configInput(IN_INPUT, "Channels");
		configOutput(POLY_OUTPUT, "Poly");
		retipAll();
	}
	/** Which members to my right take my cable's channels, as the widget
	finds them along its run (`PolyWidget::feedsRight`): how many CVs and
	TRGs up to the next Poly, whose poly slot I fill, and how many channels
	they take between them (`Cartridge::feedWidth`), a byte each, the CVs
	lowest. */
	static int feedsOf(int cvs, int trgs, int width) {
		return std::min(cvs, 255) | std::min(trgs, 255) << 8 | std::min(width, 255) << 16;
	}
	std::atomic<int> feedsUi{0};
	/** And who gathers me to my right (`gatheredBy`), its manifest id, minus
	one for none, for OUT's lesson; and whether its strip asks for the
	nearest, so a cable in my IN, releasing what I gather, hands that to it. */
	std::atomic<int> gatheredByUi{-1};
	std::atomic<bool> gatheredNearUi{false};
	/** Who follows OUT's channels, for a sentence, counted so one CV is not
	called CVs: "The CV", "The CVs and TRG", empty for none; `one` whether
	it is one member, for the verb. */
	static std::string fedWho(int feeds, bool& one) {
		const int cvs = feeds & 0xff, trgs = feeds >> 8 & 0xff;
		one = cvs + trgs == 1;
		const std::string c = cvs > 1 ? "CVs" : "CV", t = trgs > 1 ? "TRGs" : "TRG";
		return cvs && trgs ? "The " + c + " and " + t : cvs ? "The " + c : trgs ? "The " + t : std::string();
	}
	/** The channel of OUT row `r` goes out on, counted over the rows left
	on before it, nought for a row left out (`GatherReads::outChannel`). */
	int outChannel(int r) override {
		if (r < 0 || r >= CHANNELS || !rowOn(r))
			return 0;
		int out = 0;
		for (int c = 0; c <= r; c++)
			out += rowOn(c);
		return out;
	}
	/** Every tooltip as the last wake left me, the MIX's words where the
	job is the same. */
	void retipAll() override {
		const int rows = relaxed(srcNNow);
		const bool jack = inputs[IN_INPUT].isConnected();
		const bool cables = relaxed(stackedNow);
		const int feeds = relaxed(feedsUi);
		bool one = false;
		const std::string who = fedWho(feeds, one);
		const int cvs = feeds & 0xff, trgs = feeds >> 8 & 0xff, width = feeds >> 16 & 0xff;
		// IN: a cable's channels on rows of their own, stacked or not, in
		// place of whatever is on the rows while something is; and, with
		// CVs or TRGs to my right, what they take of it (`pullFeed`), which
		// is then the surprise. A lesson holds two sentences, so the stacking
		// is said only where nothing to its right is: each channel of stacked
		// cables keeps a row of its own, where Rack would sum them (`Stack`),
		// which no hand guesses.
		const char* const STACK = "Each channel gets its own row, on stacked cables too.";
		// How many each takes is said while a cable is in, where it is
		// happening; while IN is empty only that they would follow, which with
		// what the cable would replace is the lesson's two sentences.
		const std::string verb = one ? " follows" : " follow";
		const char* const what = trgs == 0 && one ? " OUT's first channel" : one ? " OUT's channels" : " OUT's channels in turn";
		const std::string followed = who + " to its right" + verb + what;
		const std::string follow =
		    who.empty() ? std::string()
		    : !jack     ? who + " to its right then" + verb + what + "."
		                : followed + (trgs == 0 && one ? "."
		                              : trgs == 0  ? ", one each."
		                              : cvs == 0 && one ? ", one for each of its triggers."
		                              : cvs == 0   ? ", one for each trigger."
		                                           : ", one for each CV and each trigger.");
		std::string in;
		if (jack && cables)
			in = "Each channel of every stacked cable gets its own row.";
		else if (jack)
			in = follow.empty() ? std::string(STACK) : std::string();
		else if (rows > 0) {
			// A gatherer to my right asking for the nearest takes what a
			// cable here releases, which no hand guesses; said in place of
			// the stacking, and only where nothing follows OUT, the lesson
			// saying only the surprise.
			const int by = relaxed(gatheredByUi);
			const char* instead = !relaxed(gatheredNearUi) ? ""
			                      : by == manifest::M_POLY ? ", and the POLY to its right gathers them instead."
			                      : by == manifest::M_MIX  ? ", and the MIX to its right mixes them in instead."
			                                               : "";
			in = "A cable here takes the place of " + askedWords() + " on the rows" +
			     (!follow.empty() ? std::string(".") : *instead ? std::string(instead) : std::string(". ") + STACK);
		}
		else
			in = follow.empty() ? "Each channel of a cable here gets its own row, on stacked cables too."
			                    : "Each channel of a cable here gets its own row.";
		if (!follow.empty())
			in = in.empty() ? follow : in + " " + follow;
		retipIn(this, IN_INPUT, {"Channels", in.c_str()});
		// OUT: silent with nothing to carry, from nothing to its left, a
		// strip whose choice nothing free answers, or every row left out;
		// otherwise only the rows left out closing up and what was dropped.
		int on = 0;
		for (int c = 0; c < rows; c++)
			on += rowOn(c);
		std::string out;
		if (!jack && rows == 0) {
			out = silentFor(Choice::of(choiceCode()), "gather");
		}
		else if (rows > 0 && on == 0)
			out = "Silent: every row is left out.";
		else {
			out = on < rows ? std::string("Rows left out are skipped, and the rows after them close up the gap.") : std::string();
			const std::string dropped = droppedWords("row");
			if (!dropped.empty())
				out = out.empty() ? dropped : out + " " + dropped;
			// Gathered to my right, said as a gathered CV says it.
			const int by = relaxed(gatheredByUi);
			const char* taken = by == manifest::M_POLY  ? "The POLY to its right sends these channels on."
			                    : by == manifest::M_MIX ? "The MIX to its right mixes them in."
			                                            : "";
			if (*taken)
				out = out.empty() ? std::string(taken) : out + " " + taken;
			// Arpeggiated to my right: every SEQ doing it stands one line on
			// a row (`arpStands`), so the lines count them.
			const uint64_t heads = relaxed(headsPacked);
			int seqs = 0;
			for (int i = 0; i < (int)(heads & 0xf); i++)
				seqs += Heads::lineOf(heads, i);
			const char* played = seqs == 1 ? POLY_SEQ_PLAYS : seqs > 1 ? POLY_SEQS_PLAY : "";
			if (*played)
				out = out.empty() ? std::string(played) : out + " " + played;
		}
		retipOut(this, POLY_OUTPUT, {"Poly", out.c_str()});
		retipChoice();
		// Each row is named by where it came from, the source by its place
		// or the cable's channel (`rowFrom`), and its value says where it
		// goes (`PolyRowQuantity`); a lesson only for a grey row, or, with
		// CVs or TRGs following OUT, for what leaving a row out does to
		// them.
		const uint64_t kinds = relaxed(kindsNow);
		const bool fromJack = relaxed(fromJackNow);
		// Said for the row's own state, the ones that follow in turn
		// skipping a row left out, and only on a row they reach: one
		// whose channel, or the channel it would take back, is among those
		// they take, so the words are true of this row as it stands.
		const bool follows = !who.empty() && fromJack;
		const std::string leaveOut = follows ? followed + "; leave this row out and " + (!one ? "they skip it." : trgs ? "its triggers skip it." : "it takes the next row instead.") : std::string();
		const std::string skipped = follows ? followed + " and " + (!one ? "skip" : trgs ? "its triggers skip" : "skips") + " this row." : std::string();
		int onBefore = 0;
		for (int c = 0; c < CHANNELS; c++) {
			const bool reached = onBefore < width;
			onBefore += c < rows && rowOn(c);
			if (c >= rows) {
				retipParam(this, ON_PARAM + c, {string::f("Row %d", c + 1).c_str(), GATHER_EMPTY_ROW, Gestures{}});
				continue;
			}
			const std::string from = rowFrom(c, cables, fromJack, kinds);
			const char* lesson = !fromJack && relaxed(rgbNow[c]) == REPLACED_RGB ? GATHER_GREY_ROW : !reached ? "" : rowOn(c) ? leaveOut.c_str() : skipped.c_str();
			retipParam(this, ON_PARAM + c, {from.c_str(), lesson, Gestures{rowOn(c) ? &G_POLY_OUT : &G_POLY_BACK}});
		}
	}

	// The one slot, the reader before me, for my colour: what I bundle is
	// gathered over the finished parse, past any slot (`parsed`).
	enum { S_READER };
	ROW_SLOT_IS(M_POLY, S_READER, "reader");
	ROW_MANIFEST(M_POLY)
	// The cable in IN, offered to the members to my right, who take their
	// channels of it through their poly slot (`Cartridge::pullFeed`).
	int channelsMeant(int jack) override { return std::max(n, 1); } // the jack's width, for its glow
	int feedOut(const float*& out) override {
		out = v;
		return fromJack ? n : -1;
	}
	/** And what my jack carries, cable or bundle, for a Seq arpeggiating
	me. */
	int jackOut(const float*& out) override {
		out = v;
		return n;
	}
	/** Where channel `c` came from, its module's colour: a Seq's own, so an
	OUT strip of it wears it; a cable's channels are mine. */
	int chanColor[CHANNELS] = {};
	int feedColor(int c) override { return c >= 0 && c < n ? chanColor[c] : -1; }
	/** The cable's colour on channel `c` of my jack: stacked, its own
	cable's, by the row it came from. */
	uint32_t feedRgb(int c) override {
		if (!fromJack || c < 0 || c >= n)
			return 0;
		return stacked ? stack.rgb(rowOf[c]) : stack.first();
	}
	// What I offered last, for `published`: a change is what wakes them.
	int goneN = -1;
	bool goneJack = false;
	float goneV[CHANNELS] = {};
	int goneCol[CHANNELS] = {};
	bool published() override {
		bool any = Member::published();
		if (n != goneN || fromJack != goneJack) {
			goneN = n;
			goneJack = fromJack;
			any = true;
		}
		for (int c = 0; c < n; c++)
			if (goneV[c] != v[c] || goneCol[c] != chanColor[c]) {
				goneV[c] = v[c];
				goneCol[c] = chanColor[c];
				any = true;
			}
		return any;
	}
	/** My jack's channels, for a Poly or a Mix to my right that gathers
	Polys: every channel, one after the other. */
	int channelsOut(float* out) override {
		for (int c = 0; c < n; c++)
			out[c] = v[c];
		return n;
	}
	void evaluate(const ProcessArgs& args) override {
		// The rows as they came, for the body, then the jack: the rows left
		// on, closed up.
		readSources();
		int on = 0;
		for (int c = 0; c < srcN; c++)
			if (rowOn(c)) {
				v[on] = srcV[c];
				chanColor[on] = srcCol[c];
				rowOf[on] = c;
				on++;
			}
		n = on;
		outputs[POLY_OUTPUT].setChannels(std::max(n, 1));
		for (int c = 0; c < std::max(n, 1); c++)
			outputs[POLY_OUTPUT].setVoltage(c < n ? v[c] : 0.f, c);
		markRead();
	}
	/** Anything to name under the jack: a bundle that yields no channel is
	nothing to bundle. */
	bool carries() { return n > 0; }

};

std::string PolyRowQuantity::getDisplayValueString() {
	Poly* m = dynamic_cast<Poly*>(module);
	if (!m)
		return WithHint<SwitchQuantity>::getDisplayValueString();
	const int c = paramId - Poly::ON_PARAM;
	if (c >= relaxed(m->srcNNow))
		return std::string();
	const int out = m->outChannel(c);
	return out > 0 ? string::f("Channel %d of OUT", out) : std::string("Left out");
}

/** The body: a bar a channel, sixteen rows, bipolar about the middle, each in
the colour of the module it came from, white for a cable from outside; rows
past the count are ghosts. */
struct PolyScreen : Screen {
	Poly* module = nullptr;
	void drawLayer(const DrawArgs& args, int layer) override {
		Screen::drawLayer(args, layer);
		if (layer != 1)
			return;
		NVGcontext* vg = args.vg;
		int n = 4;
		uint64_t cols = 0;
		// Each bar's span, below nought and above it (`HeldSpan`).
		float foot[CHANNELS] = {0.f, -2.f, 0.f, 0.f}, top[CHANNELS] = {1.f, 0.f, 3.f, 0.5f};
		if (module) {
			cols = relaxed(module->colorsNow);
			n = relaxed(module->header) & 0xff;
			for (int c = 0; c < CHANNELS; c++)
				unpackSpan(relaxed(module->shown[c]), foot[c], top[c]);
		}
		const GatherRows r(box.size, stripShown(module));
		const float inset = r.inset, h = r.h;
		const float w = box.size.x - 2 * inset;
		for (int c = 0; c < CHANNELS; c++) {
			const float y = r.y(c) + (r.pitch - h) / 2.f;
			if (c >= n) {
				paintEmptyRow(vg, inset, y, w, h);
				continue;
			}
			const NVGcolor cc = gatheredRowColor(module ? relaxed(module->rgbNow[c]) : 0, cols, c);
			// A row left out of the jack keeps its level, faint, on a line.
			const bool on = !module || module->rowOn(c);
			const float t = on ? h : LINE_PX;
			drawSpanBar(vg, inset, y + (h - t) / 2.f, w, t, foot[c] / 10.f, top[c] / 10.f, on ? cc : color::alpha(cc, GATHER_FAINT));
		}
		// The Seqs arpeggiating my jack: each one's playhead across the row
		// its channel came from, the line and glow a Trg's ring carries
		// (`paintPlayhead`), several on one row clustered about its middle
		// in rig order, nearest Seq first.
		if (!module)
			return;
		const uint64_t heads = relaxed(module->headsPacked);
		for (int c = 0; c < std::min(n, CHANNELS); c++) {
			int hc[Heads::MAX];
			bool hl[Heads::MAX], hh[Heads::MAX];
			const int k = Heads::on(heads, c, hc, hl, hh);
			const float mid = r.y(c) + r.pitch / 2.f;
			for (int j = 0; j < k; j++)
				paintPlayhead(vg, box.size, playColor(hc[j]), mid + (j - (k - 1) / 2.f) * playheadStep(), false, hh[j],
				              hl[j]);
		}
	}
};

/** A row of the body as a switch: a click leaves its channel out of the jack
or puts it back, one undo, the channels left closing up with no gap; the
pointer over it outlines the row and lights its cable (`GatherRow`), and its
hover names where it came from and where it goes. It is a gesture, as the Seq's direction cell is: a double click
is two clicks. A row with no channel in it is not there for the hand: no
outline, no hover, no click. */
struct PolyRow : GatherRow<Poly, ParamWidget> {
	bool live() override {
		Poly* m = dynamic_cast<Poly*>(module);
		return m && index < relaxed(m->srcNNow);
	}
	void onDoubleClick(const DoubleClickEvent& e) override { e.consume(this); }
	void onButton(const ButtonEvent& e) override {
		if (!live())
			return;
		engine::ParamQuantity* pq = getParamQuantity();
		if (pq && e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			// The press flips this row, and the stroke paints its value
			// over every row it is dragged across (`RowPaint`).
			paintBegin(this, pq->getValue() > 0.5f ? 0.f : 1.f);
			wakePoly();
			e.consume(this);
			return;
		}
		ParamWidget::onButton(e);
	}
	void wakePoly() {
		if (Poly* m = dynamic_cast<Poly*>(module))
			m->wake();
	}
	void onDragStart(const DragStartEvent& e) override { e.consume(this); }
	void onDragEnter(const DragEnterEvent& e) override {
		GatherRow::onDragEnter(e);
		if (live() && paintOver(e.origin, this))
			wakePoly();
	}
	void onDragEnd(const DragEndEvent& e) override { paintEnd("change rows"); }
};

/** Whether an undo step plugs a cable into input `input` of the module
`id`, alone or among others, as a cable dropped on a jack is pushed, a
cable it replaces taken out in the same step (`anyAction`). */
bool cablesInto(history::Action* a, int64_t id, int input) {
	return anyAction(a, [id, input](history::Action* b) {
		if (dynamic_cast<history::CableRemove*>(b))
			return false;
		history::CableAdd* add = dynamic_cast<history::CableAdd*>(b);
		return add && add->inputModuleId == id && add->inputId == input;
	});
}

struct PolyWidget : GatherWidget<Poly, PolyScreen> {
	/** Which members take my cable's channels, read off my run, as a CV
	finds its gatherer (`CVWidget::gatheredBy`): every unbypassed CV and TRG
	to my right up to the next Poly, the nearest Poly to their left being
	the one whose cable they follow (`Cartridge::pullFeed`). */
	template <typename F>
	void eachFed(F f) {
		const std::vector<ModuleWidget*>* run = runOf(this, true);
		if (!run || !gatherer || gatherer->isBypassed())
			return;
		auto me = std::find(run->begin(), run->end(), this);
		if (me == run->end())
			return;
		for (auto it = me + 1; it != run->end(); ++it) {
			Member* m = dynamic_cast<Member*>((*it)->module);
			if (!m || m->isBypassed())
				continue;
			const int id = m->manifestId();
			if (id == manifest::M_POLY)
				break;
			if (id == manifest::M_CV || id == manifest::M_TRG)
				if (Cartridge* c = dynamic_cast<Cartridge*>(m))
					f(id, c);
		}
	}
	/** And how many they are, for the jack's lesson (`Poly::feedsOf`). */
	int feedsRight() {
		int cvs = 0, trgs = 0, width = 0;
		eachFed([&](int id, Cartridge* c) {
			cvs += id == manifest::M_CV;
			trgs += id == manifest::M_TRG;
			width += c->feedWidth();
		});
		return Poly::feedsOf(cvs, trgs, width);
	}
	/** Every cable I have seen in IN, by its id, since this panel was built:
	a cable is new to me only once, so an undo or a redo bringing one back
	finds it seen, and a patch loaded or a rig built is seen whole at the
	first step (`stepped`). */
	std::set<int64_t> cablesSeen;
	bool stepped = false;
	/** A new cable in IN, where there was none or in place of another,
	gives every member I feed that a hand set aside back to my channels
	(`Cartridge::setAsideParam`, a CV's or a Trg's OWN), on the UI thread as a hand's
	stroke is: one undo with the cable's own step when the hand has just
	pushed it, so one undo takes back both, else a step of its own. */
	void giveBack() {
		bool fresh = false;
		for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(getInput(Poly::IN_INPUT)))
			if (cw->cable && cablesSeen.insert(cw->cable->id).second)
				fresh = true;
		const bool first = !stepped;
		stepped = true;
		if (!fresh || first)
			return;
		history::ComplexAction* all = new history::ComplexAction;
		all->name = "follow the POLY again";
		eachFed([&](int, Cartridge* c) {
			const int p = c->setAsideParam();
			if (p < 0 || c->params[p].getValue() == 0.f)
				return;
			history::ParamChange* h = new history::ParamChange;
			h->moduleId = c->id;
			h->paramId = p;
			h->oldValue = c->params[p].getValue();
			h->newValue = 0.f;
			c->params[p].setValue(0.f);
			c->wake();
			all->push(h);
		});
		history::Action* cable = structuralStep();
		const bool join = cable && cablesInto(cable, gatherer->id, Poly::IN_INPUT);
		pushJoined(all, cable, join);
	}
	void step() override {
		if (gatherer) {
			giveBack();
			gatherer->feedsUi.store(feedsRight(), std::memory_order_relaxed);
			const GatheredBy by = gatheredBy(this);
			gatherer->gatheredByUi.store(by.kind, std::memory_order_relaxed);
			gatherer->gatheredNearUi.store(by.nearest, std::memory_order_relaxed);
		}
		GatherWidget::step();
	}
	PolyWidget(Poly* module) : GatherWidget(module, "POLY") {
		// The rows over the body, a switch each, and the strip over them.
		addRows<PolyRow>(CHANNELS, Poly::ON_PARAM);
		close(Poly::POLY_OUTPUT);
	}
};

} // namespace

Model* modelPoly = createModel<Poly, PolyWidget>("POLY");
