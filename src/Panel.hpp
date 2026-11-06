// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/** A module's worth of headers: the engine, the look, the parts and the bracket.

A module .cpp includes this and has everything. The four underneath it are
layered (Engine knows nothing of drawing, Look knows nothing of widgets or of
the rig, Parts asks the rig nothing but who stands in it, Bracket knows all
three and carries the run's own headers, Ground, Swap and Writes) and that layering is worth keeping:
if a change wants to point the other way, it is telling you something.
Anything headless (a test build with no Rack) includes Engine.hpp alone. */
#include "Bracket.hpp"
