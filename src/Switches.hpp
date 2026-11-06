// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "plugin.hpp"
#include "JsonFile.hpp"
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>

/** The switches: what the DEV and PAL modules throw, read by the look's
painters as they draw. They are kept here rather than in Look.hpp because
they are state, and Look.hpp remembers nothing between calls; a painter that
asks `dev()` asks a switch, not its own past. The palette read from its file,
`palette()`, is here for the same reason. */

/* Rack's own plugin.hpp, which rack.hpp means to include, is shadowed by
ours wherever src is on the include path (the sim), so the one call of it the
palette makes is declared here as Rack declares it. */
namespace rack {
namespace plugin {
Plugin* getPlugin(const std::string& pluginSlug);
} // namespace plugin
} // namespace rack

namespace rig {

/** The switches on the DEV module.

The panel is drawn in four layers and these are them. Static is everything that
stands still (the plate, the ground, the screens' faces, the captions, the
bracket), which is what a tile caches and blits; live is everything drawn afresh
every frame, the knobs, the bars, the playheads, the digits. Each of the two has
a lit half, drawn in Rack's layer 1 and left bright when the room darkens: the
screens' bevel is static and lit, a readout's glowing number is live and lit.

Nothing here knows about any one module. A switch turns off a layer of the
drawing API wherever it is used, so what it measures is the layer and not a
panel. The three below the four are the machinery, for a benchmark rather
than for a look: whether the run paints its ground as one picture, whether
the bracket is drawn at all, whether the tree is walked to answer onPlate and
overGround, and whether the static picture is cached in tiles.

Everything is on unless a DEV is in the rack and says otherwise, and a DEV
leaving turns everything back on, except the tiles, which are off until asked
for. Off may well break the look; that is the point of it. */
struct DevSwitches {
	std::atomic<bool> statics{true};   // the static layer: plate, ground, screens, captions, bracket
	std::atomic<bool> staticLit{true}; // its lit half: the bevel a screen carries
	std::atomic<bool> live{true};      // everything drawn every frame
	std::atomic<bool> liveLit{true};   // its lit half: digits, playheads, glows
	std::atomic<bool> ground{true};    // the run painting its ground as one picture
	std::atomic<bool> bracket{true};   // the ATOMIC bracket
	std::atomic<bool> walks{true};     // overGround and onPlate asking the tree
	/** The static picture cached a module at a time and blitted, a
	benchmarking option off unless a DEV turns it on. Everything tiles do is
	drawn live without them, so nothing depends on it. It shows artifacts as
	the rack zooms, so it stays off though it saves the most drawing. */
	std::atomic<bool> tiles{false};
	/** The colours without the scheme:
	cyan second, a CV white unless quantized, no white for a bonded module
	standing idle. Off, which is the scheme, unless a DEV turns it on to
	compare. */
	std::atomic<bool> oldColors{false};
	void all(bool on) {
		statics = staticLit = live = liveLit = ground = bracket = walks = on;
		tiles = false; // never on by itself
		oldColors = false;
	}
};
inline DevSwitches& dev() {
	static DevSwitches d;
	return d;
}

/** The rotation compiled in: the colours res/palette.json ships with, kept
here as the last fallback, so a plugin whose file is gone or broken still
draws its groups in colour rather than black. Blue second, Rack's own cable
green, #00b56e, fourth, cyan seventh, the rotation chosen on a PAL. A change
to the shipped rotation is made in both. */
inline const NVGcolor* compiledPalette() {
	static const NVGcolor c[MAX_COLORS] = {
	    SCHEME_YELLOW,         SCHEME_BLUE,           SCHEME_RED,            nvgRGB(0x00, 0xb5, 0x6e),
	    SCHEME_PURPLE,         SCHEME_ORANGE,         SCHEME_CYAN,           nvgRGB(0xff, 0x7e, 0xc8),
	    nvgRGB(0xb8, 0xf0, 0x3c), nvgRGB(0x3c, 0x9e, 0xa0), nvgRGB(0xff, 0xb8, 0x3c), nvgRGB(0xa8, 0x9c, 0xff),
	    nvgRGB(0xff, 0x6e, 0x5a), nvgRGB(0x6e, 0xd8, 0xff), nvgRGB(0x7e, 0xe8, 0xb0)};
	return c;
}

/** One colour as a palette file writes it, "#rrggbb", into `c`; false for
anything else, the hash and all six digits being required. Read into the
same bytes nvgRGB takes, so a file's colour is the compiled one exactly. */
inline bool hexColor(json_t* j, NVGcolor& c) {
	const char* s = json_string_value(j);
	if (!s || s[0] != '#' || std::strlen(s) != 7)
		return false;
	for (int k = 1; k < 7; k++)
		if (!std::isxdigit((unsigned char)s[k]))
			return false;
	const unsigned long v = std::strtoul(s + 1, nullptr, 16);
	c = nvgRGB((v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
	return true;
}

/** The rotation a palette file at `path` holds, into `c` whole or not at
all: its "rotation", exactly MAX_COLORS colours in their order. */
inline bool paletteFrom(const std::string& path, NVGcolor* c) {
	json_t* rootJ = loadJson(path);
	json_t* rotJ = rootJ ? json_object_get(rootJ, "rotation") : nullptr;
	NVGcolor got[MAX_COLORS];
	bool ok = json_is_array(rotJ) && json_array_size(rotJ) == (size_t)MAX_COLORS;
	for (int i = 0; ok && i < MAX_COLORS; i++)
		ok = hexColor(json_array_get(rotJ, i), got[i]);
	if (rootJ)
		json_decref(rootJ);
	if (ok)
		std::copy(got, got + MAX_COLORS, c);
	return ok;
}

/** Where a user's own palette stands: Atomic/palette.json in Rack's user
folder, a copy of the shipped one edited. */
inline std::string userPalettePath() { return asset::user("Atomic/palette.json"); }

/** The palette read into `c`: the user's own where it stands and reads,
else the one ATOMIC ships (res/palette.json, found through the plugin by its
slug, so the tools plugin reads the same file), else the compiled one. A
file that stands and does not read is said in Rack's log. */
inline void readPalette(NVGcolor* c) {
	const std::string user = userPalettePath();
	if (system::isFile(user)) {
		if (paletteFrom(user, c))
			return;
		WARN("Atomic: %s is not a palette of %d \"#rrggbb\" colours, so the shipped one is used", user.c_str(), MAX_COLORS);
	}
	if (plugin::Plugin* p = plugin::getPlugin("Atomic")) {
		const std::string shipped = asset::plugin(p, "res/palette.json");
		if (paletteFrom(shipped, c))
			return;
		WARN("Atomic: %s does not read, so the compiled palette is used", shipped.c_str());
	}
	std::copy(compiledPalette(), compiledPalette() + MAX_COLORS, c);
}

/** The palette every module draws in, read from its file at the first
colour drawn and kept: the UI thread's alone, as all drawing is, and never
the engine's, which counts colours and paints none. `rereadPalette` reads
the files again, the harness's "palette" step. */
inline NVGcolor* paletteKept() {
	static NVGcolor c[MAX_COLORS];
	static const bool read = (readPalette(c), true);
	(void)read;
	return c;
}
inline const NVGcolor* palette() { return paletteKept(); }
inline void rereadPalette() { readPalette(paletteKept()); }

/** A palette being tried: the PAL module's colours in place of the ones
read from the file while one stands in the rack, so a rotation can be chosen
by eye before it is written into the file. The UI thread's alone, as all
drawing is. */
struct LivePalette {
	bool on = false;
	NVGcolor c[MAX_COLORS];
};
inline LivePalette& livePalette() {
	static LivePalette p;
	return p;
}

} // namespace rig
