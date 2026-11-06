// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <vector>

/** The position maths behind dragging a whole stack by its bracket, which began
as the retired Frame module's. Kept apart from Bracket.hpp, and free
of Rack, so a headless test can exercise it: a drag needs a mouse and a
window, and no test has one. */
namespace framedrag {

/** The order in which a rigid group of modules must be re-placed so that
every move lands in space one of its own members has already left: leftmost
first when the group is heading left, rightmost first when it is heading
right. The group is given left to right and `count` long; the returned
indices are into that list.

Placing them in the wrong order asks Rack to put a module where one of its
own neighbours still stands, and Rack, being asked politely, declines. */
inline std::vector<int> placementOrder(int count, bool leftward) {
	std::vector<int> order;
	order.reserve(count);
	for (int i = 0; i < count; i++)
		order.push_back(leftward ? i : count - 1 - i);
	return order;
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
