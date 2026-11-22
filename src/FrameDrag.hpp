// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

/** The position maths behind dragging a whole stack by its bracket, which began
as the retired Frame module's. Kept apart from Bracket.hpp, and free
of Rack, so a headless test can exercise it: a drag needs a mouse and a
window, and no test has one. */
namespace framedrag {

/** A module's place on one rack row, in whole grid steps (HP). */
struct Slot {
	int x, w;
};

/** Where the other modules on a row stand once a rigid group is put down on
it at `at`, `width` steps wide: Rack's own shove for one module placed by
force (`RackWidget::setModulePosForce`), the group taken as that one module.
Those whose left edge is left of the group's are pushed left, the nearest
first, each against the last, until one is clear; the rest are pushed right
the same way. Forcing the members one at a time instead would shove a module
the group leaps past in between two of its members, and would write the
rig's bonds torn between one placement and the next. `others` is every
other module on the row, in any order. */
inline void shove(int at, int width, std::vector<Slot>& others) {
	std::vector<Slot*> left, right;
	for (Slot& s : others)
		(s.x < at ? left : right).push_back(&s);
	std::stable_sort(left.begin(), left.end(), [](const Slot* a, const Slot* b) { return a->x > b->x; });
	std::stable_sort(right.begin(), right.end(), [](const Slot* a, const Slot* b) { return a->x < b->x; });
	int cursor = at;
	for (Slot* s : left) {
		if (s->x + s->w <= cursor)
			break;
		s->x = cursor - s->w;
		cursor = s->x;
	}
	cursor = at + width;
	for (Slot* s : right) {
		if (s->x >= cursor)
			break;
		s->x = cursor;
		cursor = s->x + s->w;
	}
}

/** The pointer moves smoothly and modules do not: a drag position becomes the
nearest slot on the rack's grid. Templated only so a test can hand it a plain
pair of floats instead of a Rack Vec. */
template <typename V>
inline V snapToGrid(V pos, float gridW, float gridH) {
	V out = pos;
	out.x = std::round(pos.x / gridW) * gridW;
	out.y = std::round(pos.y / gridH) * gridH;
	return out;
}

} // namespace framedrag
