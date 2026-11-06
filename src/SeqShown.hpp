// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "plugin.hpp"
#include "Signal.hpp"
#include <atomic>
#include <cstdint>

/** What a Seq's audio thread stores for its step screen, packed: the Seq
derives from `Head` beside `Member` and stores `shown` at the end of every
scan, and its widgets (`StepQuantity`, `HeadScreen`, in Seq.cpp) read these
and nothing else. It is engine, not face, and lives under Engine.hpp so a headless test
reads a Seq's walk and screen through it without a widget header. */

namespace rig {

struct Head {
	// One packed word: count in the low sixteen bits, then position, then
	// colour, then three bits the playhead may use for its drive. Sixty-four
	// bits, since a walk reaches past 255 (MAX_SEQ_STEPS) and a byte a field
	// would clamp it there.
	std::atomic<uint64_t> shown{0};
	/** A cable in PHASE, which the step screen answers with a bar. */
	std::atomic<bool> shownPhased{false};
	/** How far the PHASE jack has turned the sequence, minus one to one, for
	the bar under the step. */
	std::atomic<float> shownPhase{0.f};
	/** The walk's own step, before the PHASE jack's turn, which the step
	screen shows while the jack is patched. */
	std::atomic<int> shownBase{0};
	/** The direction cell: nought forward, one reverse, two pendulum. */
	std::atomic<int> shownDir{0};
	/** A reset armed, waiting on the next pulse to sound step one: the
	screen lights its mark while the cells name the step still playing. */
	std::atomic<bool> shownArmed{false};
	/** The walk's own step at the last wake, armed or not: a plain field on
	the engine's side that only a headless test reads, never a screen. */
	int walkPos = 0;

	int shownCount() { return (int)(relaxed(shown) & 0xffff); }
	int shownPos() { return (int)((relaxed(shown) >> 16) & 0xffff); }
	int shownColor() { return (int)((relaxed(shown) >> 32) & 0xf); }
	int shownBits() { return (int)((relaxed(shown) >> 36) & 7); }

	void store(int count, int pos, int color, int bits, bool phased) {
		static_assert(MAX_SEQ_STEPS < 0xffff, "a step fits the screen word's sixteen bits");
		shown.store((uint64_t)clamp(count, 0, 0xffff) | ((uint64_t)clamp(pos, 0, 0xffff) << 16) |
		                ((uint64_t)clamp(color, 0, 15) << 32) | ((uint64_t)(bits & 7) << 36),
		            std::memory_order_relaxed);
		shownPhased.store(phased, std::memory_order_relaxed);
	}
};

} // namespace rig
