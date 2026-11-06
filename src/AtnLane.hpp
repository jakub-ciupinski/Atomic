// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/** One lane of an Atn's glass, the fader and its handle. A part of Atn.cpp
in a file of its own: Atn.cpp includes it once, inside its anonymous
namespace, and it is not a header to include anywhere else. */

/** One lane of the glass: a fader dragged up and down, its handle a small
seven-segment screen reading its own value and nothing of the other one's.

The lane knows which of the two it is and works out everything else from the
module: where its handle stands, how a drag snaps, what its reading says, and
whether its own jack is driving it. */
struct AtnLane : LevelParam {
	bool offsetLane = false; // the offset's lane, or the gain's
	/** Which lane, so a stroke that began on an offset goes on sweeping
	offsets when it crosses onto the Atn next door rather than grabbing that
	one's gain. */
	int lane() override { return offsetLane ? 1 : 0; }
	Atn* getAtn() { return paramModule<Atn>(this); }

	/** The travel, minus one to one: the offset's param is that already and
	the gain's is volts over ten. */
	float travel() {
		engine::ParamQuantity* pq = getParamQuantity();
		const float v = pq ? pq->getValue() : (offsetLane ? 0.f : CV_VOLTS);
		return offsetLane ? clamp(v, -1.f, 1.f) : clamp(v / CV_VOLTS, -1.f, 1.f);
	}
	bool jackLive() {
		Atn* r = getAtn();
		return r && (offsetLane ? r->ofsCv.live() : r->gainCv.live());
	}
	/** Whether this lane's cable reaches nothing past the Atn
	(`Atn::idleLanesNow`): the handle is then drawn as one whose jack is
	empty, unlit in the Atn's own colour, while the groove and the arrow keep
	the cable's, since the cable still moves the glass. */
	bool reachesNothing() {
		Atn* r = getAtn();
		return r && (relaxed(r->idleLanesNow) >> lane()) & 1;
	}
	/** The colour of the cable in this lane's own jack, or the hint grey. */
	NVGcolor jackCable() {
		Atn* r = getAtn();
		if (!r)
			return hintInk();
		return jackInk(relaxed(offsetLane ? r->ofsRGB : r->vcaRGB));
	}

	/** How deep the glass's bevel reaches in, which is where the working
	face begins. */
	float bevel() { return GLASS_BEVEL; }
	/** The output's volts, top to bottom of the glass, straight: ten volts
	either way is the glass's inner edge, inside the bevel, so a gain wide
	open is black from edge to edge. The face is `GlassZone`'s, counted from
	its middle rather than its foot, since a lane's nought is its middle. */
	float mid() { return box.size.y / 2.f; }
	float half() { return box.size.y / 2.f - bevel(); }
	float yOf(float volts) { return mid() - clamp(volts / scaleVolts(), -1.f, 1.f) * half(); }
	/** How many volts the glass's edge is from its middle, as last settled
	(`step`): ten, or on a span of notes as `scaleWanted` says. */
	float scaleVolts() { return shownScale > 0 ? (float)shownScale : (float)scaleWanted(); }
	/** Ten, or five on a span of notes, whose widest is ten octaves, five
	either side, so the widest span fills the glass as a wide-open gain does
	anywhere else; widened a whole volt at a time where the span with the
	offset under it reaches past five, up to ten, so a small range carried
	far by a transpose still shows its strip. Its ends counted in whole
	semitones, the span's own unit, from the one place they leave the volts,
	so a span ending on five volts exactly keeps the glass at five. */
	int scaleWanted() {
		const int edge = (int)CV_VOLTS;
		if (!notesSpan())
			return edge;
		float lo, hi;
		spanOf(gainVolts(), lo, hi);
		const float off = offVolts();
		const int reach = std::max(std::abs((int)std::round((off + lo) * 12.f)), std::abs((int)std::round((off + hi) * 12.f)));
		return clamp((reach + 11) / 12, edge / 2, edge);
	}
	/** The scale the glass is drawn on, settled once a frame and held while
	any Atn's lane is under the hand: a stroke maps the pointer on the
	scale it began on, so a scale widening under it can never carry the
	value further than the hand went. Nought until the first frame. */
	int shownScale = 0;
	void step() override {
		const bool stroke = APP && APP->event && dynamic_cast<AtnLane*>(APP->event->getDraggedWidget());
		if (!stroke || shownScale == 0)
			shownScale = scaleWanted();
		LevelParam::step();
	}

	/** Where the output's nought stands, as a fraction of ten volts: the
	offset in force, fader and jack together, which is what a signal of
	nothing comes out at. */
	float zeroAt() {
		Atn* r = getAtn();
		return r ? clamp(r->ofsAmount(), -1.f, 1.f) : 0.f;
	}
	/** The offset in force and the gain in force, in volts. */
	float offVolts() { return zeroAt() * CV_VOLTS; }
	float gainVolts() {
		Atn* r = getAtn();
		return r ? clamp(r->gainInForce(), -CV_VOLTS, CV_VOLTS) : CV_VOLTS;
	}
	/** The two faders where the hand left them, in volts: what the handles
	stand on. A cable in either jack moves the black and the centre line
	and never a handle, so a handle is always where it was put. */
	float knobOffVolts() {
		Atn* r = getAtn();
		return r ? r->ofsKnob() * CV_VOLTS : 0.f;
	}
	float knobGainVolts() {
		Atn* r = getAtn();
		return r ? clamp(r->gainSet(), -CV_VOLTS, CV_VOLTS) : CV_VOLTS;
	}

	/** Whether the gain is a span of notes: a scale in reach, either side.
	See `atnSpan`. */
	bool notesSpan() {
		Atn* r = getAtn();
		return r && relaxed(r->notesNow);
	}
	/** Where a gain of so many volts reaches, from the offset: the span of
	notes where it is one, and as far either side where it is not. */
	void spanOf(float g, float& lo, float& hi) {
		if (notesSpan())
			noteSpan(g, spanScale(), lo, hi);
		else {
			lo = -std::fabs(g);
			hi = std::fabs(g);
		}
	}
	/** Where the gain's handle stands for a gain of so many volts: at the
	gain on the one scale, or on a span of notes at the end it grows from,
	its top, or its foot below the line; so a step that grows the span
	underneath leaves the handle where it is and only its reading moves. */
	float gainY(float g) {
		if (!notesSpan())
			return yOf(g);
		float lo, hi;
		noteSpan(g, spanScale(), lo, hi);
		return yOf(g < 0.f ? lo : hi);
	}
	/** The scale a span is laid on (`Atn::spanScale`). */
	Scale spanScale() {
		Atn* r = getAtn();
		return r ? r->spanScale() : Scale();
	}
	/** The scale in reach, as the last wake published it. */
	Scale snap() {
		Atn* r = getAtn();
		return r ? r->snapScale.load() : Scale();
	}

	/** What the hand is asking for, Rack's own convention: a plain stroke is
	continuous, shift held lands on the coarse steps, whole volts or octaves,
	and command held is the plain stroke at a fiftieth of the pace, as on a
	CV's fader, and command with shift at a five hundredth. */
	enum Hand { HAND_PLAIN, HAND_COARSE, HAND_FINE, HAND_ULTRA };
	int hand() {
		// In notes there is nothing finer than a semitone, so command is the
		// plain stroke there.
		// `rig::Hand`, the keyboard's, since `Hand` here is the enum above.
		if (rig::Hand::among(G_LANE_FINE))
			return inNotes() ? HAND_PLAIN : rig::Hand::among(G_LANE_FINER) ? HAND_ULTRA : HAND_FINE;
		return rig::Hand::held(offsetLane ? G_LANE_OFFSET_VOLTS : G_LANE_GAIN_STEPS) ? HAND_COARSE : HAND_PLAIN;
	}
	static bool fineHand(int h) { return h == HAND_FINE || h == HAND_ULTRA; }
	static float paceOf(int h) { return h == HAND_ULTRA ? 1.f / ULTRA_PACE : h == HAND_FINE ? 1.f / FINE_PACE : 1.f; }
	/** Whether this lane counts in notes: the gain on a span of notes, or
	the offset in semitones. A plain stroke then lands on a note of the
	pattern, a semitone where there is none, and shift on an octave. */
	bool inNotes() {
		Atn* r = getAtn();
		if (!r)
			return false;
		if (offsetLane)
			return relaxed(r->ofs.unit) == OfsCore::UNIT_SEMIS;
		return notesSpan();
	}

	/** Where a stroke took hold, and with which hand. A plain or a shift
	stroke follows the pointer; one with command held goes by how far the
	pointer has moved since the key went down, at `FINE_PACE`, so a hundredth
	of a volt is a pixel or two of hand at any zoom. A stroke that has been
	fine stays by the distance when the key comes up, so the handle does not
	leap back to the pointer. */
	int heldStroke = -1, heldHand = -1;
	bool heldRelative = false;
	float heldY = 0.f, heldValue = 0.f, heldSemis = 0.f;
	// A gain stroke begun at either end of the fader, or already past it,
	// may boost: carried past the end it began at, it is not stopped there.
	// Begun inside, it is capped at the ends as ever.
	int boostStroke = -1;
	float boostFrom = 0.f, boostY = 0.f;
	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			strokeNo()++;
			engine::ParamQuantity* pq = getParamQuantity();
			if (!offsetLane && pq && !notesSpan() && std::fabs(pq->getValue()) >= CV_VOLTS) {
				boostStroke = strokeNo();
				boostFrom = pq->getValue();
				boostY = e.pos.y;
			}
		}
		LevelParam::onButton(e);
	}
	/** Takes hold again where the pointer is now whenever a new stroke
	begins or the hand changes under it. A stroke pressed with command held
	goes by distance from the first; plain and shift both follow the
	pointer, so going between them takes nothing relative. */
	void takeHold(float y, float value, int h) {
		if (heldStroke != strokeNo()) {
			heldStroke = strokeNo();
			heldRelative = fineHand(h);
		}
		else if (heldHand == h)
			return;
		else
			heldRelative = heldRelative || fineHand(h) || fineHand(heldHand);
		heldHand = h;
		heldY = y;
		heldValue = value;
		heldSemis = value * 12.f;
	}
	/** The travel a stroke has reached, as a fraction of the lane: where
	the pointer is, or where the stroke has moved the handle to. `now` is the
	handle's own travel. */
	float strokeTravel(float y, float now, int h) {
		takeHold(y, now, h);
		if (!heldRelative || inNotes())
			return travelAt(y);
		const float pace = paceOf(h);
		const float raw = heldValue + (heldY - y) / half() * pace;
		const float t = clamp(raw, -1.f, 1.f);
		// Past an end the stroke takes hold again where it stands, so no
		// travel is kept past the limit and turning back moves the handle at
		// once.
		if (raw != t) {
			heldValue = t;
			heldY = y;
		}
		return t;
	}

	/** Whether a span this many semitones wide ends on a note of the scale
	at the end it grows from, with the offset under it (`spanEndsOnScale`). */
	bool spanEndOnScale(int semis, const Scale& scale) {
		Atn* r = getAtn();
		// The offset as published, moved along the pattern as a CV's note is:
		// never `ofsVolts`, which on the UI thread would write the engine's
		// own working field.
		const int ofsSemis = r ? (int)std::round(r->ofsAmount() * (float)OfsCore::SEMI_REACH) : 0;
		return spanEndsOnScale(semis, scale, ofsSemis);
	}
	/** A stroke on a span of notes goes by how far the pointer has moved
	rather than by where it is, since one height is two spans, at an octave
	every half volt of travel, which keeps the handle near the hand. A plain
	stroke lands on the widths whose growing end is a note of the scale, and
	shift on whole octaves. The width is a count of semitones, an integer
	from the stroke on. A press alone moves nothing. */
	float spanFromY(float y, float now, int h) {
		takeHold(y, now, h);
		heldRelative = true;
		const float volts = (heldY - y) / (half() / scaleVolts());
		const int held = (int)std::round(heldSemis);
		int semis = held;
		if (h == HAND_COARSE) {
			const int steps = (int)std::trunc(2.f * volts);
			if (steps != 0) {
				// Whole octaves: the octave at or under the width going up,
				// at or over it going down.
				const int from = steps > 0 ? eucDiv(held, 12) : -eucDiv(-held, 12);
				semis = 12 * (from + steps);
			}
		}
		else {
			// The nearest width on the scale to where the octaves would have
			// the hand, searched outward from it.
			Atn* r = getAtn();
			const Scale scale = r ? r->snapScale.load() : Scale();
			const int want = held + (int)std::round(24.f * volts);
			semis = want;
			for (int d = 0; d < 24 && scale.any(); d++) {
				if (spanEndOnScale(want + d, scale)) {
					semis = want + d;
					break;
				}
				if (spanEndOnScale(want - d, scale)) {
					semis = want - d;
					break;
				}
			}
		}
		// Past an end the stroke takes hold again where it stands, as the
		// lane's other strokes do, so turning back moves the span at once.
		// Semitones are whole here, so the end is met exactly.
		const int top = 12 * (int)CV_VOLTS;
		if (semis > top || semis < -top) {
			semis = std::max(-top, std::min(top, semis));
			heldSemis = (float)semis;
			heldY = y;
		}
		return (float)semis / 12.f;
	}

	/** What a pointer at this height sets the lane to, as a fraction of its
	reach: each fader on the glass's own straight scale and nothing else's,
	the offset in volts, the gain as the volts a full ten at the input would
	make, so moving one never moves the other. */
	float travelAt(float y) { return clamp((mid() - y) / half(), -1.f, 1.f); }

	/** In volts a plain stroke is the float the pointer makes, shift lands
	on whole volts and command is the plain stroke slowed; in notes a plain
	stroke lands on a note of the pattern in reach, a semitone where there is
	none, and shift on an octave. */
	void setFromY(float y) override {
		engine::ParamQuantity* pq = getParamQuantity();
		Atn* r = getAtn();
		if (!pq)
			return;
		const int h = hand();
		if (offsetLane) {
			// On a span of notes the glass is five volts either side, or
			// wider (`scaleWanted`), and the offset's handle goes where the
			// pointer is on it.
			const float k = scaleVolts() / CV_VOLTS;
			float a = strokeTravel(y, pq->getValue() / k, h) * k;
			const int u = r ? relaxed(r->ofs.unit) : OfsCore::UNIT_VOLTS;
			if (u == OfsCore::UNIT_ROLL) {
				// Over a roll: a semitone a step, an octave under shift and a
				// cent under command, each a count once taken from the travel.
				const int reach = OfsCore::SEMI_REACH;
				if (h == HAND_COARSE)
					a = (float)(12 * (int)std::round(a * (float)(reach / 12))) / (float)reach;
				else if (fineHand(h))
					a = (float)(int)std::round(a * (float)(reach * 100)) / (float)(reach * 100);
				else
					a = (float)(int)std::round(a * (float)reach) / (float)reach;
			}
			else if (u == OfsCore::UNIT_SEMIS) {
				// A count of semitones, whole from here on. A transposition
				// on a keyboard lands on the keyboard: the plain stroke snaps
				// to the notes of the pattern in reach, the same way the
				// gain's does, so a sequence can only be moved by an interval
				// the pattern actually has and a snapped sequence stays
				// snapped. In front of a Qnt, its pattern, which its window
				// lands the bar on.
				const Scale scale = r ? r->spanScale() : Scale();
				const int reach = OfsCore::SEMI_REACH;
				int semis = h == HAND_COARSE ? 12 * (int)std::round(a * (float)(reach / 12))
				                             : (int)std::round(a * (float)reach);
				if (h != HAND_COARSE && scale.any())
					semis = shiftOnScale(semis, scale);
				a = (float)semis / (float)reach;
			}
			else if (h == HAND_COARSE) {
				// Whole volts, rounded once from the travel.
				const int volts = (int)std::round(a * (float)OfsCore::MAX_VOLTS);
				a = (float)volts / (float)OfsCore::MAX_VOLTS;
			}
			pq->setValue(clamp(a, -1.f, 1.f));
		}
		else if (notesSpan())
			pq->setValue(spanFromY(y, pq->getValue(), h));
		else if (boostStroke == strokeNo() && std::fabs(boostFrom) > CV_VOLTS) {
			// Begun already boosted: by how far the pointer moves, since the
			// fader has no height for it.
			const float pace = paceOf(h);
			float volts = boostFrom + (boostY - y) / half() * CV_VOLTS * pace;
			if (h == HAND_COARSE)
				volts = (float)(int)std::round(volts);
			pq->setValue(clamp(volts, -GAIN_MAX, GAIN_MAX));
		}
		else if (boostStroke == strokeNo() && !fineHand(h) &&
		         (boostFrom > 0.f ? mid() - y > half() : y - mid() > half())) {
			// Begun at an end and carried past it: the pointer's overshoot
			// beyond that end is the boost, a fader's height a further x1.
			const float over = (boostFrom > 0.f ? (mid() - y) : (y - mid())) / half() - 1.f;
			float volts = (boostFrom > 0.f ? 1.f : -1.f) * CV_VOLTS * (1.f + over);
			if (h == HAND_COARSE)
				volts = (float)(int)std::round(volts);
			pq->setValue(clamp(volts, -GAIN_MAX, GAIN_MAX));
		}
		else {
			float volts = strokeTravel(y, pq->getValue() / CV_VOLTS, h) * CV_VOLTS;
			if (h == HAND_COARSE)
				volts = (float)(int)std::round(volts);
			pq->setValue(clamp(volts, -CV_VOLTS, CV_VOLTS));
		}
		rig::Hand::readout(this, fineHand(h) ? pq->getDisplayValueString() + pq->getUnit() : std::string());
		if (r) {
			r->touch();
		}
	}
	/** Two clicks set this lane back to its default, what a fresh Atn has:
	the gain wide open, the offset at nought. The other lane is not touched,
	which is the whole point of there being two of them. */
	void onDoubleClick(const DoubleClickEvent& e) override {
		LevelParam::onDoubleClick(e);
		if (Atn* r = getAtn())
			r->touch();
	}

	/** What the handle says: a number and nothing else, the unit left to be
	read from the rig: volts where the rig is in volts, semitones where it
	is in notes, steps where a Qnt turns it. The tooltip says which. */
	std::string reading() {
		engine::ParamQuantity* pq = getParamQuantity();
		return reading(pq ? pq->getValue() : (offsetLane ? 0.f : CV_VOLTS));
	}
	/** The same for any setting of the lane, which is how the ghost reads
	the fader the cable has made. */
	std::string reading(float value) {
		Atn* r = getAtn();
		const float a = offsetLane ? clamp(value, -1.f, 1.f) : clamp(value / CV_VOLTS, -1.f, 1.f);
		if (offsetLane) {
			const int u = r ? relaxed(r->ofs.unit) : OfsCore::UNIT_VOLTS;
			if (u == OfsCore::UNIT_VOLTS)
				return decimals(OfsCore::volts(a), 1, 3, true);
			if (u == OfsCore::UNIT_ROLL)
				return decimals((float)OfsCore::cents(a) / 100.f, 2, 3, true);
			const int v = OfsCore::semis(r ? r->ofsHeard(a) : a);
			return v == 0 ? "0" : string::f("%+d", v);
		}
		const float v = value;
		if (notesSpan())
			return string::f("%d", (int)std::round(clamp(v, -CV_VOLTS, CV_VOLTS) * 12.f)); // the span, in semitones, x1 at most as the rig takes it
		return decimals(v / CV_VOLTS, 2, 3);
	}
	/** A reading as three seven-segment cells, the way a small screen of
	three digits would show it: no plus, a minus taking a cell of its own,
	and as many decimals as the three cells leave room for, so 0.60 and
	-0.6, 2.0 and -2.0, 10.0 and -10 all stand in the same three cells. */
	static std::string cells3(std::string v) {
		if (!v.empty() && v[0] == '+')
			v = v.substr(1);
		const size_t dot = v.find('.');
		if (dot == std::string::npos)
			return v;
		const float x = std::atof(v.c_str());
		for (int dec = (int)(v.size() - dot - 1); dec >= 0; dec--) {
			std::string t = string::f("%.*f", dec, x);
			if (t == "-0" || t == "-0.0" || t == "-0.00")
				t = t.substr(1);
			size_t n = 0;
			for (char ch : t)
				n += ch != '.';
			if (n <= 3)
				return t;
		}
		return v;
	}

	static constexpr float OVERRIDDEN_ALPHA = 0.45f; // the plate's white, an Atn that affects nothing
	/** The colour the lane is drawn in: whoever reads the Atn, or the
	cable's own where it is a VCA. */
	NVGcolor color(Atn* r) {
		NVGcolor col = SCHEME_YELLOW;
		if (r) {
			const int shownColor = relaxed(r->shownColorNow);
			// White where an OUT Atn reads something colourless, as a
			// gatherer's colourless row is (`gatheredColor`).
			col = relaxed(r->colorlessNow) ? SCHEME_WHITE : playColor(shownColor);
			if (!relaxed(r->claimedNow) && r->jackLiveNow())
				col = cableOf(r);
			// Dimmed only where something has really stood this Atn down,
			// which is a reader walking the block: a sequential switch
			// chooses one member at a time and the others are not in use. A
			// step whose own output is cabled keeps sounding there whoever
			// the playhead stands on, so it is not dimmed.
			if (relaxed(r->claimedNow) && !r->liveNow() && !r->outputs[Atn::CV_OUTPUT].isConnected())
				col = color::alpha(col, 0.4f);
			// And no colour at all where I affect nothing, the Atn on my
			// right having taken the place of my window before anything reads
			// it: no colour, no part in the rig.
			// An OUT strip with no channel of its Poly left is idle and
			// looks it the same way.
			if (relaxed(r->overriddenNow) || (relaxed(r->stripNow) >= 0 && relaxed(r->stripEmptyNow)))
				col = color::alpha(color::WHITE, OVERRIDDEN_ALPHA);
		}
		return col;
	}

	/** A small value in the plate's face. */
	void drawValue(NVGcontext* vg, float x, float y, int align, float px, NVGcolor ink, const std::string& v) {
		std::shared_ptr<Font> font = plateFont();
		if (!font)
			return;
		drawWord(vg, font, px, 0.2f, align, ink, x, y, v.c_str());
	}

	/** The handle: a small screen standing proud of the glass, with no frame,
	the shadow under it being what lifts it off. Seven-segment digits over
	their ghosts, three cells whatever the value, the minus one of them, all
	of one size on both lanes and lit in the cable's colour, with a glow of it
	around the screen, while the lane's own jack drives something past the
	Atn (`reachesNothing`). The operator is on
	the plate under the glass. Six other handles were tried beside this
	one. */
	/** Where a handle standing at this height is drawn: the whole lane's
	width but a hair, and never past the working face inside the bevel, so
	a gain wide open stands its handle on the face's edge rather than over
	it; the clamp comes after the rounding, which could otherwise carry it
	half a pixel back over, and the foot floored so a handle there stays on
	the pixel grid as one anywhere else does. Rounded on the device's
	pixels (`snapY`), never the panel's units: a unit is two or three
	pixels at the zooms a rack is worked at, and a handle rounded to units
	stood still under the hand while the gain moved (the tooltip at 0.468 while the screen
	said 0.47). */
	Rect handleRect(NVGcontext* vg, float at) {
		const float w = box.size.x, h = box.size.y;
		const float hw = w - mm2px(0.5f), hh = mm2px(3.6f);
		const float top = clamp(snapY(vg, at - hh / 2.f), bevel(), std::floor(h - bevel() - hh));
		return Rect(Vec((w - hw) / 2.f, top), Vec(hw, hh));
	}
	void drawHandle(NVGcontext* vg, float at, NVGcolor col) {
		const bool lit = jackLive() && !reachesNothing();
		const NVGcolor cable = jackCable();
		const Rect c = handleRect(vg, at);
		const float hw = c.size.x, hh = c.size.y;
		const float corner = mm2px(0.25f);
		if (lit) {
			// The glow, at half strength, and held to the glass so it never
			// runs onto the neighbours. The lane is a half of the glass, so
			// the glass is its parent's box.
			const float gx = -box.pos.x, gw = parent ? parent->box.size.x : box.size.x;
			nvgSave(vg);
			nvgIntersectScissor(vg, gx, 0.f, gw, box.size.y);
			nvgBeginPath(vg);
			nvgRect(vg, c.pos.x - 8.f, c.pos.y - 8.f, hw + 16.f, hh + 16.f);
			nvgFillPaint(vg, nvgBoxGradient(vg, c.pos.x - 1.f, c.pos.y - 1.f, hw + 2.f, hh + 2.f, corner + 2.f, 7.f,
			                                color::alpha(cable, 0.4f), color::alpha(cable, 0.f)));
			nvgFill(vg);
			nvgRestore(vg);
		}
		// The shadow.
		nvgBeginPath(vg);
		nvgRect(vg, c.pos.x - 4.f, c.pos.y - 2.f, c.size.x + 8.f, c.size.y + 8.f);
		nvgFillPaint(vg, nvgBoxGradient(vg, c.pos.x, c.pos.y + 1.5f, c.size.x, c.size.y, corner + 1.f, 4.f,
		                                nvgRGBA(0, 0, 0, 170), nvgRGBA(0, 0, 0, 0)));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, c.pos.x, c.pos.y, hw, hh, corner);
		nvgFillPaint(vg, nvgLinearGradient(vg, 0, c.pos.y, 0, c.pos.y + hh, nvgRGB(0x02, 0x02, 0x02), nvgRGB(0x14, 0x14, 0x14)));
		nvgFill(vg);
		// The reading on the handle's own little screen, through the one
		// readout painter: three cells, or four for a count that needs
		// them (-120 semitones, a notes span at its end).
		const std::string digits = cells3(reading());
		nvgSave(vg);
		nvgTranslate(vg, c.pos.x, c.pos.y);
		drawNumber(vg, c.size, digits, lit ? cable : col, std::max(3, (int)(digits.size() - std::count(digits.begin(), digits.end(), '.'))));
		nvgRestore(vg);
	}

	/** Where the cable has taken this lane's fader: the height the handle
	would stand at had the hand put it there. The gain's jack scales the
	fader and the offset's adds to it, so this is the gain in force or the
	offset in force. */
	float modY() { return offsetLane ? yOf(offVolts()) : gainY(gainVolts()); }

	/** The fader the cable has made, drawn under the handle: a slim arrow in
	the jack's cable colour out of the handle's edge, its point on the height
	the fader would stand at had the hand put it there. Nothing is drawn while
	the jack is empty or the cable leaves the fader where it is. A faint
	second handle, a rail down the lane's edge and a fading wake were tried
	beside it, and the arrow was kept. */
	void drawModulation(NVGcontext* vg, float at) {
		if (!getAtn() || !jackLive())
			return;
		const NVGcolor cable = jackCable();
		const Rect c = handleRect(vg, at);
		const float to = handleRect(vg, modY()).pos.y + c.size.y / 2.f;
		const float from = c.pos.y + c.size.y / 2.f;
		if (std::fabs(to - from) < 1.f)
			return;
		const bool up = to < from;
		const float edge = up ? c.pos.y : c.pos.y + c.size.y; // the handle's side facing the target
		const float cx = std::round(box.size.x / 2.f);
		const float head = mm2px(0.9f), wing = mm2px(0.55f);
		if (std::fabs(to - edge) < head || (up ? to > edge : to < edge))
			return; // the point would be inside the handle
		const float dir = up ? -1.f : 1.f;
		nvgBeginPath(vg);
		nvgRect(vg, cx - 0.5f, std::min(edge, to - dir * head), 1.f, std::fabs(to - dir * head - edge));
		nvgFillColor(vg, cable);
		nvgFill(vg);
		paintArrow(vg, cx, to - dir * head / 2.f, up ? ARROW_UP : ARROW_DOWN, cable, head, wing);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		Atn* r = getAtn();
		// The handle, standing where its own fader is: the offset's at the
		// offset, the gain's at the gain, each on the one scale and neither
		// carried by the other. Only the black is both of them.
		const float at = offsetLane ? yOf(knobOffVolts()) : gainY(knobGainVolts());
		drawModulation(args.vg, at);
		drawHandle(args.vg, at, color(r));
	}
};
