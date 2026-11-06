// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>

/** The playheads standing on the places of one screen, packed into one word,
so a screen never reads half a wake: a Trg's rings and a Poly's rows, each a
place a Seq's playhead can stand on and the screen draws its line across
(`paintPlayhead`). One packing for both, because the two are one instrument
being walked: the Trg's rings by a Seq walking them, the Poly's channels by a
Seq arpeggiating it.

The count in the low four bits, then eleven bits a head: its low five the
place, nought to thirty, or ACROSS for a Seq standing on the whole screen,
four the colour plus one, then whether its line stands there and whether its
halo does. Five heads at most. Under a PHASE turn one Seq is two heads, its
line on the place sounding and its halo on the place its walk stands on; with
no turn it is one head with both. Heads are packed in the order the Seqs
write, which is the rig's order, the nearest Seq first, so several on one
place cluster in rig order.

Plain data and no atomics: the owner keeps one through the wake (`work`) and
stores its `bits` into its own atomic in `publishReads`. */

namespace rig {

struct Heads {
	static const int MAX = 5;
	static const int BITS = 11;
	static const int ACROSS = 31;
	uint64_t bits = 0;

	int count() const { return (int)(bits & 0xf); }
	static int placeOf(uint64_t p, int i) { return (int)((p >> (4 + BITS * i)) & 0x1f); }
	static int colorOf(uint64_t p, int i) { return (int)((p >> (4 + BITS * i + 5)) & 0xf) - 1; }
	static bool lineOf(uint64_t p, int i) { return (p >> (4 + BITS * i + 9)) & 1; }
	static bool haloOf(uint64_t p, int i) { return (p >> (4 + BITS * i + 10)) & 1; }

	/** One head packed at `place` (ACROSS, or nought to thirty), in colour
	`col` (nought up to fifteen), returning its index, or -1 when five
	already stand. */
	int add(int place, int col, bool line, bool halo) {
		const int n = count();
		if (n >= MAX)
			return -1;
		const int at = std::min(std::max(place, 0), ACROSS);
		const int c = std::min(std::max(col, 0), 14) + 1;
		const uint64_t h = (uint64_t)at | (uint64_t)c << 5 | (uint64_t)line << 9 | (uint64_t)halo << 10;
		bits = ((bits | h << (4 + BITS * n)) & ~0xfull) | (uint64_t)(n + 1);
		return n;
	}
	/** Head `i`'s halo lit too, its line's own head: the walk stands where
	the step sounds. */
	void addHalo(int i) { bits |= (uint64_t)1 << (4 + BITS * i + 10); }

	/** The heads standing on `place` in the published word `p`, as colours,
	nearest Seq first, with whether each one's line and halo stand there.
	Returns how many, so a place two Seqs share clusters them as a CV
	clusters its lanes rather than mixing them to a meaningless white. */
	static int on(uint64_t p, int place, int* cols, bool* line, bool* halo) {
		const int n = (int)(p & 0xf);
		int m = 0;
		for (int i = 0; i < n; i++)
			if (placeOf(p, i) == place) {
				cols[m] = colorOf(p, i);
				line[m] = lineOf(p, i);
				halo[m] = haloOf(p, i);
				m++;
			}
		return m;
	}
};

} // namespace rig
