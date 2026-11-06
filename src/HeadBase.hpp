// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Panel.hpp"

/** The head column most readers share: two short screens under the title,
the mode strip under them and the output block at the foot, placed alike on
a Seq, a Clock and the rest so a rig of them lines up. The packed words a
Seq's audio thread stores for its step screen, `Head`, are engine and live
in SeqShown.hpp under Engine.hpp. */

namespace rig {

// The screens are shorter than most, touching, to leave room for the column
// below them: up to four jacks in, each with a caption over it, three out,
// and a switch. They lost two millimetres each when the jacks gained an
// eighth of their pitch: a column of seven jacks and a switch on a panel
// whose foot is fixed has to take the room from somewhere, and a readout
// four millimetres tall has more air around it than it needs.
const float HEAD_SCREEN_H = drop(7.5f);
const float HEAD_SCREEN2_Y = SCREEN_Y + HEAD_SCREEN_H;
// The four rows the column stands on are in Look.hpp: a module beside one
// of these places its own jacks against them.
/** The mode strip: a third screen under the two, touching them, flat black
and the panel's whole width, down to the air over the first caption of a Seq's
column, with the switch's word on it large enough to read. A Clock's RUN and a
Seq's PREVIEW stand in it, alike and level across a rig. */
const float HEAD_STRIP_Y = HEAD_SCREEN2_Y + HEAD_SCREEN_H;
// It stops a millimetre and a half over that caption's middle, at the column
// as it stood before the first caption went half a clearance lower
// (`HEAD_ROW1_Y`): a caption's capitals stand taller than CAP_INK, and the
// strip ending on their tops reads as the word hanging from it.
const float HEAD_STRIP_H = capAbove(STRIP_ROW1_Y) - 1.5f - HEAD_STRIP_Y;
const float HEAD_STRIP_PX = 8.f;

/** The RIG's hand: its ground from an Atn's screen's foot (Atn.cpp,
`ATN_SCREEN_H`) down to the air over SCENE, where a CRV's screen closes
(Crv.cpp, `ARC_H`), four rows of it, and a row the size every RIG button is
(Rig.cpp) and the GAP's two are (Gap.cpp), so the two read as one family. */
const float HAND_TOP = SCREEN_Y + 55.88f;
const float HAND_FOOT = seqCapY(4) - SCREEN_AIR;
const int HAND_ROWS = 4;
const float HAND_ROW_H = (HAND_FOOT - HAND_TOP) / HAND_ROWS;

/** The strip itself: flat ground, black, square, so it is painted with the
screens above it and joins the strip of a module standing beside it, and a
screen under it stands on ground and casts no line. */
struct HeadStrip : Widget, GroundPart {
	// Whether the switch on it is there at all: a Seq with nothing to
	// preview has no strip, only the plate, as if there were no button.
	std::function<bool()> shownWhen;
	bool isGround() override { return !shownWhen || shownWhen(); }
	NVGcolor groundColor() override { return FLAT_BLACK; }
	void draw(const DrawArgs& args) override { paintGroundPart(this, this, args, 0); }
};
/** A switch in the strip: the strip laid, and the button standing on it
with its word at the strip's size. */
template <class B>
B* addStripButton(ModuleWidget* w, Module* module, int paramId, const char* word,
                  std::function<bool()> shownWhen = nullptr) {
	HeadStrip* ground = new HeadStrip;
	ground->shownWhen = shownWhen;
	ground->box.pos = mm2px(Vec(0.f, HEAD_STRIP_Y));
	ground->box.size = mm2px(Vec(PANEL_W, HEAD_STRIP_H));
	w->addChild(ground);
	B* b = createParam<B>(mm2px(Vec(0.f, HEAD_STRIP_Y)), module, paramId);
	b->box.size = mm2px(Vec(PANEL_W, HEAD_STRIP_H));
	b->strip = true;
	b->text = word;
	b->px = HEAD_STRIP_PX;
	w->addChild(b);
	return b;
}
// Three outputs at the column's own pitch, counted up from the jack at the
// foot, which stands where every panel's does, the same pitch as the inputs
// above them.
const float HEAD_OUT3_Y = FOOT_Y;
const float HEAD_OUT2_Y = HEAD_OUT3_Y - FOOT_PITCH, HEAD_OUT2_CAP_Y = capAbove(HEAD_OUT2_Y);
const float HEAD_OUT3_CAP_Y = capAbove(HEAD_OUT3_Y);
// The output block with two jacks in it, the lower two of the three. Every
// output block opens BLOCK_LIP above its first caption and closes at
// BLOCK_FOOT, so the plate under a caption is the same depth wherever the
// block starts.
const float HEAD_BLOCK2_Y = blockTop(HEAD_OUT2_CAP_Y) - BLOCK_AIR;
const float HEAD_BLOCK2_H = blockHeight(HEAD_OUT2_CAP_Y) + BLOCK_AIR;


} // namespace rig
