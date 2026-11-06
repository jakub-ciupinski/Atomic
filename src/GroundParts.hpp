// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Look.hpp"
#include "Member.hpp"
#include "Rate.hpp"
#include "Stack.hpp"
#include <cstdint>
#include <map>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>
#include <algorithm>

/** Ground and the static layer: what a panel of ours lays flat and
opaque so it runs on into its neighbour with no join, the walks that find a
module's run and its ground from where the panels stand, and the marks that
say what this frame has already painted. Part of Parts.hpp. */

namespace rig {

/** `dynamic_cast<T*>(w)`, for the walks the drawing makes every frame.
Whether a widget can be a T at all is a fact of its type, so it is asked of
the type once and remembered: a widget whose type never can be one costs a
lookup rather than a search of its whole class hierarchy, which on a rack of
ours is most of the time the ground walks take. A widget that can be one is
still cast, so the answer is always the cast's. The drawing's thread only:
the table is nobody else's. */
template <typename T>
inline T* castTo(Widget* w) {
	static std::unordered_map<const std::type_info*, bool> can;
	const std::type_info* t = &typeid(*w);
	std::unordered_map<const std::type_info*, bool>::iterator it = can.find(t);
	if (it == can.end())
		it = can.emplace(t, dynamic_cast<T*>(w) != nullptr).first;
	return it->second ? dynamic_cast<T*>(w) : nullptr;
}

/** Whether the engine has stood still: its frame counter not moved for half a
second (after a restart an Audio module with no device is the master and never
ticks, so every module of ours would look broken, its screens drawing only
what a wake last published). Nothing on the engine side changes; this is the
UI reading the counter. Under the test harness (a dev-only module), which
steps the engine only on a wait, it is whether no wait has stepped it since
the harness froze it, so a
traced frame says the same in every run. Under a test host that is the
engine's master and steps it only when a script or a render says, the host
tells it outright (`test_host_held`), since a wall clock is wrong both ways
there: a test with no wait looks running, a sleep between waits stopped.

It is the one predicate of the unpowered face: while it holds, every
module of ours draws its ground and nothing on it, and the hand meets only
its jacks and the bracket (`MemberWidget`, Bracket.hpp). Here, under the parts,
since a part with a face of its own while unpowered asks it too. */
/** The engine's frame as the test harness froze it (set as it becomes the
engine's master, -1 put back as it lets go), -1 with no harness, so the bracket never asks a module's slug who holds the engine. */
inline int64_t harnessFrozenAt = -1;

/** A hook a test host may set; harmless in a user's Rack, where nothing
writes it and it stays -1, so the engine's frame decides as above. A host that
steps the engine itself writes 1 while a test has not yet run the engine and 0
after that and in renders. The host finds it by name (dlsym), which is why it
keeps the name that host asks for. Defined here rather than in the dev-only
harness, since the release form builds without it and the bracket still asks; an inline
variable is one exported symbol however many files include it. */
extern "C" {
inline int test_host_held = -1;
}

inline bool engineSilent() {
	if (test_host_held != -1)
		return test_host_held == 1;
	engine::Engine* e = APP ? APP->engine : nullptr;
	if (!e)
		return false;
	const int64_t f = e->getFrame();
	if (harnessFrozenAt >= 0)
		return f == harnessFrozenAt;
	static int64_t last = -1;
	static double movedAt = 0.0;
	const double now = frameNow();
	if (f != last) {
		last = f;
		movedAt = now;
		return false;
	}
	return now - movedAt > 0.5;
}

/** A widget drawn and handed the mouse the same with the engine stopped as
running: the bracket's NO AUDIO and its hint line. Everything static that is
no ground keeps its power by being static (the panel, the tile, the
bracket, the screws), and a jack by being a jack; this marks the rest. */
struct Unpowered {
	virtual ~Unpowered() {}
};

/** A control with a face while unpowered, where everything else on the
ground goes: what it draws in its place, in the plain layer, never lit. A
TRG's ring keeps its ring, out. */
struct DarkFace {
	virtual void drawDark(const Widget::DrawArgs& args) = 0;
	virtual ~DarkFace() {}
};

/** The widgets an unpowered module hides for the moment Rack's own handling
of an event takes (`MemberWidget`), which are still standing for the ground's
walk: a list that is empty but for that moment. */
inline std::vector<Widget*>& setAside() {
	static std::vector<Widget*> v;
	return v;
}
inline bool standing(Widget* c) {
	if (c->isVisible())
		return true;
	const std::vector<Widget*>& a = setAside();
	return !a.empty() && std::find(a.begin(), a.end(), c) != a.end();
}

/** How a panel of ours meets the panel beside it.

Two panels side by side cannot meet cleanly. Rack clips every module to its own
box and nanovg's scissor is itself antialiased over a device pixel, so wherever
a boundary falls inside a pixel each panel covers only part of it and what
shows between them is the shadow Rack paints behind the modules. Painting past
the edge cannot reach it: the scissor cuts it off.

So a rig does not meet at all. One member paints the ground of the whole run
(the plate, and the black behind the screens) as single rectangles underneath
everything, and there is then no boundary inside a run for a pixel to be half
of. That is Ground, below.

The one thing a shared ground cannot do is a band carrying each member's own
value: two CVs at different levels really are different colours at the join,
so the join is true and only the pixel is wrong. For that, and only that,
there is coatEdges, which lays the same drawing down again over a strip a
couple of device pixels wide at each edge until the coverage compounds to a
whole pixel. Measured, a fader join reads six levels of 192 with it and
thirty-four without.

Anything drawn that way must be OPAQUE, since laying a translucent thing down
again makes it darker than it should be, and the drawing must be PURE, since
it genuinely runs many times a frame. */
const int EDGE_COATS = 24;    // times an edge strip is laid down again
const float EDGE_STRIP = 2.f; // device pixels of strip at each edge

/** Lay `paint` down again over the edge strips, where a neighbour of ours
stands. See the note above: only for an opaque band whose colour is this
module's own. */
inline ModuleWidget* besideInRun(ModuleWidget* mine, int side);
inline void coatEdges(Widget* w, const Widget::DrawArgs& args,
                      const std::function<void()>& paint) {
	paint();
	ModuleWidget* mw = nullptr;
	for (Widget* p = w->getParent(); p && !mw; p = p->getParent())
		mw = dynamic_cast<ModuleWidget*>(p);
	Module* m = mw ? mw->module : nullptr;
	if (!m || !bondsIn(m))
		return;
	// The scale this frame is drawn at, which is not the rack scroll's zoom
	// while a zoom is under way.
	float xf[6];
	nvgCurrentTransform(args.vg, xf);
	const float scale = std::sqrt(xf[0] * xf[0] + xf[1] * xf[1]);
	if (scale < 0.01f)
		return;
	const float wide = EDGE_STRIP / scale;
	for (int side = 0; side < 2; side++) {
		if (!besideInRun(mw, side))
			continue;
		// The strip reaches past the panel's edge and the module's own scissor
		// cuts it back: a strip stopping exactly on the edge would put its own
		// antialiased edge on the boundary pixel, which is the pixel at issue.
		const float x = side ? w->box.size.x - wide : -wide;
		for (int coat = 1; coat < EDGE_COATS; coat++) {
			nvgSave(args.vg);
			nvgIntersectScissor(args.vg, x, 0.f, 2.f * wide, w->box.size.y);
			paint();
			nvgRestore(args.vg);
		}
	}
}

/** A marker: this widget belongs to the static layer.

The panel is drawn in two layers and every widget is in one of them. Static
is what stands still and is the same every frame (the plate, the ground, the
screens' faces, the captions, the bracket) and is what a tile caches and
blits. Live is everything else: a knob, a bar, a playhead, a readout. Ground
is static by its nature, so GroundPart is one of these; the panel, the run's
ground widget and the tile say so by deriving from it.

Nothing depends on this but the DEV module, which turns a layer off to see
what it costs. It is the one place the layers are named, so a widget that
neither derives from this nor is ground is live, and that is the rule. */
struct StaticPart {
	virtual ~StaticPart() {}
};

/** Ground: the flat, opaque areas a panel is built out of.

A panel is two kinds of thing. There is ground (the plate, the black screens,
the slider wells): flat, opaque, unchanging, and the part of the face that has
to run on into the neighbour without a join. And there are controls (knobs,
jacks, digits, lit bars): things that sit on the ground and belong to one module
alone. A widget says which it is by deriving from this, and everything else
follows from that: the run's shared ground paints every ground part of every
member underneath them all, merging the ones that touch into one shape, so a
strip of screens crossing four modules is one rectangle with one pair of rounded
ends, and there is no boundary pixel inside it to go wrong.

The rule for a ground part is the rule the shared ground needs: it paints a
rectangle of one colour, with the same corners, wherever it is asked to. If a
widget cannot say what it covers without looking at its module's state, it is
a control, not ground. */
struct GroundPart : StaticPart {
	/** Whether it is ground at all just now. Almost everything says yes and
	means it; a widget that is ground in one of its modes and a control in
	another (a Qnt's key, flat and full width as a roll and a lettered cap
	as a keyboard) says so here rather than by being two classes. */
	virtual bool isGround() {
		return true;
	}
	/** The corner, in pixels: nought is square, the way our screens are. */
	virtual float groundRadius() {
		return 0.f;
	}
	virtual NVGcolor groundColor() {
		return SCREEN_FACE;
	}
	/** Does it cast the softening line on the plate above it? Everything
	standing on the plate does, glass and flat alike, and ground laid on other
	ground is ruled out by its depth rather than by saying so, so the only
	widget that has to answer this at all is one that stands on the plate and
	still wants no line, which is the panel's own border, and nothing else. A
	screen cannot rely on the line a stock screen draws for itself: the run's
	scissor cuts it off, so a screen would have the line alone and lose it the
	moment it was bonded to a neighbour. One line, from `paintGroundShadow`, for
	every piece of ground. */
	virtual bool groundLip() {
		return true;
	}
};

/** One piece of ground, wherever it has been found: in a module's own space
when it was collected from a module, in the run's when from a run. */
struct GroundRect {
	Widget* w = nullptr; // who it came from, so a part can tell itself apart
	int depth = 0;    // how far down the tree: ground laid on ground is deeper
	Rect r;
	float radius = 0.f;
	NVGcolor col;
	bool lip = false; // does it want the softening line drawn for it?
	bool reachesLeft = false; // reached a unit into a taller neighbour on its left, so painted under it
	bool under = false; // a join's filler, painted under everything of its depth (runPicture)
	bool like(const GroundRect& o) const {
		return r.pos.y == o.r.pos.y && r.size.y == o.r.size.y && radius == o.radius &&
		       lip == o.lip && col.r == o.col.r && col.g == o.col.g && col.b == o.col.b &&
		       col.a == o.col.a;
	}
};

/** Every piece of ground under a widget, in that widget's own coordinates,
outermost first. Ground can be laid on ground (the rows of a Qnt's roll on the
screen they are drawn on), so the walk goes all the way down; what is not ground
is a control, and controls are nobody else's business. */
inline void collectGround(Widget* w, Vec at, std::vector<GroundRect>& out, int depth = 0) {
	for (Widget* c : w->children) {
		if (!standing(c))
			continue;
		const Vec pos = at.plus(c->box.pos);
		GroundPart* g = castTo<GroundPart>(c);
		if (g && g->isGround()) {
			GroundRect b;
			b.w = c;
			b.depth = depth;
			b.r = Rect(pos, c->box.size);
			b.radius = g->groundRadius();
			b.col = g->groundColor();
			// Only ground standing on the plate can cast a line on it, so
			// depth answers it for everything laid on other ground (a
			// roll's rows on the screen they are drawn on) and no such
			// widget has to remember to say so for itself.
			b.lip = depth == 0 && g->groundLip();
			out.push_back(b);
		}
		// And on down: a piece of ground can be laid on another piece of
		// ground (a Qnt's roll of keys inside the screen it is drawn on)
		// and the one on top is the one a neighbour has to meet.
		collectGround(c, pos, out, depth + 1);
	}
}

/** Whether the run a widget stands in has painted this frame, and which
part of the job: the plate under every panel, the ground parts over them, or
the bracket across the top.

The run's painter, whichever member Rack happens to draw first, paints the whole
rig and marks every member here. A panel drawn after that puts down its title
and no plate; a ground part drawn after that paints nothing, since its rectangle
is already on the screen as part of one wider shape. And when nobody has marked
a module this frame (its run's painter scrolled out of view and Rack never drew
it), the panel and the parts paint themselves alone. Nothing is predicted: a
member finds out by looking. */
enum GroundJob { PLATE_JOB, PARTS_JOB, BRACKET_JOB, TILE_JOB, LIT_JOB };
/** True while the static picture is being painted into a tile: a panel
asked to draw then puts down its title and captions and no plate, whatever
the marks say, because the tile has the plate already. */
inline bool& paintingStatic() {
	static bool on = false;
	return on;
}
/** A hash for telling whether a picture has changed: FNV-1a over bytes. */
inline uint64_t hashBytes(uint64_t h, const void* p, size_t n) {
	const unsigned char* b = (const unsigned char*) p;
	for (size_t i = 0; i < n; i++) {
		h ^= b[i];
		h *= 1099511628211ull;
	}
	return h;
}
template <typename T>
inline uint64_t hashOf(uint64_t h, const T& v) {
	return hashBytes(h, &v, sizeof v);
}
inline std::map<std::pair<const Widget*, int>, double>& groundMarks() {
	static std::map<std::pair<const Widget*, int>, double> marks;
	return marks;
}
/** Which frame this is, for everything worked out once a frame: the run a
module stands in, the run's picture, the marks. Rack's frame time, and a
count that a renderer outside Rack's loop bumps, since to Rack everything it
draws is one frame and a picture cached there would never be looked at
again, and a render outside Rack's loop would show a roll with no rows. */
struct FrameId {
	double time = -1.0;
	uint64_t bump = 0;
	bool operator!=(const FrameId& o) const {
		return bump != o.bump || time != o.time || std::isnan(time) || std::isnan(o.time);
	}
};
inline uint64_t& frameBumps() {
	static uint64_t bumps = 0;
	return bumps;
}
inline FrameId frameId() {
	FrameId f;
	f.time = APP->window ? APP->window->getFrameTime() : 0.0;
	f.bump = frameBumps();
	return f;
}
inline double groundFrame() {
	return frameId().time + (double)frameBumps() * 1e-9;
}
inline void markGroundPainted(const Widget* module, GroundJob job) {
	std::map<std::pair<const Widget*, int>, double>& marks = groundMarks();
	const double now = groundFrame();
	// A mark is only ever about this frame, and the modules it is keyed on
	// come and go: a patch closed leaves its entries behind for good. So when
	// the map has grown past any believable rack, everything not marked this
	// frame goes: those widgets have either been drawn again since or are
	// gone. Nothing is read across frames, so nothing is lost by it.
	if (marks.size() > 1024) {
		for (std::map<std::pair<const Widget*, int>, double>::iterator it = marks.begin();
		     it != marks.end();)
			if (it->second == now)
				++it;
			else
				marks.erase(it++);
	}
	marks[std::make_pair(module, (int)job)] = now;
}
/** A new frame, as far as we are concerned: every mark forgotten and every
once-a-frame answer worked out afresh. For a renderer that draws several
pictures in what Rack counts as one frame. */
inline void resetGroundMarks() {
	groundMarks().clear();
	frameBumps()++;
}
/** Has the run painted this `job` for the module `me` is part of, this
frame? */
inline bool groundPainted(Widget* me, GroundJob job) {
	const Widget* owner = nullptr;
	for (Widget* p = me; p; p = p->getParent())
		if (castTo<ModuleWidget>(p)) {
			owner = p;
			break;
		}
	if (!owner)
		return false;
	std::map<std::pair<const Widget*, int>, double>& marks = groundMarks();
	std::map<std::pair<const Widget*, int>, double>::iterator it =
	    marks.find(std::make_pair(owner, (int)job));
	return it != marks.end() && it->second == groundFrame();
}

/** A panel, the face of a module and static: the plate, the title, the
captions, the decoration. Everything a panel draws goes into the module's tile
and is drawn once, until `version` changes: a panel whose face depends on the
module's state (a caption that reads DIV CV while a clock is followed) returns a
number that changes when the face does, and nothing else. A panel that reads
state and forgets to say so keeps its old face until something else moves the
tile, which is the bug to look for when a caption is stale. */
struct Panel : Widget, StaticPart {
	virtual uint64_t version() {
		return 0;
	}
};

/** How long a mode's word holds in a plate's title after the mode changes,
and then takes to fade back into the title, in seconds. */
const float MODE_HOLD = 1.2f, MODE_FADE = 1.f;
/** A title that says a module's mode for a moment after it changes, however
it changed, an arrow, a stroke or an undo: the mode's word held `MODE_HOLD`
and faded back into the title over `MODE_FADE`, the Qnt's and the Clock's alike. The weight goes in steps,
so the tile is drawn again only as often as the fade shows a change; a
panel puts `stamp` in its `version`. */
struct ModeFlash {
	int modeWas = INT_MIN; // none yet: the first mode seen is no change
	double changedAt = -1e9;
	const char* word = nullptr;
	/** The frame's own stamp, not the wall clock, so the hold and the fade
	count frames: in a live Rack the two agree to within a frame, and in a
	test host whose frames keep a virtual time, a capture some frames after a
	change draws the same face however loaded the machine is. */
	static double now() { return frameNow(); }
	void see(int mode, const char* w) {
		if (modeWas != INT_MIN && mode != modeWas) {
			word = w;
			changedAt = now();
		}
		modeWas = mode;
	}
	float weight() const {
		const float t = (float)(now() - changedAt);
		const float w = t < MODE_HOLD ? 1.f : 1.f - (t - MODE_HOLD) / MODE_FADE;
		return w <= 0.f ? 0.f : std::ceil(w * 32.f) / 32.f;
	}
	uint64_t stamp() const { return (uint64_t)(weight() * 32.f) * 8 + (uint64_t)(modeWas + 2); }
};

/** A panel's plate, from the panel. Into a tile: the title only, since the
tile has the plate. Live, with the tile having drawn this panel already this
frame: nothing. Live otherwise: the whole thing, unless the run has painted
the plate of the whole rig, in which case only the title goes down. */
inline void drawPlate(const Widget::DrawArgs& args, Widget* panel, const char* title,
                      float bandY = -1.f, float bandH = 0.f, const char* over = nullptr, float weight = 0.f) {
	if (paintingStatic()) {
		drawPlate(args.vg, panel->box.size, title, bandY, bandH, false, over, weight);
		return;
	}
	if (groundPainted(panel, TILE_JOB))
		return;
	drawPlate(args.vg, panel->box.size, title, bandY, bandH, !groundPainted(panel, PLATE_JOB), over, weight);
}

/** Is the plate what lies directly above this rectangle, or another piece of
ground? Only the first wants the softening line: a line drawn where two dark
areas meet is a bright scratch across them. */
inline bool onPlate(const std::vector<GroundRect>& all, Rect mine) {
	for (const GroundRect& o : all) {
		// Within a pixel and a half, not half a pixel: ground laid in rows
		// leaves a pixel of the screen between one row and the next as its
		// grid, and a row is still standing on the row below it.
		if (std::fabs(o.r.pos.y + o.r.size.y - mine.pos.y) > 1.5f)
			continue;
		if (o.r.pos.x < mine.pos.x + mine.size.x - 0.5f &&
		    mine.pos.x < o.r.pos.x + o.r.size.x - 0.5f)
			return false;
	}
	return true;
}

/** The owner of a widget (the module widget it is somewhere inside) and
where the widget sits in that module's own space. */
inline ModuleWidget* ownerOf(Widget* me, Vec& at) {
	at = Vec(0.f, 0.f);
	for (Widget* p = me; p; p = p->getParent()) {
		if (ModuleWidget* mw = castTo<ModuleWidget>(p))
			return mw;
		at = at.plus(p->box.pos);
	}
	return nullptr;
}

/** One module's ground, worked out once a frame however many widgets ask for
it. Two widgets in three ask a question about the ground around them before
they paint (is the plate what lies above me, is another piece of ground
already covering me) and walking the module's whole widget tree for each, on
a Qnt with a thirty-six row roll, is the same walk done some seventy times a
frame, each walk itself proportional to the number of rows: the cost of a
panel went up with the square of what was on it. The list is the same list
every time, so it is made once and kept for the frame, keyed by the module it
belongs to and thrown away whole when the frame moves. Nothing is read across
frames, so a widget that has gone takes nothing with it. */
inline const std::vector<GroundRect>& groundOf(ModuleWidget* owner) {
	static std::map<Widget*, std::vector<GroundRect> > byOwner;
	static FrameId at;
	const FrameId now = frameId();
	if (now != at) {
		at = now;
		byOwner.clear();
	}
	std::map<Widget*, std::vector<GroundRect> >::iterator it = byOwner.find(owner);
	if (it != byOwner.end())
		return it->second;
	std::vector<GroundRect>& all = byOwner[owner];
	collectGround(owner, Vec(0.f, 0.f), all);
	return all;
}

/** Where the undo history stands: how far it is done, how long it is, and
its last step. A step's address alone does not name it, since a step undone
and dropped by the next push is freed, and the next step pushed may be given
the same address; a key taken when a step was pushed matches the history
only while nothing has been undone, redone or pushed since that it could
notice. Empty, `back` null, for no step at all. */
struct StepKey {
	int index = 0;
	size_t size = 0;
	const history::Action* back = nullptr;
	/** The history as it stands now. */
	static StepKey now() {
		const history::State* h = APP->history;
		StepKey k;
		if (h) {
			k.index = h->actionIndex;
			k.size = h->actions.size();
			k.back = h->actions.empty() ? nullptr : h->actions.back();
		}
		return k;
	}
	/** The key of `a` if it is the step last done, as just pushed, else empty. */
	static StepKey of(const history::Action* a) {
		const StepKey k = now();
		return a && k.back == a && k.index == (int)k.size ? k : StepKey();
	}
	bool empty() const { return !back; }
	bool operator==(const StepKey& o) const { return index == o.index && size == o.size && back == o.back; }
	bool operator!=(const StepKey& o) const { return !(*this == o); }
};

/** The run a module stands in: everything of ours shoulder to shoulder with
it, on the same row, left to right.

Worked out from where the modules actually stand, not from the expander links.
The two disagree for a frame whenever a drag is being placed (a module is linked
before it has been put where the link says) and anything drawn across the run
from that disagreement flickers, because two members answer the question
differently in the same frame. Every member of a run gets the same answer from
this, always, because it is the same sort over the same positions. Nor from the
expander links for any other purpose: the engine writes those on the audio
thread and only while it steps, so a stopped engine (an Audio module with no
device) leaves a fader sweep that walks them unable to find its rig. This is the
one definition of the rig for everything the hand and the drawing do, the rule
of who stands in it being `bondsIn` for the drawing, a member or a bonder (a RIG
or a GAP) and not bypassed, and `inRig` for the hand, which is the engine's. A
RIG always starts a run, whatever stands at its left: it is the boundary of the
molecule it holds, and nothing bonds to its left. A GAP bonds on both sides, so
the run goes on across it (`Bonder::bondsLeft`).

The answer is kept for the frame it was worked out in, so a rig of twenty
modules sorts the rack once a frame rather than twenty times, and for no
longer than the history stands still: an undo, a redo or a step pushed moves
modules in the middle of a frame (an undo, Swap's verbs), and a
widget stepping after it would otherwise read the rig as it stood before, a
Qnt undone out of a block still counted in it (`QntWidget::keepOneMode`).

`asEngine` asks for the hand's run rather than the drawing's: the engine's
rig, its members alone, so it parts at a RIG or a GAP where the drawing runs
on. Both stop at a bypassed module, which stands in no run at all: a bypass
splits the rig hard (`inRig`). */
inline const std::vector<ModuleWidget*>* runOf(ModuleWidget* mine, bool asEngine = false) {
	static std::vector<std::vector<ModuleWidget*>> byKind[2];
	static FrameId at[2];
	static StepKey seen[2];
	std::vector<std::vector<ModuleWidget*>>& runs = byKind[asEngine ? 1 : 0];
	const FrameId now = frameId();
	const StepKey moved = StepKey::now();
	if (now != at[asEngine ? 1 : 0] || moved != seen[asEngine ? 1 : 0]) {
		at[asEngine ? 1 : 0] = now;
		seen[asEngine ? 1 : 0] = moved;
		runs.clear();
		std::vector<ModuleWidget*> ours;
		Widget* container = APP->scene->rack->getModuleContainer();
		if (container)
			for (Widget* c : container->children) {
				ModuleWidget* mw = dynamic_cast<ModuleWidget*>(c);
				if (mw && mw->module && (asEngine ? inRig(mw->module) : bondsIn(mw->module)))
					ours.push_back(mw);
			}
		std::sort(ours.begin(), ours.end(), [](ModuleWidget* a, ModuleWidget* b) {
			return a->box.pos.y != b->box.pos.y ? a->box.pos.y < b->box.pos.y
			                                    : a->box.pos.x < b->box.pos.x;
		});
		for (size_t i = 0; i < ours.size();) {
			size_t j = i + 1;
			while (j < ours.size() && ours[j]->box.pos.y == ours[i]->box.pos.y &&
			       std::fabs(ours[j]->box.pos.x -
			                 (ours[j - 1]->box.pos.x + ours[j - 1]->box.size.x)) < 0.5f &&
			       // A RIG starts its molecule: a bonder says whether it
			       // bonds to its left.
			       bondsToLeft(ours[j]->module))
				j++;
			runs.push_back(std::vector<ModuleWidget*>(ours.begin() + i, ours.begin() + j));
			i = j;
		}
	}
	for (const std::vector<ModuleWidget*>& run : runs)
		if (std::find(run.begin(), run.end(), mine) != run.end())
			return &run;
	return nullptr;
}

/** A RIG's molecule: every module of ours bonded to the right of `mine`, as
the engine runs it, across a GAP, a bypassed module or anything else ending
a run,
the GAPs in it. Worked out from where the panels stand, as `runOf` is, but
walked on past where a run parts. Moved from RigWidget as it stood,
its half-pixel allowance between two panels' edges with it. */
inline std::vector<ModuleWidget*> moleculeOf(ModuleWidget* mine) {
	std::vector<ModuleWidget*> out;
	if (!APP->scene || !APP->scene->rack)
		return out;
	std::vector<ModuleWidget*> ours;
	for (ModuleWidget* o : APP->scene->rack->getModules())
		if (o != mine && o->module && (isFamily(o->module) || isBonder(o->module)) &&
		    o->box.pos.y == mine->box.pos.y && o->box.pos.x > mine->box.pos.x)
			ours.push_back(o);
	std::sort(ours.begin(), ours.end(),
	          [](ModuleWidget* a, ModuleWidget* b) { return a->box.pos.x < b->box.pos.x; });
	float edge = mine->box.pos.x + mine->box.size.x;
	for (ModuleWidget* o : ours) {
		Module* m = o->module;
		if (std::fabs(o->box.pos.x - edge) >= 0.5f || m->isBypassed() || (!isFamily(m) && !bondsToLeft(m)))
			break;
		out.push_back(o);
		edge = o->box.pos.x + o->box.size.x;
	}
	return out;
}

/** The module standing beside one in its run, left (side 0) or right (side
1), or null at the run's end. */
inline ModuleWidget* besideInRun(ModuleWidget* mine, int side) {
	const std::vector<ModuleWidget*>* run = runOf(mine);
	if (!run)
		return nullptr;
	const std::vector<ModuleWidget*>::const_iterator it = std::find(run->begin(), run->end(), mine);
	if (side == 0)
		return it == run->begin() ? nullptr : *(it - 1);
	return it + 1 == run->end() ? nullptr : *(it + 1);
}

/** The modules of kind T touching `me` in its run, it among them, left to
right, as the engine has the block: walked along the hand's run (`runOf`),
never the expander links a stopped engine never forms, which ends at a
bypassed module as the engine's rig does. `me` alone when it stands in no
run. A CRV's block and a Trg's are this one walk. */
template <class T>
inline std::vector<T*> touchingOf(ModuleWidget* mine, T* me) {
	std::vector<Module*> mods;
	if (const std::vector<ModuleWidget*>* run = mine ? runOf(mine, true) : nullptr)
		for (ModuleWidget* x : *run)
			mods.push_back(x->module);
	const int i = (int)(std::find(mods.begin(), mods.end(), (Module*)me) - mods.begin());
	if (i >= (int)mods.size())
		return std::vector<T*>(1, me);
	int lo = i, hi = i;
	while (lo > 0 && dynamic_cast<T*>(mods[lo - 1]))
		lo--;
	while (hi + 1 < (int)mods.size() && dynamic_cast<T*>(mods[hi + 1]))
		hi++;
	std::vector<T*> out;
	for (int k = lo; k <= hi; k++)
		out.push_back(static_cast<T*>(mods[k]));
	return out;
}

/** Whether another module of kind T stands anywhere in `me`'s run, none
for a bypassed `me`, which stands in none: what a sweep can reach
(`LevelParam::peersBetween`), for its hint piece. */
template <class T>
inline bool sharesRunOf(ModuleWidget* mine, T* me) {
	const std::vector<ModuleWidget*>* run = mine && me ? runOf(mine, true) : nullptr;
	if (run)
		for (ModuleWidget* x : *run)
			if (x->module && x->module != me && dynamic_cast<T*>(x->module))
				return true;
	return false;
}

/** The same question asked by a widget about itself: what is above me in the
module I am part of? */
inline bool onPlate(Widget* me) {
	if (!dev().walks)
		return true;
	Vec at;
	ModuleWidget* owner = ownerOf(me, at);
	if (!owner || owner == me)
		return true;
	return onPlate(groundOf(owner), Rect(at, me->box.size));
}

/** Is another piece of ground already covering me?

A screen can be the whole of another screen (a Trg's button is its rings'
black area, exactly) and then only one of the two may paint it. The stock
screen's softening line is a translucent black, and a translucent line laid
down twice is twice as dark: that is why a Trg's lip read 135 where every
other module's read 179. So a screen asks this before painting its plate,
and the one underneath does the painting. */
inline bool overGround(Widget* me, Rect* cover = nullptr, Rect* where = nullptr) {
	if (!dev().walks)
		return false;
	Vec at;
	ModuleWidget* owner = ownerOf(me, at);
	if (!owner || owner == me)
		return false;
	const std::vector<GroundRect>& all = groundOf(owner);
	const Rect mine(at, me->box.size);
	for (const GroundRect& o : all) {
		if (o.w == me)
			continue;
		if (o.r.pos.x - 0.5f <= mine.pos.x && o.r.pos.y - 0.5f <= mine.pos.y &&
		    o.r.pos.x + o.r.size.x + 0.5f >= mine.pos.x + mine.size.x &&
		    o.r.pos.y + o.r.size.y + 0.5f >= mine.pos.y + mine.size.y) {
			if (cover)
				*cover = o.r;
			if (where)
				*where = mine;
			return true;
		}
	}
	return false;
}


/** One painter for a lone piece of ground, exactly as the run paints a strip
of one (`paintShape` in Bracket.hpp): in the plain layer its face, glass or flat,
with the line above it where it stands on the plate and its top edge on the
grid where it is the shallowest ground; in the lit layer the bevel's lit half,
glass only. Nothing where the run has painted it already, where it is not
ground just now, or where another piece of ground covers it whole (a Trg's
button is its rings' screen). Every ground widget's `draw` and `drawLayer` is
this call and nothing else, so a lone panel and a bonded one cannot differ one
widget at a time. */
inline void paintGroundPart(Widget* me, GroundPart* g, const Widget::DrawArgs& args, int layer) {
	if (!g->isGround() || groundPainted(me, PARTS_JOB))
		return;
	const Rect r(Vec(0.f, 0.f), me->box.size);
	const bool glass = isGlass(g->groundColor(), g->groundRadius());
	if (layer == 1) {
		if (glass)
			paintGlassLit(args.vg, r);
		return;
	}
	if (layer != 0)
		return;
	// How deep it lies, as `collectGround` counts it: ground laid on other
	// ground casts no line and keeps the grid it was given.
	int depth = 0;
	for (Widget* p = me->getParent(); p; p = p->getParent()) {
		GroundPart* pg = castTo<GroundPart>(p);
		if (pg && pg->isGround())
			depth++;
	}
	if (depth == 0 && overGround(me))
		return;
	Vec at;
	ownerOf(me, at);
	const bool lip = depth == 0 && g->groundLip();
	const bool shadow = lip && onPlate(me);
	if (glass)
		paintGlassFace(args.vg, r, shadow, at.y);
	else
		paintGround(args.vg, r, g->groundRadius(), g->groundColor(), shadow, at.y, lip);
}

/** A screen drawn by us rather than by Rack, and the same screen for all
that: the same colour, the same corner and the same face, so a flat one and a
stock one standing side by side are one surface and not two blacks. Ground
that is not alike cannot merge, so two blacks would never become one shape and
would meet on two antialiased edges instead, a visible join that moves as the
rack zooms.

It holds on to a left press nothing on it took, between a Qnt's keys, on a
margin, in a playhead's strip, so a hand on the screen never picks the module
up: the plate is where a module is moved from, the glass is the instrument.
The CV's and the Atn's faders cover the whole glass and hold a press already.
*/
struct FlatScreen : Widget, GroundPart {
	void onButton(const ButtonEvent& e) override {
		Widget::onButton(e);
		if (!e.isConsumed() && e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS)
			e.consume(this);
	}
	NVGcolor groundColor() override { return GLASS_RIM; }
	float groundRadius() override { return GLASS_RADIUS; }
	bool groundLip() override { return true; }
	void draw(const DrawArgs& args) override {
		paintGroundPart(this, this, args, 0);
		Widget::draw(args);
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		paintGroundPart(this, this, args, layer);
		Widget::drawLayer(args, layer);
	}
};

/** The stock border's top or bottom line, as a piece of ground: one pixel
of grey the width of the panel, flat and opaque, joining its neighbour's so
a run paints one line along its whole length, the way it paints a strip of
screens. It is ground and nothing more, so it needs no register of its own:
the run's painter lays it down with everything else, and the bracket, which is
painted after, starts a pixel down and leaves it showing. */
struct BorderLine : Widget, GroundPart {
	bool foot = false;
	NVGcolor groundColor() override {
		return foot ? BORDER_FOOT : BORDER_TOP;
	}
	bool groundLip() override {
		return false;
	}
	void draw(const DrawArgs& args) override {
		paintGroundPart(this, this, args, 0);
		Widget::draw(args);
	}
};

} // namespace rig
