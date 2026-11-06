// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/** A Qnt's keys: the keyboard's cells and the keyboard that lays them, the
roll's lanes and heads, and the screen they stand on. A part of Qnt.cpp in a
file of its own: Qnt.cpp includes it once, inside its anonymous namespace,
after QntShaper.hpp, where it stands on the Qnt and the shaper, and it is not
a header to include anywhere else (a painter outside that namespace loses
internal linkage, and under the SDK's unsafe math can draw a grey level
apart). */

/** One key of the keyboard: a rounded cell, filled when the note is in the
scale and dark with an outline when it is not, lit in the reader's colour
while a Seq is sounding that note. A click toggles it and a drag paints;
either way the change is recorded for undo. The rest of the mouse handling
stays Rack's, so it can be mapped and has its menu. */
struct KeyCell : ParamWidget, CellPaint<KeyCell>, GroundPart {
	int key = 0; // of the pattern: pc + 12 * octave
	int pc = 0;
	float whiteH = 0.f; // a white key's height on this keyboard, what the lettering is sized by
	float startValue = 0.f;
	// A drag that began on a key paints the keys it crosses, unless it is
	// shaping: a stroke with shift or command held reshapes rather than paints.
	bool shaping = false;
	bool roll = false; // the roll's flat full-width row, rather than a keyboard's cap

	Qnt* getQnt() { return paramModule<Qnt>(this); }

	/** A roll's row is ground: it is flat, it is opaque, it runs the whole
	width of the panel, and it has to meet the row of the Qnt standing beside
	it without a hairline between them, which is the whole point of a roll.
	A keyboard's key is a control and says so. */
	bool isGround() override { return roll; }
	NVGcolor groundColor() override { return rollColor(); }

	/** What the row is, as one flat colour: the pattern's note lifted to
	white as a keyboard's key is, and otherwise the lane itself, dark. A reader sounding it takes it
	over, as it does on the keyboard. */
	NVGcolor rollColor() {
		Qnt* k = getQnt();
		const bool black = isBlackKey(pc);
		// Unpowered, the roll keeps its lanes and shows no note, chosen or
		// sounding (`MemberWidget::unpowered`).
		if (k && engineSilent())
			return black ? ROLL_BLACK : ROLL_WHITE;
		const Scale now = k ? k->scaleNow.load() : Scale();
		// The notes as set: the scale in force moved back by its transpose.
		const bool on = k ? (key == Qnt::TOP_KEY ? now.extra : now.has(key + k->movedBy(now))) : !black;
		// Sounding, a row fills with the colour of the Seq playing it, cut
		// into strips side by side where several play it (`KeyHeads`), as a
		// keyboard's key is, so every one of several shows.
		int heads[Qnt::MAX_HEADS];
		if (k && k->keyHeadsOf(key, heads) > 0)
			return k->headColor(k->keyOwn(key), heads[0]);
		// A note in the pattern is the keyboard's white, lifted the same
		// amount, so a roll and a keyboard say the same thing the same way:
		// dark is not in the pattern, white is chosen, a colour is in use.
		// And no dimming: a keyboard dims when its module is not the one
		// acting, which on a roll would draw the seam between two modules
		// straight back in: a roll spanning five Qnts is one picture and
		// only the playheads have any business colouring part of it. A note
		// in the pattern is the same white on a black lane as on a white one:
		// the lanes differ only while they are out of it.
		const NVGcolor lane = black ? ROLL_BLACK : ROLL_WHITE;
		if (k && !now.any())
			return nvgLerpRGBA(lane, KEY_OFF, EMPTY_LANE_FADE);
		return on ? nvgLerpRGBA(ROLL_WHITE, color::WHITE, KEY_LIFT) : lane;
	}

	/** The roll's rows are painted in the plain pass, not the lit one: a
	white key is a white key and does not glow in a dark room. And not at
	all when the run has painted them, as one row across every Qnt standing
	together: see Ground. */
	void draw(const DrawArgs& args) override {
		paintGroundPart(this, this, args, 0);
		if (roll)
			return;
		// A keyboard's key is painted in the plain pass and dims with the
		// room, unless it is sounding, which glows in the lit pass: under the
		// colour rule a white key is a chosen note nobody is using, and only
		// a note in use glows, never every chosen key full white in a dark
		// room. Decided once a frame, here, so the two passes never both
		// paint a key or both leave it out.
		Qnt* k = getQnt();
		int col;
		soundingNow = k && k->soundingColor(key, col);
		if (!soundingNow)
			paintKey(args);
	}
	bool soundingNow = false;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1 || roll || !soundingNow)
			return;
		paintKey(args);
	}

	void paintKey(const DrawArgs& args) {
		Qnt* k = getQnt();
		// What is shown is the keys as set: the scale in force moved back by
		// the transpose (`movedBy`).
		const Scale now = k ? k->scaleNow.load() : Scale();
		const int tr = k ? k->movedBy(now) : 0;
		const bool on = k ? now.has(key + tr) : !isBlackKey(pc);
		const bool empty = k && !now.any();
		// No dimming of the Qnts a Seq is not stood on: its playhead over the
		// one it is says it, and a walked block keeps one face.
		const bool black = isBlackKey(pc);

		// A key in the scale is pale, black and white alike, one neutral
		// white (`KEY_LIFT`), and takes the reader's colour while it is
		// sounding: once a key is in the pattern it lights as bright as a
		// white key. Out of the pattern the two still differ. A QNT standing
		// down for the one on its right lifts its keys less (`STOOD_DOWN_LIFT`).
		const float lift = k && relaxed(k->stoodDownUi) ? STOOD_DOWN_LIFT : KEY_LIFT;
		NVGcolor lit = color::alpha(color::WHITE, lift);
		// Sounding, the key lights in the colour of the Seq playing it, and
		// with several on one note it is cut into as many upright strips side
		// by side, one a Seq.
		int heads[Qnt::MAX_HEADS];
		const int nheads = k ? k->keyHeadsOf(key, heads) : 0;
		const bool own = k && k->keyOwn(key);
		// A key out of the pattern is one grey, black and white alike, the
		// white key's. The playhead lane is the same grey.
		const NVGcolor dark = KEY_OFF;

		// Flat, whatever the octaves: one rectangle a key, its box the exact
		// share of its column, as a roll's row is, with its edges brought
		// onto the device's pixels by `snapY` and one device pixel of the
		// screen's black left at its top for the seam, so the keyboard fills
		// its screen at every zoom, two keys differ by a pixel at most and
		// every seam is one crisp pixel.
		const NVGcolor base = on ? lit : dark;
		{
			float xf[6];
			nvgCurrentTransform(args.vg, xf);
			const float dev = xf[3] > 0.f ? 1.f / xf[3] : 1.f;
			const float top = snapY(args.vg, 0.f) + dev, foot = snapY(args.vg, box.size.y);
			const float x0 = black ? 0.f : 1.f, w = box.size.x - x0, hk = std::max(foot - top, dev);
			if (nheads > 0)
				paintHeadStrips(args.vg, x0, top, w, hk, nheads, [&](int i) { return k->headColor(own, heads[i]); });
			else {
				nvgBeginPath(args.vg);
				nvgRect(args.vg, x0, top, w, hk);
				nvgFillColor(args.vg, on ? base : color::alpha(base, empty ? EMPTY_KEY_ALPHA : 0.8f));
				nvgFill(args.vg);
			}
		}

		// The note's letter, dark on a lit white key and pale on a dark one;
		// a keyboard grown too tall for letters to fit its white keys goes
		// without on both sides. A key a microtonal keyboard has detuned says
		// its cents in the letter's place, the note being where it stands,
		// and where no letter fits a small triangle says which way.
		const int det = k ? k->detuneOf(key) : 0;
		NVGcolor text = on ? SCREEN_FACE : color::alpha(color::WHITE, empty ? EMPTY_TEXT_ALPHA : 0.3f);
		const float fit = whiteH * 0.62f;
		if (fit < 5.f) {
			if (det != 0) {
				const float h = std::min(box.size.y * 0.7f, 4.f), w = h * 1.2f;
				paintArrow(args.vg, box.size.x / 2.f, box.size.y / 2.f, det > 0 ? ARROW_UP : ARROW_DOWN,
				           on || nheads > 0 ? SCREEN_FACE : color::alpha(color::WHITE, 0.6f), h, w / 2.f);
			}
			return;
		}
		std::shared_ptr<Font> font = plateFont();
		if (!font)
			return;
		const float size = std::min(black ? 6.5f : 8.f, fit);
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, size);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, text);
		// Named by the transpose: at +1 the white keys read D flat, E flat,
		// F and on, the note each now sounds.
		if (det != 0)
			nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, string::f("%+d", det).c_str(), nullptr);
		else
			nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, pcName(eucMod(pc + tr, 12)), nullptr);
		// The transpose itself is the head's to say (`QntHead`).
	}

	/** The switch a click writes: a keyboard's key its own; a roll's row
	the switch of the note standing on it, where the roll stands on the
	piano. */
	int boundParam() {
		Qnt* k = getQnt();
		if (roll && k) {
			const int n = std::max(1, (int)relaxed(k->octavesNow));
			const int shift = relaxed(k->rollShiftNow);
			const int pitch = key == Qnt::TOP_KEY ? Qnt::rollBase(n, shift) + 12 * n : Qnt::rollSemis(key, n, shift);
			if (pitch >= -Qnt::ROLL_BELOW && pitch <= Qnt::ROLL_BELOW)
				return Qnt::rollParam(pitch);
			return paramId;
		}
		return paramId;
	}
	float boundValue() {
		Qnt* k = getQnt();
		return k ? k->params[boundParam()].getValue() : 0.f;
	}
	void set(float v) {
		Qnt* k = getQnt();
		if (k)
			k->params[boundParam()].setValue(v);
	}
	QntShaper shaper;
	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			shaper.tuneKey = roll ? -1 : key;
			shaper.by = this;
			shaping = shaper.begin(getQnt());
			if (shaping) {
				ParamWidget::onButton(e);
				return;
			}
			startValue = boundValue();
			beginPaint(startValue);
			if (Qnt* k = getQnt())
				painted.noteAt(k, boundParam(), startValue);
			set(paint);
			// A hand on a key is a hand on the Qnt, for a Seq's PREVIEW as
			// a hand on a CV's fader is.
			if (Qnt* k = getQnt())
				k->touch();
		}
		ParamWidget::onButton(e);
	}
	void onDragMove(const DragMoveEvent& e) override {
		if (shaping && e.button == GLFW_MOUSE_BUTTON_LEFT)
			shaper.move(getQnt());
		else if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			// Across the Qnts of the run.
			paintCrossed(this, [&](KeyCell* r) {
				if (!r->shaping)
					if (Qnt* k = r->getQnt()) {
						painted.note(k, r->boundParam());
						r->set(paint);
					}
			});
		}
		ParamWidget::onDragMove(e);
	}
	void onDragEnd(const DragEndEvent& e) override {
		if (shaping) {
			if (e.button == GLFW_MOUSE_BUTTON_LEFT)
				shaper.end(getQnt());
			shaping = false;
			ParamWidget::onDragEnd(e);
			return;
		}
		if (e.button == GLFW_MOUSE_BUTTON_LEFT)
			endPaint("change keys");
		ParamWidget::onDragEnd(e);
	}
	void onDoubleClick(const DoubleClickEvent& e) override {} // a fast second click is a toggle, not a reset
	/** No tooltip over a roll. A key of a keyboard is a control and names
	itself when the mouse rests on it; a row of a roll is already labelled by
	where it is, and a tooltip following the mouse across a chord is in the
	way of the only thing the roll is for. */
	void onEnter(const EnterEvent& e) override {
		if (!roll)
			ParamWidget::onEnter(e);
	}
};

/** The keyboard as a whole: lays its keys out for the octaves in force,
again whenever that changes. */
struct Keyboard : Widget {
	int laidOut = -1, laidShift = 0;
	bool laidRoll = false, laidLane = false;
	Widget* lane = nullptr; // the playhead lane, `KeyLane`, laid between the columns

	/** Laid out for so many octaves of the keyboard or of the roll: the
	roll in its mode, however few of them, and nothing at all for the
	divider, which has no keys. Each key
	is bound to the switch it shows: one of the keyboard's twelve in every
	octave, or the roll's own. */
	void layout(int octaves, bool roll, int shift, bool withLane = false) {
		laidLane = withLane;
		laidOut = octaves;
		laidRoll = roll;
		laidShift = shift;
		// Two pictures with opposite rules, and the rules are the point.
		//
		// A keyboard stands INSIDE the glass, held in by the bevel on all
		// four sides. Top and foot, because a screen carries a bevel (the
		// shading that makes it look like a piece of glass with something
		// behind it) and keys laid over it sat on the lit line and the
		// shadow. Sides, because a run of Qnts is bonded into one strip of
		// glass, and two keyboards side by side put the white column of one
		// hard against the black column of the next, which the eye read as
		// one keyboard with its halves in the wrong order; a margin of bare
		// glass down each edge makes every keyboard a slab of its own. The
		// seam between the black and white columns stays at BLACK_W whatever
		// the margin does, so a rig of Qnts keeps its columns on one line.
		//
		// A roll is paper laid over the glass, its rows are the whole
		// picture, and a run of rolls is one sheet to draw a part on: the
		// rows bond across the join. It is held in off the glass's top and
		// foot as a CV's fader is, so it stands inside the screen's dark
		// margin like everything else on the glass; its sides run to the
		// panel's edges, so a run of rolls is one sheet.
		const float in = GLASS_BEVEL;
		for (Widget* w : children) {
			KeyCell* r = dynamic_cast<KeyCell*>(w);
			if (!r)
				continue;
			const bool top = r->key == Qnt::TOP_KEY;
			r->visible = top ? roll && octaves > 0 : r->key < 12 * octaves;
			if (!r->visible)
				continue;
			if (top)
				r->pc = eucMod(Qnt::rollBase(octaves, shift), 12);
			r->paramId = Qnt::keyParamIn(r->key, roll, octaves, shift);
			float y = 0.f, hh = 0.f;
			const bool black = isBlackKey(r->pc);
			r->roll = roll;
			// A roll's row IS its painted area, edge to edge and touching
			// its neighbours: the run's bond fills whatever a row says it
			// covers, and a row that claims a pixel it does not paint puts a
			// notch in the grid at every join.
			if (roll) {
				// The row the key stands on, counted from the roll's foot.
				const int row = top ? 12 * octaves : Qnt::rollSemis(r->key, octaves, shift) - Qnt::rollBase(octaves, shift);
				rollBox(row, octaves, box.size.y - 2.f * in, y, hh);
				r->box.pos = Vec(0.f, in + y);
				r->box.size = Vec(box.size.x, hh);
			}
			else {
				// Walked by a Seq, the keys stand lower: their top on the top
				// of what stands under a Seq's second screen, so the glass
				// over them, level with the Seq's rate screen (the head
				// standing level with its step), is the playhead's.
				// The columns keep their widths. A lane down the middle of
				// the keys was tried and read as one gap too many.
				float keysTop, keysFoot;
				keysSpan(box.size.y, keysTop, keysFoot);
				if (withLane)
					keysTop = keysUnderLane();
				const float kh = keysFoot - keysTop;
				keyBox(r->key, octaves, kh, y, hh);
				const float seam = std::round(mm2px(BLACK_W));
				r->box.pos = Vec(black ? in : seam, keysTop + y);
				r->box.size = Vec(black ? seam - in : box.size.x - in - seam, hh);
				r->whiteH = kh / (7.f * octaves);
			}
		}
		if (lane) {
			lane->visible = withLane;
			lane->box.pos = Vec(in, in);
			lane->box.size = Vec(box.size.x - 2.f * in, std::max(laneFoot() - in, 0.f));
		}
	}
	/** Where the playhead's strip ends and the keys begin, walked: the foot
	of a Seq's second screen inside its bevel, on a whole pixel, this screen
	standing level with that one. */
	float laneFoot() { return std::round(mm2px(HEAD_SCREEN_H)) - LANE_GAP; }
	/** And where the keys' boxes begin under it: their first lit row on
	the top of the light line what stands under a Seq's second screen opens
	with. */
	float keysUnderLane() { return std::round(mm2px(HEAD_SCREEN_H)) + KEYS_UNDER; }
	/** The octaves in force, laid out if they moved. Asked at every step
	and again as a frame is drawn, since a frame can be drawn without a
	step before it. */
	void follow() {
		ModuleWidget* mw = getAncestorOfType<ModuleWidget>();
		Qnt* k = mw ? dynamic_cast<Qnt*>(mw->module) : nullptr;
		const int octaves = k ? clamp(relaxed(k->octavesNow), 0, MAX_OCTAVES) : 1;
		const bool roll = k && relaxed(k->rollNow);
		const int shift = k ? relaxed(k->rollShiftNow) : 0;
		const bool withLane = k && k->showsLane();
		if (octaves != laidOut || roll != laidRoll || shift != laidShift || withLane != laidLane)
			layout(octaves, roll, shift, withLane);
	}
	void step() override {
		follow();
		Widget::step();
	}
	void draw(const DrawArgs& args) override {
		follow();
		Widget::draw(args);
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		follow();
		Widget::drawLayer(args, layer);
	}
};

/** The roll's pale lane, under its rows and inside the glass's margin: the
grey a seam between two rows falls on, see `rollBox`. Ground while the Qnt is
a roll, so the run lays it across every Qnt of a sheet as one piece; nothing
at all while it is a keyboard. */
struct RollLane : Widget, GroundPart {
	Qnt* qnt = nullptr;
	bool rolling() { return qnt && qnt->showsRoll(); }
	bool isGround() override { return rolling(); }
	NVGcolor groundColor() override { return ROLL_WHITE; }
	void draw(const DrawArgs& args) override { paintGroundPart(this, this, args, 0); }
};

/** The playheads on a roll's rows: a row being sounded is filled with the
colour of each reader sounding it, cut into upright strips side by side, one a
reader, as a keyboard's key is. Live and lit, over the rows, which are ground
and paint the first colour flat, so the row stays filled with the lit layer
off. */
struct KeyHeads : Widget {
	Qnt* qnt = nullptr;
	Keyboard* kb = nullptr;
	void drawLayer(const DrawArgs& args, int layer) override {
		Widget::drawLayer(args, layer);
		if (layer != 1 || !qnt || !kb)
			return;
		for (Widget* w : kb->children) {
			KeyCell* r = dynamic_cast<KeyCell*>(w);
			if (!r || !r->visible || !r->roll)
				continue;
			int cols[Qnt::MAX_HEADS];
			const int n = qnt->keyHeadsOf(r->key, cols);
			if (n <= 0)
				continue;
			const bool own = qnt->keyOwn(r->key);
			nvgSave(args.vg);
			nvgTranslate(args.vg, kb->box.pos.x + r->box.pos.x, kb->box.pos.y + r->box.pos.y);
			nvgIntersectScissor(args.vg, 0.f, 0.f, r->box.size.x, r->box.size.y);
			paintHeadStrips(args.vg, 0.f, 0.f, r->box.size.x, r->box.size.y, n, [&](int i) { return qnt->headColor(own, cols[i]); });
			nvgRestore(args.vg);
		}
	}
};

/** The dark strip over the keys of a keyboard a Seq walks, level with the
Seq's step screen, where the Seq's playhead lands when it stands on this Qnt,
one soft line a Seq as a CV shows them, and only there. A press on it is a
hand on the Qnt, so a Seq in PREVIEW sounds this step while it is held, as a
press on a CV's fader does; it toggles no key.
*/
struct KeyLane : OpaqueWidget {
	Qnt* qnt = nullptr;
	bool pressed = false;
	KeyLane* holding = nullptr; // the lane a stroke begun here now holds, carried along the group
	void drawLayer(const DrawArgs& args, int layer) override {
		OpaqueWidget::drawLayer(args, layer);
		if (layer != 1 || !qnt)
			return;
		int on[8];
		bool line[8], halo[8];
		const int n = qnt->playheadLanes(on, line, halo);
		if (n <= 0)
			return;
		nvgSave(args.vg);
		nvgIntersectScissor(args.vg, 0.f, 0.f, box.size.x, box.size.y);
		paintPlayheads(args.vg, box.size, on, n, line, halo);
		nvgRestore(args.vg);
	}
	void onButton(const ButtonEvent& e) override {
		OpaqueWidget::onButton(e);
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && qnt) {
			pressed = true;
			holding = this;
			qnt->touch();
			e.consume(this);
		}
	}
	/** Dragged on, the stroke carries the hold to the lane of whichever Qnt
	of my group the pointer is over, so a held playhead can be walked along
	the block. The group is the Qnts standing touching me: a lane past
	anything that is not a Qnt is another block's. Between lanes, over a key,
	the last lane keeps it. */
	void onDragMove(const DragMoveEvent& e) override {
		OpaqueWidget::onDragMove(e);
		if (!pressed || e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		const math::Vec now = APP->scene->rack->getMousePos();
		std::vector<KeyLane*> under;
		cellsCrossed<KeyLane>(this, now, now, under);
		for (KeyLane* l : under)
			if (l->qnt && sameGroup(l)) {
				holding = l;
				l->qnt->touch();
			}
	}
	bool sameGroup(KeyLane* other) {
		ModuleWidget* a = getAncestorOfType<ModuleWidget>();
		ModuleWidget* b = other->getAncestorOfType<ModuleWidget>();
		const std::vector<ModuleWidget*>* run = a ? runOf(a) : nullptr;
		if (!run || !b)
			return false;
		int ia = -1, ib = -1;
		for (int i = 0; i < (int)run->size(); i++) {
			if ((*run)[i] == a) ia = i;
			if ((*run)[i] == b) ib = i;
		}
		if (ia < 0 || ib < 0)
			return false;
		for (int i = std::min(ia, ib); i <= std::max(ia, ib); i++)
			if (!dynamic_cast<Qnt*>((*run)[i]->module))
				return false;
		return true;
	}
	void onDragEnd(const DragEndEvent& e) override {
		pressed = false;
		holding = nullptr;
		OpaqueWidget::onDragEnd(e);
	}
	void step() override {
		// Held, it is held: the touch lasts a tenth of a second, so renew it
		// on whichever lane the stroke holds now.
		if (pressed && holding && holding->qnt)
			holding->qnt->touch();
		OpaqueWidget::step();
	}
};

/** The playheads over the roll and the volt divider: the same soft lines a
CV shows, in the same colours, from the same lanes, down the whole screen. A
Qnt walked by a Seq is being played, and a roll or a divider being played
should say so the way the fader beside it does, so this is `paintPlayheads`
and nothing of its own. Not over a keyboard: a keyboard a Seq walks has a lane of its own
for them (`KeyLane`). */
struct RollHeads : Widget {
	Qnt* qnt = nullptr;

	void drawLayer(const DrawArgs& args, int layer) override {
		Widget::drawLayer(args, layer);
		if (layer != 1 || !qnt)
			return;
		if (!qnt->showsRoll() && !qnt->showsDivider())
			return;
		int on[8];
		bool line[8], halo[8];
		const int n = qnt->playheadLanes(on, line, halo);
		paintPlayheads(args.vg, box.size, on, n, line, halo);
	}
};

/** The keys' screen, under the head: ground in every mode, the keyboard,
the roll and the divider's face all standing on it. */
struct KeysScreen : FlatScreen {};
