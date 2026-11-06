// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/** The screens a hand works, Screen, ScreenParam, ScreenButton and
RateScreen, with the breathing light a button wears. A part of Hand.hpp in a
file of its own: Hand.hpp includes it once, inside its namespace rig, after
the stroke and the drag they are built on, and it is not a header to include
anywhere else. */

/** A screen that is not a control: a readout.

It paints none of itself while the run has painted it. Rack's own screen
lays a lighter bevel down every edge of the rectangle it is given, and the
edge a module shares with the one beside it is one of those, so a screen
painting its own bevel put a light column at the join however well the black
underneath it matched. The run paints one screen across the whole merged
strip and the bevel is drawn once, at its two real ends. */
struct Screen : LedDisplay, GroundPart {
	NVGcolor groundColor() override { return GLASS_RIM; }
	/** The stock screen's own corner, so the ground laid under a run of
	them ends where they end and shows nowhere past them. */
	float groundRadius() override { return GLASS_RADIUS; }
	bool groundLip() override { return true; }
	/** What stands on the glass, clipped to the glass. The stock screen puts
	this scissor around its children and both halves of ours have to keep it:
	a lit thing that overruns (a cluster of playheads and their glow) would
	otherwise spill across the join onto the neighbour's screen, and only in a
	run, which is the worst way to find a bug. Narrowing, not replacing, so
	whatever clip Rack handed in still holds. */
	void mineOnly(const DrawArgs& args, int layer) {
		nvgSave(args.vg);
		nvgIntersectScissor(args.vg, 0.f, 0.f, box.size.x, box.size.y);
		if (layer < 0)
			Widget::draw(args);
		else
			Widget::drawLayer(args, layer);
		nvgRestore(args.vg);
	}
	/** Its face where the run has not painted it (a strip of one, placed
	and clipped exactly as the run places a strip of many) and then only
	what stands on it. Never LedDisplay's own draw: that is the stock face
	at its own size with its own line above it, which is the one thing a
	screen in this plugin must not draw for itself. */
	void draw(const DrawArgs& args) override {
		paintGroundPart(this, this, args, 0);
		mineOnly(args, -1);
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		paintGroundPart(this, this, args, layer);
		mineOnly(args, layer);
	}
};

/** A dragged param drawn as a screen: the readouts that are their own
control. `drawLayer` is the subclass's, lit in layer 1. */
struct ScreenParam : VerticalDragParam, GroundPart {
	NVGcolor groundColor() override {
		return GLASS_RIM;
	}
	float groundRadius() override {
		return GLASS_RADIUS;
	}
	void draw(const DrawArgs& args) override {
		paintGroundPart(this, this, args, 0);
		ParamWidget::draw(args);
	}
	/** What the screen shows, lit: the subclass's. */
	virtual void drawLit(const DrawArgs& args) {}
	void drawLayer(const DrawArgs& args, int layer) override {
		paintGroundPart(this, this, args, layer);
		if (layer == 1)
			drawLit(args);
		ParamWidget::drawLayer(args, layer);
	}
};

/** One breath of a light that breathes, in seconds. */
const double BREATH_S = 2.5;
/** A breathing button, from a fifth up to full and back once a breath, so
it is never mistaken for one lit steadily. A Seq's PREVIEW and a RIG's
REPLACE and SAVE breathe by it, together. */
inline float breathing() { return breath(BREATH_S, 0.2f); }
/** How long a button's press flashes, in seconds. */
const double BUTTON_FLASH_S = 0.15;

/** A switch, or a button, as a small screen with a word on it: lit in the
given colour while on, or while held, a ghost otherwise. `vertical` writes
the word down the screen, a letter a line. A momentary one is a button; the
param is 1 while the mouse is down. */
struct ScreenButton : Hovered<ParamWidget> {
	/** A fast second press is a second press, never Rack's reset: a button's
	press is the gesture itself, as a stock switch's is, so it is not a value
	a double click puts back (`VerticalDragParam::onDoubleClick`). */
	void onDoubleClick(const DoubleClickEvent& e) override { e.consume(this); }
	std::string text = "RUN";
	std::string offText; // shown when off, if different
	bool vertical = false;
	bool momentary = false;
	float topPad = -1.f; // vertical: the word hangs from this far down the screen, in px; centred when negative
	float stepPx = -1.f; // vertical: from one letter to the next, in px; the font's own when negative
	float px = 7.5f;
	std::function<NVGcolor()> colorOf = []() { return SCHEME_YELLOW; };
	std::function<std::string()> textOf; // overrides `text` when set
	std::function<float()> glowOf;       // 0..1: a flash the module asks for, a trigger arriving say
	std::function<void()> onPress;       // momentary: told of the press itself, however short
	bool strip = false;                  // standing on a head's mode strip: lit edge to edge, no bevel

	/** What is in force, where that is not the switch: a cable holding
	RUN, say. Unset, the switch is what is shown. */
	std::function<bool()> inForce;
	virtual bool on() {
		if (inForce && module)
			return inForce();
		engine::ParamQuantity* pq = getParamQuantity();
		return pq && pq->getValue() > 0.5f;
	}
	/** How lit the button is beyond its word: fully while it is on, and
	whatever the module's flash says, which fades after the trigger that set
	it.

	A switch that is on is lit at once and completely: the light is the
	state, not an event, so there is nothing to fade in and the frame after
	the click already shows it. Only a flash, which is an event, fades. */
	float flash() {
		float g = glowOf ? clamp(glowOf(), 0.f, 1.f) : 0.f;
		// The press itself flashes from the widget, so a click too short for
		// a frame to catch the parameter up still shows; a traced frame draws
		// it spent (gap-rig-bond's INSERT).
		return std::max(g, pressFlash(pressedAt, BUTTON_FLASH_S));
	}
	virtual float glow() {
		return on() ? 1.f : flash();
	}
	/** How much of the light, as a share of its length from the start, is
	dark: none, save where a module's button is a bar, the Seq's in ONCE. */
	virtual float darkTo() { return 0.f; }
	double pressedAt = -1.0; // frameNow() of the last press, momentary only
	/** Flat. A small button is the same dark rectangle a panel already paints
	behind its output jacks (the same colour, the same corner, and, standing
	at BLOCK_INSET, the same width) with a word on it and nothing else, so no
	panel has three or four different-looking buttons on it, none of them the
	shape of anything else there. */
	void draw(const DrawArgs& args) override {
		// Unless it is standing on ground of its own: a Trg's button is the
		// whole of its screen, exactly, and the screen underneath paints it.
		if (!overGround(this))
			drawBlockFace(args.vg, box.size);
		ParamWidget::draw(args);
	}
	/** The word, and where it sits: `y` is the middle of the word, or of its
	first letter when the word runs down the screen. */
	virtual void drawWord(const DrawArgs& args, const std::string& word, NVGcolor col) {
		std::shared_ptr<Font> font = plateFont();
		if (!font)
			return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, px);
		nvgTextLetterSpacing(args.vg, 0.4f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, col);
		if (!vertical) {
			nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, word.c_str(), nullptr);
			return;
		}
		const int n = (int)word.size();
		const float step = stepPx > 0.f ? stepPx : px * 1.05f;
		const float y0 = topPad >= 0.f ? topPad + px / 2.f : box.size.y / 2.f - step * (n - 1) / 2.f;
		for (int i = 0; i < n; i++) {
			const std::string ch(1, word[i]);
			nvgText(args.vg, box.size.x / 2.f, y0 + i * step, ch.c_str(), nullptr);
		}
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		const bool lit = on();
		const float g = glow();
		const std::string word = textOf ? textOf() : (!lit && !offText.empty() ? offText : text);
		// The press: LEDs under a milky plastic cap. A row of soft pools of
		// the button's colour along its length, each a radial glow that
		// fades before the next, over a faint even wash, and the cap's sheen
		// across the top; the word goes white in the light and the whole
		// sinks a touch. All of it fades as the flash does. The face under
		// it stays the flat rectangle it is; only the light moves.
		if (g > 0.f) {
			const NVGcolor c = colorOf();
			const bool tall = box.size.y > box.size.x;
			const float len = tall ? box.size.y : box.size.x;
			const float across = tall ? box.size.x : box.size.y;
			const int leds = std::max(1, (int)std::round(len / mm2px(11.f)));
			const float pitch = len / leds;
			// The light stays inside the face: the same rounded rectangle the
			// face itself is, so a lit button is a lit button and not a
			// square of light with the button's corners cut out of it.
			//
			// A screen's own button (a Trg's, which is the whole screen) is
			// not that. The LEDs are under the glass, not painted on it, so
			// the light begins where the glass's working face begins, inside
			// the bevel, and it is square: a diffuser under a piece of glass
			// is a rectangle, and a rounded one drawn over the bevel reads as
			// a vector laid on top of the screen rather than a lamp behind
			// it. Held off the glass's top and foot by the bevel, and across
			// its whole width, as a roll's rows are: a margin down each side
			// reads as a lamp narrower than its button. Held off only the
			// edges it shares with its glass, where the bevel is: a button
			// standing inside the working face, as the RIG's SAVE does over
			// its glass's foot, is lit its whole height, as tall as a row of
			// the hand beside it.
			Rect cover, where;
			const bool inGlass = overGround(this, &cover, &where);
			const float round = inGlass || strip ? 0.f : mm2px(BLOCK_ROUND);
			const bool bevelled = inGlass && !strip;
			// An edge is shared where overGround found it, within its half
			// pixel of where the glass's edge stands.
			const float top = bevelled && where.pos.y - cover.pos.y <= 0.5f ? GLASS_BEVEL : 0.f;
			const float foot = bevelled && cover.getBottom() - where.getBottom() <= 0.5f ? GLASS_BEVEL : 0.f;
			const Rect face(Vec(0.f, top), Vec(box.size.x, box.size.y - top - foot));
			nvgSave(args.vg);
			const float dark = clamp(darkTo(), 0.f, 1.f);
			if (dark > 0.f)
				nvgIntersectScissor(args.vg, tall ? 0.f : len * dark, tall ? len * dark : 0.f, box.size.x, box.size.y);
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, face.pos.x, face.pos.y, face.size.x, face.size.y, round);
			nvgFillColor(args.vg, color::alpha(c, 0.10f * g));
			nvgFill(args.vg);
			for (int i = 0; i < leds; i++) {
				const float at = (i + 0.5f) * pitch;
				const float cx = tall ? box.size.x / 2.f : at, cy = tall ? at : box.size.y / 2.f;
				const float reach = std::max(pitch * 0.9f, across * 0.8f);
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, face.pos.x, face.pos.y, face.size.x, face.size.y, round);
				nvgFillPaint(args.vg, nvgRadialGradient(args.vg, cx, cy, across * 0.15f, reach,
				                                        color::alpha(c, 0.55f * g), color::alpha(c, 0.f)));
				nvgFill(args.vg);
			}
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, face.pos.x, face.pos.y, face.size.x, face.size.y, round);
			nvgFillPaint(args.vg, nvgLinearGradient(args.vg, face.pos.x, face.pos.y,
			                                        tall ? face.pos.x + face.size.x : face.pos.x,
			                                        tall ? face.pos.y : face.pos.y + face.size.y,
			                                        color::alpha(color::WHITE, 0.10f * g),
			                                        color::alpha(color::WHITE, 0.f)));
			nvgFill(args.vg);
			nvgRestore(args.vg);
		}
		const float held = momentary && lit ? 1.f : 0.f;
		nvgSave(args.vg);
		nvgTranslate(args.vg, box.size.x / 2.f, box.size.y / 2.f);
		nvgScale(args.vg, 1.f - 0.04f * held, 1.f - 0.02f * held);
		nvgTranslate(args.vg, -box.size.x / 2.f, -box.size.y / 2.f);
		// The word: dim when off, brighter under the pointer so the button
		// reads as one, full when on, and white for as long as a press or a
		// trigger flash lasts.
		NVGcolor col = color::alpha(colorOf(), markInk(lit, hovered));
		// White is the event, not the state: a press or a trigger whitens
		// the word and fades, while a switch left on keeps its own colour
		// over its own light.
		const float w = flash();
		if (w > 0.f) {
			const NVGcolor white = color::WHITE;
			col = nvgRGBAf(col.r + (white.r - col.r) * w, col.g + (white.g - col.g) * w,
			               col.b + (white.b - col.b) * w, col.a + (1.f - col.a) * w);
		}
		drawWord(args, word, col);
		nvgRestore(args.vg);
	}
	void onButton(const ButtonEvent& e) override {
		engine::ParamQuantity* pq = getParamQuantity();
		if (pq && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (e.action == GLFW_PRESS) {
				if (momentary) {
					pq->setImmediateValue(1.f);
					pressedAt = frameNow();
					if (onPress)
						onPress();
				}
				else {
					const float old = pq->getValue();
					pq->setImmediateValue(old > 0.5f ? 0.f : 1.f);
					recordParamChange(this, old);
				}
				e.consume(this);
				return;
			}
		}
		ParamWidget::onButton(e);
	}
	void onDragEnd(const DragEndEvent& e) override {
		engine::ParamQuantity* pq = getParamQuantity();
		if (momentary && pq && e.button == GLFW_MOUSE_BUTTON_LEFT)
			pq->setImmediateValue(0.f);
		ParamWidget::onDragEnd(e);
	}
};

/** A mark on a screen that is a button: a click steps its param to the
next of its values (`next`), one undo; a click is the gesture, so a double
click is two of them and a drag nothing. It draws nothing, the screen
painting the mark, and tells the screen through `hot` while the pointer is
over it, so the mark brightens to say it can be pressed; where it turns
nothing now (`can`) it takes the press and neither moves nor brightens. The
Seq's direction cell and the Qnt's degree ring. */
struct MarkCell : ParamWidget {
	bool* hot = nullptr;
	virtual float next(float v) = 0;
	virtual bool can() { return true; }
	void onDoubleClick(const DoubleClickEvent& e) override { e.consume(this); }
	void onButton(const ButtonEvent& e) override {
		engine::ParamQuantity* pq = getParamQuantity();
		if (pq && e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			e.consume(this);
			if (!can())
				return;
			const float old = pq->getValue();
			pq->setImmediateValue(next(old));
			recordParamChange(this, old);
			return;
		}
		ParamWidget::onButton(e);
	}
	void onDragStart(const DragStartEvent& e) override { e.consume(this); }
	void onEnter(const EnterEvent& e) override {
		if (hot)
			*hot = can();
		ParamWidget::onEnter(e);
	}
	void onLeave(const LeaveEvent& e) override {
		if (hot)
			*hot = false;
		ParamWidget::onLeave(e);
	}
	void draw(const DrawArgs& args) override {}
};

/** A rate's screen, the one a Clock's division and a Seq's rate are each
drawn on: the step rate on three cells, the sign on the first, the whole
number on the other two (`drawCount`), and a pie past the last cell, lit by
the fraction between whole rates (`pieAt`, `pieMark`). Three cells hold no
decimals and drawNumber is the one size, so a fraction is the pie's, and the
hover gives every decimal of it. A jack sweeping the rate is a bar along the
foot in its cable's colour; the cells name the rate the hand set, as a Seq's
step screen names the step before PHASE turns it, so the bar alone says what
the jack does. Each module says only in what ink, which jack sweeps it and
what that jack reads (`shown`); the strokes are the one ladder
(`strokeMove`), and the pie is a part of its own (`pieZone`): a click on it
turns the mode over, a stroke moves the fraction and keeps the whole part
(`pieStep`). */
struct RateScreen : ScreenParam {
	RateScreen() { pxPerUnit = PX_PER_RUNG; }
	/** A pie's step, a hundredth of the way between whole rates, in pixels:
	a stroke of a hundred and fifty crosses all of it. */
	static constexpr float PX_PER_PIE = 1.5f;
	bool onPie = false;       // the press landed on the pie
	bool freeAtPress = false; // the mode as the stroke found it, for its undo
	/** Where the pie takes a press, in the screen's own pixels, set where
	it is drawn (`drawLit`), since the cells' width is the font's; empty
	until the first frame, when nothing has been seen to press. */
	Rect pieZone;
	RateMode* mode() { return module ? rateModeOf(module) : nullptr; }
	bool modeFree() {
		RateMode* m = mode();
		return m && m->rateFreeSet();
	}
	/** The mode set, and the module woken, since a mode can change with
	the value standing. */
	void setMode(bool f) {
		if (RateMode* m = mode())
			m->setRateFree(f);
		if (Member* rm = dynamic_cast<Member*>(module))
			rm->wake();
	}
	/** Where the RATE jack's bar stands, `cv` its volts over ten, by the law
	the rate's mode gives the jack (`rateBar`). */
	float jackBar(float cv) {
		engine::ParamQuantity* pq = getParamQuantity();
		const int h = pq ? rateOfValue(pq->getValue()) : RATE_ONE;
		return rateBar(wholeStepOf(h), cv, rateFreeOf(module, h));
	}
	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS)
			onPie = pieZone.contains(e.pos);
		ScreenParam::onButton(e);
	}
	void onDragStart(const DragStartEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			freeAtPress = modeFree();
			pxPerUnit = onPie ? PX_PER_PIE : PX_PER_RUNG;
		}
		ScreenParam::onDragStart(e);
	}
	/** A click on the pie turns the mode over, the second press of a fast
	pair of them included, so it is never Rack's reset. */
	void onDoubleClick(const DoubleClickEvent& e) override {
		if (onPie) {
			e.consume(this);
			return;
		}
		ScreenParam::onDoubleClick(e);
	}
	/** The default rate, and whole, as a fresh module's is. */
	void resetToDefault(engine::ParamQuantity* pq) override {
		ScreenParam::resetToDefault(pq);
		if (RateMode* m = mode())
			m->setRateFree(false);
	}
	/** At the stroke's end: a click on the pie turns the mode over, whole
	to free keeping the value (x2 to x2.) and free to whole snapping it to
	the nearest whole rate at once (x2.05 to x2); any stroke landing off a
	whole rate leaves it free, the pie's fraction, a command-drag's
	hundredths or a shift-drag's halving of x3, so the mode says what the
	screen's fraction already does. Idle, the screen takes neither. */
	void onDragEnd(const DragEndEvent& e) override {
		engine::ParamQuantity* pq = getParamQuantity();
		RateMode* m = mode();
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && pq && m && !spent && !(module && idle())) {
			const int h = rateOfValue(pq->getValue());
			if (onPie && !moved && pq->getValue() == startValue) {
				if (rateFreeOf(module, h)) {
					pq->setValue(valueOfRate(nearestWholeRate(h)));
					setMode(false);
				}
				else
					setMode(true);
			}
			else if (pq->getValue() != startValue && rateHasFraction(h))
				setMode(true);
		}
		ScreenParam::onDragEnd(e);
	}
	/** The value and the mode, one step of undo. */
	void recordStroke() override {
		engine::ParamQuantity* pq = getParamQuantity();
		RateMode* m = mode();
		if (!pq || !m || m->rateFreeSet() == freeAtPress) {
			ScreenParam::recordStroke();
			return;
		}
		GestureChanges g;
		g.noteAt(this, startValue);
		pushWithMode(g.take("change " + pq->getLabel()), freeAtPress);
	}
	/** One step of undo, `all` and the mode's change from `was` to what it
	is now, pushed onto the history. */
	void pushWithMode(history::ComplexAction* all, bool was) {
		RateModeChange* mc = new RateModeChange;
		mc->moduleId = module->id;
		mc->from = was;
		mc->to = mode()->rateFreeSet();
		all->push(mc);
		APP->history->push(all);
	}
	/** The ink, and the jack's volts over ten, NAN with no jack: asked only
	with a module behind the screen. */
	virtual void shown(NVGcolor& col, float& bar) = 0;
	/** The input whose volts the bar draws, minus one for none, and the
	colour of the cable in it, asked in the step: only the UI knows a
	cable's colour, and nothing of it reaches the engine. */
	int barJack = -1;
	uint32_t barRGB = 0;
	void step() override {
		ModuleWidget* mw = barJack >= 0 ? getAncestorOfType<ModuleWidget>() : nullptr;
		barRGB = mw ? cableColorOn(mw->getInput(barJack)) : 0;
		recordTyped();
		ScreenParam::step();
	}
	/** A mode the text typed into my value changed (`StepRateQuantity`),
	recorded as one step of undo with the value: Rack records the value
	itself once the text is read, so its step, on top of the history, is
	taken back and pushed again beside the mode's. */
	void recordTyped() {
		StepRateQuantity* q = dynamic_cast<StepRateQuantity*>(getParamQuantity());
		RateMode* m = mode();
		if (!q || q->typedModeWas < 0)
			return;
		const bool was = q->typedModeWas > 0;
		q->typedModeWas = -1;
		if (Member* rm = dynamic_cast<Member*>(module))
			rm->wake();
		if (!m || m->rateFreeSet() == was)
			return;
		history::ComplexAction* all = new history::ComplexAction;
		all->name = "set rate";
		if (history::Action* typed = takeTypedChange(this))
			all->push(typed);
		pushWithMode(all, was);
	}
	/** A stroke on a rate, in millionths throughout, made whole where the
	param stops being a float (`rateOfValue`): plain, a whole rate a step,
	a value with a fraction landing on the next whole rate first (`gridStep`
	on a grid of `RATE_ONE`); shift, doubled or halved as it stands, x3 up to
	x6 and down to x1.5 (`rateScaled`); command, a hundredth a step, x3.11 to
	x3.12; command and shift, a ten-thousandth, x1.0001 to x1.0002, about a
	twentieth of a hertz at 440. The fine two land on their
	own grid first as the plain one does, so x1.000123 under command goes to
	x1.01 or x1, never x1.010123. At an end the travel left over is dropped
	(`landStep`), so a stroke past it turns back at once. */
	void strokeMove(const DragMoveEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		engine::ParamQuantity* pq = getParamQuantity();
		if (!pq)
			return;
		const float dy = strokeDelta(e);
		moved = moved || strayed();
		dragAccum += dy;
		const int took = (int)stepsTaken();
		if (took == 0)
			return;
		float lo, hi;
		span(pq, lo, hi);
		const int loH = rateOfValue(lo), hiH = rateOfValue(hi);
		const int from = clamp(rateOfValue(pq->getValue()), loH, hiH);
		if (onPie) {
			const int to = clamp(pieStep(from, took), loH, hiH);
			if (to == from || !rateHasFraction(to) || pieStep(to, took > 0 ? 1 : -1) == to)
				dragAccum = 0.f; // at either end of the fraction, no travel kept past it
			landStep(pq, valueOfRate(to), lo, hi);
			Hand::readout(this, pq->getDisplayValueString());
			return;
		}
		int to;
		if (Hand::among(G_RATE_FINE))
			to = gridStep(from, Hand::among(G_RATE_FINER) ? RATE_ONE / 10000 : RATE_ONE / 100, -took, loH, hiH);
		else if (Hand::held(G_RATE_OCTAVES)) {
			to = from;
			for (int i = 0; i < std::abs(took); i++)
				to = rateScaled(to, took < 0);
		}
		else
			to = gridStep(from, RATE_ONE, -took, loH, hiH);
		landStep(pq, valueOfRate(clamp(to, loH, hiH)), lo, hi);
		Hand::readout(this, Hand::among(G_RATE_FINE) ? pq->getDisplayValueString() : std::string());
	}
	/** A stroke on the pie, `took` steps down: the fraction past the whole
	rate written moved, a hundredth a step, command a thousandth, command and
	shift a ten-thousandth, on its own grid first as the cells' fine strokes
	land; the whole part and the side, x or slash, kept, so the fraction
	stops at nought and one step short of the next whole rate, x3 to x3.99,
	/2 to /2.99. Up is more of the pie lit. */
	int pieStep(int h, int took) {
		const int unit = !Hand::among(G_RATE_FINE) ? RATE_ONE / 100
		                 : Hand::among(G_RATE_FINER) ? RATE_ONE / 10000
		                                             : RATE_ONE / 1000;
		const int mag = rateMagnitude(h), base = mag / RATE_ONE * RATE_ONE;
		const int part = gridStep(mag - base, unit, -took, 0, RATE_ONE - unit);
		return h >= RATE_ONE ? base + part : 2 * RATE_ONE - (base + part);
	}
	/** The pie's zone, from the last cell's right edge to the screen's, the
	whole height, where the pie stands (`pieAt`). */
	void placePie(const CellsLayout& l) {
		if (!l.ok)
			return;
		const float x = l.x + l.cell * (float)l.cells;
		pieZone = Rect(Vec(x, 0.f), Vec(box.size.x - x, box.size.y));
	}
	/** Idle (`VerticalDragParam::idle`), only the cells' ghosts stand, as an
	unlit screen's do, the pie's among them, and the screen takes no stroke. */
	void drawLit(const DrawArgs& args) override {
		if (module && idle()) {
			placePie(drawNumber(args.vg, box.size, "", SCHEME_YELLOW, 3, signMark(0), pieMark(0, 0)));
			return;
		}
		engine::ParamQuantity* pq = getParamQuantity();
		const int h = pq ? rateOfValue(pq->getValue()) : RATE_ONE;
		NVGcolor col = SCHEME_YELLOW;
		float bar = NAN;
		if (module)
			shown(col, bar);
		// The whole part on the two cells after the sign, decimal to 99 and
		// hex past it with its mark, x64 for x100 up to xFF (`drawCount`).
		placePie(drawCount(args.vg, box.size, rateMagnitude(h) / RATE_ONE, col, 3, signMark(h >= RATE_ONE ? 'X' : '/'),
		                   pieMark(rateMagnitude(h) % RATE_ONE, rateFreeOf(module, h) ? RATE_ONE : 0)));
		if (!std::isnan(bar))
			drawFootBar(args.vg, box.size, bar, modColor(barRGB, col));
	}
};
