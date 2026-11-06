// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Panel.hpp"

/** GAP: a gap in a rig of ours.

A bonder, as the RIG is, and no member of any rig: the engine's rig ends at
its left and starts afresh at its right, so the modules either side of it are
two rigs, each clocked, quantised and gathered on its own, and a Clock to its
left clocks nothing to its right. The bond runs on across it: the bracket,
its ATOMIC mark and the ground go on unbroken, and a RIG to its left holds the
modules on both sides as one molecule, one rig file and one set of scenes. It
has nothing to set.

The title is there in the module browser and for a few seconds after the GAP
is placed, then fades away, a gap being best seen as nothing, and comes back
while the pointer is over it.

The plate wears Rails, Rack's Blank's hairlines, unless another skin is
chosen in the right-click menu: Plain, Perforated or Vents (`GapSkin`). The skin
is one for the whole plugin (`gapSkin`): chosen on any GAP, every GAP in the
patch wears it at once and every GAP placed after, in any patch, so it is
neither a param nor the module's data. The skins are painted by
`paintGapSkin` in Look.hpp. */

using namespace rig;

namespace {

// The title: whole for TITLE_HOLD seconds after the GAP is placed, then gone
// over TITLE_FADE, and back over TITLE_RETURN while the pointer is over it.
const float TITLE_HOLD = 3.f, TITLE_FADE = 1.f, TITLE_RETURN = 0.2f;

/** The skin every GAP wears, a `GapSkin`, kept in Atomic.json by its name, so
the menu's order can change under it, and Rails where none is kept or the
name is no skin's. The UI's alone: read and written on the UI thread, and
nothing on the audio thread looks at it. */
int& gapSkin() {
	static int skin = -1;
	if (skin < 0) {
		skin = GAP_RAILS;
		if (json_t* j = loadSetting("gapSkin")) {
			const char* name = json_string_value(j);
			for (int k = 0; name && k < GAP_SKINS_LEN; k++)
				if (std::string(name) == GAP_SKIN_NAMES[k])
					skin = k;
			json_decref(j);
		}
	}
	return skin;
}
void chooseGapSkin(int k) {
	gapSkin() = k;
	saveSetting("gapSkin", json_string(GAP_SKIN_NAMES[k]));
}

struct Gap : Module, Bonder {
	enum ParamId { PARAMS_LEN };
	enum InputId { INPUTS_LEN };
	enum OutputId { OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };

	Gap() { config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN); }
	bool bondsLeft() override { return true; }
};

/** The plate, its title fading by the clock on the wall: the module's widget
says each frame whether the pointer is over it (`see`), and the title's share
moves toward whole while it is, or while the GAP is newly placed, and toward
nothing otherwise. The share goes in steps, so the tile is drawn again only as
often as the fade shows a change, as the Qnt's title is. In the browser, with
no module, it is whole. */
struct GapPanel : Panel {
	Gap* module = nullptr;
	float share = 1.f;
	double placedAt = -1.0, seenAt = -1.0;
	void see(bool over) {
		const double now = rig::frameNow();
		if (placedAt < 0.0)
			placedAt = seenAt = now;
		const float dt = (float)(now - seenAt);
		seenAt = now;
		if (over || now - placedAt < TITLE_HOLD)
			share = std::min(1.f, share + dt / TITLE_RETURN);
		else
			share = std::max(0.f, share - dt / TITLE_FADE);
	}
	/** The title's share as drawn. A traced frame, written down to be
	compared, draws it whole, as the browser does, wherever the clock has the
	fade, as `breathing` draws its breath at its height and a ScreenButton its
	flash spent: otherwise a trace taken while the fork outruns the wall clock
	catches the fade wherever it is. */
	float shown() {
		if (!module || rig::trace::recording())
			return 1.f;
		return std::ceil(share * 32.f) / 32.f;
	}
	/** The skin is in the version, so every GAP's tile is drawn again the
	frame after a GAP's menu changes it. */
	uint64_t version() override {
		return module ? ((uint64_t)gapSkin() << 8) + (uint64_t)(shown() * 32.f) + 1 : 0;
	}
	void draw(const DrawArgs& args) override {
		// Faded into an empty word, as the Qnt's title fades into its mode's.
		drawPlate(args, this, "GAP", -1.f, 0.f, "", 1.f - shown());
		// The skin goes down where the title does: into the tile, or live
		// when no tile has drawn this panel this frame.
		if (paintingStatic() || !groundPainted(this, TILE_JOB))
			paintGapSkin(args.vg, box.size, gapSkin());
	}
};

struct GapWidget : MemberWidget {
	GapPanel* plate = nullptr;
	/** The skins, one ticked. A choice is every GAP's and kept for the next
	patch, a preference as the plugin's global settings are, so it is no
	undo, as theirs and Rack's own View settings are none. */
	void appendContextMenu(ui::Menu* menu) override {
		if (!plate->module)
			return;
		menu->addChild(new ui::MenuSeparator);
		std::vector<std::string> names(GAP_SKIN_NAMES, GAP_SKIN_NAMES + GAP_SKINS_LEN);
		menu->addChild(createIndexSubmenuItem(
		    "Skin", names, [] { return (size_t)gapSkin(); }, [](size_t k) { chooseGapSkin((int)k); }));
		MemberWidget::appendContextMenu(menu);
	}
	void step() override {
		if (plate->module)
			plate->see(pointerWithin(this));
		MemberWidget::step();
	}
	GapWidget(Gap* module) {
		GapPanel* panel = plate = new GapPanel;
		panel->module = module;
		open(module, panel);
	}
};

} // namespace

Model* modelGap = createModel<Gap, GapWidget>("GAP");
