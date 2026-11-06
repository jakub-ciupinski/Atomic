// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Manifest.hpp"

namespace rig {
namespace manifest {

const std::vector<SlotDecl>& decls(Id id) {
	static std::vector<SlotDecl> table[M_COUNT];
	static const bool made = []() {
		for (int i = 0; i < M_COUNT; i++)
			table[i].assign(SPECS[i].slots, SPECS[i].slots + SPECS[i].nslots);
		return true;
	}();
	(void)made;
	return table[id];
}

} // namespace manifest
} // namespace rig
