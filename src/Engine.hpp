// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Pitch.hpp"
#include "Words.hpp"
#include "Glide.hpp"
#include "Signal.hpp"
#include "Arc.hpp"
#include "Member.hpp"
#include "Cartridge.hpp"
#include "Gather.hpp"
#include "Offset.hpp"
#include "Probe.hpp"
#include "Rate.hpp"
#include "Writer.hpp"
#include "Blep.hpp"
#include "Heads.hpp"
#include "SeqShown.hpp"

/** The rig: a family of modules that talk by touching, and read to the left.

This is the engine, as an umbrella over the headers that hold it, so a module or
a headless test includes this one and has all of it. Pitch.hpp is the arithmetic
of a Scale; Signal.hpp the vocabulary nodes hand each other and the steps on it;
Arc.hpp a CRV's arc; Member.hpp the base every member derives from and the host
that computes the rig; Cartridge.hpp the member of a block; Gather.hpp what a
Poly and a Mix gather; Offset.hpp the offset lane an Atn holds; Words.hpp the
tooltip text they share; Probe.hpp what a test asks that no jack carries;
Rate.hpp the rates and the divider; and Writer.hpp the one thing that moves a
Seq's position, the clock foundation's writer; Blep.hpp the band-limited edge a
Seq puts on its jack in hertz; Heads.hpp the playheads a Trg's rings and a
Poly's rows carry, packed for their screens; SeqShown.hpp the words a Seq stores
for its step screen; Glide.hpp the line a dragged setting is played along to
where the hand put it. None of them draws. */
