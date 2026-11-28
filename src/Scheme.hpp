// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/** The colour scheme as the UI reads it, a cable's colour on a jack, a
screen's modulation ink, the UI's half of a Stack, a hovered row's cable lit
and what a bonded module is in the scheme. A part of Hand.hpp in a file of
its own: Hand.hpp includes it once, inside its namespace rig, and it is not a
header to include anywhere else. */

/** One cable in an input, as the frame's list has it: the jack it goes
into, its colour packed RGBA, and where it comes from, the module's id and
its output, with the cable's own id. `cabled` says the engine has the cable
and `sourced` that it has a module at its other end too, which is all a
stack counts. */
struct CableIn {
	Module* module;
	int input;
	uint32_t rgb;
	bool cabled, sourced;
	int64_t id;
	int64_t from;
	int out;
	bool operator<(const CableIn& o) const { return module != o.module ? std::less<Module*>()(module, o.module) : input < o.input; }
};
/** The cables in one input, first to last as the rack holds them, which is
the order they were plugged. */
struct CablesIn {
	const CableIn* first = nullptr;
	const CableIn* last = nullptr;
	const CableIn* begin() const { return first; }
	const CableIn* end() const { return last; }
};
/** Every complete cable into a jack of `m`, from the one list of the
patch's cables made once a frame and sorted by the input: asked of the rack
for one jack (`getCompleteCablesOnPort`) it is a look through every cable of
the patch, and every jack of ours that wears its cable's colour and every
gatherer listing its stack asks every frame, so the asking grew as the
modules times the cables. The list holds what a cable said and no cable, so
nothing in it can be left pointing at one that has gone; it is made again
when the frame moves, the history moves or the rack's cables are not the
ones it was made from. Only the UI knows a cable, so a widget asks in its
step and hands what it reads to the module. */
inline CablesIn cablesIn(Module* m, int input) {
	if (!m || !APP || !APP->scene || !APP->scene->rack)
		return CablesIn();
	static std::vector<CableIn> ins;
	static FrameId at;
	static StepKey seen;
	static size_t count = 0;
	static const Widget* last = nullptr;
	const std::list<Widget*>& cables = APP->scene->rack->getCableContainer()->children;
	const FrameId now = frameId();
	const StepKey moved = StepKey::now();
	const Widget* back = cables.empty() ? nullptr : cables.back();
	if (now != at || moved != seen || cables.size() != count || back != last) {
		at = now;
		seen = moved;
		count = cables.size();
		last = back;
		ins.clear();
		for (Widget* w : cables) {
			CableWidget* cw = dynamic_cast<CableWidget*>(w);
			if (!cw || !cw->isComplete() || !cw->inputPort->module)
				continue;
			const engine::Cable* c = cw->cable;
			const bool sourced = c && c->outputModule;
			ins.push_back({cw->inputPort->module, cw->inputPort->portId, packRGB(cw->color), c != nullptr, sourced, c ? c->id : -1,
			               sourced ? c->outputModule->id : -1, c ? c->outputId : 0});
		}
		// The rack's order kept within an input: a stable sort.
		std::stable_sort(ins.begin(), ins.end());
	}
	const CableIn key = {m, input, 0, false, false, -1, -1, 0};
	const std::pair<std::vector<CableIn>::const_iterator, std::vector<CableIn>::const_iterator> range =
	    std::equal_range(ins.begin(), ins.end(), key);
	CablesIn of;
	of.first = ins.data() + (range.first - ins.begin());
	of.last = ins.data() + (range.second - ins.begin());
	return of;
}
/** The colour of the first cable on a jack, packed RGBA, nought for none. A
cable's colour is ours to wear wherever a module shows what arrived through
it. */
inline uint32_t cableColorIn(Module* m, int input) {
	const CablesIn in = cablesIn(m, input);
	return in.first != in.last ? in.first->rgb : 0;
}
/** The ink of a screen's modulation line, the bar along its foot: the colour
of the cable bringing what it draws, packed as `cableColorIn` gives it, else
the screen's own ink, and the screen's own under the DEV's OLD. */
inline NVGcolor modColor(uint32_t rgb, NVGcolor own) {
	return rgb && !dev().oldColors ? fromRGB(rgb) : own;
}
/** The cables stacked in an input as a `Stack` counts them: in the order
the rack holds them, at most `Stack::MAX`, each from a module (`sourced`).
The one list both the engine's count of a row's cable (`publishStack`) and
the hover's glow on it (`glowCable`) are read against, so the two never
count differently. */
struct Stacked {
	const CableIn* at[Stack::MAX];
	int n = 0;
};
inline Stacked stackCables(Module* m, int input) {
	Stacked kept;
	for (const CableIn& c : cablesIn(m, input))
		if (kept.n < Stack::MAX && c.sourced)
			kept.at[kept.n++] = &c;
	return kept;
}
/** The UI's half of a `Stack` (Stack.hpp): the cables now in the input
(`stackCables`), listed for the engine under the stack's sequence count.
True when the list changed, for the owner to ask for a wake, since the
input's sum may not have moved. */
inline bool publishStack(Stack& s, Module* m, int input) {
	if (!m || !APP || !APP->scene || !APP->scene->rack)
		return false;
	const Stacked cables = stackCables(m, input);
	const int k = cables.n;
	// Nothing moved, nothing written: the list is compared first so a
	// still patch costs the engine no retake.
	bool same = k == s.n.load(std::memory_order_relaxed);
	for (int i = 0; same && i < k; i++) {
		const CableIn* c = cables.at[i];
		same = s.ids[i].load(std::memory_order_relaxed) == c->from &&
		       s.outs[i].load(std::memory_order_relaxed) == c->out &&
		       s.rgbs[i].load(std::memory_order_relaxed) == c->rgb;
	}
	if (same)
		return false;
	Writing<uint32_t> whole(s.seq);
	for (int i = 0; i < k; i++) {
		const CableIn* c = cables.at[i];
		s.ids[i].store(c->from, std::memory_order_relaxed);
		s.outs[i].store(c->out, std::memory_order_relaxed);
		s.rgbs[i].store(c->rgb, std::memory_order_relaxed);
	}
	s.n.store(k, std::memory_order_relaxed);
	return true;
}

/** The cables lit this frame, by id: a hovered row of a gatherer names the
cable it came in by, from its own drawing, which comes before the rack's
cables in the same frame, and the glow over the cables draws each one found
still there. Only the id is kept, so a cable taken away is simply not found. */
struct CableGlowAsked {
	std::vector<int64_t> ids;
	double at = -1.0;
};
inline CableGlowAsked& cableGlowAsked() {
	static CableGlowAsked a;
	return a;
}
inline void glowCable(int64_t id) {
	if (!APP || !APP->window)
		return;
	CableGlowAsked& a = cableGlowAsked();
	const double now = APP->window->getFrameTime();
	if (a.at != now) {
		a.ids.clear();
		a.at = now;
	}
	if (std::find(a.ids.begin(), a.ids.end(), id) == a.ids.end())
		a.ids.push_back(id);
}
/** A cable drawn as Rack draws one whose jack is hovered, whole and opaque,
and its colour never touched, the colour being the patch's. Rack's own
geometry, which it keeps private: the ends drawn 14 px in towards the slump,
the slump lowered by the rack's tension with the cable's length, an outline
and a body a pixel thinner, nine pixels thick for a polyphonic cable and six
for one channel. */
inline void paintCableOpaque(NVGcontext* vg, CableWidget* cw) {
	const engine::Cable* c = cw->cable;
	const bool thick = c->outputModule->outputs[c->outputId].isPolyphonic();
	const float thickness = thick ? 9.f : 6.f;
	Vec out = cw->getOutputPos(), in = cw->getInputPos();
	Vec slump = out.plus(in).div(2.f);
	slump.y += (1.f - settings::cableTension) * (150.f + out.minus(in).norm());
	out = out.plus(slump.minus(out).normalize().mult(14.f));
	in = in.plus(slump.minus(in).normalize().mult(14.f));
	nvgSave(vg);
	nvgAlpha(vg, 1.f);
	nvgLineCap(vg, NVG_ROUND);
	nvgLineJoin(vg, NVG_ROUND);
	nvgBeginPath(vg);
	nvgMoveTo(vg, out.x, out.y);
	nvgQuadTo(vg, slump.x, slump.y, in.x, in.y);
	nvgStrokeColor(vg, color::mult(cw->color, 0.8f));
	nvgStrokeWidth(vg, thickness);
	nvgStroke(vg);
	nvgStrokeColor(vg, color::mult(cw->color, 0.95f));
	nvgStrokeWidth(vg, thickness - 1.f);
	nvgStroke(vg);
	nvgRestore(vg);
}
/** The glow: a layer of the rack over its cables, drawn after them in the
rack's own pass, so under the rack's dimming as they are, and under every
menu and tooltip. Its box is a widget's own, the whole plane, as the rack's
cables' is. */
struct CableGlow : TransparentWidget {
	void draw(const DrawArgs& args) override {}
	void drawLayer(const DrawArgs& args, int layer) override {
		const CableGlowAsked& a = cableGlowAsked();
		if (layer != 3 || !APP->window || a.at != APP->window->getFrameTime() || !APP->scene || !APP->scene->rack)
			return;
		for (int64_t id : a.ids) {
			CableWidget* cw = APP->scene->rack->getCable(id);
			if (cw && cw->isComplete() && cw->cable && cw->cable->outputModule)
				paintCableOpaque(args.vg, cw);
		}
	}
};
/** The rack's one glow, made by the first of ours to step, after the
rack's cables so drawn over them. */
inline void cableGlow() {
	if (!APP || !APP->scene || !APP->scene->rack)
		return;
	RackWidget* rack = APP->scene->rack;
	for (Widget* c : rack->children)
		if (dynamic_cast<CableGlow*>(c))
			return;
	rack->addChild(new CableGlow);
}
/** What a bonded module of ours is in the colour scheme: the colour of the
group it works for where it has one; else the colour of the cable arriving in
it; else, standing in a bond with nothing using it and no cable leaving it,
white, idle but working; else, alone or patched out, the first colour, Rack's
yellow. `group` is a palette index or minus one. */
inline NVGcolor schemeColor(int group, uint32_t cableIn, bool bonded, bool patchedOut) {
	if (group >= 0)
		return playColor(group);
	if (cableIn)
		return fromRGB(cableIn);
	if (bonded && !patchedOut)
		return color::WHITE;
	return playColor(0);
}
