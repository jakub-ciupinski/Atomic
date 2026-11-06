// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace rig {

/** What a GAP's plate may carry besides its fading title, a skin chosen in
the right-click menu: Rails the default, Rack's own Blank's hairlines, the bare
plate, Perforated (a grid of the bracket's own dimple) and Vents. Each is drawn on
the plate in the column every panel uses, from the screens' top (`SCREEN_Y`)
to the foot of the output block (`BLOCK_FOOT`), so nothing reaches into the
title's band and the bracket above it or down to the screws and the ATOMIC
mark below; and nothing touches a side edge, so a bonded rig's plate runs on
across a GAP unbroken. Widths are the panel's own, so a skin keeps its
proportions at any width. The skin is one for the whole plugin, every GAP
wearing it (Gap.cpp's `gapSkin`), and is saved by its name, so the order here
is the menu's alone. */
enum GapSkin {
	GAP_RAILS,
	GAP_PLAIN,
	GAP_PERFORATED,
	GAP_VENTS,
	GAP_SKINS_LEN
};
const char* const GAP_SKIN_NAMES[GAP_SKINS_LEN] = {"Rails", "Plain", "Perforated", "Vents"};

} // namespace rig
