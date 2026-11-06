// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Panel.hpp"

/** Atn, ATN on the plate: how far a CV's travel reaches either side of nought,
an attenuverter and offset shaping the window of what stands to its right; a
VCA of a cable in IN, shaping nothing; and with its arrow lit a VCA of the
signal to its left, as though cabled into IN. */

using namespace rig;

namespace {

struct Atn;

#include "AtnEngine.hpp"

/** The colour of the cable in an Atn's own jack, as its widget last read it.
Where it cannot be had (a render with no cable widgets in it) a plain
near-white, so the meter is drawn rather than not at all. */
inline NVGcolor cableOf(Atn* a) {
	const uint32_t rgb = a ? relaxed(a->cableRGB) : 0;
	const int fed = a ? relaxed(a->fedColNow) : -1;
	return rgb ? fromRGB(rgb) : fed >= 0 ? playColor(fed) : color::alpha(color::WHITE, 0.8f);
}

// The two jacks that drive the faders stand on the plate under the glass, on
// the rows their neighbours use rather than on rows of their own (the gain's
// on the RESET row of a Clock and a Seq, the offset's on a Clock's RUN row,
// a Seq's PHASE and a member's IN) so a rack of these standing together has
// one line of jacks across it.
const float ATN_GAIN_Y = seqRowY(3); // the RESET row, as the offset jack is on the PHASE row
const float ATN_OFS_Y = seqRowY(4);
// The glass is exactly as tall as the stock VCA-1's display, measured off
// the VCA-1 itself (its display widget stands 55.88 mm from the stock top
// edge), so the two read as one surface in a rack.
const float ATN_SCREEN_H = 55.88f;
// The times and the plus over the jacks, the operators that caption them:
// each mark's reach from its centre.
const float ATN_MARK_R = 0.8f;
// Where each jack's line climbs to its lane: halfway between the panel's
// edge and the jack's, so the climb clears the jack's ring, the gain's on
// the left under its lane and the offset's on the right under its own.
const float ATN_WIRE_X = (CX - JACK_R) / 2.f;
// Kept here rather than in Look.hpp, whose painters they are, so the ATN's
// panel keeps its exact pixels: moved there they lose this file's
// internal linkage, the build's -funsafe-math-optimizations folds them
// differently, and two rows of the panel come out one level of grey apart.
/** The two operators, drawn rather than set. A font's times and plus are
made to sit in running text beside letters: they are small, light, and at six
points they read as specks. Drawn, they are two strokes each at a weight of
their own. */
inline void drawTimesMark(NVGcontext* vg, float cx, float cy, float r, NVGcolor col) {
	const float d = r * 0.72f;
	nvgBeginPath(vg);
	nvgMoveTo(vg, cx - d, cy - d);
	nvgLineTo(vg, cx + d, cy + d);
	nvgMoveTo(vg, cx + d, cy - d);
	nvgLineTo(vg, cx - d, cy + d);
	nvgStrokeWidth(vg, std::max(1.f, r * 0.42f));
	nvgStrokeColor(vg, col);
	nvgLineCap(vg, NVG_ROUND);
	nvgStroke(vg);
}
inline void drawPlusMark(NVGcontext* vg, float cx, float cy, float r, NVGcolor col) {
	nvgBeginPath(vg);
	nvgMoveTo(vg, cx - r, cy);
	nvgLineTo(vg, cx + r, cy);
	nvgMoveTo(vg, cx, cy - r);
	nvgLineTo(vg, cx, cy + r);
	nvgStrokeWidth(vg, std::max(1.f, r * 0.42f));
	nvgStrokeColor(vg, col);
	nvgLineCap(vg, NVG_ROUND);
	nvgStroke(vg);
}

#include "AtnLane.hpp"

/** The picture behind both handles, drawn once across the whole glass so
nothing in it has a seam where the lanes meet: the reach of the output and
the room it cannot reach, the centre line, the meter, the scale and the
reach read out in volts. */
struct AtnGlass : Widget {
	AtnLane* gain = nullptr;
	AtnLane* offset = nullptr;
	/** The plain layer: the screen's face, whole and where it always stands,
	then the room the output cannot reach laid over it in a CV's fader's own
	solid colour, inside the bevel exactly as a CV's fader is, so a line of
	the screen's black is always left above and below. Both dim with the room,
	as every other screen's face does, so the Atn's glass never stays bright
	in a darkened room while the screens beside it dim. A shine drawn to the
	reach's own size, riding its edges, was tried and not liked. */
	void draw(const DrawArgs& args) override {
		if (!gain || !offset)
			return;
		NVGcontext* vg = args.vg;
		AtnLane* L = gain;
		const float W = box.size.x, h = box.size.y;
		const float off = L->offVolts(), g = L->gainVolts();
		float lo, hi;
		L->spanOf(g, lo, hi);
		// On the device's pixels, as the handles are (`AtnLane::handleRect`).
		const float yTop = snapY(vg, L->yOf(off + hi));
		const float yBot = snapY(vg, L->yOf(off + lo));
		paintGlassFace(vg, Rect(Vec(0.f, 0.f), Vec(W, h)));
		const float zTop = L->bevel(), zFoot = h - L->bevel();
		nvgBeginPath(vg);
		if (yTop > zTop)
			nvgRect(vg, 0.f, zTop, W, std::min(yTop, zFoot) - zTop);
		if (yBot < zFoot)
			nvgRect(vg, 0.f, std::max(yBot, zTop), W, zFoot - std::max(yBot, zTop));
		nvgFillColor(vg, atnVeil());
		nvgFill(vg);
		Widget::draw(args);
	}
	/** The lit layer: the glass's shine, the meter, the scale and its
	readouts, bright in a dark room as every screen's readout is. */
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1 || !gain || !offset)
			return;
		NVGcontext* vg = args.vg;
		AtnLane* L = gain;
		Atn* r = L->getAtn();
		const NVGcolor col = L->color(r);
		const NVGcolor pane = SCREEN_FACE;
		const float W = box.size.x, h = box.size.y, seam = W / 2.f;
		const float off = L->offVolts(), g = L->gainVolts();
		auto yOf = [&](float v) { return L->yOf(v); };
		float lo, hi;
		L->spanOf(g, lo, hi);
		paintGlassLit(vg, Rect(Vec(0.f, 0.f), Vec(W, h)));

		// The playheads standing on me, behind everything else on the glass,
		// as a walked CV and a walked Qnt show theirs: one soft line per Seq
		// here, in its colour, clustered and centred where several stand on
		// one Atn. `paintPlayheads` and the lanes every member of a block
		// keeps, nothing of my own, so two Seqs walking a block of Atns read
		// as two.
		if (r) {
			// A Seq in Fade stands on two at once, each at its weight.
			int on[8];
			bool line[8], halo[8];
			float lineK[8], haloK[8];
			const int n = r->playheadLanes(on, line, halo, lineK, haloK);
			paintPlayheadsOnGlass(vg, box.size, on, n, line, halo, lineK, haloK);
		}

		// The meter, on the black: from where the output sits to where it is,
		// or how far it swings either side of it where it is audio, a lane
		// for every channel my jack carries (`meterLane`). Nothing passing,
		// nothing drawn.
		if (r && relaxed(r->meter.meteringNow)) {
			const NVGcolor lit = r->jackLiveNow() ? cableOf(r) : col;
			const int n = clamp(r->meter.lanesNow.load(std::memory_order_acquire), 1, (int)PORT_MAX_CHANNELS);
			for (int c = 0; c < n; c++) {
				float from = yOf(off), to = from;
				const bool audio = relaxed(r->meter.audioNow[c]);
				if (audio) {
					const float peak = relaxed(r->meter.peakNow[c]);
					from = yOf(off + peak);
					to = yOf(off - peak);
				}
				else
					to = yOf(relaxed(r->meter.levelNow[c]));
				float x, w;
				meterLane(W, n, c, x, w);
				paintMeterLane(vg, x, w, from, to, lit, !audio);
			}
		}

		// A tick for every volt of the output either side of the seam; five
		// either way longer and heavier, and nought as heavy and twice as
		// long again, the scale's own middle, which stays where it is while
		// the centre line moves with the offset.
		// The clamp lit, the grey outside the window holds no ticks: OUT
		// never goes there.
		// Skipped rather than painted over, so the veil stays the one grey
		// it is everywhere; a tick on the window's edge stays.
		const bool clamped = r && r->clampSet();
		const int edge = (int)L->scaleVolts();
		for (int v = 1 - edge; v <= edge - 1; v++) {
			if (clamped && ((float)v > off + hi || (float)v < off + lo))
				continue;
			const bool major = v == 0 || (edge > 5 && (v == 5 || v == -5));
			const float len = mm2px(v == 0 ? 2.8f : major ? 1.4f : 0.7f);
			paintLine(vg, seam - len, 2.f * len, yOf((float)v), major ? 2.f : 1.f,
			          color::alpha(col, major ? 0.4f : 0.2f));
		}

		// The centre line: where the output sits with nothing at the input,
		// which is the offset in force, fader and jack together. Right
		// across and solid, over the scale, so standing on nought it hides
		// the nought mark under it rather than showing it through: the same
		// painter and thickness as the scale's heavy ticks, so on nought it
		// is the nought tick's device pixels exactly, at every zoom.
		paintLine(vg, 0.f, W, yOf(off), 2.f, over(pane, col, 0.45f * col.a));

		// The reach read out on the offset's side: its top, its foot, and
		// the centre line, each just inside the black against the module's
		// right edge. In volts, or where a scale is in reach as the notes
		// the reach really ends on: the highest note of the scale at or
		// under its top and the lowest at or over its foot. The centre's is
		// left out where the offset's handle stands on it already saying the
		// same, and where the black is too narrow to hold three.
		{
			const float top = off + hi, bot = off + lo;
			const float ty = yOf(top), by = yOf(bot), cy = yOf(off);
			const float px = 7.f, pad = mm2px(1.9f), x = W - mm2px(0.4f);
			const NVGcolor ink = color::alpha(col, 0.85f);
			const bool notes = r && relaxed(r->notesNow);
			// The notes the reach really ends on, worked out as the CV works
			// them out (`Atn::spanEnds`), never floored volts with a
			// tolerance, which read one note high of the note the CV could
			// reach.
			int footNote = 0, topNote = 0;
			if (notes && r)
				r->spanEnds(lo, hi, footNote, topNote);
			auto said = [&](float volts, int dir) {
				// As the CVs' jacks give it, no rails.
				if (!notes)
					return decimals(sane(volts), 1, 1);
				if (dir == 0)
					return noteName((int)std::round(volts * 12.f));
				return noteName(dir > 0 ? topNote : footNote);
			};
			// Each just inside the black where there is room, and just
			// outside it where the black is too narrow to hold both or the
			// offset's handle stands on the inside place; outside only where
			// the glass has room for it past the black's edge.
			const float handleY = yOf(offset->knobOffVolts());
			const Rect hr = offset->handleRect(vg, handleY);
			auto underHandle = [&](float y) {
				return y > hr.pos.y - px / 2.f - 1.f && y < hr.pos.y + hr.size.y + px / 2.f + 1.f;
			};
			const bool narrow = by - ty <= mm2px(3.2f);
			const float edgeTop = L->bevel() + px / 2.f + 1.f, edgeFoot = h - L->bevel() - px / 2.f - 1.f;
			// And where the outside place is under the handle as well, which
			// is where the handle stands on that very end, just past the
			// handle on the same side.
			auto place = [&](float inner, float outer, float past) {
				auto fits = [&](float y) { return y >= edgeTop && y <= edgeFoot; };
				if (!narrow && !underHandle(inner))
					return inner;
				if (fits(outer) && !underHandle(outer))
					return outer;
				if (fits(past))
					return past;
				return fits(outer) ? outer : inner;
			};
			const float topY = place(ty + pad, ty - pad, hr.pos.y - px / 2.f - 2.f);
			const float botY = place(by - pad, by + pad, hr.pos.y + hr.size.y + px / 2.f + 2.f);
			L->drawValue(vg, x, topY, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, px, ink, said(top, 1));
			L->drawValue(vg, x, botY, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, px, ink, said(bot, -1));
			// The centre's, where a cable at the offset's jack has moved it
			// off the offset's handle: on the gain's side, against the left
			// edge, so it is read beside the handle it has left rather than
			// under it; over its line or else under it, and only where it
			// stays on the black and clears the gain's handle, never on top
			// of the reach's own readings.
			const Rect gr = gain->handleRect(vg, gain->gainY(gain->knobGainVolts()));
			auto clear = [&](float y) {
				const float gap = px / 2.f + 1.f;
				const bool onGain = y > gr.pos.y - gap && y < gr.pos.y + gr.size.y + gap;
				return y > ty + gap && y < by - gap && !onGain && y >= edgeTop && y <= edgeFoot;
			};
			const float lx = mm2px(0.4f);
			if (offset->jackLive() && std::fabs(cy - handleY) > 1.f) {
				if (clear(cy - pad))
					L->drawValue(vg, lx, cy - pad, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, px, ink, said(off, 0));
				else if (clear(cy + pad))
					L->drawValue(vg, lx, cy + pad, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, px, ink, said(off, 0));
			}
		}
	}
};

void OfsQuantity::setDisplayValueString(std::string s) {
	ParamQuantity::setDisplayValueString(typedBack(s));
}
/** In notes the field reads the notes the CVs reach, "C4 to F4", which Rack's
parser could not read, so an edit of it changed nothing. Its reading handed back as it stands is no change: the
ends are the notes of the scale inside the span, so their distance is not the
span's width (six semitones over C major reads C4 to F4), and reading it as a
width would narrow the gain on an Enter that edited nothing. An edited span
is the width between its two notes, as a typed number is. */
void GainQuantity::setDisplayValueString(std::string s) {
	float semis;
	if (atn && relaxed(atn->notesNow)) {
		if (string::trim(s) == getDisplayValueString())
			return;
		if (readNoteSpan(string::trim(s), semis)) {
			setDisplayValue(semis);
			return;
		}
	}
	ParamQuantity::setDisplayValueString(typedBack(s));
}

/** The offset in force, the fader with its jack added, as the gain's reads
the gain in force and the glass draws where nought stands (`ofsAmount`). */
std::string OfsQuantity::getDisplayValueString() {
	static const OfsCore plain;
	return atn ? atn->ofs.reading(atn->ofsAmount()) : plain.reading(getValue());
}

/** A value typed into the field is read in the field's own unit, as the CRV's
points are: volts or semitones on the offset, and on the gain the multiplier,
or the span's semitones as the glass reads them where it is in notes. Rack
hands a typed number to `setDisplayValue`, which for a plain param is the raw
value, so without this typing 3 would put the offset at +10 V and 1 the gain
at x0.10. A count of semitones is whole from where it is typed. */
void OfsQuantity::setDisplayValue(float v) {
	if (atn && relaxed(atn->ofs.unit) == OfsCore::UNIT_SEMIS)
		setValue((float)(int)std::round(v) / (float)OfsCore::SEMI_REACH);
	else if (atn && relaxed(atn->ofs.unit) == OfsCore::UNIT_ROLL)
		setValue((float)(int)std::round(v * 100.f) / (float)(OfsCore::SEMI_REACH * 100)); // typed semitones, whole cents
	else
		setValue(v / (float)OfsCore::MAX_VOLTS);
}
void GainQuantity::setDisplayValue(float v) {
	if (atn && relaxed(atn->notesNow))
		setValue((float)(int)std::round(v) / 12.f);
	else
		setValue(v * CV_VOLTS);
}

std::string GainQuantity::getDisplayValueString() {
	const float v = getValue();
	if (!atn)
		return decimals(v, 1, 1) + " V";
	// The fader's own reach, as the handle reads it, not what the jack
	// makes of it.
	const Scale scale = atn->spanScale();
	if (relaxed(atn->notesNow)) {
		// The notes the CVs really reach, as the glass names them, the one
		// note alone where the span holds only one: the span in force, the
		// gain's jack with the knob and a boost held at x1 as the rig takes
		// it, beside the transpose in force.
		float lo, hi;
		const float g = clamp(atn->gainInForce(), -CV_VOLTS, CV_VOLTS);
		noteSpan(g, scale, lo, hi);
		int foot, top;
		atn->spanEnds(lo, hi, foot, top);
		if (foot == top)
			return noteName(foot);
		if (g < 0.f)
			return noteName(top) + " down to " + noteName(foot);
		return noteName(foot) + " to " + noteName(top);
	}
	// Signed, because below the line inverts and that is the whole of what
	// this module is: x-1.00 and x1.00 read the same until the sign is there,
	// and they are opposites. In force, the gain's jack with the knob, as the
	// notes say theirs.
	return "x" + decimals(atn->gainInForce() / CV_VOLTS, 2, 3);
}

/** The clamp's diode, level with OUT on the block's right edge, its tab
reaching the block's top at the corner. */
const EdgeTab ATN_CLAMP_TAB = {FOOT_CAP_Y, 1, -1};

/** An Atn is titled ATN wherever it stands and whatever it does; its output
jack says OUT. */
struct AtnPanel : Panel {
	void draw(const DrawArgs& args) override {
		drawPlate(args, this, "ATN", mm2px(SCREEN_Y), mm2px(ATN_SCREEN_H));
		// Each jack's caption is its lane's operator, the times over the
		// gain's and the plus over the offset's; the line from each jack to
		// its lane is AtnWires, since it lights with the cable.
		drawTimesMark(args.vg, mm2px(CX), mm2px(capAbove(ATN_GAIN_Y)), mm2px(ATN_MARK_R), INK);
		drawPlusMark(args.vg, mm2px(CX), mm2px(capAbove(ATN_OFS_Y)), mm2px(ATN_MARK_R), INK);
		drawCaption(args.vg, CX, IN_CAP_Y, "IN", INK);
		drawOutBlock(args.vg, BLOCK_Y, BLOCK_H, {ATN_CLAMP_TAB});
		drawCaption(args.vg, CX, FOOT_CAP_Y, "OUT", PALE);
	}
};

/** A groove pressed into the plate by a round rod along whatever path `path`
lays, offset by the vector it is handed: lit from above, so its upper wall
is in shadow and the floor brightens toward the lower wall, and the lip below
catches the light. Drawn as nested strokes, each narrower one nudged toward
the lit side and a shade lighter, which is a round section's shading in a few
steps; the corners turn round with it. A fill, a cable's colour or what the
arrow takes, fills it the same way, darker at the walls; none is the bare
plate's floor. The one painter of the Atn's grooves, the jacks' lines and the
arrow alike. Its ends are cut square, `cap` by default, since a jack's line
runs under its jack and into the glass; the arrow's are rounded, its shaft's
ends and its head's points, as the times and the plus are drawn. */
template <typename Path>
inline void paintGroove(NVGcontext* vg, Path path, const NVGcolor* fill, int cap = NVG_BUTT) {
	const float w = mm2px(0.6f);
	nvgLineJoin(vg, NVG_ROUND);
	nvgLineCap(vg, cap);
	auto stroke = [&](float width, Vec by, NVGcolor col) {
		path(vg, by);
		nvgStrokeColor(vg, col);
		nvgStrokeWidth(vg, width);
		nvgStroke(vg);
	};
	const float d = w * 0.18f;
	// Ours, with no stock twin. The lip catches a quarter of the light in the
	// dark. Where a groove runs along the lip's offset, down and to the right,
	// the lip falls under the groove itself and only the groove's own greys
	// tell it from the plate, so in the dark they stand well under the plate,
	// near black, rather than a share of it, which left that diagonal unseen.
	static const Tone LIP = {nvgRGBA(0xff, 0xff, 0xff, 0xc0), nvgRGBA(0xff, 0xff, 0xff, 0x30)};
	static const Tone SHADE = {nvgRGB(0xa9, 0xa9, 0xa9), nvgRGB(0x0a, 0x0a, 0x0a)};
	static const Tone FLOOR = {nvgRGB(0xd2, 0xd2, 0xd2), nvgRGB(0x11, 0x11, 0x11)};
	static const Tone WALL = {nvgRGB(0xbb, 0xbb, 0xbb), nvgRGB(0x0d, 0x0d, 0x0d)};
	stroke(w, Vec(d, d), LIP);       // the lip
	stroke(w, Vec(0.f, 0.f), SHADE); // the wall in shadow
	const NVGcolor floor = fill ? *fill : (NVGcolor) FLOOR;
	const NVGcolor wall = fill ? nvgLerpRGBA(floor, color::BLACK, 0.35f) : (NVGcolor) WALL;
	stroke(w * 0.78f, Vec(d * 0.3f, d * 0.3f), wall);
	stroke(w * 0.52f, Vec(d * 0.5f, d * 0.5f), nvgLerpRGBA(wall, floor, 0.6f));
	stroke(w * 0.28f, Vec(d * 0.6f, d * 0.6f), floor);
}

/** The two lines on the plate from the jacks that drive the lanes up into the
lanes they drive: out of the gain's jack to the left and up under the gain's
half of the glass, out of the offset's to the right and up under the offset's.
Each is a groove engraved in the plate, the way the jack's own ring is set
into it (its floor a shade under the plate, its upper wall in shadow and its
lower lip catching the light) and a cable in its jack fills the groove with
the cable's colour, as water fills a channel. Drawn under the jacks, so each
seems to leave from the jack's middle; and live, since a cable comes and goes
without the plate being told. They say which fader a cable has hold of without
a caption. */
struct AtnWires : Widget {
	Atn* atn = nullptr;
	void path(NVGcontext* vg, float jackY, float x, Vec by) {
		const float top = SCREEN_Y + ATN_SCREEN_H;
		nvgBeginPath(vg);
		nvgMoveTo(vg, mm2px(CX) + by.x, mm2px(jackY) + by.y);
		nvgLineTo(vg, mm2px(x) + by.x, mm2px(jackY) + by.y);
		nvgLineTo(vg, mm2px(x) + by.x, mm2px(top));
	}
	/** A groove (`paintGroove`), filled with the cable's colour while its
	jack has one. */
	void groove(NVGcontext* vg, float jackY, float x, uint32_t rgb) {
		const NVGcolor ink = jackInk(rgb);
		paintGroove(vg, [&](NVGcontext* v, Vec by) { path(v, jackY, x, by); }, rgb ? &ink : nullptr);
	}
	void draw(const DrawArgs& args) override {
		groove(args.vg, ATN_GAIN_Y, ATN_WIRE_X, atn ? relaxed(atn->vcaRGB) : 0);
		groove(args.vg, ATN_OFS_Y, PANEL_W - ATN_WIRE_X, atn ? relaxed(atn->ofsRGB) : 0);
	}
};

/** The Atn's two arrows, one switch on `GRAB_PARAM` seen twice: each a groove like the jacks' lines
(`paintGroove`), a shaft with a head at its tip, rounded at its ends.

The arrow into IN comes down from above and to the left into the upper left of the IN jack.
Lit, the Atn takes the signal to its left as though it were cabled into IN,
and the groove fills with the colour of what it takes, a strip's channel's
after a Poly, the panel's ink where that is colourless. It lights only
where its left gives what it takes, a signal or a Poly (`Atn::canGrabNow`,
the signal slot's own answer: an arrow lights only where what it points at
is real), and is drawn only there, lit or not: toward a Clock or the rig's end there is no IN
arrow, and the right arrow put out leaves the Atn standalone, a VCA of IN
with OUT giving the offset, the right arrow bare and saying so.

The arrow to the right stands under the glass and above the times, level,
a half arrow pointing right: lit, the Atn shapes the windows to its right, the IN arrow
out and IN empty, and it fills with the panel's ink always, whatever the
colours to its right, since it likely shapes several modules, most often
white and sequenced by various colours.
Drawn only where
something to its right reads, or would read, its window
(`Atn::shapesRightNow`).

A click on either turns the param, one undo, as any switch does, so the two
are always one lit and one not; where neither side has anything to say,
nothing to take on the left and nothing reading the window on the right, no
arrow is drawn at all. A hidden arrow is no control: Rack hands no press or
hover to a widget that is not visible. A cable in IN wins over both, and neither is
drawn while one is there, since an arrow with nothing to say is noise: the parse is the VCA's whatever the
param says, so neither arrow changes anything heard, and the param keeps its
value for when the cable goes. */
struct AtnArrow : app::Switch {
	Atn* atn = nullptr;
	bool right = false; // the arrow under the glass pointing right, or the one into IN
	/** An arrow's lines in the panel's millimetres: the shaft from `start`
	to `tip`, the head's barbs ending at `barb1` and `barb2`, or at `barb1`
	alone where it is a half arrow, and the box that holds them with a margin
	for the groove's width. */
	struct Shape {
		Vec start, tip, barb1, barb2;
		Rect box;
		bool half = false;
	};
	static constexpr float MARGIN = 0.6f; // mm, the groove's width round the lines
	// The IN arrow's tip stands just off the jack's ring on its upper left
	// diagonal, the shaft coming in at forty-five degrees from above and to
	// the left. Half a millimetre off the ring
	// sat too close to IN, so the tip stands a millimetre further off and
	// the shaft is shorter by as much, starting where it did.
	static constexpr float TIP_AIR = 1.5f;                            // the tip's distance from the jack's ring, mm
	static constexpr float REACH = 3.2f - 1.f * (float)M_SQRT1_2;     // the shaft's run, each way, mm
	static constexpr float HEAD = 1.3f;           // each barb's run, each way, mm
	static Shape inShape() {
		const float k = (JACK_R + TIP_AIR) * (float)M_SQRT1_2;
		const Vec t(CX - k, IN_Y - k);
		const float h = HEAD * (float)M_SQRT1_2;
		const float m = MARGIN;
		return {Vec(t.x - REACH, t.y - REACH), t, Vec(t.x - 2.f * h, t.y), Vec(t.x, t.y - 2.f * h),
		        Rect(Vec(t.x - REACH - m, t.y - REACH - m), Vec(REACH + 2.f * m, REACH + 2.f * m))};
	}
	/** The right arrow, level under the glass and pointing right, a half
	arrow, its head the lower barb alone. Its span is the jacks' rings under it, `CX` to either
	side by `JACK_R`, so each end stands as far from the groove beside it,
	the times' jack's line on the left and the plus's on the right, as that
	groove stands from the rings, and the arrow is balanced between the two.
	`LIFT` above the middle of the plate between the glass and the times, so
	it reads as the glass's rather than the times'. Its box runs from the
	glass down, so a click under the glass is the arrow's, and never into
	the glass, whose strokes are its own. */
	static constexpr float LIFT = 1.f; // mm
	static Shape rightShape() {
		const float glass = SCREEN_Y + ATN_SCREEN_H, times = capAbove(ATN_GAIN_Y) - ATN_MARK_R;
		const float y = (glass + times) / 2.f - LIFT;
		const Vec t(CX + JACK_R, y);
		const float len = 2.f * JACK_R, m = MARGIN;
		const Vec barb(t.x - HEAD, y + HEAD);
		return {Vec(t.x - len, y), t, barb, barb,
		        Rect(Vec(t.x - len - m, glass), Vec(len + 2.f * m, barb.y + m - glass)), true};
	}
	Shape shape() const { return right ? rightShape() : inShape(); }
	/** What a lit arrow fills with, the one place either's colour is chosen:
	the colour of what it takes, a strip's channel's after a Poly, and where
	that is none, colourless, the panel's ink, since white on the pale plate
	cannot be seen (the glass, being dark, still shows colourless as white,
	`AtnLane::colour`). The right
	arrow takes nothing, whatever the colours to its right, so it is ink
	always. */
	NVGcolor taken() {
		int col = -1; // colourless
		if (!right) {
			const int fed = relaxed(atn->fedColNow);
			if (fed >= 0)
				col = fed;
			else if (!relaxed(atn->colorlessNow) && relaxed(atn->canGrabNow) &&
			         !(relaxed(atn->stripNow) >= 0 && relaxed(atn->stripEmptyNow)))
				col = std::max(0, relaxed(atn->takenColNow));
		}
		return col < 0 ? INK : playColor(col);
	}
	/** Whether this arrow is drawn: only where what it points at is real,
	whatever the param says, from what the engine published. Neither while a
	cable is in IN, whose parse the param does not enter (`Atn::beforeParse`). */
	bool shown() {
		if (relaxed(atn->vcaNow))
			return false;
		return relaxed(right ? atn->shapesRightNow : atn->canGrabNow);
	}
	/** A click turns the switch, one undo named for what the ATN does now,
	as the bond names the same turn, never Rack's "move switch". */
	void onDragStart(const DragStartEvent& e) override {
		ParamWidget::onDragStart(e);
		if (e.button != GLFW_MOUSE_BUTTON_LEFT || !atn)
			return;
		const bool takes = !atn->grabSet();
		GestureChanges turn;
		turn.note(atn, Atn::GRAB_PARAM);
		atn->paramQuantities[Atn::GRAB_PARAM]->setValue(takes ? 1.f : 0.f);
		turn.push(takes ? "ATN takes its left" : "ATN shapes its right");
	}
	void draw(const DrawArgs& args) override {
		const Shape sh = shape();
		const Vec o = sh.box.pos;
		auto path = [&](NVGcontext* vg, Vec by) {
			auto at = [&](Vec p) { return Vec(mm2px(p.x - o.x) + by.x, mm2px(p.y - o.y) + by.y); };
			const Vec s = at(sh.start), e = at(sh.tip), b1 = at(sh.barb1), b2 = at(sh.barb2);
			nvgBeginPath(vg);
			nvgMoveTo(vg, s.x, s.y);
			nvgLineTo(vg, e.x, e.y);
			nvgMoveTo(vg, b1.x, b1.y);
			nvgLineTo(vg, e.x, e.y);
			if (!sh.half)
				nvgLineTo(vg, b2.x, b2.y);
		};
		const bool lit = atn && atn->grabSet() != right && !relaxed(atn->vcaNow) && (right || relaxed(atn->canGrabNow));
		const NVGcolor fill = lit ? taken() : NVGcolor();
		paintGroove(args.vg, path, lit ? &fill : nullptr, NVG_ROUND);
	}
};

struct AtnWidget : MemberWidget {
	Atn* atn = nullptr;
	AtnArrow* arrow = nullptr;      // into IN
	AtnArrow* rightArrow = nullptr; // under the glass, pointing right
	/** The neighbours this panel last saw the engine parse it between, by
	id, minus one for none, and whether it has seen any yet (`bond`). */
	int64_t bondLeft = -2, bondRight = -2;
	bool bonded = false;
	/** What the arrow does when the Atn bonds with neighbours, read every
	frame once the drag is over and the engine has parsed the rig it ended
	in, since the engine sees a module dragged along a rig as so many
	placings and cannot tell the last.

	The first time this Atn stands in a rig, the one guess: lit where a lit arrow would have
	something to take and nothing to its right takes its window, and lit
	always where what it would take is a Poly or a strip of one (POLY ATN
	ATN SEQ wants its strips), unlit
	otherwise, written as "guess the arrow". Once only: a patch or a
	duplicate brings its own arrow and its own word on whether it was
	guessed (`Atn::fromJson`).

	After that, a new bond takes the one job its new place offers (else in
	CLK ATN CRV an Atn moved from after a SEQ keeps taking its left and
	shapes nothing): where only one of the
	two arrows would be drawn, taking the left (`canGrabNow`) or shaping the
	right (`shapesRightNow`), the switch turns to it; where both are real or
	neither, it stays as set, the hand's to choose. A bond is new when a
	neighbour stands beside me that did not the last time this panel looked,
	and a hand's step that placed something has just been pushed
	(`structuralStep`): this Atn moved, or a module moved or added beside
	it. A neighbour taken away alone is no new bond, so a
	delete beside me keeps the switch (`atn-arrow-guess`). The first
	neighbours a panel sees are new only where that step added this module
	alone (`addsOnly`, the browser, a duplicate, the swap menu's insert). So
	a patch loaded, a rig built, a selection pasted, a scene recalled, a
	cable or an undo keeps the switch: an undo leaves no fresh step to
	join. The switch is written rather than the job derived because a click
	can still turn it afterwards, and the write joins the step that made the bond, so
	one undo takes back both. A cable in IN does not stop it, the switch
	waiting under the cable for when it goes. The same shape as a POLY's
	new cable giving its CVs back (`PolyWidget::giveBack`): a new bond or a
	new cable resets a remembered choice. */
	void bond() {
		if (!atn || APP->event->getDraggedWidget())
			return;
		const std::vector<ModuleWidget*>* run = runOf(this, true);
		if (!run)
			return;
		auto me = std::find(run->begin(), run->end(), this);
		if (me == run->end())
			return;
		const int64_t l = me == run->begin() ? -1 : (*(me - 1))->module->id;
		const int64_t r = me + 1 == run->end() ? -1 : (*(me + 1))->module->id;
		if (r != atn->rightSeenNow.load(std::memory_order_acquire) || l != relaxed(atn->leftSeenNow))
			return; // the engine has not parsed this rig yet
		auto stranger = [&](int64_t n) { return n >= 0 && n != bondLeft && n != bondRight; };
		const bool fresh = stranger(l) || stranger(r), first = !bonded;
		bondLeft = l;
		bondRight = r;
		bonded = true;
		if (!atn->arrowGuessed) {
			if (l < 0 && r < 0)
				return; // alone: no rig to guess from yet
			atn->arrowGuessed = true;
			if (relaxed(atn->vcaNow) || atn->grabSet())
				return; // a cable, or lit already: nothing to guess
			if (!relaxed(atn->canGrabNow) || (relaxed(atn->windowHeldNow) && !relaxed(atn->grabsPolyNow)))
				return;
			GestureChanges turn;
			turn.note(atn, Atn::GRAB_PARAM);
			atn->params[Atn::GRAB_PARAM].setValue(1.f);
			pushJoined(turn.take("guess the arrow"), structuralStep(), true);
			return;
		}
		if (!fresh)
			return;
		history::Action* placing = structuralStep();
		if (!placing || (first && !addsOnly(placing, atn->id)))
			return;
		const bool takes = relaxed(atn->canGrabNow), shapes = relaxed(atn->shapesRightNow);
		if (takes == shapes || atn->grabSet() == takes)
			return;
		GestureChanges turn;
		turn.note(atn, Atn::GRAB_PARAM);
		atn->params[Atn::GRAB_PARAM].setValue(takes ? 1.f : 0.f);
		atn->wake();
		pushJoined(turn.take(takes ? "ATN takes its left" : "ATN shapes its right"), placing, true);
	}
	/** Tells the module the colours of the cables in its jacks, which only
	the UI knows: the meter is drawn in the IN cable's and each handle lights
	in its own jack's. */
	void step() override {
		if (atn) {
			atn->cableRGB.store(cableColorOn(getInput(Atn::IN_INPUT)), std::memory_order_relaxed);
			atn->vcaRGB.store(cableColorOn(getInput(Atn::VCA_INPUT)), std::memory_order_relaxed);
			atn->ofsRGB.store(cableColorOn(getInput(Atn::OFS_CV_INPUT)), std::memory_order_relaxed);
			atn->sweepUi.store(sharesRunOf(this, atn), std::memory_order_relaxed);
			bond();
			arrow->visible = arrow->shown();
			rightArrow->visible = rightArrow->shown();
		}
		MemberWidget::step();
	}
	AtnWidget(Atn* module) {
		atn = module;
		open(module, new AtnPanel);

		FlatScreen* screen = createWidget<FlatScreen>(mm2px(Vec(0.f, SCREEN_Y)));
		screen->box.size = mm2px(Vec(PANEL_W, ATN_SCREEN_H));
		addChild(screen);
		// The gain on the left because it acts first, the offset on the
		// right, each the whole height of the glass in its own half of it,
		// and the picture both of them stand on behind them.
		const float laneH = screen->box.size.y;
		const float laneW = screen->box.size.x / 2.f;
		AtnGlass* glass = createWidget<AtnGlass>(Vec(0, 0));
		glass->box.size = screen->box.size;
		screen->addChild(glass);
		AtnLane* gain = createParam<AtnLane>(Vec(0, 0), module, Atn::GAIN_PARAM);
		gain->box.size = Vec(laneW, laneH);
		screen->addChild(gain);
		AtnLane* offset = createParam<AtnLane>(Vec(laneW, 0), module, Atn::OFS_PARAM);
		offset->offsetLane = true;
		offset->box.size = Vec(laneW, laneH);
		screen->addChild(offset);
		glass->gain = gain;
		glass->offset = offset;


		AtnWires* wires = createWidget<AtnWires>(Vec(0, 0));
		wires->box.size = box.size;
		wires->atn = module;
		addChild(wires);
		// The two views of the one switch. The IN arrow under the IN jack, so
		// a press on the ring is the jack's.
		auto addArrow = [&](bool right) {
			const Rect b = right ? AtnArrow::rightShape().box : AtnArrow::inShape().box;
			AtnArrow* a = createParam<AtnArrow>(mm2px(b.pos), module, Atn::GRAB_PARAM);
			a->box.size = mm2px(b.size);
			a->atn = module;
			a->right = right;
			addParam(a);
			return a;
		};
		arrow = addArrow(false);
		rightArrow = addArrow(true);
		addInput(createInputCentered<InPort>(mm2px(Vec(CX, ATN_GAIN_Y)), module, Atn::VCA_INPUT));
		addInput(createInputCentered<InPort>(mm2px(Vec(CX, ATN_OFS_Y)), module, Atn::OFS_CV_INPUT));
		addInput(createInputCentered<InPort>(mm2px(Vec(CX, IN_Y)), module, Atn::IN_INPUT));
		addOutput(createOutputCentered<OutPort>(mm2px(Vec(CX, FOOT_Y)), module, Atn::CV_OUTPUT));
		// The clamp, a diode on the block's right edge level with the OUT
		// caption, the very place a Seq's slew stands (`seqCapY(6)`), its tab
		// reaching the block's top (`ATN_CLAMP_TAB`): a toggle, lit in the
		// lanes' colour, one undo a click.
		EdgeLampParam* clampLamp = addEdgeLamp(this, module, Atn::CLAMP_PARAM, Atn::CLAMP_PARAM, ATN_CLAMP_TAB,
		                                       []() { return true; }, [gain, module]() { return gain->color(module); });
		clampLamp->clicked = [this, clampLamp](bool, bool) {
			const float to = clampLamp->lit() ? 0.f : 1.f;
			setAlongRun(
			    this, [to](Module*, std::vector<std::pair<int, float>>& out) { out.push_back({Atn::CLAMP_PARAM, to}); }, false,
			    to > 0.5f ? "turn on clamp" : "turn off clamp");
		};
	}
};

} // namespace

Model* modelAtn = createModel<Atn, AtnWidget>("ATN");
