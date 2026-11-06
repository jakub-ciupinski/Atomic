// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Panel.hpp"
#include "HeadBase.hpp"

/** Qnt, the quantizer: a keyboard, a piano roll or a volt divider whose lit
keys snap the notes to its right. */
using namespace rig;

namespace {

/** A key's tooltip: while a stroke with a modifier is going, what the
stroke has made; else the note under the pointer, lit or not, its strokes on
the hint line (`keyGestures`, a roll's `rollGestures`).
`key` is the cell's, since on a roll a cell stands for the roll's switch where
it is on the piano (`keyParamIn`) and names that note. A roll's own switches
are keys too, `key` minus one, so the cell standing for one says the roll's
strokes on the bracket, where a roll's rows would otherwise say nothing, its
switches having no hint to carry. */
struct KeyQuantity : WithHint<SwitchQuantity> {
	int key = -1;
	std::string getString() override;
};
/** A key says its note and nothing more: its strokes are on the hint line,
and a roll's cells take no tooltip, so nothing stands over the editing. */
const char* const KEY_LESSON = "";
const char* const ROLL_KEY_LESSON = "";
/** A QNT standing down in a block, CVs to its right: its keys shape only its
own OUT. */
const char* const KEY_STOOD_DOWN_LESSON = "Only its own OUT: the rightmost QNT shapes the CVs.";
/** The transpose's gestures (Words.hpp), a keyboard's and a roll's alike.
The head screen has no caption, so the first piece names the transpose.
With command a stroke or a double click takes
every QNT touching it, said only where one touches (`peersUi`): alone the
chord reaches nothing past the part. */
inline const Gesture G_QNT_OCTAVES = declare(On::Drag, Mod::Shift, Rank::Known, RACK_MOD_SHIFT_NAME "-drag: transpose by octaves");
inline const Gesture G_QNT_EVERY = declare(On::Drag, Mod::Command, Rank::Block, RACK_MOD_CTRL_NAME "-drag: transpose touching QNTs");
inline const Gesture G_QNT_RESET = declare(On::Double, Mod::Command, Rank::Block, RACK_MOD_CTRL_NAME "-double-click: reset touching QNTs");
/** A key's strokes and a roll's, read by the shaper (QntShaper.hpp) and
`RollOrient`: the same chords, a keyboard's words or a roll's, the octaves
one job and one piece on both, as a TRG's triggers. In a block the
octaves are every touching QNT's (`blockOf`), and the piece says so
(`G_KEY_OCTAVES_BLOCK`), the one the hand reads being the lone one's. */
inline const Gesture G_KEY_OCTAVES = declare(On::Drag, Mod::Shift, Rank::Known, RACK_MOD_SHIFT_NAME "-drag: add or remove octaves");
inline const Gesture G_KEY_OCTAVES_BLOCK =
    declare(On::Drag, Mod::Shift, Rank::Block, RACK_MOD_SHIFT_NAME "-drag: add or remove octaves on touching QNTs");
inline const Gesture G_KEY_ROTATE = declare(On::Drag, Mod::Command, Rank::Invented, RACK_MOD_CTRL_NAME "-drag: rotate the scale");
inline const Gesture G_KEY_DETUNE =
    declare(On::Drag, Mod::ShiftCommand, Rank::Rare, RACK_MOD_SHIFT_NAME "+" RACK_MOD_CTRL_NAME "-drag: detune this key");
inline const Gesture G_ROLL_MOVE = declare(On::Drag, Mod::Command, Rank::Invented, RACK_MOD_CTRL_NAME "-drag: move the notes");
inline const Gesture G_ROLL_SCROLL =
    declare(On::Drag, Mod::ShiftCommand, Rank::Rare, RACK_MOD_SHIFT_NAME "+" RACK_MOD_CTRL_NAME "-drag: scroll the roll");
/** A plain drag from a key or a row paints every one it crosses, across
the Qnts of the run, with what the press made of the first (`CellPaint`). */
inline const Gesture G_KEY_PAINT = declare(On::Drag, Mod::None, Rank::Known, "Drag across: light or clear the keys");
inline const Gesture& keyOctaves(bool block) { return block ? G_KEY_OCTAVES_BLOCK : G_KEY_OCTAVES; }
inline Gestures keyGestures(bool block) { return {&G_KEY_ROTATE, &keyOctaves(block), &G_KEY_PAINT, &G_KEY_DETUNE}; }
inline Gestures rollGestures(bool block) { return {&G_ROLL_MOVE, &keyOctaves(block), &G_KEY_PAINT, &G_ROLL_SCROLL}; }
/** What the keys' screen says off its keys, over a walked keyboard's
playhead strip and the margins, where a stroke with a modifier reshapes as
on a key (`ShapeCatch`) and a key's own detune and paint do not reach. */
inline Gestures keyScreenGestures(bool block) { return {&G_KEY_ROTATE, &keyOctaves(block)}; }
inline Gestures rollScreenGestures(bool block) { return {&G_ROLL_MOVE, &keyOctaves(block), &G_ROLL_SCROLL}; }
/** The transpose's surprise, by how it counts: by degrees the notes move
along the scale and the keys stay as set; by semitones the keys are renamed
with it (`movedBy`); a roll's notes sound moved and stay drawn where they are
set. The CVs to its right are said only where one stands there
(`cvsRightUi`), the clock pilot's rule for naming a neighbour, and never on a
QNT with another touching it on the right, which stands down for it
(Cartridge.hpp, the unwalked block). */
const char* const TRANSPOSE_DEGREE_LESSON = "Moves every note along the scale; the keys stay as set.";
const char* const TRANSPOSE_SEMITONE_LESSON = "Moves every note and renames the keys with it.";
const char* const ROLL_TRANSPOSE_LESSON = "Moves every note it sounds; the keys stay drawn as set.";
const char* const TRANSPOSE_CVS = " The CVs to its right move with it.";
/** The divider's count: at nought the value says unquantized and OUT's
name says the rest, so nothing more; under the harmonic law the count is of
harmonics (`DivQuantity`), and the lesson says where the series stands. */
const char* const DIV_LESSON = "Splits 10 V into this many parts.";
const char* const DIV_HARMONIC_LESSON = "The harmonic series from C4, the fundamental.";
/** The divider's face, its law and bend, all one stroke: up toward log and
on past full log to the harmonics, said at once on the hint line; its double
click is Rack's reset, equal and straight, and says nothing. */
inline const Gesture G_BEND_DRAG = declare(On::Drag, Mod::None, Rank::Invented, "Drag: spacing, up to log, harmonics");
inline const Gesture G_BEND_NOTCH =
    declare(On::Drag, Mod::Shift, Rank::Known, RACK_MOD_SHIFT_NAME "-drag: snap exp, log, harmonics");
inline const Gesture G_BEND_FINER = declare(On::Drag, Mod::Command, Rank::Known, RACK_MOD_CTRL_NAME "-drag: fine");
inline Gestures bendGestures() { return {&G_BEND_DRAG, &G_BEND_NOTCH, &G_BEND_FINER}; }
const char* const BEND_LESSON = "";
const char* const BEND_HARMONIC_LESSON = "The harmonics take no bend.";
/** The ring's name and its two values say all it does; its click is said on
the hint line, as every toggle's is (`G_TOGGLE`). */
const char* const DEGREE_LESSON = "";
/** A quantity on the volt divider's face, which says itself by the law the
divider stands under, the param `lawParam`. */
template <typename Base>
struct LawQuantity : Base {
	int lawParam = -1;
	bool harmonic() { return this->module && lawParam >= 0 && this->module->params[lawParam].getValue() >= 0.5f; }
};
/** The volt divider's parts, which at nought are no parts at all but the
volts passed as they come, and say so; under the harmonic law they are
harmonics, and say that. */
struct DivQuantity : LawQuantity<ParamQuantity> {
	bool none() { return (int)std::round(getValue()) == 0; }
	std::string getDisplayValueString() override {
		return none() ? std::string(rig::UNQUANTIZED_NAME) : ParamQuantity::getDisplayValueString();
	}
	std::string getUnit() override {
		if (none())
			return std::string();
		const bool one = (int)std::round(getValue()) == 1;
		return harmonic() ? (one ? " harmonic" : " harmonics") : one ? " part" : ParamQuantity::getUnit();
	}
};

/** The divider's bend, which names the law with it: the tooltip of the
divider's face, its spacing, says what the face is, equal volts, or bent so
far toward log or exp, the parts no longer equal, or the harmonic series,
which no bend touches. Named "Spacing", not the head's "Volt divider" again. */
struct BendQuantity : LawQuantity<HintQuantity> {
	std::string getDisplayValueString() override {
		if (harmonic())
			return "harmonic series";
		const int b = (int)std::round(getValue());
		return b == 0 ? "equal volts" : string::f("%d%% toward %s", b < 0 ? -b : b, b > 0 ? "log" : "exp");
	}
	std::string getUnit() override { return std::string(); }
};

struct Qnt : Cartridge, QuantFace, Probe {
	MEMBER_FOOTPRINT
	enum ParamId {
		MODE_PARAM,  // the mode, set by the title's arrows: 1 the keyboard, -1 the roll, 0 the volt divider
		TRANSPOSE_PARAM, // the transpose, a keyboard's or a roll's, applied before quantizing
		DIV_PARAM,      // the volt divider's parts: nought is unquantized
		DEGREE_PARAM,   // a keyboard's transpose in degrees of the pattern, its default, rather than semitones
		ENUMS(KEY_PARAM, 12 * 10),      // the keyboard's keys, ten octaves from its lowest
		ENUMS(ROLL_KEY_PARAM, 12 * 10), // the roll's own keys, a semitone each from C-1 up
		ROLLTOP_PARAM, // the roll's C9, the one note past the other hundred and twenty
		DIVLAW_PARAM,   // the volt divider's law: nought equal volts, one the harmonic series
		DIVBEND_PARAM,  // the bend of its equal parts, hundredths of the full bend: plus toward log, nought straight
		ENUMS(DETUNE_PARAM, 12 * 10), // the keyboard's keys' detunes, whole cents, by key as set
		ROLL_PARAM,  // where its roll stands on the piano, in semitones
		KEYS_OCT_PARAM, // the keyboard's octaves, set by its shift stroke
		ROLL_OCT_PARAM, // the roll's, likewise
		EXPANDED_PARAM, // whether the keyboard has ever shown more than its first octave
		PARAMS_LEN
	};
	enum InputId {
		IN_INPUT, // standing alone, the pitch this Qnt quantizes
		INPUTS_LEN
	};
	enum OutputId {
		CV_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	Scale scale;          // the scale in force, which the rig snaps to

	// What the screens read: worked out by `prepare` into the plain fields
	// beside each, which the engine reads, and stored into the atomics at the
	// end of the wake with the lit keys, so a frame never draws the new
	// keyboard under the old lights.
	int octavesWork = 1, transposeWork = 0, rollShiftWork = 0;
	bool rollWork = false;
	std::atomic<int> octavesNow{1};      // octaves the keyboard spans
	std::atomic<bool> rollNow{false};    // the keys are the roll's rather than the keyboard's
	std::atomic<int> rollShiftNow{0};    // semitones the roll is moved along the piano
	ScaleAtomic scaleNow;                // the scale in force
	// The volt divider's bands lit, packed as a key's playheads are
	// (`packHead`), every reader sounding a band in its colour, so two Seqs
	// on one band cut it in two as they cut a key: what the readers lit and
	// what my own cable sounds, merged as they are published
	// (`publishReads`). A band my own cable lit, and no reader, carries
	// `OWN_CABLE`, and the screen paints it in the cable's colour, which only
	// the UI knows, as a key does (`headColor`).
	// Two cells on the head screen, beside the mode glyphs: decimal to 99 and
	// hex past it to FF (`drawCount`). The scale's ten bits hold it (Pitch.hpp).
	static const int MAX_DIVS = 255;
	static constexpr uint32_t OWN_CABLE = 1u << 31; // above every packed head: lit by my own cable, no reader
	std::atomic<uint32_t> bandHeadsNow[MAX_DIVS] = {};
	uint32_t bandHeadsWork[MAX_DIVS] = {};
	uint32_t bandHeadsShown[MAX_DIVS] = {};
	int litBands[MAX_DIVS] = {};
	int nlitBands = 0;
	// The bands published lit, exactly those with `bandHeadsShown` not
	// nought, so a wake publishes only those and the ones lit now rather than
	// walking all of them, which was half a driven Qnt's cost.
	int16_t shownBands[MAX_DIVS] = {};
	int nShownBands = 0;
	int ownBands[PORT_MAX_CHANNELS];
	int nOwnBands = 0;
	uint32_t bandPacked(int band) const { return band >= 0 && band < MAX_DIVS ? relaxed(bandHeadsNow[band]) : 0u; }
	int bandHeadsOf(int band, int* cols) const { return unpackHeads(bandPacked(band) & ~OWN_CABLE, cols); }
	bool bandOwn(int band) const { return (bandPacked(band) & OWN_CABLE) != 0; }
	int bandShown(int band) const {
		int cols[MAX_HEADS];
		return bandHeadsOf(band, cols) > 0 ? cols[0] + 1 : 0;
	}
	int bandLit(int band) override { return bandShown(band); }
	int groupOut() override { return relaxed(groupNow); }
	int bandHeads(int band, int* cols) override { return bandHeadsOf(band, cols); }
	// And by the wake's last pass.
	std::atomic<bool> claimedNow{false}; // a reader walks the block I stand in, which is when a member not stood on dims
	// The group the divider's count wears (`faceColor`), minus one for none:
	// a reader walking my block's colour; else the colour of the reader
	// sounding what I modify, a Seq walking the CVs whose window I shape,
	// which lights me in its colour as it lights a key, so a Qnt between two
	// Seqs' blocks wears the one it modifies for; else my own hue while a
	// member uses my window or lights me unwalked, as a key lit by a fader
	// wears it; else none, and the scheme goes on to the cable and to white.
	std::atomic<int> groupNow{-1};
	// The first reader's colour this wake that lit me through what I
	// modify, minus one lit only by a member nobody walks, minus two unlit.
	int litByWork = -2;
	void litBy(int color) {
		if (color >= 0 ? litByWork < 0 : litByWork == -2)
			litByWork = color >= 0 ? color : -1;
	}
	// What only the UI knows, handed over in the widget's step: the cable in
	// my jack's colour, and whether I stand in a bond.
	std::atomic<uint32_t> inRGB{0};
	std::atomic<bool> bondedUi{false};
	// Whether a CV stands to my right in my lane, as my widget saw the run,
	// for the transpose's lesson to name them only where they are.
	std::atomic<bool> cvsRightUi{false};
	// And whether a QNT of my block to my right shapes them instead, I
	// standing down, for my keys' lesson.
	std::atomic<bool> stoodDownUi{false};
	// Whether another QNT touches me either side, as my widget saw the run
	// (`QntShaper::blockOf`), for the hints to name the block's reach only
	// where there is one.
	std::atomic<bool> peersUi{false};
	/** The colour the divider's count wears, by the scheme (`schemeColor`),
	as a Clock's tempo and a CV's box wear theirs:
	the colour I am read in while the rig reads me, yellow in the first block,
	else the cable in my IN, else white bonded with nothing reading me and no
	cable leaving, orphaned, and yellow alone. Under the DEV's OLD, white as
	before. */
	NVGcolor faceColor() {
		if (dev().oldColors)
			return color::WHITE;
		return schemeColor(relaxed(groupNow), relaxed(inRGB), relaxed(bondedUi),
		                    outputs[CV_OUTPUT].isConnected());
	}
	// The UI's alone.
	bool orientHeld = false; // a stroke reshaping the roll is going: its Cs are named
	// What the hand saw of me the frame before (`QntWidget::keepOneMode`):
	// the block I stood in, by its leftmost Qnt's id, none yet below nought,
	// and the mode I was in, none yet at 2.
	int64_t stoodIn = -1;
	int modeSeen = 2;

	// A key a slot, and one more for a roll's note past its period (`TOP_KEY`).
	static const int TOP_KEY = 12 * MAX_OCTAVES;
	std::atomic<uint8_t> keyColor[TOP_KEY + 1] = {}; // per key: colour + 1
	uint8_t keyColorWork[TOP_KEY + 1] = {};          // the same, this wake, before it is published
	// The playheads on each key: every reader sounding it, in its colour, up
	// to four, packed as a count in the low four bits and a colour in each
	// four above (`packHead`), so several Seqs on one key each show.
	static const int MAX_HEADS = 4;
	std::atomic<uint32_t> keyHeadsNow[TOP_KEY + 1] = {};
	uint32_t keyHeadsWork[TOP_KEY + 1] = {};
	// The keys lit this wake, so the next clears only those; and what each
	// key's two atomics were last told, so a key that has not changed is not
	// stored again, rather than clearing and storing all of them every sample
	// under a moving cable.
	uint8_t litKeys[TOP_KEY + 1] = {};
	int nlitKeys = 0;
	uint8_t keyColorShown[TOP_KEY + 1] = {};
	uint32_t keyHeadsShown[TOP_KEY + 1] = {};
	// The keys shown lit, so a wake publishes those, the lit and my own,
	// rather than every key of ten octaves, as the bands do.
	uint8_t shownKeys[TOP_KEY + 1] = {};
	int nShownKeys = 0;
	static uint32_t packHead(uint32_t p, int col) {
		const int n = p & 0xf;
		for (int i = 0; i < n; i++)
			if ((int)((p >> (4 + 4 * i)) & 0xf) == col)
				return p;
		if (n >= MAX_HEADS)
			return p;
		return ((p & ~0xfu) | (uint32_t)(n + 1)) | (uint32_t)col << (4 + 4 * n);
	}
	static int unpackHeads(uint32_t p, int* cols) {
		const int n = p & 0xf;
		for (int i = 0; i < n; i++)
			cols[i] = (int)((p >> (4 + 4 * i)) & 0xf);
		return n;
	}

	static int keyParam(int key) { return KEY_PARAM + key; }
	static const int MAX_CENTS = 99;
	/** A key's detune, whole cents from the one place it is read, set by a
	command stroke on the key. */
	int detuneOf(int key) {
		if (key < 0 || key >= 12 * MAX_OCTAVES)
			return 0;
		return clamp((int)std::round(params[DETUNE_PARAM + key].getValue()), -MAX_CENTS, MAX_CENTS);
	}
	/** Whether a keyboard's transpose is in semitones, moving the keys, rather
	than in degrees of the pattern, its default: the ring after the head's count turned off. */
	bool chromaticNow() { return params[DEGREE_PARAM].getValue() < 0.5f; }
	/** How far the transpose may go, which no stroke on it passes: the
	piano's reach either way, and by degrees the notes of the pattern within
	it (`degreeReach`), so the count stored is the count sounding. A roll's is
	in semitones, the whole reach. Asked on the UI thread, of what the engine
	published. */
	void transposeSpan(int& lo, int& hi) {
		lo = -TRANSPOSE_MAX;
		hi = TRANSPOSE_MAX;
		const Scale sc = scaleNow.load();
		if (relaxed(rollNow) || chromaticNow() || !sc.any() || sc.spread)
			return;
		lo = degreeReach(sc, -TRANSPOSE_MAX, TRANSPOSE_MAX);
		hi = degreeReach(sc, TRANSPOSE_MAX, TRANSPOSE_MAX);
	}
	/** What one shifted step of the transpose moves by, so that it moves
	every note by the period: an octave of semitones on a roll or a keyboard in semitones,
	and by degrees the pattern's notes in its period (`notesInPeriod`), which
	the degree-octave relation holds to be the period. One where a pattern
	has no notes, whose transpose has nowhere to go. */
	int transposePeriod() {
		if (relaxed(rollNow) || chromaticNow())
			return 12;
		return std::max(1, notesInPeriod(scaleNow.load()));
	}
	/** The transpose's count in the unit it is in, for whoever names it. */
	struct TransposeQuantity : HintQuantity {
		std::string getUnit() override {
			Qnt* q = dynamic_cast<Qnt*>(module);
			// A roll has no pattern to count along: always semitones.
			const bool semis = q && (relaxed(q->rollNow) || q->chromaticNow());
			const int t = (int)std::round(getValue());
			return std::string(semis ? " semitone" : " degree") + (std::abs(t) == 1 ? "" : "s");
		}
	};
	/** The roll's switches by where they stand rather than by key: one a
	semitone from C-1 up, the sixty under C4 first, since a roll grows
	downward as well as up. */
	static const int ROLL_BELOW = 60;
	/** How far a keyboard's transpose goes either way: the piano's own
	reach from C4, as far as a roll stands below it. */
	static const int TRANSPOSE_MAX = ROLL_BELOW;
	static int rollParam(int semis) {
		if (semis >= ROLL_BELOW)
			return ROLLTOP_PARAM;
		return ROLL_KEY_PARAM + semis + ROLL_BELOW;
	}
	/** The octaves a roll of so many spans, around the one C4 is in: the
	second goes under it, the third over, the fourth under again, so a
	chord written in one octave stays in the middle as the roll grows. How
	many octaves stand under C4. */
	static int rollBelow(int octaves) { return octaves / 2; }
	/** Where a key of the roll's pattern stands, in semitones from C4: the
	pattern is one period from C4 up, repeating, and the roll shows that
	period from its lowest octave, so the keys past the top of the roll are
	the ones under C4. */
	static int rollSemis(int key, int octaves, int shift = 0) {
		const int base = rollBase(octaves, shift), p = 12 * octaves;
		return base + eucMod(key - base, p);
	}
	/** The roll's lowest note, in semitones from C4: under C4 by the octaves
	that go under it, moved along the piano by the roll's own shift. */
	static int rollBase(int octaves, int shift) { return -12 * rollBelow(octaves) + shift; }
	/** How far the offset may move a roll of so many octaves: as far as
	keeps the whole of it on the piano, C-1 to B8, which is every switch the
	roll has. Ten octaves is the whole piano and does not move at all. */
	static int rollShiftIn(int shift, int octaves) {
		const int below = 12 * rollBelow(octaves);
		return clamp(shift, -ROLL_BELOW + below, ROLL_BELOW - 12 * octaves + below);
	}
	/** The switch behind a key of what is showing: the keyboard's, each
	octave its own, or the roll's where that key stands. Never one octave's
	twelve repeated, which would take away the point of giving it two. */
	static int keyParamIn(int key, bool roll, int octaves, int shift = 0) {
		if (key >= 12 * MAX_OCTAVES)
			return rollParam(rollBase(octaves, shift) + 12 * octaves);
		return roll ? rollParam(rollSemis(key, octaves, shift)) : keyParam(key);
	}
	/** The keys the table sweeps: those of the octaves showing, a
	keyboard's twelve an octave or a roll's semitones in its window with its
	top C, and every param that is not a key. The rest stand unwatched until
	the octaves or the shift move, when the set is made again
	(`Member::watches`); a key outside the window is read nowhere. */
	int watchedOct = -1, watchedShift = 0;
	bool watchedRoll = false;
	std::vector<bool> watchedKeys; // by param index, the keys in the window
	static bool isKeyParam(int p) {
		// built once under the language's guard, since two rigs' hosts may ask at once
		static const std::vector<bool> keys = [] {
			std::vector<bool> k(PARAMS_LEN, false);
			for (int i = 0; i < 12 * MAX_OCTAVES; i++)
				k[keyParam(i)] = true;
			for (int st = -ROLL_BELOW; st <= ROLL_BELOW; st++)
				k[rollParam(st)] = true;
			for (int i = 0; i < 12 * MAX_OCTAVES; i++)
				k[DETUNE_PARAM + i] = true; // a detune is a key's, watched with it
			return k;
		}();
		return p >= 0 && p < (int)keys.size() && keys[p];
	}
	void watchSet(int octaves, bool roll, int shift) {
		watchedOct = octaves;
		watchedRoll = roll;
		watchedShift = shift;
		watchedKeys.assign(PARAMS_LEN, false);
		const int oct = std::max(octaves, 1);
		for (int k = 0; k < 12 * oct; k++) {
			watchedKeys[keyParamIn(k, roll, oct, shift)] = true;
			if (!roll)
				watchedKeys[DETUNE_PARAM + k] = true;
		}
		if (roll)
			watchedKeys[keyParamIn(12 * MAX_OCTAVES, roll, oct, shift)] = true;
	}
	/** Randomize rolls the keys and nothing that says where they stand or how
	they are read: the mode, the octaves, the roll, the transpose, the
	divider, the detunes. */
	bool rolled(int p) override {
		return isKeyParam(p) && !(p >= DETUNE_PARAM && p < DETUNE_PARAM + 12 * MAX_OCTAVES);
	}
	bool watches(int p) override {
		if (!isKeyParam(p))
			return true;
		const int mode = modeNow(), octaves = octavesOf(mode);
		const bool roll = mode < 0;
		const int shift = roll ? rollShiftIn((int)std::round(params[ROLL_PARAM].getValue()), octaves) : 0;
		if (octaves != watchedOct || roll != watchedRoll || shift != watchedShift)
			watchSet(octaves, roll, shift);
		return watchedKeys[p];
	}
	/** Whether a keyboard is showing its playhead lane: a Seq walks the block
	I stand in and I am a keyboard, not a roll. The white keys then stand back
	to the black keys' width and leave the middle of the screen dark for the
	playheads. */
	bool showsLane() const {
		return relaxed(claimedNow) && !relaxed(rollNow) && relaxed(octavesNow) > 0;
	}
	/** Whether it is showing a roll, rather than a keyboard or nothing. */
	bool showsRoll() const {
		return relaxed(rollNow) && relaxed(octavesNow) > 0;
	}
	/** Whether it is showing the volt divider, which has no octaves. */
	bool showsDivider() const {
		return relaxed(octavesNow) == 0;
	}

	/** Whether the keyboard has ever shown more than its first octave: the
	first time its octaves open it wider, the octaves above take the first
	octave's pattern, so a scale set on a lone Qnt carries on up rather than
	meeting the white keys; after that each octave is its own. A hidden
	param, `EXPANDED_PARAM`, so the stroke that opens it records it with the
	copied keys in its one undo. */
	bool expandedNow() { return params[EXPANDED_PARAM].getValue() > 0.5f; }
	/** The mode a mode arrow comes to, as writes handed to `set`:
	1 the keyboard, -1 the roll, 0 the volt divider.
	Never a wipe: each mode keeps its own keys and its own octaves for when it
	comes back, and only the arrows change it, the shift stroke never crossing
	from one to another. */
	template <typename F> void modeWith(int mode, F set) {
		if (mode > 0 && octavesOf(1) > 1 && !expandedNow())
			openKeyboardWith(set);
		set(MODE_PARAM, (float)mode);
	}
	/** How far the scale in force stands moved from the keys as set, for
	whatever draws them as set: a keyboard's transpose in semitones, its
	`base`, and a roll's, its foot past where the roll stands on the piano.
	A roll's transpose sounds its notes moved and leaves them drawn where
	they are set, as a MIDI transpose over a piano roll does. */
	int movedBy(const Scale& s) {
		return s.spread ? s.base - rollBase(s.octaves, relaxed(rollShiftNow)) : s.base;
	}
	/** The mode in force, as `modeWith` takes it, an integer from the one
	place it is read. */
	int modeNow() { return clamp((int)std::round(params[MODE_PARAM].getValue()), -1, 1); }
	/** The octaves a mode spans, its own param's: one to ten for the
	keyboard and the roll, nought for the divider, which has none. */
	int octavesOf(int mode) {
		return mode == 0 ? 0 : clamp((int)std::round(params[mode > 0 ? KEYS_OCT_PARAM : ROLL_OCT_PARAM].getValue()), 1, MAX_OCTAVES);
	}
	template <typename F> void openKeyboardWith(F set) {
		set(EXPANDED_PARAM, 1.f);
		for (int k = 12; k < 12 * MAX_OCTAVES; k++)
			set(keyParam(k), params[keyParam(k % 12)].getValue());
	}

	Qnt() : Cartridge(manifest::M_QNT) {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int k = 0; k < 12 * MAX_OCTAVES; k++)
			configSwitch<KeyQuantity>(keyParam(k), 0.f, 1.f, isBlackKey(k) ? 0.f : 1.f, noteName(k),
			                          {"Out of scale", "In scale"})->key = k;
		// The roll's top C is a cell of the keyboard's too, the one past
		// its keys (`TOP_KEY`).
		for (int st = -ROLL_BELOW; st <= ROLL_BELOW; st++)
			if (rollParam(st) == ROLLTOP_PARAM)
				configSwitch<KeyQuantity>(ROLLTOP_PARAM, 0.f, 1.f, 0.f, noteName(st), {"Out of scale", "In scale"})->key = TOP_KEY;
			else
				configSwitch<KeyQuantity>(rollParam(st), 0.f, 1.f, 0.f, noteName(st), {"Out of scale", "In scale"});
		// The mode, the title's arrows' alone, and each mode's octaves, set
		// by a stroke with shift held (`QntShaper`): a keyboard of one octave
		// and a roll of four, the keyboard first.
		configSwitch(MODE_PARAM, -1.f, 1.f, 1.f, "Variant", {"Piano roll", "Volt divider", "Keyboard"});
		configSwitch(EXPANDED_PARAM, 0.f, 1.f, 0.f, "Keyboard shown", {"No", "Yes"});
		configParam(KEYS_OCT_PARAM, 1.f, (float)MAX_OCTAVES, 1.f, "Keyboard octaves");
		configParam(ROLL_OCT_PARAM, 1.f, (float)MAX_OCTAVES, 4.f, "Piano roll octaves");
		for (int p : {KEYS_OCT_PARAM, ROLL_OCT_PARAM})
			paramQuantities[p]->snapEnabled = true;
		configParam(ROLL_PARAM, -60.f, 60.f, 0.f, "Roll position", " semitones");
		paramQuantities[ROLL_PARAM]->snapEnabled = true;
		// A keyboard's transpose, set by a stroke on the head screen: the
		// signal moved before it is snapped and the keys renamed with it, so
		// the whole of what comes out moves by the semitones.
		// By degrees of the pattern unless the menu says semitones: every note moved that many of the
		// pattern's notes along it, the keys staying as set, and a count past
		// the reach of the semitones held at the last note within them
		// (`degreeReach`).
		configParam<TransposeQuantity>(TRANSPOSE_PARAM, (float)-TRANSPOSE_MAX, (float)TRANSPOSE_MAX, 0.f, "Transpose");
		paramQuantities[TRANSPOSE_PARAM]->snapEnabled = true;
		// The volt divider, the third mode: ten volts in so many equal parts,
		// nought parts unquantized and one the whole ten volts one band. A
		// fresh divider stands at nought, unquantized.
		configParam<DivQuantity>(DIV_PARAM, 0.f, (float)MAX_DIVS, 0.f, "Volt divider", " parts")->lawParam = DIVLAW_PARAM;
		retipParam(this, DIV_PARAM, {"Volt divider", ""});
		paramQuantities[DIV_PARAM]->snapEnabled = true;
		// Its law and its bend, both set on its face: equal volts straight,
		// fresh.
		configSwitch(DIVLAW_PARAM, 0.f, 1.f, 0.f, "Divider law", {"Equal volts", "Harmonic series"});
		BendQuantity* bq = configParam<BendQuantity>(DIVBEND_PARAM, (float)-rig::DIV_BEND_MAX, (float)rig::DIV_BEND_MAX, 0.f, "Spacing");
		bq->lawParam = DIVLAW_PARAM;
		bq->snapEnabled = true;
		retipParam(this, DIVBEND_PARAM, {"Spacing", BEND_LESSON, bendGestures()});
		// The detunes: a key of the keyboard detuned up to 99 cents either
		// way by a command stroke on it.
		for (int k = 0; k < 12 * MAX_OCTAVES; k++) {
			configParam(DETUNE_PARAM + k, (float)-MAX_CENTS, (float)MAX_CENTS, 0.f, "Detune " + noteName(k), " cents");
			paramQuantities[DETUNE_PARAM + k]->snapEnabled = true;
		}
		// Off, the transpose is in semitones.
		configSwitch<WithHint<SwitchQuantity>>(DEGREE_PARAM, 0.f, 1.f, 1.f, "Transpose by", {"Semitones", "Degrees"});
		retipParam(this, DEGREE_PARAM, {"Transpose by", DEGREE_LESSON, Gestures{&G_TOGGLE}});
		configInput(IN_INPUT, "Voltage");
		cv.bind(inputs[IN_INPUT]);
		configOutput(CV_OUTPUT, "Quantized");
	}


	/** The lessons of the keys and the transpose, which a roll and a
	keyboard tell apart, rewritten only when the one turns into the other or
	a QNT comes to touch me or leaves; the transpose's also when its count
	turns between degrees and semitones or a CV comes to stand to my right
	or leaves. */
	int keysTipped = -1, transposeTipped = -1;
	void retipKeys() {
		const int roll = relaxed(rollNow) ? 1 : 0;
		const int peers = relaxed(peersUi) ? 1 : 0;
		const int down = relaxed(stoodDownUi) ? 1 : 0;
		if ((down * 2 + roll) * 2 + peers != keysTipped) {
			keysTipped = (down * 2 + roll) * 2 + peers;
			for (int p = 0; p < PARAMS_LEN; p++)
				if (dynamic_cast<KeyQuantity*>(paramQuantities[p]))
					retipParam(this, p, {paramQuantities[p]->name.c_str(), down ? KEY_STOOD_DOWN_LESSON : roll ? ROLL_KEY_LESSON : KEY_LESSON,
					                     roll ? rollGestures(peers) : keyGestures(peers)});
		}
		const int count = roll ? 2 : chromaticNow() ? 1 : 0;
		const int cvs = relaxed(cvsRightUi) ? 1 : 0;
		if ((count * 2 + cvs) * 2 + peers == transposeTipped)
			return;
		transposeTipped = (count * 2 + cvs) * 2 + peers;
		const std::string lesson = std::string(count == 2 ? ROLL_TRANSPOSE_LESSON : count == 1 ? TRANSPOSE_SEMITONE_LESSON : TRANSPOSE_DEGREE_LESSON) +
		                           (cvs ? TRANSPOSE_CVS : "");
		retipParam(this, TRANSPOSE_PARAM, {"Transpose", lesson.c_str(),
		                                   peers ? Gestures{&G_QNT_EVERY, &G_QNT_RESET, &G_QNT_OCTAVES} : Gestures{&G_QNT_OCTAVES}});
	}
	/** The divider's count and face say which law they go by, rewritten only
	when the law turns over, so a lesson held back under a resting pointer
	(`LEARN_AFTER`) is not written over every frame. */
	int lawTipped = -1;
	void retipLaw() {
		const int harmonics = params[DIVLAW_PARAM].getValue() >= 0.5f ? 1 : 0;
		const int none = (int)std::round(params[DIV_PARAM].getValue()) == 0 ? 1 : 0;
		if (harmonics * 2 + none == lawTipped)
			return;
		lawTipped = harmonics * 2 + none;
		retipParam(this, DIV_PARAM, {"Volt divider", none ? "" : harmonics ? DIV_HARMONIC_LESSON : DIV_LESSON});
		retipParam(this, DIVBEND_PARAM, {"Spacing", harmonics ? BEND_HARMONIC_LESSON : BEND_LESSON, bendGestures()});
	}
	/** With a pitch in its jack the output is that pitch snapped; with none
	it is the lit keys themselves, a chord. */
	void retipAll() override {
		retipKeys();
		const bool snapping = jackLiveNow();
		// With no key lit it is a bypass, which a cable
		// passes as it is and an empty chord gives as 0 V.
		const Scale now = scaleNow.load();
		const bool empty = !now.any() || relaxed(octavesNow) == 0;
		retipLaw();
		// IN says what OUT makes of it, the chord said only while it is
		// empty; OUT says what it gives, with no unit on it and no
		// channel count, a chord being one. The divider at nought has
		// no keys to name and passes IN as it is. A divider takes the
		// nearest line toward 0 V where the keys take the nearest key
		// (`dividerSnap`), so its IN says which.
		const bool none = relaxed(octavesNow) == 0 && !now.divides();
		const std::string lines = "Snapped at OUT to " + dividerSnap(false) + ", the parts of 10 V repeating past either end.";
		const std::string linesChord = lines + " Empty, OUT gives the lines as a chord.";
		const char* inLesson = none ? "Passed to OUT as it is."
		                       : now.divides() && now.harmonic
		                           ? (snapping ? "Snapped at OUT to the nearest harmonic toward 0 V, held at the top one."
		                                       : "Snapped at OUT to the nearest harmonic toward 0 V, held at the top one. Empty, OUT gives them as a chord.")
		                       : now.divides()
		                           ? (snapping ? lines.c_str() : linesChord.c_str())
		                       : empty ? "With no key lit, passed to OUT as it is."
		                       : snapping ? "Snapped at OUT to the nearest lit key."
		                                  : "Snapped at OUT to the nearest lit key. Empty, OUT gives the lit keys as a chord.";
		retipIn(this, IN_INPUT, {"Voltage", inLesson});
		if (none) {
			retipOut(this, CV_OUTPUT, {"Unquantized", snapping ? "" : "Silent: IN is empty."});
			return;
		}
		if (now.divides()) {
			const int shown = std::min((int)now.divs, NOTES);
			const std::string desc = now.harmonic ? (snapping ? string::f("IN snapped to the first %d harmonics.", (int)now.divs)
			                                                  : string::f("The first %d harmonics as a chord.", shown))
			                         : snapping ? string::f("IN snapped to the %d parts.", (int)now.divs)
			                                    : string::f("The first %d lines as a chord.", shown);
			retipOut(this, CV_OUTPUT, {"Divided", desc.c_str()});
			return;
		}
		// A chord is a channel a note and a cable carries sixteen, so more
		// lit keys than that are named as the lowest of them, as the
		// divider names its first lines.
		const int lit = now.count();
		const std::string chord = lit > NOTES ? string::f("The lowest %d of the %d lit keys as a chord.", NOTES, lit)
		                                      : std::string("The lit keys as a chord.");
		retipOut(this, CV_OUTPUT, {empty ? "Unquantized" : snapping ? "Quantized" : "Chord",
		         snapping ? (empty ? "No key lit: passes IN as it is." : "IN snapped to the nearest lit key.")
		                  : (empty ? "No key lit and IN empty: silent." : chord.c_str())});
	}

	/** The colour a key is sounding in: a playhead's, or my own signal's
	where the key is the one I light myself. False while it is silent. */
	int keyLit(int key) override { return key >= 0 && key <= TOP_KEY ? (int)relaxed(keyColor[key]) : 0; }
	uint32_t keyPacked(int key) const { return key >= 0 && key <= TOP_KEY ? relaxed(keyHeadsNow[key]) : 0u; }
	int keyHeadsOf(int key, int* cols) const { return unpackHeads(keyPacked(key) & ~OWN_CABLE, cols); }
	bool keyOwn(int key) const { return (keyPacked(key) & OWN_CABLE) != 0; }
	/** The colour a key or a band lit in `col` is painted in: the cable in my
	IN's where my own cable lit it and no reader did, as a member wears what
	arrived through its jack, else the palette's. Under the DEV's OLD, the
	palette's always. */
	NVGcolor headColor(bool own, int col) const {
		const uint32_t cable = relaxed(inRGB);
		return own && cable && !dev().oldColors ? fromRGB(cable) : playColor(col);
	}
	int keyHeads(int key, int* cols) override { return keyHeadsOf(key, cols); }
	int dimmedOut() override { return relaxed(claimedNow) && !liveNow() ? 1 : 0; }
	bool soundingColor(int key, int& col) const {
		const int kc = (int)relaxed(keyColor[key]) - 1;
		if (kc < 0)
			return false;
		col = kc;
		return true;
	}

	/** The keys as set, over so many octaves:
	the roll's own, or the keyboard's twelve in every octave. None at all
	is no scale, which a Qnt passes everything through untouched. */
	Scale readScale(int octaves, bool roll, int shift = 0) {
		Scale m;
		m.octaves = (int8_t)octaves;
		for (int k = 0; k < m.period(); k++)
			m.set(k, params[keyParamIn(k, roll, octaves, shift)].getValue() > 0.5f);
		// A roll closes its top octave: the note one period over its foot.
		if (roll)
			m.extra = params[rollParam(rollBase(octaves, shift) + m.period())].getValue() > 0.5f;
		return m;
	}

	/** A keyboard's octaves opened past one without a stroke, a patch
	loaded or a param set from outside: the engine asks the widget (or a
	headless host) to carry the first octave's pattern up, once, and reads the params as
	they are until it is done. A stroke does it itself, on the hand's side,
	inside its undo (`QntShaper`), and so does a mode arrow (`modeWith`). */
	bool postedOpen = false; // the opening the engine has asked for and not yet seen
	void takeOpening(int octaves, bool roll) {
		if (!roll && octaves > 1 && !expandedNow() && !postedOpen) {
			postedOpen = true;
			openKeyboardWith([this](int p, float v) { postWrite(p, v, "open the keyboard"); });
		}
		if (expandedNow())
			postedOpen = false;
	}
	/** The pattern's notes from its lowest up, into `notes`, as places from
	the foot of its period `p`: a keyboard's keys from C round its octaves, a
	roll's places in its window, its top C last. How many. */
	static int patternNotes(const Scale& raw, bool roll, int lo, int p, int* notes) {
		int n = 0;
		for (int at = 0; at < p; at++)
			if (raw.has(roll ? lo + at : at))
				notes[n++] = at;
		if (roll && raw.extra)
			notes[n++] = p;
		return n;
	}

	/** The scale in force: the keys as set, a keyboard's moved by its transpose,
	a roll's where it stands on the piano. */
	void prepare(const Mapping& win, const ProcessArgs& args) override {
		// All of it is my settings' and nothing else's, so a wake that only a
		// jack caused keeps the scale it built rather than building it every
		// sample under a moving cable.
		if (!settingsMoved)
			return;
		settingsMoved = false;
		// The mode and its octaves are my own: the keyboard, each octave its
		// own pattern; the roll, every key its own; the divider no keys at
		// all.
		const int mode = modeNow(), octaves = octavesOf(mode), oct = std::max(octaves, 1), p = 12 * oct;
		const bool roll = mode < 0;
		takeOpening(octaves, roll);
		// A roll's window is where it stands on the piano, its own to set;
		// its notes are the whole of what it quantizes to, the travel shared
		// among them (`Slots::windowNotes`).
		const int shift = roll ? rollShiftIn((int)std::round(params[ROLL_PARAM].getValue()), octaves) : 0;
		// The keys in play moved: the table is built again with them.
		if (octaves != watchedOct || roll != watchedRoll || shift != watchedShift)
			bumpWiring();
		const int lo = roll ? rollBase(oct, shift) : 0;
		const Scale raw = readScale(oct, roll, shift);
		// A keyboard's transpose, an integer from the one place it is read:
		// in semitones moving the keys, or in degrees moving the notes along
		// them, its default.
		// A roll's transpose is in semitones whatever the menu says: its
		// notes are each their own, and a degree of them is no step at all.
		const int t = clamp((int)std::round(params[TRANSPOSE_PARAM].getValue()), -TRANSPOSE_MAX, TRANSPOSE_MAX);
		const bool chromatic = roll || chromaticNow();
		const int tr = chromatic ? t : 0;
		int notes[12 * MAX_OCTAVES + 1];
		const int total = patternNotes(raw, roll, lo, p, notes);
		Scale now;
		if (roll) {
			// The keys as set are the roll's notes already, each at its
			// pitch round the period and its top C as `extra`
			// (`rollSemis`): the roll stands where it is on the piano, and
			// sounds moved by its transpose, the switches untouched, so the
			// roll shows its notes as set as a keyboard does and the head
			// screen says by how much they sound moved.
			now.octaves = (int8_t) oct;
			now.spread = true;
			now.base = (int8_t)(lo + tr);
			for (int k = 0; k < p; k++)
				if (raw.has(lo + k))
					now.set(eucMod(lo + k + tr, p), true);
			now.extra = raw.extra;
		}
		else {
			now.octaves = (int8_t) oct;
			// The scale in force is the keys as set moved by the transpose:
			// every note, its chord and its first note from C4 moved by the
			// semitones, so a pitch moved by them before the snap comes out
			// moved by them after it. The keyboard draws the keys as set and
			// names them by the transpose, read back off `base`.
			for (int i = 0; i < total; i++) {
				now.set(eucMod(notes[i] + tr, p), true);
				// Each key's cents go with it to where the transpose puts it.
				const int c = detuneOf(notes[i]);
				now.cents[eucMod(notes[i] + tr, p)] = (int8_t) c;
				now.detuned = now.detuned || c != 0;
			}
			now.base = (int8_t) tr;
			// By degrees the keys stay as set and the count moves the notes,
			// as far as the root reaches within the transpose's semitones.
			if (!chromatic)
				now.degrees = (int8_t) degreeReach(now, t, TRANSPOSE_MAX);
		}
		// No note is no quantizer; and the volt divider, which has no
		// octaves, quantizes by its parts, an integer from the one place it
		// is read, nought parts unquantized.
		if (total == 0 || mode == 0)
			now = Scale();
		if (mode == 0) {
			now.divs = (int16_t)clamp((int)std::round(params[DIV_PARAM].getValue()), 0, MAX_DIVS);
			now.harmonic = params[DIVLAW_PARAM].getValue() >= 0.5f;
			now.bend = (int8_t) clamp((int)std::round(params[DIVBEND_PARAM].getValue()), -rig::DIV_BEND_MAX, rig::DIV_BEND_MAX);
		}
		rollShiftWork = shift;
		transposeWork = tr;
		octavesWork = octaves;
		rollWork = roll;
		scale = now;
	}
	/** The window: the scale is mine, and the reach and shift arriving pass
	through, so an Atn before me shapes what stands past me as it shapes what
	stands before. With no note picked I end the scale and nothing else: an
	Atn's offset standing before me in another Qnt's scope was a shift after
	the snap, in semitones, and with no snap past me it is the same interval
	in volts. The Atn's window, its offset with it, reaches to the next Atn
	whatever Qnt stands between, and a Qnt replaces only the scale,
	so QNT ATN QNT CV with the last Qnt empty keeps the Atn's offset
	(relation atn-offset-reaches). */
	void shape(Mapping& m) override {
		// A lane is my mode, not me: taking the lane of
		// the block of CVs before, I keep its range where its mode is mine, a
		// keyboard laying the Atn that set it again on my own keys, as though
		// it stood in front of me; of another mode I start afresh, the full
		// range.
		const int mode = laneModeNow();
		if (!at(sWindow) && windowSource()) {
			if (m.laneMode != mode)
				m = Mapping();
			else if (mode == 1 && m.atnNode) {
				Mapping again;
				m.atnNode->shapeAfresh(again);
				m = again;
			}
		}
		m.laneMode = (int8_t) mode;
		if (!scale.any() && m.postShift != 0) {
			m.preShift += (float)m.postShift / 12.f;
			m.postShift = 0;
		}
		// An Atn's offset in notes in front of me lands on my pattern, the
		// interval nearest what it asked for, as it would after me; a roll
		// moves a semitone a step, and an empty Qnt leaves it for the next.
		if (scale.any()) {
			if (!scale.spread)
				m.preShift += (float)(shiftOnScale(m.landSemis, scale) - m.landSemis) / 12.f;
			m.landSemis = 0;
		}
		m.scale = scale;
		m.quantNode = this;
		// The CVs quantized through me are white while nothing sequences me,
		// and a Seq walking me gives its colour to the window it hands on
		// (`Cartridge::sound`), so a step's box says where its quantization
		// came from.
		m.quantColor = -1;
	}
	// A cable in my own jack is mine to quantize and changes nothing I hand
	// to my right: I shape the window whatever is plugged in, and only what
	// holds me decides what I am to it. The band `act` snapped by, handed to `sound` within the same
	// call.
	int bandActed = NO_BAND;
	int noteActedWork = 0; // the whole semitone `act` snapped to, its cents aside
	float act(Signal& s, int color, int* value) override {
		if (s.present && transposeWork != 0 && scale.any())
			s.set(s.semis + (float)transposeWork, s.color);
		const float out = quantStage(s, scale, value, &bandActed, &noteActedWork);
		if (scale.divides())
			noteActedWork = (int)std::round(out);
		if (value && *value >= 0)
			*value = keyRow(noteActedWork);
		return out;
	}
	int noteActed(float semis) override { return noteActedWork; }
	/** The key that sounds a pitch, as the screen draws it: the keys stand
	as set, a keyboard's and a roll's alike, so a transposed note is the key it
	was moved from, a roll's top C its own slot. */
	int keyRow(int semis) const {
		if (scale.spread && scale.extra && semis == scale.base + scale.period())
			return TOP_KEY;
		return scale.keyOf(semis - transposeWork);
	}
	bool windowIn(Mapping& win) override {
		win = winIn;
		return true;
	}

	// A keyboard is started afresh by turning it from keys to a roll and
	// back; no Trg clears it.
	enum { S_LANE = 2 };
	ROW_SLOT_IS(M_QNT, S_LANE, "lane");
	/** Whose window I lay my scale on: my window slot's, the shaper before
	me in my gap; and where it finds nothing and a block of CVs holds my
	gap (`TOLD_LANE_Q`), the lane of the block of CVs before that, which
	`shape` keeps or starts afresh by its mode. */
	Member* windowSource() override {
		if (Member* w = at(sWindow))
			return w;
		return (told() & tree::TOLD_LANE_Q) ? at(S_LANE) : nullptr;
	}
	/** My mode, as a lane counts it (`Mapping::laneMode`): 1 a keyboard, 2 a
	roll, 3 a divider, 0 unquantized. */
	int laneModeNow() {
		const int mode = modeNow();
		return mode > 0 ? 1 : mode < 0 ? 2 : (int)std::round(params[DIV_PARAM].getValue()) > 0 ? 3 : 0;
	}
	void parsed(const tree::Left& left, int pass) override {
		Cartridge::parsed(left, pass);
		if (pass == 0)
			countHue(left);
	}
	/** My own colour: one more than the Qnts to my left in the rig, so a
	line of quantizers is a line of colours, and the CVs quantized through me
	wear it too. Counted along the expanders, which is what the rig is. In a
	block a playhead walks, the playhead's colour instead, the first lane's,
	since the members of a sequence are one thing. */
	int hue = 0;
	/** My block's own colour, and nothing else: a Seq walking the block
	lights the notes it sounds in its own colour, key by key, and the block
	keeps its colour under them, so the two can be told apart, and the CVs
	quantized through it wear the block's. */
	int restColor() const override { return hue; }
	/** The colour of the block I stand in: one colour a touching block of
	Qnts, counted in blocks along the rig over the parse, so five Qnts
	touching are one colour and the CVs after them wear it, not a fifth from
	nowhere. */
	void countHue(const tree::Left& left) {
		int blocks = 0;
		bool inBlock = false;
		for (int i = 0; i < left.size(); i++) {
			const bool qnt = (left[i]->traits & tree::T_QNT) != 0;
			if (qnt && !inBlock)
				blocks++;
			inBlock = qnt;
		}
		hue = (inBlock ? blocks - 1 : blocks) % MAX_COLORS; // touching a Qnt on my left, I am in its block
	}
	QuantFace* quantFace() override { return this; }
	/** The one sounding set. What a reader lights is cleared at the top of
	every wake and rewritten by the reader, which is at every wake for it;
	what I light for myself (`ownWork`, my own cable snapped, a key a channel) is my own evaluate's and kept apart, since a wake that clears
	the readers' lights may leave me asleep, and the two are merged as they
	are published, a reader's colour over my own. */
	void light(int semis, int color) override {
		if (scale.divides()) {
			lightVolts((float)semis / 12.f, color);
			return;
		}
		written();
		litBy(color); // unquantized too: what I modify is read, whatever it lands on
		if (!scale.holds(semis))
			return; // not a note of mine: it passed through, and no key sounds it
		const int row = keyRow(semis), col = clamp(color < 0 ? restColor() : color, 0, MAX_COLORS - 1);
		if (keyColorWork[row] == 0 && keyHeadsWork[row] == 0)
			litKeys[nlitKeys++] = (uint8_t)row;
		keyColorWork[row] = (uint8_t)(col + 1);
		keyHeadsWork[row] = packHead(keyHeadsWork[row], col);
	}
	/** A divider's band, lit for the voltage it snaps: the band it stands in,
	where the screen shows it (`Divider::shown`), wrapped every ten volts and
	mirrored below nought. */
	void lightVolts(float volts, int color) override {
		if (!scale.divides()) {
			light((int)std::round(volts * 12.f), color);
			return;
		}
		lightBand(Divider::of(scale).band(volts), color);
	}
	/** A band by its own integer, where the screen shows it. */
	void lightBand(int band, int color) override {
		if (!scale.divides() || band == NO_BAND)
			return;
		written();
		litBy(color);
		const int b = Divider::of(scale).shown(band);
		if (b < 0 || b >= MAX_DIVS)
			return;
		const int col = clamp(color < 0 ? restColor() : color, 0, MAX_COLORS - 1);
		if (bandHeadsWork[b] == 0)
			litBands[nlitBands++] = b;
		bandHeadsWork[b] = packHead(bandHeadsWork[b], col);
	}
	int ownWork[PORT_MAX_CHANNELS]; // the keys I sound myself, a channel each
	int nOwn = 0;
	bool ownKey(int k) const {
		for (int i = 0; i < nOwn; i++)
			if (ownWork[i] == k)
				return true;
		return false;
	}
	void clearReads() override {
		Cartridge::clearReads();
		for (int i = 0; i < nlitKeys; i++) {
			keyColorWork[litKeys[i]] = 0;
			keyHeadsWork[litKeys[i]] = 0;
		}
		nlitKeys = 0;
		for (int i = 0; i < nlitBands; i++)
			bandHeadsWork[litBands[i]] = 0;
		nlitBands = 0;
		litByWork = -2;
	}
	void publishReads() override {
		Cartridge::publishReads();
		octavesNow.store(octavesWork, std::memory_order_relaxed);
		rollNow.store(rollWork, std::memory_order_relaxed);
		rollShiftNow.store(rollShiftWork, std::memory_order_relaxed);
		scaleNow.store(scale);
		// Only a key shown lit, lit by a reader or sounded by my own cable
		// can change, as with the bands below: those shown first, keeping
		// the ones still lit, then the ones lit now joining the list.
		const auto publishKey = [&](int k) {
			const bool own = keyColorWork[k] == 0 && ownKey(k);
			const int ownCol = colorWork >= 0 ? colorWork : hue;
			// My own note in the signal's colour, else my block's: one rule with `light`.
			const uint8_t c = own ? (uint8_t)(ownCol + 1) : keyColorWork[k];
			const uint32_t h = own ? packHead(0, clamp(ownCol, 0, MAX_COLORS - 1)) | OWN_CABLE : keyHeadsWork[k];
			if (c != keyColorShown[k]) {
				keyColorShown[k] = c;
				keyColor[k].store(c, std::memory_order_relaxed);
			}
			if (h != keyHeadsShown[k]) {
				keyHeadsShown[k] = h;
				keyHeadsNow[k].store(h, std::memory_order_relaxed);
			}
			return c != 0 || h != 0;
		};
		int keptKeys = 0;
		for (int j = 0; j < nShownKeys; j++)
			if (publishKey(shownKeys[j]))
				shownKeys[keptKeys++] = shownKeys[j];
		nShownKeys = keptKeys;
		const auto joinKey = [&](int k) {
			if (k < 0 || k > TOP_KEY || keyColorShown[k] != 0 || keyHeadsShown[k] != 0)
				return; // off the keyboard, or listed and published already
			if (publishKey(k))
				shownKeys[nShownKeys++] = (uint8_t)k;
		};
		for (int i = 0; i < nlitKeys; i++)
			joinKey(litKeys[i]);
		for (int i = 0; i < nOwn; i++)
			joinKey(ownWork[i]);
		// The divider's bands, my own cable's in its colour under a reader's.
		const int ownCol = clamp(colorWork >= 0 ? colorWork : hue, 0, MAX_COLORS - 1);
		// Only a band shown lit, lit by a reader or by my own cable can
		// change. Those shown first, keeping the ones still lit; then the
		// ones lit now, of which those not shown yet join the list, never
		// twice, since a band lit now is never dark once published.
		const auto publish = [&](int b) {
			uint32_t h = bandHeadsWork[b];
			if (h == 0)
				for (int i = 0; i < nOwnBands; i++)
					if (ownBands[i] == b)
						h = packHead(0, ownCol) | OWN_CABLE;
			if (h != bandHeadsShown[b]) {
				bandHeadsShown[b] = h;
				bandHeadsNow[b].store(h, std::memory_order_relaxed);
			}
			return h;
		};
		int kept = 0;
		for (int j = 0; j < nShownBands; j++)
			if (publish(shownBands[j]) != 0)
				shownBands[kept++] = shownBands[j];
		nShownBands = kept;
		const auto join = [&](int b) {
			if (b < 0 || b >= MAX_DIVS || bandHeadsShown[b] != 0)
				return; // off the screen, or listed and published already
			publish(b);
			shownBands[nShownBands++] = (int16_t)b;
		};
		for (int i = 0; i < nlitBands; i++)
			join(litBands[i]);
		for (int i = 0; i < nOwnBands; i++)
			join(ownBands[i]);
		claimedNow.store(claimedByReader(), std::memory_order_relaxed);
		const bool read = claimedByReader() || usedNow() || litByWork == -1;
		groupNow.store(colorRead >= 0 ? colorRead : litByWork >= 0 ? litByWork : read ? std::max(shownColorWork, 0) : -1,
		               std::memory_order_relaxed);
	}
	/** A pitch snapped as my own cable's is: passed as it came with no
	note lit, else to the nearest note of the pattern. */
	/** The same in volts: a divider's line as the line itself, never by way
	of semitones, so a line in is that line out at any count (twelve times a
	line over twelve is a float off it at seven parts). */
	float snappedVolts(float v) const {
		return scale.divides()                  ? Divider::of(scale).snap(v)
		       : scale.any() && !scale.spread ? scale.tunedVolts(snappedNote(v))
		                                      : snapped(v) / 12.f;
	}
	float snapped(float v) const {
		if (scale.divides())
			return 12.f * Divider::of(scale).snap(v);
		return !scale.any()   ? v * 12.f
		       : scale.spread ? (float)nearestSpread(scale, v * 12.f + (float)transposeWork)
		                      : scale.tunedSemis(degreeMoved(scale, nearestInScale(v * 12.f + (float)transposeWork, scale)));
	}
	/** The whole semitone a pitch is snapped to, its cents aside, for the
	key it lights. */
	int snappedNote(float v) const {
		return !scale.any()   ? (int)std::round(v * 12.f)
		       : scale.spread ? nearestSpread(scale, v * 12.f + (float)transposeWork)
		                      : degreeMoved(scale, nearestInScale(v * 12.f + (float)transposeWork, scale));
	}
	void publishJack() override {
		const bool ownPitch = ownCable();
		nOwn = 0;
		nOwnBands = 0;
		if (!ownPitch) {
			jackWidth = writeChord(outputs[CV_OUTPUT], scale);
			routedJack();
			return;
		}
		// A polyphonic cable is quantized a channel each, as many out as came
		// in; fed by a Poly, one.
		const int n = cv.plugged() ? std::max(inputs[IN_INPUT].getChannels(), 1) : 1;
		outputs[CV_OUTPUT].setChannels(n);
		for (int c = 0; c < n; c++) {
			const float in = c == 0 ? cv.get() : inputs[IN_INPUT].getVoltage(c);
			const int semis = snappedNote(in);
			if (scale.divides()) {
				// The band from the voltage itself, never back from the
				// snapped semitones, where the screen shows it.
				ownBands[nOwnBands++] = Divider::of(scale).shown(Divider::of(scale).band(in));
			}
			else if (scale.holds(semis))
				ownWork[nOwn++] = keyRow(semis); // the top C of a roll its own, as every other light names it
			outputs[CV_OUTPUT].setVoltage(sane(snappedVolts(in)), c);
		}
		jackWidth = n;
		routedJack();
	}
	/** A Seq's ROUTE while its playhead stands on me: my jack is the cable it
	hands me, quantized a voice a channel, as the Seq sounds it, and my own
	cable's notes stay lit as they are, by the rule a routed Atn keeps. */
	void route(int lane, bool on, const float* volts, int n) override {
		if (!routed.take(lane, on, volts, n))
			return;
		written();
		// The router moves at the Seq's wake and not at mine, so my jack is
		// rewritten at once, and only my jack: what a reader has lit me this
		// wake stands.
		const int col = colorWork;
		const bool acting = actingWork;
		publishJack();
		colorWork = col;
		actingWork = acting;
	}
	void routedJack() {
		if (!claimedByReader())
			routed.clear(); // no Seq walking me, so nobody routing to me
		if (!routed.any())
			return;
		outputs[CV_OUTPUT].setChannels(routed.n);
		for (int c = 0; c < routed.n; c++)
			outputs[CV_OUTPUT].setVoltage(sane(snappedVolts(routed.v[c])), c);
		jackWidth = routed.n;
	}
	// How wide the jack is meant to be, which an uncabled port does not
	// keep, for its glow (`channelsMeant`).
	int jackWidth = 1;
	int channelsMeant(int jack) override { return jack == CV_OUTPUT ? jackWidth : 1; }
	void evaluate(const ProcessArgs& args) override {
		Cartridge::evaluate(args);
		// Whether the window it shapes reaches anyone past me: I pass it
		// on, so I use it while what holds me uses mine.
		tellUse(at(sWindow), this, usedNow(), useTold);
	}
	/** A CV's word on how it plays the window I pass it, passed on to what
	shapes it in front of me, which I neither know nor ask about: a window
	passes through me, and so does what is said of it. */
	void heldMode(int by, int mode) override {
		if (Member* w = at(sWindow))
			w->heldMode(by, mode);
	}
	void heldModeOwed() override {
		if (Member* w = at(sWindow))
			w->heldModeOwed();
	}
	/** Standing on it with nothing to snap, neither a reader's cable nor a
	cable of my own, a playhead sounds the keyboard as a chord. My own cable
	is snapped as my jack snaps it, never the scale sounded over it. */
	void sound(int sub, const Signal& sig, int col, Sound& o) override {
		bandActed = NO_BAND;
		Cartridge::sound(sub, sig, col, o);
		if (o.signal.present && scale.divides() && bandActed != NO_BAND) {
			o.band = bandActed;
			o.volts = Divider::of(scale).line(bandActed);
		}
		if (!o.signal.present) {
			o.type = Sound::CHORD;
			o.chord = scale;
		}
	}
	int bundle(float* v) override { return 0; } // a chord: no channel of a bundle, and a Poly never asks (its row `gives` none, Manifest.hpp)

};

/** The keyboard, laid out flat. A pitch class is either one of the seven
white keys or one of the five black ones. The whites are a column of seven
rectangles filling the right of the screen top to bottom, the blacks a column
of five rectangles filling the left, no gaps and no margins between them, so
each side is one solid strip of touching cells to click and drag over. */
/** A keyboard's key out of the pattern, black and white alike: the white
key's dark, laid at four fifths over the glass. */
const NVGcolor KEY_OFF = nvgRGB(0x1e, 0x1e, 0x1e);
/** The glass a keyboard leaves dark above and below its keys, in pixels, and,
walked, the dark between the playhead's strip and a Seq's second screen: two
pixels of black either end, as the walked keyboard's gap under the playhead
has. The keys' first lit row stands a pixel under its box. */
const float KEY_MARGIN = 2.f;
/** The height a keyboard's keys stand in, unwalked, on a screen `boxH`
tall: inside the glass's bevel and KEY_MARGIN of dark past it, top and foot
on whole pixels. The volt divider's bands divide the same height, so its top
band and its foot band end where a keyboard's top and lowest key do and never
on the bevel. */
inline void keysSpan(float boxH, float& top, float& foot) {
	const float in = GLASS_BEVEL;
	top = in + KEY_MARGIN;
	foot = in + std::round(boxH - 2.f * in) - KEY_MARGIN;
}
const float LANE_GAP = 2.f;
const float KEYS_UNDER = 2.f;
const float BLACK_W = 6.0f; // the left column's width; the right column butts against it and fills the rest
/** The head screen's margins, in mm: the degree ring's down its right and
as much down its left, the count centred between them. */
const float QNT_MARK_W = 1.4f;

/** The roll, the keyboard's second mode:
twelve equal rows an octave, each the whole width of the panel, white keys
white and black keys black, the notes in the pattern painted over them. The
point of it is that a roll does not end at the panel. Two columns, blacks on
the left and whites on the right, are a keyboard: a thing you read one module
at a time. Rows edge to edge line up with the rows of the Qnt beside them, so
a stack of these standing together is one roll a chord can be drawn across in
a single sweep, the way notes are drawn in a sequencer. */

/** The roll's own colours. Not a keyboard's white and black: a roll is read
for its chords, not for its keys, so the two shades are a DAW's piano roll
background (a grey lane and a lane only a little darker) and the pattern's
notes are painted over them. The keys are there to be counted from when you
need them and to keep out of the way when you do not, and the thing that
stands out is the music; a wall of black and white is all the eye sees. */
const NVGcolor ROLL_WHITE = nvgRGB(0x35, 0x35, 0x35);
const NVGcolor ROLL_BLACK = nvgRGB(0x28, 0x28, 0x28);

/** A Qnt with no note picked passes what it is given untouched, which is a
bypass (there is no chromatic default), and it looks it: its keys stand
back a notch, a keyboard's darker and its letters fainter, a roll's lanes
halfway to the dark with less between them, so it reads as disabled and ready
to be clicked rather than as a scale of nothing. How far toward the dark a
roll's lanes go, and what an empty keyboard's keys and letters are laid at. */
const float EMPTY_LANE_FADE = 0.5f;
/** How far a chosen note is lifted toward white, one neutral white on every
key and row, so no note reads brighter than another. */
const float KEY_LIFT = 0.88f;
/** A QNT standing down for the one touching it on the right
(`stoodDownUi`) shapes only its own OUT, so its chosen keys are lifted only
this far: still read as its pattern, never as the scale the CVs take. */
const float STOOD_DOWN_LIFT = 0.4f;
const float EMPTY_KEY_ALPHA = 0.5f;
const float EMPTY_TEXT_ALPHA = 0.15f;

/** Where one key sits on a screen `h` pixels tall, measured from its top:
each column divides the whole height into equal cells, seven an octave for
the whites and five for the blacks, filling it edge to edge over however
many octaves the keyboard spans, its lowest key at the bottom. The edges
fall where they fall, as a roll's rows do, and the key snaps them to the
device's pixels as it draws: rounding cells to whole units made some a
pixel shorter than the rest (F among the whites, E flat among the blacks),
and a keyboard is only a keyboard while its keys are alike. */
inline void keyBox(int key, int octaves, float h, float& y, float& hh) {
	const int pc = key % 12;
	const bool black = isBlackKey(pc);
	const int cells = (black ? 5 : 7) * octaves;
	// Its cell in its column: how many keys of its colour stand between C and
	// it.
	int i = 0;
	for (int d = 0; d < key; d++)
		i += isBlackKey(d) == black;
	const float top = h - (float)(i + 1) * h / (float)cells, foot = h - (float)i * h / (float)cells;
	y = top;
	hh = foot - top;
}

/** Where one row of the roll sits, in the panel's own pixels. A roll has one
row a semitone and every row exactly the same height, so the rows of one Qnt
stand level with the rows of the next and a chord reads as one shape. Exactly:
the rows are NOT rounded to whole pixels. Forty-eight rows in two hundred and
fifty-two is five and a quarter pixels a row, and there is no way to round
that which does not leave some rows a fifth taller than their neighbours: B
flat wider than B, and wider in one octave than in the next. Rounding would
buy a crisp seam, because two rows whose shared edge falls inside a pixel each
cover part of it and the two parts together come to less than one, which lets
what is behind them through as a hairline. What pays for that instead is the
grey underneath: the screen's own ground is the roll's pale lane rather than
the black pane, so a seam that falls inside a pixel blends one grey into
another and there is nothing dark left to show. The outer edges stop where the
screen stops, on its fraction of a pixel, and blend with the plate there the
way every screen's edge does; the plate lies under the screen, and a roll that
reached past the screen would read as a roll one pixel too tall. */
inline void rollBox(int key, int octaves, float h, float& y, float& hh) {
	// Every semitone of its octaves and the one closing the top octave.
	const int cells = 12 * octaves + 1;
	const float y0 = h - (float)(key + 1) * h / (float)cells;
	const float y1 = h - (float)key * h / (float)cells;
	y = y0;
	hh = std::max(y1 - y0, 1.f);
}

#include "QntShaper.hpp"

#include "QntKeys.hpp"

/** Where a roll stands on the piano, while the hand is on it: C3, C4 and so
on, on the C of every octave of the block's leftmost roll alone, while a
stroke with shift, or command and shift, held is reshaping the roll, and
already while either is held with the pointer over the block's keys, before any
stroke, for a glance at where the roll stands; gone the instant the stroke lets go or the keys are
let go. Each is a red dot laid exactly on its C's row, as tall as the row
at any zoom and over everything else on the screen, with the name standing
white above it, so the dot says which row and the name never covers one; a C
with no room above it for its name is left out, dot and all. Nothing is drawn otherwise, so
it is there to find one's bearings and not to read the roll by. */
struct RollOrient : Widget {
	Qnt* qnt = nullptr;
	bool shown = false;
	/** Shift held, with or without command, over the keys' screen of any Qnt
	of my block, me its leftmost: the keys a stroke reshaping the roll begins
	with. Over the head the same keys move the transpose by octaves, so the
	marks stay away there. */
	bool peeked() {
		if (!Hand::among(G_KEY_OCTAVES))
			return false;
		Widget* h = APP->event->hoveredWidget;
		KeysScreen* over = h ? dynamic_cast<KeysScreen*>(h) : nullptr;
		if (h && !over)
			over = h->getAncestorOfType<KeysScreen>();
		ModuleWidget* mw = over ? over->getAncestorOfType<ModuleWidget>() : nullptr;
		if (!mw || !mw->module)
			return false;
		const std::vector<Qnt*> block = QntShaper::blockOf(qnt);
		if (block.empty() || block.front() != qnt)
			return false;
		for (Qnt* m : block)
			if (m == mw->module)
				return true;
		return false;
	}
	void step() override {
		shown = qnt && relaxed(qnt->rollNow) && relaxed(qnt->octavesNow) > 0 && (qnt->orientHeld || peeked());
		Widget::step();
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		Widget::drawLayer(args, layer);
		if (layer != 1 || !qnt || !shown)
			return;
		const int oct = relaxed(qnt->octavesNow);
		const int shift = relaxed(qnt->rollShiftNow);
		std::shared_ptr<Font> font = plateFont();
		if (!font || oct <= 0)
			return;
		const float in = GLASS_BEVEL, h = box.size.y - 2.f * in;
		const int base = Qnt::rollBase(oct, shift);
		nvgFontFaceId(args.vg, font->handle);
		// One size of name at every octave count, readable, standing over its
		// dot, which sits under the middle of the name.
		const float px = 7.5f, padX = 3.f, air = 1.f;
		nvgFontSize(args.vg, px);
		nvgTextLetterSpacing(args.vg, 0.2f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
		// The names first and the dots after, so a dot is over any name.
		for (int pass = 0; pass < 2; pass++)
			for (int row = 0; row <= 12 * oct; row++) {
				const int semis = base + row;
				if (eucMod(semis, 12) != 0)
					continue;
				float y = 0.f, hh = 0.f;
				rollBox(row, oct, h, y, hh);
				if (in + y - air - px < 0.f)
					continue;
				const std::string name = noteName(semis);
				const float w = nvgTextBounds(args.vg, 0.f, 0.f, name.c_str(), nullptr, nullptr) + 2.f * padX;
				const float x = mm2px(0.6f) + w / 2.f;
				if (pass == 0) {
					nvgFillColor(args.vg, color::alpha(color::WHITE, 0.95f));
					nvgText(args.vg, x, in + y - air, name.c_str(), nullptr);
					continue;
				}
				// Just filling the C's row, under the middle of its name.
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, x, in + y + hh / 2.f, hh / 2.f);
				nvgFillColor(args.vg, SCHEME_RED);
				nvgFill(args.vg);
			}
	}
};

/** What a stroke with a modifier takes hold of wherever on the screen it
begins: the keys hold one each already, and this lies behind them over the
whole screen, so the playhead's strip over a keyboard a Seq walks, the gap
under it, a roll's margin and a divider's face reshape the Qnt as a key does.
It sits above the keyboard in the order a press is offered, so it takes a
press only with a modifier held and lets a plain one through to the key or the
strip under it; on the divider, with no keys under it, it takes every
press, as a screen does. */
struct ShapeCatch : Widget {
	Qnt* qnt = nullptr;
	QntShaper shaper;
	bool shaping = false;
	bool keyless() { return qnt && qnt->showsDivider(); }
	Widget* kb = nullptr; // the keyboard under me, for the key a press stands on
	/** The keyboard key under a point of mine, minus one off the keys or on a roll. */
	int keyAt(math::Vec pos) {
		if (!kb)
			return -1;
		const math::Vec in = pos.minus(kb->box.pos);
		for (Widget* w : kb->children) {
			KeyCell* r = dynamic_cast<KeyCell*>(w);
			if (r && r->visible && !r->roll && r->box.contains(in))
				return r->key;
		}
		return -1;
	}
	void onButton(const ButtonEvent& e) override {
		if (!qnt || e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		if (e.action != GLFW_PRESS) {
			if (keyless() || shaping)
				e.consume(this);
			return;
		}
		shaper.tuneKey = keyAt(e.pos);
		shaper.by = this;
		shaping = shaper.begin(qnt);
		if (shaping || keyless())
			e.consume(this);
	}
	void onDragStart(const DragStartEvent& e) override {
		if (shaping)
			e.consume(this);
	}
	void onDragMove(const DragMoveEvent& e) override {
		if (shaping && e.button == GLFW_MOUSE_BUTTON_LEFT)
			shaper.move(qnt);
	}
	void onDragEnd(const DragEndEvent& e) override {
		if (shaping && e.button == GLFW_MOUSE_BUTTON_LEFT)
			shaper.end(qnt);
		shaping = false;
	}
};

/** The head screen, over the keys as a Seq's step stands over its rate, in
every mode: the mode's own count, dragged as every
screen is and drawn by `drawNumber`, centred between two margins, the ring's
on the right. On a keyboard it is the transpose,
in degrees of the pattern with the ring after it lit (`drawDegreeMark`), or
in semitones with the ring dark, a click on the ring (`DegreeCell`) turning
one into the other; on a roll the transpose in semitones, no ring, since a
roll has no pattern to count along; on the divider its parts, nought to
ninety-nine, nought unquantized. One param screen whose param is the mode's:
it follows what the engine published, and never while a stroke on it is
going, so a stroke ends on the param it began on. A double click is the
param's default, nought either way. On a transpose, command takes the whole
block, a stroke moving every transpose by the same steps and a double click
setting every one back to nought. */
struct QntHead : ScreenParam {
	bool degreeHot = false;
	/** Where the ring's button stands, from the last digit's cell to the
	screen's right edge, worked out from the cells as they were last drawn
	(`degreeMarkBox`), so the button is wherever the ring is: placed by a
	margin of its own it lay right of the ring, and a click on the ring
	itself fell on the count. */
	Rect ringHit;
	/** The ring's margin down the right, and as much down the left. */
	static float markW() { return std::round(mm2px(QNT_MARK_W)); }
	Qnt* qnt() { return paramModule<Qnt>(this); }
	/** Whether the count is a keyboard's transpose, which carries a unit. */
	bool keyboard() {
		Qnt* q = qnt();
		return q && !q->showsDivider() && !relaxed(q->rollNow);
	}
	void follow() {
		Qnt* q = qnt();
		if (!q || APP->event->getDraggedWidget() == this)
			return;
		paramId = q->showsDivider() ? Qnt::DIV_PARAM : Qnt::TRANSPOSE_PARAM;
	}
	void step() override {
		follow();
		ScreenParam::step();
	}
	/** A step of the count, and with shift held a step of the period on a
	transpose (`transposePeriod`), landing on its multiples
	(`VerticalDragParam::strokeMove`): a divider's count has no period. */
	float unit() override {
		Qnt* q = qnt();
		if (!q || paramId == Qnt::DIV_PARAM || !Hand::among(G_QNT_OCTAVES))
			return 1.f;
		return (float)q->transposePeriod();
	}
	void span(engine::ParamQuantity* pq, float& lo, float& hi) override {
		Qnt* q = qnt();
		if (!q || paramId == Qnt::DIV_PARAM) {
			ScreenParam::span(pq, lo, hi);
			return;
		}
		int l, h;
		q->transposeSpan(l, h);
		lo = (float)l;
		hi = (float)h;
	}
	/** Whether a stroke (`G_QNT_EVERY`) or a double click (`G_QNT_RESET`)
	with command held takes the whole block: on a transpose, a keyboard's or a
	roll's, never a divider's parts. */
	bool together(const Gesture& g) {
		Qnt* q = qnt();
		return q && paramId == Qnt::TRANSPOSE_PARAM && !q->showsDivider() && Hand::among(g);
	}
	// Every transpose a command stroke moved, with what it was, for one undo.
	GestureChanges block;
	bool tookBlock = false;
	void onDragStart(const DragStartEvent& e) override {
		ScreenParam::onDragStart(e);
		block.was.clear();
		block.noteAt(this, startValue);
		tookBlock = false;
	}
	/** One member's transpose moved `steps` steps of `per`, from where it
	stands, as far as whole steps go within its own span: a member at +55
	moved up an octave stays where it is, so every member a block stroke moves
	keeps its distance from the others in whole steps. Says whether it took
	the steps whole. */
	bool stepMember(Qnt* m, int steps, int per) {
		int lo, hi;
		m->transposeSpan(lo, hi);
		engine::ParamQuantity* pq = m->paramQuantities[Qnt::TRANSPOSE_PARAM];
		const int now = clamp((int)std::round(pq->getValue()), lo, hi);
		int k = steps;
		while (k != 0 && (now + k * per > hi || now + k * per < lo))
			k -= k > 0 ? 1 : -1;
		if ((float)(now + k * per) != pq->getValue()) {
			block.note(m, Qnt::TRANSPOSE_PARAM);
			pq->setValue((float)(now + k * per));
		}
		return k == steps;
	}
	/** With command, every transpose of the block moves by the same steps
	from where each stands, as a CRV's command stroke moves every shape of its block: a semitone or a
	degree a step, and with shift too a period of each, an octave of
	semitones; each held at its own ends. The steps are taken from what each
	stands at, so no travel is kept past an end, and the dragged one's end
	drops the pixels left over as `landStep` does, so turning back moves at
	once (the over-drag rule, `qnt-transpose-block-overdrag`). */
	void strokeMove(const DragMoveEvent& e) override {
		if (!together(G_QNT_EVERY) || e.button != GLFW_MOUSE_BUTTON_LEFT) {
			ScreenParam::strokeMove(e);
			return;
		}
		Qnt* q = qnt();
		const float dy = strokeDelta(e);
		moved = moved || strayed();
		dragAccum += dy;
		const int took = (int)stepsTaken();
		if (took == 0)
			return;
		tookBlock = true;
		const bool shift = Hand::among(G_QNT_OCTAVES);
		bool whole = true;
		for (Qnt* m : QntShaper::blockOf(q)) {
			if (m->showsDivider())
				continue;
			const bool all = stepMember(m, -took, shift ? m->transposePeriod() : 1);
			if (m == q)
				whole = all;
		}
		if (!whole)
			dragAccum = 0.f;
	}
	/** A double click puts the transpose back to nought; with command, every
	transpose of the block, one undo with the stroke the second press began. */
	void resetToDefault(engine::ParamQuantity* pq) override {
		if (!together(G_QNT_RESET)) {
			ScreenParam::resetToDefault(pq);
			return;
		}
		tookBlock = true;
		for (Qnt* m : QntShaper::blockOf(qnt())) {
			if (m->showsDivider())
				continue;
			engine::ParamQuantity* t = m->paramQuantities[Qnt::TRANSPOSE_PARAM];
			if (t->getValue() == t->getDefaultValue())
				continue;
			block.note(m, Qnt::TRANSPOSE_PARAM);
			t->setValue(t->getDefaultValue());
		}
	}
	void onDragEnd(const DragEndEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (tookBlock) {
				block.push("transpose");
				// The gesture is pushed whole; the screen's own record sees no change.
				if (engine::ParamQuantity* pq = getParamQuantity())
					startValue = pq->getValue();
			}
			block.was.clear();
			tookBlock = false;
		}
		ScreenParam::onDragEnd(e);
	}
	void drawLit(const DrawArgs& args) override {
		Qnt* q = qnt();
		engine::ParamQuantity* pq = getParamQuantity();
		const NVGcolor col = q ? q->faceColor() : SCHEME_YELLOW;
		const bool divider = paramId == Qnt::DIV_PARAM;
		int n = pq ? (int)std::round(pq->getValue()) : 0;
		// By degrees, the count sounding, which the engine holds at the
		// pattern's reach (`degreeReach`); a stroke never passes it.
		const bool keys = !divider && keyboard();
		const bool degrees = keys && q && !q->chromaticNow();
		if (degrees)
			n = q->scaleNow.load().degrees;
		nvgSave(args.vg);
		nvgTranslate(args.vg, markW(), 0.f);
		const Vec size(box.size.x - 2.f * markW(), box.size.y);
		// Two cells: a divider's parts decimal to 99 and hex past it with its
		// mark (`drawCount`), a degree count and a transpose under a hundred,
		// a transpose's sign on a cell of its own. Nought is no transpose
		// at all, so the screen goes dark, ghosts only, ring and all, rather
		// than saying a number nobody set.
		const CellsLayout l = divider ? drawCount(args.vg, size, n, col, 2)
		                              : drawNumber(args.vg, size, n ? string::f("%d", std::abs(n)) : "", col, 3, signedMark(n));
		if (keys)
			drawDegreeMark(args.vg, l, col, degrees && n, degreeHot);
		nvgRestore(args.vg);
		if (keys && l.ok) {
			const Rect ring = degreeMarkBox(l);
			const float x = markW() + l.x + l.cell * (float)l.cells;
			ringHit = Rect(Vec(x, 0.f), Vec(std::max(box.size.x - x, markW() + ring.pos.x + ring.size.x - x), box.size.y));
		}
	}
};

/** The ring after a keyboard's transpose, as a button: a click turns the
count from degrees to semitones and back, one undo, the count staying as it is
and read again in the other unit (`MarkCell`). It covers the ring and what is right of
it, where the head last drew it (`QntHead::ringHit`); the screen paints the
ring (`drawDegreeMark`), brightened under the pointer. Not there on a roll or
a divider, which have no degrees. */
struct DegreeCell : MarkCell {
	QntHead* head = nullptr;
	float next(float v) override { return v > 0.5f ? 0.f : 1.f; }
	void step() override {
		visible = head && head->keyboard() && !head->ringHit.size.isZero();
		if (visible)
			box = head->ringHit;
		if (!visible && hot)
			*hot = false;
		ParamWidget::step();
	}
};

/** The volt divider's face, on the keys' glass while the Qnt divides: one
period of its lines from the foot up, ten volts of equal parts or the
harmonic series on a scale of octaves (`Divider::faceAt`), cut by thin lines
into its parts, a faint VOLT DIVIDER or HARMONICS standing up behind them,
and the band sounding filled in the colour of whoever sounds it, as a key is.
Equal parts bent carry a small mark of their curve in the top corner, so a
bend too slight for the eye is still said. The lines, the word and the mark
are the plain pass and dim with the room; a band in use glows. A line closer
than a pixel and a half to its neighbour is left out, and the bands alone say
where the volt is; a band thinner than two pixels, as 255 parts make them, is
lit two pixels tall on whole pixels, centred on its true middle, as a CV's
note is (`paintNotes`), so the band sounding is never a smear. */
struct DividerFace : Widget {
	Qnt* qnt = nullptr;
	/** The divider, and the height the keys stand in (`keysSpan`), never on
	the glass's bevel. */
	bool parts(Divider& d, float& foot, float& h) {
		if (!qnt || !qnt->showsDivider())
			return false;
		d = Divider::of(qnt->scaleNow.load());
		float top;
		keysSpan(box.size.y, top, foot);
		h = foot - top;
		return h > 0.f;
	}
	/** Where edge `j` of the bands stands: equal parts straight a band's
	height a part, as they always stood; bent or harmonic where the law puts
	it. */
	static float edgeY(const Divider& d, int j, float foot, float h) {
		if (!d.harmonic && d.bend == 0)
			return foot - h / (float)d.n * (float)j;
		return foot - h * d.faceAt(j);
	}
	/** Where band `b` is lit: its own height, or a line two thick about its
	middle where it is thinner than two, placed as every line on a screen is
	(`lineBand`). */
	static void bandSpan(NVGcontext* vg, const Divider& d, int b, float foot, float h, float& y0, float& th) {
		if (!d.harmonic && d.bend == 0) {
			const float bh = h / (float)d.n;
			y0 = foot - bh * (float)(b + 1);
			th = bh;
			if (bh < 2.f) {
				const LineBand l = lineBand(vg, foot - bh * ((float)b + 0.5f), 2.f);
				y0 = l.top;
				th = l.h;
			}
			return;
		}
		const float lo = edgeY(d, b, foot, h), hi = edgeY(d, b + 1, foot, h);
		y0 = hi;
		th = lo - hi;
		if (th < 2.f) {
			const LineBand l = lineBand(vg, (lo + hi) / 2.f, 2.f);
			y0 = l.top;
			th = l.h;
		}
	}
	/** A word standing up the screen, reading from the foot, centred at x. */
	void upright(const DrawArgs& args, float x, const char* word, NVGcolor ink) {
		std::shared_ptr<Font> font = plateFont();
		if (!font)
			return;
		nvgSave(args.vg);
		float top, foot;
		keysSpan(box.size.y, top, foot);
		nvgTranslate(args.vg, x, (top + foot) / 2.f);
		nvgRotate(args.vg, -M_PI / 2.f);
		drawWord(args.vg, font, CAPTION_PX, 0.6f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, ink, 0.f, 0.f, word);
		nvgRestore(args.vg);
	}
	/** Where the two words stand at nought: VOLT DIVIDER and UNQUANTIZED
	side by side, each a column off the middle, which one word alone holds. */
	float wordX(int n, int which) {
		const float off = n == 0 ? std::round(CAPTION_PX * 0.75f) : 0.f;
		return box.size.x / 2.f + (which == 0 ? -off : off);
	}
	/** The bend's mark: its curve, foot to top, in a square in the top
	corner, a straight line being no mark at all. */
	void bendMark(const DrawArgs& args, const Divider& d, float foot, float h) {
		const float side = 6.f, x1 = box.size.x - KEY_MARGIN - 1.f, y0 = foot - h + 1.f;
		nvgBeginPath(args.vg);
		for (int i = 0; i <= 8; i++) {
			const float t = (float)i / 8.f;
			const float x = x1 - side + side * t, y = y0 + side - side * bendExp(d.steepness(), t);
			if (i == 0)
				nvgMoveTo(args.vg, x, y);
			else
				nvgLineTo(args.vg, x, y);
		}
		nvgStrokeWidth(args.vg, 1.f);
		nvgStrokeColor(args.vg, color::alpha(color::WHITE, 0.5f));
		nvgStroke(args.vg);
	}
	void draw(const DrawArgs& args) override {
		Divider d;
		float foot, h;
		if (!parts(d, foot, h))
			return;
		// The faint word at nought parts too, so the screen says what it is
		// while it divides nothing.
		upright(args, wordX(d.n, 0), d.harmonic ? "HARMONICS" : "VOLT DIVIDER", color::alpha(color::WHITE, 0.14f));
		if (d.n < 1)
			return;
		if (!d.harmonic && d.bend != 0)
			bendMark(args, d, foot, h);
		if (!d.harmonic && d.bend == 0) {
			// Straight, every band the same height: all the lines or none.
			if (h / (float)d.n < 1.5f)
				return;
		}
		nvgBeginPath(args.vg);
		for (int j = 1; j < d.n; j++) {
			const float y = edgeY(d, j, foot, h);
			if (d.harmonic || d.bend != 0) {
				const float below = edgeY(d, j - 1, foot, h) - y, above = y - edgeY(d, j + 1, foot, h);
				if (std::min(below, above) < 1.5f)
					continue;
			}
			nvgMoveTo(args.vg, KEY_MARGIN, y);
			nvgLineTo(args.vg, box.size.x - KEY_MARGIN, y);
		}
		nvgStrokeWidth(args.vg, 0.5f);
		nvgStrokeColor(args.vg, color::alpha(color::WHITE, 0.35f));
		nvgStroke(args.vg);
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		Widget::drawLayer(args, layer);
		Divider d;
		float foot, h;
		if (layer != 1 || !parts(d, foot, h))
			return;
		const int n = d.n;
		// Under the count's nought, UNQUANTIZED: a setting of the rig's
		// quantization that holds from here on, as a scale does. Upright,
		// since the screen is three units wide and the word eleven letters.
		if (n < 1) {
			upright(args, wordX(n, 1), rig::UNQUANTIZED_WORD, color::alpha(color::WHITE, 0.8f));
			return;
		}
		const float x0 = KEY_MARGIN, w = box.size.x - 2.f * KEY_MARGIN;
		for (int b = 0; b < n; b++) {
			int cols[Qnt::MAX_HEADS];
			const int nh = qnt->bandHeadsOf(b, cols);
			if (nh == 0)
				continue;
			// My own cable's band in the cable's colour, as a member wears
			// what arrived through its jack; a band several readers sound
			// cut into upright strips side by side, one a reader, as a key
			// is (`paintHeadStrips`).
			const bool own = qnt->bandOwn(b);
			float y0, th;
			bandSpan(args.vg, d, b, foot, h, y0, th);
			paintHeadStrips(args.vg, x0, y0, w, th, nh, [&](int i) { return qnt->headColor(own, cols[i]); });
		}
	}
};

/** The divider's face as a hand takes it: one stroke along one line of
places, full exp at the foot, straight in the middle, full log, and past it
at the top the harmonic series, through the stroke every screen takes
(`VerticalDragParam`), so it steps from where the face stands and stops at
either end with no travel kept. Up bends the equal parts toward log, their
span kept, and down toward exp; straight is a detent a plain drag holds for
a few pixels before leaving it, and a drag crossing it lands on it; the
harmonics are a detent at the top, reached by a stroke pushing on past full
log for as many pixels, and left by one coming down as far, onto full log.
Shift steps among the four that mean something, full exp, straight, full log
and the harmonics; command steps finer. A double click is the reset, equal
volts and straight, as everywhere. The law and the bend are two params, and
the harmonics leave the bend at full log, where a stroke down finds it. A
stroke is one undo, the law and the bend together. It stands over the face
only while the Qnt divides into a part or more. */
struct BendFace : VerticalDragParam {
	Qnt* qnt = nullptr;
	float lawStart = 0.f;
	float detentPx = 0.f; // a plain stroke's travel held at a detent
	static constexpr float PX = 1.5f, FINE_PX = 6.f, NOTCH_PX = 24.f, DETENT_PX = 12.f;
	/** The top of the line, the harmonics, one place past full log. */
	static constexpr int HARMONICS = rig::DIV_BEND_MAX + 1;
	bool harmonic() { return module && module->params[Qnt::DIVLAW_PARAM].getValue() >= 0.5f; }
	/** Where the face stands on the line: the bend, or the harmonics. */
	int place() {
		engine::ParamQuantity* pq = getParamQuantity();
		return harmonic() ? HARMONICS : pq ? (int)std::round(pq->getValue()) : 0;
	}
	/** The face set to `to` on the line, the law and the bend. */
	void setPlace(int to) {
		engine::ParamQuantity* lq = module->paramQuantities[Qnt::DIVLAW_PARAM];
		lq->setValue(to == HARMONICS ? 1.f : 0.f);
		if (to != HARMONICS)
			getParamQuantity()->setValue((float)to);
	}
	void step() override {
		visible = qnt && qnt->showsDivider() && qnt->scaleNow.load().divides();
		VerticalDragParam::step();
	}
	void onDragStart(const DragStartEvent& e) override {
		lawStart = module ? module->params[Qnt::DIVLAW_PARAM].getValue() : 0.f;
		detentPx = 0.f;
		VerticalDragParam::onDragStart(e);
	}
	/** Equal volts and straight, the face's default, in the stroke's one
	undo. */
	void resetToDefault(engine::ParamQuantity* pq) override {
		VerticalDragParam::resetToDefault(pq);
		if (module)
			module->paramQuantities[Qnt::DIVLAW_PARAM]->setValue(0.f);
	}
	void strokeMove(const DragMoveEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT || !module)
			return;
		engine::ParamQuantity* pq = getParamQuantity();
		if (!pq)
			return;
		const float dy = strokeDelta(e);
		moved = moved || strayed();
		const bool notch = Hand::among(G_BEND_NOTCH);
		pxPerUnit = notch ? NOTCH_PX : Hand::among(G_BEND_FINER) ? FINE_PX : PX;
		const int from = place(), most = rig::DIV_BEND_MAX;
		// Down is a positive travel, a step toward exp.
		if (!notch && from == most && detentPx + dy < 0.f) {
			// Full log holds a stroke going on up, and past the detent
			// lands on the harmonics.
			detentPx += dy;
			dragAccum = 0.f;
			if (detentPx > -DETENT_PX)
				return;
			detentPx = 0.f;
			setPlace(HARMONICS);
			return;
		}
		if (!notch && (from == 0 || from == HARMONICS)) {
			// Straight holds a plain stroke for a few pixels either way,
			// and the harmonics a stroke coming down; one pushing on up
			// keeps nothing.
			detentPx += dy;
			if (from == HARMONICS)
				detentPx = std::max(detentPx, 0.f);
			dragAccum = 0.f;
			if (std::fabs(detentPx) < DETENT_PX)
				return;
			dragAccum = detentPx - std::copysign(DETENT_PX, detentPx) + std::copysign(pxPerUnit / 2.f, detentPx);
			detentPx = 0.f;
		}
		else {
			detentPx = 0.f;
			dragAccum += dy;
		}
		const int took = (int)stepsTaken();
		if (took == 0)
			return;
		int to;
		if (notch) {
			// The four places that mean something, in order up the line.
			const int notches[] = {-most, 0, most, HARMONICS};
			int at = 0;
			for (int i = 0; i < 4; i++)
				if (notches[i] <= from)
					at = i;
			// Between two, a step down lands on the one below.
			const int down = notches[at] < from ? 1 : 0;
			at = clamp(at - took + (took > 0 ? down : 0), 0, 3);
			to = notches[at];
		}
		else {
			to = clamp(from - took, -most, most);
			if (from != 0 && from != HARMONICS && (to == 0 || (to < 0) != (from < 0))) {
				// A stroke crossing straight lands on it, and is held there.
				to = 0;
				dragAccum = 0.f;
			}
		}
		if (to == -most || to == most || to == HARMONICS)
			dragAccum = 0.f; // an end, or the edge of the harmonics' detent: no travel kept
		setPlace(to);
	}
	/** The law and the bend as they stood at the press, one undo. */
	void recordStroke() override {
		if (!module)
			return;
		GestureChanges g;
		g.noteAt(module, Qnt::DIVBEND_PARAM, startValue);
		g.noteAt(module, Qnt::DIVLAW_PARAM, lawStart);
		const bool turned = module->params[Qnt::DIVLAW_PARAM].getValue() != lawStart;
		g.push(turned ? "divider law" : "divider bend");
	}
	void draw(const DrawArgs& args) override {}
};

std::string KeyQuantity::getString() {
	// The note and its detune: whether it is lit is plain to see on the
	// key and needs no words. What a reshaping stroke has made is said on
	// the hint line (`QntShaper::say`).
	Qnt* k = dynamic_cast<Qnt*>(module);
	if (!k || key < 0)
		return getLabel();
	const bool roll = relaxed(k->rollNow);
	ParamQuantity* at =
		k->paramQuantities[Qnt::keyParamIn(key, roll, std::max(1, (int)relaxed(k->octavesNow)), relaxed(k->rollShiftNow))];
	// A keyboard's key is named as its letter reads, by the transpose in
	// semitones (`movedBy`, QntKeys.hpp), the note it now sounds. Its
	// pattern repeats every so many octaves from C4, so a key past C9, the
	// top of what a jack plays, is named a period down, where it sounds:
	// a ten-octave keyboard's keys over C9 are C#-1 to B3, not C#9 to B13.
	int semis = key + k->movedBy(k->scaleNow.load());
	const int period = 12 * std::max(1, (int)relaxed(k->octavesNow));
	while (semis > Qnt::ROLL_BELOW) // C9, the roll's top as C-1 is its foot
		semis -= period;
	while (semis < -Qnt::ROLL_BELOW)
		semis += period;
	const std::string name = roll || key >= Qnt::TOP_KEY ? at->name : noteName(semis);
	std::string s = name;
	const int det = roll || key >= Qnt::TOP_KEY ? 0 : k->detuneOf(key);
	if (det != 0)
		s += string::f(", %+d cents", det);
	return s;
}

/** The variants in the order the right arrow walks them, a variant by its sign
as `modeWith` takes it, and the word the title says for each as it comes. */
const int MODE_WALK[3] = {1, -1, 0};
inline const char* modeWord(int mode) { return mode > 0 ? "KEY" : mode < 0 ? "PNO" : "DIV"; }
/** A mode's place on the walk. */
inline int modeIndex(int mode) {
	int i = 0;
	while (i < 2 && MODE_WALK[i] != mode)
		i++;
	return i;
}

/** The plate, its title the mode's word for a moment after the mode changes
(`ModeFlash`). */
struct QntPanel : Panel {
	ModeFlash flash;
	void see(int mode) { flash.see(mode, modeWord(mode)); }
	uint64_t version() override { return flash.stamp(); }
	void draw(const DrawArgs& args) override {
		drawPlate(args, this, "QNT", mm2px(SCREEN_Y), mm2px(SCREEN_H), flash.word, flash.weight());
		drawCaption(args.vg, CX, IN_CAP_Y, "IN", INK);
		drawOutBlock(args.vg, BLOCK_Y, BLOCK_H);
		drawCaption(args.vg, CX, FOOT_CAP_Y, "OUT", PALE);
	}
};

/** The variant arrows' lesson, alone and with a QNT touching, which the
arrows turn too (`setMode`); "each variant", not "each", so the sentence
needs no noun found for it. */
const char* const ARROW_LESSON = "Keyboard, piano roll or volt divider. Each variant keeps its own keys and octaves.";
const char* const ARROW_BLOCK_LESSON =
    "Keyboard, piano roll or volt divider, for every touching QNT. Each variant keeps its own keys and octaves.";

struct QntWidget : MemberWidget {
	Qnt* qnt = nullptr;
	QntPanel* plate = nullptr;
	VariantArrows arrows;
	Widget* keysScreen = nullptr;
	/** Over the keys' screen where no key says anything, a walked keyboard's
	playhead strip and the margins, the strokes a modifier makes there
	(`ShapeCatch`); the divider's face says its own, and its screen with no
	parts nothing. Elsewhere, nothing of its own. */
	const Hinted* partHinted() override {
		// By kind and by reach: [roll][a QNT touching].
		static Hinted screens[2][2];
		if (screens[0][0].gestures.empty())
			for (int r = 0; r < 2; r++)
				for (int b = 0; b < 2; b++) {
					screens[r][b].gestures = r ? rollScreenGestures(b) : keyScreenGestures(b);
					screens[r][b].hint = hintOf(screens[r][b].gestures);
				}
		// Where the pointer stands, not what it hovers: over a margin the
		// panel itself is the widget hovered.
		if (qnt && keysScreen && APP->scene && APP->scene->rack &&
		    keysScreen->box.contains(APP->scene->rack->getMousePos().minus(box.pos)))
			return qnt->showsDivider() ? nullptr : &screens[relaxed(qnt->rollNow) ? 1 : 0][relaxed(qnt->peersUi) ? 1 : 0];
		return nullptr;
	}
	/** Whether a CV stands to my right in the lane I open
	(manifest::SPECS, the Qnt and the CV): past my block of Qnts and any
	cartridge between, up to a GAP, another Qnt's block, or the end of the
	first block of CVs or CRVs, whose lane is the last a CV finds me from. */
	bool cvsRight(const std::vector<ModuleWidget*>* run) {
		if (!run)
			return false;
		auto me = std::find(run->begin(), run->end(), (ModuleWidget*)this);
		if (me == run->end())
			return false;
		bool peers = true, inLane = false;
		for (auto w = me + 1; w != run->end(); ++w) {
			const std::string& slug = (*w)->model->slug;
			if (slug == "QNT" && peers)
				continue;
			peers = false;
			if (slug == "CV")
				return true;
			if (slug == "CRV") {
				inLane = true;
				continue;
			}
			if (inLane || slug == "GAP" || slug == "QNT")
				return false;
		}
		return false;
	}
	/** What only the UI knows of my colour: the cable arriving and the bond;
	the arrows shown while the hand is over me; and the mode, for the title. */
	void step() override {
		if (qnt) {
			qnt->inRGB.store(cableColorOn(getInput(Qnt::IN_INPUT)), std::memory_order_relaxed);
			const std::vector<ModuleWidget*>* run = runOf(this);
			qnt->bondedUi.store(run && run->size() > 1, std::memory_order_relaxed);
			// A QNT touching me on the right claims me and I stand down
			// (Cartridge.hpp), so the CVs to my right are its to move. Not
			// while a reader walks the block: it claims every member, none
			// stands down for its neighbour, and the one under its playhead
			// is the scale the rig hears, so the walk alone says which.
			const std::vector<Qnt*> block = APP->scene && APP->scene->rack ? QntShaper::blockOf(qnt) : std::vector<Qnt*>(1, qnt);
			qnt->peersUi.store(block.size() > 1, std::memory_order_relaxed);
			const bool cvs = cvsRight(run);
			qnt->cvsRightUi.store(block.back() == qnt && cvs, std::memory_order_relaxed);
			qnt->stoodDownUi.store(block.back() != qnt && cvs && !relaxed(qnt->claimedNow), std::memory_order_relaxed);
			for (GlyphButton* a : arrows.arrow)
				if (a)
					a->lesson = block.size() > 1 ? ARROW_BLOCK_LESSON : ARROW_LESSON;
			plate->see(qnt->modeNow());
			// A Qnt previews while the pointer rests anywhere on it, its
			// keys, its playhead and its transpose alike, in every mode: a
			// Seq's PREVIEW hears it without a press, so patterns can be
			// tried with no button held.
			if (pointerWithin(this))
				qnt->hover();
		}
		arrows.show(this);
		keepOneMode();
		MemberWidget::step();
	}
	/** Every Qnt of `block` not in mode `to` taken there (`modeWith`), its
	writes noted in `turn`: what a mode arrow does, and a bond. */
	static void takeMode(const std::vector<Qnt*>& block, int to, GestureChanges& turn) {
		for (Qnt* m : block)
			if (m->modeNow() != to)
				m->modeWith(to, [&](int p, float v) {
					turn.note(m, p);
					m->params[p].setValue(v);
				});
	}
	/** The mode at place `i` of the walk, for this Qnt's whole block of
	touching Qnts at once, one undo: a block never mixes modes by the hand. */
	void setMode(int i) {
		if (!qnt)
			return;
		const int to = MODE_WALK[i];
		GestureChanges turn;
		takeMode(QntShaper::blockOf(qnt), to, turn);
		turn.push(std::string("QNT variant: ") + modeWord(to));
	}
	/** No block of Qnts ever stands in two modes, however it came to be one
	kept every frame by the block's leftmost Qnt, the block the arrows change.
	A block found mixed takes the mode of what mixed it: a Qnt whose mode
	changed where it stands carries the block with it, as an arrow would;
	else the right-hand Qnt's wins, the rig reading right to left, so where
	two blocks meet the right-hand one does; save a lone Qnt the hand has
	just placed, dragged, set in, duplicated or unbypassed into a block,
	which takes the block's. The pieces are what stood together the frame
	before. A bond is the hand's, so its writes join the step that made it
	and one undo takes back the place and the mode together; a block mixed
	by no step of the hand's, a patch loaded or a param set from outside, is
	put right with no undo of its own, which would undo into the mix and be
	put right again. Nothing is asked while a drag is going, a module passing
	over a block on its way being no bond. */
	void keepOneMode() {
		if (!qnt || APP->event->getDraggedWidget())
			return;
		const std::vector<Qnt*> block = QntShaper::blockOf(qnt);
		if (block.front() != qnt)
			return;
		history::Action* placing = structuralStep();
		auto piece = [](Qnt* m) { return m->stoodIn < 0 ? m->id : m->stoodIn; };
		Qnt* by = qnt;
		int rank = -1;
		for (Qnt* m : block) {
			int with = 0;
			for (Qnt* o : block)
				with += piece(o) == piece(m);
			const int r = m->modeSeen != 2 && m->modeNow() != m->modeSeen ? 2
			              : with == 1 && placing && placesModule(placing, m->id) ? 0 : 1;
			if (r >= rank) {
				by = m;
				rank = r;
			}
		}
		const int to = by->modeNow();
		GestureChanges turn;
		takeMode(block, to, turn);
		if (placing)
			pushJoined(turn.take(std::string("QNT variant: ") + modeWord(to)), placing, true);
		for (Qnt* m : block) {
			m->stoodIn = qnt->id;
			m->modeSeen = m->modeNow();
		}
	}
	QntWidget(Qnt* module) {
		qnt = module;
		QntPanel* panel = new QntPanel;
		plate = panel;
		open(module, panel);
		// The variant arrows, either side of the title, in its ink.
		// The whole block turns at once (`setMode`), the surprise said
		// where a QNT touches (`step`).
		arrows.add(this, 3, [=]() { return qnt ? modeIndex(qnt->modeNow()) : 0; }, [=](int i) { setMode(i); }, ARROW_LESSON);

		addInput(createInputCentered<InPort>(mm2px(Vec(CX, IN_Y)), module, Qnt::IN_INPUT));

		// The head, over the keys' screen as a Seq's step is over its
		// rate: the modes and the mode's count.
		QntHead* head = addScreen<QntHead>(this, module, Qnt::TRANSPOSE_PARAM, SCREEN_Y, HEAD_SCREEN_H);
		DegreeCell* ring = createParam<DegreeCell>(Vec(), module, Qnt::DEGREE_PARAM);
		ring->head = head;
		ring->hot = &head->degreeHot;
		head->addChild(ring);

		// The screen casts its softening line like every piece of ground,
		// rolling or not; the divider's face stands on it too, first, so the
		// reshaping catch, which a divider's face answers too, lies over it.
		KeysScreen* screen = createWidget<KeysScreen>(mm2px(Vec(0.f, HEAD_SCREEN2_Y)));
		screen->box.size = mm2px(Vec(PANEL_W, SCREEN_BOTTOM - HEAD_SCREEN2_Y));
		addChild(screen);
		keysScreen = screen;
		DividerFace* divider = new DividerFace;
		divider->box.size = screen->box.size;
		divider->qnt = module;
		screen->addChild(divider);

		// The keyboard: blacks down the left of the screen, whites down the
		// right, the two columns side by side and never overlapping. Every
		// key of every octave exists from the start; the keyboard shows as
		// many octaves as the Qnt says and lays them out to fill the screen.
		RollLane* lane = new RollLane;
		lane->qnt = module;
		lane->box.pos = Vec(0.f, GLASS_BEVEL);
		lane->box.size = Vec(screen->box.size.x, screen->box.size.y - 2.f * GLASS_BEVEL);
		screen->addChild(lane);

		Keyboard* kb = new Keyboard;
		kb->box.size = screen->box.size;
		screen->addChild(kb);
		for (int key = 0; key <= Qnt::TOP_KEY; key++) {
			KeyCell* r = createParam<KeyCell>(Vec(), module, key == Qnt::TOP_KEY ? Qnt::ROLLTOP_PARAM : Qnt::keyParam(key));
			r->key = key;
			r->pc = key % 12;
			kb->addChild(r);
		}
		KeyLane* keyLane = new KeyLane;
		keyLane->qnt = module;
		keyLane->visible = false;
		kb->lane = keyLane;
		kb->addChild(keyLane);
		kb->layout(1, false, 0);
		KeyHeads* keyHeads = new KeyHeads;
		keyHeads->box.size = screen->box.size;
		keyHeads->qnt = module;
		keyHeads->kb = kb;
		screen->addChild(keyHeads);

		ShapeCatch* reshape = new ShapeCatch;
		reshape->box.size = screen->box.size;
		reshape->qnt = module;
		reshape->kb = kb;
		screen->addChild(reshape);
		// Over the catch, which a divider's face gives every press, the face
		// as a hand takes it: its law and its bend.
		BendFace* bend = createParam<BendFace>(Vec(), module, Qnt::DIVBEND_PARAM);
		bend->box.size = screen->box.size;
		bend->qnt = module;
		screen->addChild(bend);

		RollHeads* heads = new RollHeads;
		heads->box.pos = lane->box.pos;
		heads->box.size = lane->box.size;
		heads->qnt = module;
		screen->addChild(heads);
		RollOrient* orient = new RollOrient;
		orient->box.size = screen->box.size;
		orient->qnt = module;
		screen->addChild(orient);

		addOutput(createOutputCentered<OutPort>(mm2px(Vec(CX, FOOT_Y)), module, Qnt::CV_OUTPUT));
	}
};

} // namespace

Model* modelQnt = createModel<Qnt, QntWidget>("QNT");
