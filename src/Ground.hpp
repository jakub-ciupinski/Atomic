// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Bracket.hpp"

/** The run's picture: every piece of ground its members stand, merged into
shapes across the joins and painted as one with the bracket, cached in a tile per
module, and the ground widgets that lay it under and over each panel. Included
by Bracket.hpp, whose bracket it paints into the picture. */

namespace rig {

/** The static picture of a run: every piece of ground its members declare,
in the run's coordinates, merged into shapes, and a hash that changes when
any of it does. Worked out once a frame for the run, whichever member asks.

Merging: two pieces that are alike (same height, same colour, same corner,
same wish for a line above) and touch end to end are one piece, however
many modules it crosses. Two that meet but differ, a note beside a bare
lane or a short screen beside a tall one, stay two, and the one standing
within the other's height reaches a unit into it, so the column they share
is covered whole rather than half by each with the plate between. Shallower ground goes first, so a roll's
rows lie over the screen they stand on. */
struct RunPicture {
	std::vector<ModuleWidget*> members; // the run, or the one module alone
	float w = 0.f;                      // the run's width
	std::vector<GroundRect> all;        // every piece, for onPlate
	std::vector<GroundRect> shapes;     // merged, to paint
	uint64_t hash = 0;                  // of every piece, every member and where it stands
};
/** One shape of a run's picture: a merged strip of screens painted as one
stock screen, everything else as a rectangle. */
inline void paintShape(NVGcontext* vg, const RunPicture& pic, const GroundRect& s) {
	if (isGlass(s.col, s.radius)) {
		// The face only: the bevel's lit half is the lit layer's, and is laid
		// by `Ground::drawLayer`, as a lone screen lays its own, so a bonded
		// rig's bevels never dim with the room while the same screens
		// standing alone do not.
		paintGlassFace(vg, s.r, s.lip && onPlate(pic.all, s.r), s.r.pos.y);
		return;
	}
	// The shallowest ground stands on the plate, and its edges go on the
	// grid; deeper ground, a roll's rows, keeps the grid it was given.
	int shallowest = s.depth;
	for (const GroundRect& o : pic.all)
		shallowest = std::min(shallowest, o.depth);
	paintGround(vg, s.r, s.radius, s.col, s.lip && onPlate(pic.all, s.r), s.r.pos.y, s.lip && s.depth == shallowest);
}
inline const RunPicture& runPicture(ModuleWidget* me) {
	static std::map<ModuleWidget*, RunPicture> pictures; // by the run's first member
	static FrameId at;
	const FrameId now = frameId();
	if (now != at) {
		at = now;
		pictures.clear();
	}
	const std::vector<ModuleWidget*>* run = runOf(me);
	ModuleWidget* front = run && !run->empty() ? run->front() : me;
	std::map<ModuleWidget*, RunPicture>::iterator it = pictures.find(front);
	if (it != pictures.end())
		return it->second;
	RunPicture& pic = pictures[front];
	if (run && !run->empty())
		pic.members = *run;
	else
		pic.members.push_back(me);
	pic.w = pic.members.back()->box.pos.x + pic.members.back()->box.size.x - front->box.pos.x;
	uint64_t h = 1469598103934665603ull;
	for (ModuleWidget* m : pic.members) {
		const std::vector<GroundRect>& his = groundOf(m);
		const float dx = m->box.pos.x - front->box.pos.x;
		h = hashOf(h, m);
		h = hashOf(h, dx);
		for (GroundRect b : his) {
			b.r.pos.x += dx;
			pic.all.push_back(b);
			h = hashOf(h, b.depth);
			h = hashOf(h, b.r);
			h = hashOf(h, b.radius);
			h = hashOf(h, b.col);
			h = hashOf(h, b.lip);
		}
	}
	// The switches change the picture too.
	const DevSwitches& d = dev();
	const int sw = (d.statics ? 1 : 0) | (d.staticLit ? 2 : 0) | (d.bracket ? 4 : 0) |
	               (d.ground ? 8 : 0);
	h = hashOf(h, sw);
	pic.hash = h;
	std::sort(pic.all.begin(), pic.all.end(), [](const GroundRect& a, const GroundRect& b) {
		if (a.depth != b.depth)
			return a.depth < b.depth;
		return a.r.pos.x != b.r.pos.x ? a.r.pos.x < b.r.pos.x : a.r.pos.y < b.r.pos.y;
	});
	for (const GroundRect& b : pic.all) {
		bool grown = false;
		for (GroundRect& s : pic.shapes) {
			if (s.depth == b.depth && s.like(b) &&
			    std::fabs(b.r.pos.x - (s.r.pos.x + s.r.size.x)) < 0.5f) {
				s.r.size.x = b.r.pos.x + b.r.size.x - s.r.pos.x;
				grown = true;
				break;
			}
		}
		if (!grown)
			pic.shapes.push_back(b);
	}
	// Two shapes that meet but are not alike (an Atn's glass, shorter than
	// the CV's beside it, or a flat screen beside a glass one) each cover
	// part of the pixel column they share, and the plate would show through
	// between two blacks as a pale hairline. So where one stands wholly
	// within the other's height, it reaches a widget unit into the other: the
	// shared column is then covered whole by it, and the other paints over or
	// under it with nothing let through. Between two of the same height only
	// the left one reaches.
	const float BLEED = 1.f;
	// Two that meet where neither stands within the other's height (a CRV's
	// screen beside a Qnt's keys, which open lower and close lower) have no
	// one to reach: a reach would stand out of its neighbour's top or foot as
	// a nub on the plate. Yet down the height they share, the column at the
	// join is still half each with the plate between, an edge that aliased as
	// the rack zoomed. So that
	// band alone gets a filler: the left one's ground, a unit either side of
	// the join, painted under everything of its depth, so the column is
	// covered whole and both faces lie over it. Found before any shape
	// reaches, on the joins as they stand.
	std::vector<GroundRect> fillers;
	for (const GroundRect& s : pic.shapes)
		for (const GroundRect& t : pic.shapes) {
			if (&s == &t || s.depth != t.depth || std::fabs(t.r.pos.x - (s.r.pos.x + s.r.size.x)) >= 0.5f)
				continue;
			const float top = std::max(s.r.pos.y, t.r.pos.y);
			const float foot = std::min(s.r.pos.y + s.r.size.y, t.r.pos.y + t.r.size.y);
			const bool sInT = s.r.pos.y >= t.r.pos.y - 0.01f && s.r.pos.y + s.r.size.y <= t.r.pos.y + t.r.size.y + 0.01f;
			const bool tInS = t.r.pos.y >= s.r.pos.y - 0.01f && t.r.pos.y + t.r.size.y <= s.r.pos.y + s.r.size.y + 0.01f;
			if (foot <= top || sInT || tInS)
				continue;
			GroundRect f = s;
			f.w = nullptr;
			f.r = Rect(Vec(t.r.pos.x - BLEED, top), Vec(2.f * BLEED, foot - top));
			f.lip = false; // both its tops stand over it, so no plate above
			f.under = true;
			fillers.push_back(f);
		}
	for (size_t i = 0; i < pic.shapes.size(); i++)
		for (size_t j = 0; j < pic.shapes.size(); j++) {
			if (i == j)
				continue;
			GroundRect& s = pic.shapes[i];
			const GroundRect& t = pic.shapes[j];
			if (s.depth != t.depth)
				continue;
			const bool within = s.r.pos.y >= t.r.pos.y - 0.01f &&
			                    s.r.pos.y + s.r.size.y <= t.r.pos.y + t.r.size.y + 0.01f;
			const bool same = std::fabs(s.r.pos.y - t.r.pos.y) < 0.01f && std::fabs(s.r.size.y - t.r.size.y) < 0.01f;
			// Not where the line cast above me would reach over the other's
			// face: a taller neighbour's top stands above mine. Only a line
			// that is cast, though: a screen standing on another screen, a
			// Seq's second under its first, casts none, and holding it back
			// for the line it might have cast left the column it shares with
			// a CV beside it half covered by each, with the plate between.
			if (!within || (s.lip && onPlate(pic.all, s.r) && std::fabs(s.r.pos.y - t.r.pos.y) > 0.01f))
				continue;
			if (std::fabs(t.r.pos.x - (s.r.pos.x + s.r.size.x)) < 0.5f)
				s.r.size.x += BLEED; // t on my right
			else if (!same && std::fabs(s.r.pos.x - (t.r.pos.x + t.r.size.x)) < 0.5f) {
				s.r.pos.x -= BLEED; // t on my left, and taller
				s.r.size.x += BLEED;
				s.reachesLeft = true;
			}
		}
	// And a shape that reached into a taller one on its left is painted
	// before everything else of its depth, so the unit it reached lies under
	// that neighbour's face: painted in the order they stand it lay over the
	// taller screen, and a Clock's screens and its RUN bonded right of a CRV
	// stood a unit out of their own column.
	// A shape reaching right is already painted before what it reached.
	// The fillers go under all of it.
	pic.shapes.insert(pic.shapes.end(), fillers.begin(), fillers.end());
	std::stable_sort(pic.shapes.begin(), pic.shapes.end(), [](const GroundRect& a, const GroundRect& b) {
		if (a.depth != b.depth)
			return a.depth < b.depth;
		if (a.under != b.under)
			return a.under;
		return a.reachesLeft && !b.reachesLeft;
	});
	return pic;
}

/** The module's own panel, the one added with addPanel, or nullptr. */
inline Panel* panelOf(ModuleWidget* mw) {
	for (Widget* c : mw->children)
		if (Panel* p = dynamic_cast<Panel*>(c))
			return p;
	return nullptr;
}

/** How far a tile reaches past its module's edges, in widget units: enough
to cover the device pixel column the module shares with its neighbour at any
zoom, so that the column is painted whole by both tiles and not in two
fractions. Rack scissors a module to its box and NanoVG's scissor is soft,
which is why the tile is blitted with the scissor reset: a blit cut at the
module's fractional edge would show a seam. */
const float TILE_REACH = 2.f;

/** One module's slice of the static picture, painted in the module's own
space: the plate of the run where it crosses this module, every shape of the
run's ground that reaches into it, the panel's title and captions, and the
run's bracket where it passes over. Everything here is drawn from the run's
geometry, so where two modules' slices share a pixel column they compute the
same pixels for it, and two tiles set side by side have no seam. Two
widget-units past the module's edges is enough to reach that shared column
at any zoom. */
inline void paintStatic(NVGcontext* vg, ModuleWidget* me, const RunPicture& pic) {
	const float dx = me->box.pos.x - pic.members.front()->box.pos.x; // me in the run
	const float h = me->box.size.y;
	const Rect mine(Vec(dx - TILE_REACH, 0.f), Vec(me->box.size.x + 2.f * TILE_REACH, h));
	nvgSave(vg);
	nvgTranslate(vg, -dx, 0.f);
	// The plate: the run's, so a gradient with no step at the joins.
	const Rect plate = Rect(Vec(0.f, 0.f), Vec(pic.w, h)).intersect(mine);
	if (plate.size.x > 0.f && dev().statics) {
		nvgBeginPath(vg);
		nvgRect(vg, plate.pos.x, plate.pos.y, plate.size.x, plate.size.y);
		nvgFillPaint(vg, plateGradient(vg, h));
		nvgFill(vg);
	}
	// The ground, every shape that reaches in. A merged strip of screens is
	// painted as one stock screen; everything else is a rectangle.
	for (const GroundRect& s : pic.shapes)
		if (s.r.intersects(mine))
			paintShape(vg, pic, s);
	nvgRestore(vg);
	// The face: title and captions, no plate.
	if (Panel* panel = panelOf(me)) {
		Widget::DrawArgs args;
		args.vg = vg;
		args.clipBox = me->box.zeroPos();
		paintingStatic() = true;
		panel->draw(args);
		paintingStatic() = false;
	}
	// The bracket, where it passes over.
	float x0, w;
	if (dev().bracket && rigSpan(me, x0, w))
		paintBracket(vg, x0, w, RACK_GRID_WIDTH, -TILE_REACH, me->box.size.x + TILE_REACH);
}

/** The picture as a widget, for the tile to cache. */
struct Picture : Widget, StaticPart {
	void draw(const DrawArgs& args) override {
		ModuleWidget* me = getAncestorOfType<ModuleWidget>();
		if (!me)
			return;
		// The tile stands TILE_REACH left of the module; the picture is
		// painted in the module's own space.
		nvgSave(args.vg);
		nvgTranslate(args.vg, TILE_REACH, 0.f);
		paintStatic(args.vg, me, runPicture(me));
		nvgRestore(args.vg);
	}
};

/** The tile: a module's slice of the static picture, rendered once into a
framebuffer and blitted every frame after that, until the picture changes.

Rack's own FramebufferWidget does the caching, the way it does for every
stock panel: it renders on the next step after being told the picture is
dirty, re-renders when the zoom or the sub-pixel offset moves, and while a
zoom gesture is under way draws the old image scaled rather than rendering
every frame. What this adds is knowing when the picture is dirty: the hash
of the run's ground, the panel's own version, and the module's place in the
run, compared every step with what was last rendered.

Each module carries its own tile, so there is no painter to elect and no
member to be culled: a module on screen shows its slice, and a module off
screen has nothing to show. Where two slices share a pixel column they were
computed from the same geometry and agree. When a tile has drawn its module
this frame it marks the module, and the panel, the ground parts, the ground
painters and the bracket all stand down; with the tiles switched off none of
that happens and the module is drawn live, the way it was. */
struct Tile : FramebufferWidget, StaticPart {
	Picture* picture = nullptr;
	uint64_t rendered = 0;
	bool everRendered = false;

	ModuleWidget* owner() { return getAncestorOfType<ModuleWidget>(); }

	uint64_t current(ModuleWidget* me) {
		uint64_t h = runPicture(me).hash;
		if (Panel* p = panelOf(me))
			h = hashOf(h, p->version());
		if (darkPanels())
			h = hashOf(h, 1); // the theme is in every colour of the picture
		return h;
	}
	void step() override {
		ModuleWidget* me = owner();
		if (me && dev().tiles) {
			box.pos = Vec(-TILE_REACH, 0.f);
			box.size = Vec(me->box.size.x + 2.f * TILE_REACH, me->box.size.y);
			picture->box.size = box.size;
			const uint64_t h = current(me);
			if (!everRendered || h != rendered) {
				rendered = h;
				everRendered = true;
				setDirty();
			}
		}
		FramebufferWidget::step();
	}
	void draw(const DrawArgs& args) override {
		ModuleWidget* me = owner();
		if (!me || !dev().tiles)
			return;
		markGroundPainted(me, TILE_JOB);
		markGroundPainted(me, PLATE_JOB);
		markGroundPainted(me, PARTS_JOB);
		markGroundPainted(me, BRACKET_JOB);
		nvgSave(args.vg);
		nvgResetScissor(args.vg); // the blit must not be cut at the module's fractional edge
		FramebufferWidget::draw(args);
		nvgRestore(args.vg);
		if (args.fb)
			return; // drawn straight into somebody else's framebuffer
		NVGLUframebuffer* fb = getFramebuffer();
		if (!fb) {
			picture->draw(args); // no framebuffer to be had: live, and no worse than before
			return;
		}
	}
};

inline void addTile(ModuleWidget* w) {
	Tile* tile = new Tile;
	tile->box.pos = Vec(-TILE_REACH, 0.f);
	tile->box.size = Vec(w->box.size.x + 2.f * TILE_REACH, w->box.size.y);
	tile->picture = new Picture;
	tile->picture->box.size = tile->box.size;
	tile->addChild(tile->picture);
	w->addChildBottom(tile); // under everything, the ground painters included
}

/** The ground under a whole rig, painted once, underneath all of it.

Two panels cannot meet cleanly: each covers part of the boundary pixel, the
two parts come to less than one, and what shows through is the rack's
shadow. A rig that shares one piece of ground has no boundary inside it to
meet at. So a run paints its ground as one picture: the plate of the whole
rig as a single rectangle under every panel, and then every ground part of
every member (screens, the rows of a roll) collected in the run's own
coordinates, any two that touch and are alike merged into one wider shape,
and each shape painted exactly once. Thirty-six rows across five Qnts are
thirty-six rectangles, a strip of screens across four modules is one
rectangle with one pair of rounded ends, and there is no pixel anywhere that
two passes have each half-covered.

Who paints is not predicted. Rack draws the module container in the order
modules were added to it, which has nothing to do with where they stand, and
skips a module that has scrolled out of view. So each member offers, and a
register in Parts says whether the job has been done this frame: it falls to
the first member actually drawn, and to nobody twice. The painter marks every
member of the run, and a panel drawn after that puts down only its title, a
ground part drawn after that nothing at all. When the painter has scrolled
away and Rack skips it, nobody is marked, and every panel paints its own
plate and its own ground in the ordinary way: the rig degrades to the faint
hairline it had before any of this, never to a pane of glass.

What this cannot do is anything that differs between members. A CV's fader
band carries that CV's own value, so it is a control, drawn by the module. */
struct Ground : Widget, StaticPart {
	/** Which half of the job this one is. The plate has to go under the
	panel, because the panel paints its title on top of it; the ground parts
	have to go over it, because the panel would otherwise paint over them.
	So a module carries two of these, one on each side of its panel. */
	enum What { PLATE, PARTS };
	What what = PLATE;

	ModuleWidget* owner() {
		ModuleWidget* mine = nullptr;
		for (Widget* p = getParent(); p && !mine; p = p->getParent())
			mine = dynamic_cast<ModuleWidget*>(p);
		return mine;
	}

	/** The box is the run, so Rack keeps drawing this while any part of the
	run is on screen rather than only while this module's corner is. */
	void step() override {
		ModuleWidget* me = owner();
		float x0 = 0.f, w = 1.f;
		if (!me || !rigSpan(me, x0, w)) {
			box.pos = Vec(0.f, 0.f);
			box.size = Vec(1.f, 1.f);
		}
		else {
			box.pos = Vec(x0, 0.f);
			box.size = Vec(w, me->box.size.y);
		}
		Widget::step();
	}

	void draw(const DrawArgs& args) override {
		if (!dev().ground)
			return; // every module paints itself, as if the painter were culled
		ModuleWidget* me = owner();
		const std::vector<ModuleWidget*>* run = me ? runOf(me) : nullptr;
		if (!run || run->size() < 2)
			return;
		if (args.clipBox.size.x <= 0.f || args.clipBox.size.y <= 0.f)
			return; // nothing of us on screen, and nothing of the run either
		const GroundJob job = what == PLATE ? PLATE_JOB : PARTS_JOB;
		if (groundPainted(me, job))
			return; // somebody drawn before us has done it this frame
		const std::vector<ModuleWidget*>& members = *run;
		for (ModuleWidget* m : members)
			markGroundPainted(m, job);

		NVGcontext* vg = args.vg;
		nvgSave(vg);
		nvgResetScissor(vg); // the ground is the run's, not this panel's box
		/* Our own box already starts at the run's left edge (Rack has
		translated us there on the way in) so paint from nought. Translating
		by the offset again shifts the ground left by the whole of it, which
		is invisible while the leftmost member holds the job and a run's width
		of stray plate the moment anyone else does. The one correction left is
		for a box a frame behind the modules, mid-drag: put us back where the
		run is now rather than where step() last saw it. */
		nvgTranslate(vg, (members.front()->box.pos.x - me->box.pos.x) - box.pos.x, 0.f);
		const float w = members.back()->box.pos.x + members.back()->box.size.x -
		                members.front()->box.pos.x;
		const float h = me->box.size.y;
		if (what == PLATE) {
			nvgBeginPath(vg);
			nvgRect(vg, 0.f, 0.f, w, h);
			nvgFillPaint(vg, plateGradient(vg, h));
			nvgFill(vg);
			nvgRestore(vg);
			return;
		}

		// And paint each shape of the run's picture once: see runPicture.
		const RunPicture& pic = runPicture(me);
		for (const GroundRect& s : pic.shapes)
			paintShape(vg, pic, s);
		nvgRestore(vg);
	}
	/** The lit half of the run's glass, once for the run, in the layer that
	stays bright when the room darkens. */
	void drawLayer(const DrawArgs& args, int layer) override {
		Widget::drawLayer(args, layer);
		if (layer != 1 || what != PARTS || !dev().ground)
			return;
		ModuleWidget* me = owner();
		const std::vector<ModuleWidget*>* run = me ? runOf(me) : nullptr;
		if (!run || run->size() < 2 || groundPainted(me, LIT_JOB))
			return;
		const std::vector<ModuleWidget*>& members = *run;
		for (ModuleWidget* m : members)
			markGroundPainted(m, LIT_JOB);
		NVGcontext* vg = args.vg;
		nvgSave(vg);
		nvgResetScissor(vg);
		nvgTranslate(vg, (members.front()->box.pos.x - me->box.pos.x) - box.pos.x, 0.f);
		const RunPicture& pic = runPicture(me);
		// Not a join's filler: its face is all it is for, and a bevel of its
		// own would show as a tick at the join's ends through the faces' own.
		for (const GroundRect& s : pic.shapes)
			if (isGlass(s.col, s.radius) && !s.under)
				paintGlassLit(vg, s.r);
		nvgRestore(vg);
	}
};

/** The ground goes under everything the module draws. */
inline void addGround(ModuleWidget* w) {
	Ground* under = new Ground;
	under->what = Ground::PLATE;
	under->box.size = w->box.size;
	w->addChildBottom(under); // below the panel

	Ground* over = new Ground;
	over->what = Ground::PARTS;
	over->box.size = w->box.size;
	w->addChild(over); // above the panel, below every control
}

/** What every panel of ours has (`MemberWidget::open`): the ground under and
over it, the tile, the border's lines, the screws and the bracket. */
inline void addScrews(ModuleWidget* w) {
	addGround(w);
	addTile(w);
	// The stock border's two lines, as ground: see BorderLine.
	for (int foot = 0; foot < 2; foot++) {
		BorderLine* line = new BorderLine;
		line->foot = foot;
		line->box.pos = Vec(0.f, foot ? w->box.size.y - 1.f : 0.f);
		line->box.size = Vec(w->box.size.x, 1.f);
		w->addChild(line);
	}
	w->addChild(createWidget<TopScrew>(Vec(RACK_GRID_WIDTH, 0)));
	w->addChild(createWidget<AtomScrew>(
	    Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	addBracket(w);
}

} // namespace rig
