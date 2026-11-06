// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GroundParts.hpp"
#include "Tips.hpp"
#include "Hand.hpp"

/** The parts: the widgets a panel is assembled from.

Every one of them is one of two things, and says which. Ground is flat,
opaque and unchanging (a screen, a slider well) the part of a face that has
to run on into the neighbour with no join; it derives from GroundPart and
answers with a rectangle and a colour. A control is a knob, a jack, a
readout, a lit bar: it belongs to one module and is nobody else's business.

The rest is the screens: a readout, a readout that is its own drag, a readout
that is its own button. All three wear the stock LedDisplay's look, which
Rack draws, so a Clock's screen and a Trg's are the same object.

They are in three headers, this one their umbrella as Engine.hpp is the
engine's: GroundParts.hpp is ground and the static layer, the frame's marks
and the walks that find the run and the ground of a module; Tips.hpp is
the tooltips that say what a module is doing now; Hand.hpp is everything a
hand touches, the strokes, the modifier keys and the screens that are
controls, with the cable colours a widget reads in its step. */
