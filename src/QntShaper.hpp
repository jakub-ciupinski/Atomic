// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/** A Qnt's shaper, the stroke with a modifier held that reshapes it rather
than painting keys. A part of Qnt.cpp in a file of its own: Qnt.cpp includes
it once, inside its anonymous namespace, where it stands on the Qnt and the
key and row boxes above it, and it is not a header to include anywhere
else. */

/** A stroke with a modifier held reshapes the Qnt instead of painting its
keys, up the screen for more and down for less:

shift, the octaves of the keyboard or the roll, one to ten, and never a
change of mode, which is the title's arrows' alone; in the divider, nothing;
command (control off a Mac), a keyboard's pattern turned round its octaves,
a change of mode, and on a roll its notes moved along it;
command and shift, on a keyboard the key under the press tuned in cents, or
a roll's window along the piano, the notes staying at their pitch.

The transpose is the head screen's alone (`QntHead`).

What the stroke changes is recorded for undo in one go, and while it goes
what it has made is said first on the hint line (`Hand::readout`), as a
lamp's and a fader's strokes are, a tooltip being the player's to switch off. Every key holds one of
these, and so does the whole screen behind them (`ShapeCatch`), so a
stroke with a modifier reshapes from anywhere on it, the playhead's strip
and a divider's face included. */
/** Twelve keys' bits turned up by `t` semitones, round the octave. */
inline uint16_t rotateMask(uint16_t bits, int t) {
	t = eucMod(t, 12);
	return t == 0 ? bits : (uint16_t)(((bits << t) | (bits >> (12 - t))) & 0xfff);
}
struct QntShaper {
	enum Shape { SHAPE_NONE, SHAPE_OCTAVES, SHAPE_NOTES, SHAPE_WINDOW, SHAPE_TUNE };
	// The key under the press, set by whoever begins the stroke, for its
	// tuning; minus one where no key is.
	int tuneKey = -1;
	// The widget the stroke is dragged on, set by whoever begins it, whose
	// readout the hint line shows while it is the one dragged.
	const Widget* by = nullptr;
	void say(const std::string& text) { Hand::readout(by, text); }
	int shape = SHAPE_NONE;
	float shapeY = 0.f;
	int shapeFrom = 0; // the setting the stroke started from, a count, rounded once
	GestureChanges keysWere; // the keyboard's keys as the stroke found them, for an opening's undo
	// Every Qnt the stroke reshapes: the one under the hand, and for the
	// octaves, the transpose and where the keys stand the whole block it
	// stands in, so a block is one shape. Held by id, as the keys below: an
	// undo in the middle of the stroke may take one away (`HeldModule`).
	std::vector<HeldModule> held;
	/** The members that still stand. */
	std::vector<Qnt*> members() const {
		std::vector<Qnt*> out;
		for (const HeldModule& h : held)
			if (Qnt* q = h.as<Qnt>())
				out.push_back(q);
		return out;
	}
	struct Key {
		HeldModule q;
		int param;
		float was;
	};
	std::vector<std::vector<Key>> rows; // per member, its switches in pitch order, and what each held
	std::vector<float> froms;           // per member, the setting the stroke began from
	int shapeLo = 0, shapeHi = 0;       // how far a roll's notes may go
	int shapeStepsWas = 0;

	static int shapeOf(Qnt* k, int key = -1) {
		const bool cmd = Hand::among(G_KEY_ROTATE), shift = Hand::among(G_KEY_OCTAVES);
		if (!k)
			return SHAPE_NONE;
		// Read off the params, the hand's own side of the mode, so a stroke
		// right after an arrow takes the mode the arrow came to.
		const int mode = k->modeNow();
		const bool roll = mode < 0;
		// The divider has no octaves and no keys: no stroke reshapes it.
		if (mode == 0)
			return SHAPE_NONE;
		// Command moves the notes: a keyboard's turned round its octaves, a
		// change of mode, a roll's along it. With shift too, a keyboard's
		// key is tuned, up to 99 cents either way, and off a key nothing;
		// a roll is moved along the piano.
		if (cmd && shift)
			return roll ? SHAPE_WINDOW : key >= 0 ? SHAPE_TUNE : SHAPE_NONE;
		if (cmd)
			return SHAPE_NOTES;
		if (shift)
			return SHAPE_OCTAVES;
		return SHAPE_NONE;
	}
	/** The Qnts touching this one either side, and it, left to right, as
	the engine has the block (`touchingOf`): a bypassed module passed over. */
	static std::vector<Qnt*> blockOf(Qnt* k) {
		return touchingOf(APP->scene->rack->getModule(k->id), k);
	}
	/** This Qnt and the Qnts touching it either side for as long as a test
	holds between it and each of them, left to right, walked the same way. */
	static std::vector<Qnt*> besideWhile(Qnt* k, const std::function<bool(Qnt*, Qnt*)>& keep) {
		std::vector<Qnt*> out;
		out.push_back(k);
		ModuleWidget* mine = APP->scene->rack->getModule(k->id);
		if (!mine)
			return out;
		for (int side = 0; side < 2; side++)
			for (ModuleWidget* mw = besideInRun(mine, side); mw; mw = besideInRun(mw, side)) {
				Qnt* q = dynamic_cast<Qnt*>(mw->module);
				if (!q || !keep(k, q))
					break;
				if (side == 0)
					out.insert(out.begin(), q);
				else
					out.push_back(q);
			}
		return out;
	}
	/** A roll's switches, low to high, the top C included. */
	static std::vector<int> rollSwitches() {
		std::vector<int> out;
		for (int st = -Qnt::ROLL_BELOW; st <= Qnt::ROLL_BELOW; st++)
			out.push_back(Qnt::rollParam(st));
		return out;
	}
	/** Whether a Qnt is a roll with the same notes on it as mine: the rolls
	a stroke moving notes carries along together. */
	static bool sameRoll(Qnt* a, Qnt* b) {
		if (!relaxed(b->rollNow) || relaxed(b->octavesNow) <= 0)
			return false;
		for (int p : rollSwitches())
			if ((a->params[p].getValue() > 0.5f) != (b->params[p].getValue() > 0.5f))
				return false;
		return true;
	}
	bool roll = false; // the stroke began on a roll
	int paramOf() const {
		return shape == SHAPE_OCTAVES ? (roll ? Qnt::ROLL_OCT_PARAM : Qnt::KEYS_OCT_PARAM)
		       : shape == SHAPE_WINDOW ? Qnt::ROLL_PARAM : Qnt::DETUNE_PARAM + tuneKey;
	}

	/** Asked at the press: whether a modifier is held at all, in which case
	the stroke is this one's, even where it has nothing to do. */
	bool begin(Qnt* k) {
		shape = shapeOf(k, tuneKey);
		if (shape == SHAPE_NONE)
			return Hand::among(G_KEY_ROTATE) || Hand::among(G_KEY_OCTAVES);
		roll = k->modeNow() < 0;
		shapeY = APP->scene->rack->getMousePos().y;
		held.clear();
		rows.clear();
		froms.clear();
		shapeStepsWas = 0;
		if (shape != SHAPE_NOTES) {
			// A key's tuning is that key's alone.
			const std::vector<Qnt*> ms = shape == SHAPE_TUNE ? std::vector<Qnt*>(1, k) : blockOf(k);
			// The Cs named on the block's leftmost roll alone, one set of
			// bearings for the whole block.
			ms.front()->orientHeld = shape == SHAPE_OCTAVES || shape == SHAPE_WINDOW;
			for (Qnt* q : ms) {
				held.push_back(HeldModule(q));
				froms.push_back(q->params[paramOf()].getValue());
			}
			shapeFrom = (int)std::round(k->params[paramOf()].getValue());
			// A keyboard opened past one octave for the first time copies its
			// first octave up (`openKeyboardWith`), so the keys are part of
			// what the stroke changed and go into its undo, and an undone
			// opening takes the copied keys back.
			keysWere.was.clear();
			if (shape == SHAPE_OCTAVES && !roll)
				for (Qnt* q : ms) {
					for (int key = 0; key < 12 * MAX_OCTAVES; key++)
						keysWere.note(q, Qnt::keyParam(key));
					keysWere.note(q, Qnt::EXPANDED_PARAM);
				}
		}
		else {
			// For a roll, the same notes either side, as far as they go.
			const std::vector<Qnt*> ms = roll ? besideWhile(k, sameRoll) : std::vector<Qnt*>(1, k);
			shapeLo = -1000;
			shapeHi = 1000;
			for (Qnt* q : ms) {
				held.push_back(HeldModule(q));
				std::vector<Key> row;
				if (roll) {
					// Every switch on the piano, and the notes kept on it,
					// the stroke held short of either end of it.
					const std::vector<int> sw = rollSwitches();
					for (int i = 0; i < (int)sw.size(); i++) {
						const float v = q->params[sw[i]].getValue();
						row.push_back({HeldModule(q), sw[i], v});
						if (v > 0.5f) {
							shapeLo = std::max(shapeLo, -i);
							shapeHi = std::min(shapeHi, (int)sw.size() - 1 - i);
						}
					}
				}
				else {
					// A keyboard's pattern, its own keys round its octaves.
					const int n = std::max(1, (int)relaxed(q->octavesNow));
					for (int key = 0; key < 12 * n; key++)
						row.push_back({HeldModule(q), Qnt::keyParam(key), q->params[Qnt::keyParam(key)].getValue()});
				}
				rows.push_back(row);
			}
		}
		move(k);
		return true;
	}
	void move(Qnt* k) {
		if (!k || shape == SHAPE_NONE)
			return;
		// A step every few millimetres: octaves are few and far apart, a
		// semitone is a small move.
		// A cent is finer still: the whole ninety-nine in thirty millimetres.
		const float pitch = mm2px(shape == SHAPE_OCTAVES ? 4.f : shape == SHAPE_TUNE ? 0.3f : 1.8f);
		int steps = (int)std::round((shapeY - APP->scene->rack->getMousePos().y) / pitch);
		const int n = std::max(1, (int)relaxed(k->octavesNow));
		// Past an end the stroke takes hold again where it stands, as every
		// screen does, so turning back moves the value at once: the anchor
		// slides by the steps the end refused.
		auto refuse = [&](int wanted, int got) {
			if (wanted != got)
				shapeY -= (float)(wanted - got) * pitch;
		};
		if (shape == SHAPE_OCTAVES) {
			// Up the screen is more octaves, keyboard and roll alike, held at
			// one and at ten with the stroke taking hold again where it
			// stands, and never past one into another mode.
			const int from = shapeFrom, o = clamp(from + steps, 1, MAX_OCTAVES);
			refuse(from + steps, o);
			for (Qnt* q : members()) {
				if (!roll && o > 1 && !q->expandedNow())
					q->openKeyboardWith([q](int p, float v) { q->params[p].setValue(v); });
				q->params[paramOf()].setValue((float)o);
			}
			say(k->paramQuantities[paramOf()]->getLabel() + string::f(": %d", o));
		}
		else if (shape == SHAPE_WINDOW) {
			// The roll is taken hold of: up the screen carries what is on it
			// up, as a hand dragging paper does, so the window goes down the
			// piano; the other way reads backwards.
			const int w = Qnt::rollShiftIn(shapeFrom - steps, n);
			refuse(w, shapeFrom - steps); // the window runs the other way
			for (Qnt* q : members())
				q->params[Qnt::ROLL_PARAM].setValue((float)w);
			say("Roll from: " + noteName(Qnt::rollBase(n, w)));
		}
		else if (shape == SHAPE_TUNE) {
			// Up the screen is sharper, a cent a step, held at ninety-nine
			// either way with the stroke taking hold again where it stands,
			// so turning back moves it at once (the over-drag rule).
			const int c = clamp(shapeFrom + steps, -Qnt::MAX_CENTS, Qnt::MAX_CENTS);
			refuse(shapeFrom + steps, c);
			k->params[paramOf()].setValue((float)c);
			say(k->paramQuantities[paramOf()]->getLabel() + (c == 0 ? std::string(": in tune") : string::f(": %+d cents", c)));
		}
		else {
			// A roll's notes move along the piano and stop short of either
			// end, the stroke taking hold again where it stands; a turn round
			// a keyboard's octaves has no end, so nothing is refused.
			if (roll) {
				const int wanted = steps;
				steps = clamp(steps, shapeLo, shapeHi);
				refuse(wanted, steps);
			}
			if (steps != shapeStepsWas || steps == 0) {
				for (std::vector<Key>& row : rows) {
					const int count = (int)row.size();
					for (int i = 0; i < count; i++) {
						// A roll's notes move along it and off nothing; a
						// keyboard's come round.
						const int from = roll ? i - steps : eucMod(i - steps, count);
						if (Qnt* q = row[i].q.as<Qnt>())
							q->params[row[i].param].setValue(from >= 0 && from < count ? row[from].was : 0.f);
					}
				}
			}
			shapeStepsWas = steps;
			if (roll)
				say(string::f("Notes moved: %+d", steps));
			else {
				// The turn and the mode it has made of this keyboard's own
				// first octave, named as the transpose names its keys, and
				// nothing more: one short piece of the line.
				uint16_t mask = 0;
				for (int i = 0; i < 12; i++)
					if (k->params[Qnt::keyParam(i)].getValue() > 0.5f)
						mask |= (uint16_t)(1u << i);
				const int tr = k->chromaticNow() ? (int)std::round(k->params[Qnt::TRANSPOSE_PARAM].getValue()) : 0;
				const std::string nm = patternNameFrom(rotateMask(mask, tr), eucMod(tr, 12));
				say(string::f("Rotation: %+d", steps) + (nm.empty() ? std::string() : ", " + nm));
			}
		}
		for (Qnt* q : members())
			q->poke.store(true, std::memory_order_relaxed);
	}
	void end(Qnt* k) {
		if (!k || shape == SHAPE_NONE) {
			shape = SHAPE_NONE;
			return;
		}
		say("");
		for (Qnt* q : members())
			q->orientHeld = false;
		// One recorder for every gesture: every param the stroke touched is
		// noted at the value it had, and one push records those that moved as
		// one undo, or none.
		GestureChanges stroke;
		if (shape != SHAPE_NOTES)
			for (size_t i = 0; i < held.size(); i++)
				stroke.noteAt(held[i].get(), paramOf(), froms[i]);
		else
			for (std::vector<Key>& row : rows)
				for (Key& key : row)
					stroke.noteAt(key.q.get(), key.param, key.was);
		for (const GestureChanges::Was& w : keysWere.was)
			stroke.noteAt(w.module.get(), w.param, w.value);
		keysWere.was.clear();
		stroke.push(shape == SHAPE_OCTAVES ? "change octaves" : shape == SHAPE_NOTES ? "move notes" :
		            shape == SHAPE_TUNE ? "detune key" : "rotate keys");
		shape = SHAPE_NONE;
	}
};
