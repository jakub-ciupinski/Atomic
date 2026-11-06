// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "plugin.hpp"
// Before anything that draws: it lays a recording layer over the NanoVG
// verbs, so every call this plugin makes can be written down as text. See
// Trace.hpp for why that is worth more than a picture.
#include "Trace.hpp"
#include "GapSkin.hpp"
#include "Switches.hpp"
#include <atomic>
#include <cctype>
#include <functional>
#include <memory>

/** The look: what a panel of ours is made of, in one place.

Everything here is a pure paint function: a context, some numbers, and
nothing else. No widget is drawn, no module is read, nothing is remembered
between calls. That is the whole rule of this file, and it is what makes the
look answerable: a panel can be rendered offscreen and compared pixel for
pixel, and the drawing cannot be wrong for a reason that lives somewhere
else. The switches a painter asks, `dev()` and `livePalette()`, are the
DEV's and the PAL's state and live in Switches.hpp, beside `palette()`, the
rotation read from res/palette.json. The one thing held here is
drawGlassLayer's LedDisplay, because Rack's stock screen is drawn by a
widget and drawing one means having one; it carries nothing from one call to
the next but the size set before each draw.

The metrics come first, because they are the grammar: one jack height, one
screen top and bottom, one column of caption and control positions that every
module places against. A rig of these has to read as a single instrument, and
that only happens if every module agrees on where things sit. */

namespace rig {

// ---- Shared drawing: the Fundamental plate, faithfully ----------------------

/** Whether the panels are dark: Rack's own "Use dark panels if available",
the one switch, read wherever a colour is chosen, so a panel turns the frame
the switch is thrown, as the stock panels do. */
inline bool darkPanels() { return settings::preferDarkPanels; }
/** A colour of the plate and what stands on it, as the stock modules have it
light and dark: it is the light one or the dark one wherever it is used as a colour, and
nothing else ever chooses between them. */
struct Tone {
	NVGcolor light, dark;
	operator NVGcolor() const { return darkPanels() ? dark : light; }
};

const float PANEL_W = 3 * 5.08f;
const float CX = PANEL_W / 2.f;
// The plate's captions: ink on the plate, pale on the dark block at the foot.
const Tone INK = {nvgRGB(0x1f, 0x1f, 0x1f), nvgRGB(0xeb, 0xeb, 0xeb)};
const Tone PALE = {nvgRGB(0xf0, 0xf0, 0xf0), nvgRGB(0x1f, 0x1f, 0x1f)};
/** A screen's own face, the dark every piece of ground is painted in. */
const NVGcolor SCREEN_FACE = nvgRGB(0x0d, 0x0d, 0x0d);

const float CAPTION_PX = 8.f;
const float PALE_CAPTION_PX = 8.5f; // a caption on a block: see drawCaption
/** The vertical rhythm, written once.

A caption belongs to the thing under it, and sits a fixed distance above it:
one distance for a jack, a larger one for a knob, because a knob is wider than
its caption and needs the air. An output block opens a little above its first
caption and closes at the same height on every panel, so the plate under a
caption is the same depth wherever the block begins.

Modules place against these rather than typing millimetres, which is the only
way a rig of them reads as one instrument. Where a module still types its own
number, that is a module not yet brought into the rhythm, not a licence. */
/* Where the panel opens and closes is fixed from outside. The screens open
where the stock panels' screens open: Fundamental sets every one of its
displays 38.5 units down a panel 380 units tall, and a rig of ours standing
beside a Scope or a Viz has to agree with it or the whole rack reads as two
instruments. The output block closes one grid unit over the bottom edge.
Everything between the two is ours to space.

The millimetres a panel gives below its screen are measured down a column a
millimetre and four fifths taller than that one, opening at `RHYTHM_TOP`
over the same foot. They are squeezed into the real column rather than
typed again: every distance down it takes the same small share off, so a row
of jacks still lines up across four panels and the pair at the foot is still
exactly where a patch expects it. */
const float BRACKET_H = 5.08f;                  // one grid unit, the bracket's depth
const float SCREEN_Y = 38.5f * 25.4f / 75.f; // 13.04, the stock display's own top edge
const float BLOCK_FOOT = 128.5f - 5.08f;     // a block closes here: see the foot, below
// The foot: the output block closes as far above the bottom screw's head as
// the head stands from the panel's edge, which is one grid unit up whatever
// the head's radius, and the jack at the foot stands over that.
const float FOOT_Y = BLOCK_FOOT - (122.f - 116.5f); // the jack at the foot, the plate under it
const float RHYTHM_TOP = 11.224f;            // the top of the column a panel's millimetres are given in
const float COLUMN = (FOOT_Y - SCREEN_Y) / (FOOT_Y - RHYTHM_TOP); // 0.983
/** A distance down the column, squeezed with it. Every millimetre measured
from the screen downwards goes through this, which is what keeps the
proportions of a panel while its top edge moves and its foot does not. */
inline float drop(float mm) {
	return mm * COLUMN;
}
/** And a height given on the column from `RHYTHM_TOP`, brought into the
real one: the same squeeze, measured up from the foot, which both share. */
inline float column(float y) {
	return FOOT_Y - drop(FOOT_Y - y);
}

// Air: the clear plate a caption wants on each side of it, and the half of
// a caption's ink that has to be cleared. Both measured off a render (a
// caption set at CAPTION_PX stands about two millimetres of ink) and both
// the reason the numbers below are what they are rather than round.
const float CAP_INK = 1.f;   // half a caption's ink, mm
const float CAP_AIR = 0.4f;  // the plate wanted above a caption, and under it
const float JACK_R = 4.f;    // a PJ301M's radius

// A caption sits its own half plus its air above the thing it names, and one
// control row to the next is that twice, so the air above a caption is the
// air below it, everywhere, on every panel. That is the whole of the rhythm:
// it was 5.5 over a jack at a pitch of 11, which centred the caption between
// the jacks and left it a tenth of a millimetre from each: the squeeze that
// made a BPM CV read as belonging to the RESET above it.
const float CAP_ABOVE_JACK = JACK_R + CAP_INK + CAP_AIR;  // 5.4
// The pitch was that twice, which tied the two together: a caption could
// only move closer to its jack by bringing the rows closer as well. They are
// two numbers now, and they went opposite ways (the caption a tenth nearer
// the thing it names, the rows an eighth further apart) because a caption
// belongs to its jack and should read as attached to it, while two jacks
// belong to nothing and want air. The two jacks at the foot of every panel
// stand a little closer than the rows above them: the pair reads as the
// module's in and out, together. It is the one pitch that did not squeeze:
// the foot of every panel is exactly where it was, so the pair standing on
// it is typed rather than derived.
const float FOOT_PITCH = 12.5f;
// A block standing on the pair of jacks at the foot opens a little further
// above its first caption than the lip alone gives, for air. It was the
// Seq's own number for a while, which meant the Seq's output block and the
// two-jack block every other head module draws opened six tenths of a
// millimetre apart on the same row: near enough to look like a mistake
// rather than a difference, which is what it was.
const float BLOCK_AIR = 0.6f;
const float BLOCK_LIP = CAP_INK + CAP_AIR; // a block opens this far above its caption
                                           // and closes at BLOCK_FOOT, above

inline float capAbove(float jackY) {
	return jackY - CAP_ABOVE_JACK;
}
inline float blockTop(float firstCapY) {
	return firstCapY - BLOCK_LIP;
}
inline float blockHeight(float firstCapY) {
	return BLOCK_FOOT - blockTop(firstCapY);
}

// The module's most logical input stands at the foot, one pitch above the
// output, so a patch runs in and out at the same end of every panel, and the
// screens start right under the title. They all start at the same height and
// end at the same height, because a rig of them has to read as one surface.
//
// The title is centred in the plate the bracket and the screen leave it: the
// same air over the ink as under it, and the baseline follows from that
// rather than being typed in pixels, so it stays centred if the band changes
// again.
const float TITLE_INK = 3.f;                                   // the cap height, near enough
const float TITLE_AIR = (SCREEN_Y - BRACKET_H - TITLE_INK) / 2.f; // the air over the title
const float TITLE_BASE = BRACKET_H + TITLE_AIR + TITLE_INK;       // and the same under it: 10.56
const float IN_Y = FOOT_Y - FOOT_PITCH;       // the IN jack, over the foot
const float IN_CAP_Y = capAbove(IN_Y);
// A screen closes higher than a block opens. A caption under a block has
// bare plate over it; a caption under a screen is read against the dark, and
// wants a little more air than the block's lip to stand clear of it.
const float SCREEN_AIR = BLOCK_LIP + 1.f;
const float SCREEN_BOTTOM = IN_CAP_Y - SCREEN_AIR;
const float SCREEN_H = SCREEN_BOTTOM - SCREEN_Y;

// Where every module's first jack stands under its screen; the rows under it
// are the Seq's column below, which is the one column there is, so no jack
// stands a few tenths of a millimetre off the row.
//
// It stands half a caption's clearance lower than the strip under a Clock's
// or a Seq's screens closes against, so BPM and PHASE do not sit too close
// under RUN and PREVIEW. The clearance is the plate a caption leaves between
// its ink and the jack above it on the column as it stood, and the rows
// between the first and the foot's pair divide the plate evenly again, so
// every module's jacks move with it and the foot's pair, the anchor, stays
// exactly where it was.
const float STRIP_ROW1_Y = SCREEN_Y + drop(26.f); // where the first jack stood before the strip, which the strip still closes on
const float CAP_CLEAR = (FOOT_Y - FOOT_PITCH - STRIP_ROW1_Y) / 5.f - JACK_R - CAP_ABOVE_JACK - CAP_INK; // 2.97
const float HEAD_ROW1_Y = STRIP_ROW1_Y + CAP_CLEAR / 2.f;
// The Seq's own column. Seven jacks stand under its two screens (PHASE, RATE,
// RESET, CLK, IN and the two outputs) and a column that full cannot keep the
// shared pitch and still close on the jack at the foot, which every panel
// puts in the same place. So the first six take one pitch of their own, the
// plate between the first and the jack over the foot divided evenly, and the
// last stands at the foot's own pitch under it. RESET to the pulse jack was
// the gap that gave this away: the outputs counted up from the foot at the
// shared pitch and the inputs counted down from the screens at it, and the
// two counts met wherever they met, which was two and a half millimetres
// short. It lives here rather than in Seq.cpp because a panel standing beside
// a Seq places against it: an Atn's jacks sit on the Clock's CLK row and on
// its RESET row.
const float SEQ_ROW1_Y = HEAD_ROW1_Y;                             // where every module's first jack stands
const float SEQ_PITCH = (FOOT_Y - FOOT_PITCH - SEQ_ROW1_Y) / 5.f; // seven jacks, the last two at the foot's own pitch
inline float seqRowY(int i) {
	return i == 6 ? FOOT_Y : i == 5 ? FOOT_Y - FOOT_PITCH : SEQ_ROW1_Y + i * SEQ_PITCH;
}
inline float seqCapY(int i) { return capAbove(seqRowY(i)); }
const float FOOT_CAP_Y = capAbove(FOOT_Y);
const float BLOCK_Y = blockTop(FOOT_CAP_Y), BLOCK_H = blockHeight(FOOT_CAP_Y);

inline std::shared_ptr<Font> plateFont() {
	static const std::string path = asset::system("res/fonts/Nunito-Bold.ttf");
	return APP->window->loadFont(path);
}
/** The seven-segment face every readout's digits are set in. */
inline std::shared_ptr<Font> segFont() {
	static const std::string path = asset::system("res/fonts/DSEG7ClassicMini-Bold.ttf");
	return APP->window->loadFont(path);
}
/** The heavier cut for a caption on a block. Fundamental's output labels are
a third thicker in the stem than its plate labels at the same height,
measured at eight times beside a VCF: pale on dark is set in a heavier
weight, and Rack ships only the Bold, so the ExtraBold is ours, in res, an
instance of the Nunito variable font at weight 800 under the OFL. */
inline std::shared_ptr<Font> blockFont() {
	static const std::string path = asset::plugin(pluginInstance, "res/fonts/Nunito-ExtraBold.ttf");
	return APP->window->loadFont(path);
}

/** The clock every animation reads on the UI thread: the frame's own stamp,
so all a frame draws agrees on the moment and a loaded machine moves an
animation by whole frames, the wall clock only where no window stands. */
inline double frameNow() {
	return APP && APP->window ? APP->window->getFrameTime() : system::getTime();
}

/** A light that breathes, by the frame's clock (`frameNow`): from `floor` up to
full and back once every `period` seconds, a cosine between. A traced frame
is written down as text and compared with a stored one, so it draws the breath at
its height rather than wherever the wall clock has it: otherwise no two runs
of a breathing panel trace alike. */
inline float breath(double period, float floor) {
	if (trace::recording())
		return 1.f;
	const double ph = std::fmod(frameNow(), period) / period;
	return floor + (1.f - floor) * (0.5f - 0.5f * (float)std::cos(2.0 * M_PI * ph));
}

/** What is left of the flash that answers a press made at `pressedAt`
(`frameNow`'s seconds, below nought for none), fading from full to
nothing over `length` seconds. Spent in a traced frame, as it is by any
capture a wait after the press, so a trace taken while the fork outruns the
wall clock never catches the fade part way. */
inline float pressFlash(double pressedAt, double length) {
	if (pressedAt < 0.0 || trace::recording())
		return 0.f;
	return clamp(1.f - (float)((frameNow() - pressedAt) / length), 0.f, 1.f);
}

// The title's baseline, in pixels from the top of the plate.
const float TITLE_Y = TITLE_BASE * 75.f / 25.4f; // 31.18
/** One size for every title: a name is at most five letters. */
inline void setTitleFont(NVGcontext* vg, const std::shared_ptr<Font>& font) {
	nvgFontFaceId(vg, font->handle);
	nvgFontSize(vg, 12.5f);
	nvgTextLetterSpacing(vg, 0.6f);
}

/** A word set in `font`: its size, spacing, alignment and ink, then the
word at x, y. Every one-shot word on a panel is set through it, so the calls
come in one order wherever a word stands. */
inline void drawWord(NVGcontext* vg, const std::shared_ptr<Font>& font, float px, float spacing, int align, NVGcolor ink,
                     float x, float y, const char* word) {
	nvgFontFaceId(vg, font->handle);
	nvgFontSize(vg, px);
	nvgTextLetterSpacing(vg, spacing);
	nvgTextAlign(vg, align);
	nvgFillColor(vg, ink);
	nvgText(vg, x, y, word, nullptr);
}

/** The plate's own gradient, down a panel `h` tall from `top`: every piece
of plate is painted with it, so a run's plate has no step at a join. */
const Tone PLATE_TOP = {nvgRGB(0xeb, 0xeb, 0xeb), nvgRGB(0x2a, 0x2a, 0x2b)};
const Tone PLATE_FOOT = {nvgRGB(0xe1, 0xe1, 0xe1), nvgRGB(0x17, 0x17, 0x17)};
inline NVGpaint plateGradient(NVGcontext* vg, float h, float top = 0.f) {
	return nvgLinearGradient(vg, 0.f, top, 0.f, top + h, PLATE_TOP, PLATE_FOOT);
}
/** A colour as the room's light leaves it: the lit layer is drawn after
Rack has darkened the rack, so a patch of plate laid in it, over a name the
lit layer replaces, is dimmed by hand to match the plate around it. */
inline NVGcolor dimmed(NVGcolor c, float room) { return nvgRGBf(c.r * room, c.g * room, c.b * room); }
inline NVGpaint plateGradientIn(NVGcontext* vg, float room) {
	return nvgLinearGradient(vg, 0.f, 0.f, 0.f, RACK_GRID_HEIGHT, dimmed(PLATE_TOP, room), dimmed(PLATE_FOOT, room));
}

/** The plate, and the band it leaves unpainted for an edge-to-edge screen,
in pixels: a screen that runs edge to edge must not have plate under it, or
the same pixels are painted twice and the plate's grey comes through the
screen's own clipped edge as a light line down the join. */
/** The dark plate of a tool rather than an instrument, the DEV's and the
PAL's: no ground, no bracket, its title small and grey near the top. */
inline void drawToolPlate(NVGcontext* vg, Vec size, const char* title) {
	nvgBeginPath(vg);
	nvgRect(vg, 0.f, 0.f, size.x, size.y);
	nvgFillPaint(vg, nvgLinearGradient(vg, 0.f, 0.f, 0.f, size.y, nvgRGB(0x1c, 0x1d, 0x20), nvgRGB(0x0f, 0x10, 0x12)));
	nvgFill(vg);
	std::shared_ptr<Font> font = plateFont();
	if (!font)
		return;
	drawWord(vg, font, 12.f, 1.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, nvgRGB(0x8a, 0x8a, 0x8a), size.x / 2.f, mm2px(8.f), title);
}

/** `over`, when given, is a word standing in for the title for a moment
(the Qnt's mode as it changes): at `weight` one it is all there is, at nought
the title alone, and between the two are laid one over the other, each at its
share, so the word fades back into the title. An empty word is the title
fading to nothing, the GAP's after it is placed. */
inline void drawPlate(NVGcontext* vg, Vec size, const char* title, float bandY = -1.f,
                      float bandH = 0.f, bool fill = true, const char* over = nullptr, float weight = 0.f) {
	if (!dev().statics)
		return;
	// The band is left out by drawing the plate above and below it rather
	// than by cutting a hole: a hole is filled through the stencil buffer,
	// and a cached panel is rendered into a framebuffer that has none, where
	// the hole silently does not happen and grey ends up behind the screen.
	// `fill` is false when the run this panel stands in has already painted
	// the plate of the whole rig this frame: the panel then puts down only
	// its title, and lays nothing over the ground the run painted for it.
	if (fill) {
		nvgBeginPath(vg);
		if (bandH > 0.f) {
			nvgRect(vg, 0.f, 0.f, size.x, std::max(bandY, 0.f));
			nvgRect(vg, 0.f, bandY + bandH, size.x, std::max(size.y - bandY - bandH, 0.f));
		}
		else {
			nvgRect(vg, 0.f, 0.f, size.x, size.y);
		}
		nvgFillPaint(vg, plateGradient(vg, size.y));
		nvgFill(vg);
	}

	std::shared_ptr<Font> font = plateFont();
	if (!font)
		return;
	setTitleFont(vg, font);
	nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
	const NVGcolor ink = INK;
	const float w = over ? clamp(weight, 0.f, 1.f) : 0.f;
	if (w < 1.f) {
		nvgFillColor(vg, color::alpha(ink, 1.f - w));
		nvgText(vg, size.x / 2.f, TITLE_Y, title, nullptr);
	}
	if (w > 0.f && over[0]) {
		nvgFillColor(vg, color::alpha(ink, w));
		nvgText(vg, size.x / 2.f, TITLE_Y, over, nullptr);
	}
}

inline void drawCaption(NVGcontext* vg, float xMm, float yMm, const char* s, NVGcolor col,
                        float px = CAPTION_PX) {
	if (!dev().statics)
		return;
	// Pale ink is ink on a block, and a block's word is set in the heavier
	// cut, as Fundamental sets its own: see blockFont.
	const NVGcolor p = PALE;
	const bool pale = col.r == p.r && col.g == p.g && col.b == p.b;
	std::shared_ptr<Font> font = pale ? blockFont() : plateFont();
	if (!font)
		return;
	// Pale on dark reads smaller than the same letters dark on light, and
	// the heavier cut's ink stands a shade shorter besides, so a block's
	// word is set a little larger to read the same size as a plate's.
	drawWord(vg, font, pale && px == CAPTION_PX ? PALE_CAPTION_PX : px, 0.4f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, col,
	         mm2px(xMm), mm2px(yMm), s);
}

/** A caption with a quiet afterthought: `main` as `drawCaption` sets it, and
`aside` hanging after it, smaller and greyed, the word centred where it always
stood, so from afar the word reads alone (the Poly's and the Mix's IN(S),
whose bracket read as a symbol). Ink on the plate only. */
inline void drawCaptionAside(NVGcontext* vg, float xMm, float yMm, const char* main, const char* aside, NVGcolor col) {
	if (!dev().statics)
		return;
	std::shared_ptr<Font> font = plateFont();
	if (!font)
		return;
	const float small = CAPTION_PX * 0.72f;
	nvgFontFaceId(vg, font->handle);
	nvgTextLetterSpacing(vg, 0.4f);
	nvgFontSize(vg, CAPTION_PX);
	float b[4];
	const float wm = nvgTextBounds(vg, 0.f, 0.f, main, nullptr, b);
	nvgFontSize(vg, small);
	const float wa = nvgTextBounds(vg, 0.f, 0.f, aside, nullptr, b);
	// The word centred as every caption is, so IN stands where every IN
	// stands, and the afterthought hangs off it.
	(void)wa;
	const float gap = 0.6f, x0 = mm2px(xMm) - wm / 2.f, y = mm2px(yMm);
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
	// One baseline, the caption's own: its middle less half its cap height.
	const float base = y + CAPTION_PX * 0.36f;
	nvgFontSize(vg, CAPTION_PX);
	nvgFillColor(vg, col);
	nvgText(vg, x0, base, main, nullptr);
	nvgFontSize(vg, small);
	nvgFillColor(vg, color::alpha(col, 0.45f));
	nvgText(vg, x0 + wm + gap, base, aside, nullptr);
}

/** The plugin's name, as the bracket spells it and a tool's plate signs it. */
const char* const BRAND = "ATOMIC";

inline void drawBrand(NVGcontext* vg, Vec size) {
	if (!dev().statics)
		return;
	std::shared_ptr<Font> font = plateFont();
	if (!font)
		return;
	drawWord(vg, font, 11.f, 0.3f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, INK, size.x / 2.f, mm2px(120.5f),
	         BRAND);
}

/** The mark that stands where our screws would: a ring, a nucleus, and one
electron riding the ring at its upper right, hollow, with the plate showing
through it. Every bolt is drawn at the bracket bolt's size, on the bracket and at a
panel's foot alike, which is a little smaller than a stock screw. The
proportions were settled on paper: in a mark 41 units to its outer edge, the
ring runs at 38 and is 6 thick, the nucleus is 11 across the radius, and the
electron is 9 with a line of 3, set 26.9 along each axis.

It is not drawn as paths. At Rack's own zoom the ring is under a pixel wide
and the nucleus two, and NanoVG's edge fringe is only good to about a pixel:
a stroke thinner than that is faded by the square of its width, and the ring
came out a chain of light and dark beads wherever it crossed the pixel grid.
So the mark is worked out once as a picture, coverage sampled nine times a
texel, with every smaller size of it made by averaging, and drawn as an image
that the card shrinks the way a browser shrinks a picture. The electron's
hollow is a hole in it, so whatever stands under it shows through. */
const float ATOM_R = mm2px(1.5f);
const float ATOM_PAD = 44.f; // the picture's half width, in the mark's units: room for the filtering
/** The mark's ink, ours rather than a stock colour: the share of the way
from the plate's ink to the plate it stands at light, kept in the dark. */
const Tone ATOM_INK = {nvgRGB(0x2a, 0x2a, 0x2a), nvgRGB(0xe1, 0xe1, 0xe1)};
/** The bracket's name and RESET, and the hint line's words, found the same way. */
const Tone BRACKET_NAME_INK = {nvgRGB(0x6e, 0x6e, 0x6e), nvgRGB(0xa0, 0xa0, 0xa0)};
const Tone HINT_WORDS = {nvgRGB(0x4a, 0x4a, 0x4a), nvgRGB(0xc2, 0xc2, 0xc2)};
inline int atomImage(NVGcontext* vg) {
	static std::map<std::pair<NVGcontext*, bool>, int> images;
	const bool dark = darkPanels();
	int& img = images[{vg, dark}];
	int w = 0, h = 0;
	if (img)
		nvgImageSize(vg, img, &w, &h);
	if (w > 0)
		return img;
	const int N = 256, SS = 3;
	const float ink = ((NVGcolor) ATOM_INK).r;
	std::vector<unsigned char> px((size_t)N * N * 4);
	for (int j = 0; j < N; j++)
		for (int i = 0; i < N; i++) {
			int hits = 0;
			for (int sj = 0; sj < SS; sj++)
				for (int si = 0; si < SS; si++) {
					const float x = ((i + (si + 0.5f) / SS) / N * 2.f - 1.f) * ATOM_PAD;
					const float y = ((j + (sj + 0.5f) / SS) / N * 2.f - 1.f) * ATOM_PAD;
					const float d = std::hypot(x, y);
					const float e = std::hypot(x - 26.9f, y + 26.9f);
					const bool shape = (d >= 35.f && d <= 41.f) || d <= 11.f || e <= 10.5f;
					hits += shape && e >= 7.5f;
				}
			const float a = (float)hits / (SS * SS);
			unsigned char* q = &px[((size_t)j * N + i) * 4];
			q[0] = q[1] = q[2] = (unsigned char) std::lround(ink * a * 255.f); // premultiplied
			q[3] = (unsigned char) std::lround(a * 255.f);
		}
	img = nvgCreateImageRGBA(vg, N, N, NVG_IMAGE_GENERATE_MIPMAPS | NVG_IMAGE_PREMULTIPLIED, px.data());
	return img;
}
/** The mark centred at `cx, cy`, `ATOM_R` to its ring's outer edge and
whole: the bolts and the bracket's name. */
inline void paintAtom(NVGcontext* vg, float cx, float cy) {
	const int img = atomImage(vg);
	if (!img)
		return;
	const float r = ATOM_R / 41.f * ATOM_PAD;
	nvgBeginPath(vg);
	nvgRect(vg, cx - r, cy - r, 2.f * r, 2.f * r);
	nvgFillPaint(vg, nvgImagePattern(vg, cx - r, cy - r, 2.f * r, 2.f * r, 0.f, img, 1.f));
	nvgFill(vg);
}

/** A dimple of the bracket's grip (`paintBracket`), pressed into its face: its
radius, and the fill that inks a path of them in the grip's grey. A GAP's
Perforated skin is the same dimple on the plate. */
const float DIMPLE_R = mm2px(0.28f);
/** The grey of a hole in Rack's own rail, light and dark. */
const Tone DIMPLE_INK = {nvgRGB(0xc4, 0xc4, 0xc4), nvgRGB(0x1a, 0x1a, 0x1a)};
inline void fillDimples(NVGcontext* vg) {
	nvgFillColor(vg, DIMPLE_INK);
	nvgFill(vg);
}

/** The inset of every dark rounded thing on the plate, the out block and
the switches alike, so their edges line up down a panel. */
const float BLOCK_INSET = 2.77f;

const Tone BLOCK_INK = {nvgRGB(0x1f, 0x1f, 0x1f), nvgRGB(0xed, 0xed, 0xed)}; // the block's own colour
const float BLOCK_ROUND = 0.96f;                    // and its corner, in mm

/** The dark block Fundamental paints behind an OUT jack. */
/** The lines every stock panel has along its top and its bottom. Rack's
PanelBorder is a one pixel stroke of half grey at half strength just inside
the edge; over the plate's gradient that comes to these two greys, the top
of the plate being a shade lighter than the foot. Opaque, so a run can
declare them as ground and paint them once, merged, like its screens. The
sides are left alone on purpose: a run of ours is one plate with no edge
inside it. */
const Tone BORDER_TOP = {nvgRGB(0xb5, 0xb5, 0xb5), nvgRGB(0x55, 0x55, 0x55)};
const Tone BORDER_FOOT = {nvgRGB(0xb0, 0xb0, 0xb0), nvgRGB(0x4b, 0x4b, 0x4b)};

/** A tab standing out of an out block's edge, what a lamp on the edge
stands on (`addEdgeLamp`, Hand.hpp): a half round of the block's own
material centred on its left edge (`side` -1) or its right (1) at `yMm`,
over the plate. One that `meets` the block's top (-1) or its foot (1)
keeps its centre and reaches that edge exactly, the corner there square,
so the block's edge runs straight on into the round and leaves it at the
round's far point; one beside no corner is `EDGE_TAB_R`. */
const float EDGE_TAB_R = 1.3f; // mm
struct EdgeTab {
	float yMm;
	int side;
	int meets = 0;
};
/** The out block, and the tabs standing on it now: one outline, filled
once, so where a tab leaves the block there is one edge and no seam of two
shapes laid over each other. A corner no tab meets is round, its curve the
very one `nvgRoundedRect` draws, which is what a block with no tab is. */
inline void drawOutBlock(NVGcontext* vg, float yMm, float hMm, std::vector<EdgeTab> tabs = {}) {
	const float x = mm2px(BLOCK_INSET), w = mm2px(PANEL_W - 2 * BLOCK_INSET), y = mm2px(yMm), h = mm2px(hMm);
	const float round = mm2px(BLOCK_ROUND);
	nvgBeginPath(vg);
	if (tabs.empty())
		nvgRoundedRect(vg, x, y, w, h, round);
	else {
		const float l = x, r = x + w, t = y, b = y + h;
		const float pi = (float)M_PI;
		auto square = [&](int side, int end) {
			for (const EdgeTab& e : tabs)
				if (e.side == side && e.meets == end)
					return true;
			return false;
		};
		// A corner: a quarter turn of the block's round about its centre
		// (`cx`, `cy`), from `a0` a quarter back, or the corner's own point
		// where a tab meets it.
		auto corner = [&](int side, int end, float a0) {
			const float cx = side < 0 ? l : r, cy = end < 0 ? t : b;
			if (square(side, end))
				nvgLineTo(vg, cx, cy);
			else
				nvgArc(vg, cx - side * round, cy - end * round, round, a0, a0 - 0.5f * pi, NVG_CCW);
		};
		// A tab, a half turn out of its edge; its radius in pixels from its
		// centre's own, so a tab that meets an edge leaves or joins it there
		// exactly.
		auto tab = [&](const EdgeTab& e) {
			const float cy = mm2px(e.yMm);
			const float rad = e.meets < 0 ? cy - t : e.meets > 0 ? b - cy : mm2px(EDGE_TAB_R);
			nvgArc(vg, e.side < 0 ? l : r, cy, rad, e.side * -0.5f * pi + pi, e.side * -0.5f * pi, NVG_CCW);
		};
		std::sort(tabs.begin(), tabs.end(), [](const EdgeTab& p, const EdgeTab& q) { return p.yMm < q.yMm; });
		// Round as nvgRoundedRect goes: down the left edge, along the foot,
		// up the right edge and back along the top, each tab on the way.
		nvgMoveTo(vg, l, square(-1, -1) ? t : t + round);
		for (const EdgeTab& e : tabs)
			if (e.side < 0)
				tab(e);
		corner(-1, 1, pi);
		corner(1, 1, 0.5f * pi);
		for (auto e = tabs.rbegin(); e != tabs.rend(); ++e)
			if (e->side > 0)
				tab(*e);
		corner(1, -1, 0.f);
		corner(-1, -1, -0.5f * pi);
		nvgClosePath(vg);
	}
	nvgFillColor(vg, BLOCK_INK);
	nvgFill(vg);
}

/** The lamp in an edge tab, drawn in the light layer: a small disc of `col`
of radius `r` on the edge, with a soft glow round it while `lit`; dim while
not, a quarter of its colour, and brighter under the pointer (`hot`), as a
button's word is, so it reads as a control; on a dark panel, out is a
screen's black alone. */
/** Whether a dragging lamp draws its value as an arc round its rim, from
twelve o'clock clockwise, `fill` of the way round, in its colour (the slew's
time, a full turn at `SLEW_HALO_FULL_MS`). Off, the lamp draws no arc. */
constexpr bool SLEW_HALO = true;
const float SLEW_HALO_FULL_MS = 500.f;
inline void paintEdgeLamp(NVGcontext* vg, float xPx, float yPx, float r, NVGcolor col, bool lit, bool hot,
                          float fill = 0.f) {
	if (SLEW_HALO && lit && fill > 0.f) {
		const float a0 = -0.5f * (float)M_PI;
		nvgBeginPath(vg);
		nvgArc(vg, xPx, yPx, 1.6f * r, a0, a0 + 2.f * (float)M_PI * std::min(fill, 1.f), NVG_CW);
		nvgStrokeColor(vg, col);
		nvgStrokeWidth(vg, 1.f);
		nvgStroke(vg);
	}
	if (lit) {
		const float glow = 2.5f * r;
		nvgBeginPath(vg);
		nvgCircle(vg, xPx, yPx, glow);
		nvgFillPaint(vg, nvgRadialGradient(vg, xPx, yPx, r * 0.5f, glow, color::alpha(col, 0.4f),
		                                   color::alpha(col, 0.f)));
		nvgFill(vg);
	}
	// Out, on a dark panel's pale tab, it is the one dark of a screen's
	// face, the black of an empty jack and of a CV's panel, whatever the
	// module's colour: a quarter of a yellow there read as the lamp lit.
	nvgBeginPath(vg);
	nvgCircle(vg, xPx, yPx, r);
	nvgFillColor(vg, lit ? col : darkPanels() ? SCREEN_FACE : color::alpha(col, hot ? 0.5f : 0.25f));
	nvgFill(vg);
}

/** The same block, in a widget's own coordinates: what a small button on the
plate stands on. A button is a short output block with a word on it (the same
colour, the same corner, and, placed at BLOCK_INSET, the same width), so a panel
has one dark rectangle shape and two uses for it. */
inline void drawBlockFace(NVGcontext* vg, Vec size) {
	if (!dev().statics)
		return;
	nvgBeginPath(vg);
	nvgRoundedRect(vg, 0.f, 0.f, size.x, size.y, mm2px(BLOCK_ROUND));
	nvgFillColor(vg, BLOCK_INK);
	nvgFill(vg);
}

/** How strongly a clickable mark is inked, the alpha of its colour: a
`ScreenButton`'s word, a `GlyphButton`'s glyph, the TRG's pulse mark. Full
while `lit`, faint while it `can` take no click, brighter under the pointer
(`hot`) than at rest, so every mark a hand presses reads one way. A readout's
ghost segments and a lamp keep sets of their own, being no button. */
inline float markInk(bool lit, bool hot, bool can = true) {
	return lit ? 1.f : !can ? 0.15f : hot ? 0.7f : 0.45f;
}

/** A step arrow: a small solid triangle, pointing `dir`, centred on (`cx`,
`cy`), `ARROW_LEN` from its base to its point. One shape for every arrow that
says "the next one, that way" (a `GlyphButton`, Hand.hpp) and for the mark
after a word that says where something goes, the RIG's INSERT. Not a
readout's: a direction a display holds, the Seq's, is segments on a cell
(`drawSegDir`), because it shows a state rather than taking a click. Every
solid triangle is this one: a mark of another size passes its own `len`,
base to point, and `half`, half its base, in pixels (the QNT's detune, a
CRV point's handle, the ATN's modulation head, the swap's source arrow);
the arrowheads of `paintImpulse` and `drawArmMark` are strokes of their own. */
enum ArrowDir { ARROW_LEFT, ARROW_RIGHT, ARROW_UP, ARROW_DOWN };
const float ARROW_LEN = 1.1f;  // mm, base to point
const float ARROW_HALF = 1.2f; // mm, half the base
inline void paintArrow(NVGcontext* vg, float cx, float cy, ArrowDir dir, NVGcolor col,
                       float len = mm2px(ARROW_LEN), float half = mm2px(ARROW_HALF)) {
	// Along the way it points, and across it.
	const bool flat = dir == ARROW_LEFT || dir == ARROW_RIGHT;
	const float s = dir == ARROW_LEFT || dir == ARROW_UP ? -1.f : 1.f;
	const float l = len / 2.f, h = half;
	auto at = [&](float along, float across) {
		return flat ? Vec(cx + along, cy + across) : Vec(cx + across, cy + along);
	};
	const Vec tip = at(s * l, 0.f), b0 = at(-s * l, -h), b1 = at(-s * l, h);
	nvgBeginPath(vg);
	nvgMoveTo(vg, tip.x, tip.y);
	nvgLineTo(vg, b0.x, b0.y);
	nvgLineTo(vg, b1.x, b1.y);
	nvgClosePath(vg);
	nvgFillColor(vg, col);
	nvgFill(vg);
}

/** The crossfade, a Seq's Fade over Atns (`FadeSquare`): two ramps crossing,
one falling as the other rises, each held level a little at its ends, as a
crossfade is drawn on a mixing desk. Centred on (`cx`, `cy`), in pixels, in
`col`, which the button that carries it gives, as a glyph's is. */
const float CROSSFADE_W = 2.2f;    // mm, end to end
const float CROSSFADE_H = 1.4f;    // mm, foot to top
const float CROSSFADE_FLAT = 0.4f; // mm, held level at either end
inline void paintCrossfade(NVGcontext* vg, float cx, float cy, NVGcolor col) {
	const float w = mm2px(CROSSFADE_W) / 2.f, h = mm2px(CROSSFADE_H) / 2.f, f = mm2px(CROSSFADE_FLAT);
	nvgStrokeColor(vg, col);
	nvgStrokeWidth(vg, mm2px(0.25f));
	nvgLineJoin(vg, NVG_ROUND);
	nvgLineCap(vg, NVG_ROUND);
	for (int s = -1; s <= 1; s += 2) {
		nvgBeginPath(vg);
		nvgMoveTo(vg, cx - w, cy + s * h);
		nvgLineTo(vg, cx - w + f, cy + s * h);
		nvgLineTo(vg, cx + w - f, cy - s * h);
		nvgLineTo(vg, cx + w, cy - s * h);
		nvgStroke(vg);
	}
}

/** A rig's note, a square of paper with its corner turned up, on the RIG's
note button beside SAVE: three lines of writing on it, in outline while
nothing is written on it and filled once something is. Centred on (`cx`, `cy`), in pixels, `side` mm square, in
`col`, which the button that carries it gives, as a glyph's is. Live, not
static: it changes with what the note holds. */
const float NOTE_SIDE = 6.f; // mm, the size it was drawn at, the fold and the lines its proportions
const NVGcolor NOTE_PAPER = nvgRGB(0xff, 0xd7, 0x14); // Rack's SCHEME_YELLOW, as its Notes would have it
const NVGcolor NOTE_EMPTY = nvgRGB(0x8a, 0x8a, 0x8a);
inline void paintStickyNote(NVGcontext* vg, float cx, float cy, float side, bool written, NVGcolor col) {
	const float k = side / NOTE_SIDE;
	const float s = mm2px(side), f = mm2px(1.6f * k), line = mm2px(0.3f * std::max(k, 0.6f));
	const float x0 = cx - s / 2.f, y0 = cy - s / 2.f;
	// The sheet, its foot's right corner cut where it turns up.
	nvgBeginPath(vg);
	nvgMoveTo(vg, x0, y0);
	nvgLineTo(vg, x0 + s, y0);
	nvgLineTo(vg, x0 + s, y0 + s - f);
	nvgLineTo(vg, x0 + s - f, y0 + s);
	nvgLineTo(vg, x0, y0 + s);
	nvgClosePath(vg);
	if (written) {
		nvgFillColor(vg, col);
		nvgFill(vg);
	}
	else {
		nvgStrokeWidth(vg, line);
		nvgStrokeColor(vg, col);
		nvgStroke(vg);
	}
	// The corner turned up, its underside showing.
	nvgBeginPath(vg);
	nvgMoveTo(vg, x0 + s, y0 + s - f);
	nvgLineTo(vg, x0 + s - f, y0 + s - f);
	nvgLineTo(vg, x0 + s - f, y0 + s);
	nvgClosePath(vg);
	if (written) {
		nvgFillColor(vg, nvgRGBAf(col.r * 0.78f, col.g * 0.76f, 0.f, col.a));
		nvgFill(vg);
	}
	else
		nvgStroke(vg);
	// Three lines of writing, the last one short, ruled in the outline's own
	// colour while nothing is written, so an empty note reads as a note and
	// never as a blank page.
	const float m = mm2px(1.1f * k);
	nvgStrokeWidth(vg, line);
	nvgStrokeColor(vg, written ? color::alpha(INK, 0.6f * col.a) : col);
	nvgLineCap(vg, NVG_ROUND);
	for (int j = 0; j < 3; j++) {
		const float y = y0 + m + j * (s - 2.f * m) / 2.6f;
		const float x1 = j == 2 ? x0 + s - f - m * 0.6f : x0 + s - m;
		nvgBeginPath(vg);
		nvgMoveTo(vg, x0 + m, y);
		nvgLineTo(vg, x1, y);
		nvgStroke(vg);
	}
}

/** A part of a whole as a pie: a disc of `r` pixels centred on (`cx`,
`cy`), ghosted whole as a dark segment is, and while it is on (`whole` above
nought) its rim lit and its inside lit from the top round clockwise, as a
clock's hand sweeps, by `part` of `whole`, so on at nought is the lit rim
alone and a half the right half lit; off, the ghost alone, so it is there to
be found and pressed. Round where the hex mark is square (`drawHexMark`), so
the two never read as one. A readout's end mark (`CellMark::PIE`), a rate's
fraction beside its last cell. */
inline void paintPie(NVGcontext* vg, float cx, float cy, float r, int part, int whole, NVGcolor col) {
	nvgBeginPath(vg);
	nvgCircle(vg, cx, cy, r);
	nvgFillColor(vg, color::alpha(col, 0.15f));
	nvgFill(vg);
	if (whole <= 0)
		return;
	// The rim, a fifth of the radius, inside the disc so on and off take
	// the same room.
	const float w = r * 0.2f;
	nvgBeginPath(vg);
	nvgCircle(vg, cx, cy, r - w / 2.f);
	nvgStrokeWidth(vg, w);
	nvgStrokeColor(vg, col);
	nvgStroke(vg);
	if (part <= 0)
		return;
	nvgBeginPath(vg);
	if (part >= whole)
		nvgCircle(vg, cx, cy, r);
	else {
		// The one place the part becomes a float: an angle to draw.
		const float a0 = -0.5f * (float)M_PI, a1 = a0 + 2.f * (float)M_PI * (float)part / (float)whole;
		nvgMoveTo(vg, cx, cy);
		nvgArc(vg, cx, cy, r, a0, a1, NVG_CW);
		nvgClosePath(vg);
	}
	nvgFillColor(vg, col);
	nvgFill(vg);
}

#include "Readout.hpp"

/** The line that softens the top edge of a dark area, the one the stock LCDs
have always had and the flat screens never did. Drawn on the plate above the
ground, never inside it, so a merged strip carries one unbroken line. */
const float GROUND_SHADOW_H = 1.f;           // px, the stock screen's own
const float GROUND_SHADOW_A = 61.f / 255.f;  // how much of the plate it takes

/** The plate's own grey at a height: the gradient every panel is painted
with, read off rather than guessed, so anything drawn to look like a shade of
the plate is the same shade the plate is. */
inline NVGcolor plateAt(float y) {
	const float t = clamp(y / (float)RACK_GRID_HEIGHT, 0.f, 1.f);
	const NVGcolor a = PLATE_TOP, b = PLATE_FOOT;
	// Channel by channel, in whole units, since the dark top is not a grey.
	auto at = [&](float p, float q) {
		const int u = (int)std::lround(p * 255.f), v = (int)std::lround(q * 255.f);
		return (int)std::round(u + (v - u) * t);
	};
	return nvgRGB(at(a.r, b.r), at(a.g, b.g), at(a.b, b.b));
}

/** A Trg's off ring's dark middle, and the disc a ring's plug lays behind its
face so the ring reads on the plug as it does on the screen: GLASS_RIM's
value, but the ring's own dark, free to move without the glass. */
const NVGcolor RING_DARK = nvgRGB(0x12, 0x12, 0x12);
/** The rim of a stock screen, which is what its outermost pixels are: the
colour to lay behind a run of them, so that where two meet there is that and
not the plate. */
const NVGcolor GLASS_RIM = nvgRGB(0x12, 0x12, 0x12);
/** Flat black ground, square and edge to edge of the panel: a head's mode
strip. */
const NVGcolor FLAT_BLACK = nvgRGB(0x00, 0x00, 0x00);
/** And its corner: the radius Rack's LedDisplay rounds itself with. */
const float GLASS_RADIUS = 5.f;
/** How deep the bevel a stock screen carries reaches in from its edge: a
rim of about two pixels and the lit line under it, measured off a render at
six times size rather than guessed. Inside it the face is flat, so that is
where a screen's working height begins and ends: a fader that ran the whole
box drew its own level line on top of the bevel, and the illusion of a piece
of glass with a thing behind it went with it. */
const float GLASS_BEVEL = 3.f;

/** A screen's working height, the face inside its bevel: `foot` its bottom,
`h` its height, and `travel` where a height `y` stands on it, nought at the
foot and one at the top. A CV's fader and a CRV's arc stand on it; an Atn's
lane is the same face counted from its middle (`AtnLane::half`). */
struct GlassZone {
	float foot, h;
	explicit GlassZone(Vec size) : foot(size.y - GLASS_BEVEL), h(std::max(1.f, size.y - 2.f * GLASS_BEVEL)) {}
	float travel(float y) const { return clamp((foot - y) / h, 0.f, 1.f); }
};

/** `panelY` is where the top of this ground really sits on the panel, which
is not r.pos.y when the rectangle is being painted in a widget's own space:
a screen drawn at nought whose panel position is seventy pixels down. The
plate is a gradient, so the shade of the line above a screen depends on it,
and reading it at nought made a flat screen's line two levels lighter than a
stock screen's on the same row. */
/** The line a piece of ground casts on the plate above it, and the only place
it is drawn.

A stock screen draws one of these itself, a translucent black along its own
top edge, and it cannot be relied on. A run paints its screens as one strip
and has to scissor that strip to its own bounds (the corners are carried out
of sight past each end and clipped back, which is what keeps a merged strip
from curving away from its own edges) and a line the stock screen draws ABOVE
its box is outside those bounds and goes with the clip. So a lone module would
have the line, from Rack, and lose it the moment it was bonded to another,
which is the worst kind of difference: the same panel drawn two ways depending
on who its neighbours are.

Every piece of ground casts it, glass and flat alike, from here, and the stock
screen's own is scissored away wherever it would land so that no line is ever
painted twice. The colour is the one measured off a stock screen (the plate at
that height, less GROUND_SHADOW_A of itself), so what replaces Rack's line is
the same line to the pixel.

It is an opaque grey rather than black at an alpha, and that is on purpose:
two ground parts can cover the same rectangle (a screen and the button that
is the whole screen) and a translucent shadow painted twice is twice as
dark. An opaque one is the same shade however many times it lands. */
/** A height brought onto the device pixel grid of the surface being painted.

A screen's topmost row and the line it casts are one pixel each, and a one
pixel line drawn across a pixel boundary is not a line but two grey halves
of one. The screens opened at 33.14 px for as long as their height came from
the title's own rhythm, which is close enough to a whole pixel to look
crisp; the stock height they open at now is 38.5, exactly half of one. Half
a pixel of movement is nothing to see; the line is everything. */
inline float snapY(NVGcontext* vg, float y) {
	float xf[6];
	nvgCurrentTransform(vg, xf);
	const float scaleY = xf[3];
	return scaleY != 0.f ? (std::round(scaleY * y + xf[5]) - xf[5]) / scaleY : y;
}

/** A thing several heads sound at once, a key or a divider's band, cut into
`n` upright strips side by side across `x0` to `x0 + w`, one a head in its
colour (`colour(i)`), the edges on whole pixels so two colours meet on a
line. */
template <typename Color>
inline void paintHeadStrips(NVGcontext* vg, float x0, float y, float w, float h, int n, Color color) {
	for (int i = 0; i < n; i++) {
		const float a = std::round(x0 + w * i / n), b = std::round(x0 + w * (i + 1) / n);
		nvgBeginPath(vg);
		nvgRect(vg, a, y, b - a, h);
		nvgFillColor(vg, color(i));
		nvgFill(vg);
	}
}

/** Where a line across a screen stands: `th` units thick, centred on `y`,
both its edges on the device pixel grid and its thickness a whole count of
device pixels, one at the least. A line placed in the panel's units, or one
edge snapped and the other carried a fixed number of units below it, lands on
a fraction of a pixel at any zoom but one, and two lines meant to be one, an
Atn's nought tick and its centre line at nought, came out a pixel or half of
one apart. Two lines of
one thickness at one height are the same device pixels at every zoom. */
struct LineBand {
	float top, h;
};
inline LineBand lineBand(NVGcontext* vg, float y, float th) {
	float xf[6];
	nvgCurrentTransform(vg, xf);
	const float s = xf[3], t = xf[5];
	if (s <= 0.f)
		return {y - th / 2.f, th};
	// A count of device pixels, an integer from the one place it is made.
	const int n = std::max(1, (int)std::lround(s * th));
	const float top = std::round(s * y + t - 0.5f * (float)n);
	return {(top - t) / s, (float)n / s};
}
/** The one painter of a line across a screen, `x` to `x + w`, placed by
`lineBand`: a scale's ticks, a centre or nought line, a level's line. */
inline void paintLine(NVGcontext* vg, float x, float w, float y, float th, NVGcolor col) {
	const LineBand b = lineBand(vg, y, th);
	nvgBeginPath(vg);
	nvgRect(vg, x, b.top, w, b.h);
	nvgFillColor(vg, col);
	nvgFill(vg);
}

inline void paintGroundShadow(NVGcontext* vg, Rect r, float panelY = -1.f) {
	const float lip = GROUND_SHADOW_H;
	const NVGcolor plate = plateAt((panelY >= 0.f ? panelY : r.pos.y) - lip / 2.f);
	nvgBeginPath(vg);
	nvgRect(vg, r.pos.x, r.pos.y - lip, r.size.x, lip);
	nvgFillColor(vg, nvgRGB((int)std::round(plate.r * 255.f * (1.f - GROUND_SHADOW_A)),
	                        (int)std::round(plate.g * 255.f * (1.f - GROUND_SHADOW_A)),
	                        (int)std::round(plate.b * 255.f * (1.f - GROUND_SHADOW_A))));
	nvgFill(vg);
}

/** Paint a piece of ground: the shadow above it, then the shape itself.

The shadow is an opaque grey rather than black at an alpha, and that is on
purpose: two ground parts can cover the same rectangle (a screen and the
button that is the whole screen) and a translucent shadow painted twice is
twice as dark. An opaque one is the same shade however many times it lands. */
inline void paintGround(NVGcontext* vg, Rect r, float radius, NVGcolor col, bool shadow = true,
                        float panelY = -1.f, bool edges = false) {
	if (!dev().statics)
		return;
	// Ground standing on the plate beside other ground, a mode strip under a
	// screen, meets it on the pixel grid at both edges, as two stacked
	// screens do (`placeGlass`), whether or not it casts a line, or its top
	// falls between pixels under a screen's snapped foot and a sliver of
	// plate opens between them as the rack zooms. Rows laid on a screen are
	// not asked to.
	if (edges && !shadow) {
		const float top = snapY(vg, r.pos.y), foot = snapY(vg, r.pos.y + r.size.y);
		r = Rect(Vec(r.pos.x, top), Vec(r.size.x, std::max(foot - top, 0.f)));
	}
	// Ground that stands on the plate has its top edge on the pixel grid,
	// with the line it casts sitting on that edge: the two are one picture
	// and a smeared edge under a crisp line looks like neither. Ground laid
	// on other ground (a roll's rows on the screen they are drawn on, which
	// is what asks for no line) keeps the grid it was given, since snapping
	// each row of a ladder separately is how a ladder comes out uneven.
	if (shadow) {
		const float top = snapY(vg, r.pos.y);
		r.size.y += r.pos.y - top;
		r.pos.y = top;
		paintGroundShadow(vg, r, panelY);
	}
	nvgBeginPath(vg);
	if (radius > 0.f)
		nvgRoundedRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y, radius);
	else
		nvgRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y);
	nvgFillColor(vg, col);
	nvgFill(vg);
}

/** The screen every readout sits on: Rack's own LedDisplay, drawn by Rack,
so a Clock's and a Seq's screens are one look, and the look is the stock
one: its rim, the face shaded under it, the line it casts above itself. A
widget that cannot derive from LedDisplay, a param, has a stand-in draw both
of its layers. What the run's shared ground adds is only what is behind
them. */
/** Is this piece of ground a screen: the stock glass, in the stock colour
with the stock corner. A run of them is painted as one stock screen rather
than as a plain rectangle, so a merged strip of screens has the rim, the
shading and the line above that a single one has. */
inline bool isGlass(const NVGcolor& col, float radius) {
	return radius == GLASS_RADIUS && col.r == GLASS_RIM.r && col.g == GLASS_RIM.g &&
	       col.b == GLASS_RIM.b && col.a == GLASS_RIM.a;
}
/** Where a screen is drawn, for every path that draws one: its top edge and
its foot on the pixel grid, and its corners carried out of sight past each
end.

The corners: a rounded rectangle curves away from its own top and bottom
edges at the corners, so a strip drawn at its own size began a few pixels
late on the first panel and ended early on the last, and a fader standing
over that gap broke the illusion that a run is one long screen with the
controls inside it. The corners are pushed out and clipped back, which
leaves the top and bottom shading running the whole width and costs
nothing: the strip is the ends of the rig, and past them is another
module's panel. A lone screen is a strip of one and gets the same, so a
panel looks the same alone as it does bonded. The edge: see `snapY`.

The foot as well as the top, because a screen's foot is where the next
screen's top is: a Clock's and a Seq's two screens stand one on
the other, and with only the top snapped the lower one began on the grid
while the upper one ended wherever the zoom put it, so as the rack zoomed a
sliver of the plate opened and closed between them, or the two overlapped
by a fraction and the seam changed shade. Both edges snapped from the same
height land on the same pixel boundary, and meet with nothing between. */
struct GlassPlace {
	Rect strip; // what is visible: the screen, its top edge snapped
	Vec at;     // where the stock screen is drawn from, a corner past the strip's start
	Vec size;   // and how big, two corners wider than the strip
};
inline GlassPlace placeGlass(NVGcontext* vg, Rect r) {
	const float over = GLASS_RADIUS;
	const float top = snapY(vg, r.pos.y);
	const float foot = snapY(vg, r.pos.y + r.size.y);
	GlassPlace g;
	g.strip = Rect(Vec(r.pos.x, top), Vec(r.size.x, foot - top));
	g.at = Vec(r.pos.x - over, top);
	g.size = Vec(r.size.x + 2.f * over, g.strip.size.y);
	return g;
}
/** Rack's own stock screen, drawn where `placeGlass` says and clipped to the
strip: the unlit face in layer 0, the bevel in layer 1. The only two places
in the plugin that draw a stock screen. The scissor is what makes the line
above a screen ours alone: the stock screen draws one for itself above its
box, and it never survives the clip. */
inline void drawGlassLayer(NVGcontext* vg, const GlassPlace& g, int layer) {
	static LedDisplay glass;
	glass.box.size = g.size;
	Widget::DrawArgs args;
	args.vg = vg;
	args.clipBox = Rect(Vec(0.f, 0.f), g.size);
	nvgSave(vg);
	nvgIntersectScissor(vg, g.strip.pos.x, g.strip.pos.y, g.strip.size.x, g.strip.size.y);
	nvgTranslate(vg, g.at.x, g.at.y);
	if (layer == 0)
		glass.draw(args);
	else
		glass.drawLayer(args, layer);
	nvgRestore(vg);
}
/** A screen's face, unlit: the line it casts on the plate, then the stock
screen. `shadow` is whether plate lies above it (the run answers that from
its picture, a lone widget from `onPlate`) and `panelY` is where its top
really sits on the panel, which is what the plate's shade is read at. */
inline void paintGlassFace(NVGcontext* vg, Rect r, bool shadow = false, float panelY = -1.f) {
	if (!dev().statics)
		return;
	const GlassPlace g = placeGlass(vg, r);
	if (shadow)
		paintGroundShadow(vg, g.strip, panelY >= 0.f ? panelY : r.pos.y);
	drawGlassLayer(vg, g, 0);
}
/** And lit: the bevel a stock screen carries in the lit layer, so a screen
of ours keeps its faint depth when the room goes dark. Placed by the same
call as the face, so the two halves land on the same pixels. */
inline void paintGlassLit(NVGcontext* vg, Rect r) {
	if (!dev().staticLit)
		return;
	drawGlassLayer(vg, placeGlass(vg, r), 1);
}

/** Colours, one palette shared by every type; -1 is the unread: a module
working on its own, or in a group no Seq walks, is still working, and its
screen says so in the first colour, yellow, Rack's own, as a lone Clock or Seq
always has, so a lone Atn never looks switched off beside a lone Clock. */
inline NVGcolor playColor(int i) {
	// Rack's seven, then seven more between and beside them, lighter or
	// deeper so no two neighbours read alike; white is not one, since white
	// is what a lit key or a flash already is. Fifteen, because a colour
	// travels packed in four bits with nought meaning none. Blue second,
	// green fourth and cyan pushed back to seventh; red stays third, often a
	// recording playhead. The colours are read from res/palette.json, or a
	// user's own copy (`palette()`, Switches.hpp); under the DEV's OLD, the
	// fixed order below, cyan second.
	static const NVGcolor was[MAX_COLORS] = {
	    SCHEME_YELLOW,         SCHEME_CYAN,           SCHEME_RED,            SCHEME_GREEN,
	    SCHEME_PURPLE,         SCHEME_ORANGE,         SCHEME_BLUE,           nvgRGB(0xff, 0x7e, 0xc8),
	    nvgRGB(0xb8, 0xf0, 0x3c), nvgRGB(0x3c, 0x9e, 0xa0), nvgRGB(0xff, 0xb8, 0x3c), nvgRGB(0xa8, 0x9c, 0xff),
	    nvgRGB(0xff, 0x6e, 0x5a), nvgRGB(0x6e, 0xd8, 0xff), nvgRGB(0x7e, 0xe8, 0xb0)};
	if (dev().oldColors)
		return was[std::max(i, 0) % MAX_COLORS];
	if (livePalette().on)
		return livePalette().c[std::max(i, 0) % MAX_COLORS];
	return palette()[std::max(i, 0) % MAX_COLORS];
}

/** How far a playhead's halo, its glow, reaches each side of it. */
inline float haloReach() { return mm2px(3.4f); }
/** How strong a halo is where it stands, fading to nothing at its reach. */
const float HALO_ALPHA = 0.16f;

/** Where a playhead's ends are, along its length in the box's own
coordinates, `from` up to `to`, and how far in from each it fades: nothing
at an end, whole `len` in. A playhead running on past the box, a Trg's line
down its rings drawn ring by ring, is one length however many boxes draw it.
No feather, `len` nought, is a playhead of one strength end to end. */
struct Feather {
	float from = 0.f, to = 0.f, len = 0.f;
	/** A playhead from `from` to `to` fading in over `share` of that
	length at each end. */
	static Feather over(float from, float to, float share) {
		Feather f;
		f.from = from;
		f.to = to;
		f.len = share * (to - from);
		return f;
	}
	/** How strong the playhead is at `b` along it, nought to one: whole
	from a feather in up to a feather from the far end, by the same sums the
	line is cut at, so a piece between the cuts is steady to the bit. */
	float at(float b) const {
		if (!(len > 0.f) || (b >= from + len && b <= to - len))
			return 1.f;
		return clamp(std::min(b - from, to - b) / len, 0.f, 1.f);
	}
};

/** One playhead: a soft glow either side of `at` and a line on it, in `c`,
down the whole of `size` when `down` and across it otherwise. Under a Seq's
PHASE turn a playhead parts in two: the halo, the glow alone, where the walk
itself stands, and the line alone on the step actually sounding; with no turn
both stand together. Every member a Seq walks draws its playheads with this, a
CV's fader, an Atn's, a Qnt's roll and keyboard down the screen and a Trg's
ring across it, so the pair reads the same wherever it stands. With a
`feather` the two fade in from its ends: the line in pieces, steady or a ramp, and the glow as one
box gradient, whose one feather is made the glow's reach across the
playhead and the feather's length along it by scaling the length. `lineK`
and `glowK` are the line's and the glow's strengths, nought to one: a Seq in
Fade shows its playhead on two Atns at once, each at its weight, so one at
full strength means that Atn alone. Nought draws nothing. */
inline void paintPlayhead(NVGcontext* vg, Vec size, NVGcolor c, float at, bool down, bool glow = true,
                          bool line = true, Feather feather = Feather(), float lineK = 1.f, float glowK = 1.f) {
	glow = glow && glowK > 0.f;
	line = line && lineK > 0.f;
	const float gw = haloReach();
	const NVGcolor clear = color::alpha(c, 0.f), soft = color::alpha(c, HALO_ALPHA * glowK);
	const float length = down ? size.y : size.x;
	// A rectangle by its place across the playhead and along it.
	auto rect = [&](float a, float len, float b, float blen) {
		if (down)
			nvgRect(vg, a, b, len, blen);
		else
			nvgRect(vg, b, a, blen, len);
	};
	auto band = [&](float a, float len) {
		nvgBeginPath(vg);
		rect(a, len, 0.f, length);
	};
	auto fade = [&](float a0, float a1, NVGcolor c0, NVGcolor c1) {
		return down ? nvgLinearGradient(vg, a0, 0.f, a1, 0.f, c0, c1) : nvgLinearGradient(vg, 0.f, a0, 0.f, a1, c0, c1);
	};
	const bool feathered = feather.len > 0.f;
	if (glow && !feathered) {
		band(at - gw, gw);
		nvgFillPaint(vg, fade(at - gw, at, clear, soft));
		nvgFill(vg);
		band(at, gw);
		nvgFillPaint(vg, fade(at, at + gw, soft, clear));
		nvgFill(vg);
	}
	else if (glow) {
		// Along the playhead the length is scaled by `k`, so the gradient's
		// one feather, the glow's reach, is `feather.len` along it: soft on
		// the line from a feather in from either end, nothing at the ends
		// and at the reach, straight in between, as the two bands are.
		const float k = feather.len / gw;
		const float b0 = std::max(feather.from, 0.f), b1 = std::min(feather.to, length);
		if (b1 > b0) {
			nvgSave(vg);
			if (down)
				nvgScale(vg, 1.f, k);
			else
				nvgScale(vg, k, 1.f);
			nvgBeginPath(vg);
			rect(at - gw, 2.f * gw, b0 / k, (b1 - b0) / k);
			const float inFrom = feather.from / k + gw / 2.f, inLen = (feather.to - feather.from) / k - gw;
			nvgFillPaint(vg, down ? nvgBoxGradient(vg, at - gw / 2.f, inFrom, gw, inLen, 0.f, gw, soft, clear)
			                      : nvgBoxGradient(vg, inFrom, at - gw / 2.f, inLen, gw, 0.f, gw, soft, clear));
			nvgFill(vg);
			nvgRestore(vg);
		}
	}
	if (line) {
		const float a = std::round(at) - 0.75f;
		const NVGcolor full = color::alpha(c, 0.5f * lineK);
		if (!feathered) {
			band(a, 1.5f);
			nvgFillColor(vg, full);
			nvgFill(vg);
		}
		else {
			// In pieces where the strength is steady or a straight ramp:
			// the box cut at a feather in from either end.
			float cut[4] = {0.f, feather.from + feather.len, feather.to - feather.len, length};
			for (int i = 0; i < 3; i++) {
				const float b0 = clamp(std::max(cut[i], feather.from), 0.f, length);
				const float b1 = clamp(std::min(cut[i + 1], feather.to), 0.f, length);
				if (!(b1 > b0))
					continue;
				nvgBeginPath(vg);
				rect(a, 1.5f, b0, b1 - b0);
				const NVGcolor c0 = color::alpha(c, 0.5f * lineK * feather.at(b0)), c1 = color::alpha(c, 0.5f * lineK * feather.at(b1));
				if (feather.at(b0) == 1.f && feather.at(b1) == 1.f)
					nvgFillColor(vg, full);
				else
					nvgFillPaint(vg, down ? nvgLinearGradient(vg, 0.f, b0, 0.f, b1, c0, c1) : nvgLinearGradient(vg, b0, 0.f, b1, 0.f, c0, c1));
				nvgFill(vg);
			}
		}
	}
}

/** The spacing of playheads clustered on one place, touching lines that
glow together. */
inline float playheadStep() { return mm2px(0.85f); }

/** The playheads standing on a screen: one soft glowing vertical line per
Seq whose playhead is here, in that Seq's colour, drawn over whatever the
screen holds. One line stands dead centre; several cluster into touching
lines that glow together, still centred, so two Seqs on one member read as
two lines rather than as one. Nothing standing here draws nothing.

One painter, because a playhead has to look the same wherever it stands: a
CV's fader and a Qnt's roll are the same instrument being walked, and the
eye should not have to learn it twice. `cols` are colour indices; pass only
the ones that are lit, with `line` and `halo` saying which half of each
stands here (`paintPlayhead`), both when they are left out, and `lineK`
and `haloK` how strongly, full when they are left out. */
inline void paintPlayheads(NVGcontext* vg, Vec size, const int* cols, int n, const bool* line = nullptr,
                           const bool* halo = nullptr, const float* lineK = nullptr, const float* haloK = nullptr) {
	const float centre = size.x / 2.f;
	for (int j = 0; j < n; j++)
		paintPlayhead(vg, size, playColor(cols[j]), centre + (j - (n - 1) / 2.f) * playheadStep(), true,
		              !halo || halo[j], !line || line[j], Feather(), lineK ? lineK[j] : 1.f, haloK ? haloK[j] : 1.f);
}

/** The playheads on a whole screen, kept off its bevel top and foot, as a
Qnt's roll keeps them, so two blocks walked side by side show one line of one
length. */
inline void paintPlayheadsOnGlass(NVGcontext* vg, Vec size, const int* cols, int n, const bool* line = nullptr,
                                  const bool* halo = nullptr, const float* lineK = nullptr,
                                  const float* haloK = nullptr) {
	if (n <= 0)
		return;
	nvgSave(vg);
	nvgTranslate(vg, 0.f, GLASS_BEVEL);
	const Vec inner(size.x, size.y - 2.f * GLASS_BEVEL);
	nvgIntersectScissor(vg, 0.f, 0.f, inner.x, inner.y);
	paintPlayheads(vg, inner, cols, n, line, halo, lineK, haloK);
	nvgRestore(vg);
}

/** The light a wave or a curve carries where it is being read: a soft
glow and a ball in its colour. */
/** How far a light's glow reaches from its centre: a screen within this of
a light paints its share of it. */
inline float lightGlow() { return mm2px(2.6f); }
inline void paintLight(NVGcontext* vg, Vec p, NVGcolor col) {
	const float glow = lightGlow(), r = mm2px(0.85f);
	nvgBeginPath(vg);
	nvgCircle(vg, p.x, p.y, glow);
	nvgFillPaint(vg, nvgRadialGradient(vg, p.x, p.y, r * 0.5f, glow, color::alpha(col, 0.34f),
	                                   color::alpha(col, 0.f)));
	nvgFill(vg);
	nvgBeginPath(vg);
	nvgCircle(vg, p.x, p.y, r);
	nvgFillColor(vg, col);
	nvgFill(vg);
}

/** A playhead's halo on a curve, a CRV's, which has no playhead line: the
glow alone, round `p` rather than down a screen, the same colour and reach as
the halo `paintPlayhead` lays beside a line, and no ball, so it reads as the
walk standing there apart from the light the sounding place casts. */
inline void paintHalo(NVGcontext* vg, Vec p, NVGcolor c) {
	const float gw = haloReach();
	nvgBeginPath(vg);
	nvgCircle(vg, p.x, p.y, gw);
	// Twice a band's strength at its heart: a band down a screen reads by its
	// length, and a glow round one point has none, so at a band's strength it
	// is lost at Rack's normal zoom.
	nvgFillPaint(vg, nvgRadialGradient(vg, p.x, p.y, mm2px(0.6f), gw, color::alpha(c, 2.f * HALO_ALPHA),
	                                   color::alpha(c, 0.f)));
	nvgFill(vg);
}

/** `c` laid over `back` at strength `a`, worked out rather than blended, so
it can be laid down solid: a translucent fill cannot meet its neighbour's
at a join, and a solid one of the same colour can. */
inline NVGcolor over(NVGcolor back, NVGcolor c, float a) {
	return nvgRGBf(back.r + (c.r - back.r) * a, back.g + (c.g - back.g) * a, back.b + (c.b - back.b) * a);
}

/** The width of a curve drawn across screens. */
const float CURVE_W = 1.4f;
/** A curve as one path through `pts`, in pixels from the drawing screen,
stroked once with round joins so it keeps one width through every bend, and
the lights on it after the line, all clipped to `clip`: the same line and
light a wave drawn across a block of CVs has, so a curve reads as one
whatever draws it. A CRV hands it a whole chain of joined arcs at once,
and the halos on it (`paintHalo`), laid under the lights. The line alone
is held to `band` too, a CRV's window, past whose edge a bend is not drawn
while its light still is. */
inline void drawCurvePath(NVGcontext* vg, Rect clip, Rect band, const std::vector<Vec>& pts, NVGcolor col, const Vec* lights,
                          const NVGcolor* cols, int n, const Vec* halos = nullptr, const NVGcolor* hcols = nullptr,
                          int nh = 0) {
	nvgSave(vg);
	nvgScissor(vg, clip.pos.x, clip.pos.y, clip.size.x, clip.size.y);
	if (pts.size() >= 2) {
		nvgSave(vg);
		nvgIntersectScissor(vg, band.pos.x, band.pos.y, band.size.x, band.size.y);
		nvgLineCap(vg, NVG_BUTT);
		nvgLineJoin(vg, NVG_ROUND);
		nvgStrokeWidth(vg, CURVE_W);
		// Solid, worked out over the glass: a translucent stroke laid its
		// round join over itself at a sharp turn, a brighter dot at the tip.
		nvgStrokeColor(vg, over(GLASS_RIM, col, 0.7f));
		nvgBeginPath(vg);
		nvgMoveTo(vg, pts[0].x, pts[0].y);
		for (size_t i = 1; i < pts.size(); i++)
			nvgLineTo(vg, pts[i].x, pts[i].y);
		nvgStroke(vg);
		nvgRestore(vg);
	}
	// The halos and then the lights after the line, each clipped as the
	// line is: a screen beside paints its own share of a glow that reaches
	// it.
	for (int i = 0; i < nh; i++)
		paintHalo(vg, halos[i], hcols[i]);
	for (int i = 0; i < n; i++)
		paintLight(vg, lights[i], cols[i]);
	nvgRestore(vg);
}


/** How thick a Poly's or a Mix's row is when nothing of it goes on: a line
that still shows its signal. */
const float LINE_PX = 1.f;

/** A Poly's or a Mix's row in the colour of where it came from, packed as
the engine keeps it, nought for none: a Seq's colour, and white for anything
colourless, a cable or a Mix, white being the Mix's own sum. The gatherers
have no colour of their own. */
inline NVGcolor gatheredColor(int packed) { return packed > 0 ? playColor(packed - 1) : SCHEME_WHITE; }
/** A row with nothing in it: a dark grey barely off the screen it stands on,
on a Poly's rows and a Mix's sum alike. */
inline void paintEmptyRow(NVGcontext* vg, float x, float y, float w, float h) {
	nvgBeginPath(vg);
	nvgRect(vg, x, y, w, h);
	nvgFillColor(vg, color::alpha(SCHEME_WHITE, 0.035f));
	nvgFill(vg);
}
/** The hand over a gatherer's row: an outline round it, a pixel outside the
bar's inset either side. */
inline void paintRowHover(NVGcontext* vg, float inset, Vec size) {
	nvgBeginPath(vg);
	nvgRect(vg, inset - 1.f, 0.5f, size.x - 2.f * inset + 2.f, size.y - 1.f);
	nvgStrokeColor(vg, color::alpha(color::WHITE, 0.7f));
	nvgStrokeWidth(vg, 1.f);
	nvgStroke(vg);
}

/** A thin bar along a screen's edge, nought in the middle, lit from `foot`
to `top`, each a fraction from minus one to one, the rest a ghost: a level
is the span from nought to it, and a signal swinging both ways the span of
its swing (a gatherer's bars, `HeldSpan`). */
inline void drawSpanBar(NVGcontext* vg, float x, float y, float w, float h, float foot, float top, NVGcolor col) {
	nvgBeginPath(vg);
	nvgRect(vg, x, y, w, h);
	nvgFillColor(vg, color::alpha(col, 0.2f));
	nvgFill(vg);
	nvgBeginPath(vg);
	const float mid = x + w / 2.f;
	const float from = clamp(foot, -1.f, 1.f) * w / 2.f, to = clamp(top, -1.f, 1.f) * w / 2.f;
	nvgRect(vg, mid + from, y, to - from, h);
	nvgFillColor(vg, col);
	nvgFill(vg);
}
/** A thin bar along a screen's edge: `frac` of it lit, the rest a ghost.
Bipolar, nought is the middle and the bar grows either way, the span from
nought to `frac`. */
inline void drawBar(NVGcontext* vg, float x, float y, float w, float h, float frac, NVGcolor col,
                    bool bipolar = false) {
	if (bipolar) {
		drawSpanBar(vg, x, y, w, h, std::min(frac, 0.f), std::max(frac, 0.f), col);
		return;
	}
	nvgBeginPath(vg);
	nvgRect(vg, x, y, w, h);
	nvgFillColor(vg, color::alpha(col, 0.2f));
	nvgFill(vg);
	nvgBeginPath(vg);
	nvgRect(vg, x, y, w * clamp(frac, 0.f, 1.f), h);
	nvgFillColor(vg, col);
	nvgFill(vg);
}

/** Where lane `i` of `n` stands across a level meter `width` wide, a lane a
channel of what the meter watches: the Poly's proportion of bar to gap, fifty-five to forty-five
(`GatherRows`), turned on its side, with no gap at the meter's edges, so one
lane is the whole width, the meter of a mono signal. */
inline void meterLane(float width, int n, int i, float& x, float& w) {
	const float gap = 45.f / 55.f;
	w = width / ((float)n + (float)(n - 1) * gap);
	x = (float)i * w * (1.f + gap);
}
/** A level meter's lane, vertical, from `from` to `to` in the screen's
pixels, `x` across and `w` wide: a veil in the meter's colour and, where it
reads a level rather than a swing (`level`), a line where the level stands.
Nothing for less than half a pixel. The Atn's meter, every lane of it. */
inline void paintMeterLane(NVGcontext* vg, float x, float w, float from, float to, NVGcolor lit, bool level) {
	if (std::fabs(to - from) <= 0.5f)
		return;
	nvgBeginPath(vg);
	nvgRect(vg, x, std::min(from, to), w, std::fabs(to - from));
	nvgFillColor(vg, color::alpha(lit, 0.3f));
	nvgFill(vg);
	if (level)
		paintLine(vg, x, w, to, 1.f, color::alpha(lit, 0.8f));
}

/** The same along a screen `size` across its foot, edge to edge, `lift`
millimetres up, a pixel over where it first stood so it sits between the
digits' ink and the screen's foot: what a screen whose jack has taken its number shows of the jack,
or of the screen's own setting. */
inline void drawFootBar(NVGcontext* vg, Vec size, float frac, NVGcolor col, bool bipolar = true, float lift = 1.54f) {
	drawBar(vg, 0.f, size.y - mm2px(lift), size.x, mm2px(0.5f), frac, col, bipolar);
}

// ---- The GAP's skins --------------------------------------------------------

// What the skins are, and their names, is the GAP's data (GapSkin.hpp);
// here are their painters.

namespace gapskin {
/** Rack's own Blank's hairline: a pixel wide, a mid grey at half strength,
the one colour on the light plate and the dark, flat, with no lip or shade.
Over the light plate it reads a soft grey, over the dark one a dim one,
since the plate shows through it in each. */
const NVGcolor HAIRLINE = nvgRGBAf(0.5f, 0.5f, 0.5f, 0.5f);
/** The far lip of a vent, catching the light a pixel under the slot. On the
dark plate it keeps a quarter of its strength, as the ATN's grooves do, since
a full white lip there reads as a line drawn on the plate. */
const Tone LIP = {nvgRGBA(255, 255, 255, 170), nvgRGBA(255, 255, 255, 42)};
inline void line(NVGcontext* vg, float x0, float y0, float x1, float y1, NVGcolor col) {
	nvgBeginPath(vg);
	nvgMoveTo(vg, x0, y0);
	nvgLineTo(vg, x1, y1);
	nvgStrokeWidth(vg, 1.f);
	nvgStrokeColor(vg, col);
	nvgStroke(vg);
}
/** How many of something at `pitch` fit in `len`, and the offset that
centres them, so a pattern sits evenly in the column whatever its length. */
inline int fit(float len, float pitch, float& lead) {
	const int n = std::max(1, (int)std::floor(len / pitch) + 1);
	lead = (len - (float)(n - 1) * pitch) / 2.f;
	return n;
}
} // namespace gapskin

/** A GAP's skin on a plate `size` across, in the panel's own pixels. Pure,
static and plate-coloured: it goes into the panel's tile with the title. */
inline void paintGapSkin(NVGcontext* vg, Vec size, int skin) {
	using namespace gapskin;
	if (!dev().statics || skin == GAP_PLAIN || skin < 0 || skin >= GAP_SKINS_LEN)
		return;
	const float w = size.x, cx = w / 2.f;
	const float y0 = mm2px(SCREEN_Y), y1 = mm2px(BLOCK_FOOT), h = y1 - y0;
	const float in = mm2px(BLOCK_INSET); // the inset every dark thing on a plate keeps
	nvgSave(vg);
	nvgLineCap(vg, NVG_BUTT);
	switch (skin) {
		case GAP_RAILS:
			// Rack's own Blank's two hairlines a side, five and ten pixels
			// in, each on a pixel's middle.
			for (float x : {5.f, 10.f, w - 11.f, w - 6.f})
				line(vg, std::floor(x) + 0.5f, y0, std::floor(x) + 0.5f, y1, HAIRLINE);
			break;
		case GAP_PERFORATED: {
			// The bracket grip's dimple on every half grid unit, three
			// across, down the column.
			const float pitch = mm2px(2.54f);
			float lead;
			const int n = fit(h - mm2px(2.f), pitch, lead);
			nvgBeginPath(vg);
			for (int i = 0; i < n; i++)
				for (int j = -1; j <= 1; j++)
					nvgCircle(vg, cx + j * pitch, y0 + mm2px(1.f) + lead + i * pitch, DIMPLE_R);
			fillDimples(vg);
		} break;
		case GAP_VENTS: {
			// Slots punched through, as a blank that cools what is behind it.
			const float sw = w - 2.f * in - mm2px(1.2f), sh = mm2px(1.1f), pitch = mm2px(3.6f);
			float lead;
			const int n = fit(h - sh - mm2px(2.f), pitch, lead);
			for (int i = 0; i < n; i++) {
				const float y = y0 + mm2px(1.f) + lead + i * pitch;
				nvgBeginPath(vg);
				nvgRoundedRect(vg, cx - sw / 2.f, y, sw, sh, sh / 2.f);
				nvgFillPaint(vg, nvgLinearGradient(vg, 0.f, y, 0.f, y + sh, nvgRGBA(0, 0, 0, 80), nvgRGBA(0, 0, 0, 30)));
				nvgFill(vg);
				line(vg, cx - sw / 2.f + sh / 2.f, std::floor(y + sh) + 0.5f, cx + sw / 2.f - sh / 2.f, std::floor(y + sh) + 0.5f,
				     LIP);
			}
		} break;
	}
	nvgRestore(vg);
}

// ---- Painters a module kept for itself ------------------------------------

/** A Seq's title while it records: the letters of `red` laid red over the
title, where `title` would stand centred, so the rest of it is the plate's
own (Seq.cpp's REC). */
inline void drawRecTitle(NVGcontext* vg, Vec size, const char* title, const char* red) {
	std::shared_ptr<Font> font = plateFont();
	if (!font)
		return;
	setTitleFont(vg, font);
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
	const float w = nvgTextBounds(vg, 0.f, 0.f, title, nullptr, nullptr);
	nvgFillColor(vg, SCHEME_RED);
	nvgText(vg, size.x / 2.f - w / 2.f, TITLE_Y, red, nullptr);
}

/** An impulse, drawn as the textbook draws one, centred in `size`: a
baseline with a single stem up from its middle and an arrowhead on it, at
the plate's line weight (the Trg's PULSE). */
inline void paintImpulse(NVGcontext* vg, Vec size, NVGcolor ink) {
	const float w = mm2px(3.f), h = mm2px(1.8f), head = mm2px(0.7f), half = mm2px(0.45f);
	const float x0 = (size.x - w) / 2.f, y0 = (size.y + h) / 2.f, y1 = y0 - h, mid = size.x / 2.f;
	nvgBeginPath(vg);
	nvgMoveTo(vg, x0, y0);
	nvgLineTo(vg, x0 + w, y0);
	nvgMoveTo(vg, mid, y0);
	nvgLineTo(vg, mid, y1 + head * 0.5f);
	nvgStrokeColor(vg, ink);
	nvgStrokeWidth(vg, mm2px(0.3f));
	nvgLineJoin(vg, NVG_MITER);
	nvgLineCap(vg, NVG_BUTT);
	nvgStroke(vg);
	nvgBeginPath(vg);
	nvgMoveTo(vg, mid, y1);
	nvgLineTo(vg, mid + half, y1 + head);
	nvgLineTo(vg, mid - half, y1 + head);
	nvgClosePath(vg);
	nvgFillColor(vg, ink);
	nvgFill(vg);
}

/** The RIG's two places, drawn in the panel's line as the COPY and PASTE
icons are, in the box at `at` of `size`, in pixels. The factory: a small
works, a chimney at the left, then two teeth of a saw-tooth roof, each rising
to the right and dropping straight. */
inline void paintFactory(NVGcontext* vg, Vec at, Vec size, NVGcolor col) {
	const float x0 = at.x, y0 = at.y, w = size.x, h = size.y;
	nvgStrokeColor(vg, col);
	nvgStrokeWidth(vg, mm2px(0.25f));
	nvgLineJoin(vg, NVG_ROUND);
	nvgBeginPath(vg);
	const float roof = y0 + h * 0.45f, chim = mm2px(0.7f), tooth = (w - chim) / 2.f;
	nvgMoveTo(vg, x0, y0 + h);
	nvgLineTo(vg, x0, y0);
	nvgLineTo(vg, x0 + chim, y0);
	nvgLineTo(vg, x0 + chim, roof);
	for (int k = 0; k < 2; k++) {
		const float tx = x0 + chim + k * tooth;
		nvgLineTo(vg, tx + tooth, y0 + h * 0.15f);
		nvgLineTo(vg, tx + tooth, roof);
	}
	nvgLineTo(vg, x0 + w, y0 + h);
	nvgClosePath(vg);
	nvgStroke(vg);
}
/** The user's folder, in outline: its tab on the left of the top, the body
under it, `crossed` corner to corner while it has gone missing. */
inline void paintFolder(NVGcontext* vg, Vec at, Vec size, NVGcolor col, bool crossed) {
	const float x0 = at.x, y0 = at.y, w = size.x, h = size.y;
	nvgStrokeColor(vg, col);
	nvgStrokeWidth(vg, mm2px(0.25f));
	nvgLineJoin(vg, NVG_ROUND);
	nvgBeginPath(vg);
	const float tab = mm2px(1.4f), lip = mm2px(0.5f);
	nvgMoveTo(vg, x0, y0 + h);
	nvgLineTo(vg, x0, y0);
	nvgLineTo(vg, x0 + tab, y0);
	nvgLineTo(vg, x0 + tab + lip, y0 + lip);
	nvgLineTo(vg, x0 + w, y0 + lip);
	nvgLineTo(vg, x0 + w, y0 + h);
	nvgClosePath(vg);
	nvgStroke(vg);
	if (crossed) {
		nvgBeginPath(vg);
		nvgMoveTo(vg, x0 - mm2px(0.4f), y0 + h + mm2px(0.4f));
		nvgLineTo(vg, x0 + w + mm2px(0.4f), y0 - mm2px(0.4f));
		nvgStroke(vg);
	}
}

/** A colour packed RGBA in one word, the way the engine keeps a cable's
colour: the engine never names NanoVG, so a widget packs what it reads and
a screen unpacks it again (`fromRGB`). */
inline uint32_t packRGB(NVGcolor c) {
	auto b = [](float f) { return (uint32_t)(clamp(f, 0.f, 1.f) * 255.f); };
	return b(c.r) << 24 | b(c.g) << 16 | b(c.b) << 8 | 0xff;
}
inline NVGcolor fromRGB(uint32_t rgb) { return nvgRGB((rgb >> 24) & 0xff, (rgb >> 16) & 0xff, (rgb >> 8) & 0xff); }

// An Atn's inks.
// The screen at rest, and the fill a CV's fader lays over it: both solid,
// the second the very colour a CV works its veil out to.
inline NVGcolor atnVeil() { return over(SCREEN_FACE, color::WHITE, 0.13f); }

/** The faint grey a lane's handle falls back to where its jack is empty. */
inline NVGcolor hintInk() { return color::alpha(color::WHITE, 0.16f); }

/** A cable's colour as a lane's own jack has it, or the hint grey where
that jack is empty. */
inline NVGcolor jackInk(uint32_t rgb) {
	return rgb ? fromRGB(rgb) : hintInk();
}


/** The hint line: a part's hidden gestures in small letters, the pieces of
its hint (`Hinted`, Words.hpp) one after another with a dot between, set on
whatever ground it stands on, the bracket or a screen. The one painter of it:
laid out from the pieces and the stretch it may use, as many leading pieces
as fit and never one cut, centred on the part and slid to stay inside, then
painted with a patch of its ground under it so nothing behind runs through a
letter. Set in Rack's own interface face, which has the command sign. */
struct HintLine {
	static constexpr float SIZE = 6.5f;
	static constexpr const char* DOT = "  \u00b7  ";
	/** The ticker: a line wider than its place stands still for `DWELL`
	seconds from the place's left edge, its whole text from the first letter
	as far as the place goes, then runs it through the place at `PACE`
	pixels a second, holds `HOLD` at the far end, runs back, and holds again,
	so the run starts from what was standing and nothing jumps (centred on
	the pieces that fit, it would leap left as it began). */
	static constexpr float DWELL = 2.f, PACE = 12.f, HOLD = 1.f;
	std::string text;
	int pieces = 0;   // how many of the hint's leading pieces it holds
	float x = 0.f;    // the left of its ink
	float w = 0.f;    // the width of its ink
	std::string whole; // every piece, for the ticker, when they do not all fit
	int all = 0;       // how many pieces the hint has
	float x0 = 0.f, x1 = 0.f, wholeW = 0.f;
	/** How far past its place the whole line runs; nought when it fits. */
	float overflow() const { return whole.empty() ? 0.f : wholeW - (x1 - x0); }
	/** Whether the ticker has the line rather than its leading pieces: from
	the first frame, for any line wider than its place. */
	bool ticking() const { return overflow() > 0.f; }
	/** The ticker's offset `t` seconds after the part was first hovered. */
	float tickerAt(double t) const {
		const float over = overflow();
		if (over <= 0.f || t < DWELL)
			return 0.f;
		const double run = over / PACE, period = 2.0 * (run + HOLD);
		const double u = std::fmod(t - DWELL, period);
		if (u < run)
			return (float)(u * PACE);
		if (u < run + HOLD)
			return over;
		if (u < 2.0 * run + HOLD)
			return over - (float)((u - run - HOLD) * PACE);
		return 0.f;
	}
	/** Lays out `hint`'s pieces within `x0..x1`, centred on `cx` when they
	all fit; else the ticker has it, from `x0`. */
	void layout(NVGcontext* vg, std::shared_ptr<Font> font, const std::string& hint, float left, float right, float cx) {
		text.clear();
		whole.clear();
		pieces = all = 0;
		w = wholeW = 0.f;
		this->x0 = left;
		this->x1 = right;
		if (!vg || !font || right <= left)
			return;
		nvgSave(vg);
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, SIZE);
		nvgTextLetterSpacing(vg, 0.f);
		size_t at = 0;
		bool fits = true;
		while (at < hint.size()) {
			size_t end = hint.find('\n', at);
			if (end == std::string::npos)
				end = hint.size();
			whole = (whole.empty() ? "" : whole + DOT) + hint.substr(at, end - at);
			all++;
			at = end + 1;
			if (!fits)
				continue;
			const float nw = nvgTextBounds(vg, 0.f, 0.f, whole.c_str(), nullptr, nullptr);
			if (nw > right - left) {
				fits = false;
				continue;
			}
			text = whole;
			w = nw;
			pieces++;
		}
		if (pieces == all)
			whole.clear();
		else
			wholeW = nvgTextBounds(vg, 0.f, 0.f, whole.c_str(), nullptr, nullptr);
		nvgRestore(vg);
		x = clamp(cx - w / 2.f, left, right - w);
	}
	/** The pieces shown: all of them while the ticker runs. */
	int shown() const { return ticking() ? all : pieces; }
	/** The stretch of the place the patch covers: the ink, or the whole
	place while ticking. */
	float left() const { return ticking() ? x0 : x; }
	float right() const { return ticking() ? x1 : x + w; }
	/** Paints it on a baseline at `y`, `ink` on a patch of `ground`
	`pad` beyond the ink each side and `h` tall from `top`. */
	void paint(NVGcontext* vg, std::shared_ptr<Font> font, float y, NVGcolor ink, NVGpaint ground, float top, float h,
	           float pad, double t = 0.0) const {
		const bool tick = ticking();
		if (!font || (text.empty() && !tick))
			return;
		const float l = left(), r = right();
		nvgBeginPath(vg);
		nvgRect(vg, l - pad, top, r - l + 2.f * pad, h);
		nvgFillPaint(vg, ground);
		nvgFill(vg);
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, SIZE);
		nvgTextLetterSpacing(vg, 0.f);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		nvgFillColor(vg, ink);
		if (!tick) {
			nvgText(vg, x, y, text.c_str(), nullptr);
			return;
		}
		// The letters leave at the place's edges.
		nvgSave(vg);
		nvgIntersectScissor(vg, x0, top, x1 - x0, h);
		nvgText(vg, x0 - tickerAt(t), y, whole.c_str(), nullptr);
		nvgRestore(vg);
	}
};

} // namespace rig

