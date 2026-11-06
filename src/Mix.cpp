// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Panel.hpp"
#include "GatherFace.hpp"
#include "Stack.hpp"

/** Mix: mono outputs to its left, summed into one jack.

The Poly's mirror: it gathers to its left exactly as a Poly does and from the
same pool (`gather`, Gather.hpp), the kind of the nearest module to its left
that no Poly or Mix before it took and every free module of that kind, fifteen
at most (a Poly takes sixteen), in rig order; and where a Poly packs them into one polyphonic cable,
a Mix adds them into one voltage. The body is a Poly's, a row a source in its
source's colour, fifteen of them and the sum as the sixteenth, so the two
bodies are one geometry. Each row is a fader: a drag up or down sets its level
in the sum, and the row grows thinner as it comes down, to a line at nothing
that still shows its source's signal; the hover says the level as a
percentage. The sum row is a fader too, the mix's level, thinning as its rows
do; and a click with shift held on any row is its mute, a switch of its own
beside the level, which the mute keeps: the row is silent and its bar
gone, a gap where a level at nothing is still a faint line, its tooltip
says the level kept and "(muted)", a second shift click gives the level
back, and a stroke from a muted row starts at nothing, unmuted. A click with
command held on a channel's row is its solo, another switch of its own:
while any row with a source is soloed only the soloed rows are heard and
drawn, each pointed at from both sides of the glass, the others held silent
and their bars gone, their levels and mutes untouched and said "(silenced
by solo)". A solo stands over a mute: a soloed row is heard though muted,
and muted again once unsoloed. A command stroke solos each channel it crosses in turn,
the others not, so one hold auditions channel after channel and the last
stays soloed; a command click on the master clears every mute and solo.
The faders so give up the fine stroke every other fader has on command: a
mixer's level is set by ear, and typed where it must be exact. A stroke
with shift and command moves every channel together, as ganged faders, and
a double click with them puts every channel back at all of it. The
sum is the real number, unclamped, as every processor's is.

The IN jack is a polyphonic in. With a cable in it, its channels take the
gathered sources' place, a row each with the same mutes, so standing on its
own a Mix sums a polyphonic cable; and with several cables stacked on it, each
cable is a row in its own colour (`Stack`), so a Mix alone is a small mixer of
whatever is plugged into it, every source drawn and each one at its own level.
A cable in IN also releases what the Mix would gather, so a Poly or a Mix to
its right takes it instead. Nothing to its right takes anything of it through
the rig: a master fader is an Atn cabled from its jack. */

using namespace rig;

namespace {

/** A channel's gestures and the master's (Words.hpp): the mute, said as
what the click does now, mute or unmute, and on a channel one piece for the
click and the shift stroke that carries it over every channel it crosses,
the same press; on a channel the solo and its scan, on the master the
clearing of every mute and solo, on the same key; and the stroke that
moves every channel together, where there is a channel to move. A fader is a channel, never a rig, which is the chain of
modules. The mute is read as `G_MIX_MUTE` alone, its other wordings the
same chord, and the solo as `G_MIX_SOLO`, ranked as a chord reaching past
its part, since it silences every other channel. */
inline const Gesture G_MIX_MUTE =
    declare(On::Click, Mod::Shift, Rank::Invented, RACK_MOD_SHIFT_NAME "-click or -drag: mute channels");
inline const Gesture G_MIX_UNMUTE =
    declare(On::Click, Mod::Shift, Rank::Invented, RACK_MOD_SHIFT_NAME "-click or -drag: unmute channels");
inline const Gesture G_MIX_MUTE_MIX = declare(On::Click, Mod::Shift, Rank::Invented, RACK_MOD_SHIFT_NAME "-click: mute the mix");
inline const Gesture G_MIX_UNMUTE_MIX =
    declare(On::Click, Mod::Shift, Rank::Invented, RACK_MOD_SHIFT_NAME "-click: unmute the mix");
inline const Gesture G_MIX_SOLO = declare(On::Click, Mod::Command, Rank::Block, RACK_MOD_CTRL_NAME "-click: solo channel");
inline const Gesture G_MIX_UNSOLO = declare(On::Click, Mod::Command, Rank::Block, RACK_MOD_CTRL_NAME "-click: unsolo channel");
inline const Gesture G_MIX_SCAN =
    declare(On::Drag, Mod::Command, Rank::Block, RACK_MOD_CTRL_NAME "-drag: solo each channel crossed in turn");
inline const Gesture G_MIX_OPEN = declare(On::Click, Mod::Command, Rank::Block, RACK_MOD_CTRL_NAME "-click: clear every mute and solo");
inline const Gesture G_MIX_ALL =
    declare(On::Drag, Mod::ShiftCommand, Rank::Rare, RACK_MOD_SHIFT_NAME "+" RACK_MOD_CTRL_NAME "-drag: scale all channels");
inline const Gesture G_MIX_RESET_ALL =
    declare(On::Double, Mod::ShiftCommand, Rank::Rare, RACK_MOD_SHIFT_NAME "+" RACK_MOD_CTRL_NAME "-double-click: reset all channels");
/** A channel's, muted or not and soloed or not. */
inline Gestures channelGestures(bool muted, bool soloed) {
	return {muted ? &G_MIX_UNMUTE : &G_MIX_MUTE, soloed ? &G_MIX_UNSOLO : &G_MIX_SOLO, &G_MIX_SCAN, &G_MIX_ALL, &G_MIX_RESET_ALL};
}
/** The master's, muted or not, and scaling the channels only where there
are any. */
inline Gestures masterGestures(bool muted, bool channels) {
	Gestures g{muted ? &G_MIX_UNMUTE_MIX : &G_MIX_MUTE_MIX, &G_MIX_OPEN};
	if (channels) {
		g.push_back(&G_MIX_ALL);
		g.push_back(&G_MIX_RESET_ALL);
	}
	return g;
}
/** A muted fader's one surprise: a stroke takes it out of its mute from
nothing, not from the level its value still says (`MixRow::strokeMove`),
and a reset at its default, all of it (`MixRow::onDoubleClick`), said with
no gesture's name, the tooltip naming none. */
const char* const MIX_MUTED_LESSON = "Moved, it unmutes from zero; reset, at full.";
/** What nothing on the panel says of a heard fader: its bar's thickness is
its level and its length the held swing of what comes in (`MixScreen`), so
a thin bar is a low level, not a quiet
source. A channel's swing is its source's, before the level; the master's is
the sum's, after it. */
const char* const MIX_CHANNEL_LESSON = "Its thickness is the level, its length how far the source moves.";
const char* const MIX_MASTER_LESSON = "Its thickness is the mix's level, its length how far the mix moves.";

const int ROWS = GATHER_MAX - 1; // the sixteenth row is the sum's

/** A channel's level as its tooltip says it: the level set, and "(muted)"
after it while the row's mute holds it at nothing, the one place a mute is
told from a level at nothing; while a solo stands, "(soloed)" or "(silenced
by solo)" instead. */
struct LevelQuantity : HintQuantity {
	std::string getUnit() override;
};

struct Mix : Gatherer<Mix, ROWS, ROWS + 1> {
	enum ParamId {
		GATHER_PARAM,                 // what I gather, the strip's choice (`Choice`)
		ENUMS(LEVEL_PARAM, ROWS), // each row's level in the sum, a drag on its bar
		SUM_PARAM,                    // the sum's level, a drag on the last bar
		ENUMS(MUTE_PARAM, ROWS + 1),  // each row's mute and the sum's, apart from its level (`heard`)
		ENUMS(SOLO_PARAM, ROWS),      // each row's solo; the sum has none (`heard`)
		PARAMS_LEN
	};
	enum InputId {
		IN_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		MIX_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	float sum = 0.f; // the sum of the rows left on, at the sum's level; the last bar meters it
	static_assert(SUM_PARAM == LEVEL_PARAM + ROWS, "the sum's level is the row after the last");
	/** Row `c`'s level, or the sum's for `ROWS`: the level set, which a
	mute keeps. */
	float level(int c) { return clamp(params[LEVEL_PARAM + c].getValue(), 0.f, 1.f); }
	/** Whether row `c`, or the sum for `ROWS`, is muted: a switch of its own
	beside the level, so a mute keeps the level set and unmuting gives it
	back. */
	bool muted(int c) { return params[MUTE_PARAM + c].getValue() > 0.5f; }
	/** Whether channel `c` is soloed, a switch of its own; never the sum. */
	bool soloed(int c) { return c < ROWS && params[SOLO_PARAM + c].getValue() > 0.5f; }
	/** Whether a solo stands among the first `rows` rows, the ones with a
	source: a solo left on a row whose source has gone is on no row a hand
	can reach to undo it, so it silences nothing. */
	bool soloing(int rows) {
		for (int c = 0; c < std::min(rows, ROWS); c++)
			if (soloed(c))
				return true;
		return false;
	}
	/** Whether the switches let row `c` through, `solo` being `soloing` of
	the rows there are: while a solo stands a channel is through when
	soloed and only then, its mute held under the solo for when it is
	unsoloed; otherwise, and always for the sum, when not muted. The one
	place the rule lives: what is heard and what the screen draws both ask
	it. */
	bool open(int c, bool solo) { return solo && c < ROWS ? soloed(c) : !muted(c); }
	/** Whether row `c`'s mute silences it now, a solo on it not standing
	over the mute: a row with a source is soloed only while a solo stands. */
	bool muteHolds(int c) { return muted(c) && !soloed(c); }
	/** The level the row is heard at: its own while open, else nothing. */
	float heard(int c, bool solo) { return open(c, solo) ? level(c) : 0.f; }
	/** The levels as the engine plays them, gliding to where the hand put
	them (`Glide`), so a
	stroke on a bar is no click in the sum. The bars show the levels. I am
	on the tick for my meters already, so each glide is stepped every
	sample, one comparison while it stands. */
	Glide levelGlide[ROWS + 1];
	float levelPlayed(int c, bool solo) { return clamp(levelGlide[c].played(heard(c, solo)), 0.f, 1.f); }
	bool everySample(const ProcessArgs& args) override {
		const bool land = landing();
		const bool solo = soloing(srcN);
		bool glided = false;
		// A mute or a solo glides to nothing and back as a stroke does, so
		// it is no click in the sum either.
		for (int c = 0; c <= ROWS; c++)
			glided |= levelGlide[c].step(heard(c, solo), args.sampleRate, args.frame, land);
		return Gatherer::everySample(args) || glided;
	}

	Mix() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int c = 0; c <= ROWS; c++) {
			configParam<LevelQuantity>(LEVEL_PARAM + c, 0.f, 1.f, 1.f, c < ROWS ? string::f("Channel %d", c + 1) : std::string("Master"), "%", 0.f, 100.f);
			configSwitch(MUTE_PARAM + c, 0.f, 1.f, 0.f, c < ROWS ? string::f("Channel %d mute", c + 1) : std::string("Master mute"), {"Off", "On"});
			if (c < ROWS)
				configSwitch(SOLO_PARAM + c, 0.f, 1.f, 0.f, string::f("Channel %d solo", c + 1), {"Off", "On"});
		}
		configChoice(this, GATHER_PARAM, true);
		configInput(IN_INPUT, "Channels");
		configOutput(MIX_OUTPUT, "Mix");
		retipAll();
	}
	/** Every tooltip as the last wake left me. */
	void retipAll() override {
		const int rows = relaxed(srcNNow);
		const bool jack = inputs[IN_INPUT].isConnected();
		const bool cables = relaxed(stackedNow);
		// IN: a cable's channels on faders of their own, stacked or not, and
		// in place of whatever is on the faders while something is.
		const char* const STACKED = "Each channel gets its own fader, on stacked cables too.";
		const std::string in = jack && cables ? std::string("Each channel of every stacked cable gets its own fader.")
		                       : jack         ? std::string(STACKED)
		                       : rows > 0     ? "A cable here takes the place of " + askedWords() + " on the faders. " + STACKED
		                                      : std::string("Each channel of a cable here gets its own fader, on stacked cables too.");
		// What a cable's channels past the faders lose is said on IN too,
		// where the cable is, while it happens.
		const std::string lost = jack ? droppedWords("fader") : std::string();
		retipIn(this, IN_INPUT, {"Channels", (lost.empty() ? in : in + " " + lost).c_str()});
		// OUT: silent with nothing to mix, from nothing to its left or from a
		// strip whose choice nothing free to its left answers (taken by a
		// gatherer further left, or not there); otherwise only what was left
		// out, the one thing about a sum a player could not guess.
		std::string out = droppedWords("fader");
		if (!jack && rows == 0) {
			out = silentFor(Choice::of(choiceCode()), "mix in");
		}
		retipOut(this, MIX_OUTPUT, {"Mix", out.c_str()});
		retipChoice();
		const bool fromJack = relaxed(fromJackNow);
		const uint64_t kinds = relaxed(kindsNow);
		for (int c = 0; c < ROWS; c++) {
			if (c >= rows) {
				retipParam(this, LEVEL_PARAM + c, {string::f("Channel %d", c + 1).c_str(), GATHER_EMPTY_ROW, Gestures{}});
				continue;
			}
			// A grey row, a fed CV taken off its channel, says so, as a
			// Poly's does (`GATHER_GREY_ROW`).
			const std::string lesson = std::string(muteHolds(c) ? MIX_MUTED_LESSON : MIX_CHANNEL_LESSON) +
			                           (!fromJack && relaxed(rgbNow[c]) == REPLACED_RGB ? std::string(" ") + GATHER_GREY_ROW : std::string());
			retipParam(this, LEVEL_PARAM + c, {rowFrom(c, cables, fromJack, kinds).c_str(), lesson.c_str(), channelGestures(muted(c), soloed(c))});
		}
		retipParam(this, SUM_PARAM, {"Master", muted(ROWS) ? MIX_MUTED_LESSON : MIX_MASTER_LESSON, masterGestures(muted(ROWS), rows > 0)});
	}

	/** Randomize rolls the levels, never a mute or a solo, the switches
	standing after every level. */
	bool rolled(int p) override { return p < MUTE_PARAM; }

	enum { S_READER };
	ROW_SLOT_IS(M_MIX, S_READER, "reader");
	ROW_MANIFEST(M_MIX)
	float goneSum = 0.f;
	int goneN = -1;
	bool published() override {
		bool any = Member::published();
		if (sum != goneSum || srcN != goneN) {
			goneSum = sum;
			goneN = srcN;
			any = true;
		}
		return any;
	}
	/** My sum as one channel of a gatherer to my right: a Poly bundles Mixes,
	and a Mix sums them. */
	int channelsOut(float* v) override {
		v[0] = sum;
		return 1;
	}
	void evaluate(const ProcessArgs& args) override {
		readSources();
		const bool solo = soloing(srcN);
		float s = 0.f;
		for (int c = 0; c < srcN; c++)
			s += srcV[c] * levelPlayed(c, solo);
		sum = sane(s * levelPlayed(ROWS, solo));
		outputs[MIX_OUTPUT].setChannels(1);
		outputs[MIX_OUTPUT].setVoltage(sum);
		markRead();
	}
	/** The bars are a Poly's peak meter, and the sum's bar is the last. */
	float meterLevel(int c, int k) { return c == ROWS ? sum : c < k ? srcV[c] : 0.f; }
	bool carries() { return srcN > 0; }

};

std::string LevelQuantity::getUnit() {
	Mix* m = dynamic_cast<Mix*>(module);
	const int c = paramId - Mix::LEVEL_PARAM;
	if (!m)
		return ParamQuantity::getUnit();
	const int rows = relaxed(m->srcNNow);
	const bool solo = c < std::min(rows, ROWS) && m->soloing(rows);
	const char* state = !solo         ? (m->muted(c) ? " (muted)" : "")
	                    : m->soloed(c) ? (m->muted(c) ? " (soloed, muted once unsoloed)" : " (soloed)")
	                                   : " (silenced by solo)";
	return ParamQuantity::getUnit() + state;
}

/** The body: a Poly's rows, each in its source's colour and as thick as
its level, a line at nothing, faint, still showing its source; and the
sum in the last row, white, across the same width. A row the switches
silence (`Mix::open`), muted or left out by a solo, is not drawn at all, a
gap, so a mute is told from a level at nothing; while a solo stands each
soloed row has a triangle at either side of the glass pointing in at it, in
its colour, in the margin beside the bars. */
struct MixScreen : Screen {
	Mix* module = nullptr;
	void drawLayer(const DrawArgs& args, int layer) override {
		Screen::drawLayer(args, layer);
		if (layer != 1)
			return;
		NVGcontext* vg = args.vg;
		int n = 3;
		uint64_t cols = 0;
		// Each bar's span, below nought and above it (`HeldSpan`).
		float foot[ROWS + 1] = {0.f, -2.f, 0.f}, top[ROWS + 1] = {1.f, 0.f, 3.f};
		top[ROWS] = 2.f;
		if (module) {
			cols = relaxed(module->colorsNow);
			n = relaxed(module->header) & 0xff;
			for (int c = 0; c <= ROWS; c++)
				unpackSpan(relaxed(module->shown[c]), foot[c], top[c]);
		}
		const GatherRows r(box.size, stripShown(module));
		const float w = box.size.x - 2 * r.inset;
		const bool solo = module && module->soloing(n);
		// A bar as thick as its level, faint at nothing, and none at all
		// while the switches silence it; a soloed row pointed at from
		// both sides.
		auto bar = [&](int c, NVGcolor cc) {
			if (module && !module->open(c, solo))
				return;
			const float y = r.y(c) + (r.pitch - r.h) / 2.f;
			const float lv = module ? module->level(c) : 1.f;
			const float t = std::max(LINE_PX, r.h * lv);
			drawSpanBar(vg, r.inset, y + (r.h - t) / 2.f, w, t, foot[c] / 10.f, top[c] / 10.f,
			            lv > 0.f ? cc : color::alpha(cc, GATHER_FAINT));
			if (solo && c < ROWS) {
				const float cy = y + r.h / 2.f;
				paintArrow(vg, r.inset / 2.f, cy, ARROW_RIGHT, cc);
				paintArrow(vg, box.size.x - r.inset / 2.f, cy, ARROW_LEFT, cc);
			}
		};
		for (int c = 0; c < ROWS; c++) {
			if (c >= n) {
				paintEmptyRow(vg, r.inset, r.y(c) + (r.pitch - r.h) / 2.f, w, r.h);
				continue;
			}
			bar(c, gatheredRowColor(module ? relaxed(module->rgbNow[c]) : 0, cols, c));
		}
		// The sum is there whether or not anything is summed yet: an empty Mix
		// still has its master fader.
		bar(ROWS, gatheredColor(0));
	}
};

/** A row of the body as a fader: a drag up or down sets its source's level in
the sum a per cent a pixel, from where it stands and folding at either end
(`VerticalDragParam`), one undo a stroke, and a double click puts it back to
all of it; the pointer over it outlines the row and lights its cable
(`GatherRow`), and its hover names where it came from and its level. A row with nothing in it is not there for the hand:
no outline, no hover, no stroke. A click with shift held mutes, or unmutes, the row's own switch
with its level kept, one undo, painted over every row a shift stroke
crosses; the press begins no stroke. A click with command held solos, or
unsolos, a channel, and its hold scans (`scanTo`); on the sum's row it
clears every mute and solo. A stroke on a muted row takes it out
of its mute at nothing once the pointer strays, and a double click at all
of it, each one undo with the mute. The sum's row is one of these on the sum's level, and always
there, an empty Mix's master fader as much as a full one's. */
struct MixRow : GatherRow<Mix, VerticalDragParam> {
	MixRow() { pxPerUnit = 1.f; }
	/** A per cent a pixel, with no fine stroke: command on a row is a
	solo's or the master's clearing, and a level that must be exact is
	typed. */
	float unit() override { return 0.01f; }
	/** What this row's press began, the rows a stroke crosses asking the
	row it began on: a stroke on the level, a mute painted
	(`paintBegin`), a solo and its scan (`scanTo`), or the master's
	clearing, done at the press. Anything but a stroke moves no level. */
	enum Press { STROKE, MUTE, SOLO, OPEN };
	Press press = STROKE;
	/** A solo's hold has left the row it was pressed on: from then on the
	row under the pointer is the one soloed. */
	bool scanned = false;
	/** A stroke begun with command and shift held moves every input row
	together, as ganged faders, each by the same per cent a pixel a fader
	steps, from its level as it stands: one at the top or at nothing stops
	there on its own while the rest go on, until every one is at that end,
	where the stroke keeps no travel (`landStep`'s fold), so a stroke to an
	end flattens the mix and from there they move as one. Begun on the
	sum's row it is the same, the sum's own level left where it stands.
	The whole stroke is one undo. */
	bool scaling = false;
	int scaleN = 0;
	/** A stroke whose undo is more than this row's level: a scale, or a
	stroke or a double click that took a muted row out of its mute. */
	bool ownStroke = false;
	GestureChanges own;
	Mix* mix() { return dynamic_cast<Mix*>(module); }
	bool mutedNow() {
		Mix* m = mix();
		return m && m->muteHolds(index);
	}
	/** A muted row taken out of its mute by the hand, from nothing: a
	stroke on it starts at nothing as if it had always stood there, and a
	double click is its default, all of it, heard.
	Noted first, so the stroke's one undo puts back the level and the mute
	alike. */
	void unmuteFromNothing(bool keepLevel) {
		Mix* m = mix();
		if (!m)
			return;
		ownStroke = true;
		own.note(m, Mix::LEVEL_PARAM + index);
		own.note(m, Mix::MUTE_PARAM + index);
		if (!keepLevel)
			m->paramQuantities[Mix::LEVEL_PARAM + index]->setValue(0.f);
		m->paramQuantities[Mix::MUTE_PARAM + index]->setValue(0.f);
		wakeMix();
	}
	bool live() override {
		Mix* m = dynamic_cast<Mix*>(module);
		const int n = m ? relaxed(m->srcNNow) : 0;
		return m && (index < n || index == ROWS);
	}
	void wakeMix() {
		if (Mix* m = dynamic_cast<Mix*>(module))
			m->wake();
	}
	void onButton(const ButtonEvent& e) override {
		if (!live())
			return;
		// Asked again at the press alone: a release may reach the row before
		// the stroke's end, which must still know the stroke was a mute.
		const bool left = e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS;
		if (left)
			press = STROKE;
		Mix* m = mix();
		const bool channel = index < ROWS;
		if (m && left && Hand::in(G_MIX_MUTE, e.mods)) {
			// A mute, painted over every row the stroke crosses: the row's
			// own switch, its level kept, so a second click gives the
			// level back.
			press = MUTE;
			paintBegin(this, m, Mix::MUTE_PARAM + index, m->muted(index) ? 0.f : 1.f);
		}
		else if (m && left && channel && Hand::in(G_MIX_SOLO, e.mods)) {
			// The row's solo flipped, every solo noted for the hold's one
			// undo, in case it becomes a scan.
			press = SOLO;
			scanned = false;
			own.was.clear();
			for (int c = 0; c < ROWS; c++)
				own.note(m, Mix::SOLO_PARAM + c);
			m->paramQuantities[Mix::SOLO_PARAM + index]->setValue(m->soloed(index) ? 0.f : 1.f);
		}
		else if (m && left && !channel && Hand::in(G_MIX_OPEN, e.mods)) {
			press = OPEN;
			openAll(m);
		}
		if (press != STROKE) {
			wakeMix();
			e.consume(this);
			return;
		}
		VerticalDragParam::onButton(e);
	}
	/** The master's clearing: every channel's mute and solo and the mix's
	own mute off, the levels where they stand, one undo. */
	void openAll(Mix* m) {
		GestureChanges g;
		for (int p = Mix::MUTE_PARAM; p < Mix::SOLO_PARAM + ROWS; p++) {
			g.note(m, p);
			m->paramQuantities[p]->setValue(0.f);
		}
		g.push("clear mutes and solos");
	}
	/** A solo's hold reaching channel `row`: once the pointer has left the
	row it was pressed on, the row under it is the one soloed, the others
	not, so one hold hears each channel it crosses in turn and the last
	stays soloed. Rack's first move of a drag enters the row it began on,
	which changes nothing until the pointer has been elsewhere. */
	void scanTo(int row) {
		Mix* m = mix();
		if (!m || (row == index && !scanned))
			return;
		scanned = true;
		for (int c = 0; c < ROWS; c++)
			m->paramQuantities[Mix::SOLO_PARAM + c]->setValue(c == row ? 1.f : 0.f);
		wakeMix();
	}
	void onDragStart(const DragStartEvent& e) override {
		if (press != STROKE)
			return;
		scaling = false;
		ownStroke = false;
		own.was.clear();
		Mix* m = mix();
		if (m && e.button == GLFW_MOUSE_BUTTON_LEFT && Hand::among(G_MIX_ALL) && live()) {
			// A muted row is scaled as it stands, its level kept under its
			// mute, so unmuting it later gives its share back.
			scaling = ownStroke = true;
			scaleN = clamp(relaxed(m->srcNNow), 0, ROWS);
			for (int c = 0; c < scaleN; c++)
				own.note(m, Mix::LEVEL_PARAM + c);
		}
		VerticalDragParam::onDragStart(e);
		if (!scaling && e.button == GLFW_MOUSE_BUTTON_LEFT)
			say();
	}
	/** The level a stroke has made, on the bracket's hint line while the fader
	is dragged, named as its tooltip names it ("CV 2: 37%"): the bar's
	thickness shows it only roughly. A scale moves every row and says
	nothing. */
	void say() {
		if (engine::ParamQuantity* pq = getParamQuantity())
			Hand::readout(this, pq->getLabel() + ": " + pq->getDisplayValueString() + pq->getUnit());
	}
	void onDragEnd(const DragEndEvent& e) override {
		if (press == MUTE)
			paintEnd("mute rows");
		if (press == SOLO && e.button == GLFW_MOUSE_BUTTON_LEFT)
			own.push("solo channels");
		if (press != STROKE)
			return;
		if (!ownStroke) {
			VerticalDragParam::onDragEnd(e);
			return;
		}
		// The stroke's own end, its undo the rows it scaled, or the level and
		// the mute, rather than the one level (`VerticalDragParam::onDragEnd`
		// records that).
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		const bool wasScale = scaling, wasReset = resetting;
		scaling = ownStroke = resetting = false;
		if (settings::knobMode <= settings::KNOB_MODE_LINEAR)
			APP->window->cursorUnlock();
		onDragActive(false);
		own.push(wasReset ? "reset channels" : wasScale ? "scale rows" : "change " + getParamQuantity()->getLabel());
		ParamWidget::onDragEnd(e);
	}
	/** A move of a scaling stroke (`scaling`). */
	void scaleMove(const DragMoveEvent& e) {
		Mix* m = dynamic_cast<Mix*>(module);
		if (!m || e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		dragAccum += strokeDelta(e);
		moved = moved || strayed();
		const float took = stepsTaken();
		if (took == 0.f)
			return;
		// Each row a step on its own grid, as its own fader's stroke lands
		// (`VerticalDragParam::strokeMove`), and the travel kept only while
		// some row can still go the way the stroke goes.
		const float u = unit(), end = took < 0.f ? 1.f : 0.f;
		bool atEnd = true;
		for (int c = 0; c < scaleN; c++) {
			const float v = clamp(std::round((m->level(c) - took * u) / u) * u, 0.f, 1.f);
			m->paramQuantities[Mix::LEVEL_PARAM + c]->setValue(v);
			atEnd = atEnd && v == end;
		}
		if (atEnd)
			dragAccum = 0.f;
		wakeMix();
	}
	void onDragEnter(const DragEnterEvent& e) override {
		GatherRow::onDragEnter(e);
		// A mute painted from the sum's row stops there, as one from a
		// channel's never reaches the sum: the master and the channels are
		// muted apart. A solo's hold scans.
		MixRow* from = dynamic_cast<MixRow*>(e.origin);
		if (!live() || index >= ROWS || !from || from->index == ROWS)
			return;
		if (from->press == MUTE && paintOver(e.origin, module, Mix::MUTE_PARAM + index))
			wakeMix();
		if (from->press == SOLO)
			from->scanTo(index);
	}
	void onDoubleClick(const DoubleClickEvent& e) override {
		if (!live())
			return;
		if (scaling && Hand::among(G_MIX_RESET_ALL)) {
			resetAll();
			e.consume(this);
			return;
		}
		if (mutedNow() && !scaling)
			unmuteFromNothing(true);
		VerticalDragParam::onDoubleClick(e);
		wakeMix();
	}
	/** Every channel's own double click at once, from a double click with
	shift and command held: each level at its default, all of it, and each
	mute off, as one row's double click takes it out of its mute; the solos
	and the sum's own level left. Rack delivers it after the second press has
	begun its scaling stroke, which noted every level, so the stroke's end
	pushes the one undo. */
	bool resetting = false;
	void resetAll() {
		Mix* m = mix();
		if (!m)
			return;
		resetting = spent = true; // no tremor scales the reset away
		for (int c = 0; c < scaleN; c++) {
			own.note(m, Mix::MUTE_PARAM + c);
			engine::ParamQuantity* pq = m->paramQuantities[Mix::LEVEL_PARAM + c];
			pq->setValue(pq->getDefaultValue());
			m->paramQuantities[Mix::MUTE_PARAM + c]->setValue(0.f);
		}
		wakeMix();
	}
	void strokeMove(const DragMoveEvent& e) override {
		if (press != STROKE)
			return;
		if (scaling) {
			scaleMove(e);
			return;
		}
		if (mutedNow() && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			// A muted rig is taken out of its mute at nothing once the
			// pointer has strayed past a click's wobble, a click alone
			// changing nothing; the travel so far stays in the accumulator
			// and the next move steps from nothing.
			dragAccum += strokeDelta(e);
			moved = moved || strayed();
			if (strayed()) {
				unmuteFromNothing(false);
				say();
			}
			return;
		}
		VerticalDragParam::strokeMove(e);
		say();
		wakeMix();
	}
	/** A stroke on a level holds the cursor still on its row, which keeps
	the pointer till it ends (`GatherRow`). */
	void onDragActive(bool active) override { stroking = active; }
};

struct MixWidget : GatherWidget<Mix, MixScreen> {
	MixWidget(Mix* module) : GatherWidget(module, "MIX") {
		// The sum's row is the last, its level the param after the rows'.
		addRows<MixRow>(ROWS + 1, Mix::LEVEL_PARAM);
		close(Mix::MIX_OUTPUT);
	}
};

} // namespace

Model* modelMix = createModel<Mix, MixWidget>("MIX");
