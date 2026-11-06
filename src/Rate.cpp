// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Rate.hpp"
#include "JsonFile.hpp"

namespace rig {

/** The file and JSON reading of the window lives here, out of the engine's
headers (declared in Rate.hpp). */
void loadWindowMs(const std::string& path) {
	json_t* root = loadJson(path);
	if (!root)
		return;
	if (json_t* ms = json_object_get(root, "windowMs"))
		if (json_is_number(ms))
			windowMs() = clampWindowMs(json_number_value(ms));
	json_decref(root);
}

} // namespace rig
