// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Look.hpp"
#include "Words.hpp"
#include <string>

/** Tooltips that say what the module is doing now.

A module of this family means different things in different places, so a
port that lists every place it could stand explains none of them. The rule
is one sentence about now: the widget rewrites the port's name and the line
under it from what the audio thread published, and a module that never
changes what it is says it once in its constructor and leaves it there.

These run from a widget's `step()`, the UI thread, which is where Rack reads
them, and they write only when the text actually changed, so a hovering
mouse is not building a string sixty times a second. The name is a bare
noun: Rack appends "input" or "output" to it. */

namespace rig {

/** Tooltips in two levels: the name and the value are what a hand that knows
the module needs, and show at once; the description is the lesson, how a thing
is held, dragged or double-clicked, and shows only once the pointer has stood
still over it for `LEARN_AFTER` seconds with nothing dragged. Only the one
info under the pointer is ever held back, its text kept aside and put back the
moment the pointer rests or leaves, so every other description stands whole in
its field, which is where the test rigs read it. */
const double LEARN_AFTER = 2.0;
/** Whether the pointer has rested long enough for the lesson: worked out
once a frame, from where the scene last saw it and whether anything is
being dragged; always, where there is no window. The rest is timed on the
frame's own stamp, not the wall clock, so it is the frames that count: in a
live Rack the two agree to within a frame, and in a test host whose frames
keep a virtual time, a capture some frames after a click reads the same words
however loaded the machine is. */
inline bool learning() {
	static double frame = -1.0;
	static bool now = false;
	static Vec last;
	static double since = 0.0;
	if (!APP || !APP->window || !APP->scene)
		return true; // no pointer, no levels: a headless test or Rack reads every word
	const double f = APP->window->getFrameTime(); // the frame's own stamp, so the work is once a frame
	if (f == frame)
		return now;
	frame = f;
	const Vec p = APP->scene->getMousePos();
	if (!p.equals(last) || (APP->event && APP->event->getDraggedWidget())) {
		last = p;
		since = f;
	}
	now = f - since >= LEARN_AFTER;
	return now;
}
/** The description held back, whose it is, what it said and what stands
in its place while held: nothing before the pointer has rested, the same
words broken into lines once it has (`wrapTip`). */
struct HeldTip {
	Module* module = nullptr;
	std::string* field = nullptr;
	std::string saved;
	std::string shown;
};
/** A lesson broken into lines of about `TIP_LINE` characters at its
spaces, its own line breaks kept: Rack's tooltip is as wide as its longest
line and never breaks one, so a lesson of three sentences stands as one strip
across the rack. */
const size_t TIP_LINE = 60;
inline std::string wrapTip(const std::string& text) {
	std::string out;
	size_t col = 0, i = 0;
	while (i < text.size()) {
		if (text[i] == '\n') {
			out += '\n';
			col = 0;
			i++;
			continue;
		}
		size_t j = i;
		while (j < text.size() && text[j] != ' ' && text[j] != '\n')
			j++;
		const size_t word = j - i;
		if (col > 0 && col + 1 + word > TIP_LINE) {
			out += '\n';
			col = 0;
		}
		else if (col > 0) {
			out += ' ';
			col++;
		}
		out.append(text, i, word);
		col += word;
		i = j;
		while (i < text.size() && text[i] == ' ')
			i++;
	}
	return out;
}
inline HeldTip& heldTip() {
	static HeldTip h;
	return h;
}
/** The description field of whatever of `m`'s is under the pointer, or none. */
inline std::string* hoveredTip(Module* m) {
	if (!APP || !APP->event)
		return nullptr;
	for (widget::Widget* w = APP->event->getHoveredWidget(); w; w = w->parent) {
		if (app::ParamWidget* pw = dynamic_cast<app::ParamWidget*>(w)) {
			engine::ParamQuantity* pq = pw->module == m ? pw->getParamQuantity() : nullptr;
			return pq ? &pq->description : nullptr;
		}
		if (app::PortWidget* pw = dynamic_cast<app::PortWidget*>(w)) {
			engine::PortInfo* pi = pw->module == m ? pw->getPortInfo() : nullptr;
			return pi ? &pi->description : nullptr;
		}
	}
	return nullptr;
}
/** Once a frame from `m`'s widget's step, after its tooltips have been
retitled: the lesson under the pointer put back, then held back again while
the pointer has not rested on it, or, once it has, shown broken into lines.
A retip writes the whole text again each frame it finds the field changed,
and that is kept as the text to put back. Only where there is a window: a
headless Rack has no tooltip to break and reads every word as written. */
inline void showTips(Module* m) {
	HeldTip& h = heldTip();
	if (h.module == m && h.field) {
		if (*h.field == h.shown)
			*h.field = h.saved;
		h.module = nullptr;
		h.field = nullptr;
	}
	std::string* f = m ? hoveredTip(m) : nullptr;
	if (!f || f->empty())
		return;
	const bool learnt = learning();
	if (learnt && !APP->window)
		return;
	std::string shown = learnt ? wrapTip(*f) : std::string();
	if (shown == *f)
		return;
	// Another module's still held, the pointer having crossed to me before
	// its step came round: put back first, or its lesson stays as held.
	if (h.field && h.field != f && *h.field == h.shown)
		*h.field = h.saved;
	h.module = m;
	h.field = f;
	h.saved = *f;
	h.shown = shown;
	*f = shown;
}
/** A widget going: nothing of its module is held back any more. */
inline void forgetTips(Module* m) {
	if (heldTip().module == m) {
		heldTip().module = nullptr;
		heldTip().field = nullptr;
	}
}

/** The shape every tooltip fills, `Tip`, and its writers `retipParam`,
`retipIn` and `retipOut`, are in Words.hpp beside `HintQuantity`, since an
engine-side constructor (a RIG's, Scenes.hpp) writes its tips too and a headless
test compiles it with nothing that draws. */

/** Where a tooltip over one of our modules stands: its top at the top of the
module's jacks' band, the caption over its highest jack, and its left at the
part's left, kept inside the window as Rack keeps it, so it drops below every
screen rather than over them or over the module beside. Rack puts a param's at
its widget's bottom-right corner and any other at the pointer, which would
tuck a fader's under its screen and throw a ring's over the next module. A
jack's, a part standing at or below the band's top, a module with no jack and
anything not over one of our modules stand where Rack put them. Asked of the
tooltip showing for the hovered widget, after every step and before the
tooltip draws (`NoAudioTips::draw`), and by the test harness. */
inline void placeTip(ui::Tooltip* tip) {
	widget::Widget* part = APP->event->hoveredWidget;
	app::ModuleWidget* mw = nullptr;
	for (widget::Widget* w = part; w && !mw; w = w->parent) {
		if (dynamic_cast<app::PortWidget*>(w))
			return;
		mw = dynamic_cast<app::ModuleWidget*>(w);
	}
	if (!mw || !mw->model || mw->model->plugin != pluginInstance || !tip->parent)
		return;
	float band = INFINITY;
	for (widget::Widget* c : mw->children)
		if (c->isVisible() && dynamic_cast<app::PortWidget*>(c))
			band = std::min(band, c->box.pos.y);
	if (band == INFINITY)
		return;
	band -= mm2px(CAP_ABOVE_JACK + CAP_INK - JACK_R); // the jack's box top up to its caption's ink
	const float top = mw->getAbsoluteOffset(math::Vec(0.f, band)).y;
	const math::Vec at = part->getAbsoluteOffset(math::Vec(0.f, 0.f));
	if (at.y >= top)
		return;
	tip->box.pos = math::Vec(at.x, top).round();
	tip->box = tip->box.nudge(tip->parent->box.zeroPos());
}

/** A tooltip for a widget that is no param: shown while the pointer is on
it, as Rack's own are, and only where the user has tooltips on, with its
`lesson` under it once the pointer has rested (`learning`), as a param's
description is, and broken into lines as a param's is (`wrapTip`), since a
lesson set unbroken runs as one strip across the rack. */
struct HoverTip {
	ui::Tooltip* tip = nullptr;
	std::string lesson;
	/** The first tier as last said, whatever Rack's tooltip setting, for a
	test harness to read where the widget is no glyph button. */
	std::string said;
	/** The words under it once the pointer has rested: a lone module's
	gestures for the part (`hintInTip`), then the lesson. */
	std::string tipWords(const std::string& text) {
		const std::string s = tipLines(text, hintInTip ? hintInTip->hint : std::string());
		return learning() && !lesson.empty() ? s + "\n" + wrapTip(lesson) : s;
	}
	/** Asked at every step while shown: the words as they stand. */
	void tipKeep(const std::string& text) {
		said = text;
		if (tip)
			tip->text = tipWords(text);
	}
	void tipShow(const std::string& text) {
		said = text;
		if (!settings::tooltips || (text.empty() && !hintInTip)) {
			tipHide();
			return;
		}
		if (!tip) {
			tip = new ui::Tooltip;
			APP->scene->addChild(tip);
		}
		tip->text = tipWords(text);
	}
	/** The tip taken down by the scene's own step, never off its children
	here: a menu item goes inside that step, as the menu a click closed is
	deleted, and a child pulled out of the list the step was walking crashes
	Rack. */
	void tipHide() {
		if (!tip)
			return;
		tip->requestDelete();
		tip = nullptr;
	}
	~HoverTip() { tipHide(); }
};

/** A widget that knows whether the pointer is on it: `hovered`, set on
enter and cleared on leave, the base's own enter and leave passed on after.
A widget with more to do on either keeps it in its own override, calling
this one. */
template <class Base>
struct Hovered : Base {
	bool hovered = false;
	void onEnter(const widget::Widget::EnterEvent& e) override {
		hovered = true;
		Base::onEnter(e);
	}
	void onLeave(const widget::Widget::LeaveEvent& e) override {
		hovered = false;
		Base::onLeave(e);
	}
};

/** A widget that is no param, whose hover is its own tip (`HoverTip`): the
words `hoverTip` gives shown on enter, kept as they change while the pointer
rests, and taken down on leave. The base's enter and leave are not passed on,
since a param's would raise Rack's own tooltip for a param that is not there;
`hovered` is set as `Hovered` sets it. */
template <class Base>
struct TipHovered : Base, HoverTip {
	bool hovered = false;
	/** The words as they stand; empty says nothing. */
	virtual std::string hoverTip() = 0;
	void onEnter(const widget::Widget::EnterEvent& e) override {
		hovered = true;
		tipShow(hoverTip());
	}
	void onLeave(const widget::Widget::LeaveEvent& e) override {
		hovered = false;
		tipHide();
	}
	void step() override {
		if (hovered)
			tipKeep(hoverTip());
		Base::step();
	}
};

/** A menu item with a tooltip: its words in the menu are its name alone, and
what it does is said on the hover, as a param's is, since a label line under
each would make a right-click menu read as a manual. Rack's own MenuItem has
none, so this raises a `HoverTip` on enter and takes it down on leave and
when the menu goes. The one way a module's menu explains an item;
`tipBoolMenuItem` builds the switch. Its `lesson` comes under the tip once
the pointer has rested, as a param's description does. */
struct TipMenuItem : ui::MenuItem, HoverTip {
	std::string tipText;
	void onEnter(const EnterEvent& e) override {
		ui::MenuItem::onEnter(e);
		tipShow(tipText);
	}
	void step() override {
		ui::MenuItem::step();
		tipKeep(tipText);
	}
	void onLeave(const LeaveEvent& e) override {
		ui::MenuItem::onLeave(e);
		tipHide();
	}
};
/** A switch in a module's menu, ticked when on, named `text` and explained
by `tip` on its hover, `lesson` under it once the pointer rests (`TipMenuItem`). */
inline TipMenuItem* tipBoolMenuItem(const std::string& text, const std::string& tip, std::function<bool()> getter,
                                    std::function<void(bool)> setter, const std::string& lesson = "") {
	TipMenuItem* item = createBoolMenuItem<TipMenuItem>(text, "", getter, setter);
	item->tipText = tip;
	item->lesson = lesson;
	return item;
}

/** A menu item's getter or action, `f(w, ...)`, on the widget `w` the menu
was opened from, through a `WeakPtr`: once that widget is gone it answers
the default (false, nought) and does nothing. A menu outlives the module it
was opened on, a Delete and an undo with it standing,
and Rack calls a check's getter every frame, so a lambda
holding the widget's `this` or its `Module*` reads freed memory; every
menu lambda of ours that reaches a widget or its module goes through this,
as Rack's own ModuleWidget menu holds itself by a `WeakPtr`. The module is
reached from the widget inside `f`, never captured beside it. */
template <class W, class F>
auto whileStanding(W* w, F f) {
	WeakPtr<W> weak = w;
	return [weak, f](auto... a) {
		W* now = weak.get();
		using R = decltype(f(now, a...));
		if (!now)
			return R();
		return f(now, a...);
	};
}

} // namespace rig
