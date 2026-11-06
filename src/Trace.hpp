// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "plugin.hpp"
#include <string>
#include <vector>

/** The panel as words: every drawing call this plugin makes, written down.

A trace of the jacks' voltages covers jacks only, so a screen that draws the
wrong thing passes it, and a picture says what was painted only to someone
looking. Almost everything worth asserting about a panel is a statement about
the sequence of calls, not about the pixels they produce: that the handle is
at this height, that it is the cable's colour and not the scheme's yellow,
that the caption says TRN and not OFS, that the mask is painted after the
handle rather than before it. So the calls are recorded. What comes out is
text: it diffs, it greps, it can be stored as a reference like any other, and
it can be read. "What is on the Atn's screen" becomes a question with an
answer in words.

How it works is a macro layer. Every NanoVG verb the plugin draws with is
wrapped in an inline of the same name, which forwards to the real one and,
while `on` is set, writes a line down first. The wrappers are ordinary
functions defined before the macros, so they call through without expanding
into themselves. Only this plugin's own calls are caught: a stock Rack widget,
a jack's SVG or a knob, draws through code we do not compile, so it leaves no
line, which is why the widget tree is dumped beside the trace: the tree says
where a jack is, the trace says what was painted.

Recording needs a real context, because the calls are forwarded to it, and
working with no context at all would mean never entering Rack's own widget
code, which the tree walk cannot promise; so a trace is a windowed run.

Nothing here costs anything while `on` is false: one global bool, tested on
a thread that is already drawing. */

namespace rig {
namespace trace {

/** Recording. Set by the test harness around a widget's draw, and false in every
ordinary frame. */
inline bool& recording() {
	static bool on = false;
	return on;
}
/** Where the lines go: one string per op, in the order they were drawn. */
inline std::vector<std::string>& ops() {
	static std::vector<std::string> v;
	return v;
}

/** Numbers are rounded to hundredths. A trace is compared with another
trace, and the last bits of a float are noise that would make every stored
trace fail for no reason; a hundredth of a Rack pixel is a hundred and fiftieth of
a millimetre, which is finer than anything a panel means. */
inline std::string num(float v) {
	const float r = std::round(v * 100.f) / 100.f;
	std::string s = string::f("%.2f", r == 0.f ? 0.f : r); // no "-0.00"
	while (s.size() > 1 && s.back() == '0')
		s.pop_back();
	if (!s.empty() && s.back() == '.')
		s.pop_back();
	return s;
}
inline std::string col(NVGcolor c) {
	auto b = [](float v) { return (int)std::lround(clamp(v, 0.f, 1.f) * 255.f); };
	return string::f("#%02x%02x%02x%02x", b(c.r), b(c.g), b(c.b), b(c.a));
}
/** Where a point really lands on the panel.

Rack applies each widget's own offset with a translate of its own, inside
code we do not compile, so a coordinate as the drawing code writes it is
local to whatever widget is drawing and says nothing on its own: two lanes
side by side both write their caption at the same x. The current transform is
asked of the context instead and the point put through it, so every number in
a trace is Rack pixels from the panel's top-left: the same frame the widget
dump uses, and one that does not move when a widget is reparented.

Panels here translate and scale and never rotate, so a length is scaled by
the transform's own scale and that is the whole of it. */
inline void here(NVGcontext* vg, float& x, float& y) {
	float t[6];
	nvgCurrentTransform(vg, t);
	const float nx = x * t[0] + y * t[2] + t[4];
	const float ny = x * t[1] + y * t[3] + t[5];
	x = nx;
	y = ny;
}
inline float span(NVGcontext* vg, float v) {
	float t[6];
	nvgCurrentTransform(vg, t);
	return v * std::sqrt(t[0] * t[0] + t[1] * t[1]);
}
inline void say(const std::string& line) { ops().push_back(line); }
inline void op(const char* verb) { say(std::string(verb)); }
inline void op(const char* verb, float a) { say(verb + (" " + num(a))); }
inline void op(const char* verb, float a, float b) {
	say(verb + (" " + num(a) + " " + num(b)));
}
inline void op(const char* verb, float a, float b, float c) {
	say(verb + (" " + num(a) + " " + num(b) + " " + num(c)));
}
inline void op(const char* verb, float a, float b, float c, float d) {
	say(verb + (" " + num(a) + " " + num(b) + " " + num(c) + " " + num(d)));
}
inline void op(const char* verb, float a, float b, float c, float d, float e) {
	say(verb + (" " + num(a) + " " + num(b) + " " + num(c) + " " + num(d) + " " + num(e)));
}

// ---- the wrappers ----------------------------------------------------------
// Each forwards to the real call and, while recording, writes the line.

inline void tBeginPath(NVGcontext* vg) {
	if (recording())
		op("path");
	nvgBeginPath(vg);
}
inline void tClosePath(NVGcontext* vg) {
	if (recording())
		op("close");
	nvgClosePath(vg);
}
inline void tMoveTo(NVGcontext* vg, float x, float y) {
	if (recording()) {
		float px = x, py = y;
		here(vg, px, py);
		op("move", px, py);
	}
	nvgMoveTo(vg, x, y);
}
inline void tLineTo(NVGcontext* vg, float x, float y) {
	if (recording()) {
		float px = x, py = y;
		here(vg, px, py);
		op("line", px, py);
	}
	nvgLineTo(vg, x, y);
}
inline void tRect(NVGcontext* vg, float x, float y, float w, float h) {
	if (recording()) {
		float px = x, py = y;
		here(vg, px, py);
		op("rect", px, py, span(vg, w), span(vg, h));
	}
	nvgRect(vg, x, y, w, h);
}
inline void tRoundedRect(NVGcontext* vg, float x, float y, float w, float h, float r) {
	if (recording()) {
		float px = x, py = y;
		here(vg, px, py);
		op("round", px, py, span(vg, w), span(vg, h), span(vg, r));
	}
	nvgRoundedRect(vg, x, y, w, h, r);
}
inline void tCircle(NVGcontext* vg, float cx, float cy, float r) {
	if (recording()) {
		float px = cx, py = cy;
		here(vg, px, py);
		op("circle", px, py, span(vg, r));
	}
	nvgCircle(vg, cx, cy, r);
}
inline void tArc(NVGcontext* vg, float cx, float cy, float r, float a0, float a1, int dir) {
	if (recording()) {
		float px = cx, py = cy;
		here(vg, px, py);
		op("arc", px, py, span(vg, r), a0, a1);
	}
	nvgArc(vg, cx, cy, r, a0, a1, dir);
}
inline void tFill(NVGcontext* vg) {
	if (recording())
		op("fill");
	nvgFill(vg);
}
inline void tStroke(NVGcontext* vg) {
	if (recording())
		op("stroke");
	nvgStroke(vg);
}
inline void tFillColor(NVGcontext* vg, NVGcolor c) {
	if (recording())
		say("ink " + col(c));
	nvgFillColor(vg, c);
}
inline void tStrokeColor(NVGcontext* vg, NVGcolor c) {
	if (recording())
		say("pen " + col(c));
	nvgStrokeColor(vg, c);
}
inline void tStrokeWidth(NVGcontext* vg, float w) {
	if (recording())
		op("width", span(vg, w));
	nvgStrokeWidth(vg, w);
}
inline void tFillPaint(NVGcontext* vg, NVGpaint p) {
	// A gradient is two colours and the line between them, which is what
	// reads on a panel; the rest of the paint is the machinery.
	if (recording())
		say("wash " + col(p.innerColor) + " " + col(p.outerColor));
	nvgFillPaint(vg, p);
}
inline void tStrokePaint(NVGcontext* vg, NVGpaint p) {
	if (recording())
		say("penwash " + col(p.innerColor) + " " + col(p.outerColor));
	nvgStrokePaint(vg, p);
}
inline void tText(NVGcontext* vg, float x, float y, const char* s, const char* e) {
	if (recording()) {
		float px = x, py = y;
		here(vg, px, py);
		say("text " + num(px) + " " + num(py) + " \"" +
		    std::string(s, e ? (size_t)(e - s) : strlen(s)) + "\"");
	}
	return (void) nvgText(vg, x, y, s, e);
}
inline void tFontSize(NVGcontext* vg, float s) {
	if (recording())
		op("size", span(vg, s));
	nvgFontSize(vg, s);
}
inline void tTextAlign(NVGcontext* vg, int a) {
	if (recording())
		op("align", (float)a);
	nvgTextAlign(vg, a);
}
inline void tTextLetterSpacing(NVGcontext* vg, float s) {
	if (recording())
		op("spacing", s);
	nvgTextLetterSpacing(vg, s);
}
// A translate or a scale writes no line of its own: every coordinate in a
// trace has already been through it, so saying it again would only be a
// number that means nothing on its own and a stored trace that moves whenever a
// widget is reparented. They are still forwarded, of course.
inline void tTranslate(NVGcontext* vg, float x, float y) { nvgTranslate(vg, x, y); }
inline void tScale(NVGcontext* vg, float x, float y) { nvgScale(vg, x, y); }
inline void tRotate(NVGcontext* vg, float a) {
	if (recording())
		op("turn", a);
	nvgRotate(vg, a);
}
inline void tSave(NVGcontext* vg) {
	if (recording())
		op("save");
	nvgSave(vg);
}
inline void tRestore(NVGcontext* vg) {
	if (recording())
		op("restore");
	nvgRestore(vg);
}
inline void tScissor(NVGcontext* vg, float x, float y, float w, float h) {
	if (recording()) {
		float px = x, py = y;
		here(vg, px, py);
		op("clip", px, py, span(vg, w), span(vg, h));
	}
	nvgScissor(vg, x, y, w, h);
}
/** An intersecting clip is a clip in the transcript too: what the reader
wants to know is what was cut, not which verb cut it, and the screens are
clipped this way now so that a lone screen's clip narrows Rack's rather than
replacing it. */
inline void tIntersectScissor(NVGcontext* vg, float x, float y, float w, float h) {
	if (recording()) {
		float px = x, py = y;
		here(vg, px, py);
		op("clip", px, py, span(vg, w), span(vg, h));
	}
	nvgIntersectScissor(vg, x, y, w, h);
}
inline void tGlobalAlpha(NVGcontext* vg, float a) {
	if (recording())
		op("alpha", a);
	nvgGlobalAlpha(vg, a);
}

/** A marker, so a flat stream of ops can be read back as a picture: the
test harness writes one before each module and each layer, and a widget that
wants to name itself in the trace may write one too. */
inline void mark(const std::string& what) {
	if (recording())
		say("-- " + what);
}

} // namespace trace
} // namespace rig

// Everything the plugin draws with goes through the wrappers from here down,
// in the dev form, the only one the test harness records: the release form
// draws straight into NanoVG.
#ifdef ATOMIC_DEV
#define nvgBeginPath ::rig::trace::tBeginPath
#define nvgClosePath ::rig::trace::tClosePath
#define nvgMoveTo ::rig::trace::tMoveTo
#define nvgLineTo ::rig::trace::tLineTo
#define nvgRect ::rig::trace::tRect
#define nvgRoundedRect ::rig::trace::tRoundedRect
#define nvgCircle ::rig::trace::tCircle
#define nvgArc ::rig::trace::tArc
#define nvgFill ::rig::trace::tFill
#define nvgStroke ::rig::trace::tStroke
#define nvgFillColor ::rig::trace::tFillColor
#define nvgStrokeColor ::rig::trace::tStrokeColor
#define nvgStrokeWidth ::rig::trace::tStrokeWidth
#define nvgFillPaint ::rig::trace::tFillPaint
#define nvgStrokePaint ::rig::trace::tStrokePaint
#define nvgText ::rig::trace::tText
#define nvgFontSize ::rig::trace::tFontSize
#define nvgTextAlign ::rig::trace::tTextAlign
#define nvgTextLetterSpacing ::rig::trace::tTextLetterSpacing
#define nvgTranslate ::rig::trace::tTranslate
#define nvgScale ::rig::trace::tScale
#define nvgRotate ::rig::trace::tRotate
#define nvgSave ::rig::trace::tSave
#define nvgRestore ::rig::trace::tRestore
#define nvgScissor ::rig::trace::tScissor
#define nvgIntersectScissor ::rig::trace::tIntersectScissor
#define nvgGlobalAlpha ::rig::trace::tGlobalAlpha
#endif
