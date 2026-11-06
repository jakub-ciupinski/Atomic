// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Pitch.hpp"
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

/** Words: tooltip text the engine and the modules share, a number with as
many decimals as it needs and what a turn makes of a keyboard. Text, not
engine: nothing here is asked on the audio thread's path. */

namespace rig {

/** A gesture: anything a hand can do to a part beyond the plain drag and
plain click Rack teaches everyone, a modifier held, a double click that does
something, a cable dropped somewhere unusual: every shortcut through one class,
each with its words for the bracket. Declared once, at namespace scope in the file of the
part that owns it, through `declare`, and nowhere else; a handler can only
read the keyboard by asking `Hand` (Hand.hpp) with one, and a part says it by
attaching it to its tip (`Tip::gestures`), so a modifier read and its words
are one declaration.

`on`, what the hand does; `mod`, the keys held; `rank`, how hard it is to
guess, which is also its order on the hint line, ties keeping the order the
part attaches them in: `Invented`, what nobody could guess (a double click
that joins, a cable on a ring), `Block`, a chord reaching beyond the part
(every CRV, every SEQ of the rig), `Known`, what any Rack hand expects (shift
for whole or coarse, command for fine), `Rare`, the chord few will need
(shift and command together). `words`, short and pointing, built from
RACK_MOD_CTRL_NAME and RACK_MOD_SHIFT_NAME so Windows and Linux read Ctrl,
never a value. A gesture whose words change with the part's state (join or
split) is declared once for each, and the part attaches the one that holds. */
enum class On { Click, Double, Drag, Scroll, Key, Cable, Menu };
enum class Mod { None, Shift, Command, Alt, ShiftCommand, CommandOrAlt };
enum class Rank { Invented, Block, Known, Rare };
struct Gesture {
	On on;
	Mod mod;
	Rank rank;
	std::string words;
};
/** The one way a gesture is made: words it must have, since a gesture nobody
is told of is the thing this class is here to prevent; refused at static
initialisation, so the plugin fails to load in the first test. */
inline Gesture declare(On on, Mod mod, Rank rank, std::string words) {
	if (words.empty())
		throw std::logic_error("a gesture declared without words");
	return Gesture{on, mod, rank, std::move(words)};
}
using Gestures = std::vector<const Gesture*>;
/** The hint line's words for `gs`: one piece a line, by rank, ties in the
order given. */
inline std::string hintOf(Gestures gs) {
	std::stable_sort(gs.begin(), gs.end(), [](const Gesture* a, const Gesture* b) { return a->rank < b->rank; });
	std::string h;
	for (const Gesture* g : gs) {
		if (!h.empty())
			h += "\n";
		h += g->words;
	}
	return h;
}

/** A control's hidden gestures, the strokes a hand cannot guess from its
face: `gestures`, as the part attached
them, and `hint`, their words a piece a line by rank (`hintOf`), "Shift-drag:
octaves". They are shown on our own panel while the pointer is over the
part, by the hint line on the bracket (`BracketHint`, Bracket.hpp), whatever
Rack's tooltip switch says, a piece the line has no room for being reached by
its ticker. A module standing alone has no bracket, and its title band is too
small for a line, so there they are said in Rack's tooltip instead, under the
value, once the pointer has rested (`hintInTip`); a bonded module's tooltip
says no gesture. Set through `retipParam`, the one place a control's tooltip
is written. */
struct Hinted {
	std::string hint;
	Gestures gestures;
	virtual ~Hinted() {}
};
/** The part whose hint Rack's tooltip says this frame: the part hovered on a
module standing alone, once the pointer has rested on it, with the hint line
switched on; none otherwise, and none where no window is, so a capture of
every tooltip reads none. Written by the window once a frame before any
tooltip steps (`hintFrame`, Bracket.hpp), read by the tooltip of that part, a
param's (`WithHint`) or one of ours (`HoverTip`, Tips.hpp). Compared by
address alone. */
inline const Hinted* hintInTip = nullptr;
/** `a` and `b` a line apart, either alone where the other is empty: the way
a tooltip's tiers join. */
inline std::string tipLines(const std::string& a, const std::string& b) {
	return a.empty() ? b : b.empty() ? a : a + "\n" + b;
}
/** A control's state as its value, where the state is more than the
param's own position: an ATN's arrows taking the left or shaping the right
by what stands beside it, a switch a cable has taken over. One fixed name
says what the control is, and Rack shows this after it as the value, "Reach:
Takes the left"; empty, the quantity's own value stands. Written through
`restate` alone. */
struct Stated {
	std::string state;
	virtual ~Stated() {}
};
/** The hint on any kind of quantity, a plain one (`HintQuantity`) or a
switch's (a Seq's mode, a Qnt's keys), so a control's kind never decides
whether it can say its gesture; and its state (`Stated`). Rack's tooltip is
the base quantity's own description, with the hint above it only where it is
the one `hintInTip` names, under the value Rack puts first. */
template <class Base>
struct WithHint : Base, Hinted, Stated {
	std::string getDisplayValueString() override {
		return state.empty() ? Base::getDisplayValueString() : state;
	}
	std::string getDescription() override {
		const std::string d = Base::getDescription();
		return hintInTip == static_cast<const Hinted*>(this) ? tipLines(hint, d) : d;
	}
};
using HintQuantity = WithHint<ParamQuantity>;

/** One tooltip's shape, the fields every control and jack of the plugin
fills, whatever module it stands on:

`name`, the job and never the kind of widget ("Shapes the right", not
"Switch"); Rack puts the value after it, the quantity's own
(`getDisplayValueString`), which a tooltip never writes, and "input" or
"output" after a jack's.

`lesson`, held back until the pointer rests (`showTips`): what a newcomer
needs to patch it right, about this part alone, a jack's law in the jack's
own tooltip and never its control's; empty where the name and value say
everything.

`gestures`, a control's hidden gestures (`Gesture`, above), said by the
hint line, or on a lone module by its tooltip after a rest (`hintInTip`); none
on a jack, which takes no gesture. A tip given no
gestures leaves the control's as they were; `Gestures{}` clears them.

Written through `retipParam`, `retipIn` and `retipOut`, in a constructor for
a part that always says the same and from `retipAll` for one that says what
the module is doing now, and nowhere else. The words are borrowed for the
call, so a string built for it lives as long as the statement. */
struct Tip {
	const char* name;
	const char* lesson = "";
	Gestures gestures;
	bool hinted = false;
	Tip(const char* name, const char* lesson = "") : name(name), lesson(lesson) {}
	Tip(const char* name, const char* lesson, Gestures gestures)
	    : name(name), lesson(lesson), gestures(std::move(gestures)), hinted(true) {}
};

inline void retip(engine::PortInfo* info, const Tip& t) {
	if (!info)
		return;
	if (info->name != t.name)
		info->name = t.name;
	if (info->description != t.lesson)
		info->description = t.lesson;
}
inline void retipIn(Module* m, int id, const Tip& t) {
	if (m)
		retip(m->inputInfos[id], t);
}
inline void retipOut(Module* m, int id, const Tip& t) {
	if (m)
		retip(m->outputInfos[id], t);
}
inline void retip(engine::ParamQuantity* pq, const Tip& t) {
	if (!pq)
		return;
	if (pq->name != t.name)
		pq->name = t.name;
	if (pq->description != t.lesson)
		pq->description = t.lesson;
	if (t.hinted)
		if (Hinted* hq = dynamic_cast<Hinted*>(pq))
			if (hq->gestures != t.gestures) {
				hq->gestures = t.gestures;
				hq->hint = hintOf(t.gestures);
			}
}
inline void retipParam(Module* m, int id, const Tip& t) {
	if (m)
		retip(m->paramQuantities[id], t);
}
/** A control's state said as its value (`Stated`), "" giving the value back
to the param; a control must be a `WithHint` to have one. */
inline void restate(Module* m, int id, const std::string& state) {
	if (m)
		if (Stated* s = dynamic_cast<Stated*>(m->paramQuantities[id]))
			if (s->state != state)
				s->state = state;
}
/** A toggle's click where its value already says what it flips: one name,
the state as the value, and the click said as one word. */
inline const Gesture G_TOGGLE = declare(On::Click, Mod::None, Rank::Invented, "Click: toggle");

/** A dark control's tooltip while the engine stands still, after its name
(`MemberWidget::sayOwn`): the hand meets no control then, so the part under the
pointer says it is dark, and the line every tooltip over our modules ends with
while the engine stands still (`NO_AUDIO_TIP`) says why. */
const char* const DARK_TIP = ": dark";

/** The hidden switch of a CV and a Trg standing over a Poly's channel,
which has no face, named for MIDI-map and automation lists by what it
chooses, one configuration for both. */
inline void configHandOrPoly(Module* m, int id) {
	m->configSwitch(id, 0.f, 1.f, 0.f, "Hand or POLY", {"Follows the POLY", "Set by hand"});
}

/** A cable holding a value, a CV's fader or a CRV's start, leaves it where
it held it when pulled (`keepWhatTheCableLeft`), said while it is in. */
const char* const IN_KEEPS = " The last value stays when the cable is pulled.";

/** Where a divider puts a voltage, said one way by a Qnt's IN and a CV's
under it: on the nearest line toward where the divider counts from
(`Divider::band` truncates toward its origin), nought volts alone and an
Atn's offset under one, so it holds for negative volts and a moved origin. */
inline std::string dividerSnap(bool underAtn) {
	return std::string("the nearest line toward ") + (underAtn ? "the ATN's offset" : "0 V");
}

/** A Qnt standing at nought octaves and nought parts, which passes the volts
as they come: unquantized, the word for a setting of the rig's quantization
that holds from there on, as a scale does. The name in a tooltip, the word on
a screen. */
const char* const UNQUANTIZED_NAME = "Unquantized";
const char* const UNQUANTIZED_WORD = "UNQUANTIZED";

/** What a variant arrow, either side of a plate's title, says it does: the
Clock's BPM and HZ and the Qnt's three are a module's
variants, and the arrow says which way it steps through them, as the RIG's
say "Next scene", never naming the one it goes to. */
inline const char* variantStepName(int by) {
	return by > 0 ? "Next variant" : "Previous variant";
}

/** Where a step arrow goes: `by` along from item `at` of `n`, round the ends,
the last's next the first and the first's previous the last, so no arrow
anywhere stops at an end: everything wraps (two items, a Clock's BPM and
HZ, only look like a toggle). An `at` that is none of them enters at the first
going forward and at the last going back; with none at all, -1. The RIG's
scenes and rigs, the variant arrows and a gatherer's strip all step by it. */
inline int stepRound(int at, int by, int n) {
	if (n <= 0)
		return -1;
	if (at < 0 || at >= n)
		return by > 0 ? 0 : n - 1;
	return eucMod(at + by, n);
}

/** A number with as few decimals as it needs, but at least `least` and
at most `most`: 2.5 at one to three is 2.5, and 2.437 is 2.437, so a
reading grows the places a fine drag has put there and no more. */
inline std::string decimals(float v, int least, int most, bool sign = false) {
	int d = least;
	const float scale = std::pow(10.f, (float)most);
	const float exact = std::round(v * scale) / scale;
	while (d < most && std::fabs(std::round(exact * std::pow(10.f, (float)d)) / std::pow(10.f, (float)d) - exact) > 0.5f / scale)
		d++;
	std::string t = string::f(sign ? "%+.*f" : "%.*f", d, exact);
	if (t.size() > 1 && (t[0] == '-' || t[0] == '+') && std::atof(t.c_str() + 1) == 0.f)
		t = (sign ? "+" : "") + t.substr(1);
	return t;
}

/** The RIG's words, the second tier of every control of it (Tips.hpp),
each for the state on screen: what the control does with the modules
to its right, said once and the same way on every part that does it, and no
lesson where the state leaves nothing to do. SAVE's shift-click is said only
while there is a file of the user's to overwrite. */
inline const Gesture G_RIG_OVERWRITE =
    declare(On::Click, Mod::Shift, Rank::Invented, RACK_MOD_SHIFT_NAME "-click: overwrite its file");
/** SAVE's name, one word whether or not there is anything to save, the
dialog it opens saying the rest. */
const char* const RIG_SAVE_NAME = "Save";
/** SAVE before it has a file, and with one, with scenes or without: with
scenes a setting is REPLACE's to light, without them the file is the one
scene and a setting lights SAVE. */
const char* const RIG_SAVE_NEW_LESSON =
    "Saves the modules to its right, with their cables, scenes and note, as a rig file.";
const char* const RIG_SAVE_SCENES_LESSON =
    "Saves the modules to its right, with their cables, scenes and note, as a new rig file. Lit while the "
    "modules, the scenes or the note differ from its file.";
const char* const RIG_SAVE_PLAIN_LESSON =
    "Saves the modules to its right, with their cables and note, as a new rig file. Lit while the modules, "
    "their settings or the note differ from its file.";
const char* const RIG_OVERWRITE_LESSON = "Saves over the file this rig came from, the system asking first.";
const char* const RIG_INSERT_FIRST_LESSON = "Stores the settings of the modules to its right as the first scene.";
const char* const RIG_INSERT_LESSON = "Stores the settings of the modules to its right as a new scene after this one.";
const char* const RIG_REPLACE_LESSON =
    "Writes the settings of the modules to its right over this scene. Pulses while they differ from it.";
const char* const RIG_COPY_LESSON = "Copies this scene, for PASTE on this RIG alone; the copy is not saved with the patch.";
const char* const RIG_PASTE_LESSON = "Adds the copied scene after this one and recalls it";
/** PASTE with shift held, said while there is a scene to paste over. */
inline const Gesture G_RIG_PASTE_OVER =
    declare(On::Click, Mod::Shift, Rank::Invented, RACK_MOD_SHIFT_NAME "-click: paste over this scene");
const char* const RIG_PASTE_OVER_LESSON = "Writes the copied scene over this one and recalls it";
const char* const RIG_PASTE_FIRST_LESSON = "Adds the copied scene as the first and recalls it";
const char* const RIG_FULL_LESSON = "A RIG holds 255 scenes; delete one to make room.";
const char* const RIG_DELETE_LESSON = "Removes this scene and recalls the one now showing";
const char* const RIG_DELETE_LAST_LESSON = "Removes the only scene; the settings stay as they are.";
const char* const RIG_DELETE_PATCHED_LESSON = "Removes this scene; the SCENE jack goes on choosing.";
/** A part with nothing to work on says why at once, in its name: REPLACE,
COPY and DELETE with no scene, PASTE with nothing copied, INSERT and SAVE
with nothing to its right. */
const char* const RIG_NO_SCENES = ": no scenes yet";
const char* const RIG_NOTHING_COPIED = ": nothing copied";
/** INSERT and SAVE on a RIG with no module of ours bonded to its right,
having nothing to keep. */
const char* const RIG_NOTHING_RIGHT = ": nothing to its right";
/** What every part that picks the scene says after its name while SCENE is
patched, an arrow's and the scene screen's, the state as the value, as
`RIG_NO_SCENES` is. */
const char* const RIG_JACK_CHOOSES = ": set by the SCENE jack";
const char* const RIG_TRIGGERS_UNHEARD = "While the SCENE jack chooses the scene, a trigger here does nothing.";
/** A recall by the hand or a trigger restarts every SEQ to the RIG's right
(`resetMolecule`) and the SCENE jack's morph never does: said only where
there is a SEQ there (`rigSeqs`), each lesson above ending in `rigEnd`. */
const char* const RIG_SCENE_IN_LESSON =
    "1 V is the first scene, 2 V the second; between two it morphs, a switch or a stepped setting changing halfway.";
const char* const RIG_SEQS_PLAY_ON = " The SEQs play on.";
const char* const RIG_RESTARTS = ", restarting the SEQs to its right.";
inline std::string rigEnd(const char* lesson, bool seqs) { return std::string(lesson) + (seqs ? RIG_RESTARTS : "."); }
const char* const RIG_PREV_LESSON = "Recalls the scene before, round from the first to the last";
const char* const RIG_NEXT_LESSON = "Recalls the next scene, round from the last to the first";
/** The scene screen's stroke, said on the bracket while there is another
scene to go to and the jack does not choose. */
inline const Gesture G_RIG_SCENE_DRAG = declare(On::Drag, Mod::None, Rank::Invented, "Drag: pick a scene");
/** The scene's screen: what a scene is, then, with another to go to, what
its stroke does that no other screen's does. */
const char* const RIG_SCENE_IS = "A scene holds the settings of the modules to its right, not their cables.";
const char* const RIG_SCENE_DRAG = " A drag recalls each scene it passes.";
const char* const RIG_SCENE_DRAG_SEQS =
    " A drag recalls each scene it passes, and restarts the SEQs to its right where it stops.";
const char* const RIG_STEP_NEXT_LESSON = "Replaces the modules to its right with the next rig in the folder.";
const char* const RIG_STEP_PREV_LESSON = "Replaces the modules to its right with the previous rig in the folder.";
const char* const RIG_FACTORY_LESSON = "Points the arrows and the name's menu at the rigs that come with the plugin.";
const char* const RIG_USER_LESSON =
    "Points the arrows and the name's menu at your own rigs, where SAVE opens. The RIG's menu sets the folder.";

/** A Seq's rate that moves nothing, said at once as its value. */
const char* const SEQ_IDLE_WORD = "idle";
/** A Seq standing alone with nothing in POLY IN has nothing to play: its
step screen says so at once, where its value would be, and the lesson
under it the one patch that wakes it, a lone SEQ being an arpeggiator:
nothing to play yet, and then the simplest way to make it play. */
const char* const SEQ_NOTHING_YET = "Nothing to play yet";
const char* const SEQ_WAKE_LESSON = "Patch a poly cable into POLY IN to sequence through it.";
/** With a POLY to its left the lone SEQ already reads it, so the patch
above is not the way to wake it: the POLY holds no channel yet. */
const char* const SEQ_POLY_EMPTY_LESSON = "The POLY to its left has nothing to play yet.";

/** A lone Seq's POLY IN, the lesson under its name: what to patch there and
what comes out, since the name alone says neither, easy to read before short. */
const char* const SEQ_ARP_LESSON = "The notes to arpeggiate, on a poly cable: each channel is a step, read live so "
                                   "the chord can change as it plays.";
/** The same jack while it is unpatched beside a POLY, which it then plays. */
const char* const SEQ_ARP_POLY_LESSON = "Unpatched, it arpeggiates the POLY to its left. Patch a poly cable to play "
                                        "its channels instead.";

/** A Seq's PHASE jack: its law, and where it counts from, the step the
step screen shows under it (`StepQuantity`); minus ten to ten volts, a turn
each way (`phaseBy`), so negative volts move it back, and past them on
round, a turn every ten volts (`wrapTurn`). */
const char* const SEQ_PHASE_LESSON =
    "0 to 10 V moves the playhead once through the sequence from the step on the screen, and negative volts move it back.";


/** A Clock's CLOCK jack in clock mode: what a pulse in it counts as, since a
sixteenth clock from a DAW otherwise runs every Seq four times too fast with
no word why. A format: its `%s`
takes what a pull leaves, said only while a heard cable is in. Pulses per
quarter note are PPQ, as the panel's caption says them (`CLOCK_PPQ`), in the
tooltip and the menu alike. */
const char* const CLOCK_PPQ = "PPQ";
const char* const CLOCK_JACK_TIP =
    "One trigger a beat sets the tempo%s. For sixteenths, 4PPQ, or 24PPQ, set PPQ in its menu.";
/** A POLY's OUT arpeggiated by a SEQ to its right, or by several. */
const char* const POLY_SEQ_PLAYS = "The SEQ to its right plays these channels in turn.";
const char* const POLY_SEQS_PLAY = "The SEQs to its right play these channels in turn.";
/** A gathered row's name where it came from a stacked cable, the cable
counted in the stacking's order, its channel said where it has several; and
where it is a trigger of a SEQ walking TRGs, the SEQ counted from the left. */
const char* const GATHER_ROW_CABLE = "Cable %d";
const char* const GATHER_ROW_CABLE_CHANNEL = "Cable %d, channel %d";
const char* const GATHER_ROW_TRIGGER = "SEQ %d, trigger %d";
/** A gathered row with nothing in it, a Poly's and a Mix's alike. */
const char* const GATHER_EMPTY_ROW = "Empty.";
/** A gathered row in the grey of a channel replaced (`REPLACED_RGB`): the
CV it came from is fed by a Poly and has stopped following it, taken over by
a drag or by a cable of its own. A Poly's and a Mix's alike, said by what took it over and which POLY
fed it, since hovered on a gatherer "the POLY that feeds it" reads as the
one hovered. */
const char* const GATHER_GREY_ROW = "Grey: this CV's own cable or a drag has taken it over from the POLY to its left.";
/** A gatherer's strip where it shows or steps to a group of a kind
(`Choice`), a format for the kind's name: "group" is the code's word, which
nothing on the panel explains. */
const char* const GATHER_GROUP_WORDS = "A group is %s standing side by side, counted from the left.";
/** What a gatherer's strip says, a Poly's and a Mix's in one wording:
`name` the strip's, beside the kind
gathered now; `arrow` an arrow's, which says only that a click changes what is
gathered, never naming what it goes to before it is seen; and `drag` the
strip's stroke, on its hint only where there is another choice to change to
(a gesture is the hint's, never a lesson's). "Something else", not "another
kind", since the stroke and the arrows step through each group of one kind
as well (`choiceCycle`). The double click back to the nearest is any screen's
default. */
inline const Gesture G_POLY_KIND = declare(On::Drag, Mod::None, Rank::Invented, "Drag: gather something else");
inline const Gesture G_MIX_KIND = declare(On::Drag, Mod::None, Rank::Invented, "Drag: mix in something else");
struct GatherStripWords {
	const char* name;
	const char* arrow;
	const Gesture* drag;
};
inline const GatherStripWords& gatherStripWords(bool mix) {
	static const GatherStripWords POLY = {"Gathers", "Gather something else", &G_POLY_KIND};
	static const GatherStripWords MIX = {"Mixes in", "Mix in something else", &G_MIX_KIND};
	return mix ? MIX : POLY;
}
/** A rig's note, its button beside SAVE: where it is kept,
where it shows, and the key that closes it besides the button. */
const char* const RIG_NOTE_LESSON =
    "Saved in the rig file, and shown beside the name while the pointer rests on it. Escape closes it too.";

} // namespace rig
