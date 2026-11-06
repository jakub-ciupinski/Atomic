// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/** The colour scheme as the UI reads it, a cable's colour on a jack, a
screen's modulation ink, the UI's half of a Stack, a hovered row's cable lit
and what a bonded module is in the scheme. A part of Hand.hpp in a file of
its own: Hand.hpp includes it once, inside its namespace rig, and it is not a
header to include anywhere else. */

/** The colour of the first cable on a jack, packed RGBA, nought for none:
only the UI knows a cable's colour, so a widget asks in its step and hands it
to the module. A cable's colour is ours to wear wherever a module shows what
arrived through it. */
inline uint32_t cableColorOn(PortWidget* pw) {
	if (!pw || !APP || !APP->scene || !APP->scene->rack)
		return 0;
	std::vector<CableWidget*> cables = APP->scene->rack->getCompleteCablesOnPort(pw);
	if (cables.empty())
		return 0;
	return packRGB(cables[0]->color);
}
/** The ink of a screen's modulation line, the bar along its foot: the colour
of the cable bringing what it draws, packed as `cableColorOn` gives it, else
the screen's own ink, and the screen's own under the DEV's OLD. */
inline NVGcolor modColor(uint32_t rgb, NVGcolor own) {
	return rgb && !dev().oldColors ? fromRGB(rgb) : own;
}
/** The cables stacked on `port` as a `Stack` counts them: in the order the
rack holds them, which is the order they were plugged, at most
`Stack::MAX`, each from a module. The one list both the engine's count of a
row's cable (`publishStack`) and the hover's glow on it (`glowCable`) are
read against, so the two never count differently. */
inline std::vector<CableWidget*> stackCables(PortWidget* port) {
	std::vector<CableWidget*> kept;
	if (!port || !APP || !APP->scene || !APP->scene->rack)
		return kept;
	for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(port))
		if ((int)kept.size() < Stack::MAX && cw->cable && cw->cable->outputModule)
			kept.push_back(cw);
	return kept;
}
/** The UI's half of a `Stack` (Stack.hpp): the cables now on `port`
(`stackCables`), listed for the engine under the stack's sequence count.
True when the list changed, for the owner to ask for a wake, since the
input's sum may not have moved. */
inline bool publishStack(Stack& s, PortWidget* port) {
	if (!port || !APP || !APP->scene || !APP->scene->rack)
		return false;
	const std::vector<CableWidget*> cables = stackCables(port);
	const int k = (int)cables.size();
	// Nothing moved, nothing written: the list is compared first so a
	// still patch costs the engine no retake.
	bool same = k == s.n.load(std::memory_order_relaxed);
	for (int i = 0; same && i < k; i++) {
		const engine::Cable* c = cables[i]->cable;
		same = s.ids[i].load(std::memory_order_relaxed) == c->outputModule->id &&
		       s.outs[i].load(std::memory_order_relaxed) == c->outputId &&
		       s.rgbs[i].load(std::memory_order_relaxed) == packRGB(cables[i]->color);
	}
	if (same)
		return false;
	s.seq.fetch_add(1, std::memory_order_acq_rel);
	for (int i = 0; i < k; i++) {
		const engine::Cable* c = cables[i]->cable;
		s.ids[i].store(c->outputModule->id, std::memory_order_relaxed);
		s.outs[i].store(c->outputId, std::memory_order_relaxed);
		s.rgbs[i].store(packRGB(cables[i]->color), std::memory_order_relaxed);
	}
	s.n.store(k, std::memory_order_relaxed);
	s.seq.fetch_add(1, std::memory_order_acq_rel);
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
inline void glowCable(CableWidget* cw) {
	if (!cw || !cw->cable || !APP || !APP->window)
		return;
	CableGlowAsked& a = cableGlowAsked();
	const double now = APP->window->getFrameTime();
	if (a.at != now) {
		a.ids.clear();
		a.at = now;
	}
	if (std::find(a.ids.begin(), a.ids.end(), cw->cable->id) == a.ids.end())
		a.ids.push_back(cw->cable->id);
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
