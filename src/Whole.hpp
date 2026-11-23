// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>

/** The whole number nearest `x`, a half going away from nought: where a
float or a double stops being one and becomes a count, the one rounding the
plugin has (`std::round` and `std::lround` cast to an int, to the bit, for
every float and every double whose nearest whole number an int holds; past
that neither says anything).

Written out because the library's is a call on x86, where the toolchain
keeps the rounding mode and the traps in mind and will not open it, and a
count is read from a param on every sample: a Trg's rings, a Seq's mode, a
trigger's length. There it is the conversion toward nought, which is one
instruction, and two comparisons that say whether the part dropped was a
half or more. Each is made in a double, which holds an int and its half
exactly, so nothing in it is rounded and no way of compiling it can move
the answer. An ARM has this very rounding as one instruction, which the
library's opens into, so there the library's it stays. */
template <typename Float>
inline int wholeOf(Float x) {
#if defined(__aarch64__)
	return (int)std::lround(x);
#else
	const int toward = (int)x;
	const double at = (double)x, from = (double)toward;
	return toward + (at >= from + 0.5 ? 1 : 0) - (at <= from - 0.5 ? 1 : 0);
#endif
}
