// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Signal.hpp"
#include "Words.hpp"
#include <atomic>
#include <string>

/** The offset lane: the retired Ofs module's code, held by the Atn. It
stays under the engine's umbrella because headless tests read its limits. */

namespace rig {

/** An Ofs, as the rig sees it, in a form a module can hold rather than be:
the retired Ofs module's own code, lifted into the Atn rather than written
again, so the Atn's offset lane behaves as the Ofs did by construction, the
same unit, the same shift of a window, the same everything.

The owner feeds it the window arriving, through `prepared`, and hands it the
bar, minus one to one, with every question it asks: `shape` on a window, `act`
on a reader's cable, and `shift` for a jack, each taking the bar as `amount`
in the unit the owner worked it out in. */
struct OfsCore {
	// UNIT_ROLL: semitones still, but free, a count of cents, for an Atn its
	// CV holds over a roll (`HELD_ROLL`), which transposes the chord as one.
	enum Unit { UNIT_VOLTS, UNIT_SEMIS, UNIT_ROLL };
	static const int MAX_VOLTS = 10;

	std::atomic<int> unit{UNIT_VOLTS}; // what the bar reads in
	int unitWork = UNIT_VOLTS;         // through the wake, published at its end
	/** How many semitones the whole bar is worth on a pitch: ten octaves, so
	that an Atn's lane means the same thing whatever the rig is in. Ten volts
	one way and a hundred and twenty semitones the other are the same ten
	octaves, and a user dragging the same fader does not find it reaching half
	as far because a keyboard came into view. */
	static const int SEMI_REACH = 12 * MAX_VOLTS;

	static int semis(float amount) { return (int)std::round(amount * (float)SEMI_REACH); }
	static float volts(float amount) { return amount * (float)MAX_VOLTS; }
	/** The bar as cents, a hundred to the semitone: a count, whole from the
	param on. */
	static int cents(float amount) { return (int)std::round(amount * (float)(SEMI_REACH * 100)); }
	/** The bar's unit: semitones with a quantizer in reach, before it or
	after it (`notes`, what the owner was told), else volts. */
	void prepared(const Mapping& win, bool notes = false) {
		unitWork = win.musical() || notes ? UNIT_SEMIS : UNIT_VOLTS;
	}
	/** The unit is worked out in the wake and handed to the screen at its
	end, with everything else the owner publishes. */
	void publish() { unit.store(unitWork, std::memory_order_relaxed); }
	void shape(Mapping& m, float amount) {
		// In notes with no quantizer yet in reach: in front of a Qnt, whose
		// pattern lands the count when it lays the window (`landSemis`).
		if (unitWork == UNIT_SEMIS && !m.quantized() && !m.quantNode) {
			m.preShift += (float)semis(amount) / 12.f;
			m.landSemis += semis(amount);
			return;
		}
		shapeOffset(m, semis(amount), volts(amount));
	}
	float act(Signal& s, const Mapping& win, float amount) {
		return transStage(s, win, semis(amount), volts(amount));
	}
	/** What the bar adds to a jack, in semitones. */
	float shift(const Mapping& win, float amount) const {
		return shiftOf(Signal(), win, semis(amount), volts(amount));
	}
	/** How the bar reads, for a tooltip: volts or semitones. The bar is
	passed in because a quantity holds the value and the core holds what it
	means. */
	std::string reading(float a) const {
		a = clamp(a, -1.f, 1.f);
		if (relaxed(unit) == UNIT_VOLTS)
			return decimals(volts(a), 1, 3, true) + " V";
		if (relaxed(unit) == UNIT_ROLL) {
			const int c = cents(a);
			return c == 0 ? "0" : string::f("%+.2f semitones", (float)c / 100.f);
		}
		const int v = semis(a);
		return v == 0 ? "0" : string::f("%+d semitones", v);
	}
};

} // namespace rig
