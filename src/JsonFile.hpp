// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <rack.hpp>
#include <cstdio>
#include <string>

namespace rig {

/** The plugin's own files, its settings, its rigs and window.json, read and
written as JSON the way Rack reads and writes its patches and settings: opened
with std::fopen, which Rack's headers turn into a UTF-8 open on Windows
(common.hpp, fopen_u8), and handed to jansson's loadf and dumpf. jansson's own
json_load_file and json_dump_file open inside libRack with the C runtime's
fopen, which on Windows reads a path in the ANSI code page, so a user folder
with a letter such as ł in it could be neither read nor written. */
inline json_t* loadJson(const std::string& path, json_error_t* err = nullptr) {
	FILE* f = std::fopen(path.c_str(), "r");
	if (!f) {
		if (err)
			std::snprintf(err->text, sizeof(err->text), "cannot open the file");
		return nullptr;
	}
	json_t* rootJ = json_loadf(f, 0, err);
	std::fclose(f);
	return rootJ;
}

/** `rootJ` written to `path`, false where the file could not be opened or
written whole. */
inline bool saveJson(json_t* rootJ, const std::string& path, size_t flags) {
	FILE* f = std::fopen(path.c_str(), "w");
	if (!f)
		return false;
	const bool wrote = json_dumpf(rootJ, f, flags) == 0;
	return std::fclose(f) == 0 && wrote;
}

/** Atomic.json in Rack's user folder: what holds for the whole plugin
rather than for one patch, a key each. */
inline std::string pluginSettingsPath() { return asset::user("Atomic.json"); }

/** The plugin's setting `key` as Atomic.json holds it, a new reference, or
null where the file or the key is missing. */
inline json_t* loadSetting(const char* key) {
	json_t* rootJ = loadJson(pluginSettingsPath());
	json_t* j = rootJ ? json_incref(json_object_get(rootJ, key)) : nullptr;
	if (rootJ)
		json_decref(rootJ);
	return j;
}

/** The plugin's setting `key` written as `valueJ` (which it takes), every
other key in Atomic.json kept as it stands. */
inline void saveSetting(const char* key, json_t* valueJ) {
	json_t* rootJ = loadJson(pluginSettingsPath());
	if (!rootJ || !json_is_object(rootJ)) {
		if (rootJ)
			json_decref(rootJ);
		rootJ = json_object();
	}
	json_object_set_new(rootJ, key, valueJ);
	saveJson(rootJ, pluginSettingsPath(), JSON_INDENT(2));
	json_decref(rootJ);
}

} // namespace rig
