// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/** A gesture's undo, GestureChanges, a member following a Poly taken over
by a stroke and given back (`takeOver`, `followAgain`), and the paint strokes
that change many params at once, CellPaint and RowPaint, with unpush and
recordParamChange. A part of Hand.hpp in a file of its own: Hand.hpp includes
it once, inside its namespace rig, and it is not a header to include anywhere
else. */

/** A module a stroke holds from its press to its release, held by its id and
found again at every use. An undo in the middle of a stroke (an insert, a
duplicate or a rig's load taken back) deletes modules the stroke may already
have reached, and a pointer kept from the press would be written through
freed. Null once the module has gone; an undo that puts it back puts it back
under the same id, and the stroke finds it again. */
struct HeldModule {
	int64_t id = -1;
	HeldModule() {}
	explicit HeldModule(Module* m) : id(m ? m->id : -1) {}
	Module* get() const { return id < 0 ? nullptr : APP->engine->getModule(id); }
	template <class M>
	M* as() const { return dynamic_cast<M*>(get()); }
	/** Its panel, for a stroke that moves panels. */
	ModuleWidget* widget() const { return id < 0 ? nullptr : APP->scene->rack->getModule(id); }
};

/** Several params one gesture changed, pushed as one undo: a stroke that
swept a row of faders, a paint stroke down a column of keys, a double-click
that undid the press it began with. Each param is noted once, with the value
it had before the gesture touched it; at the end only those that moved are
recorded, and one alone is an ordinary change, so one undo puts back the whole
stroke and not the fader pressed alone. A module the gesture reached and an
undo took away before it ended is simply not in its step (`HeldModule`). */
struct GestureChanges {
	struct Was {
		HeldModule module;
		int param;
		float value;
	};
	std::vector<Was> was;
	/** Notes param `param` of `m` at `value`, its value before the gesture,
	unless it is noted already. */
	void noteAt(Module* m, int param, float value) {
		if (!m)
			return;
		for (const Was& w : was)
			if (w.module.id == m->id && w.param == param)
				return;
		was.push_back({HeldModule(m), param, value});
	}
	void noteAt(ParamWidget* pw, float value) {
		if (pw)
			noteAt(pw->module, pw->paramId, value);
	}
	/** Notes a param as it stands now. */
	void note(Module* m, int param) {
		if (m && param >= 0 && param < (int)m->params.size())
			noteAt(m, param, m->params[param].getValue());
	}
	void note(ParamWidget* pw) {
		if (pw)
			note(pw->module, pw->paramId);
	}
	/** The gesture as one complex step named `name`, not pushed: only the
	params that moved, and none at all when nothing did. */
	history::ComplexAction* take(const std::string& name) {
		history::ComplexAction* all = new history::ComplexAction;
		all->name = name;
		for (const Was& w : was) {
			Module* m = w.module.get();
			if (!m || w.param >= (int)m->params.size())
				continue;
			const float now = m->params[w.param].getValue();
			if (now == w.value)
				continue;
			history::ParamChange* h = new history::ParamChange;
			h->name = name;
			h->moduleId = m->id;
			h->paramId = w.param;
			h->oldValue = w.value;
			h->newValue = now;
			all->push(h);
		}
		was.clear();
		return all;
	}
	/** Pushes the gesture as one undo and says which action that was, or
	null when nothing changed. */
	history::Action* push(const std::string& name) {
		history::ComplexAction* all = take(name);
		if (all->isEmpty()) {
			delete all;
			return nullptr;
		}
		if (all->actions.size() == 1) {
			history::Action* one = all->actions[0];
			all->actions.clear();
			delete all;
			APP->history->push(one);
			return one;
		}
		APP->history->push(all);
		return all;
	}
};

/** A member following a Poly's channels taken over by a hand's stroke,
before the stroke's first write: every param its feed holds is set to what
it shows (`heldByCable`), so the stroke starts from what was heard, and its
own param (`setAsideParam`) to one, each noted in the stroke's undo `into`.
A CV's fader and a Trg's rings alike; a member already set aside, or not
following, is left alone. */
inline void takeOver(Cartridge* c, GestureChanges& into) {
	const int own = c ? c->setAsideParam() : -1;
	if (own < 0 || !c->followingNow() || c->params[own].getValue() > 0.5f)
		return;
	into.note(c, own);
	for (int p = 0; p < (int)c->params.size(); p++) {
		float shown = 0.f;
		if (p != own && c->heldByCable(p, shown)) {
			into.note(c, p);
			c->params[p].setValue(shown);
		}
	}
	c->params[own].setValue(1.f);
}
/** And given back to the Poly by a double click, its own param noted at
`ownWas`, what it was before the gesture began. */
inline void followAgain(Cartridge* c, GestureChanges& into, float ownWas) {
	const int own = c ? c->setAsideParam() : -1;
	if (own < 0)
		return;
	into.noteAt(c, own, ownWas);
	c->params[own].setValue(0.f);
}

/** A stroke that paints cells, the Trg's rings and the Qnt's keys: the press
takes the opposite of the cell it lands on as its paint, every cell of the
same kind in the run the pointer passes after it takes that paint too, not
only the one it is over at each move (`cellsCrossed`), and the stroke is one
undo when it ends. The cell says how a cell is noted and set, and which cells
are out of the stroke, since a ring and a key are bound to their params
differently. */
template <class Cell>
struct CellPaint {
	float paint = 1.f;
	math::Vec lastRack; // where the pointer was on the last move, for the cells a sweep crossed
	// Every cell the stroke painted, gathered by the cell it began on and
	// pushed as one undo when it ends.
	GestureChanges painted;
	/** The press, on a cell standing at `startValue`; the caller notes it
	and sets it to the paint. */
	void beginPaint(float startValue) {
		paint = startValue > 0.5f ? 0.f : 1.f;
		painted.was.clear();
		lastRack = APP->scene->rack->getMousePos();
	}
	/** A move: `take` is called on every other cell passed since the last. */
	template <class Take>
	void paintCrossed(Cell* self, Take take) {
		const math::Vec now = APP->scene->rack->getMousePos();
		std::vector<Cell*> passed;
		cellsCrossed<Cell>(self, lastRack, now, passed);
		lastRack = now;
		for (Cell* r : passed)
			if (r != self)
				take(r);
	}
	history::Action* endPaint(const std::string& name) { return painted.push(name); }
};

/** A row of switches painted by one stroke: the press flips the row it lands
on, and every row of the same module the pointer is dragged across after it
takes that same value, whatever it held, the whole stroke one undo. A Poly's
rows paint on a plain press, a Mix's on a shift press, its plain drag being a
fader. The stroke is the drag of the widget it was pressed on (`origin`,
compared and never followed): one left behind by a drag that never ended,
its widget taken away mid-stroke, paints nothing another drag crosses. */
struct RowPaint {
	widget::Widget* origin = nullptr;
	HeldModule module;
	float value = 0.f;
	GestureChanges g;
};
inline RowPaint*& rowPaint() {
	static RowPaint* p = nullptr;
	return p;
}
/** The stroke begun by a press on `origin`, on param `param` of `m`,
painting `v`: a Poly's row's own param, a Mix's row's mute beside its level. */
inline void paintBegin(widget::Widget* origin, Module* m, int param, float v) {
	delete rowPaint();
	RowPaint* p = rowPaint() = new RowPaint;
	p->origin = origin;
	p->module = HeldModule(m);
	p->value = v;
	p->g.note(m, param);
	if (engine::ParamQuantity* pq = m ? m->getParamQuantity(param) : nullptr)
		pq->setValue(v);
}
inline void paintBegin(ParamWidget* pw, float v) { paintBegin(pw, pw->module, pw->paramId, v); }
/** The drag of `origin` reaching param `param` of `m`: true if it took the
stroke's value. */
inline bool paintOver(widget::Widget* origin, Module* m, int param) {
	RowPaint* p = rowPaint();
	engine::ParamQuantity* pq = m ? m->getParamQuantity(param) : nullptr;
	if (!p || !pq || origin != p->origin || m->id != p->module.id)
		return false;
	p->g.note(m, param);
	pq->setValue(p->value);
	return true;
}
inline bool paintOver(widget::Widget* origin, ParamWidget* pw) { return paintOver(origin, pw->module, pw->paramId); }
inline void paintEnd(const std::string& name) {
	RowPaint* p = rowPaint();
	if (!p)
		return;
	p->g.push(name);
	delete p;
	rowPaint() = nullptr;
}

/** Takes back the undo step a gesture pushed, if it is still the last one
done: the first click of a double click on a screen whose press is a
gesture, a fader's level placed, which the double click's own step now
covers from before it, so a double click is one undo. Nothing is set; the
step is dropped, and the patch counts as changed. The step is named by its
key (`StepKey::of`, taken as it was pushed), never by its address, which a
later step may have been given. Says whether it was. */
inline bool unpush(const StepKey& pushed) {
	history::State* h = APP->history;
	if (pushed.empty() || h->actionIndex <= 0 || StepKey::now() != pushed)
		return false;
	history::Action* a = h->actions.back();
	h->actions.pop_back();
	delete a;
	if (h->savedIndex >= h->actionIndex)
		h->savedIndex = -1;
	h->actionIndex--;
	return true;
}

/** Takes the last undo step back off the history, undeleted, if it is still
the last one done, to be pushed again inside a larger one: a typed rate's
value, which Rack records itself after the text is read, joined to the
change of mode the text made (`RateScreen::step`). Null otherwise. */
inline history::Action* takeLastPushed() {
	history::State* h = APP->history;
	if (h->actionIndex <= 0 || h->actionIndex != (int)h->actions.size())
		return nullptr;
	history::Action* a = h->actions.back();
	h->actions.pop_back();
	if (h->savedIndex >= h->actionIndex)
		h->savedIndex = -1;
	h->actionIndex--;
	return a;
}
/** Takes back off the history the step Rack recorded for a value typed
into `pw`'s field, if it is that and still the last one done, to be pushed
again inside the gesture the typing made: a rate's change of mode
(`RateScreen::recordTyped`), a followed member taken over (`foldTyped`).
Null otherwise. */
inline history::Action* takeTypedChange(ParamWidget* pw) {
	history::State* h = APP->history;
	history::ParamChange* pc =
	    pw && pw->module && h->actionIndex > 0 && h->actionIndex == (int)h->actions.size()
	        ? dynamic_cast<history::ParamChange*>(h->actions.back())
	        : nullptr;
	if (!pc || pc->moduleId != pw->module->id || pc->paramId != pw->paramId)
		return nullptr;
	return takeLastPushed();
}
/** A typed value that took its member over from a Poly (`takeOver`,
noted in `g` as the text was read): Rack's own step for the value is taken
back and the whole of it, the value and the hand, pushed as one undo. */
inline void foldTyped(ParamWidget* pw, GestureChanges& g) {
	engine::ParamQuantity* pq = pw->getParamQuantity();
	if (history::Action* a = takeTypedChange(pw)) {
		if (history::ParamChange* pc = dynamic_cast<history::ParamChange*>(a))
			g.noteAt(pw, pc->oldValue);
		delete a;
	}
	g.push("change " + (pq ? pq->getLabel() : std::string("value")));
}

/** A rate's mode changed by the hand (`RateMode`), as a step of undo:
Rack's ParamChange for the one thing beside a rate's value that says how
it moves. */
struct RateModeChange : history::ModuleAction {
	bool from = false, to = false;
	RateModeChange() { name = "rate mode"; }
	void put(bool f) {
		if (RateMode* m = rateModeOf(APP->engine->getModule(moduleId))) {
			m->setRateFree(f);
			if (Member* rm = dynamic_cast<Member*>(APP->engine->getModule(moduleId)))
				rm->wake();
		}
	}
	void undo() override { put(from); }
	void redo() override { put(to); }
};

/** A change to what a module keeps beside its params, a choice in its
menu (a GAP's skin, a Clock's BLEP or PPQN), as one step of undo, as Rack
takes any change to a module's data: its JSON before `change` and after it.
Nothing is pushed where nothing moved. The one path every such menu item
takes. */
template <typename F>
inline void dataChange(Module* m, const std::string& name, F change) {
	json_t* was = m->toJson();
	change();
	json_t* now = m->toJson();
	if (json_equal(was, now)) {
		json_decref(was);
		json_decref(now);
		return;
	}
	history::ModuleChange* h = new history::ModuleChange;
	h->name = name;
	h->moduleId = m->id;
	h->oldModuleJ = was;
	h->newModuleJ = now;
	APP->history->push(h);
}

/** Push an undo step for `pw` from `oldValue` to its current value: a gesture
of one, through the one recorder. */
inline void recordParamChange(ParamWidget* pw, float oldValue) {
	engine::ParamQuantity* pq = pw->getParamQuantity();
	if (!pq)
		return;
	GestureChanges one;
	one.noteAt(pw, oldValue);
	one.push("change " + pq->getLabel());
}
