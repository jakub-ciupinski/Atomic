// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GroundParts.hpp"
#include "Tips.hpp"
#include "Words.hpp"
#include "Cartridge.hpp" // a member following a Poly, taken over by a stroke (`takeOver`)
#include "NoteRows.hpp" // the caret's walk a text field's keys name (`Hand::caret`)

/** What a hand touches: the param widgets and their strokes, a gesture
made one undo, the modifier keys, the screens that are drags and buttons,
and the cable colours a widget reads in its step for its module. Part of
Parts.hpp. */

namespace rig {

/** The module behind a param widget, as its own type, or nullptr. */
template <typename T>
inline T* paramModule(ParamWidget* w) {
	engine::ParamQuantity* pq = w->getParamQuantity();
	return pq ? dynamic_cast<T*>(pq->module) : nullptr;
}

/** Every widget of class T in my run whose box the segment from rack point
`a` to rack point `b` crosses, both ends included, in no particular order. A
paint sweep asks it on every move for the cells the pointer passed since the
last one: Rack's drag-enter fires only on the widget under the pointer at each
event, so a brisk stroke down a roll or a bar of rings would skip the cells
between two events and never paint them. The segment is walked a pixel at a
time, which is exact enough for cells several pixels tall. */
template <typename T>
inline void cellsCrossed(Widget* self, math::Vec a, math::Vec b, std::vector<T*>& out) {
	ModuleWidget* mine = self->getAncestorOfType<ModuleWidget>();
	if (!mine || !mine->module)
		return;
	const std::vector<ModuleWidget*>* run = runOf(mine);
	if (!run)
		return;
	std::vector<T*> cells;
	std::function<void(Widget*)> gather = [&](Widget* w) {
		for (Widget* c : w->children) {
			if (T* t = dynamic_cast<T*>(c))
				cells.push_back(t);
			gather(c);
		}
	};
	const float lo = std::min(a.x, b.x) - 1.f, hi = std::max(a.x, b.x) + 1.f;
	for (ModuleWidget* mw : *run)
		if (!(hi < mw->box.pos.x || lo >= mw->box.getRight()))
			gather(mw);
	const math::Vec d = b.minus(a);
	const int steps = std::max(1, (int)std::ceil(std::max(std::fabs(d.x), std::fabs(d.y))));
	for (T* t : cells) {
		if (!t->visible)
			continue;
		const math::Vec at = t->getRelativeOffset(math::Vec(0.f, 0.f), APP->scene->rack);
		const math::Rect r(at, t->box.size);
		bool hit = false;
		for (int k = 0; k <= steps && !hit; k++)
			hit = r.contains(a.plus(d.mult((float)k / (float)steps)));
		if (hit)
			out.push_back(t);
	}
}

#include "Scheme.hpp"

#include "Gesture.hpp"

/** A widget laid on the panel the panel's whole width, `y` millimetres down
it and `h` tall, bound to `paramId`: how a module places a screen that is
its parameter. */
template <class T>
T* addScreen(ModuleWidget* w, Module* module, int paramId, float y, float h) {
	T* s = createParam<T>(mm2px(Vec(0.f, y)), module, paramId);
	s->box.size = mm2px(Vec(PANEL_W, h));
	w->addChild(s);
	return s;
}

/** The test rig's keys: shift, command and alt held down by the test
harness rather than by a hand. Nothing in the plugin ever sets them; the
harness does, around a scripted stroke, because the real answer comes from the
keyboard and a script has no keyboard. They are a seam rather than a mock:
the stroke they run is the stroke a hand runs, down to the same `setFromY`.
Only `Hand` reads them. */
inline bool& heldShift() {
	static bool held = false;
	return held;
}
inline bool& heldCommand() {
	static bool held = false;
	return held;
}
inline bool& heldAlt() {
	static bool held = false;
	return held;
}

/** The one reader of the keyboard: a handler asks whether a gesture's keys are held, and can ask no
other way, so a modifier read without a declared `Gesture` (Words.hpp), and
so without its words on the bracket, does not compile, and nothing else in
the plugin reads the keys. Command is Rack's control key,
command on a Mac and control on Windows and Linux, the key Rack itself uses
for its fine drag; alt is Rack's own for panning the rack, and a stroke with
it held never reaches a module.

`held`, the gesture's keys and no others, now (the rig's keys count as
held whatever else is); `among`, the gesture's keys now, others allowed, so
a shift-drag is still one with command added; `in` and `within`, the same
two of an event's own keys, for a press; `caret`, a text field's caret
keys; `readout`, a value finer than its
screen shows, said on the bracket during a fine stroke, or one a part shows
not at all, a lamp's time, during any stroke of it. */
struct Hand {
	/** The precision readout: what a
	fine stroke has made, more exactly than its screen can say, or what a
	stroke of a lamp has made, which its face does not say at all
	(`EdgeLampParam::say`), set by the widget being dragged as it moves and shown first on the bracket's hint line
	only while that widget is still the one dragged (`hintFrame`, Bracket.hpp).
	A value, so it never enters a `Hinted` or a tooltip. Empty says nothing,
	which a stroke sets when its fine keys are let go. */
	struct Readout {
		const Widget* by = nullptr;
		std::string text;
	};
	static Readout& readoutNow() {
		static Readout r;
		return r;
	}
	static void readout(const Widget* by, const std::string& text) {
		Readout& r = readoutNow();
		r.by = by;
		if (r.text != text)
			r.text = text;
	}
	static bool held(const Gesture& g) {
		switch (g.mod) {
			case Mod::None: return !shift() && !command() && !alt() && now() == 0;
			case Mod::Shift: return heldShift() || now() == GLFW_MOD_SHIFT;
			case Mod::Command: return heldCommand() || now() == RACK_MOD_CTRL;
			case Mod::Alt: return heldAlt() || now() == GLFW_MOD_ALT;
			default: return among(g);
		}
	}
	static bool among(const Gesture& g) {
		switch (g.mod) {
			case Mod::None: return true;
			case Mod::Shift: return shift();
			case Mod::Command: return command();
			case Mod::Alt: return alt();
			case Mod::ShiftCommand: return shift() && command();
			case Mod::CommandOrAlt: return command() || alt();
		}
		return false;
	}
	static bool in(const Gesture& g, int mods) {
		const int m = mods & RACK_MOD_MASK;
		if (g.mod == Mod::CommandOrAlt) {
			const int k = m & ~GLFW_MOD_SHIFT;
			return k == RACK_MOD_CTRL || k == GLFW_MOD_ALT;
		}
		return m == chord(g);
	}
	/** The chord of a key's gesture with shift let through, as Alt+D
	duplicates with shift held or not. */
	static bool loose(const Gesture& g, int mods) {
		return ((mods & RACK_MOD_MASK) & ~GLFW_MOD_SHIFT) == (chord(g) & ~GLFW_MOD_SHIFT);
	}
	static bool within(const Gesture& g, int mods) {
		const int m = mods & RACK_MOD_MASK;
		if (g.mod == Mod::CommandOrAlt)
			return (m & (RACK_MOD_CTRL | GLFW_MOD_ALT)) != 0;
		return (m & chord(g)) == chord(g);
	}
	/** A key that walks a text field's caret over its drawn rows, as `m`,
	or false for the field's own keys: up and down, home and end, and on a
	Mac command with left or right, as Rack has them, to a row's ends;
	`stretch` is shift held with it, which stretches the selection. */
	static bool caret(const Widget::SelectKeyEvent& e, note::Move& m, bool& stretch) {
		struct K {
			int key, mods;
			note::Move m;
		};
		const K keys[] = {
		    {GLFW_KEY_UP, 0, note::UP},
		    {GLFW_KEY_DOWN, 0, note::DOWN},
		    {GLFW_KEY_HOME, 0, note::HOME},
		    {GLFW_KEY_END, 0, note::END},
#if defined ARCH_MAC
		    {GLFW_KEY_LEFT, RACK_MOD_CTRL, note::HOME},
		    {GLFW_KEY_RIGHT, RACK_MOD_CTRL, note::END},
#endif
		};
		for (const K& k : keys)
			for (const bool shift : {false, true})
				if (e.isKeyCommand(k.key, k.mods | (shift ? GLFW_MOD_SHIFT : 0))) {
					m = k.m;
					stretch = shift;
					return true;
				}
		return false;
	}

  private:
	static int now() { return APP->window ? APP->window->getMods() & RACK_MOD_MASK : 0; }
	static bool shift() { return heldShift() || (now() & GLFW_MOD_SHIFT); }
	static bool command() { return heldCommand() || (now() & RACK_MOD_CTRL); }
	static bool alt() { return heldAlt() || (now() & GLFW_MOD_ALT); }
	static int chord(const Gesture& g) {
		switch (g.mod) {
			case Mod::Shift: return GLFW_MOD_SHIFT;
			case Mod::Command: return RACK_MOD_CTRL;
			case Mod::Alt: return GLFW_MOD_ALT;
			case Mod::ShiftCommand: return GLFW_MOD_SHIFT | RACK_MOD_CTRL;
			default: return 0;
		}
	}
};

/** A widget that says gestures without being a param, or says other ones
than its param's: a Trg's black between its rings. The hint line asks the
widget under the pointer (`hintedOf`) before it climbs to a param. */
struct HintSource {
	virtual const Hinted* hinted() = 0;
	/** The module the gestures are said for, where the source stands outside
	it, as the swap menu stands over a module from the rack; none for one
	inside its module's panel, which the line finds by climbing. */
	virtual ModuleWidget* hintsFor() { return nullptr; }
	virtual ~HintSource() {}
};
/** The gestures the widget `w` says, climbing from it: the first
`HintSource` or param with a hinted quantity; `at` the widget that said
them. Null where nothing on the way says any. */
inline const Hinted* hintedOf(Widget* w, Widget*& at) {
	for (; w; w = w->parent) {
		if (HintSource* s = dynamic_cast<HintSource*>(w)) {
			at = w;
			return s->hinted();
		}
		if (ParamWidget* pw = dynamic_cast<ParamWidget*>(w)) {
			at = w;
			return dynamic_cast<const Hinted*>(pw->getParamQuantity());
		}
	}
	return nullptr;
}

/** Mouse pixels per step of a screen dragged, the same on every screen, at
Rack's own knob sensitivity (`strokeDelta` follows the user's setting). */
const float PX_PER_STEP = 4.f;
/** And per rung of a rate, the same on every rate screen: a Clock's division,
a Seq's rate. */
const float PX_PER_RUNG = 3.f;
/** And per step of a Seq's playhead dragged. */
const float PX_PER_POS = 4.f;
/** How far a click may wobble and still be a click, in mouse pixels. */
const float CLICK_SLOP = 3.f;

/** A stroke's travel read the way a locked cursor gives it: what every drag
on a screen counts its pixels with, a `VerticalDragParam` and a Trg's ring
count alike. */
struct Stroke {
	double strokeFrom = 0.0; // when the stroke began, for `strokeDelta`
	int strokeMoves = 0;
	float strokeTravel = 0.f; // pixels the pointer has travelled this stroke, either way
	Vec strokeLeap;           // the one false move a locked cursor can put early in a stroke: see `strokeDelta`
	/** Marks the start of a stroke, for `strokeDelta`. */
	void strokeBegins() {
		strokeFrom = system::getTime();
		strokeMoves = 0;
		strokeTravel = 0.f;
		// Where the press stood from the window's middle, in the units a move
		// is read in: the false move is this distance, whole.
		strokeLeap = Vec();
		if (APP->window && APP->scene) {
			const float ratio = APP->window->windowRatio > 0.f ? APP->window->pixelRatio / APP->window->windowRatio : 1.f;
			strokeLeap = APP->scene->getMousePos().minus(APP->window->getSize().div(2.f * ratio));
		}
	}
	/** The pointer's travel this move, with the one spurious leap a locked
	cursor on a Mac can put at the start of a stroke taken out. Rack hides the
	cursor and warps it to the window's centre when a drag begins, and reads
	each frame's travel as the distance from there; a first reading taken
	before the warp has landed is the distance from the press to the centre,
	hundreds of pixels, which a screen at six pixels a step turns into a jump
	to the end of its range, where a knob at a thousandth of its range a pixel
	barely moves. Rack's own Window.cpp has been patched for it several times. So the
	first move of a stroke, and only the first, is dropped when it is over a
	hundred pixels, which no hand makes in the frame after a press; a tighter
	rule, any move over twenty-four pixels in the first quarter second, lost a
	brisk flick's whole start and turned a drag on a Clock's division into a
	click. A dropped move is written to Rack's log, which is how the cause
	gets confirmed. */
	float strokeDelta(const widget::Widget::DragMoveEvent& e) {
		const double since = system::getTime() - strokeFrom;
		const float dy = e.mouseDelta.y;
		// And whatever move of the first few is that very distance, from the
		// press to the window's middle, which is what the cursor reads before
		// Rack's warp has landed, and can be read on the second move as well
		// as the first, and at any size: a stroke on a rate took one now and
		// then to a far value at its start. A hand's move matching it to a
		// few pixels at the start of a stroke is not a thing a hand does.
		const bool warp = strokeMoves < 4 && strokeLeap.norm() > 12.f && e.mouseDelta.minus(strokeLeap).norm() < 4.f;
		const bool leap = warp || (strokeMoves == 0 && std::max(std::fabs(dy), std::fabs(e.mouseDelta.x)) > 100.f);
		if (leap)
			INFO("Atomic stroke: a first move of %.1f,%.1f dropped (%s, move %d, the press %.1f,%.1f from the middle), %.3fs in",
			     e.mouseDelta.x, dy, warp ? "the warp" : "over a hundred", strokeMoves, strokeLeap.x, strokeLeap.y, since);
		// And a large move early in a stroke that was kept, which is the
		// other half of the evidence: a screen that now and then jumps to a
		// far value at a stroke's start (seen once, not
		// reproduced) either shows a warp here that neither test knew, or a
		// hand.
		else if (strokeMoves < 4 && std::max(std::fabs(dy), std::fabs(e.mouseDelta.x)) > 24.f)
			INFO("Atomic stroke: an early move of %.1f,%.1f kept (move %d, the press %.1f,%.1f from the middle), %.3fs in",
			     e.mouseDelta.x, dy, strokeMoves, strokeLeap.x, strokeLeap.y, since);
		strokeMoves++;
		if (!leap)
			strokeTravel += std::fabs(dy);
		// Rack's knob sensitivity, which its own knobs follow, as a factor on
		// its default: a user who has made knobs faster finds screens faster
		// too. The click's wobble above is measured unscaled.
		const float speed = settings::knobLinearSensitivity > 0.f ? settings::knobLinearSensitivity / 0.001f : 1.f;
		return leap ? 0.f : dy * speed;
	}
	/** Whether the stroke has become a drag: the pointer has travelled more
	than a click's wobble, so a trackpad click that slides a pixel is a click
	and not a drag of no steps, and on a Seq's step a click, which is a reset,
	never becomes a jump to the step already playing. */
	bool strayed() const { return strokeTravel > CLICK_SLOP; }
};

/** `steps` lines of a grid of `unit` on from `n`, up for a positive count:
the first the next line past `n` that way, so a value off the grid lands on
it and never jumps one, and held at the last line within lo..hi, never moving
back past `n`, where the end itself is off the grid (a transpose to +99 by
twelves stops at +96). With a unit of one it is `n` plus the steps, held at
the ends. Integers throughout, by floor division. */
inline int gridStep(int n, int unit, int steps, int lo, int hi) {
	if (steps == 0 || unit <= 0)
		return n;
	const int top = math::eucDiv(hi, unit) * unit, foot = -math::eucDiv(-lo, unit) * unit;
	if (steps > 0)
		return std::min((math::eucDiv(n, unit) + steps) * unit, std::max(n, top));
	return std::max((-math::eucDiv(-n, unit) + steps) * unit, std::min(n, foot));
}

/** A double click's reset, kept from the hand that made it. Rack delivers a
double click after its second press has begun a stroke, and a hand never lets
go without a pixel's tremor, so a screen that went on following that stroke
would undo the reset it had just been given: a fader's level following the
pointer, or a screen that steps by the pixel, a Mix's row or a Clock's
frequency. The stroke that carried a double click is spent and moves
nothing more; a press begins a fresh one. Every screen with a double-click
reset keeps it, the faders (`LevelParam`) and the stepping screens
(`VerticalDragParam`) alike, each passing its moves through `moveUnlessSpent`
and saying in `strokeMove` what a move does. */
struct ResetSpends {
	bool spent = false;
	virtual ~ResetSpends() {}
	virtual void strokeMove(const widget::Widget::DragMoveEvent& e) = 0;
	void moveUnlessSpent(const widget::Widget::DragMoveEvent& e) {
		if (!spent)
			strokeMove(e);
	}
};

/** A parameter set by dragging up and down, `pxPerUnit` mouse pixels per
unit, the way Rack's knobs drag: the cursor is held still in the knob modes
that ask for it, and the change is recorded for undo. A unit is one of the
param by default; `unit()` says otherwise, a hundredth of a cycle say, and the
value is snapped to it. A click on a value changes nothing and a double click
puts it back to its default, one undo, the rule of VCV's own knobs:
`resetToDefault` says what the default is where a screen has more to it than
its param. */
struct VerticalDragParam : ParamWidget, Stroke, ResetSpends {
	float pxPerUnit = PX_PER_STEP;
	float startValue = 0.f;
	float dragAccum = 0.f;
	bool moved = false;

	virtual void onDragActive(bool active) {}
	/** Whether the value moves nothing now, or is not the hand's to move: a
	rate with nothing reaching its module to be paced, a Clock's tempo while a
	cable sets it. Then the screen takes no stroke and no reset, since a value
	that does nothing is not set blind (a Seq
	with no clock, its rate dimmed, does not turn it under a drag), and a screen
	draws itself as it says it is. Asked only with a module behind it. */
	virtual bool idle() { return false; }
	/** Marks the start of a stroke, for `strokeDelta`. */
	void strokeBegins() {
		Stroke::strokeBegins();
		spent = false;
	}
	/** The whole steps the travel in the accumulator has crossed, taken out
	of it: the nearest whole, so a stroke from rest crosses to the next value
	at half a unit's travel and to each after it at a whole one, the same up
	and down, and turning back crosses back where it crossed; truncated, the
	first step would wait a whole unit, which on a rate's screen or a
	divider's count feels like a drag that will not start. What is left stays
	within half a unit either way, and an end (`landStep`) empties it, so a
	stroke past an end turns back at half a unit as well. An idle screen
	takes no step at all (`idle`). */
	float stepsTaken() {
		if (module && idle()) {
			dragAccum = 0.f;
			return 0.f;
		}
		if (pxPerUnit <= 0.f)
			return 0.f;
		const float took = std::round(dragAccum / pxPerUnit);
		dragAccum -= took * pxPerUnit;
		return took;
	}
	virtual float unit() { return 1.f; }
	/** The values the screen can show, which a stroke never leaves: the
	param's own range, unless the screen says less. A Seq's step picks one of
	the four CVs it walks from a param that holds ninety-nine, and a stroke up
	past the fourth would go on stepping where nobody could see it, so turning
	back would take as many steps again before the number moved. A stroke
	steps from what is shown, never from what is held. */
	virtual void span(engine::ParamQuantity* pq, float& lo, float& hi) {
		lo = pq->getMinValue();
		hi = pq->getMaxValue();
	}
	/** One step of a stroke landed: the value set, kept inside `span`, and
	at an end the pixels left over dropped, so a stroke past an end takes
	hold again where it stands and turning back moves the value at the first
	half unit's worth of travel (`stepsTaken`). */
	void landStep(engine::ParamQuantity* pq, float v, float lo, float hi) {
		const float to = clamp(v, lo, hi);
		if (to == lo || to == hi)
			dragAccum = 0.f;
		pq->setValue(to);
	}
	/** The value back to its default: the param's own, as a stock knob's
	double click has it. A screen whose default is more than one param says
	so here (a Seq's step screen, whose double click is a restart while
	it is driven, does). */
	virtual void resetToDefault(engine::ParamQuantity* pq) { pq->setValue(pq->getDefaultValue()); }
	/** A double click is the reset, and the one place a value is set back.
	Rack delivers it after the second press has begun its stroke, so the
	stroke's end records it, one undo from where the value stood, and Rack's
	own reset, which would push an undo of its own beside the stroke's, never
	runs. A single click changes nothing. */
	void onDoubleClick(const DoubleClickEvent& e) override {
		e.consume(this);
		engine::ParamQuantity* pq = getParamQuantity();
		if (!pq || (module && idle()))
			return;
		spent = true;
		resetToDefault(pq);
	}

	void onDragStart(const DragStartEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		engine::ParamQuantity* pq = getParamQuantity();
		startValue = pq ? pq->getValue() : 0.f;
		dragAccum = 0.f;
		moved = false;
		strokeBegins();
		if (wheeling)
			strokeLeap = Vec(); // no cursor to warp under a wheel
		else if (settings::knobMode <= settings::KNOB_MODE_LINEAR)
			APP->window->cursorLock();
		onDragActive(true);
		ParamWidget::onDragStart(e);
	}
	/** A move of a stroke, which a screen with a stroke of its own says in
	`strokeMove`; a stroke spent on a double click's reset (`ResetSpends`)
	never reaches it, whatever the screen. */
	void onDragMove(const DragMoveEvent& e) final { moveUnlessSpent(e); }
	/** The stroke moves the value, it does not place it: every time the
	pointer's travel crosses a unit's worth of pixels (the first at half of
	one, `stepsTaken`) the value takes one step of whatever the unit is at
	that moment, and the pixels left over stay in the accumulator. So a unit
	that changes mid-stroke (a shift held down, a hundred where a one was)
	carries on from where the number stands instead of recomputing it from
	where the stroke began, which would make one screen hold two numbers at
	once and jump between them. A step lands on the grid of the unit it was
	taken in: a unit that is a count steps by `gridStep`, so a shifted
	transpose stepping by sevens goes from 4 to 7 and letting shift go
	carries on from 7 by ones, and a fraction rounds to its own grid. */
	void strokeMove(const DragMoveEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		engine::ParamQuantity* pq = getParamQuantity();
		if (!pq)
			return;
		const float dy = strokeDelta(e);
		moved = moved || strayed();
		dragAccum += dy;
		const float u = unit();
		if (u <= 0.f || pxPerUnit <= 0.f)
			return;
		const float took = stepsTaken();
		if (took == 0.f)
			return;
		float lo, hi;
		span(pq, lo, hi);
		const float from = clamp(pq->getValue(), lo, hi);
		const int whole = (int)std::round(u);
		if ((float)whole == u) {
			// A unit that is a count steps as integers, exact where the
			// thing is exact, so a value off the unit's grid, a
			// transpose of 4 under a shift stepping by sevens, lands on the
			// next line of it, 7, and never skips one.
			landStep(pq, (float)gridStep((int)std::round(from), whole, -(int)took, (int)std::ceil(lo), (int)std::floor(hi)), lo, hi);
			return;
		}
		landStep(pq, std::round((from - took * u) / u) * u, lo, hi);
	}
	void onDragEnd(const DragEndEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		if (!wheeling && settings::knobMode <= settings::KNOB_MODE_LINEAR)
			APP->window->cursorUnlock();
		onDragActive(false);
		// A double click's reset is recorded here, and so is whatever a
		// click's wobble nudged on a screen that moves by the pixel, like any
		// change.
		recordStroke();
		ParamWidget::onDragEnd(e);
	}
	/** The stroke's one step of undo, from where the value stood at its
	press: a screen that changes more than its param in a stroke (a rate's
	mode, `RateScreen`) says so here. */
	virtual void recordStroke() { recordParamChange(this, startValue); }

	/** The scroll wheel, under Rack's own "Scroll wheel knob control"
	setting and only under it, as Rack's knobs take it: the wheel is a
	stroke, the same press, moves and release a drag on the screen is, so
	every screen steps by its own ladder, its keys held as a drag holds them,
	and an end keeps no travel (`landStep`). A notch is one step at Rack's
	default scroll sensitivity, whatever the knobs' drag speed, up the wheel
	up the value. The stroke opens at the first notch and is closed, one
	undo, when the pointer leaves the screen or presses it, where a stock
	knob records its wheel. A screen whose press is more than the start of
	a stroke says no in `wheelTakes`, and the wheel scrolls the rack. */
	bool wheeling = false;
	virtual bool wheelTakes() { return true; }
	void onHoverScroll(const HoverScrollEvent& e) override {
		if (!settings::knobScroll || !getParamQuantity() || (module && idle()) || APP->scene->rackScroll->isScrolling() ||
		    (!wheeling && !wheelTakes())) {
			ParamWidget::onHoverScroll(e);
			return;
		}
		e.consume(this);
		if (!wheeling) {
			ButtonEvent press;
			press.pos = e.pos;
			press.button = GLFW_MOUSE_BUTTON_LEFT;
			press.action = GLFW_PRESS;
			press.mods = 0;
			onButton(press);
			wheeling = true;
			DragStartEvent start;
			start.button = GLFW_MOUSE_BUTTON_LEFT;
			onDragStart(start);
		}
#if defined ARCH_MAC
		const float notch = 10.f; // Rack's scroll delta for one notch (Window.cpp)
#else
		const float notch = 50.f;
#endif
		const float drag = settings::knobLinearSensitivity > 0.f ? settings::knobLinearSensitivity / 0.001f : 1.f;
		const float wheel = settings::knobScrollSensitivity / 0.001f;
		DragMoveEvent move;
		move.button = GLFW_MOUSE_BUTTON_LEFT;
		move.mouseDelta = Vec(0.f, -e.scrollDelta.y / notch * pxPerUnit * wheel / drag);
		onDragMove(move);
		// A wheel is never a click.
		moved = true;
	}
	/** The wheel's stroke closed: one undo for all its notches. */
	void wheelEnds() {
		if (!wheeling)
			return;
		DragEndEvent end;
		end.button = GLFW_MOUSE_BUTTON_LEFT;
		onDragEnd(end);
		wheeling = false;
	}
	void onLeave(const LeaveEvent& e) override {
		wheelEnds();
		ParamWidget::onLeave(e);
	}
	void onButton(const ButtonEvent& e) override {
		if (wheeling)
			wheelEnds();
		ParamWidget::onButton(e);
	}
};

#include "Screens.hpp"

/** Command held is the finest setting: a stroke at a fiftieth of the pace. */
const float FINE_PACE = 50.f;

/** A lamp on the edge of a block, a diode: a half round of the block's ink
standing out of its left or right edge with a lamp in it (`EdgeTab`,
drawn by the panel in the block's outline, and `paintEdgeLamp`), lit in `colorOf` while `onParam` is on
and dim while off, with no word on the panel; its tooltip names it. Two
kinds, one part: the slew lamp and BLEP's and Oversampling's diodes are the
same element.

A lamp that drags (`clicked` empty) is a value and its switch: the slew at
a Seq's OUT. Its stroke runs along a scale whose bottom is off, one step
below the value's lowest: up from off lights it at the bottom, down past
the bottom puts it out there. A plain stroke steps the value's whole
numbers, a rung at a rate's pace, and from between two lands on the next
whole one the way it goes; a command stroke moves it by a fiftieth of one
at the same pace (`FINE_PACE`), anywhere on the scale, and lands on that
grid, a count of fiftieths, so a whole one stays whole. A stroke past
either end keeps no travel: it turns back at the first half step. A press
that does not stray past a click's wobble moves nothing; shift and a click
turns the switch over, the value kept, so a second gives it back; a double
click is the default, which is off, the switch put out and the value kept
for a shift-click to light again, so the default says honestly that there
is no slew; each is one undo.

A lamp that is a switch alone says what a press does in `clicked`, handed
whether command and shift are held, and is no stroke: BLEP's and
Oversampling's either side of OUT, whose gestures reach the other diode and
the other Seqs of the rig, the bracket's to make (`setAlongRun`, Bracket.hpp).

`shown` says whether it means anything where its module stands, and it is
not there while it does not, so a hand never meets a control that moves
nothing; with no module, in the browser, the one shown there is, dim. */
const float EDGE_LAMP_BOX = 3.f;  // mm, what a hand takes hold of, centred on the edge
const float EDGE_LAMP_R = 0.6f;   // mm, the lamp in it
struct EdgeLampParam : Hovered<VerticalDragParam>, DarkFace {
	int onParam = -1;
	std::function<bool()> shown;
	std::function<NVGcolor()> colorOf = []() { return SCHEME_YELLOW; };
	/** How much of its rim the lamp's value fills, nought to one
	(`SLEW_HALO`, Look.hpp); none, nothing. */
	std::function<float()> fillOf;
	/** A switch alone: what a press does, by the keys held (`command`,
	`shift`); empty, the lamp drags. */
	std::function<void(bool command, bool shift)> clicked;
	/** The gestures the owner declared for it (Words.hpp): a switch alone's
	`one` and `row`, the keys `clicked` is handed; a stroke's `toggle`, the
	click that puts it out and back, and `fine`, its finer stroke. A lamp
	given none takes none. */
	const Gesture* one = nullptr;
	const Gesture* row = nullptr;
	const Gesture* toggle = nullptr;
	const Gesture* fine = nullptr;
	bool inBrowser = true;  // shown with no module, in the browser
	/** A lamp that is one choice with others: `inForce` says whether its
	switch acts where it stands, lit only while it does (none, always);
	`putsOut`, the switches of its module that its lighting puts out, in
	the same undo. */
	std::function<bool()> inForce;
	std::vector<int> putsOut;
	bool toggled = false;   // this press was a click that is no stroke
	GestureChanges own;
	EdgeLampParam() {
		pxPerUnit = PX_PER_RUNG;
		box.size = mm2px(Vec(EDGE_LAMP_BOX, EDGE_LAMP_BOX));
	}
	/** A switch and its value at once: the wheel scrolls past it. */
	bool wheelTakes() override { return false; }
	bool lit() {
		return module && onParam >= 0 && module->params[onParam].getValue() > 0.5f && (!inForce || inForce());
	}
	/** The switch to `on`, noted first so the gesture's one undo puts it
	back with the value. */
	void turn(bool on) {
		if (!module || onParam < 0)
			return;
		own.note(module, onParam);
		module->paramQuantities[onParam]->setValue(on ? 1.f : 0.f);
		if (on)
			for (int id : putsOut)
				if (id >= 0 && id < (int)module->params.size() && module->paramQuantities[id]) {
					own.note(module, id);
					module->paramQuantities[id]->setValue(0.f);
				}
	}
	void step() override {
		visible = module ? !shown || shown() : inBrowser;
		VerticalDragParam::step();
	}
	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			toggled = false;
			if (clicked && module) {
				clicked(one && Hand::within(*one, e.mods), row && Hand::within(*row, e.mods));
				toggled = true;
				e.consume(this);
				return;
			}
			if (toggle && Hand::in(*toggle, e.mods) && module && onParam >= 0) {
				own.was.clear();
				turn(!lit());
				engine::ParamQuantity* pq = getParamQuantity();
				own.push((lit() ? "turn on " : "turn off ") + (pq ? pq->getLabel() : std::string()));
				toggled = true;
				e.consume(this);
				return;
			}
		}
		VerticalDragParam::onButton(e);
	}
	/** A switch alone has no double click of its own: each press was a
	click. */
	void onDoubleClick(const DoubleClickEvent& e) override {
		if (clicked) {
			e.consume(this);
			return;
		}
		VerticalDragParam::onDoubleClick(e);
	}
	void onDragStart(const DragStartEvent& e) override {
		if (toggled)
			return;
		own.was.clear();
		VerticalDragParam::onDragStart(e);
		// The stroke's one undo is the value and the switch together.
		if (module && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			own.noteAt(this, startValue);
			if (onParam >= 0)
				own.note(module, onParam);
			say();
		}
	}
	/** A stroke's value on the bracket, first on its hint line, while the
	lamp is dragged: its face shows only on or off and a rim, so this is the
	one place a time just set is read, a player's tooltips being theirs to
	switch off. Named, so
	the line says what is set: "Slew: 2 ms", "Slew: Off". */
	void say() {
		engine::ParamQuantity* pq = getParamQuantity();
		if (pq && onParam >= 0)
			Hand::readout(this, pq->getLabel() + ": " + pq->getDisplayValueString());
	}
	void onDragEnd(const DragEndEvent& e) override {
		if (toggled)
			return;
		VerticalDragParam::onDragEnd(e);
	}
	/** The stroke along the scale whose bottom is off (above): where the
	lamp stands on it, `at`, off being a step below `lo` of whichever size
	the stroke steps by, and the place it goes to `took` steps on, the
	whole numbers plain or the fiftieths under command. */
	void strokeMove(const DragMoveEvent& e) override {
		if (toggled || e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		strokeStep(e);
		say();
	}
	void strokeStep(const DragMoveEvent& e) {
		engine::ParamQuantity* pq = getParamQuantity();
		if (!pq || !module || onParam < 0)
			return;
		dragAccum += strokeDelta(e);
		moved = moved || strayed();
		if (!strayed())
			return;
		const float took = stepsTaken();
		if (took == 0.f)
			return;
		float lo, hi;
		span(pq, lo, hi);
		const bool fineHeld = this->fine && Hand::among(*this->fine);
		const float from = clamp(pq->getValue(), lo, hi);
		float to;
		if (fineHeld) {
			// In fiftieths, a count, so a whole place stays whole.
			const long at = lit() ? std::lround(from * FINE_PACE) : std::lround(lo * FINE_PACE) - 1;
			to = (float)(at - (long)took) / FINE_PACE;
		}
		else {
			const float at = lit() ? from : lo - 1.f;
			to = took < 0.f ? std::floor(at) - took : std::ceil(at) - took;
		}
		if (to < lo) {
			// Past the bottom: off, the value at the bottom, no travel kept.
			if (lit())
				turn(false);
			dragAccum = 0.f;
			if (pq->getValue() != lo)
				pq->setValue(lo);
			return;
		}
		// Lit from off at the bottom, which is no end on this scale: the
		// travel left over goes on up.
		if (!lit()) {
			turn(true);
			if (to == lo) {
				pq->setValue(lo);
				return;
			}
		}
		landStep(pq, to, lo, hi);
	}
	/** Off, the default, and nothing else: the value is kept, as a
	shift-click keeps it, and a stroke from off starts at the bottom of the
	scale whatever was kept (`strokeMove`). */
	void resetToDefault(engine::ParamQuantity*) override { turn(false); }
	void recordStroke() override {
		engine::ParamQuantity* pq = getParamQuantity();
		own.noteAt(this, startValue);
		own.push("change " + (pq ? pq->getLabel() : std::string()));
	}
	/** Its tab is the panel's, part of the block's outline (`EdgeTab`). */
	void draw(const DrawArgs&) override {}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		paintEdgeLamp(args.vg, box.size.x / 2.f, box.size.y / 2.f, mm2px(EDGE_LAMP_R), colorOf(), lit(), hovered,
		              fillOf && module ? fillOf() : 0.f);
	}
	/** Unpowered, the lamp is out whatever it holds, as a Trg's ring is,
	all in the plain layer (`MemberWidget::unpowered`): the dim disc of a
	lamp that is off, no glow and no rim, on the tab the panel keeps. */
	void drawDark(const DrawArgs& args) override {
		paintEdgeLamp(args.vg, box.size.x / 2.f, box.size.y / 2.f, mm2px(EDGE_LAMP_R), colorOf(), false, false);
	}
};
/** An edge lamp for `paramId` of `module`, switched by `onParam` (itself,
for a switch alone), centred on `tab`, on the out block every panel places
at BLOCK_INSET. The panel draws the tab, in the block's outline, wherever
the lamp is `shown` (`drawOutBlock`), so the two name one `EdgeTab`. */
inline EdgeLampParam* addEdgeLamp(ModuleWidget* mw, Module* module, int paramId, int onParam, EdgeTab tab,
                                  std::function<bool()> shown, std::function<NVGcolor()> colorOf) {
	const float x = tab.side < 0 ? BLOCK_INSET : PANEL_W - BLOCK_INSET;
	EdgeLampParam* e = createParamCentered<EdgeLampParam>(mm2px(Vec(x, tab.yMm)), module, paramId);
	e->onParam = onParam;
	e->shown = shown;
	e->colorOf = colorOf;
	mw->addParam(e);
	return e;
}

/** And command with shift, on a CV's plain volts, finer again: a five
hundredth. */
const float ULTRA_PACE = 500.f;

/** The presses that begin a stroke, counted across every screen of ours, so
a stroke knows itself by number and a double click its own two presses. */
inline int& strokeNo() {
	static int n = 0;
	return n;
}

/** A stroke that goes by distance rather than following the pointer: begun
with command held, it goes by how far the pointer has moved from where it
took hold, at `FINE_PACE`, so a level never leaps to the pointer. It takes
hold again whenever the key goes down or up in the middle of the stroke, and
a stroke that has once been fine goes on by distance when the key comes up.
The stroke is known by its number (`strokeNo`, which the widget bumps on
every press), and past an end the hold is folded back to the end, so
turning back moves the value at once (a stroke never keeps travel past a
limit). The CV's level and the CRV's points; the Atn's lanes keep a form of
their own, since their hand has three states and a span of notes counts on
from wherever a change of hand left it, plain or shifted. */
struct FineStroke {
	int stroke = -1;
	bool fine = false, relative = false;
	float pace = FINE_PACE; // while fine: how many pixels of hand one of the pointer's is
	float y = 0.f, value = 0.f;
	/** Called on every move with the value as it stands: true on the first
	move of a new stroke. `paceNow` is the fine stroke's pace, which a CV
	makes one for a block carried by distance and `ULTRA_PACE` for its
	finest; a change of it takes hold again, as the key does. */
	bool take(int strokeNo, float atY, float now, bool fineNow, float paceNow = FINE_PACE) {
		if (stroke != strokeNo) {
			// A new stroke: pressed with command held it goes by distance
			// from where the value already is; plain, it follows the pointer.
			stroke = strokeNo;
			relative = fineNow;
			fine = fineNow;
			pace = paceNow;
			y = atY;
			value = now;
			return true;
		}
		if (fineNow != fine || (fineNow && paceNow != pace)) {
			relative = relative || fineNow || fine;
			fine = fineNow;
			pace = paceNow;
			y = atY;
			value = now;
		}
		return false;
	}
	/** Where a stroke going by distance has the value, nought to one, over
	a zone `h` pixels tall. */
	float travel(float atY, float h) {
		const float raw = value + (y - atY) / h / (fine ? pace : 1.f);
		const float x = clamp(raw, 0.f, 1.f);
		if (raw != x) {
			value = x;
			y = atY;
		}
		return x;
	}
};

/** A screen that is its parameter the other way: press anywhere and the
level goes to the pointer's height, drag and it follows the pointer wherever
it goes, above or below the screen clamped at the ends. Subclasses say what a
height means in `setFromY`, and may say the pointer is locked out. */
struct LevelParam : ParamWidget, ResetSpends {
	float startValue = 0.f;
	// Every screen of this kind the stroke has swept, with the value each had
	// before it, so the whole stroke undoes in one go.
	GestureChanges swept;
	float lastRackX = NAN; // where the pointer was on the last move, along the rig
	// The value before the first press of a double-click, which a press has
	// already moved to the pointer: see `doubleClicked`.
	float firstStart = 0.f;
	double pressedAt = -1.0;
	/** How long after a press a second one is the second of a double click,
	in seconds. */
	static constexpr double DOUBLE_CLICK_S = 0.5;
	/** Whether a press at `now` is a first one, the start of what may be a
	double click, rather than its second. */
	bool firstPress(double now) const { return pressedAt < 0.0 || now - pressedAt > DOUBLE_CLICK_S; }
	StepKey pushed; // the undo step my last stroke pushed
	/** For a double-click: the gesture undoes to what the level was before
	its first press, not to the height that press jumped it to, a value the
	user never chose, and the first press's own undo step is taken back, so
	the double click is one undo. */
	void doubleClicked() {
		unpush(pushed);
		pushed = StepKey();
		swept.was.clear();
		swept.noteAt(this, firstStart);
	}

	virtual void setFromY(float y) = 0;
	virtual bool locked() { return false; }
	/** Two clicks put the level back to its default, what a fresh module
	has, and undo in one step to where it was before the first of them:
	Rack's own reset left the first press's jump as an undo step of its own.
	The press itself places the level, which is the gesture of a fader, so
	a click here is the one click on a value that changes it. */
	void onDoubleClick(const DoubleClickEvent& e) override {
		engine::ParamQuantity* pq = getParamQuantity();
		if (!locked() && pq) {
			pq->reset();
			doubleClicked();
			spent = true;
		}
		e.consume(this);
	}

	/** Where the pointer really is, in this screen's coordinates. */
	Vec pointer() {
		return APP->scene->rack->getMousePos().minus(getRelativeOffset(Vec(), APP->scene->rack));
	}
	/** Every screen of my kind on a module of my rig standing anywhere
	between two rack x's, both ends included, left to right. */
	void peersBetween(float x0, float x1, std::vector<LevelParam*>& out) {
		ModuleWidget* mine = getAncestorOfType<ModuleWidget>();
		if (!mine || !mine->module)
			return;
		// My run only (`runOf`), the hand's rig, never the engine's expander
		// links, which a stopped engine never forms. A bypassed module
		// ends it, as it ends the engine's rig, and stands in none: a stroke
		// begun on one reaches nothing past it.
		const std::vector<ModuleWidget*>* run = runOf(mine, true);
		if (!run)
			return;
		const float lo = std::min(x0, x1), hi = std::max(x0, x1);
		for (ModuleWidget* mw : *run) {
			if (hi < mw->box.pos.x || lo >= mw->box.getRight())
				continue;
			if (LevelParam* l = sameKindIn(mw))
				out.push_back(l);
		}
	}
	/** Which of several screens of one class this is. Two lanes of an Atn
	are both LaneParams, so the class alone made a stroke begun on an offset
	land on the neighbour's gain: same kind, wrong lane. A screen that is one
	of a set says which one it is. */
	virtual int lane() { return 0; }
	/** How far along the rig from the pointer the sweep looks for the
	screen it hands the stroke to: nought, the module under the pointer,
	for every screen but one whose value stands at an edge (a CRV's points). */
	virtual float sweepShift() { return 0.f; }
	LevelParam* sameKindIn(Widget* w) {
		for (Widget* c : w->children) {
			LevelParam* l = dynamic_cast<LevelParam*>(c);
			if (l && typeid(*l) == typeid(*this) && l->lane() == lane())
				return l;
			if (LevelParam* deeper = sameKindIn(c))
				return deeper;
		}
		return nullptr;
	}
	void onButton(const ButtonEvent& e) override {
		if (!locked() && e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			spent = false;
			engine::ParamQuantity* pq = getParamQuantity();
			startValue = pq ? pq->getValue() : 0.f;
			const double now = system::getTime();
			if (firstPress(now))
				firstStart = startValue;
			pressedAt = now;
			swept.was.clear();
			swept.noteAt(this, startValue);
			lastRackX = APP->scene->rack->getMousePos().x + sweepShift();
			setFromY(e.pos.y);
		}
		ParamWidget::onButton(e); // the right-click menu still opens
	}
	/** The level follows the pointer wherever it goes, and off to the side it
	is the screen of whichever module of my kind stands under the pointer that
	takes the height, so one stroke can drag a whole block to the floor or the
	ceiling with the pointer anywhere along the rig. Every module the pointer
	passed since the last move takes it, not only the one it is over now,
	since a quick sweep reads the pointer once a frame and would jump over a
	fader between two frames. */
	void onDragMove(const DragMoveEvent& e) final { moveUnlessSpent(e); }
	void strokeMove(const DragMoveEvent& e) override {
		if (locked() || e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		const Vec local = pointer();
		const float x = APP->scene->rack->getMousePos().x + sweepShift();
		std::vector<LevelParam*> passed;
		peersBetween(std::isnan(lastRackX) ? x : lastRackX, x, passed);
		lastRackX = x;
		for (LevelParam* f : passed) {
			if (f->locked())
				continue;
			swept.note(f);
			f->setFromY(local.y);
		}
	}
	void onDragEnd(const DragEndEvent& e) override {
		if (!locked() && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			engine::ParamQuantity* pq = getParamQuantity();
			pushed = StepKey::of(swept.push("change " + (pq ? pq->getLabel() : std::string("value"))));
		}
		swept.was.clear();
		lastRackX = NAN;
		ParamWidget::onDragEnd(e);
	}
};

/** A small glyph on a screen that a click acts on: a step arrow (the
triangle, `paintArrow`), painted in the middle
of its box, which is all of it a click lands on. Lit (`lit`), it is full
strength, the choice in force; else brighter under the pointer and faint where
there is nowhere further to go. What a click does is the screen's (`act`),
`can` says whether it would do anything, for the drawing only, `tipOf` names
what a click does as it stands, and `ink` is its colour, the scheme's yellow
unless the screen says otherwise. A fast second click is a second click. */
struct GlyphButton : TipHovered<OpaqueWidget> {
	std::function<void(NVGcontext*, float, float, NVGcolor)> glyph; // paints at a centre, in a colour
	std::function<void()> act;
	std::function<bool()> can;
	std::function<bool()> lit;
	std::function<NVGcolor()> ink;
	std::function<std::string()> tipOf;
	std::string hoverTip() override { return tipOf ? tipOf() : ""; }
	void onDoubleClick(const DoubleClickEvent& e) override { e.consume(this); }
	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			// Asked whatever the screen shows, which is a frame behind.
			if (act)
				act();
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}
	void draw(const DrawArgs& args) override {}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1 || !glyph)
			return;
		const float a = markInk(lit && lit(), hovered, !can || can());
		glyph(args.vg, box.size.x / 2.f, box.size.y / 2.f, color::alpha(ink ? ink() : SCHEME_YELLOW, a));
	}
};
/** A glyph button over the rectangle `pos`, `size` in mm, on `parent`; the
caller says what its click does. */
inline GlyphButton* addGlyphButton(Widget* parent, Vec pos, Vec size,
                                   std::function<void(NVGcontext*, float, float, NVGcolor)> glyph) {
	GlyphButton* a = createWidget<GlyphButton>(mm2px(pos));
	a->box.size = mm2px(size);
	a->glyph = glyph;
	parent->addChild(a);
	return a;
}
/** Whether the pointer is over `w` or anything on it: what shows a control
only while the hand is over its module, the Qnt's mode arrows. */
inline bool pointerWithin(Widget* w) {
	Widget* h = APP->event->hoveredWidget;
	return h && (h == w || h->isDescendantOf(w));
}
/** A step arrow pointing `dir`: the glyph button of a triangle. */
inline GlyphButton* addStepArrow(Widget* parent, Vec pos, Vec size, ArrowDir dir) {
	return addGlyphButton(parent, pos, size, [dir](NVGcontext* vg, float cx, float cy, NVGcolor col) {
		paintArrow(vg, cx, cy, dir, col);
	});
}

/** The strip either side of a plate's title a variant arrow stands in, the
RIG's scene arrows' width. */
const float VARIANT_ARROW_W = 3.f;
/** A module's variant arrows, either side of its title in the plate's ink and
shown only while the hand is over the module (`show`, from the widget's
step): the module has `n` variants, `at` says which of them stands, and an
arrow takes it to the next or the previous round the ends (`stepRound`),
`go(i)` making variant `i` the one; each arrow's tip says which way
(`variantStepName`) and never names where, and `lesson` is the left arrow's
and the right's, or one for both where a module has two variants and either
arrow is the other one, which is still a wrap. The Qnt's, the Clock's and the
Atn's. */
struct VariantArrows {
	GlyphButton* arrow[2] = {nullptr, nullptr};
	void add(Widget* w, int n, std::function<int()> at, std::function<void(int)> go, const std::string lesson[2]) {
		for (int side = 0; side < 2; side++) {
			const int by = side ? 1 : -1;
			GlyphButton* a = addStepArrow(w, Vec(side ? PANEL_W - VARIANT_ARROW_W : 0.f, BRACKET_H),
			                              Vec(VARIANT_ARROW_W, SCREEN_Y - BRACKET_H), side ? ARROW_RIGHT : ARROW_LEFT);
			a->ink = []() { return INK; };
			a->act = [=]() { go(stepRound(at(), by, n)); };
			a->tipOf = [=]() { return std::string(variantStepName(by)); };
			a->lesson = lesson[side];
			a->visible = false;
			arrow[side] = a;
		}
	}
	void add(Widget* w, int n, std::function<int()> at, std::function<void(int)> go, const std::string& lesson) {
		const std::string both[2] = {lesson, lesson};
		add(w, n, at, go, both);
	}
	void show(Widget* w) {
		const bool over = pointerWithin(w);
		for (GlyphButton* a : arrow)
			if (a)
				a->visible = over;
	}
};

} // namespace rig
