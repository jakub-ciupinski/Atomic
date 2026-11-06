// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <rack.hpp>
#include <atomic>
#include <cstdint>

/** Cables stacked on one input (Rack 2.5 and later), read one by one.

Rack hands a module only the sum of the cables stacked on an input, and a
module hears one port event when the first arrives and one when the last
leaves, none for the ones between (measured in Rack Pro 2.6). So who is
stacked there is known only to the UI, which owns the cables: the widget lists
them every frame (`publish`), each by the id of the module its cable comes
from, that module's output and the cable's colour, and the engine reads each
source's output itself (`read`). A source is looked up by its id on every
read, through the engine's own lookup that takes no lock, which is safe in
`process` because the engine holds its lock for the whole step and a module is
only ever removed under the exclusive one; a source deleted since the list was
made is simply not found. A cable pulled while its module stays is read for at
most a frame more, until the widget lists again.

A Poly and a Mix use it: with two cables or more stacked in IN, each cable
gives its rows in the cable's own colour; one cable is read plainly, its
channels the rows. A polyphonic cable in a stack gives every channel it
carries, one after the other, so two Polys' cables stacked on a third are
combined, not mixed: two channels and three are five. Sixteen rows at most;
the channels past them are counted (`past`), for the tooltip. */
struct Stack {
	static const int MAX = 16;

	// Written by the UI under a sequence count, odd while it writes, so the
	// engine takes a list whole or not at all.
	std::atomic<uint32_t> seq{0};
	std::atomic<int> n{0};
	std::atomic<int64_t> ids[MAX];
	std::atomic<int> outs[MAX];
	std::atomic<uint32_t> rgbs[MAX];

	// The engine's copy, the last whole list it took.
	uint32_t taken = 0;
	int an = 0;
	int64_t aid[MAX] = {};
	int aout[MAX] = {};
	uint32_t argb[MAX] = {};
	uint32_t rowRgb[MAX] = {}; // each row's cable's colour, for the last read
	int rowCable[MAX] = {};    // and which cable it is, counted from nought in the stacking's order
	int rows = 0;
	int past = 0; // the channels of the last read past the sixteenth row, left out

	Stack() {
		for (int i = 0; i < MAX; i++) {
			ids[i].store(-1, std::memory_order_relaxed);
			outs[i].store(0, std::memory_order_relaxed);
			rgbs[i].store(0, std::memory_order_relaxed);
		}
	}

	/** The engine: each stacked cable's channels into `v`, cable after
	cable, and how many rows that made, or minus one where fewer than two are
	stacked, so the caller reads the input as one cable. */
	int read(float* v) {
		const uint32_t s = seq.load(std::memory_order_acquire);
		if (!(s & 1) && s != taken) {
			const int k = n.load(std::memory_order_relaxed);
			int64_t id[MAX];
			int out[MAX];
			uint32_t rgb[MAX];
			for (int i = 0; i < k; i++) {
				id[i] = ids[i].load(std::memory_order_relaxed);
				out[i] = outs[i].load(std::memory_order_relaxed);
				rgb[i] = rgbs[i].load(std::memory_order_relaxed);
			}
			// The loads above are not to be read after the check below, which
			// the acquire on it alone does not forbid.
			std::atomic_thread_fence(std::memory_order_acquire);
			if (seq.load(std::memory_order_relaxed) == s) {
				an = k;
				for (int i = 0; i < k; i++) {
					aid[i] = id[i];
					aout[i] = out[i];
					argb[i] = rgb[i];
				}
				taken = s;
			}
		}
		if (an < 2 || !APP || !APP->engine)
			return -1;
		int k = 0;
		past = 0;
		for (int i = 0; i < an; i++) {
			rack::engine::Module* m = APP->engine->getModule_NoLock(aid[i]);
			rack::engine::Output* o = m && aout[i] >= 0 && aout[i] < (int)m->outputs.size() ? &m->outputs[aout[i]] : nullptr;
			// A cable whose source has gone, or gives no channel at the
			// moment, is still a row, at nought, so the rows stay put.
			const int ch = o ? std::max(o->getChannels(), 1) : 1;
			past += std::max(k + ch - MAX, 0);
			for (int c = 0; c < ch && k < MAX; c++, k++) {
				v[k] = o ? o->getVoltage(c) : 0.f;
				rowRgb[k] = argb[i];
				rowCable[k] = i;
			}
		}
		rows = k;
		return k;
	}
	/** The colour of the one cable, or the first, of the last list taken,
	packed RGBA, nought for none: a single cable in IN is read as one and
	its rows wear its colour. */
	uint32_t first() const { return an >= 1 ? argb[0] : 0; }
	/** The colour of row `i` of the last read, packed RGBA. */
	uint32_t rgb(int i) const { return i >= 0 && i < rows ? rowRgb[i] : 0; }
	/** The cable row `i` of the last read came from, counted from nought. */
	int cable(int i) const { return i >= 0 && i < rows ? rowCable[i] : 0; }
};
