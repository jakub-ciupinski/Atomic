// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Bracket.hpp"

/** The writes the engine posts on the user's behalf (`Member::postWrite`),
made on the UI thread as one step of undo, joined to the structural step that
caused them. Included by Bracket.hpp. */

namespace rig {

/** Whether `f` holds for any action of an undo step: the step itself, or
each of a complex one's, at any depth. Rack's `ModuleRemove` and
`CableRemove` are a `ModuleAdd` and a `CableAdd` turned round, so a cast to
the add matches the remove too: a question that tells them apart asks the
remove first. */
template <typename F>
inline bool anyAction(history::Action* a, const F& f) {
	if (history::ComplexAction* c = dynamic_cast<history::ComplexAction*>(a)) {
		for (history::Action* b : c->actions)
			if (anyAction(b, f))
				return true;
		return false;
	}
	return f(a);
}

/** Whether an undo step changed what stands where or what is patched: a
module moved, added, removed or bypassed, or a cable added or removed, alone
or among others in one step. A cable is here because pulling one changes a
driver as a deleted Clock does, and the step a Seq keeps for it must join the
cable's own, or the first undo after a pulled clock cable jumps the Seq to the
stale step and the cable comes back only on the second. */
inline bool structural(history::Action* a) {
	return anyAction(a, [](history::Action* b) {
		return dynamic_cast<history::ModuleMove*>(b) || dynamic_cast<history::ModuleAdd*>(b) ||
		       dynamic_cast<history::ModuleRemove*>(b) || dynamic_cast<history::ModuleBypass*>(b) ||
		       dynamic_cast<history::CableAdd*>(b);
	});
}

/** The last undo step, if a hand pushed it and nothing has been undone
past it, and how long ago it appeared. */
inline history::Action* freshStep(double& age) {
	static StepKey seen;
	static double seenAt = 0.0;
	history::State* h = APP->history;
	history::Action* back =
	    h->actionIndex > 0 && h->actionIndex == (int)h->actions.size() ? h->actions.back() : nullptr;
	const StepKey key = StepKey::of(back);
	if (key != seen) {
		seen = key;
		seenAt = system::getTime();
	}
	age = system::getTime() - seenAt;
	return back;
}

/** Whether an undo step adds the module `id`, alone or among others. */
inline bool addsModule(history::Action* a, int64_t id) {
	return anyAction(a, [id](history::Action* b) {
		history::ModuleAdd* add = dynamic_cast<history::ModuleAdd*>(b);
		return add && add->moduleId == id;
	});
}

/** Whether an undo step puts the module `id` where it now stands, adding,
moving or bypassing it, alone or among others: what the hand has just placed
(`QntWidget::keepOneMode`). */
inline bool placesModule(history::Action* a, int64_t id) {
	return anyAction(a, [id](history::Action* b) {
		return (dynamic_cast<history::ModuleAdd*>(b) || dynamic_cast<history::ModuleMove*>(b) ||
		        dynamic_cast<history::ModuleBypass*>(b)) &&
		       static_cast<history::ModuleAction*>(b)->moduleId == id;
	});
}

/** The step a hand has just pushed that changed what stands where or what
is patched (`structural`), while it is the last step and under a second old:
the step that writes made on its behalf join, so one undo takes back both.
Null when there is none. */
inline history::Action* structuralStep() {
	double age = 0.0;
	history::Action* fresh = freshStep(age);
	return fresh && structural(fresh) && age < 1.0 ? fresh : nullptr;
}

/** How many modules an undo step adds and how many it removes, alone or
among others. */
inline void modulesChanged(history::Action* a, int& added, int& removed) {
	anyAction(a, [&](history::Action* b) {
		if (dynamic_cast<history::ModuleRemove*>(b))
			removed++;
		else if (dynamic_cast<history::ModuleAdd*>(b))
			added++;
		return false;
	});
}

/** Whether an undo step adds the module `id` and nothing else: one module
added and none taken away, as the browser, the swap menu's insert or a
duplicate of one module does, and never a selection pasted, a block
duplicated whole or a RIG loading a rig. */
inline bool addsOnly(history::Action* a, int64_t id) {
	int added = 0, removed = 0;
	modulesChanged(a, added, removed);
	return added == 1 && removed == 0 && addsModule(a, id);
}

/** Whether an undo step changes param `param` of the module `id`, alone or
among others. */
inline bool changesParam(history::Action* a, int64_t id, int param) {
	return anyAction(a, [id, param](history::Action* b) {
		history::ParamChange* p = dynamic_cast<history::ParamChange*>(b);
		return p && p->moduleId == id && p->paramId == param;
	});
}

/** `all` pushed as an undo step of its own, or, where `join` says so,
joined to `fresh`, the step last pushed, a lone one becoming the first of a
complex one in its place. Takes `all`, and drops it if it is empty. */
inline void pushJoined(history::ComplexAction* all, history::Action* fresh, bool join) {
	if (all->isEmpty()) {
		delete all;
		return;
	}
	if (!join || !fresh) {
		APP->history->push(all);
		return;
	}
	history::ComplexAction* into = dynamic_cast<history::ComplexAction*>(fresh);
	if (!into) {
		history::State* h = APP->history;
		into = new history::ComplexAction;
		into->name = fresh->name;
		into->push(fresh);
		h->actions.back() = into;
		double age = 0.0;
		freshStep(age); // the step in place is the new one, already seen
	}
	for (history::Action* a : all->actions)
		into->push(a);
	all->actions.clear();
	delete all;
}

/** The module a posted write is for, as the engine holds it now, or none:
one deleted since the post, by a hand or an undo, or a param it has not. */
inline Module* writeTarget(const Member::ParamWrite& w) {
	Module* to = APP->engine->getModule(w.module);
	return to && w.param >= 0 && w.param < (int)to->params.size() ? to : nullptr;
}

/** The writes the engine posted on the user's behalf, made on the UI
thread as one undo action: see `Member::postWrite`. Every MemberWidget calls
it from its step, and the windowed test harness after every wait, since its whole
script runs inside one step. */
inline void makeWrites(Member* m) {
	static std::vector<Member::ParamWrite> writes;
	// The last undo step made of the patch's own performing, while it is
	// still the last step on the history; forgotten the moment anything
	// else is, so a step undone and freed is never taken for it, nor a
	// step pushed later at its address.
	static StepKey performance;
	double age = 0.0;
	history::Action* fresh = freshStep(age);
	if (StepKey::of(fresh) != performance)
		performance = StepKey();
	{
		bool performed = false;
		const char* name = m->takeWrites(writes, &performed);
		if (!name)
			return;
		const int follows = m->writesFollow.exchange(-1, std::memory_order_relaxed);
		history::ComplexAction* all = new history::ComplexAction;
		all->name = name;
		for (const Member::ParamWrite& w : writes) {
			Module* to = writeTarget(w);
			if (!to)
				continue;
			const float was = to->params[w.param].getValue();
			if (was == w.value)
				continue;
			APP->engine->setParamValue(to, w.param, w.value);
			history::ParamChange* h = new history::ParamChange;
			h->moduleId = w.module;
			h->paramId = w.param;
			h->oldValue = was;
			h->newValue = w.value;
			all->push(h);
		}
		// Writes a move, an add, a delete, a bypass or a cable made, landing
		// within a second of it, join its undo step: one undo puts the module
		// back and its params with it, and the redo is not lost to a step of
		// the engine's own. Writes the patch made performing, a Poly's feed
		// letting a CV go as its channels come and go, join the step the last
		// such writes made while nothing else has been pushed since, as a REC
		// take is one undo however many steps it wrote: one undo takes back
		// the performance and the next reaches the hand's own last stroke,
		// never three undos after a few bars that do not reach it.
		// Writes that follow from a turn of the hand join the turn's own step
		// (`Member::postFollowing`), as a move's do.
		const bool afterTurn = follows >= 0 && fresh && age < 1.0 && changesParam(fresh, m->id, follows);
		const bool afterMove = afterTurn || structuralStep() != nullptr;
		const bool onPerformance = performed && !performance.empty() && StepKey::of(fresh) == performance;
		const bool pushed = !afterMove && !onPerformance && !all->isEmpty();
		pushJoined(all, fresh, afterMove || onPerformance);
		if (pushed)
			performance = performed ? StepKey::now() : StepKey();
		for (const Member::ParamWrite& w : writes)
			if (Member* t = dynamic_cast<Member*>(writeTarget(w)))
				t->wake();
	}
}

} // namespace rig
