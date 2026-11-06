// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Panel.hpp"
#include "Stack.hpp"

/** The face a Poly and a Mix share: sixteen rows down the body, and over them,
while there is anything to its left to choose among, a strip naming what it
gathers. A stroke
on the strip, or its arrows, step through what it may gather, the nearest
kind or a kind and a group of it (`Choice`, Gather.hpp), and the pick is a
param, one undo. The
strip names the pick whether or not it is there: a choice nothing to the left
answers gathers nothing and still says what it asked for. Standing alone a
gatherer has no strip and its rows have the whole body, as they always had. */

namespace rig {

/** The rows of the body, the strip's or none at the top, in one place so the
screen and the hand over it agree. */
struct GatherRows {
	static constexpr float STRIP_MM = 4.5f;
	float top, pitch, inset, h, strip;
	GatherRows(Vec size, bool withStrip) {
		inset = mm2px(1.6f);
		top = mm2px(1.5f);
		strip = withStrip ? mm2px(STRIP_MM) : 0.f;
		pitch = (size.y - 2 * top - strip) / GATHER_MAX;
		h = pitch * 0.55f;
	}
	float y(int c) const { return top + strip + c * pitch; } // a row's top
};

inline bool stripShown(GatherReads* g) { return g && relaxed(g->stripNow); }

/** The cable row `c` of a gatherer came in by, lit over the others while
the pointer is on the row (`GatherRow`): the stacked cable the engine counted it from, in the
stacking `stackCables` lists, or the one cable in IN. A row gathered from
members has no cable and lights nothing. */
template <class G>
inline void glowRowCable(G* g, ModuleWidget* mw, int c) {
	if (!g || !mw || !relaxed(g->fromJackNow) || c < 0 || c >= std::min(relaxed(g->srcNNow), GATHER_MAX))
		return;
	const std::vector<CableWidget*> cables = stackCables(mw->getInput(G::IN_INPUT));
	const int k = relaxed(g->stackedNow) ? (int)((relaxed(g->cablesNow) >> (4 * c)) & 0xf) : 0;
	if (k < (int)cables.size())
		glowCable(cables[k]);
}

/** The row of a gatherer the pointer is on, whatever the hand is doing,
one for the whole rack (`GatherRow`). UI thread only. */
inline widget::Widget*& gatherPointed() {
	static widget::Widget* p = nullptr;
	return p;
}

/** A row of a gatherer's body under the hand, the one part a Poly's switch
and a Mix's fader share: the row the pointer is on is outlined, the module
it came from pointed at from the bracket, and the cable it came in by lit
over the others (`glowRowCable`), and all three follow the pointer through
every gesture. Rack's own hover stands still on the row a press began on
for as long as the button is held, so a stroke painted or scanned across
the rows would keep lighting the first; a row takes the pointer on every
hover and every drag passing over it, gives it up when the pointer leaves,
and takes it back when a drag is let go on it, since Rack leaves the row
first and drops the drag on it after. A stroke whose cursor is held still
on its own row (`stroking`) keeps it until it ends. A row with nothing in it
(`live`) is not there for the hand. A module's row overriding a drag's
enter calls this one's first. */
template <class G, class Base>
struct GatherRow : Hovered<Base> {
	float inset = 0.f;
	int index = 0;
	bool stroking = false;
	virtual bool live() = 0;
	G* gatherer() { return dynamic_cast<G*>(this->module); }
	void point(bool on) {
		widget::Widget*& p = gatherPointed();
		if (on && live())
			p = this;
		else if (!on && p == this && !stroking)
			p = nullptr;
	}
	~GatherRow() {
		if (gatherPointed() == this)
			gatherPointed() = nullptr;
	}
	void onEnter(const widget::Widget::EnterEvent& e) override {
		if (live())
			Hovered<Base>::onEnter(e);
		point(true);
	}
	void onHover(const widget::Widget::HoverEvent& e) override {
		point(true);
		Hovered<Base>::onHover(e);
	}
	void onLeave(const widget::Widget::LeaveEvent& e) override {
		point(false);
		Hovered<Base>::onLeave(e);
	}
	void onDragEnter(const widget::Widget::DragEnterEvent& e) override { point(true); }
	void onDragLeave(const widget::Widget::DragLeaveEvent& e) override {
		point(false);
		Hovered<Base>::onDragLeave(e);
	}
	void onDragDrop(const widget::Widget::DragDropEvent& e) override {
		point(true);
		Hovered<Base>::onDragDrop(e);
	}
	void draw(const widget::Widget::DrawArgs& args) override {}
	void drawLayer(const widget::Widget::DrawArgs& args, int layer) override {
		if (layer != 1 || gatherPointed() != this || !live())
			return;
		G* g = gatherer();
		if (g && index < relaxed(g->srcNNow)) {
			pointAtSource(relaxed(g->srcIdNow[index]));
			glowRowCable(g, this->template getAncestorOfType<ModuleWidget>(), index);
		}
		paintRowHover(args.vg, inset, this->box.size);
	}
};

/** The strip: a screen's button with the choice written on it, in the white
of everything a colourless gatherer names, the nearest kind and a chosen one
alike (a grey default beside a white choice would read as two kinds of
thing, and the default is no lesser choice, only the first). A stroke up
or down steps through
the choices (`choiceCycle`), held at either end with nothing kept past it, a
double click puts it back on the nearest, and where there is more than one
choice a step arrow at either end of the strip turns through them, round and
round; a click on the word does nothing. While the hand is over the strip,
an arrow hangs over every module it gathers. Every change is the param's,
one undo. */
struct GatherStrip : ScreenButton, Stroke {
	/** Mouse pixels a stroke travels from one choice to the next. */
	static constexpr float PX_PER_CHOICE = 12.f;
	/** The arrows, standing on the body at either end of the strip, their
	points on the margin line the rows below are trimmed to (`arm`, `step`). */
	GlyphButton* arrow[2] = {nullptr, nullptr};
	float startValue = 0.f, accum = 0.f;
	std::vector<int> strokeCycle; // the choices as the stroke began, which it steps through
	int strokeAt = 0;
	GatherStrip() {
		strip = true;
		textOf = [this]() { return word(); };
		colorOf = []() { return color::WHITE; };
	}
	/** The arrows, onto `body`, which holds the strip. Their ink is the
	word's at full strength, which a glyph button dims to its own levels. */
	void arm(Widget* body) {
		for (int side = 0; side < 2; side++) {
			const int by = side ? 1 : -1;
			GlyphButton* a = addStepArrow(body, Vec(), Vec(), side ? ARROW_RIGHT : ARROW_LEFT);
			a->ink = [this]() {
				NVGcolor c = colorOf();
				c.a = 1.f;
				return c;
			};
			a->act = [=]() { turn(by); };
			a->tipOf = [this]() { return std::string(gatherStripWords(mixes()).arrow); };
			a->visible = false;
			arrow[side] = a;
		}
	}
	GatherReads* reads() { return dynamic_cast<GatherReads*>(module); }
	int code() {
		engine::ParamQuantity* pq = getParamQuantity();
		return pq ? (int)std::lround(pq->getValue()) : 0;
	}
	std::string word() {
		GatherReads* g = reads();
		return choiceWord(Choice::of(code()), g ? relaxed(g->gatheredNow) : -1);
	}
	std::vector<int> cycle() {
		GatherReads* g = reads();
		return g ? g->cycle(code()) : std::vector<int>{0};
	}
	/** The choice `by` along from the one standing, round the ends. */
	int next(int by) {
		const std::vector<int> c = cycle();
		const int n = (int)c.size();
		const int at = (int)(std::find(c.begin(), c.end(), code()) - c.begin());
		return c[stepRound(at, by, n)];
	}
	/** Whether I stand on a Mix, whose words say mixed in. */
	bool mixes() {
		Member* m = dynamic_cast<Member*>(module);
		return m && m->readerKind() == READ_MIX;
	}
	// The word is the state: written in full, with no light behind it.
	bool on() override { return true; }
	float glow() override { return 0.f; }
	/** The param set to `c` and the row woken; the gesture records it. */
	void pick(int c) {
		engine::ParamQuantity* pq = getParamQuantity();
		if (!pq || c == code())
			return;
		pq->setValue((float)c);
		if (Member* m = dynamic_cast<Member*>(module))
			m->wake();
	}
	/** An arrow's click: one along, round the ends, one undo. */
	void turn(int by) {
		engine::ParamQuantity* pq = getParamQuantity();
		if (!pq)
			return;
		const float old = pq->getValue();
		pick(next(by));
		if (pq->getValue() != old)
			recordParamChange(this, old);
	}
	void step() override {
		// The arrows stand inside the strip's ends wherever it is laid, the
		// outer point of each on the margin line the rows below are trimmed
		// to, the strip's own edge, rather than against the glass's edge;
		// a glyph paints
		// at its box's middle, so each box reaches from the glass's edge as
		// far past the triangle as the margin before it, the whole of it a
		// click. Only while the strip is there with something to turn to.
		const bool many = visible && module && cycle().size() > 1;
		const float mid = box.pos.x + mm2px(ARROW_LEN) / 2.f; // an arrow's middle, from its side's edge
		const float bodyW = box.getRight() + box.pos.x;
		for (int side = 0; side < 2; side++)
			if (GlyphButton* a = arrow[side]) {
				a->box.pos = Vec(side ? bodyW - 2.f * mid : 0.f, box.pos.y);
				a->box.size = Vec(2.f * mid, box.size.y);
				a->visible = many;
			}
		ScreenButton::step();
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		ScreenButton::drawLayer(args, layer);
		// Where the gathering comes from, every module of it, while the hand
		// is over the strip or its arrows.
		if (layer == 1 && module && (pointerWithin(this) || pointerWithin(arrow[0]) || pointerWithin(arrow[1])))
			if (GatherReads* g = reads())
				for (int c = 0; c < std::min(relaxed(g->srcNNow), GATHER_MAX); c++)
					pointAtSource(relaxed(g->srcIdNow[c]));
	}
	/** A press is only the start of a stroke: a click chooses nothing. */
	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && module) {
			e.consume(this);
			return;
		}
		ParamWidget::onButton(e);
	}
	/** The nearest again, as any screen's double click puts its default
	back. Rack delivers it after the second press has begun its stroke, so
	the stroke's end records it. */
	void onDoubleClick(const DoubleClickEvent& e) override {
		e.consume(this);
		if (module)
			pick(0);
	}
	void onDragStart(const DragStartEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT || !module)
			return;
		engine::ParamQuantity* pq = getParamQuantity();
		startValue = pq ? pq->getValue() : 0.f;
		accum = 0.f;
		strokeBegins();
		strokeCycle = cycle();
		strokeAt = (int)(std::find(strokeCycle.begin(), strokeCycle.end(), code()) - strokeCycle.begin());
		if (settings::knobMode <= settings::KNOB_MODE_LINEAR)
			APP->window->cursorLock();
		ParamWidget::onDragStart(e);
	}
	/** Up is on through the choices, down back, a choice every
	`PX_PER_CHOICE`, the first at half of that, as every screen's stroke
	steps (`VerticalDragParam::stepsTaken`); at an end the travel left over
	is dropped, so turning back moves at once. */
	void onDragMove(const DragMoveEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT || !module || strokeCycle.empty())
			return;
		accum += strokeDelta(e);
		const int took = (int)std::round(accum / PX_PER_CHOICE);
		if (took == 0)
			return;
		accum -= took * PX_PER_CHOICE;
		const int last = (int)strokeCycle.size() - 1;
		int to = strokeAt - took;
		if (to <= 0 || to >= last) {
			to = clamp(to, 0, last);
			accum = 0.f;
		}
		if (to != strokeAt) {
			strokeAt = to;
			pick(strokeCycle[to]);
		}
	}
	void onDragEnd(const DragEndEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT || !module)
			return;
		if (settings::knobMode <= settings::KNOB_MODE_LINEAR)
			APP->window->cursorUnlock();
		engine::ParamQuantity* pq = getParamQuantity();
		if (pq && pq->getValue() != startValue)
			recordParamChange(this, startValue);
		ParamWidget::onDragEnd(e);
	}
};

/** The rows and the strip over a body, laid again whenever the strip comes
or goes. */
struct GatherLayout {
	Widget* body = nullptr;
	std::vector<Widget*> rows;
	GatherStrip* strip = nullptr;
	int laid = -1;
	void begin(Widget* b, Module* module, int choiceParam) {
		body = b;
		strip = createParam<GatherStrip>(Vec(0.f, 0.f), module, choiceParam);
		strip->visible = false;
		body->addChild(strip);
		strip->arm(body);
	}
	void add(Widget* r) {
		rows.push_back(r);
		body->addChild(r);
	}
	void lay(bool withStrip) {
		if ((int)withStrip == laid)
			return;
		laid = withStrip;
		const GatherRows r(body->box.size, withStrip);
		for (int c = 0; c < (int)rows.size(); c++) {
			rows[c]->box.pos = Vec(0.f, r.y(c));
			rows[c]->box.size = Vec(body->box.size.x, r.pitch);
		}
		strip->box.pos = Vec(r.inset, r.top);
		strip->box.size = Vec(body->box.size.x - 2 * r.inset, r.strip);
		strip->visible = withStrip;
	}
};

/** The strip's param, which reads as the kind it gathers now, in a
sentence's words, and says its stroke on the hint (`retipChoice`). */
struct ChoiceQuantity : HintQuantity {
	std::string getDisplayValueString() override {
		GatherReads* g = dynamic_cast<GatherReads*>(module);
		return choiceName(Choice::of((int)std::lround(getValue())), g ? relaxed(g->gatheredNow) : -1);
	}
};
/** The strip's param on a gatherer, every choice a whole one, named in
its own module's word, so a Mix never shows the Poly's. */
inline void configChoice(Module* m, int paramId, bool mix) {
	m->configParam<ChoiceQuantity>(paramId, 0.f, 1023.f, 0.f, gatherStripWords(mix).name);
	m->paramQuantities[paramId]->snapEnabled = true;
}

/** Who gathers `me`, read off what each gatherer to my right in my run
published of its rows (`srcIdNow`): the first whose rows hold me, its
manifest id, and the channel of its jack my first row goes out on
(`outChannel`, nought for a row left out or a Mix). A CV's lesson and a
Poly's OUT read it, as a Poly's row value reads the same count. */
struct GatheredBy {
	int kind = -1; // the gatherer's manifest id, minus one for none
	int channel = 0;
	bool nearest = false; // its strip asks for the nearest kind, the default
};
inline GatheredBy gatheredBy(ModuleWidget* me) {
	GatheredBy by;
	const std::vector<ModuleWidget*>* run = runOf(me, true);
	if (!run || !me->module)
		return by;
	const int64_t id = me->module->id;
	auto at = std::find(run->begin(), run->end(), me);
	for (auto it = at == run->end() ? at : at + 1; it != run->end(); ++it) {
		Member* m = dynamic_cast<Member*>((*it)->module);
		GatherReads* g = dynamic_cast<GatherReads*>((*it)->module);
		if (!m || !g)
			continue;
		const int rows = std::min(relaxed(g->srcNNow), GATHER_MAX);
		for (int r = 0; r < rows; r++)
			if (relaxed(g->srcIdNow[r]) == id) {
				by.kind = m->manifestId();
				by.channel = g->outChannel(r);
				by.nearest = std::lround(m->params[g->choiceParam()].getValue()) == 0;
				return by;
			}
	}
	return by;
}

/** A row's bar colour: the source's own where it publishes one, else the
row's packed colour (`gatheredColor`), four bits a row in `cols`. */
inline NVGcolor gatheredRowColor(uint32_t rgb, uint64_t cols, int c) {
	return rgb ? fromRGB(rgb) : gatheredColor((int)((cols >> (4 * c)) & 0xf));
}
/** How faint a row left out of the jack, or at nothing in the sum, is. */
constexpr float GATHER_FAINT = 0.22f;

/** A gatherer's panel, a Poly's or a Mix's, which differ by the title: the
caption under the jack is OUT, whatever is gathered, as every output's is;
the strip names the kind. */
template <class M>
struct GatherPanel : Panel {
	const char* title = "";
	void draw(const DrawArgs& args) override {
		drawPlate(args, this, title);
		// Cables stack here: IN, and a quiet (s) after it.
		drawCaptionAside(args.vg, CX, IN_CAP_Y, "IN", "(s)", INK);
		drawOutBlock(args.vg, BLOCK_Y, BLOCK_H);
		drawCaption(args.vg, CX, FOOT_CAP_Y, "OUT", PALE);
	}
};

/** A gatherer's widget: the panel, IN, the body and the strip over it, the
rows a module adds (`addRows`) and the jack at the foot (`close`); every
frame the cables stacked in IN are listed for the engine and the strip laid
or taken away. */
template <class M, class Body>
struct GatherWidget : MemberWidget {
	M* gatherer = nullptr;
	GatherLayout face;
	Body* body = nullptr;
	void step() override {
		if (gatherer && publishStack(gatherer->stack, getInput(M::IN_INPUT)))
			gatherer->wake();
		face.lay(stripShown(gatherer));
		MemberWidget::step();
	}
	GatherWidget(M* module, const char* title) {
		gatherer = module;
		GatherPanel<M>* panel = new GatherPanel<M>;
		panel->title = title;
		open(module, panel);

		addInput(createInputCentered<InPort>(mm2px(Vec(CX, IN_Y)), module, M::IN_INPUT));
		// The body edge to edge, one surface with a Trg's or a CV's.
		body = createWidget<Body>(mm2px(Vec(0.f, SCREEN_Y)));
		body->box.size = mm2px(Vec(PANEL_W, SCREEN_H));
		body->module = module;
		addChild(body);
		face.begin(body, module, M::GATHER_PARAM);
	}
	/** The rows over the body, laid as it lays its bars: `n` of them on the
	params from `firstParam`. */
	template <class R>
	void addRows(int n, int firstParam) {
		const float inset = GatherRows(body->box.size, false).inset;
		for (int c = 0; c < n; c++) {
			R* r = createParam<R>(Vec(), gatherer, firstParam + c);
			r->inset = inset;
			r->index = c;
			face.add(r);
		}
	}
	void close(int outputId) {
		face.lay(false);
		addOutput(createOutputCentered<OutPort>(mm2px(Vec(CX, FOOT_Y)), gatherer, outputId));
	}
};

} // namespace rig
