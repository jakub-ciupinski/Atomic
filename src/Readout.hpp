// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/** The readouts, every number on a screen and the marks its end cells hold,
`drawNumber` and the segments. A part of Look.hpp in a file of its own, the
look as much as the rest of that file: Look.hpp includes it once, inside its
namespace rig, and it is not a header to include anywhere else. */

/** How tall a thing's ink stands, as a fraction of the point size it is set at.
nanovg's text bounds are the line's, not the ink's (it asks the font for its
ascender and descender and never looks at the glyph), so these were measured off
a render, or read from the font's own cap height. */
const float SEG_INK = 0.84f;   // DSEG7 Classic Mini, a digit
const float SEG_BEARING = 0.099f; // DSEG7 again, read from the font: a digit's ink stops 99 of its 1000 units short of its advance
const float SEG_EIGHT = 1.f; // DSEG7 again, read from the font: an 8's ink runs the face's whole height, 1000 of its 1000 units
const float PLATE_INK = 0.56f; // Nunito Bold, a capital: its cap height over its ascender and descender, 1442 / 2571
/** One size for every readout on every panel of ours.

A rig is one instrument, and a tempo on a CLK beside a rate and a step count on
a SEQ have to be the same digits or the rig reads as parts from different
machines. The screens they sit in are not the same height, so this is the size
asked for and the fit below is what a screen too small for it does about it,
which is shrink, and only as far as it has to. */
const float READOUT_PX = 12.f;
const float READOUT_MARGIN = 1.2f; // mm of screen left clear around the digits
/** How far a readout `wantW` by `wantH` has to shrink to fit a screen
`size`, the margin left clear: one, or less, and never more. */
inline float readoutFit(Vec size, float wantW, float wantH) {
	const float margin = mm2px(READOUT_MARGIN);
	float fit = 1.f;
	if (wantW > 0.f)
		fit = std::min(fit, std::max(size.x - margin, 1.f) / wantW);
	if (wantH > 0.f)
		fit = std::min(fit, std::max(size.y - margin, 1.f) / wantH);
	return fit;
}

/** A sign made of segments, on a cell of its own (`CellMark::SIGN`).

A seven-segment cell cannot make a slash, and a sign set beside the digits
in the plate's face reads as a letter standing beside a readout rather than
as part of one.

So this cell has four segments of its own, the four a display would have: a
plus sign turned through forty-five degrees, one short bar to each corner,
parted at the middle the way every segment is parted from its neighbour. A
slash lights the pair that lean one way, a backslash the other pair, an X all
four (`lit`, a bit each: one the slash's pair, two the backslash's), and the
ones left dark are ghosted exactly as a digit's are. It is the diagonal
quarter of a starburst display, which is where a real machine gets its X
from.

`cx`, `cy` are the middle of the cell and `h` the ink height of a digit; the
sign is drawn a little smaller than that, so it marks the number rather than
competing with it. */
inline void drawSegSign(NVGcontext* vg, float cx, float cy, float h, NVGcolor col, int lit) {
	const float reach = h * 0.34f;  // how far each bar runs from the middle
	const float t = h * 0.13f;      // and the weight of one, a lit segment's
	const float gap = t * 0.8f;     // the parting at the middle
	const float d = 0.7071f;        // forty-five degrees
	const NVGcolor dark = color::alpha(col, 0.15f);
	nvgLineCap(vg, NVG_BUTT);
	nvgStrokeWidth(vg, t);
	// The four arms, as (x, y) directions: the pair that lean like a slash
	// first, then the pair that lean like a backslash.
	static const float arm[4][2] = {{1.f, -1.f}, {-1.f, 1.f}, {-1.f, -1.f}, {1.f, 1.f}};
	for (int pass = 0; pass < 2; pass++) {
		const bool on = pass == 1;
		for (int a = 0; a < 4; a++) {
			if (on && !(lit & (a < 2 ? 1 : 2)))
				continue;
			const float ux = arm[a][0] * d, uy = arm[a][1] * d;
			nvgBeginPath(vg);
			nvgMoveTo(vg, cx + ux * gap, cy + uy * gap);
			nvgLineTo(vg, cx + ux * reach, cy + uy * reach);
			nvgStrokeColor(vg, on ? col : dark);
			nvgStroke(vg);
		}
	}
}

/** A direction made of segments, on a cell of its own (`CellMark::DIRECTION`):
chevrons, a < and a > side by side, each two short bars parted at the point
the way a segment is parted from its neighbour, the dark one ghosted as a
digit's is. `lit` says which are lit, a bit each, one the < and two the >:
the Seq's step screen lights the > forward, the < reverse and both, <>, for
the pendulum. The weight and the parting are the diagonal sign's
(`drawSegSign`), so the cell reads as the same display. Under the pointer
(`hot`) the ghosts brighten and the lit ones glow, to say the cell is a
button. */
inline void drawSegDir(NVGcontext* vg, float cx, float cy, float h, float w, NVGcolor col, int lit, bool hot) {
	const float reach = h * 0.30f;  // how far each bar runs up or down from the point
	const float t = h * 0.13f;      // a lit segment's weight
	const float gap = t * 0.8f;     // the parting at the point
	const float lean = reach * 0.75f; // how far a bar runs back from the point
	const float apart = w * 0.24f;    // each chevron's middle from the cell's
	const NVGcolor dark = color::alpha(col, hot ? 0.4f : 0.15f);
	nvgLineCap(vg, NVG_BUTT);
	for (int pass = hot ? -1 : 0; pass < 2; pass++) {
		// The glow under the lit ones, while hot: the same bars, wide and faint.
		const bool glow = pass < 0;
		const bool on = pass != 0;
		nvgStrokeWidth(vg, glow ? t * 2.6f : t);
		for (int c = 0; c < 2; c++) {
			const bool left = c == 0; // the <, pointing back
			const int bit = left ? 1 : 2;
			if (on && !(lit & bit))
				continue;
			const float px = cx + (left ? -apart - lean / 2.f : apart + lean / 2.f);
			const float dir = left ? 1.f : -1.f; // which way the bars run from the point
			const float len = std::sqrt(lean * lean + reach * reach);
			const float ux = dir * lean / len, uy = reach / len;
			for (int arm = 0; arm < 2; arm++) {
				const float sy = arm == 0 ? -1.f : 1.f;
				nvgBeginPath(vg);
				nvgMoveTo(vg, px + ux * gap, cy + sy * uy * gap);
				nvgLineTo(vg, px + ux * len, cy + sy * uy * len);
				nvgStrokeColor(vg, glow ? color::alpha(col, 0.22f) : on ? col : dark);
				nvgStroke(vg);
			}
		}
	}
}

/** What an end cell of a readout holds in place of a digit: nothing, the
cell being a digit's; a direction (`drawSegDir`); a diagonal sign
(`drawSegSign`); or a sign, the one middle segment of a digit's cell with an
arm touching it from above and one from below, so the minus lit alone is below
nought, all three a plus above it, and nothing lit nought, since a cell that
only ever says the sign has no use for the other six. The plus is a cheat on the face, with no
gap between its segments, so it reads as a plus rather than as a digit. `lit`
is a bit each, as those painters take it, and for a sign the minus and the
arms; `hot` is a direction under the pointer. A pie is the one mark that
keeps its cell's digit: a disc in the corner past the last cell's foot
right (`pieAt`), where a seven-segment cell keeps its decimal point, lit by
`lit` of `whole` (`paintPie`), so a free rate reads x3 with a little over a tenth of its pie lit for x3.11,
x2 with the lit rim alone for x2., and the hover gives every decimal; a
whole of nought is the pie off, its ghost alone. */
struct CellMark {
	enum Kind { DIGIT, DIRECTION, SIGN, SIGNED, PIE };
	Kind kind = DIGIT;
	int lit = 0;
	int whole = 0; // a pie's whole, `lit` its part
	bool hot = false;
	/** Whether the mark takes its cell from the digits: every mark but a
	pie, which stands beside the last digit. */
	bool ownCell() const { return kind != DIGIT && kind != PIE; }
};
inline CellMark directionMark(int lit, bool hot) {
	CellMark m;
	m.kind = CellMark::DIRECTION;
	m.lit = lit;
	m.hot = hot;
	return m;
}
/** `sign` is '/', '\\', 'X' or nought for every segment dark. */
inline CellMark signMark(char sign) {
	CellMark m;
	m.kind = CellMark::SIGN;
	m.lit = (sign == '/' || sign == 'X' ? 1 : 0) | (sign == '\\' || sign == 'X' ? 2 : 0);
	return m;
}

/** The last cell's pie, `part` of `whole` lit; a `whole` of nought, off. */
inline CellMark pieMark(int part, int whole) {
	CellMark m;
	m.kind = CellMark::PIE;
	m.lit = part;
	m.whole = whole;
	return m;
}

/** A signed count's sign cell: a minus below nought, a plus above it, and
only the ghosts at nought. */
inline CellMark signedMark(int n) {
	CellMark m;
	m.kind = CellMark::SIGNED;
	m.lit = n < 0 ? 1 : n > 0 ? 3 : 0;
	return m;
}

/** Where a readout's cells stand on its screen: the left edge of the first,
the middle, one cell's width and the point size, shrunk to fit. */
struct CellsLayout {
	float x = 0.f, y = 0.f, cell = 0.f, px = READOUT_PX;
	float w = 0.f; // the screen's width, for what stands beside the cells
	int cells = 0;
	bool ok = false;
};

/** Where a readout's pie stands (`CellMark::PIE`): in the corner the cells
leave at the screen's foot right, where the cell's decimal point would stand
and the hex mark (`drawHexMark`) stands at its head. Its lowest point is the
lowest of the digits' ink, an 8's lower segment's edge (`SEG_EIGHT`), and it
keeps the margin the digits keep from the glass (`READOUT_MARGIN`, half of
it each side) from the glass's right edge and from the last digit's ink
alike (`SEG_BEARING` inside its advance), so the two gaps are one; its width
is what they leave, never taller than an 8. `r` its radius. The painter and
a hand's press ask the same. */
inline Vec pieAt(const CellsLayout& l, float& r) {
	const float gap = mm2px(READOUT_MARGIN) / 2.f, foot = l.y + l.px * SEG_EIGHT / 2.f;
	const float from = l.x + l.cell * (float)l.cells - l.px * SEG_BEARING + gap, to = l.w - gap;
	r = clamp((to - from) / 2.f, 0.f, l.px * SEG_EIGHT / 2.f);
	return Vec((from + to) / 2.f, foot - r);
}

/** A number on a screen: the one readout painter, which every number of ours
goes through.

The digits go on `cells` seven-segment cells with their ghosts behind them,
right-aligned as a readout's digits are, so a short value never straddles two
ghosts and a number that grows never shuffles sideways. Every cell is placed
by the ghost's ADVANCE, never by the ink it happens to cover: a seven-segment
1 lights only the right-hand pair of segments, so aligning on ink slid a
value a cell sideways depending on its last digit. The block of cells is
centred on the screen and shrunk, never grown, until it fits.

Either end cell may hold a mark instead of a digit (`CellMark`), drawn as
segments on that cell, because a seven-segment cell has no x and no arrow and
a sign set beside the digits in another face reads as a letter beside a
readout: the Seq's direction, a rate's x or slash. The layout is returned for
what else a screen sets against its cells. */
inline CellsLayout drawNumber(NVGcontext* vg, Vec size, const std::string& digits, NVGcolor col, int cells,
                              CellMark first = CellMark(), CellMark last = CellMark()) {
	CellsLayout l;
	std::shared_ptr<Font> seg = segFont();
	if (!seg || cells < 1)
		return l;
	nvgFontFaceId(vg, seg->handle);
	nvgFontSize(vg, l.px);
	nvgTextLetterSpacing(vg, 0.f);
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
	float cell = nvgTextBounds(vg, 0.f, 0.f, "8", nullptr, nullptr);
	const float fit = readoutFit(size, cell * (float)cells, l.px * SEG_INK);
	if (fit < 1.f) {
		l.px *= fit;
		cell *= fit;
	}
	l.x = (size.x - cell * (float)cells) / 2.f;
	l.y = size.y / 2.f;
	l.cell = cell;
	l.cells = cells;
	l.w = size.x;
	l.ok = true;
	// The digits' cells, between the marks.
	const int from = first.ownCell(), to = cells - last.ownCell();
	const std::string ghost((size_t)(to - from), '8');
	nvgFontSize(vg, l.px);
	nvgFillColor(vg, color::alpha(col, 0.15f));
	nvgText(vg, l.x + l.cell * (float)from, l.y, ghost.c_str(), nullptr);
	nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
	nvgFillColor(vg, col);
	nvgText(vg, l.x + l.cell * (float)to, l.y, digits.c_str(), nullptr);
	// The marks, on the end cells.
	const float h = l.px * SEG_INK;
	const CellMark* ends[2] = {&first, &last};
	for (int e = 0; e < 2; e++) {
		const CellMark& m = *ends[e];
		const float cx = l.x + l.cell * (e ? (float)cells - 0.5f : 0.5f);
		if (m.kind == CellMark::DIRECTION)
			drawSegDir(vg, cx, l.y, h, l.cell, col, m.lit, m.hot);
		else if (m.kind == CellMark::PIE && e) {
			float r;
			const Vec c = pieAt(l, r);
			paintPie(vg, c.x, c.y, r, m.lit, m.whole, col);
		}
		else if (m.kind == CellMark::SIGN)
			drawSegSign(vg, cx, l.y, h, col, m.lit);
		else if (m.kind == CellMark::SIGNED) {
			// The face's own minus, so the bar is a digit's middle segment
			// exactly, set on its cell as the digits are, by the right edge.
			const float right = l.x + l.cell * (e ? (float)cells : 1.f);
			nvgFillColor(vg, (m.lit & 1) ? col : color::alpha(col, 0.15f));
			nvgText(vg, right, l.y, "-", nullptr);
			// The arms, read off the face's minus (DSEG7 Classic Mini, in
			// thousandths of the point size: the bar 468 long and 124 thick,
			// its middle 408 left of the advance's end and, the face's
			// ascender being the whole em, on the line's middle), so the
			// plus stands as tall as it is wide, its ends pointed as the
			// bar's are.
			const float t = l.px * 0.062f, arm = l.px * 0.234f, x = right - l.px * 0.408f;
			nvgBeginPath(vg);
			for (int s = -1; s <= 1; s += 2) {
				nvgMoveTo(vg, x - t, l.y + s * t);
				nvgLineTo(vg, x - t, l.y + s * (arm - t));
				nvgLineTo(vg, x, l.y + s * arm);
				nvgLineTo(vg, x + t, l.y + s * (arm - t));
				nvgLineTo(vg, x + t, l.y + s * t);
				nvgClosePath(vg);
			}
			nvgFillColor(vg, (m.lit & 2) ? col : color::alpha(col, 0.15f));
			nvgFill(vg);
		}
	}
	return l;
}

/** The mark that says the cells before it are hex: a small square lit at the
top right of the last cell, where no segment and no decimal point stands, so
step 100, which two cells of hex write 64, never reads as sixty-four. */
inline void drawHexMark(NVGcontext* vg, const CellsLayout& l, NVGcolor col) {
	if (!l.ok)
		return;
	const float h = l.px * SEG_INK, t = h * 0.13f;
	nvgBeginPath(vg);
	nvgRect(vg, l.x + l.cell * (float)l.cells + t * 0.6f, l.y - h * 0.42f, t, t);
	nvgFillColor(vg, col);
	nvgFill(vg);
}

/** A count on its cells, the one way every count a screen shows past what
its cells hold in decimal goes on, an explicit hexadecimal with a little
marking saying the screen is small and is still pushed further: decimal while the cells hold it, 99 on two, 999 on three,
then hex with the mark lit (`drawHexMark`), 64 for a hundred, up to the most
the cells hold in hex, FF on two and FFF on three, and held there. The hex
letters are a seven-segment cell's, b and d in lower case so neither reads as
an 8 or a 0. No leading zero, as every number on a screen. A count of its own
cap is the module's: this only writes what it is given, and a cap that means
something (an Atn's unity, a CV's ten volts, cents within a semitone) never
comes here. `cells` is the digits' cells alone, the marks' not counted. */
inline int countDecimalMax(int cells) {
	int m = 1;
	for (int i = 0; i < cells; i++)
		m *= 10;
	return m - 1;
}
inline int countHexMax(int cells) { return (1 << (4 * cells)) - 1; }
inline bool countInHex(int n, int cells) { return n > countDecimalMax(cells); }
inline std::string countCells(int n, int cells) {
	static const char digits[] = "0123456789AbCdEF";
	const int v = clamp(n, 0, countHexMax(cells));
	if (!countInHex(v, cells))
		return std::to_string(v);
	std::string s;
	for (int x = v; x > 0; x >>= 4)
		s.insert(s.begin(), digits[x & 0xf]);
	return s;
}
/** And drawn, on `cells` cells in all, the marks at either end taking theirs
(a direction, a sign) as `drawNumber` places them; the hex mark lit where the
count has gone past decimal. A count under nought is none: the ghosts alone,
as a screen with nothing to say shows. */
inline CellsLayout drawCount(NVGcontext* vg, Vec size, int n, NVGcolor col, int cells, CellMark first = CellMark(),
                             CellMark last = CellMark()) {
	const int digits = cells - first.ownCell() - last.ownCell();
	const CellsLayout l = drawNumber(vg, size, n < 0 ? std::string() : countCells(n, digits), col, cells, first, last);
	if (n >= 0 && countInHex(n, digits))
		drawHexMark(vg, l, col);
	return l;
}

/** The mark that names a readout's unit, HZ or kHZ on a Clock in hertz: the
plate's face, small, standing on the cells' foot just right of the last one,
as a unit is printed after a number on a meter's glass. */
inline void drawUnitMark(NVGcontext* vg, const CellsLayout& l, const char* unit, NVGcolor col) {
	std::shared_ptr<Font> font = plateFont();
	if (!l.ok || !font)
		return;
	const float h = l.px * SEG_INK;
	nvgFontFaceId(vg, font->handle);
	nvgFontSize(vg, l.px * 0.42f);
	nvgTextLetterSpacing(vg, 0.f);
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
	nvgFillColor(vg, col);
	nvgText(vg, l.x + l.cell * (float)l.cells + h * 0.08f, l.y + h * 0.5f, unit, nullptr);
}

/** The mark that says a count is in degrees of a pattern rather than in
semitones: a small ring at the top right of the last cell, as a degree sign
stands after a number, with a little air between them so it reads as a mark
of its own and not a part of the last digit. Lit, the count is degrees; dark,
a ghost as a segment's is, the count is semitones; under the pointer (`hot`)
the ghost brightens, to say it is a button. Drawn finer than a segment, a
sign beside the digits rather than one of them. `degreeMarkBox` is where it stands, the one place
that says so, which the Qnt's button is placed by. */
inline Rect degreeMarkBox(const CellsLayout& l) {
	const float h = l.px * SEG_INK, t = h * 0.08f, r = h * 0.12f;
	const float x = l.x + l.cell * (float)l.cells + h * 0.12f, y = l.y - h * 0.5f;
	return Rect(Vec(x, y), Vec(2.f * r + t, 2.f * r + t));
}
inline void drawDegreeMark(NVGcontext* vg, const CellsLayout& l, NVGcolor col, bool lit, bool hot) {
	if (!l.ok)
		return;
	const Rect b = degreeMarkBox(l);
	const float t = l.px * SEG_INK * 0.08f;
	nvgBeginPath(vg);
	nvgCircle(vg, b.getCenter().x, b.getCenter().y, (b.size.x - t) / 2.f);
	nvgStrokeWidth(vg, t);
	nvgStrokeColor(vg, lit ? col : color::alpha(col, hot ? 0.4f : 0.15f));
	nvgStroke(vg);
}

/** The mark that says a reset is armed: a small circle of arrow at the top
left of the step screen, lit while the Seq waits on the next pulse to sound
step one, the cells meanwhile naming the step still playing: the screen shows
what is actually playing, and the arm is told by a mark rather than by the
cells jumping to one. */
inline void drawArmMark(NVGcontext* vg, const CellsLayout& l, NVGcolor col) {
	if (!l.ok)
		return;
	const float h = l.px * SEG_INK, r = h * 0.15f, w = h * 0.06f;
	const float cx = std::max(l.x * 0.5f, r + w), cy = l.y - h * 0.5f + r;
	// Three quarters of a turn and more, open at the top right, the head on
	// its end pointing on round.
	const float a0 = -0.5f * (float)M_PI + 0.45f, a1 = a0 + 1.6f * (float)M_PI;
	nvgBeginPath(vg);
	nvgArc(vg, cx, cy, r, a0, a1, NVG_CW);
	nvgStrokeColor(vg, col);
	nvgStrokeWidth(vg, w);
	nvgLineCap(vg, NVG_BUTT);
	nvgStroke(vg);
	const float ex = cx + r * std::cos(a1), ey = cy + r * std::sin(a1);
	const float tx = -std::sin(a1), ty = std::cos(a1); // along the arc, the way it turns
	const float nx = std::cos(a1), ny = std::sin(a1);  // out from the centre
	const float k = w * 2.2f;
	nvgBeginPath(vg);
	nvgMoveTo(vg, ex + tx * k * 1.1f, ey + ty * k * 1.1f);
	nvgLineTo(vg, ex + nx * k, ey + ny * k);
	nvgLineTo(vg, ex - nx * k, ey - ny * k);
	nvgClosePath(vg);
	nvgFillColor(vg, col);
	nvgFill(vg);
}
