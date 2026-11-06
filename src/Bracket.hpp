// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Parts.hpp"
#include "Engine.hpp"
#include "FrameDrag.hpp"
#include "JsonFile.hpp"
#ifdef ATOMIC_DEV
#include "Follow.hpp" // the tools followed, the dev form's alone
#endif
#include <map>
#include <vector>

/** The bracket: what a rig of ours is, as a thing on the screen.

Everything else here draws one module. This file, with the three it includes
at its foot, is the only place that knows several of them are standing
together: it grows the ATOMIC bracket over their top screws and drags them as
one, Ground.hpp bonds the joins between their grounds so that the pixel two
modules share is theirs rather than the rack's shadow, Swap.hpp edits the
rig in place, and Writes.hpp makes the engine's writes as the hand's.

The one thing to keep in mind when reading it: Rack draws the module
container in the order modules were added to it, which has nothing to do with
where they stand. Anything painted across a run must therefore be painted by
a member chosen for the job, not by whoever happens to go first. */

namespace rig {

/** The plate of a module of ours. */
inline void addPanel(ModuleWidget* w, Widget* panel) {
	panel->box.size = w->box.size;
	w->addChild(panel);
}

/* The bracket: the bracket across the top of a rig.

Two or more of ours standing together are one instrument, so they grow a bracket
over their top screws, the width of the rack's own, with the name of the
collection on it and a grip down its length to say it can be taken hold of.
A module standing on its own keeps its screws and looks like any other.

The bracket is drawn whole, not a slice per panel, and once: it belongs to the
rig rather than to any one panel, so the first member Rack actually draws
works out where the rig begins and how wide it is, resets the scissor Rack
puts on it, paints the entire thing, and marks the rig as bracketed for the
frame, the same register the ground uses. There is no seam to cover, since
there are no edges inside the rig to blend. Drawn by every member, a rig of
thirty would be thirty brackets a frame, each a path of some six hundred dimples
the full width of the rig, which alone halves the frame rate. If the first
member is culled the next one drawn takes the job.

Dragging it carries the rig: a rig has its own handle and needs no module
spent on it. Its word is the plugin's name, `BRAND` (Look.hpp). */

/** The run around a widget, as an offset to its left edge and a width, both
in pixels in that widget's own space. False for a module standing on its own,
which keeps its screws and grows no bracket. */
inline bool rigSpan(ModuleWidget* mw, float& x0, float& width) {
	if (!mw)
		return false;
	const std::vector<ModuleWidget*>* run = runOf(mw);
	if (!run || run->size() < 2)
		return false;
	x0 = run->front()->box.pos.x - mw->box.pos.x;
	width = run->back()->box.pos.x + run->back()->box.size.x - run->front()->box.pos.x;
	return true;
}

/** Whether the bracket's name takes the mark for its O. Off, it is the plain
name, and the bolts keep the mark either way: the two are separate choices,
either undone without the other. */
constexpr bool ATOM_IN_NAME = true;

/** Rack's Nunito Bold, in its own units, for setting the name by its ink:
fontstash sizes a face by its ascent less its descent, so a size of 7.5 is
7.5 pixels over 2571 units. Each letter is its advance and the left and right
edges of its ink, read out of the font file. */
constexpr float NUNITO_LINE = 1999.f + 572.f, NUNITO_CAP = 1442.f;
inline void nunitoInk(const char* s, float sc, float ls, float& first, float& last) {
	struct Ink { char c; float adv, l, r; };
	static const Ink inks[] = {{'A', 1468, 59, 1413}, {'T', 1203, 22, 1181}, {'M', 1941, 168, 1759},
	                           {'I', 647, 181, 468},  {'C', 1364, 108, 1277}, {'E', 1256, 168, 1155},
	                           {'N', 1568, 164, 1403}, {'D', 1537, 168, 1431}};
	float x = 0.f;
	first = NAN;
	for (; *s; s++)
		for (const Ink& k : inks)
			if (k.c == *s) {
				if (std::isnan(first))
					first = x + k.l * sc;
				last = x + k.r * sc;
				x += k.adv * sc + ls;
			}
}

/** The bracket's name laid out once, from `x0` for `w`: the letters as they
always were, in Nunito at 7.5 spread a little wider, and the O the bolts' own
mark at the bolts' own size, larger than the letters. The whole is centred
by its ink rather than by its advances, so the spacing trailing the C costs
nothing. The air either side of the mark is set by eye rather than even: a
T leaves open space under its bar, and the electron crowds the M. Read by
the bracket that paints it, and by RESET, which fades it and stands where it
stands. */
struct BracketName {
	static constexpr float SC = 7.5f / NUNITO_LINE, LS = 3.2f;
	static constexpr float AIR_T = 3.1f, AIR_M = 3.52f;
	float a0 = 0.f, a1 = 0.f, m0 = 0.f, m1 = 0.f; // the ink of AT and of MIC, each from its own origin
	float left, right; // the ink of the whole, mark and all
	float oc;          // the mark's centre
	BracketName(float x0, float w) {
		nunitoInk("AT", SC, LS, a0, a1);
		nunitoInk("MIC", SC, LS, m0, m1);
		const float wide = (a1 - a0) + AIR_T + 2.f * ATOM_R + AIR_M + (m1 - m0);
		left = x0 + w / 2.f - wide / 2.f;
		right = left + wide;
		oc = left + (a1 - a0) + AIR_T + ATOM_R;
	}
	/** The caps centred on a bracket `h` tall, where the mark's centre is. */
	static float base(float h) { return h / 2.f + NUNITO_CAP * SC / 2.f; }
	float atX() const { return left - a0; }
	float micX() const { return oc + ATOM_R + AIR_M - m0; }
	void slide(float dx) {
		left += dx;
		right += dx;
		oc += dx;
	}
	/** AT, the mark and MIC on a bracket `h` tall, the font, its size, its
	spacing, a left baseline and the ink already set: the bracket's name and the
	bracket's, fading under RESET, are this one drawing. */
	void paint(NVGcontext* vg, float h) const {
		const float b = base(h);
		nvgText(vg, atX(), b, "AT", nullptr);
		nvgText(vg, micX(), b, "MIC", nullptr);
		paintAtom(vg, oc, h / 2.f);
	}
};

/** The bracket's own face as the lit layer paints it under a word laid over the
bracket (RESET, NO AUDIO, the hint line), the plate's gradient darkened with the
room as the plate is. */
inline NVGpaint bracketFace(NVGcontext* vg) {
	return plateGradientIn(vg, settings::rackBrightness);
}

/** The bracket itself, from `x0` for `w`, `h` tall, in whatever space the
caller has set up. `clip0..clip1` is the stretch that will be seen: the
dimples are a path of some six hundred circles the length of a long rig,
and a tile that shows one module of it need not tessellate the rest. */
inline void paintBracket(NVGcontext* vg, float x0, float w, float h, float clip0, float clip1) {
	// From a pixel down: the top pixel is the border line, ground of the
	// run's own, painted before the bracket and left showing.
	nvgBeginPath(vg);
	nvgRect(vg, x0, 1.f, w, h - 1.f);
	nvgFillPaint(vg, plateGradient(vg, RACK_GRID_HEIGHT));
	nvgFill(vg);
	// A shadow under it, the way the bracket shades the panel it sits on: on the
	// plate below the bracket's edge, darkest at the edge and fading down;
	// inside the bracket's own foot, darkest at the plate, it would read as the
	// plate standing over the bracket and shading it.
	nvgBeginPath(vg);
	nvgRect(vg, x0, h, w, mm2px(0.5f));
	nvgFillPaint(vg, nvgLinearGradient(vg, 0.f, h, 0.f, h + mm2px(0.5f),
	                                   nvgRGBA(0, 0, 0, 40), nvgRGBA(0, 0, 0, 0)));
	nvgFill(vg);

	// A bolt at each end, where a bracket is fixed to the rack: the mark.
	for (int i = 0; i < 2; i++) {
		const float cx = i ? x0 + w - mm2px(2.54f) : x0 + mm2px(2.54f);
		const float cy = h / 2.f;
		if (cx + ATOM_R < clip0 || cx - ATOM_R > clip1)
			continue;
		paintAtom(vg, cx, cy);
	}

	// The grip: two rows of dimples the length of the bracket, stopping
	// clear of the name in the middle.
	std::shared_ptr<Font> font = plateFont();
	const float mid = x0 + w / 2.f;
	float left = mid, right = mid; // the name's ink, which the dimples stand clear of
	// The name with the mark for its O (`BracketName`), in the bracket's grey.
	const BracketName name(x0, w);
	if (ATOM_IN_NAME) {
		left = name.left;
		right = name.right;
	}
	else if (font) {
		// The plain name, every letter set in the font, no mark for its O.
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, 7.5f);
		nvgTextLetterSpacing(vg, 2.4f);
		const float half = nvgTextBounds(vg, 0.f, 0.f, BRAND, nullptr, nullptr) / 2.f;
		left = mid - half;
		right = mid + half;
	}
	const float dr = DIMPLE_R;
	nvgBeginPath(vg);
	for (float x = x0 + mm2px(5.2f); x < x0 + w - mm2px(4.8f); x += mm2px(1.5f)) {
		if (x + dr < clip0 || x - dr > clip1)
			continue;
		if (x > left - mm2px(2.f) && x < right + mm2px(2.f))
			continue;
		for (int i = 0; i < 2; i++)
			nvgCircle(vg, x, h * (i ? 0.64f : 0.36f), dr);
	}
	fillDimples(vg);

	if (!font || right < clip0 || left > clip1)
		return;
	nvgFontFaceId(vg, font->handle);
	nvgFontSize(vg, 7.5f);
	nvgFillColor(vg, BRACKET_NAME_INK);
	if (ATOM_IN_NAME) {
		nvgTextLetterSpacing(vg, BracketName::LS);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		name.paint(vg, h);
	}
	else {
		nvgTextLetterSpacing(vg, 2.4f);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		// The letter spacing trails the last letter, so the word is
		// pulled back by half of one to sit true on the middle.
		nvgText(vg, mid - 1.2f, h / 2.f, BRAND, nullptr);
	}
}

struct Bracket : OpaqueWidget, StaticPart {
	// Every member, left to right, at drag start, held by id: an undo in the
	// middle of the drag may take a member away (`HeldModule`).
	std::vector<HeldModule> rig;
	std::vector<Vec> offsets;       // each one's position relative to our panel
	bool grabbed = false;
	/** Where the pointer has taken us, carried rather than read back from the
	panel: Rack refuses a move onto an occupied slot, so a position derived
	from box.pos stalls the moment the drag pushes into our own rig. */
	Vec dragPos;

	ModuleWidget* panel() { return dynamic_cast<ModuleWidget*>(getParent()); }

	/** Live, and only as the fallback: with tiles on, every member's tile
	carries its slice of the bracket and marks the rig bracketed. */
	void draw(const DrawArgs& args) override {
		float x0, w;
		if (!dev().bracket || !rigSpan(panel(), x0, w))
			return;
		if (groundPainted(panel(), BRACKET_JOB))
			return; // a member drawn before us has bracketed the rig this frame
		if (const std::vector<ModuleWidget*>* run = runOf(panel()))
			for (ModuleWidget* m : *run)
				markGroundPainted(m, BRACKET_JOB);
		NVGcontext* vg = args.vg;
		nvgSave(vg);
		nvgResetScissor(vg); // the bracket is the rig's, not this panel's
		paintBracket(vg, x0, w, box.size.y, x0, x0 + w);
		nvgRestore(vg);
	}

	/** Only a rig has a bracket; on a lone module the screws below are Rack's to
	handle, so the click goes straight past us. */
	void onButton(const ButtonEvent& e) override {
		float x0, w;
		if (!rigSpan(panel(), x0, w))
			return;
		OpaqueWidget::onButton(e);
	}

	void onDragStart(const DragStartEvent& e) override {
		rig.clear();
		offsets.clear();
		grabbed = false;
		ModuleWidget* mine = panel();
		if (e.button != GLFW_MOUSE_BUTTON_LEFT || !mine || !mine->module)
			return;
		RackWidget* rack = APP->scene->rack;
		dragPos = mine->box.pos;
		// The run the bracket stands on, the one the bracket was drawn for.
		const std::vector<ModuleWidget*>* run = runOf(mine);
		if (!run || run->size() < 2)
			return;
		for (ModuleWidget* w : *run) {
			rig.push_back(HeldModule(w->module));
			offsets.push_back(w->box.pos.minus(mine->box.pos));
		}
		rack->updateModuleOldPositions(); // what the undo step is measured from
		grabbed = true;
	}

	/** Move the rig as one rigid thing. Each module is placed where a member
	of the rig already stands, so they go in the order that keeps every target
	clear: leftmost first heading left, rightmost first heading right. */
	void onDragMove(const DragMoveEvent& e) override {
		if (!grabbed || settings::lockModules)
			return;
		const float zoom = std::max(APP->scene->rackScroll->getZoom(), 0.01f);
		dragPos = dragPos.plus(e.mouseDelta.div(zoom));
		const Vec base = framedrag::snapToGrid(dragPos, RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
		RackWidget* rack = APP->scene->rack;
		ModuleWidget* mine = panel();
		const bool leftward = mine && base.x < mine->box.pos.x;
		for (int i : framedrag::placementOrder((int)rig.size(), leftward)) {
			const Vec want = base.plus(offsets[i]);
			ModuleWidget* w = rig[i].widget();
			if (w && !w->box.pos.equals(want))
				rack->setModulePosForce(w, want);
		}
	}

	void onDragEnd(const DragEndEvent& e) override {
		if (grabbed) {
			// One undo step covering every module that moved.
			history::ComplexAction* h = APP->scene->rack->getModuleDragAction();
			if (h && !h->isEmpty())
				APP->history->push(h);
			else
				delete h;
		}
		rig.clear();
		offsets.clear();
		grabbed = false;
	}
};

/** The bracket's two states.
Plain, the bracket as it always stands: its name and its grip. Under a hint
line, the hint and nothing else between the bolts at its ends: no name and no
dimples, the bolts' marks kept. In on the frame a line goes up on the rig and
out on the frame the last goes, with no hold and no fade either way. The rig is named by the first member of
its run, set by `hintFrame` each frame before anything draws. */
inline const ModuleWidget*& hintBracketFront() {
	static const ModuleWidget* front = nullptr;
	return front;
}
/** Whether the bracket of the run `front` leads is in its hint state. */
inline bool hintState(const ModuleWidget* front) {
	return front && hintBracketFront() == front;
}

/** RESET on the bracket: while the rig holds anything a Sync starts again, a Seq
or a Clock, the name on the bracket is a Sync's button, and under the pointer it
turns to RESET in red, a click starting every reader and Clock of the rig
again together, in line or not. While anything is off the line a Sync would
put it on, the name breathes, ATOMIC fading out as RESET fades in and back
again, a few seconds a breath, in the bracket's own grey; and the moment the rig
falls off the line, a ring of a Seq's or a Clock's mark going from whole to
empty, RESET flashes in, bright red and glowing, for a few seconds before it
fades into the breath, so a first-time user sees it is there. A rig in line
and not under the pointer shows ATOMIC as ever. Live and in the lit layer,
which is drawn after every module's plate, so the word lies over the bracket
whichever member painted it, its patch of bracket and its letters darkened with
the room as the bracket is, the flash excepted. Every member has one and the
first of the run draws it, while a click is taken by whichever member's is
under the pointer. */
struct BracketReset : Widget, HintSource {
	bool hovered = false;
	/** Under the pointer the word is a button whose red says what a click
	does, so it says no gesture and the bracket's keys give way to it rather
	than paint their line over it (`MemberWidget::hinted`). */
	const Hinted* hinted() override { return nullptr; }
	/** One breath, in seconds. */
	static constexpr double BREATH = 4.0;
	/** The flash: in, held, and faded into the breath, in seconds. */
	static constexpr double FLASH_IN = 0.25, FLASH_HOLD = 1.5, FLASH_OUT = 1.5;
	/** When the rig last fell off the line, as this widget saw it; and
	whether it has seen it in line, so a rig loaded already off does not
	flash. */
	double fellAt = -1e9;
	bool wasOff = true;
	/** When the word was last pressed here, for the flash that answers a
	press. */
	double pressedAt = -1e9;
	static constexpr double PRESS_FLASH = 0.45;
	ModuleWidget* panel() { return dynamic_cast<ModuleWidget*>(getParent()); }
	/** The rigs this word speaks for: one member of each rig the drawn run
	holds, a bonder parting them, so a bracket across a GAP resets the
	rigs on both sides of it at once, and a RIG's speaks for the rig bonded to
	its right, a bonder being no member of any rig. */
	std::vector<Member*> rigModules() {
		std::vector<Member*> out;
		ModuleWidget* mw = panel();
		if (!mw)
			return out;
		const std::vector<ModuleWidget*>* run = runOf(mw);
		if (!run) {
			if (Member* m = dynamic_cast<Member*>(mw->module))
				out.push_back(m);
			return out;
		}
		bool fresh = true; // the next member starts a rig
		for (ModuleWidget* o : *run) {
			if (o->module && isBonder(o->module))
				fresh = true;
			else if (Member* m = dynamic_cast<Member*>(o->module)) {
				if (fresh)
					out.push_back(m);
				fresh = false;
			}
		}
		return out;
	}
	/** Whether any of my rigs is off the line, from the engine's word on it. */
	bool off() {
		for (Member* m : rigModules())
			if (relaxed(m->rigOffNow))
				return true;
		return false;
	}
	/** Whether any of my rigs holds anything a press would start again. */
	bool resettable() {
		for (Member* m : rigModules())
			if (relaxed(m->rigResettableNow))
				return true;
		return false;
	}
	/** The word's place, in my own space; false with no bracket. */
	bool place(float& left, float& right) {
		float x0, w;
		if (!rigSpan(panel(), x0, w))
			return false;
		const BracketName name(x0, w);
		left = name.left - mm2px(1.f);
		right = name.right + mm2px(1.f);
		return true;
	}
	bool over(Vec p) {
		float l, r;
		return place(l, r) && p.x >= l && p.x <= r && p.y >= 0.f && p.y <= box.size.y;
	}
	/** How bright the flash stands now, nought to one. */
	float flash() {
		const double t = frameNow() - fellAt;
		if (t < 0.0 || t > FLASH_IN + FLASH_HOLD + FLASH_OUT)
			return 0.f;
		if (t < FLASH_IN)
			return (float)(t / FLASH_IN);
		if (t < FLASH_IN + FLASH_HOLD)
			return 1.f;
		const float u = (float)((t - FLASH_IN - FLASH_HOLD) / FLASH_OUT);
		return 0.5f + 0.5f * std::cos((float)M_PI * u);
	}
	void step() override {
		const bool o = off();
		if (o && !wasOff)
			fellAt = frameNow();
		wasOff = o;
		Widget::step();
	}
	void onHover(const HoverEvent& e) override {
		hovered = resettable() && over(e.pos);
		if (hovered)
			e.consume(this);
		Widget::onHover(e);
	}
	void onLeave(const LeaveEvent& e) override {
		hovered = false;
		Widget::onLeave(e);
	}
	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && resettable() && over(e.pos)) {
			if (e.action == GLFW_PRESS) {
				for (Member* m : rigModules()) {
					m->resetAsked.store(true, std::memory_order_relaxed);
					m->wake();
					pressedAt = frameNow();
				}
			}
			e.consume(this);
			return;
		}
		Widget::onButton(e);
	}
	/** Whether the word is up this frame, painting its own patch of bracket over
	the name: on the first of a run that holds anything to reset, while the
	rig is off the line, the word hovered, or a press is being answered.
	`lit` the hover, any member's, `o` off the line, `pf` the press's flash. */
	bool speaking(bool& lit, bool& o, float& pf) {
		lit = o = false;
		pf = 0.f;
		if (!resettable())
			return false;
		ModuleWidget* mw = panel();
		const std::vector<ModuleWidget*>* run = mw ? runOf(mw) : nullptr;
		if (!run || run->size() < 2 || run->front() != mw)
			return false;
		double pressed = -1e9;
		for (ModuleWidget* m : *run)
			for (Widget* c : m->children)
				if (BracketReset* rs = castTo<BracketReset>(c)) {
					lit = lit || rs->hovered;
					pressed = std::max(pressed, rs->pressedAt);
				}
		o = off();
		// A press answers with a flash of its own, the word bright and
		// glowing and fading back over a moment, in line or not.
		pf = pressFlash(pressed, PRESS_FLASH);
		return o || lit || pf > 0.f;
	}
	bool speaking() {
		bool lit, o;
		float pf;
		return speaking(lit, o, pf);
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		bool lit, o;
		float pf;
		if (layer != 1 || !speaking(lit, o, pf))
			return;
		ModuleWidget* mw = panel();
		// The hover is any member's; the first of the run draws it, and its
		// own flash.
		const float fl = trace::recording() ? 0.f : std::max(o && !lit ? flash() : 0.f, pf);
		float x0, w, left, right;
		if (!rigSpan(mw, x0, w) || !place(left, right))
			return;
		std::shared_ptr<Font> font = plateFont();
		if (!font)
			return;
		NVGcontext* vg = args.vg;
		const float h = box.size.y, room = settings::rackBrightness;
		const NVGcolor grey = dimmed(BRACKET_NAME_INK, room);
		// How far into RESET the breath is: nought ATOMIC, one RESET, a cosine
		// between, by the frame's clock as a Seq's PREVIEW breathes; held on
		// RESET under the pointer and through the flash.
		// A traced frame is taken at the top of the breath.
		const float sync = lit || trace::recording() ? 1.f : std::max(o ? breath(BREATH, 0.f) : 0.f, fl);
		const NVGcolor red = SCHEME_RED;
		nvgSave(vg);
		nvgResetScissor(vg);
		// The bracket's own face over the name, as dark as the room has made it.
		nvgBeginPath(vg);
		nvgRect(vg, left, 1.f, right - left, h - 2.f);
		nvgFillPaint(vg, bracketFace(vg));
		nvgFill(vg);
		const float base = BracketName::base(h);
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, 7.5f);
		nvgTextLetterSpacing(vg, BracketName::LS);
		// ATOMIC, as `paintBracket` sets it, fading as RESET comes.
		if (sync < 1.f) {
			const BracketName name(x0, w);
			// and gone in the bracket's hint state, as the name is (`hintState`)
			nvgGlobalAlpha(vg, hintState(mw) ? 0.f : 1.f - sync);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
			nvgFillColor(vg, grey);
			name.paint(vg, h);
		}
		if (sync > 0.f) {
			const float sx = (left + right) / 2.f + 1.6f; // half a letter's spacing trails it
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
			// The flash's glow, the word blurred under itself, bright
			// whatever the room.
			if (fl > 0.f) {
				nvgGlobalAlpha(vg, fl);
				nvgFontBlur(vg, 3.f);
				nvgFillColor(vg, nvgRGBA(0xff, 0x30, 0x20, 0xc0));
				nvgText(vg, sx, base, "RESET", nullptr);
				nvgFontBlur(vg, 0.f);
			}
			nvgGlobalAlpha(vg, sync);
			nvgFillColor(vg, lit ? red : fl > 0.f ? rig::over(grey, red, fl) : grey);
			nvgText(vg, sx, base, "RESET", nullptr);
		}
		nvgRestore(vg);
	}
};

/** NO AUDIO on the bracket while the engine stands still (`engineSilent`), in
place of the bracket's name, once a run, bright red and bold, since a dim word in
the bracket's own grey does not catch the eye: the screens are not broken, the engine is not running. Gone the frame the engine steps
again. Drawn by the first of the run, over RESET's breath if that is up. */
struct BracketNoAudio : TransparentWidget, Unpowered {
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1 || !engineSilent())
			return;
		ModuleWidget* mw = dynamic_cast<ModuleWidget*>(getParent());
		const std::vector<ModuleWidget*>* run = mw ? runOf(mw) : nullptr;
		if (!run || run->size() < 2 || run->front() != mw)
			return;
		float x0, w;
		if (!rigSpan(mw, x0, w))
			return;
		std::shared_ptr<Font> font = plateFont();
		if (!font)
			return;
		NVGcontext* vg = args.vg;
		const float h = box.size.y;
		const float ls = BracketName::LS;
		nvgSave(vg);
		nvgResetScissor(vg);
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, 7.5f);
		nvgTextLetterSpacing(vg, ls);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
		// The word's advance, the last letter's trailing spacing taken off,
		// with a letter's spacing of air either side and the bracket's face
		// under it, so no dot of the bracket touches a letter.
		const float wide = nvgTextBounds(vg, 0.f, 0.f, "NO AUDIO", nullptr, nullptr) - ls;
		const float cx = x0 + w / 2.f, half = wide / 2.f + mm2px(2.5f);
		nvgBeginPath(vg);
		nvgRect(vg, cx - half, 1.f, 2.f * half, h - 2.f);
		nvgFillPaint(vg, bracketFace(vg));
		nvgFill(vg);
		// Bold by drawing it three times a little apart, since the plate's
		// one face is the boldest Nunito there is; red in the lit layer, so it
		// stays bright when the room darkens.
		nvgFillColor(vg, SCHEME_RED);
		for (float dx : {-0.25f, 0.f, 0.25f})
			nvgText(vg, cx + ls / 2.f + dx, BracketName::base(h), "NO AUDIO", nullptr); // half a spacing trails it, as RESET's
		nvgRestore(vg);
	}
};

/** The line every tooltip over a module of ours ends with while the engine
stands still, which is what the bracket's NO AUDIO says. Kept
here rather than with the modules' words, since it is the window's and never
the engine's. */
const char* const NO_AUDIO_TIP = "No audio device selected, so Rack's engine is stopped.";

/** A widget laid over the whole scene, its size kept the scene's as the
window changes, or the scene's draw would pass it by as standing outside
what it draws: the line under a tooltip (`NoAudioTips`) and the swap menu's
picture (`SwapMenuOverlay`). */
struct SceneOverlay : TransparentWidget {
	void step() override {
		if (parent)
			box.size = parent->box.size;
		TransparentWidget::step();
	}
};

/** Puts `NO_AUDIO_TIP` under whatever tooltip is showing over one of our
modules while the engine stands still, and takes it off again when it runs.
Rack's own tooltips write their words afresh in every step, so the line is
added in the draw, after every step and before the tooltip's own draw (this
stands in the scene before any tooltip, made by the first of ours to step),
and the box made to fit it as a tooltip's own step fits it. The words a
module's params and jacks are given are left alone, so a capture of them is
the same with the engine running or not. The same draw stands each tooltip
where it belongs (`placeTip`, Tips.hpp), there being no later moment before
it is drawn. */
struct NoAudioTips : SceneOverlay {
	void draw(const DrawArgs& args) override {
		if (!APP->scene || !APP->event)
			return;
		const std::string tail = std::string("\n\n") + NO_AUDIO_TIP;
		bool ours = false;
		if (engineSilent())
			for (Widget* w = APP->event->getHoveredWidget(); w; w = w->parent)
				if (ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w)) {
					ours = mw->model && mw->model->plugin == pluginInstance;
					break;
				}
		for (Widget* c : APP->scene->children) {
			ui::Tooltip* tip = dynamic_cast<ui::Tooltip*>(c);
			if (!tip)
				continue;
			placeTip(tip);
			const bool has = tip->text.size() >= tail.size() &&
			                 tip->text.compare(tip->text.size() - tail.size(), tail.size(), tail) == 0;
			if (ours == has)
				continue;
			if (ours)
				tip->text += tail;
			else
				tip->text.resize(tip->text.size() - tail.size());
			tip->box.size.x = bndLabelWidth(args.vg, -1, tip->text.c_str()) + 10.f;
			tip->box.size.y = bndLabelHeight(args.vg, -1, tip->text.c_str(), INFINITY);
		}
	}
};

/** The scene's one `NoAudioTips`, made on the first call. */
inline void noAudioTips() {
	if (!APP->scene)
		return;
	for (Widget* c : APP->scene->children)
		if (dynamic_cast<NoAudioTips*>(c))
			return;
	NoAudioTips* t = new NoAudioTips;
	t->box.size = APP->scene->box.size;
	APP->scene->addChild(t);
}

inline void addBracketHint(ModuleWidget* w); // below

/** The bracket over the top screws, which step aside for it: see TopScrew. */
inline void addBracket(ModuleWidget* w) {
	Bracket* b = new Bracket;
	b->box.size = Vec(w->box.size.x, RACK_GRID_WIDTH);
	w->addChild(b);
	BracketReset* s = new BracketReset;
	s->box.size = b->box.size;
	w->addChild(s);
	BracketNoAudio* q = new BracketNoAudio;
	q->box.size = b->box.size;
	w->addChild(q);
	addBracketHint(w);
}

/** Where a panel of ours would have a screw it has the mark instead, at the
bracket bolt's size, centred where the stock screw's head was. */
struct AtomScrew : Widget, StaticPart {
	AtomScrew() {
		box.size = Vec(RACK_GRID_WIDTH, RACK_GRID_WIDTH);
	}
	void draw(const DrawArgs& args) override {
		if (!dev().statics)
			return;
		paintAtom(args.vg, box.size.x / 2.f, box.size.y / 2.f);
	}
};

/** The top screw, which steps aside while its module stands in a rig. The
bracket is painted once, by the first member drawn, and a screw of a later
member would otherwise be drawn on top of it; and a screw under a bracket is a
screw nobody sees, so it is not drawn rather than covered. Alone, the module
keeps it. */
struct TopScrew : AtomScrew {
	void step() override {
		ModuleWidget* mw = getAncestorOfType<ModuleWidget>();
		float x0, w;
		visible = !(mw && rigSpan(mw, x0, w));
		AtomScrew::step();
	}
};

inline void addScrews(ModuleWidget* w); // with the ground it lays, in Ground.hpp

/** Params of `mine`'s module set as `writesOf` says for it, or with `wholeRun`
those of every module of its model under its bracket, each asked for its own, bypassed or
not (`runOf`, a GAP parting nothing), as one undo named `name`: a hand's one gesture over several
modules, which only the bracket makes. A Seq's BLEP and Oversampling, so a rig
of Seqs in hertz keeps them alike unless a hand asks otherwise, each Seq writing the pair in force
where it stands (`Seq::bandWrites`). */
inline void setAlongRun(ModuleWidget* mine,
                        const std::function<void(Module*, std::vector<std::pair<int, float>>&)>& writesOf, bool wholeRun,
                        const std::string& name) {
	if (!mine || !mine->module)
		return;
	std::vector<Module*> them{mine->module};
	if (wholeRun)
		// The rig, the bracket's span, so a GAP parts nothing: what "the
		// rig's" says is true, as the bracket's RESET acts across a GAP.
		if (const std::vector<ModuleWidget*>* run = runOf(mine))
			for (ModuleWidget* mw : *run)
				if (mw->module && mw->module != mine->module && mw->model == mine->model)
					them.push_back(mw->module);
	GestureChanges g;
	std::vector<std::pair<int, float>> writes;
	for (Module* m : them) {
		writes.clear();
		writesOf(m, writes);
		for (const std::pair<int, float>& w : writes)
			if (w.first >= 0 && w.first < (int)m->params.size() && m->paramQuantities[w.first]) {
				g.note(m, w.first);
				m->paramQuantities[w.first]->setValue(w.second);
			}
	}
	g.push(name);
}

/** The widget every module of ours is: a ModuleWidget that can be drawn
with one of its layers stood down, so the DEV module can measure what a
layer costs.

Which layer a widget is in is decided by `StaticPart`, and the static half
needs no help here: every paint function a static thing goes through (the
plate, the captions, the ground, the glass, the bracket) asks `dev()` itself,
so turning statics off empties them wherever they are drawn. What is left
for this is the live half, and the live half cannot be found among the
module's own children: a screen is ground, and therefore static, but the
readout, the bar, the rings and the playheads are its children. Hiding the
screen would take its contents with it and leave the panel measuring
nothing. So the walk goes all the way down and stands down every widget that
is not static, wherever it hangs, and the ground it stands on stays.

With every switch on, which is how it always is outside a benchmark, this is
Rack's own ModuleWidget and nothing else. */
inline void makeWrites(Member* m); // Writes.hpp
/** The bracket's gestures (Words.hpp): the menu key over a module opens the
swap menu, which a player knows as the Atomic menu ("Swap" tells nobody what the
key opens), and with the pointer on the bracket Delete takes the rig and the
duplicate keys copy it, shift adding its cables. Shift held with the menu's key,
a click on the head deletes the module (`SwapMenu`, Swap.hpp): said beside the
key on the title, and alone on the menu's head once it is open. */
inline const Gesture G_BRACKET_SWAP =
    declare(On::Key, Mod::CommandOrAlt, Rank::Invented, "Hold " RACK_MOD_CTRL_NAME " or Alt: Atomic menu");
inline const Gesture G_SWAP_DELETE =
    declare(On::Click, Mod::Shift, Rank::Invented, RACK_MOD_SHIFT_NAME "+" RACK_MOD_CTRL_NAME "-click: delete module");
inline const Gesture G_BRACKET_DELETE = declare(On::Key, Mod::None, Rank::Known, "Delete: remove rig");
inline const Gesture G_BRACKET_DUP = declare(On::Key, Mod::CommandOrAlt, Rank::Known, RACK_MOD_CTRL_NAME "-D or Alt-D: duplicate rig");
inline const Gesture G_BRACKET_CABLES = declare(On::Key, Mod::Shift, Rank::Rare, RACK_MOD_SHIFT_NAME " too: with its cables");
struct SwapMenu;
inline SwapMenu* swapMenu(); // Swap.hpp
inline bool deleteRigAt(ModuleWidget* mw, const event::HoverKey& e); // Swap.hpp
inline bool duplicateRigAt(ModuleWidget* mw, const event::HoverKey& e); // Swap.hpp
inline void hintFrame();                                // below
inline void forgetHint(ModuleWidget* mw);              // below
inline void appendHelp(ModuleWidget* mw, ui::Menu* menu); // below
inline ModuleWidget* spawnApart(ModuleWidget* mw, Model* model); // Swap.hpp

struct MemberWidget : ModuleWidget, HintSource {
	/** What the panel itself says, under the pointer where no part does:
	on the title, the key that opens the swap menu over it, and the chord
	that deletes from it; on the bracket, nothing, so the hand meeting it
	finds it as it stands, the mark and the RESET word, to drag the rig by
	(its keys, `G_BRACKET_DELETE` and its kin, are Rack's own on a module,
	read through `Hand` in Swap.hpp); elsewhere what a module's panel says of a place with no widget of its own
	(`partHinted`). Unpowered the hand meets no control
	(`unpowered`), so nothing but the title's keys, which still work, says a
	stroke: one rule for every module, the dark tooltip naming the control
	(`sayOwn`) and the line under it saying why (`NoAudioTips`). */
	const Hinted* hinted() final {
		static Hinted title;
		if (title.gestures.empty()) {
			title.gestures = {&G_BRACKET_SWAP, &G_SWAP_DELETE};
			title.hint = hintOf(title.gestures);
		}
		if (!APP || !APP->scene || !APP->scene->rack)
			return nullptr;
		const float y = APP->scene->rack->getMousePos().y - box.pos.y;
		if (y < mm2px(BRACKET_H))
			return nullptr;
		if (y >= mm2px(BRACKET_H) && y < mm2px(SCREEN_Y))
			return &title;
		return unpowered() ? nullptr : partHinted();
	}
	/** What a place on the panel with no widget of its own says, a margin
	of a screen; none by default. */
	virtual const Hinted* partHinted() { return nullptr; }
	/** The usual opening of a module's widget: the module, a panel of the
	usual width, its plate, and the screws and ground every panel has. */
	void open(Module* m, Widget* panel) {
		setModule(m);
		box.size = Vec(mm2px(PANEL_W), RACK_GRID_HEIGHT);
		addPanel(this, panel);
		addScrews(this);
	}
	/** Everything worked out once a frame (the runs, each module's ground,
	the run's picture) holds module widgets by pointer, so a widget going
	makes the frame a new one: deleted in a step, before the draw of the frame
	that had already sorted the runs, it would be drawn from the list it had
	left (found by a test host that draws after every step). */
	~MemberWidget() override {
		resetGroundMarks();
		forgetTips(module);
		forgetHint(this);
	}
	/** Every module's menu ends with Atomic Help, one submenu the same on
	each, after whatever the module sets for itself. A module adding its own
	items calls this after them, on every path. */
	void appendContextMenu(ui::Menu* menu) override {
		appendHelp(this, menu);
	}
	void onHoverKey(const HoverKeyEvent& e) override {
		if (deleteRigAt(this, e) || duplicateRigAt(this, e))
			return;
		if (!unpowered())
			ModuleWidget::onHoverKey(e);
		else if (!toPowered(this, &Widget::onHoverKey, e)) {
			SetAside aside(this);
			ModuleWidget::onHoverKey(e);
		}
	}

	/* The unpowered face: the entire rig says plainly that there is no
	electricity. While the engine stands still (`engineSilent`) every module of ours draws
	its ground and nothing standing on it: the plate, its captions and the
	glass stay, and every digit, ghost, curve, lamp, playhead, fader and
	button goes, the hole it stood in black. What it keeps is what keeps its
	power: the static parts that are no ground (the panel, the tile, the
	bracket, the screws), the jacks, unlit, and what is marked `Unpowered`;
	a control with a face of its own while unpowered draws it (`DarkFace`).
	The hand meets only the same: a press, a drag, a scroll, a hover goes to
	a jack or the bracket under it, or else to the module as Rack's own
	plate, never to a control. Nothing a module holds changes, so on the
	frame the engine steps again the face is the one it left. One walk for
	every module, the drawing's and the hand's, so no module can be half
	dark; its per-module part is what a ground part paints while unpowered
	(a Qnt's roll keeps its lanes and loses its notes, `KeyCell::rollColor`)
	and a `DarkFace`. */
	bool unpowered() { return module && engineSilent(); }
	/** Whether `c` keeps its power unpowered. */
	static bool keepsPower(Widget* c) {
		if (castTo<GroundPart>(c))
			return false;
		return castTo<StaticPart>(c) || castTo<PortWidget>(c) || castTo<app::SvgScrew>(c) || castTo<Unpowered>(c);
	}
	/** Rack's `drawChild` for the unpowered walk: what keeps its power drawn
	as ever, a jack in the plain layer alone, its lit centre being the
	engine's; a ground part its ground; a `DarkFace` its own; and on down
	through everything else, which draws nothing itself, for the ground and
	the jacks standing in it. A screen keeps what stands on it clipped to its
	glass, as `Screen::mineOnly` does. */
	static void drawUnpowered(Widget* w, const DrawArgs& args, int layer) {
		for (Widget* c : w->children) {
			if (!c->isVisible() || !args.clipBox.intersects(c->box))
				continue;
			DrawArgs ca = args;
			ca.clipBox = args.clipBox.intersect(c->box);
			ca.clipBox.pos = ca.clipBox.pos.minus(c->box.pos);
			nvgSave(args.vg);
			nvgTranslate(args.vg, c->box.pos.x, c->box.pos.y);
			if (keepsPower(c)) {
				if (layer == 0)
					c->draw(ca);
				else if (!castTo<PortWidget>(c))
					c->drawLayer(ca, layer);
			}
			else {
				GroundPart* g = castTo<GroundPart>(c);
				if (g && g->isGround())
					paintGroundPart(c, g, ca, layer);
				if (DarkFace* d = castTo<DarkFace>(c)) {
					if (layer == 0)
						d->drawDark(ca);
				}
				else {
					if (castTo<Screen>(c))
						nvgIntersectScissor(args.vg, 0.f, 0.f, c->box.size.x, c->box.size.y);
					drawUnpowered(c, ca, layer);
				}
			}
			nvgRestore(args.vg);
		}
	}
	/** `ModuleWidget::draw` with the unpowered walk for its children: the
	scissor, a bypassed module's fade and the selection's red, and no CPU
	meter, there being no engine to measure. */
	void drawUnpowered(const DrawArgs& args) {
		nvgScissor(args.vg, RECT_ARGS(args.clipBox));
		if (module->isBypassed())
			nvgAlpha(args.vg, 0.33f);
		drawUnpowered(this, args, 0);
		if (APP->scene && APP->scene->rack && APP->scene->rack->isSelected(this)) {
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
			nvgFillColor(args.vg, nvgRGBAf(1.f, 0.f, 0.f, 0.25f));
			nvgFill(args.vg);
			nvgStrokeWidth(args.vg, 2.f);
			nvgStrokeColor(args.vg, nvgRGBAf(1.f, 0.f, 0.f, 0.5f));
			nvgStroke(args.vg);
		}
		nvgResetScissor(args.vg);
	}
	/** A positional event handed, as Rack's recursion hands it, to what
	keeps its power under the pointer, wherever it stands in the tree (a
	TRG's jacks stand in its screen). Whether one took it. */
	template <class TEvent>
	static bool toPowered(Widget* w, void (Widget::*f)(const TEvent&), const TEvent& e) {
		for (auto it = w->children.rbegin(); it != w->children.rend() && e.isPropagating(); it++) {
			Widget* c = *it;
			if (!c->isVisible() || !c->box.contains(e.pos))
				continue;
			TEvent e2 = e;
			e2.pos = e.pos.minus(c->box.pos);
			if (keepsPower(c))
				(c->*f)(e2);
			else
				toPowered(c, f, e2);
		}
		return !e.isPropagating();
	}
	/** Every child hidden for as long as Rack's own handling of an event
	takes, so what is left is the module itself (its drag, its menu, its
	selection, its keys), and shown again; the ground's walk still counts
	them (`setAside`). */
	struct SetAside {
		std::vector<Widget*>& held = setAside();
		size_t from;
		SetAside(Widget* w) : from(held.size()) {
			for (Widget* c : w->children)
				if (c->isVisible()) {
					c->setVisible(false);
					held.push_back(c);
				}
		}
		~SetAside() {
			for (size_t i = from; i < held.size(); i++)
				held[i]->setVisible(true);
			held.resize(from);
		}
	};
#define MEMBER_UNPOWERED_EVENT(Event, On)                     \
	void On(const Event& e) override {                      \
		if (!unpowered())                                   \
			return ModuleWidget::On(e);                     \
		if (toPowered(this, &Widget::On, e))                \
			return;                                         \
		SetAside aside(this);                               \
		ModuleWidget::On(e);                                \
	}
	MEMBER_UNPOWERED_EVENT(HoverEvent, onHover)
	/** And a left press on the ground is held, so the glass stays the
	instrument rather than the plate, unpowered as powered (`FlatScreen`):
	a stroke on a dark screen neither moves a control nor the module. */
	void onButton(const ButtonEvent& e) override {
		if (!unpowered())
			return ModuleWidget::onButton(e);
		if (toPowered(this, &Widget::onButton, e))
			return;
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS)
			for (const GroundRect& g : groundOf(this))
				if (g.depth == 0 && g.lip && g.r.contains(e.pos)) {
					e.consume(nullptr); // taken, and nothing dragged
					return;
				}
		SetAside aside(this);
		ModuleWidget::onButton(e);
	}
	MEMBER_UNPOWERED_EVENT(HoverScrollEvent, onHoverScroll)
	MEMBER_UNPOWERED_EVENT(HoverTextEvent, onHoverText)
	MEMBER_UNPOWERED_EVENT(DragHoverEvent, onDragHover)
	MEMBER_UNPOWERED_EVENT(PathDropEvent, onPathDrop)
#undef MEMBER_UNPOWERED_EVENT
	/** Every widget in the subtree that is not static, hidden. Hiding rather
	than skipping, so Rack's own drawing (the scissor, the bypassed panel,
	the selection) is the stock one and only the children change. */
	static void standDownLive(Widget* w, std::vector<Widget*>& hid) {
		for (Widget* c : w->children) {
			if (!c->isVisible())
				continue;
			if (dynamic_cast<StaticPart*>(c)) {
				standDownLive(c, hid); // static, but its children may not be
				continue;
			}
			c->setVisible(false);
			hid.push_back(c);
		}
	}
	void drawLayers(const DrawArgs& args, bool wantLive, int layer) {
		std::vector<Widget*> hid;
		if (!wantLive)
			standDownLive(this, hid);
		if (layer < 0)
			ModuleWidget::draw(args);
		else
			ModuleWidget::drawLayer(args, layer);
		for (Widget* c : hid)
			c->setVisible(true);
	}
	void draw(const DrawArgs& args) override {
		if (unpowered())
			return drawUnpowered(args);
		drawLayers(args, dev().live, -1);
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer >= 0 && unpowered())
			return drawUnpowered(this, args, layer);
		if (layer != 1) {
			ModuleWidget::drawLayer(args, layer);
			return;
		}
		drawLayers(args, dev().liveLit, layer);
	}
	/** A dark control says it is dark in a tooltip: unpowered, the hand
	meets no control and the module itself is what is hovered, so the
	control under the pointer is found here, one path for every part of
	every module, and named (`DARK_TIP`); `NoAudioTips` adds why. A roll's
	row says it too: it keeps quiet powered only so as not to stand in the
	way of painting, and dark there is no painting to stand in the way of.
	The same tip says what the panel itself says on a lone module, its title
	band or a screen's margin, once the pointer has rested (`hintInTip`),
	the part having no tooltip of its own: one tip for the module's own
	surface. */
	HoverTip ownTip;
	static ParamWidget* darkAt(Widget* w, Vec pos) {
		for (auto it = w->children.rbegin(); it != w->children.rend(); it++) {
			Widget* c = *it;
			if (!c->isVisible() || !c->box.contains(pos) || keepsPower(c))
				continue;
			if (ParamWidget* p = dynamic_cast<ParamWidget*>(c))
				return p;
			if (ParamWidget* p = darkAt(c, pos.minus(c->box.pos)))
				return p;
		}
		return nullptr;
	}
	void sayOwn() {
		const bool mine = APP->event->getHoveredWidget() == this;
		ParamWidget* c = nullptr;
		if (mine && unpowered() && APP->scene && APP->scene->rack)
			c = darkAt(this, APP->scene->rack->getMousePos().minus(box.pos));
		engine::ParamQuantity* pq = c ? c->getParamQuantity() : nullptr;
		if (!mine || (!pq && !hintInTip)) {
			ownTip.tipHide();
			return;
		}
		const std::string said = pq ? pq->getLabel() + DARK_TIP : std::string();
		ownTip.tip ? ownTip.tipKeep(said) : ownTip.tipShow(said);
	}
	/** Every frame the module names what it is now, in its tooltips, and
	makes the writes the engine asked for. */
	void step() override {
		if (Member* m = dynamic_cast<Member*>(module)) {
			m->retipAll();
			makeWrites(m);
		}
		hintFrame(); // the hovered part's gestures on our panels, once a frame
		keepManualMode(); // the Manual item opens in the panels' mode
		sayOwn();
		showTips(module); // the lesson after a rest, the essentials at once
		swapMenu(); // the rack's, made by the first of ours to step
		noAudioTips(); // the scene's, the same
		cableGlow(); // the rack's, the same
#ifdef ATOMIC_DEV
		followTools(); // the tools plugin's DEV and PAL, once a frame
#endif
		ModuleWidget::step();
	}
};

/* The hint line: a part's hidden gestures shown on our own panels.

Rack's tooltips can be switched off in its View menu, and that switch is
the player's: we never override it. What a hand cannot guess, a modifier's
stroke, is shown instead on the panels, in a line of small letters while the
pointer is over the part, along the bracket of its rig (`HintPlace`), the
bracket's name giving way to it. A line longer than its place runs through it
as a ticker from the place's left (`HintLine`), and a bonded module's tooltip
says no gesture. A module standing alone has no bracket, and its title band
is too small for a line, so the same words go into the hovered part's own
tooltip, under its value, once the pointer has rested (`hintInTip`,
Words.hpp), its title band saying nothing. Both can be hidden by a
plugin-wide switch in Atomic Help, the submenu at the foot of every
module's menu, for a hand that knows the keys; nothing then says them
elsewhere. Drawn in the lit layer (`BracketHint`) by one member of
the rig, the last the rack draws of those the line crosses, so no
neighbour's lit layer lies over it. */

/** The switch, kept in Atomic.json in Rack's user folder. */
struct HintSettings {
	bool bracket = true;
};
inline std::string hintSettingsPath() { return asset::user("Atomic.json"); }
inline HintSettings& hintSettings() {
	static HintSettings s;
	static bool read = false;
	if (!read) {
		read = true;
		if (json_t* rootJ = loadJson(hintSettingsPath())) {
			if (json_t* j = json_object_get(rootJ, "hintBracket"))
				s.bracket = json_is_true(j);
			json_decref(rootJ);
		}
	}
	return s;
}
inline void saveHintSettings() {
	const HintSettings& s = hintSettings();
	json_t* rootJ = loadJson(hintSettingsPath());
	if (!rootJ || !json_is_object(rootJ)) {
		if (rootJ)
			json_decref(rootJ);
		rootJ = json_object();
	}
	json_object_set_new(rootJ, "hintBracket", json_boolean(s.bracket));
	json_object_del(rootJ, "hintStrip"); // the CRV screens' switch, which is gone
	json_object_del(rootJ, "hintRail");  // the same switch under the strip's old name
	saveJson(rootJ, hintSettingsPath(), JSON_INDENT(2));
	json_decref(rootJ);
}
/** Atomic Help, the one submenu at the foot of every module's menu, the same
on each, under the module's own items, built here alone. Its first item is
the plugin's first lesson: a RIG to learn from the factory's rigs, stood
apart to the right of the rig the menu was opened on (`spawnApart`,
Swap.hpp), so a rig being built is never touched; a new RIG shows the factory
(`RigWidget::placeShowing`). On a RIG itself there is nothing to spawn, and
the item is a line saying where its factory rigs are. Under it, the way to
the plugin's website (the manifest's `pluginUrl`), then the switch for the
hints, kept in Rack's user folder rather than in a module: anything
plugin-wide goes in here and never on one module's menu. The GAP's skin is
the one exception, chosen where it is seen, on any GAP's own menu, and the
RIG's folder, being where the RIG loads from, stands beside its loads. */
inline void appendHelp(ModuleWidget* mw, ui::Menu* menu) {
	menu->addChild(new ui::MenuSeparator);
	menu->addChild(createSubmenuItem("Atomic Help", "", whileStanding(mw, [](ModuleWidget* w, ui::Menu* sub) {
		if (w->model == modelRig)
			sub->addChild(createMenuLabel("Factory rigs: Load factory rig above, or the factory icon on the screen, then the arrows"));
		else
			sub->addChild(createMenuItem("Spawn RIG to learn from factory presets", "",
			                             whileStanding(w, [](ModuleWidget* o) { spawnApart(o, modelRig); })));
		sub->addChild(createMenuItem("Official website", "", [] { system::openBrowser(pluginInstance->pluginUrl); }));
		sub->addChild(tipBoolMenuItem(
		    "Hints on the bracket", "Shows keyboard shortcuts on the Atomic bracket.", [] { return hintSettings().bracket; },
		    [](bool on) {
			    hintSettings().bracket = on;
			    saveHintSettings();
		    }));
	})));
}

/** The bracket's height and where its line stands: the letters' middle on the
bracket's middle, clear of the bolts at each end. */
const float HINT_BRACKET_H = RACK_GRID_WIDTH;
const float HINT_BRACKET_BASE = HINT_BRACKET_H / 2.f + HintLine::SIZE * 0.36f;
const float HINT_BRACKET_CLEAR = mm2px(5.08f);
/** The ground under a line, beyond its ink at each end. */
const float HINT_PAD = mm2px(1.f);

/** Where a module's hint line stands, in its own pixels: `lo..hi` the
stretch its ink may take, `base` its baseline, its ground `top` and `h`
tall, and `faceL..faceR` the face laid over what the line replaces while it
is up: the bracket, between the bolts, the face from clear of one bolt's mark
to clear of the other's, the grip running from 5.2 mm to 4.8 mm short of the
ends (`paintBracket`). A module standing alone has none. */
struct HintPlace {
	float lo = 0.f, hi = 0.f;
	float base = 0.f;
	float top = 0.f, h = 0.f;
	float faceL = 0.f, faceR = 0.f;
};
inline bool hintPlaceOf(ModuleWidget* mw, HintPlace& p) {
	if (!mw)
		return false;
	float x0, w;
	if (!rigSpan(mw, x0, w))
		return false;
	const float bolt = mm2px(2.54f) + ATOM_R + mm2px(0.3f);
	p.lo = x0 + HINT_BRACKET_CLEAR;
	p.hi = x0 + w - HINT_BRACKET_CLEAR;
	p.base = HINT_BRACKET_BASE;
	p.top = 1.f;
	p.h = HINT_BRACKET_H - 2.f;
	p.faceL = x0 + bolt;
	p.faceR = x0 + w - bolt;
	return true;
}

/** What the panels show of the hovered part this frame: the module it
stands on, its place, and the line laid out in that module's own pixels. */
struct HintNow {
	ModuleWidget* mw = nullptr;
	HintPlace place;
	HintLine line;
	ModuleWidget* by = nullptr; // which member paints it (`hintPainter`)
	double since = 0.0; // when the part was first hovered, for the ticker
	/** Seconds since then, nought in a traced frame, which is written down
	to be compared and so is taken before the ticker moves. */
	double held() const { return trace::recording() ? 0.0 : frameNow() - since; }
};
inline HintNow& hintNow() {
	static HintNow h;
	return h;
}
inline void forgetHint(ModuleWidget* mw) {
	if (hintBracketFront() == mw)
		hintBracketFront() = nullptr;
	if (hintNow().mw == mw || hintNow().by == mw)
		hintNow() = HintNow();
}

/** The member that paints a line from `x0` to `x1` in `mw`'s pixels: of
those it crosses, the last the rack draws, so no lit layer of a neighbour's
drawn after it lies over a letter. */
inline ModuleWidget* hintPainter(ModuleWidget* mw, float x0, float x1) {
	ModuleWidget* by = mw;
	const std::vector<ModuleWidget*>* run = runOf(mw);
	if (!run || !APP->scene || !APP->scene->rack)
		return by;
	const float a0 = mw->box.pos.x + x0, a1 = mw->box.pos.x + x1;
	for (Widget* c : APP->scene->rack->getModuleContainer()->children) {
		ModuleWidget* o = dynamic_cast<ModuleWidget*>(c);
		if (!o || o->box.pos.x >= a1 || o->box.pos.x + o->box.size.x <= a0)
			continue;
		if (std::find(run->begin(), run->end(), o) != run->end())
			by = o;
	}
	return by;
}

/** Once a frame, from the first of our widgets to step: the part under the
pointer, or being dragged, its hint, and how much of it its place can show. */
inline void hintFrame() {
	static FrameId at;
	const FrameId now = frameId();
	if (!(now != at))
		return;
	at = now;
	HintNow& h = hintNow();
	h = HintNow();
	hintBracketFront() = nullptr;
	hintInTip = nullptr;
	if (!APP || !APP->event || !APP->window || !APP->window->vg)
		return;
	// The pointer decides:
	// what is under it, and what is being dragged only where nothing under
	// it says anything, the pointer having strayed off the part mid-stroke.
	Widget* pw = nullptr;
	const Hinted* hq = hintedOf(APP->event->getHoveredWidget(), pw);
	if (!hq || hq->hint.empty())
		hq = hintedOf(APP->event->getDraggedWidget(), pw);
	MemberWidget* mw = dynamic_cast<MemberWidget*>(pw);
	if (HintSource* hs = !mw ? dynamic_cast<HintSource*>(pw) : nullptr)
		mw = dynamic_cast<MemberWidget*>(hs->hintsFor());
	if (!mw && pw)
		mw = pw->getAncestorOfType<MemberWidget>();
	static const Hinted* was = nullptr;
	static ModuleWidget* wasOn = nullptr;
	static double since = 0.0;
	if (!hq || hq->hint.empty() || !mw) {
		was = nullptr;
		return;
	}
	if (hq != was || mw != wasOn) {
		was = hq;
		wasOn = mw;
		since = frameNow();
	}
	// Alone, no line: the title band is too small for one, so the part's own
	// tooltip says the hint once the pointer has rested (`hintInTip`).
	float x0, w;
	if (!rigSpan(mw, x0, w)) {
		if (hintSettings().bracket && learning())
			hintInTip = hq;
		return;
	}
	h.since = since;
	h.mw = mw;
	NVGcontext* vg = APP->window->vg;
	std::shared_ptr<Font> font = APP->window->uiFont;
	// The part's middle in my pixels, through the rack for a source that
	// stands outside me (`hintsFor`).
	Widget* rack = APP->scene ? APP->scene->rack : nullptr;
	float cx = pw == mw || pw->isDescendantOf(mw) || !rack
	               ? pw->getRelativeOffset(pw->box.size.div(2.f), mw).x
	               : pw->getRelativeOffset(pw->box.size.div(2.f), rack).x - mw->getRelativeOffset(Vec(), rack).x;
	if (hintSettings().bracket && hintPlaceOf(mw, h.place)) {
		// A fine stroke's readout first, so it is never what does not fit:
		// on the line of the part it is dragged on, or of the module it is
		// dragged in, where the widget dragged stands over the part hovered
		// (a QNT's reshaping catch over its keys, `QntShaper::say`).
		const Hand::Readout& r = Hand::readoutNow();
		const bool readout = r.by && !r.text.empty() && r.by == APP->event->getDraggedWidget() &&
		                     (pw == r.by || const_cast<Widget*>(r.by)->isDescendantOf(mw));
		h.line.layout(vg, font, readout ? r.text + "\n" + hq->hint : hq->hint, h.place.lo, h.place.hi, cx);
	}
	if (h.line.pieces || h.line.ticking()) {
		h.by = hintPainter(mw, h.line.left() - HINT_PAD, h.line.right() + HINT_PAD);
		// The bracket of the rig the line is up on is in its hint state.
		hintBracketFront() = runOf(mw)->front();
	}
}

/** The painter of the hint lines, one on every module, in the lit layer:
the one `hintFrame` chose for a line draws it, in the hovered module's
pixels, the scissor taken off as the bracket's own words take it. */
struct BracketHint : TransparentWidget, Unpowered {
	/** The hint state (`hintState`): a face over what the line replaces,
	the bracket's grip and name between the bolts, which stay. Painted by the member that paints the line, before it, or with no
	line on this rig by the first of the run. While RESET speaks it paints
	its own patch over the name, so the face here leaves the name's stretch
	to it. */
	void cover(NVGcontext* vg, ModuleWidget* mine) {
		const std::vector<ModuleWidget*>* run = runOf(mine);
		HintPlace p;
		if (!run || !hintState(run->front()) || !hintPlaceOf(mine, p))
			return;
		ModuleWidget* front = run->front();
		const HintNow& h = hintNow();
		const bool lineHere = h.by && std::find(run->begin(), run->end(), h.mw) != run->end();
		if ((lineHere ? h.by : front) != mine)
			return;
		bool reset = false;
		for (Widget* c : front->children)
			if (BracketReset* rs = castTo<BracketReset>(c))
				reset = reset || rs->speaking();
		nvgSave(vg);
		nvgResetScissor(vg);
		nvgBeginPath(vg);
		if (reset) {
			float x0, w;
			rigSpan(mine, x0, w);
			const BracketName name(x0, w);
			const float n0 = name.left - mm2px(1.f), n1 = name.right + mm2px(1.f);
			nvgRect(vg, p.faceL, p.top, n0 - p.faceL, p.h);
			nvgRect(vg, n1, p.top, p.faceR - n1, p.h);
		}
		else
			nvgRect(vg, p.faceL, p.top, p.faceR - p.faceL, p.h);
		nvgFillPaint(vg, bracketFace(vg));
		nvgFill(vg);
		nvgRestore(vg);
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		const HintNow& h = hintNow();
		ModuleWidget* mine = dynamic_cast<ModuleWidget*>(getParent());
		if (layer != 1 || !mine)
			return;
		NVGcontext* vg = args.vg;
		const float room = settings::rackBrightness;
		cover(vg, mine);
		if (!h.mw || h.by != mine)
			return;
		std::shared_ptr<Font> font = APP->window->uiFont;
		if (!font)
			return;
		nvgSave(vg);
		nvgResetScissor(vg);
		nvgTranslate(vg, h.mw->box.pos.x - mine->box.pos.x, h.mw->box.pos.y - mine->box.pos.y);
		// On the place's own face, darkened with the room as the plate is.
		h.line.paint(vg, font, h.place.base, dimmed(HINT_WORDS, room),
		             bracketFace(vg), h.place.top,
		             h.place.h, HINT_PAD, h.held());
		nvgRestore(vg);
	}
};
inline void addBracketHint(ModuleWidget* w) {
	BracketHint* r = new BracketHint;
	r->box.size = w->box.size;
	w->addChild(r);
}

} // namespace rig

// The rest of the run, each in a header of its own standing on this one:
// how it is painted, how it is edited, and the engine's writes made as the
// hand's. They define what the bracket and MemberWidget above declare ahead.
#include "Ground.hpp"
#include "Swap.hpp"
#include "Writes.hpp"
