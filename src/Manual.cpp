// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugin.hpp"
#include "Look.hpp"

/** Where the manual lives, the one place it is named: each module's page is
its slug in lower case, and the mode after the hash. */
static const char* const MANUAL_URL = "https://atomic-modular.com/";

/** Each model's manual link carries the panels' mode, so Rack's own Manual
item and its F1 key open the page light or dark as the panels are. Rack reads
the link when the menu is built or the key is pressed, so it is set at once
and again whenever the switch is thrown; a frame from the first of our
widgets to step is enough, and the compare keeps the frame free. */
void keepManualMode() {
	static int was = -1;
	const int dark = rig::darkPanels();
	if (dark == was)
		return;
	was = dark;
	for (Model* m : pluginInstance->models)
		m->manualUrl = MANUAL_URL + string::lowercase(m->slug) + ".html#" + (dark ? "dark" : "light");
}
