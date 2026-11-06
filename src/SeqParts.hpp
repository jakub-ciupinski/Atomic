// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/** A Seq's face, its panel, its screens and buttons and the widget that
places them, with the display strings of its quantities. A part of Seq.cpp in
a file of its own: Seq.cpp includes it once, inside its anonymous namespace,
after the module, and it is not a header to include anywhere else. */

// The Seq's column (`seqRowY`, its pitch and the reason for it) is in
// Look.hpp, since a panel standing beside a Seq places against it.
const float SEQ_OUT1_Y = seqRowY(5), SEQ_OUT2_Y = seqRowY(6);
// BLEP's and Oversampling's diodes, on the OUT block's edges level with the
// foot of OUT's jack: beside its centre a tab stood over the jack's ring, and
// here it clears it by eight tenths of a millimetre, its round reaching the
// block's foot at a square corner (`EdgeTab`). Under a Clock in hertz and with
// none alike, so the pair has one place whatever drives the Seq.
const float SEQ_DIODE_Y = SEQ_OUT2_Y + JACK_R;
const EdgeTab SEQ_BLEP_TAB = {SEQ_DIODE_Y, -1, 1}, SEQ_OVER_TAB = {SEQ_DIODE_Y, 1, 1};
// The slew's lamp, on the right edge level with OUT, beside no corner.
const EdgeTab SEQ_SLEW_TAB = {seqCapY(6), 1};
// The block opens a little further above STEP than the lip alone gives, for
// air: BLOCK_AIR in Look.hpp, which every two-jack block now shares.
const float SEQ_BLOCK_Y = blockTop(seqCapY(5)) - BLOCK_AIR;
const float SEQ_BLOCK_H = blockHeight(seqCapY(5)) + BLOCK_AIR;

/** The slew's time at place `p` of its ladder as a hand reads it: on a
rung a count of samples under the seam and milliseconds from one up, to one
place where a rung has one; between two, as a command stroke leaves it, the
time between theirs at the rate the engine runs at (`Seq::slewLength`), to
two places in samples and the places a millisecond's size needs above. */
inline std::string slewText(float p) {
	const float q = clamp(p, 0.f, (float)(Seq::SLEW_RUNGS - 1));
	const int k = (int)q;
	if ((float)k == q) {
		if (k < Seq::SLEW_SAMPLE_RUNGS)
			return string::f("%d samples", Seq::slewSampleCount(k));
		const int t = Seq::slewTenths(k);
		return t % 10 ? string::f("%d.%d ms", t / 10, t % 10) : string::f("%d ms", t / 10);
	}
	const float rate = APP && APP->engine ? APP->engine->getSampleRate() : 48000.f;
	const double n = Seq::slewLength(q, rate);
	if (k < Seq::SLEW_SAMPLE_RUNGS - 1)
		return string::f("%.2f samples", n);
	const double ms = n * 1000.0 / rate;
	return string::f(ms < 10.0 ? "%.2f ms" : ms < 100.0 ? "%.1f ms" : "%.0f ms", ms);
}
/** The slew's place as its time, and "Off" alone while its switch is off,
in Slew and Interpolate alike: the time is kept, and shows once the lamp is
lit. */
std::string SlewQuantity::getDisplayValueString() {
	Seq* s = dynamic_cast<Seq*>(module);
	if (s && !s->slewOn())
		return "Off";
	return slewText(getValue());
}
/** A time typed in, kept as typed, inside the ladder's ends: samples where
it says so ("8 samples", "8 smp"), milliseconds otherwise; "off" or nought
puts the slew out, its time kept, and a time typed lights it, the switch
in force where the Seq stands (`Seq::slewSwitch`). A time typed does not
put the diodes out: the slew lit wins over them anyway (`Seq::bandHere`). */
void SlewQuantity::setDisplayValueString(std::string s) {
	Seq* m = dynamic_cast<Seq*>(module);
	const double x = std::atof(s.c_str());
	if (!(x > 0.0)) {
		if (m)
			m->params[m->slewSwitch()].setValue(0.f);
		return;
	}
	const bool samples = s.find('s') != std::string::npos && s.find("ms") == std::string::npos;
	// At the rate the engine runs at, so a time in samples and one in
	// milliseconds are on one line.
	const float rate = APP && APP->engine ? APP->engine->getSampleRate() : 48000.f;
	setValue(Seq::slewPlaceOf(samples ? x : x * rate / 1000.0, rate));
	if (m)
		m->params[m->slewSwitch()].setValue(1.f);
}

/** The screen of the step the head is on, decimal from 1 to 99 and hex past
it with its mark, no leading zero (`drawCount`), with the direction on the cell before
it and a bar along its foot while a cable in PHASE turns the sequence under
it. */
struct HeadScreen : Screen {
	Head* head = nullptr;
	Seq* seq = nullptr; // whose step screen I am, asked whether I stand idle (`Seq::stepIdle`)
	bool dirHot = false; // the direction cell over me is under the pointer
	/** The PHASE input, whose cable's colour the bar wears (`modColor`),
	asked in the step, minus one for none. */
	int barJack = -1;
	uint32_t barRGB = 0;
	void step() override {
		ModuleWidget* mw = barJack >= 0 ? getAncestorOfType<ModuleWidget>() : nullptr;
		barRGB = mw ? cableColorOn(mw->getInput(barJack)) : 0;
		Screen::step();
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		Screen::drawLayer(args, layer);
		if (layer != 1)
			return;
		// The slew's lamp, either clock context's, while it is the widget
		// dragged: I show its time, in milliseconds on my three cells, the
		// lamp having no screen of its own.
		const EdgeLampParam* lamp = dynamic_cast<const EdgeLampParam*>(APP->event->getDraggedWidget());
		if (seq && lamp && lamp->module == seq && lamp->paramId == Seq::SLEW_PARAM) {
			drawSlewTime(args);
			return;
		}
		// Idle, only the cells' ghosts stand, as on the idle rate screen
		// under me (`RateScreen::drawLit`).
		if (seq && seq->stepIdle()) {
			drawNumber(args.vg, box.size, "", SCHEME_YELLOW, 3, directionMark(0, false));
			return;
		}
		int shown = -1; // the step named, one up; with no steps, a dash
		int dir = 0;
		NVGcolor col = SCHEME_YELLOW;
		int count = 0;
		bool phased = false;
		float phase = 0.f;
		if (head) {
			count = head->shownCount();
			phased = relaxed(head->shownPhased);
			// The walk's own step, before a PHASE turn.
			const int step = phased ? relaxed(head->shownBase) : head->shownPos();
			if (count > 0)
				shown = step + 1;
			dir = relaxed(head->shownDir);
			col = playColor(head->shownColor());
			phase = relaxed(head->shownPhase);
		}
		// The direction on the first cell, the step on the two after it,
		// decimal to 99 and hex past it with its mark (`drawCount`), the
		// rate's cells under them lining up with them; the < lit reverse,
		// the > forward, both for the pendulum.
		const int lit = dir == DIR_FORWARD ? 2 : dir == DIR_REVERSE ? 1 : 3;
		const CellsLayout cells = shown < 0 ? drawNumber(args.vg, box.size, "-", col, 3, directionMark(lit, dirHot))
		                                    : drawCount(args.vg, box.size, shown, col, 3, directionMark(lit, dirHot));
		if (head && count > 0 && relaxed(head->shownArmed))
			drawArmMark(args.vg, cells, col);
		// A cable in PHASE turns the sequence under the playhead: the cells
		// name the walk's own step, and a bar along the bottom edge, bipolar
		// from the middle, says how far the turn has carried the playhead
		// from it, in places of the cycle with the last one full, flowing for
		// a block of CRVs, in the colour of the cable in PHASE.
		if (!phased)
			return;
		drawFootBar(args.vg, box.size, phase, modColor(barRGB, col));
	}
	/** The slew's time in milliseconds, three figures wherever it stands
	("0.04" for two samples at 48 kHz, "1.25", "12.5", "500"), a dash while
	the slew is off, in the Seq's colour. */
	void drawSlewTime(const DrawArgs& args) {
		const NVGcolor col = playColor(seq->shownColor());
		if (!seq->slewOn()) {
			drawNumber(args.vg, box.size, "-", col, 3);
			return;
		}
		const float rate = APP->engine->getSampleRate();
		const double ms = Seq::slewLength(seq->slewPlace(), rate) * 1000.0 / rate;
		drawNumber(args.vg, box.size, string::f(ms < 10.0 ? "%.2f" : ms < 100.0 ? "%.1f" : "%.0f", ms), col, 3);
	}
};

struct SeqPanel : Panel {
	Seq* module = nullptr;
	uint64_t version() override {
		if (!module)
			return 0;
		return (relaxed(module->recNow) ? 1 : 0) |
		       (relaxed(module->outPoly) ? 4 : 0) |
		       (module->phasedShown() ? 8 : 0) | (relaxed(module->routeNow) ? 16 : 0) |
		       (relaxed(module->overAtnNow) || relaxed(module->overQntNow) ? 32 : 0) |
		       (module->triggersShown() ? 64 : 0) | (rateFreeOf(module, module->rateSet()) ? 128 : 0) |
		       (module->slewShown() ? 256 : 0) | (module->bandShown() ? 512 : 0) | (module->overShown() ? 1024 : 0);
	}
	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		const bool rec = module && relaxed(module->recNow);
		const bool route = module && relaxed(module->routeNow);
		drawPlate(args, this, route ? "ROQ" : rec ? "REQ" : "SEQ");
		// REQ: the plate's title while the IN jack is being taken, the R in
		// red over the S; and ROQ while it is routed into a block of Atns,
		// the RO in red. The red is drawn where the centred title put it, so
		// the letters line up exactly.
		if (route)
			drawRecTitle(vg, box.size, "ROQ", "RO");
		else if (rec)
			drawRecTitle(vg, box.size, "REQ", "R");
		// From the top REC, CLK, RATE, RESET and PHASE, row for row beside
		// a Clock's BPM, CLK, RATE, RESET and RUN. The Seq's own clock is there whether or not a block is
		// walked. RATE is FM while the rate is free, its pie showing, the jack
		// then a volt an octave at audio rate.
		// The pulse jack is the end of the cycle, except with a cable in
		// PHASE, that being the one thing that moves the step without the
		// count moving.
		drawCaption(vg, CX, seqCapY(1), "CLK", INK);
		drawCaption(vg, CX, seqCapY(2), rateJackCaption(module && rateFreeOf(module, module->rateSet())), INK);
		drawCaption(vg, CX, seqCapY(3), "RESET", INK);
		drawCaption(vg, CX, seqCapY(4), "PHASE", INK);
		// The jack is named by what the Seq walks. REC over CVs, CRVs and Trgs, not IN: what
		// goes in it is written into the step the playhead stands on, and IN
		// said only that it was an input. ROUTE over a block of Atns, through
		// the Atn stood on and out of it (ROUT read as R OUT, an output). QNT
		// IN over a block of Qnts, into the Qnt stood on and out of it
		// quantized, since nothing is recorded into a Qnt. POLY IN
		// standing alone, where the channels are the steps: the player must
		// know a poly cable goes there.
		const char* word = !module                      ? "REC"
		                   : relaxed(module->overAtnNow) ? "ROUTE"
		                   : relaxed(module->overQntNow) ? "QNT IN"
		                   : relaxed(module->aloneNow)   ? "POLY IN"
		                                                 : "REC";
		drawCaption(vg, CX, seqCapY(0), word, INK);
		// The block with the tabs of the lamps shown on it (`SeqWidget`), all
		// three in the browser.
		std::vector<EdgeTab> tabs;
		if (!module || module->slewShown())
			tabs.push_back(SEQ_SLEW_TAB);
		if (!module || module->bandShown())
			tabs.push_back(SEQ_BLEP_TAB);
		if (!module || module->overShown())
			tabs.push_back(SEQ_OVER_TAB);
		drawOutBlock(vg, SEQ_BLOCK_Y, SEQ_BLOCK_H, tabs);
		drawCaption(vg, CX, seqCapY(5), (module && module->phasedShown()) ? "STEP" : "EOC", PALE);
		// The jack says what it carries: one channel, or several. Over Trgs,
		// one a step, it is the hits, and says so: a newcomer patched the
		// drums from the Trg's own OUT, the pattern as a gate a ring, and
		// heard nothing move.
		const bool poly = module && relaxed(module->outPoly);
		drawCaption(vg, CX, seqCapY(6), module && module->triggersShown() ? "TRIG" : poly ? "POLY" : "OUT", PALE);
	}
};

/** The strip's square, P over Trgs and Step or Fade over Atns: as tall as
the strip and square, a hair of the strip's black between it and the
modes. */
const float POLY_SQUARE_W = HEAD_STRIP_H;
const float POLY_SQUARE_GAP = 0.5f;
/** The switch over the jacks: the Seq's mode. The mode is four, and a
click goes round them, PLAY, PREVIEW, HALT, ONCE and PLAY again, and
where there is nothing to preview, standing alone, an arpeggiator, and over
a block of Trgs, the same without PREVIEW (`Seq::modeAfter`): the word names
the one in force (`Seq::modeShown`), and the light says whether the walk
runs, as the Clock's RUN does: PLAY lit steadily, PREVIEW breathing,
running with an ear open, and HALT dark, the sequencer standing still; in ONCE the light is the pass, dark
before and after it (`darkTo`). Over Trgs it gives the strip's left square
to P, poly (`StripSquare`), so a trigger Seq keeps its modes and can be
stopped without stopping its Clock. Over Atns it gives it to Step or Fade. */
struct ModeButton : ScreenButton {
	Seq* seq = nullptr;
	Seq::Mode shownMode() { return seq ? seq->modeShown() : Seq::MODE_PREVIEW; }
	/** Lit while the walk runs, PLAY or PREVIEW, and dark in HALT: a
	PREVIEW kept on a lone Seq is PLAY there, and lit steadily. In ONCE
	lit while the pass runs, and dark armed or spent. */
	bool on() override {
		if (shownMode() == Seq::MODE_ONCE)
			return seq && seq->onceDark() >= 0 && seq->onceDark() < seq->onceCycle();
		return shownMode() != Seq::MODE_HALT;
	}
	/** In ONCE the light is the pass, a share of it for every place of the
	cycle, and the dark comes in from the left a share a step, so the step
	playing is the first lit share and the last step the last. */
	float darkTo() override {
		if (shownMode() != Seq::MODE_ONCE || !seq || seq->onceCycle() <= 0)
			return 0.f;
		return clamp((float)seq->onceDark() / seq->onceCycle(), 0.f, 1.f);
	}
	/** Asked at every step and again as a frame is drawn, since a frame can
	be drawn without a step before it. The word follows the press at once,
	as the light does. */
	void follow() {
		static const char* words[Seq::MODES] = {"PLAY", "PREVIEW", "HALT", "ONCE"};
		text = words[shownMode()];
	}
	/** Over Trgs and over Atns the strip is split, its square on the left
	(`StripSquare`) and the modes in the rest. */
	void place() {
		const float left = seq && seq->squareShown() ? POLY_SQUARE_W + POLY_SQUARE_GAP : 0.f;
		box.pos.x = mm2px(left);
		box.size.x = mm2px(PANEL_W - left);
	}
	/** PREVIEW's light breathes (`breathing`), so it is never mistaken for
	PLAY's, which stands still. */
	float glow() override {
		if (shownMode() == Seq::MODE_PREVIEW)
			return std::max(breathing(), flash());
		return ScreenButton::glow();
	}
	/** A click goes on to the next mode, one undo each; shift-click goes
	straight to PLAY and command-click to HALT, each to the other where it
	is in force already (`Seq::modeToward`). */
	void onButton(const ButtonEvent& e) override {
		engine::ParamQuantity* pq = getParamQuantity();
		if (pq && e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			const float old = pq->getValue();
			Seq::Mode to = seq ? seq->modeAfter() : (Seq::Mode)(((int)shownMode() + 1) % Seq::MODES);
			if (seq && Hand::in(G_MODE_PLAY, e.mods))
				to = seq->modeToward(Seq::MODE_PLAY, Seq::MODE_HALT);
			else if (seq && Hand::in(G_MODE_HALT, e.mods))
				to = seq->modeToward(Seq::MODE_HALT, Seq::MODE_PLAY);
			pq->setImmediateValue((float)to);
			recordParamChange(this, old);
			e.consume(this);
			return;
		}
		ScreenButton::onButton(e);
	}
	void step() override {
		follow();
		place();
		ScreenButton::step();
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		follow();
		ScreenButton::drawLayer(args, layer);
	}
};

/** The strip's left square, a two-state switch lit while on, standing only
where the block it means something over is walked (`shown`); the rest of the
strip is the modes (`ModeButton`). Over Trgs it is P: vertical poly, a Trg a
step and its rings the channels. Over
Atns it is Fade (`FadeSquare`). One component, so the two are the same
square. */
struct StripSquare : ScreenButton {
	std::function<bool()> shown;
	void step() override {
		visible = shown && shown();
		ScreenButton::step();
	}
};

/** Over routing Atns, the square is Step or Fade (`Seq::FADE_PARAM`), lit in Fade,
and carries the crossfade's glyph in place of a letter (`paintCrossfade`). */
struct FadeSquare : StripSquare {
	void drawWord(const DrawArgs& args, const std::string& word, NVGcolor col) override {
		paintCrossfade(args.vg, box.size.x / 2.f, box.size.y / 2.f, col);
	}
};

/** The direction cell, the first cell of the step screen: a click goes round
forward, pendulum and reverse, one undo each. It is a gesture, a button, so a
single click acts and a double click is two of them (`MarkCell`). It covers the left of the screen, the cell and the margin
before it, so a press there never reaches the step under it; the screen paints
the cell (`drawNumber`), lit up under the pointer. On an idle screen the cell
is dark and turns nothing. */
struct DirCell : MarkCell {
	Seq* seq = nullptr;
	bool can() override { return !(seq && seq->stepIdle()); }
	// Round the directions as the mode button rounds its modes
	// (`Seq::modeAfter`), one plain click a step (`stepRound`).
	float next(float v) override { return (float)stepRound((int)std::round(v), 1, DIRS); }
};

/** The step screen, the top one, as a parameter. Driven, it is not a
parameter at all: a double click on it restarts the playhead, the way the
RESET jack does, and a drag on it holds the step under the mouse, playing it,
until the mouse is let go, when the Seq jumps there and carries on: a reset to
anywhere. With nothing driving it, dragging it walks the steps there are, a
drag unit being one of them, and a double click is step one, the param's
default. Either way a click alone changes nothing, VCV's rule for every
screen. With a cable in the PHASE jack it is the step PHASE counts from,
and a drag holds or picks it as above. */
struct StepParam : VerticalDragParam {
	StepParam() { pxPerUnit = PX_PER_POS; }
	int holdFrom = 0;
	float holdAccum = 0.f;
	Seq* seq() { return paramModule<Seq>(this); }
	/** Nothing to play (`Seq::stepIdle`): the screen takes no stroke, no
	hold and no reset. */
	bool idle() override {
		Seq* m = seq();
		return m && m->stepIdle();
	}
	/** Whether the stroke picks the step (unclocked) or holds and jumps
	(driven): decided at the press and kept to the release, so a drive
	arriving or leaving mid-stroke cannot hand the end of a hold to the
	picking code, which would leave the Seq holding with nobody to let go and
	record an undo against a value the stroke never took. */
	bool pickingStroke = false;
	bool picking() {
		if (APP->event->getDraggedWidget() == this)
			return pickingStroke;
		Seq* m = seq();
		return m && m->picksShown();
	}
	/** Driven, a press holds the playhead, so the wheel picks a step only
	where a drag would pick one. */
	bool wheelTakes() override { return picking(); }
	bool slaved() {
		Seq* m = seq();
		return m && m->slavedShown();
	}
	/** Picking, the steps there are to pick: the param holds the cap, MAX_SEQ_STEPS,
	and a block of four shows four. */
	void span(engine::ParamQuantity* pq, float& lo, float& hi) override {
		Seq* m = seq();
		lo = 0.f;
		hi = m ? (float)std::max(m->shownCount() - 1, 0) : pq->getMaxValue();
	}
	/** Picking, the default is the param's, step one; driven, the restart
	happens at the release (`onDragEnd`). */
	void resetToDefault(engine::ParamQuantity* pq) override {
		if (picking())
			VerticalDragParam::resetToDefault(pq);
	}
	void onDragStart(const DragStartEvent& e) override {
		Seq* m = seq();
		pickingStroke = false;
		if (m && idle())
			return;
		pickingStroke = m && m->picksShown();
		if (pickingStroke) {
			VerticalDragParam::onDragStart(e);
			return;
		}
		if (e.button != GLFW_MOUSE_BUTTON_LEFT || !m || !slaved())
			return;
		moved = false;
		holdAccum = 0.f;
		strokeBegins();
		holdFrom = m->phasedShown() ? relaxed(m->shownBase) : m->shownPos(); // the step the screen names
		m->holdPos.store(holdFrom, std::memory_order_relaxed);
		m->holding.store(true, std::memory_order_relaxed);
		m->wake();
		if (settings::knobMode <= settings::KNOB_MODE_LINEAR)
			APP->window->cursorLock();
	}
	void strokeMove(const DragMoveEvent& e) override {
		if (picking()) {
			VerticalDragParam::strokeMove(e);
			return;
		}
		Seq* m = seq();
		if (e.button != GLFW_MOUSE_BUTTON_LEFT || !m || !relaxed(m->holding))
			return;
		const float dy = strokeDelta(e);
		moved = moved || strayed();
		holdAccum += dy;
		const int n = std::max(m->shownCount(), 1);
		holdAccum = clamp(holdAccum, (float)(holdFrom - (n - 1)) * pxPerUnit, (float)holdFrom * pxPerUnit);
		m->holdPos.store(clamp(holdFrom - (int)std::round(holdAccum / pxPerUnit), 0, n - 1), std::memory_order_relaxed);
		m->wake();
	}
	void onDragEnd(const DragEndEvent& e) override {
		if (picking()) {
			VerticalDragParam::onDragEnd(e);
			return;
		}
		Seq* m = seq();
		if (e.button != GLFW_MOUSE_BUTTON_LEFT || !m || !relaxed(m->holding))
			return;
		if (settings::knobMode <= settings::KNOB_MODE_LINEAR)
			APP->window->cursorUnlock();
		// Let go elsewhere: a jump there. Let go where the second press of a
		// double click was: a restart. A click alone: nothing. Said before
		// the hold is let go, so no wake sees the hold gone and no jump.
		if (moved)
			m->jumpClick.store(relaxed(m->holdPos), std::memory_order_relaxed);
		else if (spent)
			m->resetClick.store(true, std::memory_order_relaxed);
		m->holding.store(false, std::memory_order_relaxed);
		m->wake();
	}
};

/** The mode in force, a word: PREVIEW where there is nothing to preview
being PLAY (`Seq::modeShown`). What it does there is its lesson
(`Seq::retipMode`). */
std::string ModeQuantity::getDisplayValueString() {
	Seq* m = dynamic_cast<Seq*>(module);
	if (!m)
		return SwitchQuantity::getDisplayValueString();
	return labels[m->modeShown()];
}

std::string SeqRateQuantity::getDisplayValueString() {
	Seq* m = dynamic_cast<Seq*>(module);
	return m && m->rateIdle() ? std::string(SEQ_IDLE_WORD) : StepRateQuantity::getDisplayValueString();
}

/** Nothing to play, the hover says so in place of "Step: 1", the screen
already showing it idle. */
std::string StepQuantity::getString() {
	Seq* m = dynamic_cast<Seq*>(module);
	return m && m->stepIdle() ? std::string(SEQ_NOTHING_YET) : HintQuantity::getString();
}
std::string StepQuantity::getDisplayValueString() {
	Seq* m = dynamic_cast<Seq*>(module);
	return m && m->stepIdle() ? std::string(SEQ_NOTHING_YET) : ParamQuantity::getDisplayValueString();
}

/** The rate, on the screen every rate is drawn on (`RateScreen`), the
Clock's ladder: /2 is a step every second tick, x1 the driver's own rate, x2
two steps a tick. With its jack patched the number shown is still the one
set and the bar along the bottom edge is the voltage arriving, bipolar
from the middle, as the bar under the step is the phase arriving. A plain
drag moves by one and shift held lands on the powers of two, as on every
ladder, command held sets the hundredths and command and shift the
ten-thousandths (`RateScreen`). */
struct SeqRateParam : RateScreen {
	/** No clock driving the walk, nothing reaching it or HALT ignoring it,
	the rate paces nothing, and a Seq standing left of its Clock would read
	x1 over a step it never left: the rate stands dark (`Seq::rateIdle`). */
	bool idle() override {
		Seq* w = paramModule<Seq>(this);
		return w && w->rateIdle();
	}
	/** And says nothing under the pointer, as a part with nothing to do
	is hidden: the hover Rack gives a param goes the frame it would show,
	and a hover begun while it paced goes when it stops. */
	void onEnter(const EnterEvent& e) override {
		RateScreen::onEnter(e);
		if (module && idle())
			destroyTooltip();
	}
	void step() override {
		if (module && idle())
			destroyTooltip();
		RateScreen::step();
	}
	void shown(NVGcolor& col, float& bar) override {
		Seq* w = paramModule<Seq>(this);
		if (!w)
			return;
		col = playColor(w->shownColor());
		if (relaxed(w->ratePatched))
			bar = jackBar(relaxed(w->rateCv));
	}
};

/** How much of the step screen's width the direction cell takes from the
left: the first of its three cells and the margin before it, up to where
the second cell begins, a little less than four tenths at every zoom since
the cells are centred and sized with the screen. */
const float DIR_CELL_W = 0.38f;

struct SeqWidget : MemberWidget {
	Seq* seq = nullptr;
	void step() override {
		recordTake();
		MemberWidget::step();
	}
	/** A REC take is one undo: what the recording wrote into
	the faders and rings is pushed as one change when it stops, so an
	accidental take comes back in one step however many steps it wrote. A take
	is the patch performing on the params, not a hand, so nothing is pushed
	while it runs. The values it starts from are the frame's before it began:
	every frame a take is not running, every param of every module standing to
	my left in my run is kept, since the engine writes the first step on the
	wake the take starts, before this widget sees it. */
	GestureChanges shadow, take;
	bool taking = false;
	void recordTake() {
		const bool rec = seq && relaxed(seq->recNow);
		if (rec && !taking)
			take = shadow;
		// A module deleted during the take is not in it (`HeldModule`).
		else if (!rec && taking)
			take.push("record");
		taking = rec;
		if (taking)
			return;
		shadow.was.clear();
		const std::vector<ModuleWidget*>* run = runOf(this);
		if (!run)
			return;
		for (ModuleWidget* mw : *run) {
			if (mw == this)
				break;
			// A bonder (a RIG) parts the rigs: what stands before it is no
			// part of mine, and never in a take of mine.
			if (mw->module && isBonder(mw->module)) {
				shadow.was.clear();
				continue;
			}
			if (mw->module)
				for (int p = 0; p < (int)mw->module->params.size(); p++)
					shadow.was.push_back({HeldModule(mw->module), p, mw->module->params[p].getValue()});
		}
	}
	SeqWidget(Seq* module) {
		SeqPanel* panel = new SeqPanel;
		panel->module = module;
		open(module, panel);

		// The signal goes in at the top of the column.
		addInput(createInputCentered<InPort>(mm2px(Vec(CX, seqRowY(0))), module, Seq::IN_INPUT));

		// The step playing stands at the top; the rate is the screen under
		// it.
		HeadScreen* playing = createWidget<HeadScreen>(mm2px(Vec(0.f, SCREEN_Y)));
		playing->box.size = mm2px(Vec(PANEL_W, HEAD_SCREEN_H));
		playing->head = module;
		playing->seq = module;
		StepParam* pp = createParam<StepParam>(Vec(0, 0), module, Seq::STEP_PARAM);
		pp->box.size = playing->box.size;
		playing->addChild(pp);
		// The direction cell over the left of it, on top so it takes the press.
		DirCell* dc = createParam<DirCell>(Vec(0, 0), module, Seq::DIR_PARAM);
		dc->box.size = Vec(playing->box.size.x * DIR_CELL_W, playing->box.size.y);
		dc->seq = module;
		dc->hot = &playing->dirHot;
		playing->addChild(dc);
		addChild(playing);
		playing->barJack = Seq::PHASE_INPUT;
		addScreen<SeqRateParam>(this, module, Seq::RATE_PARAM, HEAD_SCREEN2_Y, HEAD_SCREEN_H)->barJack = Seq::RATE_INPUT;

		// Under REC, CLK, then RATE, then RESET, then PHASE.
		addInput(createInputCentered<InPort>(mm2px(Vec(CX, seqRowY(1))), module, Seq::CLOCK_INPUT));
		addInput(createInputCentered<InPort>(mm2px(Vec(CX, seqRowY(2))), module, Seq::RATE_INPUT));
		addInput(createInputCentered<InPort>(mm2px(Vec(CX, seqRowY(3))), module, Seq::RESET_INPUT));
		addInput(createInputCentered<InPort>(mm2px(Vec(CX, seqRowY(4))), module, Seq::PHASE_INPUT));
		// PREVIEW stands at the head of the column, right under the second
		// screen and almost touching it: it is the one thing here that is
		// about what the Seq is rather than about what is patched into it,
		// and it belongs above the jacks rather than among them.
		// Always there: the mode over a block, PLAY or HALT standing
		// alone, and POLY over Trgs.
		ModeButton* pv = addStripButton<ModeButton>(this, module, Seq::MODE_PARAM, "PREVIEW");
		pv->seq = module;
		pv->colorOf = [module]() { return playColor(module ? module->shownColor() : 0); };
		// Over Trgs, P on the strip's left, the modes beside it; over Atns,
		// Step or Fade there.
		auto square = [&](StripSquare* q, std::function<bool()> shown) {
			q->box.size = mm2px(Vec(POLY_SQUARE_W, HEAD_STRIP_H));
			q->strip = true;
			q->px = HEAD_STRIP_PX;
			q->shown = shown;
			q->visible = false;
			q->colorOf = pv->colorOf;
			addChild(q);
		};
		StripSquare* p = createParam<StripSquare>(mm2px(Vec(0.f, HEAD_STRIP_Y)), module, Seq::ACROSS_PARAM);
		p->text = "P";
		square(p, [module]() { return module && relaxed(module->gatesNow); });
		FadeSquare* f = createParam<FadeSquare>(mm2px(Vec(0.f, HEAD_STRIP_Y)), module, Seq::FADE_PARAM);
		f->text = "FADE";
		square(f, [module]() { return module && relaxed(module->overRoutersNow); });
		seq = module;
		addOutput(createOutputCentered<OutPort>(mm2px(Vec(CX, SEQ_OUT1_Y)), module, Seq::STEP_OUTPUT));
		addOutput(createOutputCentered<OutPort>(mm2px(Vec(CX, SEQ_OUT2_Y)), module, Seq::CV_OUTPUT));
		// The slew, a lamp on the block's right edge beside OUT, lit while
		// it is on, there only where it acts (`Seq::slewShown`): over CVs
		// and CRVs, over Atns in Step, and standing alone. Oversampling's diode is
		// hidden over Atns, BLEP's standing alone (`Seq::overShown`).
		// In the Seq's colour, as its PREVIEW is: a control of this Seq's,
		// lit as its own.
		auto color = [module]() { return playColor(module ? module->shownColor() : 0); };
		// BLEP and Oversampling, diodes of the same part on the block's
		// left edge and its right, beside the lamp wherever each acts, and one
		// choice with it, so a diode lit puts the slew out and the slew lit
		// puts both out. A click is both; command,
		// the one alone; shift, on every Seq of the run; command and shift,
		// the one on every Seq of the run, each Seq the pair in force where it
		// stands (`setAlongRun`, `Seq::bandWrites`). Each clock context keeps
		// its own three (`Seq::Choice`), so the three are made for each, on
		// the same places, and those of the context in force are shown; the
		// browser shows those outside hertz, dim.
		for (int hz = 0; hz <= 1; hz++) {
			const Seq::Choice c = Seq::choiceIn(hz);
			auto here = [module, hz]() { return relaxed(module->hertzNow) == (hz != 0); };
			EdgeLampParam* slew = addEdgeLamp(this, module, Seq::SLEW_PARAM, c.slewOn, SEQ_SLEW_TAB,
			                                  [module, here]() { return here() && module->slewShown(); }, color);
			slew->inBrowser = !hz;
			slew->toggle = &G_SLEW_TOGGLE;
			slew->fine = &G_LAMP_FINE;
			// The time, round the lamp's rim (`SLEW_HALO`), a full turn at half
			// a second, its first rungs being counts of samples at the engine's rate.
			slew->fillOf = [module]() {
				const float sr = APP->engine->getSampleRate();
				return clamp((float)(Seq::slewLength(module->slewPlace(), sr) / sr * 1000.0) / SLEW_HALO_FULL_MS, 0.f, 1.f);
			};
			slew->putsOut = {c.blep, c.over};
			for (int side = -1; side <= 1; side += 2) {
				const bool blep = side < 0;
				const int id = blep ? c.blep : c.over;
				// Both pairs stand on one place, below the jack: the lamp keeps
				// its shoulder, and beside the jack itself a tab overlapped its
				// ring (`SEQ_DIODE_Y`).
				EdgeLampParam* d = addEdgeLamp(this, module, id, id, blep ? SEQ_BLEP_TAB : SEQ_OVER_TAB,
				                               [module, here, blep]() { return here() && (blep ? module->bandShown() : module->overShown()); },
				                               color);
				d->inBrowser = !hz;
				d->inForce = [module]() { return !module->slewInForce(); };
				d->one = &G_DIODE_ONE;
				d->row = &G_DIODE_ROW;
				// `one` is shift and command, which holds `row`'s command too:
				// this diode alone on this SEQ, never rig-wide.
				d->clicked = [this, d, blep](bool alone, bool rig) {
					const bool command = alone, shift = rig && !alone;
					const float to = d->lit() ? 0.f : 1.f;
					const char* what = command ? (blep ? "BLEP" : "oversampling") : "BLEP and oversampling";
					const std::string name =
					    std::string(to > 0.5f ? "turn on " : "turn off ") + what + (shift ? " on the rig" : "");
					const bool b = !command || blep, o = !command || !blep;
					setAlongRun(
					    this,
					    [b, o, to](Module* m, std::vector<std::pair<int, float>>& out) {
						    if (Seq* s = dynamic_cast<Seq*>(m))
							    s->bandWrites(b, o, to, out);
					    },
					    shift, name);
				};
			}
		}
	}
};
