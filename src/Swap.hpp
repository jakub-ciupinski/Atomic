// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Bracket.hpp"
#include <map>
#include <vector>

/** Editing the rack in place: a module replaced by another, one set in
beside it, deleted or duplicated with the modules either side closing up or
making room, each as one step of undo, and the swap menu that offers them
over a module's title, with its overlay. Included by Bracket.hpp. */

namespace rig {

/** The swap menu's own gesture beside the delete (`G_SWAP_DELETE`, Bracket.hpp,
whose title says it): Alt+D, the menu's key with D, duplicates what the head
copies. */
inline const Gesture G_SWAP_DUP = declare(On::Key, Mod::Alt, Rank::Known, "Alt+D: duplicate");

/** A width and a position on the rack, in grid units counted: they are whole
numbers of them, and are compared as such. */
inline int gridUnits(float px) {
	return (int)std::round(px / RACK_GRID_WIDTH);
}

/** Where every module stands, before a step that moves some of them. */
inline std::map<ModuleWidget*, Vec> standing() {
	std::map<ModuleWidget*, Vec> was;
	for (ModuleWidget* o : APP->scene->rack->getModules())
		was[o] = o->box.pos;
	return was;
}
/** A move for undo for every module that has moved since `was`. */
inline void pushMoves(history::ComplexAction* h, const std::map<ModuleWidget*, Vec>& was) {
	for (std::map<ModuleWidget*, Vec>::const_iterator it = was.begin(); it != was.end(); ++it) {
		if (it->first->box.pos.equals(it->second))
			continue;
		history::ModuleMove* mv = new history::ModuleMove;
		mv->moduleId = it->first->module->id;
		mv->oldPos = it->second;
		mv->newPos = it->first->box.pos;
		h->push(mv);
	}
}

/** `mw` taken off the rack, its cables with it, as steps of `h`. Nothing
of `mw` after this, and the frame's runs, which held it by pointer, are
marked afresh. */
inline void removeWithHistory(history::ComplexAction* h, ModuleWidget* mw) {
	mw->appendDisconnectActions(h);
	history::ModuleRemove* gone = new history::ModuleRemove;
	gone->setModule(mw);
	h->push(gone);
	APP->scene->rack->removeModule(mw);
	delete mw;
	resetGroundMarks();
}

/** The module `mw` replaced by a new `next`, the swap menu's middle third:
the old one goes, cables and all, and the new one stands where it stood, as
one step of undo. A wider module pushes its right-hand neighbours on, as
Rack's own placing does; a narrower one draws the modules bonded to its right
in after it, so a rig stays one rig. Nothing of `mw` after this. */
inline void replaceModule(ModuleWidget* mw, Model* next) {
	RackWidget* rack = APP->scene->rack;
	const Vec pos = mw->box.pos;
	const int oldW = gridUnits(mw->box.size.x);

	history::ComplexAction* h = new history::ComplexAction;
	h->name = "replace module";
	removeWithHistory(h, mw);

	const std::map<ModuleWidget*, Vec> was = standing();
	engine::Module* m = next->createModule();
	APP->engine->addModule(m);
	ModuleWidget* nw = next->createModuleWidget(m);
	nw->box.pos = pos;
	const int newW = gridUnits(nw->box.size.x);
	if (newW < oldW) {
		// The modules standing edge to edge to the old one's right, in
		// order, drawn left by the difference, the nearest first so each
		// lands where its neighbour has just left.
		std::vector<ModuleWidget*> chain;
		int edge = gridUnits(pos.x) + oldW;
		for (bool found = true; found;) {
			found = false;
			for (ModuleWidget* o : rack->getModules())
				if (gridUnits(o->box.pos.y) == gridUnits(pos.y) && gridUnits(o->box.pos.x) == edge) {
					chain.push_back(o);
					edge += gridUnits(o->box.size.x);
					found = true;
					break;
				}
		}
		rack->addModule(nw);
		rack->requestModulePos(nw, pos);
		for (ModuleWidget* o : chain)
			rack->requestModulePos(o, o->box.pos.minus(Vec((oldW - newW) * RACK_GRID_WIDTH, 0.f)));
	}
	else {
		rack->addModule(nw);
		rack->setModulePosForce(nw, pos);
	}
	nw->loadTemplate();

	pushMoves(h, was);
	history::ModuleAdd* came = new history::ModuleAdd;
	came->setModule(nw);
	h->push(came);
	APP->history->push(h);
}

/** A new `model` set in at the left edge of `mw` or at its right, the swap
menu's outer thirds and its DUPLICATEs, as one step of undo. At a seam the
right never moves: the new one
stands against the seam on its left, and whatever stands in this row to the
seam's left is pushed left by as much as it must, each only as far as it
must, the way Rack shoves a module dropped among others, so a bonded rig
stays bonded and a module standing clear of it moves only if it is reached.
`pushRight` is the other way round, the DUPLICATE on a module's own title: the new one stands against the
seam on its right, `mw` and its left stay where they stand, and whatever
stands to the seam's right, ours or not, is shoved right, each only as far as
it must, as Rack's own Cmd+D would. Either way, at a bond's right edge with
free rack beyond it the new one stands there and nothing moves at all. Rack
2's rack has no left edge, so there is always room to the left. */
inline void insertModule(ModuleWidget* mw, Model* model, bool after, json_t* state = nullptr, bool pushRight = false) {
	RackWidget* rack = APP->scene->rack;
	const int row = gridUnits(mw->box.pos.y);
	const int seam = gridUnits(mw->box.pos.x) + (after ? gridUnits(mw->box.size.x) : 0);
	const std::map<ModuleWidget*, Vec> was = standing();

	history::ComplexAction* h = new history::ComplexAction;
	h->name = state ? "duplicate module" : "insert module";
	engine::Module* m = model->createModule();
	// A copy's state is loaded before the engine has it, as Rack's own clone
	// does, so no lock is needed; its id and its links are its own.
	if (state) {
		json_object_del(state, "id");
		json_object_del(state, "leftModuleId");
		json_object_del(state, "rightModuleId");
		m->fromJson(state);
	}
	APP->engine->addModule(m);
	ModuleWidget* nw = model->createModuleWidget(m);
	const int newW = gridUnits(nw->box.size.x);

	// Whether the span [from, from + w) of this row is free.
	auto freeAt = [&](int from, int w) {
		for (ModuleWidget* o : rack->getModules()) {
			if (o == nw || gridUnits(o->box.pos.y) != row)
				continue;
			const int ox = gridUnits(o->box.pos.x), ow = gridUnits(o->box.size.x);
			if (ox < from + w && ox + ow > from)
				return false;
		}
		return true;
	};
	int at = seam - newW;
	if (after && freeAt(seam, newW))
		at = seam; // past the bond's right edge, into free rack
	else if (pushRight) {
		// Everything from the seam rightwards, the nearest first, each pushed
		// to the right edge of the one before it if it reaches into it.
		at = seam;
		std::vector<ModuleWidget*> right;
		for (ModuleWidget* o : rack->getModules())
			if (gridUnits(o->box.pos.y) == row && gridUnits(o->box.pos.x) + gridUnits(o->box.size.x) > seam)
				right.push_back(o);
		std::sort(right.begin(), right.end(),
		          [](ModuleWidget* a, ModuleWidget* b) { return a->box.pos.x < b->box.pos.x; });
		std::vector<std::pair<ModuleWidget*, int>> to;
		int limit = seam + newW;
		for (ModuleWidget* o : right) {
			const int ox = gridUnits(o->box.pos.x), ow = gridUnits(o->box.size.x);
			if (ox >= limit)
				break;
			to.push_back(std::make_pair(o, limit));
			limit += ow;
		}
		// The farthest first, so each lands where nothing stands yet.
		for (int k = (int)to.size() - 1; k >= 0; k--) {
			const Vec p(to[k].second * RACK_GRID_WIDTH, to[k].first->box.pos.y);
			if (!rack->requestModulePos(to[k].first, p))
				rack->setModulePosForce(to[k].first, p);
		}
	}
	else {
		// Everything to the seam's left, the nearest first, each pushed to
		// the left edge of the one before it if it reaches past it.
		std::vector<ModuleWidget*> left;
		for (ModuleWidget* o : rack->getModules())
			if (gridUnits(o->box.pos.y) == row && gridUnits(o->box.pos.x) < seam)
				left.push_back(o);
		std::sort(left.begin(), left.end(),
		          [](ModuleWidget* a, ModuleWidget* b) { return a->box.pos.x > b->box.pos.x; });
		std::vector<std::pair<ModuleWidget*, int>> to;
		int limit = at;
		for (ModuleWidget* o : left) {
			const int ox = gridUnits(o->box.pos.x), ow = gridUnits(o->box.size.x);
			if (ox + ow <= limit)
				break;
			to.push_back(std::make_pair(o, limit - ow));
			limit -= ow;
		}
		// The farthest first, so each lands where nothing stands yet.
		for (int k = (int)to.size() - 1; k >= 0; k--) {
			const Vec p(to[k].second * RACK_GRID_WIDTH, to[k].first->box.pos.y);
			if (!rack->requestModulePos(to[k].first, p))
				rack->setModulePosForce(to[k].first, p);
		}
	}
	rack->addModule(nw);
	const Vec pos(at * RACK_GRID_WIDTH, mw->box.pos.y);
	if (!rack->requestModulePos(nw, pos))
		rack->setModulePosForce(nw, pos);
	if (!state) // a copy keeps the state it was made with
		nw->loadTemplate();

	pushMoves(h, was);
	history::ModuleAdd* came = new history::ModuleAdd;
	came->setModule(nw);
	h->push(came);
	APP->history->push(h);
}

/** A new `model` standing on its own to the right of `mw`'s rig, the run it
stands in (`runOf`, `mw` alone where it stands in none): on the same rack row,
at the first place past the rig's right edge with one free grid unit either
side of it, so it bonds to nothing and nothing it stands beside bonds to it,
and nothing moved. One step of undo, built as Rack's module browser builds
one, its default preset loaded and the new module selected (Atomic Help). */
inline ModuleWidget* spawnApart(ModuleWidget* mw, Model* model) {
	RackWidget* rack = APP->scene->rack;
	const int row = gridUnits(mw->box.pos.y);
	const std::vector<ModuleWidget*>* run = runOf(mw);
	const ModuleWidget* last = run ? run->back() : mw;
	engine::Module* m = model->createModule();
	APP->engine->addModule(m);
	ModuleWidget* nw = model->createModuleWidget(m);
	const int w = gridUnits(nw->box.size.x);
	// Past the edge by one free unit; anything reaching within a unit of the
	// span sends it on past that thing's right edge, and again a unit on.
	int at = gridUnits(last->box.pos.x) + gridUnits(last->box.size.x) + 1;
	for (bool moved = true; moved;) {
		moved = false;
		for (ModuleWidget* o : rack->getModules()) {
			if (gridUnits(o->box.pos.y) != row)
				continue;
			const int ox = gridUnits(o->box.pos.x), ow = gridUnits(o->box.size.x);
			if (ox < at + w + 1 && ox + ow > at - 1) {
				at = ox + ow + 1;
				moved = true;
			}
		}
	}
	rack->addModule(nw);
	rack->setModulePosForce(nw, Vec(at * RACK_GRID_WIDTH, mw->box.pos.y));
	nw->loadTemplate();
	history::ModuleAdd* came = new history::ModuleAdd;
	came->name = "create module";
	came->setModule(nw);
	APP->history->push(came);
	rack->deselectAll();
	rack->select(nw);
	return nw;
}

/** `mw` deleted from the swap menu's head, as one step of undo, and the
modules bonded to its right drawn left by its width, its left staying where it
stands, so the bond closes up from the right and the next module lands under
the pointer for another click. It is the title's DUPLICATE undone, which
pushes the right along, so a
DUPLICATE and then a delete of the copy leave the rig where it stood. Nothing
of `mw` after this. */
inline void deleteModule(ModuleWidget* mw) {
	RackWidget* rack = APP->scene->rack;
	const Vec pos = mw->box.pos;
	const int w = gridUnits(mw->box.size.x);
	std::vector<ModuleWidget*> chain;
	int edge = gridUnits(pos.x) + w;
	for (bool found = true; found;) {
		found = false;
		for (ModuleWidget* o : rack->getModules())
			if (o != mw && gridUnits(o->box.pos.y) == gridUnits(pos.y) && gridUnits(o->box.pos.x) == edge) {
				chain.push_back(o);
				edge += gridUnits(o->box.size.x);
				found = true;
				break;
			}
	}
	history::ComplexAction* h = new history::ComplexAction;
	h->name = "delete module";
	removeWithHistory(h, mw);
	const std::map<ModuleWidget*, Vec> was = standing();
	// The nearest first, so each lands where its neighbour has just left.
	for (ModuleWidget* o : chain)
		rack->requestModulePos(o, o->box.pos.minus(Vec(w * RACK_GRID_WIDTH, 0.f)));
	pushMoves(h, was);
	APP->history->push(h);
}

/** `src` copied, its params and its state, and set in at the seam on
`mw`'s left or its right as `insertModule` sets one in: the swap menu's head.
On a swap `src` is `mw`, the seam its right, and the right is pushed along
(`pushRight`); at a seam between two it is the module to the seam's right; at
a bond's edge it is the module at that edge, copied out past it. Cables
stay with the original. */
inline void duplicateInto(ModuleWidget* mw, ModuleWidget* src, bool after, bool pushRight = false) {
	if (!mw || !src || !src->module)
		return;
	json_t* state = src->module->toJson();
	insertModule(mw, src->model, after, state, pushRight);
	json_decref(state);
}

/** The swap menu: with Alt or Cmd held and the pointer on a module's title,
the band between the bracket and the screen, a small dark menu of the cycle's
modules drops from under that title, and the pointer's third of the module
says what a click on one does. The middle third replaces the module; the left
third sets the new one in before it and the right third after it, and the menu
stands half a module over to that side, centred on the seam. The menu covers
the title too, and its head there says what the third does: on a swap it is a
button, DUPLICATE, which copies the module in at its right and pushes its right
along; before or after
it is DUPLICATE too, which at a seam between two modules copies the module
to the seam's right into the seam, and at a bond's own edge copies the module
at that edge out past it; and with the pointer on an item, the head
says what a click on it does there, INSERT between two and ADD at the bond's
own edge. The third is read while the
pointer is on the module's title, the menu's head over it included, and never
while it is down in the list, so going down into the list never moves it.
Letting go of the key, or leaving both the module and the menu, closes it and
does nothing. One of it, a child of the rack, so it is placed and zoomed in
the rack's own coordinates and drawn over the modules and the cables; it is
always there and is empty while closed, which is when its box is nothing and
no event can find it. Everything here is the UI thread's: it is worked out in
its own step, once a frame. */
struct SwapMenu : OpaqueWidget, HintSource {
	int64_t over = -1; // the module it is open over, by id, or none
	int side = 0;      // -1 before it, 0 in its place, 1 after it
	int hot = -1;      // the item under the pointer
	/** On the head of the open menu, the delete a shift-click there makes,
	in any third (`sideAt`), said on the module's bracket; down in the list,
	nothing, the items saying what they do. */
	const Hinted* hinted() override {
		static Hinted head;
		if (head.gestures.empty()) {
			head.gestures = {&G_SWAP_DELETE};
			head.hint = hintOf(head.gestures);
		}
		return open() && headHot ? &head : nullptr;
	}
	ModuleWidget* hintsFor() override { return target(); }

	static float itemH() { return mm2px(5.2f); }
	static float headH() { return mm2px(SCREEN_Y - BRACKET_H); } // the title's band, which the head covers
	bool headHot = false; // the pointer on the head where it is DUPLICATE: a swap's, or a seam's between two
	static float pad() { return mm2px(1.f); }

	ModuleWidget* target() {
		return over < 0 ? nullptr : APP->scene->rack->getModule(over);
	}
	void close() {
		over = -1;
		hot = -1;
		headHot = false;
		box.size = Vec();
	}
	bool open() { return over >= 0; }
	/** What the head's DUPLICATE copies: on a swap the module itself, at a
	seam between two the module standing to the seam's right, `mw` itself
	before it and its bonded neighbour after it, and at the bond's own edge
	the module at that edge, `mw` either way. */
	ModuleWidget* seamSource() {
		ModuleWidget* mw = target();
		if (!mw || side <= 0)
			return mw;
		const std::vector<ModuleWidget*>* run = runOf(mw);
		if (!run)
			return mw;
		const auto it = std::find(run->begin(), run->end(), mw);
		if (it == run->end() || it + 1 == run->end())
			return mw;
		return *(it + 1);
	}
	/** Whether the head is a button where the menu stands now: everywhere it
	opens, since 18:20, when the bond's edges were given a DUPLICATE too. */
	bool headDuplicates() { return seamSource() != nullptr; }
	/** Narrower than a module by a little either side, so it reads as
	floating over the module rather than as part of it. */
	static float width() { return mm2px(PANEL_W - 2.4f); }
	void place(ModuleWidget* mw) {
		const float w = width();
		const float cx = mw->box.pos.x + mw->box.size.x * (1 + side) / 2.f;
		box.pos = Vec(cx - w / 2.f, mw->box.pos.y + mm2px(BRACKET_H));
		const int n = (int)swapCycle().size();
		box.size = Vec(w, deleting() ? headH() : itemTop(n) + pad());
	}
	/** DELETE under the pointer: shift held on the head of a swap. The list
	goes while it is, so DELETE is all the menu says. */
	bool deleting() const {
		return side == 0 && headHot && Hand::among(G_SWAP_DELETE);
	}
	static float sepH() { return mm2px(1.6f); }
	/** Where item `i` begins, under the head, its sections' rules counted. */
	static float itemTop(int i) {
		int rules = 0;
		for (int k = 1; k <= i && k < (int)swapCycle().size(); k++)
			rules += swapSectionAt(k) ? 1 : 0;
		return headH() + (float)i * itemH() + (float)rules * sepH();
	}
	/** Which third of `mw` the rack point `p` is over, as a side. */
	static int thirdOf(ModuleWidget* mw, Vec p) {
		const int t = (int)std::floor(3.f * (p.x - mw->box.pos.x) / mw->box.size.x);
		return clamp(t, 0, 2) - 1;
	}
	/** The item at a point in the menu's own coordinates, or none. */
	int itemAt(Vec q) {
		const int n = (int)swapCycle().size();
		if (q.x < 0.f || q.x >= box.size.x || q.y < headH())
			return -1;
		for (int i = 0; i < n; i++)
			if (q.y >= itemTop(i) && q.y < itemTop(i) + itemH())
				return i;
		return -1;
	}
	/** Where the head's middle stands, in the rack's coordinates. */
	Vec headMiddle() { return box.pos.plus(Vec(box.size.x / 2.f, headH() / 2.f)); }
	/** Where item `i`'s middle stands, in the rack's coordinates. */
	Vec itemMiddle(int i) {
		return box.pos.plus(Vec(box.size.x / 2.f, itemTop(i) + 0.5f * itemH()));
	}

	/** Whether the rack point `p` is on `mw`'s title, the band between the
	bracket and the screen, which is where the head stands whatever the menu's
	width or side. */
	static bool onTitle(ModuleWidget* mw, Vec p) {
		if (!mw->box.contains(p))
			return false;
		const float y = p.y - mw->box.pos.y;
		return y >= mm2px(BRACKET_H) && y < mm2px(SCREEN_Y);
	}
	/** The side a pointer on `mw` asks for. Shift held on the title is DELETE
	in any third: the menu stands over the module. The title is asked of the
	module rather than of the menu, since a menu over the module is narrower
	than it and one at a seam is not: asked of the menu, the pointer in the
	margin at a seam would be on the head of the seam's menu and off the head
	of the module's, and the menu would change between the two every frame. */
	static int sideAt(ModuleWidget* mw, Vec p) {
		return Hand::among(G_SWAP_DELETE) && onTitle(mw, p) ? 0 : thirdOf(mw, p);
	}

	/** A stroke seen while the key is held keeps the menu shut until the key
is let go. The menu opens the moment its key is held over a title, so a
command-drag that strays up over one would otherwise land on its head. */
	bool barred = false;
	/** Once a frame, and by the test rig after each of its hand's moves:
	open, follow, or close, from the key and where the pointer is. */
	void tick() {
		RackWidget* rack = APP->scene->rack;
		if (!Hand::among(G_BRACKET_SWAP)) {
			barred = false;
			close();
			return;
		}
		const Vec p = rack->getMousePos();
		if (open()) {
			ModuleWidget* mw = target();
			const bool onHead = box.contains(p) && p.y - box.pos.y < headH();
			// On the menu's head over another module's title with shift held,
			// that module's DELETE is what the pointer asks for, so the menu
			// goes to it below rather than staying at the seam.
			bool elsewhere = false;
			if (onHead && mw && !mw->box.contains(p) && Hand::among(G_SWAP_DELETE))
				for (ModuleWidget* o : rack->getModules())
					if (dynamic_cast<MemberWidget*>(o) && o->module && onTitle(o, p))
						elsewhere = true;
			if (mw && box.contains(p) && !(onHead && mw->box.contains(p)) && !elsewhere) {
				hot = itemAt(p.minus(box.pos));
				headHot = onHead && headDuplicates();
				return;
			}
			hot = -1;
			headHot = false;
			if (mw && mw->box.contains(p)) {
				side = sideAt(mw, p);
				headHot = onTitle(mw, p) && headDuplicates();
				place(mw);
				return;
			}
			close();
		}
		// Never opened by a stroke passing over a title: a command-drag on a
		// Qnt's transpose or rotation that strays up over the band is still
		// that drag until the button is let go. A press on the menu's own
		// head or list is no such stroke: Rack holds the menu as the dragged
		// widget until the button comes up, and barring on it would keep the
		// menu shut for a second DUPLICATE or DELETE with the key still held.
		widget::Widget* dragged = APP->event ? APP->event->getDraggedWidget() : nullptr;
		if (dragged && dragged != this) {
			barred = true;
			return;
		}
		if (barred)
			return;
		for (ModuleWidget* o : rack->getModules()) {
			if (!dynamic_cast<MemberWidget*>(o) || !o->module || !onTitle(o, p))
				continue;
			over = o->module->id;
			side = sideAt(o, p);
			headHot = headDuplicates(); // on the title, which is the head's band
			place(o);
			return;
		}
	}
	void step() override {
		tick();
		OpaqueWidget::step();
	}

	void onButton(const ButtonEvent& e) override {
		if (!open())
			return;
		e.consume(this); // the whole menu is the menu's: nothing under it hears
		if (e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS)
			return;
		ModuleWidget* mw = target();
		if (e.pos.y < headH()) {
			if (side == 0 && mw) {
				close();
				if (Hand::within(G_SWAP_DELETE, e.mods))
					deleteModule(mw); // nothing of mw after this
				else
					duplicateInto(mw, mw, true, true); // the copy to its right, the right pushed along
			}
			else if (ModuleWidget* src = seamSource()) {
				const bool after = side > 0;
				close();
				duplicateInto(mw, src, after);
			}
			return;
		}
		const int i = itemAt(e.pos);
		if (i < 0 || !mw)
			return;
		Model* model = swapCycle()[i];
		const int s = side;
		close();
		if (s == 0) {
			if (model != mw->model)
				replaceModule(mw, model); // nothing of mw after this
		}
		else
			insertModule(mw, model, s > 0);
	}

	/** A key with the pointer on the menu is the module's: Rack's own keys,
	Cmd+D among them, work over the title as though the menu were not there,
	since it opens the moment the key that also begins them is held. And
	Alt+D, the menu's key with D, is its DUPLICATE: whatever the head copies
	where the menu stands, the module itself on a swap. */
	void onHoverKey(const HoverKeyEvent& e) override {
		ModuleWidget* mw = open() ? target() : nullptr;
		if (!mw)
			return;
		if (e.action == GLFW_PRESS && e.keyName == "d" && Hand::loose(G_SWAP_DUP, e.mods)) {
			e.consume(this);
			ModuleWidget* src = seamSource();
			const bool after = side >= 0, onSwap = side == 0;
			close();
			duplicateInto(mw, src, after, onSwap);
			return;
		}
		HoverKeyEvent at = e;
		at.pos = e.pos.plus(box.pos).minus(mw->box.pos);
		mw->onHoverKey(at);
	}

	/** Nothing in the rack's own pass: the scene's overlay paints the menu
	after everything else, cables and the lit layer included, so nothing of
	the rack stands over it. */
	void draw(const DrawArgs& args) override {}
	/** Whether the seam the menu adds at is the bond's own edge, where a
	module is added, rather than between two, where it is inserted. */
	bool atBondEdge(ModuleWidget* mw) {
		const std::vector<ModuleWidget*>* run = runOf(mw);
		if (!run || run->empty())
			return true;
		return side < 0 ? run->front() == mw : run->back() == mw;
	}
	/** The menu, in the rack's coordinates from its own corner. */
	void paint(NVGcontext* vg) {
		ModuleWidget* mw = open() ? target() : nullptr;
		if (!mw)
			return;
		// White, as the stock plate is, and its ink: the screen's glass with
		// its words lit on it was not sleek enough.
		const NVGcolor lit = INK;
		const NVGcolor faint = DIMPLE_INK;
		// The menu grows out of the bracket: the bracket's own plate, painted on
		// with the bracket's gradient so the two are one surface, its top
		// corners fillets turning out into the bracket rather than rounded off,
		// its foot rounded, a soft shadow under.
		const float rad = mm2px(1.f), fil = mm2px(1.2f), w = box.size.x, hgt = box.size.y;
		nvgBeginPath(vg);
		nvgRect(vg, -10.f, 0.f, w + 20.f, hgt + 10.f);
		nvgFillPaint(vg, nvgBoxGradient(vg, 0.f, -rad, w, hgt + rad, rad, 7.f, nvgRGBA(0, 0, 0, 80), nvgRGBA(0, 0, 0, 0)));
		nvgFill(vg);
		// Adding at the bond's edge, the half of the menu past the seam hangs
		// where no bracket is, so its top corner there is rounded off as the
		// foot's are, and only the half under the module meets the bracket.
		const bool edge = side != 0 && atBondEdge(mw);
		const bool freeL = edge && side < 0, freeR = edge && side > 0;
		nvgBeginPath(vg);
		if (freeL) {
			nvgMoveTo(vg, 0.f, rad);
			nvgQuadTo(vg, 0.f, 0.f, rad, 0.f);
			nvgLineTo(vg, w / 2.f, 0.f);
			nvgLineTo(vg, w / 2.f, -1.f);
		}
		else
			nvgMoveTo(vg, -fil, -1.f);
		if (freeR) {
			nvgLineTo(vg, w / 2.f, -1.f);
			nvgLineTo(vg, w / 2.f, 0.f);
			nvgLineTo(vg, w - rad, 0.f);
			nvgQuadTo(vg, w, 0.f, w, rad);
		}
		else {
			nvgLineTo(vg, w + fil, -1.f);
			nvgLineTo(vg, w + fil, 0.f);
			nvgQuadTo(vg, w, 0.f, w, fil);
		}
		nvgLineTo(vg, w, hgt - rad);
		nvgQuadTo(vg, w, hgt, w - rad, hgt);
		nvgLineTo(vg, rad, hgt);
		nvgQuadTo(vg, 0.f, hgt, 0.f, hgt - rad);
		if (!freeL) {
			nvgLineTo(vg, 0.f, fil);
			nvgQuadTo(vg, 0.f, 0.f, -fil, 0.f);
		}
		nvgClosePath(vg);
		const float bracketTop = -mm2px(BRACKET_H);
		nvgFillPaint(vg, plateGradient(vg, RACK_GRID_HEIGHT, bracketTop));
		nvgFill(vg);
		auto rule = [&](float y) {
			nvgBeginPath(vg);
			nvgRect(vg, pad(), std::round(y) - 0.35f, box.size.x - 2.f * pad(), 0.7f);
			nvgFillColor(vg, faint);
			nvgFill(vg);
		};
		if (!deleting())
			rule(headH());

		std::shared_ptr<Font> font = plateFont();
		if (!font)
			return;
		// The head, over the title. On a swap it is the DUPLICATE button,
		// glowing under the pointer as a hot item does, made as small as it
		// must be to fit; before or after, a playhead's soft line at the
		// seam, which is the head's middle, and the word on the new one's
		// side of it.
		nvgFontFaceId(vg, font->handle);
		nvgTextLetterSpacing(vg, 0.4f);
		const float hy = headH() / 2.f + 1.f;
		// What the head says follows the pointer: on the head of a swap
		// DUPLICATE, or DELETE with shift held, which a click does; down in
		// the list SWAP, what a click there does; before or after, INS or ADD
		// only while the pointer is on an item, so no word stands under the
		// pointer where a click would do nothing.
		const bool shift = Hand::among(G_SWAP_DELETE);
		if (side == 0) {
			const char* word = headHot ? (shift ? "DELETE" : "DUPLICATE") : "SWAP";
			const NVGcolor ink = headHot && shift ? SCHEME_RED : lit;
			nvgFontSize(vg, 7.f);
			float b[4];
			const float tw = nvgTextBounds(vg, 0.f, 0.f, word, nullptr, b);
			const float room = box.size.x - 2.f * pad();
			nvgFontSize(vg, tw > room ? 7.f * room / tw : 7.f);
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			if (headHot) {
				nvgBeginPath(vg);
				nvgRect(vg, 0.f, 0.f, box.size.x, headH());
				nvgFillColor(vg, color::alpha(nvgRGB(0, 0, 0), 0.08f));
				nvgFill(vg);
			}
			nvgFillColor(vg, headHot ? ink : color::alpha(ink, 0.45f));
			nvgText(vg, box.size.x / 2.f, hy, word, nullptr);
		}
		else {
			// The word alone, centred, no line at the seam, and only while a
			// click on an item would do it; on the head of a seam between two
			// DUPLICATE, glowing as a swap's does, which copies the module to
			// the seam's right into it.
			const char* word = headHot ? "DUPLICATE" : hot < 0 ? "" : atBondEdge(mw) ? "ADD" : "INSERT";
			if (headHot) {
				nvgBeginPath(vg);
				nvgRect(vg, 0.f, 0.f, box.size.x, headH());
				nvgFillColor(vg, color::alpha(nvgRGB(0, 0, 0), 0.08f));
				nvgFill(vg);
			}
			nvgFontSize(vg, 7.f);
			float bb[4];
			const float tw = nvgTextBounds(vg, 0.f, 0.f, word, nullptr, bb);
			const float room = box.size.x - 2.f * pad();
			nvgFontSize(vg, tw > room ? 7.f * room / tw : 7.f);
			nvgFillColor(vg, lit);
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgText(vg, box.size.x / 2.f, hy, word, nullptr);
		}
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

		if (deleting())
			return; // DELETE alone
		const std::vector<Model*> cycle = swapCycle();
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, 11.f);
		nvgTextLetterSpacing(vg, 0.6f);
		for (int i = 0; i < (int)cycle.size(); i++) {
			const float y = itemTop(i);
			if (swapSectionAt(i))
				rule(y - sepH() / 2.f);
			const bool same = side == 0 && cycle[i] == mw->model; // a swap for itself does nothing
			// Every name in ink, softened; the one under the pointer full on
			// a grey band; its own kind, on a swap, faint.
			const bool hotHere = i == hot && !same;
			const float ty = y + itemH() / 2.f;
			if (hotHere) {
				nvgBeginPath(vg);
				nvgRect(vg, 0.f, y, box.size.x, itemH());
				nvgFillColor(vg, color::alpha(nvgRGB(0, 0, 0), 0.08f));
				nvgFill(vg);
			}
			nvgFillColor(vg, hotHere ? lit : color::alpha(lit, same ? 0.2f : 0.7f));
			nvgText(vg, box.size.x / 2.f, ty, cycle[i]->name.c_str(), nullptr);
		}
	}
};

/** Where a hovered row of a Poly or a Mix came from, or a hovered strip's
every source: a small triangle hanging from the bracket over each module's
title, pointing down at it, pure black, the gatherers being colourless,
drawn only in a frame the
row or the strip is hovered in. They say so from their own drawing, which
comes before the scene's overlay in the same frame. */
struct SourceArrow {
	std::vector<int64_t> ids; // every module pointed at this frame: a row's one, or a strip's every one
	double at = -1.0;
};
inline SourceArrow& sourceArrow() {
	static SourceArrow a;
	return a;
}
inline void pointAtSource(int64_t id) {
	if (id < 0 || !APP || !APP->window)
		return;
	SourceArrow& a = sourceArrow();
	const double now = APP->window->getFrameTime();
	if (a.at != now) {
		a.ids.clear();
		a.at = now;
	}
	if (std::find(a.ids.begin(), a.ids.end(), id) == a.ids.end())
		a.ids.push_back(id);
}
/** The arrow, a solid triangle 3 mm wide hung 2.2 mm deep from the bracket's
foot, in the rack's coordinates from the module's corner: black on the light
plate, the plate's ink on the dark. */
const Tone SOURCE_ARROW = {nvgRGB(0, 0, 0), INK.dark};
inline void paintSourceArrow(NVGcontext* vg, ModuleWidget* mw) {
	const float deep = mm2px(2.2f);
	paintArrow(vg, mw->box.size.x / 2.f, mm2px(BRACKET_H) + deep / 2.f, ARROW_DOWN, SOURCE_ARROW, deep, mm2px(1.5f));
}

/** Paints the swap menu over the whole scene, last, in the rack's own
zoom and place: the menu itself stays in the rack, where the pointer and
the modules are, and only its picture is lifted above everything. */
struct SwapMenuOverlay : SceneOverlay {
	void draw(const DrawArgs& args) override {
		RackWidget* rack = APP->scene ? APP->scene->rack : nullptr;
		SwapMenu* menu = nullptr;
		if (rack)
			for (Widget* c : rack->children)
				if ((menu = dynamic_cast<SwapMenu*>(c)))
					break;
		const float zoom = rack ? APP->scene->rackScroll->getZoom() : 1.f;
		const SourceArrow& arrow = sourceArrow();
		if (rack && APP->window && arrow.at == APP->window->getFrameTime())
			for (int64_t id : arrow.ids)
				if (ModuleWidget* mw = rack->getModule(id)) {
					const Vec mat = rack->getRelativeOffset(mw->box.pos, parent);
					nvgSave(args.vg);
					nvgResetScissor(args.vg);
					nvgTranslate(args.vg, mat.x - box.pos.x, mat.y - box.pos.y);
					nvgScale(args.vg, zoom, zoom);
					paintSourceArrow(args.vg, mw);
					nvgRestore(args.vg);
				}
		if (!menu || !menu->open())
			return;
		const Vec at = rack->getRelativeOffset(menu->box.pos, parent);
		nvgSave(args.vg);
		nvgResetScissor(args.vg);
		nvgTranslate(args.vg, at.x - box.pos.x, at.y - box.pos.y);
		nvgScale(args.vg, zoom, zoom);
		menu->paint(args.vg);
		nvgRestore(args.vg);
	}
};

/** The rack's one swap menu, made the first time it is asked for, and its
overlay on the scene. */
inline SwapMenu* swapMenu() {
	if (!APP->scene || !APP->scene->rack)
		return nullptr;
	RackWidget* rack = APP->scene->rack;
	for (Widget* c : rack->children)
		if (SwapMenu* s = dynamic_cast<SwapMenu*>(c))
			return s;
	SwapMenu* s = new SwapMenu;
	s->close(); // a widget's box is the whole plane until it says otherwise, which took every hover until the menu's first step
	rack->addChild(s);
	SwapMenuOverlay* o = new SwapMenuOverlay;
	o->box.size = APP->scene->box.size;
	APP->scene->addChild(o);
	return s;
}

/** The whole of `mw`'s rig selected, and nothing else, for one of Rack's
own actions on a selection: false, and nothing touched, where it stands in
no rig of two or more. */
inline bool selectRunOf(ModuleWidget* mw) {
	float x0, w;
	const std::vector<ModuleWidget*>* run = rigSpan(mw, x0, w) ? runOf(mw) : nullptr;
	if (!run || run->size() < 2)
		return false;
	const std::vector<ModuleWidget*> members = *run;
	RackWidget* rack = APP->scene->rack;
	rack->deselectAll();
	for (ModuleWidget* m : members)
		rack->select(m);
	return true;
}

/** Delete with the pointer on the bracket takes the whole rig, as one step of
undo: the bracket is the rig's, so what is done to it is done to all of them.
Asked of the module widget rather than of the bracket, because Rack's module
widget takes Delete for itself before its children hear of it, which is why
the bracket's own handler never ran. True if it took the key. */
inline bool deleteRigAt(ModuleWidget* mw, const event::HoverKey& e) {
	const bool del = e.key == GLFW_KEY_DELETE || e.key == GLFW_KEY_BACKSPACE;
	if (!del || e.action != GLFW_PRESS || !Hand::in(G_BRACKET_DELETE, e.mods) || e.pos.y >= RACK_GRID_WIDTH)
		return false;
	if (!selectRunOf(mw))
		return false;
	e.consume(mw);
	APP->scene->rack->deleteSelectionAction(); // deletes the caller too: nothing of it after this
	return true;
}

/** Duplicate with the pointer on the bracket, Rack's own keys for it (Cmd+D, and
Cmd+Shift+D with the cables) or Alt+D, the swap menu's key with D, copies the
whole rig, as Delete there takes it: the bracket is the rig's. The copy is placed
as Rack places a duplicated selection; a part of a rig is still copied by
selecting it. True if it took the key. */
inline bool duplicateRigAt(ModuleWidget* mw, const event::HoverKey& e) {
	if (e.keyName != "d" || e.action != GLFW_PRESS || e.pos.y >= RACK_GRID_WIDTH || !Hand::in(G_BRACKET_DUP, e.mods))
		return false;
	if (!selectRunOf(mw))
		return false;
	e.consume(mw);
	APP->scene->rack->cloneSelectionAction(Hand::within(G_BRACKET_CABLES, e.mods));
	return true;
}

} // namespace rig
