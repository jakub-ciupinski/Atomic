// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <vector>

/** A RIG's note as rows, the way its glass wraps it, and the caret's walk
over them. Rack's text field steps up, down, home and end over the text's
lines, cut at its newlines, while the glass wraps a long line into several
rows, so an arrow from the start of a wrapped row landed at the same count
of letters into the line above, mid-row, and home went to the start of the
whole line. Here the rows
are the drawn ones, found once by the field (`NoteField::breakRows`) and
used alike to draw the caret and to walk it, and a column is a count of
code points, never of bytes, so no step lands inside a letter. Nothing here
draws, so the walk can be tested headless. */

namespace rig {
namespace note {

/** One row as the glass shows it: bytes [start, end), the space a soft wrap
breaks on and a line's newline left out. */
struct Line {
	int start = 0;
	int end = 0;
};

/** Whether byte `c` begins a code point. */
inline bool leads(unsigned char c) {
	return (c & 0xC0) != 0x80;
}

/** The code points between two bytes of `t`. */
inline int columns(const std::string& t, int from, int to) {
	int n = 0;
	for (int i = from; i < to && i < (int)t.size(); i++)
		n += leads((unsigned char)t[i]);
	return n;
}

/** The byte `cols` code points on from `from`, never past `limit`. */
inline int advance(const std::string& t, int from, int limit, int cols) {
	int i = from;
	while (cols > 0 && i < limit) {
		i++;
		while (i < limit && !leads((unsigned char)t[i]))
			i++;
		cols--;
	}
	return i;
}

/** The rows completed: a text ending on a newline, or none at all, has a
row after it that the breaking gives none of, where the caret then stands. */
inline void close(const std::string& t, std::vector<Line>& rows) {
	if (t.empty() || t.back() == '\n')
		rows.push_back({(int)t.size(), (int)t.size()});
}

/** The row the caret at byte `pos` stands on: the last that starts at or
before it, so a caret at a wrapped row's first letter is on that row and
one in the spaces a wrap broke on is on the row they end. */
inline int rowOf(const std::vector<Line>& rows, int pos) {
	int r = 0;
	for (int i = 0; i < (int)rows.size(); i++)
		if (rows[i].start <= pos)
			r = i;
	return r;
}

/** Where a key moves the caret over the rows. `goal` is the column an up
or a down keeps, carried over a run of them (`NoteField::goal`) and set
from the caret where it is negative. */
enum Move { UP, DOWN, HOME, END };
inline int move(const std::string& t, const std::vector<Line>& rows, int pos, Move m, int& goal) {
	if (rows.empty())
		return pos;
	const int r = rowOf(rows, pos);
	const Line& l = rows[r];
	switch (m) {
		case HOME:
			return l.start;
		case END:
			return l.end;
		case UP:
		case DOWN: {
			if (goal < 0)
				goal = columns(t, l.start, pos);
			if (m == UP && r == 0)
				return 0;
			if (m == DOWN && r + 1 == (int)rows.size())
				return (int)t.size();
			const Line& to = rows[m == UP ? r - 1 : r + 1];
			return advance(t, to.start, to.end, goal);
		}
	}
	return pos;
}

} // namespace note
} // namespace rig
