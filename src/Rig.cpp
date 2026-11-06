// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Panel.hpp"
#include "Scenes.hpp"
#include "HeadBase.hpp"
#include "NoteRows.hpp"
#include "JsonFile.hpp"
#include <osdialog.h>
#include <algorithm>

/** RIG: a molecule saved and loaded, and its scenes.

It stands at the left of a molecule, the bond of ours to its right, and nothing
bonds to its left (`runOf`). It does two jobs, top down on the panel. The rig:
the whole molecule, its modules, their settings and the cables between them and
between them and the RIG, saved to a file and loaded back, the old molecule
taken away and the new one built in its place as one step of undo and reset as
the bracket's RESET resets it. The rigs live in two places, shown by the two
icons at the top of the rig's screen: the factory's, which come with the plugin
and are never written, and the user's own folder, Atomic/rigs in Rack's user
folder unless another is set from the right-click menu (red and crossed while a
chosen one is gone). A click on an icon shows that place, the one lit, and a
click on the name lists the rigs in it as a menu to load one from, Rack's own
file dialog the last item; the arrows above and below the name load the next or
the previous rig in it at once. A rig file is Rack's own selection file (.vcvs),
the modules and the cables between them as Rack writes them, with the scenes and
the cables to the RIG's own jacks beside them, so Rack's Import selection opens
one too, scenes and the RIG's cables left behind. SAVE always asks where,
opening in the user's folder, a new file each time; it reads SAVED, dark, while
the rig is what its file holds, and SAVE, lit, once the kinds of its modules
left to right or its scenes are not (a param, a step or a cable is playing the
rig, not changing it). A rig is loaded and saved as its scene showing. Each rig
carries one note, saved in its file, its button carved off SAVE's row; it is
read and written on glass carrying the name's screen on to the RIG's left, and
an edit of it is a change to the rig, which SAVE shows.

The scenes: the settings of the molecule as it stands, kept by each module's
identity, recalled in the engine on the sample (Scenes.hpp). The screen under
SAVE shows the scene's number with an arrow either side, and a stroke up or
down it walks the scenes as a Seq's step screen walks its steps. Under it the
hand's four rows: REPLACE the scene showing with the molecule (breathing while
they differ), INSERT a new scene after the one showing, COPY a scene and PASTE
it in as a new one after the one showing, DELETE the scene showing. A change of scene resets the molecule. SCENE is a CV:
a whole number of volts is that scene, between two whole numbers the two are
mixed. PREV and NEXT step through the scenes, round and round.

A GAP carries the molecule on past it, so one RIG holds the sections on both
sides of it, saved as one rig with one scene system.

Everything here but the engine is the UI thread's: the molecule is worked out
from where the panels stand (`molecule`), and a load or a save is Rack's own
API, made from a click. */

// Rack's own lookup of a model by its slugs. Declared here rather than by
// including Rack's plugin.hpp, which a headless build's include path shadows
// with ours.
namespace rack {
namespace plugin {
Model* getModel(const std::string& pluginSlug, const std::string& modelSlug);
}
} // namespace rack

using namespace rig;

namespace {

// The RIG's own width, the name read upright.
const float RIG_W = PANEL_W;
const float RIG_CX = CX;
// One column of ground from the stock top edge down to the air over SCENE,
// where a CRV's screen closes, each piece touching the next and each breaking
// where its neighbours' ground breaks, so bonded beside any member every piece
// stands within a piece beside it, or holds one within itself, and the run's
// picture reaches it across the shared pixel column (`runPicture`'s bleed).
// Top down: the rig's glass, its folder, its name between its arrows and SAVE
// on its foot; the scene's glass,
// a Seq's screen tall and touching the rig's as a Seq's two screens touch,
// its number between its arrows, closing where an Atn's screen does; then the
// hand's strip, its four rows REPLACE, INSERT, COPY and PASTE, and DELETE,
// closing where a CRV's screen does. Every button is a quarter of the hand,
// SAVE too, so all five rows are one button in size.
const float NAME_Y = SCREEN_Y;
const float STRIP_H = HAND_ROW_H; // HeadBase.hpp, the GAP's buttons the same
const float HAND_Y = HAND_TOP;
const float SCENE_Y = HAND_Y - HEAD_SCREEN_H;
const float NAME_H = SCENE_Y - NAME_Y;
// SAVE stands on the glass's working face, the bevel under it, so its light
// is a whole row tall as every row of the hand's is.
const float SAVE_Y = SCENE_Y - GLASS_BEVEL / mm2px(1.f) - STRIP_H;
const float ARROW_W = 3.f; // the strip at each side of the scene's number an arrow stands in
const float ARROW_H = 5.f; // and the band above and below the rig's name
// The air an arrow leaves at each end of its band, and so the air over the
// folder too.
const float ARROW_AIR = (ARROW_H - ARROW_LEN) / 2.f;
const float FOLDER_INK = 3.f;                  // the folder's icon, top to foot
const float FOLDER_H = ARROW_AIR + FOLDER_INK; // and its band, the air over it with it
// The air SAVE's word keeps over its ink inside its row, kept under the
// folder as well, so the up arrow stands as far under the folder's ink as the
// down arrow stands over SAVE's.
const float WORD_AIR = (STRIP_H - HEAD_STRIP_PX * PLATE_INK / mm2px(1.f)) / 2.f;
const float UP_Y = FOLDER_H + WORD_AIR;          // the up arrow's band, down the name's screen
const float DOWN_Y = SAVE_Y - NAME_Y - ARROW_H; // and the down arrow's, over SAVE
// The rig's note: its button a square carved off SAVE's row at its left, the
// note's sheet inside it with the row's own air round it, and the glass it is
// read and written on carrying the name's screen on to the RIG's left, the
// words held off the glass's edge.
const float NOTE_BTN_W = STRIP_H;
const float NOTE_GLYPH = STRIP_H - 2.f * WORD_AIR;
const float NOTE_W = 10 * 5.08f; // the glass's width, ten HP
const float NOTE_INSET = 1.5f;
const char* const RIG_EXT = "vcvs";
const float NAME_PX = 10.f;      // the rig's name, read upright
const float NAME_PITCH = 10.f;   // from one of its lines to the next, across the screen, in px
const float NAME_SIDE_AIR = 0.5f; // the air its outer lines keep from the screen's sides

using Rig = RigCore;

/** The two places a RIG browses, each shown by an icon at the top of the name's screen: the factory's rigs,
which come with the plugin in its own rigs folder, are replaced on every
update and are never written by a RIG; and the user's, in a folder of their
own, by default Atomic/rigs in Rack's user folder beside Rack's own settings
(as a plugin's presets live under Rack's user folder), made on the first save,
so a fresh RIG wants no setting up. Another user folder is set from the
right-click menu and kept in Atomic-rig.json; empty there is the default. A rig
opened from anywhere else is browsed in its own folder, neither icon lit. */
enum Place { FACTORY, USER, ELSEWHERE };

/** In a rig file's `rigCables`, the module id that names the RIG saving or
loading it: Rack's ids are never negative. */
const int64_t RIG_END = -1;
/** A rig file's modules in the order they stood, left to right. */
std::vector<json_t*> fileModules(json_t* rootJ) {
	std::vector<json_t*> ms;
	size_t i;
	json_t* mJ;
	json_array_foreach(json_object_get(rootJ, "modules"), i, mJ) ms.push_back(mJ);
	auto gx = [](json_t* j) { return json_integer_value(json_array_get(json_object_get(j, "pos"), 0)); };
	std::stable_sort(ms.begin(), ms.end(), [&](json_t* a, json_t* b) { return gx(a) < gx(b); });
	return ms;
}
/** The ids by which a rig file's cables and scenes can name its modules:
an id written on one module alone. A module with no id, or one sharing its id
with another, is still built, but nothing in the file reaches it, since a
cable or a scene naming that id names no one module. */
std::set<int64_t> namedIds(const std::vector<json_t*>& ms) {
	std::map<int64_t, int> seen;
	for (json_t* mJ : ms) {
		json_t* idJ = json_object_get(mJ, "id");
		if (json_is_integer(idJ) && json_integer_value(idJ) >= 0)
			seen[json_integer_value(idJ)]++;
	}
	std::set<int64_t> out;
	for (const auto& kv : seen)
		if (kv.second == 1)
			out.insert(kv.first);
	if (out.size() < ms.size())
		WARN("RIG: %d of the file's %d modules have no id of their own, and no cable or scene reaches them",
		     (int)(ms.size() - out.size()), (int)ms.size());
	return out;
}
/** Module `mJ`'s id where the file names it truly, else none. */
bool namedId(json_t* mJ, const std::set<int64_t>& named, int64_t& id) {
	id = json_integer_value(json_object_get(mJ, "id"));
	return json_is_integer(json_object_get(mJ, "id")) && named.count(id);
}
/** How many rig files a RIG has written since the plugin loaded: a
comparison with a file that failed is tried again only once this moves. */
uint64_t& rigFilesWritten() {
	static uint64_t n = 0;
	return n;
}
std::string factoryFolder() { return asset::plugin(pluginInstance, "rigs"); }
std::string defaultUserFolder() { return asset::user("Atomic/rigs"); }
std::string settingsPath() { return asset::user("Atomic-rig.json"); }
/** The user folder set from the menu, or empty for the default. */
std::string& chosenFolder() {
	static std::string folder;
	static bool read = false;
	if (!read) {
		read = true;
		if (json_t* rootJ = loadJson(settingsPath())) {
			if (const char* f = json_string_value(json_object_get(rootJ, "folder")))
				folder = f;
			json_decref(rootJ);
		}
	}
	return folder;
}
/** The user folder set, and kept in the settings file at once; the default
is kept as empty, so it follows Rack's user folder wherever that moves. */
void chooseFolder(std::string folder) {
	if (folder == defaultUserFolder())
		folder.clear();
	chosenFolder() = folder;
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "folder", json_string(folder.c_str()));
	saveJson(rootJ, settingsPath(), JSON_INDENT(2));
	json_decref(rootJ);
}
std::string userFolder() { return chosenFolder().empty() ? defaultUserFolder() : chosenFolder(); }
/** A user folder set from the menu that is no longer there: the one state
drawn red and crossed. The default is never missing, only not made yet. */
bool userMissing() { return !chosenFolder().empty() && !system::isDirectory(chosenFolder()); }
/** Whether `path` is `dir` or stands anywhere inside it. A folder not on
disk holds nothing: its canonical form is empty, which every path would
otherwise start with (the user's folder before its first save, which once
claimed every rig opened from elsewhere). A file not made yet, a save's
new name, has no canonical form either, nor has a folder a save is about to
make, so it is placed by the nearest folder above it that is on disk, the
names below that kept as they are (a dialog's answer is absolute and holds
no ".."); placed by its own folder alone, a save into a new folder inside
the factory's was let by. */
bool within(const std::string& path, const std::string& dir) {
	if (path.empty() || dir.empty())
		return false;
	std::string p = system::getCanonical(path);
	std::string rest;
	for (std::string up = path; p.empty();) {
		const std::string parent = system::getDirectory(up);
		if (parent.empty() || parent == up)
			break;
		rest = rest.empty() ? system::getFilename(up) : system::join(system::getFilename(up), rest);
		up = parent;
		const std::string folder = system::getCanonical(up);
		if (!folder.empty())
			p = system::join(folder, rest);
	}
	const std::string d = system::getCanonical(dir);
	if (p.empty() || d.empty())
		return false;
	return p == d || (p.size() > d.size() && p.compare(0, d.size(), d) == 0 && (p[d.size()] == '/' || p[d.size()] == '\\'));
}

/** A path the test rig gave in place of a dialog's answer, taken once
(`dialogAnswer`); empty when there is none. */
std::string takeAnswer() {
	std::string a = dialogAnswer();
	dialogAnswer().clear();
	return a;
}

/** A file a RIG can load: Rack's selection file, by its extension. */
bool isRigFile(const std::string& path) {
	return system::isFile(path) && string::lowercase(system::getExtension(path)) == std::string(".") + RIG_EXT;
}
/** What a folder holds, sorted as a person reads a list, folders first. */
void listFolder(const std::string& dir, std::vector<std::string>& dirs, std::vector<std::string>& files) {
	if (dir.empty() || !system::isDirectory(dir))
		return;
	for (const std::string& e : system::getEntries(dir)) {
		const std::string name = system::getFilename(e);
		if (name.empty() || name[0] == '.')
			continue;
		if (system::isDirectory(e))
			dirs.push_back(e);
		else if (isRigFile(e))
			files.push_back(e);
	}
	auto byName = [](const std::string& a, const std::string& b) {
		return string::lowercase(system::getFilename(a)) < string::lowercase(system::getFilename(b));
	};
	std::sort(dirs.begin(), dirs.end(), byName);
	std::sort(files.begin(), files.end(), byName);
}
/** Every rig under `dir` in the order the arrows walk it: its own rigs by
name, then each folder by name and the whole of it, its own rigs and then its
folders, before the next, so an
arrow goes round only past the last rig of the last folder. A folder linked
into itself would never end, so the walk stops eight folders deep. */
void gatherRigs(const std::string& dir, std::vector<std::string>& out, int depth = 0) {
	std::vector<std::string> dirs, files;
	listFolder(dir, dirs, files);
	out.insert(out.end(), files.begin(), files.end());
	if (depth < 8)
		for (const std::string& d : dirs)
			gatherRigs(d, out, depth + 1);
}

/** A module as Rack writes it, its params set to `valuesJ`, a scene's values
for it by param id; a param the scene does not hold keeps its own. A rig file
holds a molecule twice, once as each module's own params and once as its
scenes, and the scene showing is the one that wins: a rig is loaded and saved
as its scene showing, so the file agrees with itself and Rack's Import
selection builds the same molecule (where the params alone once built a
Clock at 120 until its scene, holding 59, was picked). */
void sceneIntoParams(json_t* mJ, json_t* valuesJ) {
	if (!json_is_array(valuesJ))
		return;
	size_t i;
	json_t* pJ;
	json_array_foreach(json_object_get(mJ, "params"), i, pJ) {
		json_t* idJ = json_object_get(pJ, "id");
		json_t* vJ = json_array_get(valuesJ, idJ ? (size_t)json_integer_value(idJ) : i);
		if (json_is_number(vJ))
			json_object_set_new(pJ, "value", json_real(json_number_value(vJ)));
	}
}

/** And its scene settings, past its params in `valuesJ`, set on `m` once it
is built from its JSON, which holds the module's own: a load is the scene it
shows, BLEP and Oversampling as much as the knobs. A scene stored before the
settings were stops at the params and leaves the module's own. */
void sceneIntoSettings(Module* m, json_t* valuesJ) {
	Member* rm = sceneMember(m);
	if (!rm || !json_is_array(valuesJ))
		return;
	const size_t n = sceneValues(m, rm);
	for (size_t i = m->params.size(); i < n; i++) {
		json_t* vJ = json_array_get(valuesJ, i);
		if (json_is_number(vJ))
			setSceneValue(m, rm, i, (float)json_number_value(vJ));
	}
}

/** One step of undo for everything a RIG holds that Rack's own history does
not: the scenes, the scene showing and the rig file. A load pushes one after
the modules and cables it changed, an INSERT or a REPLACE one alone. */
struct RigChange : history::ModuleAction {
	json_t* oldJ = nullptr;
	json_t* newJ = nullptr;
	~RigChange() {
		if (oldJ)
			json_decref(oldJ);
		if (newJ)
			json_decref(newJ);
	}
	void put(json_t* j) {
		if (Rig* s = dynamic_cast<Rig*>(APP->engine->getModule(moduleId)))
			s->setState(j);
	}
	void undo() override { put(oldJ); }
	void redo() override { put(newJ); }
};

/** The RIG's state as it was, `oldJ`, and as it is now, one step of undo
named `name`; `oldJ` is the step's to keep. */
void pushRigChange(Rig* rig, json_t* oldJ, const char* name) {
	RigChange* c = new RigChange;
	c->name = name;
	c->moduleId = rig->id;
	c->oldJ = oldJ;
	c->newJ = rig->stateJson();
	APP->history->push(c);
}

struct RigWidget;

/** The scene showing, as a step of undo: a hand's recall puts its params
back through Rack's own ParamChange, and the number with this. */
struct SceneShown : history::ModuleAction {
	int from = 0, to = 0;
	void put(int at) {
		if (Rig* s = dynamic_cast<Rig*>(APP->engine->getModule(moduleId)))
			s->show(at);
	}
	void undo() override { put(from); }
	void redo() override { put(to); }
};

/** A scene setting a hand's recall moved (`Member::sceneSettings`, a
Seq's BLEP), as a step of undo: Rack's ParamChange for what is no param. */
struct SceneSettingChange : history::ModuleAction {
	int setting = 0;
	float from = 0.f, to = 0.f;
	void put(float v) {
		if (Member* m = dynamic_cast<Member*>(APP->engine->getModule(moduleId))) {
			m->setSceneSetting(setting, v);
			m->wake();
		}
	}
	void undo() override { put(from); }
	void redo() override { put(to); }
};

/** Value `i` of `m` in a scene moved from `from` to `to`, into `h` as a step
of undo: a param through Rack's own ParamChange, a scene setting past the
params through `SceneSettingChange`. */
void pushSceneValue(history::ComplexAction* h, Module* m, size_t i, float from, float to) {
	if (i < m->params.size()) {
		history::ParamChange* pc = new history::ParamChange;
		pc->moduleId = m->id;
		pc->paramId = (int)i;
		pc->oldValue = from;
		pc->newValue = to;
		h->push(pc);
		return;
	}
	SceneSettingChange* sc = new SceneSettingChange;
	sc->moduleId = m->id;
	sc->setting = (int)(i - m->params.size());
	sc->from = from;
	sc->to = to;
	h->push(sc);
}

/** The scene's number: two cells, the yellow of a readout, ghosts only
while there are no scenes, between its two arrows (RigWidget); with a cable
in SCENE, a bar along its foot says where the jack stands between the first
scene and the last. */
struct SceneScreen : Screen {
	Rig* module = nullptr;
	void drawLayer(const DrawArgs& args, int layer) override {
		Screen::drawLayer(args, layer);
		if (layer != 1)
			return;
		int shown = 0, n = 1;
		bool patched = false;
		float pos = 0.f;
		if (module) {
			shown = relaxed(module->currentNow);
			n = relaxed(module->countNow);
			patched = relaxed(module->patchedNow);
			pos = relaxed(module->posNow);
		}
		// The number wears the SCENE cable's colour while that cable picks
		// it; PREV and NEXT are only pushes on the arrows and leave it alone.
		// Centred on the screen, which centres it between the arrows.
		const NVGcolor ink = patched && module ? modColor(relaxed(module->sceneRGB), SCHEME_YELLOW) : SCHEME_YELLOW;
		// Decimal to 99 and hex past it with its mark, to FF, the most a RIG
		// holds (`MAX_SCENES`, `drawCount`).
		drawCount(args.vg, box.size, n > 0 ? shown + 1 : -1, ink, 2);
		if (patched && n > 1)
			drawFootBar(args.vg, box.size, pos / (float)(n - 1), ink, false);
	}
};

/** The rig's name, the file's own without its extension, read upright
between its two arrows, dim words where no rig is held. A click on it lists
the rigs in the folder as a menu, a pick loading one, or, with no folder,
asks for one first. It has no tooltip:
the rig's note, peeking at its left while the pointer is on it, is the only
thing a hover shows. */
struct NameScreen : Hovered<Screen> {
	RigWidget* owner = nullptr;
	void onEnter(const EnterEvent& e) override;
	void onLeave(const LeaveEvent& e) override;
	/** A screen is no opaque widget, so it says it is under the pointer
	itself, for its enter and its leave. */
	void onHover(const HoverEvent& e) override {
		Screen::onHover(e);
		if (!e.isConsumed())
			e.consume(this);
	}
	void onButton(const ButtonEvent& e) override;
	void onDoubleClick(const DoubleClickEvent& e) override { e.consume(this); }
	void drawLayer(const DrawArgs& args, int layer) override;
	/** The name as it was last laid out, and its lines, worked out again only
	when the name changes. */
	std::string laidText;
	std::vector<std::string> laidLines;
};

/** A name laid out in at most `most` lines no wider than `room`, as `width`
measures them: one line where it fits,
else the fewest lines that do, broken between words where it can and the
breaks chosen so the longest line is as short as it can be, which keeps the
lines alike and the whole balanced about the middle; a word too long for a
line of its own is broken inside, with a hyphen; and past the last line an
ellipsis. */
std::vector<std::string> layName(const std::string& text, float room, int most,
                                 const std::function<float(const std::string&)>& width) {
	std::vector<std::string> words;
	{
		size_t at = 0;
		while (at < text.size()) {
			size_t sp = text.find(' ', at);
			if (sp == std::string::npos)
				sp = text.size();
			if (sp > at)
				words.push_back(text.substr(at, sp - at));
			at = sp + 1;
		}
	}
	if (words.empty())
		return {text};
	const int n = (int)words.size();
	auto join = [&](int a, int b) {
		std::string l;
		for (int k = a; k < b; k++)
			l += (k > a ? " " : "") + words[k];
		return l;
	};
	// Between words: every way of cutting the words into `lines` runs, the
	// one whose widest line is narrowest kept.
	for (int lines = 1; lines <= std::min(most, n); lines++) {
		std::vector<int> cut(lines + 1);
		std::vector<std::string> best;
		float bestW = INFINITY;
		std::function<void(int, int)> walk = [&](int line, int from) {
			if (line == lines - 1) {
				cut[line] = from;
				cut[lines] = n;
				float wide = 0.f;
				std::vector<std::string> ls;
				for (int k = 0; k < lines; k++) {
					ls.push_back(join(cut[k], cut[k + 1]));
					wide = std::max(wide, width(ls.back()));
				}
				if (wide <= room && wide < bestW) {
					bestW = wide;
					best = ls;
				}
				return;
			}
			cut[line] = from;
			for (int to = from + 1; to <= n - (lines - 1 - line); to++) {
				cut[line + 1] = to;
				walk(line + 1, to);
			}
		};
		walk(0, 0);
		if (!best.empty())
			return best;
	}
	// Inside words: the lines filled in turn, a word that does not fit on
	// the line it begins broken where the line ends, with a hyphen.
	std::vector<std::string> out;
	std::string line;
	bool unplaced = false;
	for (int k = 0; k < n && (int)out.size() < most; k++) {
		std::string w = words[k];
		while (!w.empty() && (int)out.size() < most) {
			const std::string tryL = line.empty() ? w : line + " " + w;
			if (width(tryL) <= room) {
				line = tryL;
				w.clear();
				break;
			}
			// What of the word fits on this line, with its hyphen.
			const std::string lead = line.empty() ? std::string() : line + " ";
			size_t fit = 0;
			while (fit + 1 < w.size() && width(lead + w.substr(0, fit + 1) + "-") <= room)
				fit++;
			if (fit < 2 && !line.empty()) {
				// Too little to be worth breaking here: the word goes to the next line.
				out.push_back(line);
				line.clear();
				continue;
			}
			fit = std::max(fit, (size_t)1);
			out.push_back(lead + w.substr(0, fit) + "-");
			line.clear();
			w = w.substr(fit);
		}
		if (!w.empty()) {
			// Out of lines with the word unplaced.
			unplaced = true;
			break;
		}
	}
	const bool cutShort = unplaced || ((int)out.size() >= most && !line.empty());
	if (!line.empty() && (int)out.size() < most)
		out.push_back(line);
	if (cutShort && !out.empty()) {
		std::string& last = out.back();
		while (last.size() > 1 && width(last + "...") > room)
			last.pop_back();
		if (!last.empty() && last.back() == '-')
			last.pop_back();
		last += "...";
	}
	return out;
}

/** The two places at the top of the name's screen are glyph buttons
(`addGlyphButton`): the factory, a small works with a saw-tooth roof
(`paintFactory`), and the user's own folder (`paintFolder`). The one lit is
the place showing, which the arrows and the name's menu walk; a click shows
the other. The user's is drawn red and crossed only while a folder set from
the menu has gone missing. */
const float PLACE_ICON_W = 3.6f; // mm, the icons' width

/** The RIG's buttons, on flat strips of the column, each a gesture of the
hand answered on the UI thread the moment it is pressed. One that `breathes`
breathes as a Seq's HALT does (`breathing`) while that says so, a replace
wanted, and one `lit` is lit steadily while that says so, as SAVE is; one that is not `live`
has its word faint and takes no press, there being nothing for it to do. */
struct RigButton : ScreenButton {
	std::function<bool()> breathes;
	std::function<bool()> live;
	RigButton() {
		momentary = true;
		colorOf = [=]() { return !live || live() ? SCHEME_YELLOW : color::alpha(SCHEME_YELLOW, 0.35f); };
	}
	std::function<bool()> lit; // lit steadily while it says so, as a switch that is on
	float glow() override {
		if (lit && lit())
			return 1.f;
		if (breathes && breathes())
			return std::max(breathing(), flash());
		return ScreenButton::glow();
	}
	/** INSERT's mark: the scene's right arrow after the word, the two
	centred together, saying the new scene goes after the one showing. */
	bool arrowAfter = false;
	void drawWord(const DrawArgs& args, const std::string& word, NVGcolor col) override {
		std::shared_ptr<Font> font = plateFont();
		if (!arrowAfter || !font) {
			ScreenButton::drawWord(args, word, col);
			return;
		}
		NVGcontext* vg = args.vg;
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, px);
		nvgTextLetterSpacing(vg, 0.4f);
		float b[4];
		const float tw = nvgTextBounds(vg, 0.f, 0.f, word.c_str(), nullptr, b);
		const float gap = mm2px(0.8f), aw = mm2px(ARROW_LEN);
		const float x0 = (box.size.x - (tw + gap + aw)) / 2.f, cy = box.size.y / 2.f;
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, col);
		nvgText(vg, x0, cy, word.c_str(), nullptr);
		paintArrow(vg, x0 + tw + gap + aw / 2.f, cy, ARROW_RIGHT, col);
	}
	/** A faint rule down its left edge, parting it from the button beside
	it in one row: PASTE from COPY, SAVE from the note's. */
	bool ruleLeft = false;
	void draw(const DrawArgs& args) override {
		ScreenButton::draw(args);
		if (!ruleLeft)
			return;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.f, box.size.y * 0.2f, 1.f, box.size.y * 0.6f);
		nvgFillColor(args.vg, color::alpha(color::WHITE, 0.12f));
		nvgFill(args.vg);
	}
	void onButton(const ButtonEvent& e) override {
		if (live && !live() && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
			return;
		}
		ScreenButton::onButton(e);
	}
};

/** COPY and PASTE, the two halves of one row: an icon each where the words
would not fit, two sheets for COPY and a clipboard for PASTE, and a faint
rule between them (`ruleLeft`). */
struct IconButton : RigButton {
	bool paste = false;
	void drawWord(const DrawArgs& args, const std::string& word, NVGcolor col) override {
		NVGcontext* vg = args.vg;
		const float cx = box.size.x / 2.f, cy = box.size.y / 2.f;
		nvgStrokeColor(vg, col);
		nvgStrokeWidth(vg, mm2px(0.25f));
		nvgLineJoin(vg, NVG_ROUND);
		if (!paste) {
			// Two sheets, the one behind showing above and to the right of
			// the one in front.
			const float sz = mm2px(1.8f), o = mm2px(0.7f);
			const float fx = cx - (sz + o) / 2.f, fy = cy - (sz + o) / 2.f + o, bx = fx + o, by = fy - o;
			nvgBeginPath(vg);
			nvgMoveTo(vg, bx, fy);
			nvgLineTo(vg, bx, by);
			nvgLineTo(vg, bx + sz, by);
			nvgLineTo(vg, bx + sz, by + sz);
			nvgLineTo(vg, fx + sz, by + sz);
			nvgStroke(vg);
			nvgBeginPath(vg);
			nvgRect(vg, fx, fy, sz, sz);
			nvgStroke(vg);
			return;
		}
		// A clipboard: the board, and its clip across the top edge.
		const float w = mm2px(2.f), h = mm2px(2.5f), cw = mm2px(1.f), ch = mm2px(0.6f);
		const float x0 = cx - w / 2.f, y0 = cy - h / 2.f + ch / 4.f;
		nvgBeginPath(vg);
		nvgRect(vg, x0, y0, w, h);
		nvgStroke(vg);
		nvgBeginPath(vg);
		nvgRect(vg, cx - cw / 2.f, y0 - ch / 2.f, cw, ch);
		nvgFillColor(vg, col);
		nvgFill(vg);
	}
};

/** The scene's number dragged, as a Seq's step screen is dragged: up a
later scene, down an earlier one, `PX_PER_POS` a scene, stopping at the
first and the last with the travel past them folded away, so turning back
moves at once. Each scene the stroke
lands on is recalled as it lands, so it is heard; the molecule is reset once,
where the stroke ends, and the stroke is one step of undo from the scene it
began on. A click and a double click change nothing; nothing at all while
SCENE is patched, the jack being in charge, or while there are no scenes.
No param: the scene showing is the RIG's own state, not a knob's. */
struct SceneDrag : TipHovered<VerticalDragParam>, HintSource {
	RigWidget* owner = nullptr;
	/** Its stroke on the bracket, while there is another scene to go to and
	the jack does not choose; nothing otherwise. */
	const Hinted* hinted() override;
	bool dragging = false;
	int from = 0, at = 0;
	// The molecule as the stroke began, held by id: an undo in the middle of
	// the stroke may take a member away (`HeldModule`).
	std::vector<HeldModule> strokeMolecule;
	std::vector<std::vector<float>> before; // the molecule's params and scene settings as the stroke began
	/** The members of the stroke's molecule that still stand. */
	std::vector<Module*> standing() const {
		std::vector<Module*> out;
		for (const HeldModule& h : strokeMolecule)
			if (Module* m = h.get())
				out.push_back(m);
		return out;
	}
	SceneDrag() { pxPerUnit = PX_PER_POS; }
	std::string hoverTip() override;
	/** The left button is the stroke's; the right goes on to the module's
	own menu, there being no param here to have one. */
	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
			return;
		}
		Widget::onButton(e);
	}
	void onDoubleClick(const DoubleClickEvent& e) override { e.consume(this); }
	void onDragStart(const DragStartEvent& e) override;
	void strokeMove(const DragMoveEvent& e) override;
	void onDragEnd(const DragEndEvent& e) override;
};

Rig* rigOf(int64_t id) {
	return dynamic_cast<Rig*>(APP->engine->getModule(id));
}

/** The rig's note, read and written on glass that carries the rig's name
screen on to its left: the
same height, the same top line and the same face, so the two read as one
screen, with nothing behind the words but the glass. A click on the note's
button opens it to write in and it stays open until the button is clicked
again or Escape is pressed, the edit one step of the RIG's undo, a change to
the rig as a scene stored is, so SAVE shows it; while the pointer rests on
the rig's name a written note shows there to read, and nothing takes it. It
stands over the rack rather than in the RIG, a module drawing nothing past its
own panel, so it follows the name screen wherever the rack scrolls or zooms. */
struct NoteSheet;
NoteSheet* noteOpen = nullptr; // the one note being written, if any
NoteSheet* notePeek = nullptr; // and the one being read off a hovered name
struct NoteField : LedDisplayTextField {
	int64_t id = -1;
	NoteSheet* sheet = nullptr;
	// The words only: what a field puts behind them is the glass's.
	void draw(const DrawArgs& args) override {}
	void onChange(const ChangeEvent& e) override {
		if (Rig* r = rigOf(id))
			r->note = getText();
		LedDisplayTextField::onChange(e);
	}
	void onSelectKey(const SelectKeyEvent& e) override;

	// ---- The rows (NoteRows.hpp): the glass wraps the note into them, and
	// the caret is drawn on them and walked over them, one model for both,
	// where Rack's field walks the newlines alone.
	static constexpr float FONT_SIZE = 12.f; // what the stock field draws at
	std::vector<note::Line> rows;
	std::string rowsOf; // the text the rows were found for
	float rowsW = -1.f;  // and the width
	int goal = -1;       // the column a run of ups and downs keeps
	int goalAt = -1;     // the caret it was left at, so any other move forgets it
	/** Where the words stand in the field: the stock field's box, inside
	which blendish pads by its text radius, as `bndIconLabelCaret` does. */
	float wordsX() { return textOffset.x + BND_TEXT_RADIUS; }
	float baseline() { return textOffset.y + BND_WIDGET_HEIGHT - BND_TEXT_PAD_DOWN; }
	float wordsW() { return box.size.x - 2 * textOffset.x - 2 * BND_TEXT_RADIUS; }
	bool font(NVGcontext* vg) {
		std::shared_ptr<window::Font> f = APP->window->loadFont(fontPath);
		if (!f || f->handle < 0)
			return false;
		nvgFontFaceId(vg, f->handle);
		nvgFontSize(vg, FONT_SIZE);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		return true;
	}
	/** The rows, broken as the words are drawn (`nvgTextBreakLines` at the
	same font, size and width), in as many calls as a long note takes. */
	void breakRows(NVGcontext* vg) {
		rows.clear();
		rowsOf = text;
		rowsW = wordsW();
		const char* t = text.c_str();
		const char* at = t;
		const char* e = t + text.size();
		NVGtextRow got[16];
		while (at < e) {
			const int n = nvgTextBreakLines(vg, at, e, rowsW, got, 16);
			if (n <= 0)
				break;
			for (int i = 0; i < n; i++)
				rows.push_back({(int)(got[i].start - t), (int)(got[i].end - t)});
			at = got[n - 1].next;
		}
		note::close(text, rows);
	}
	/** The rows of the text as it stands: those the last frame drew when the
	text is the same, else found now, between frames, as a key typed and an
	arrow pressed before a frame need. */
	void ensureRows() {
		if (rowsOf == text && rowsW == wordsW() && !rows.empty())
			return;
		NVGcontext* vg = APP->window->vg;
		nvgSave(vg);
		if (font(vg))
			breakRows(vg);
		nvgRestore(vg);
	}
	/** The caret at byte `pos`: its row and its left edge, in the field. */
	void caretAt(NVGcontext* vg, int pos, int& r, float& x) {
		r = note::rowOf(rows, pos);
		const int start = rows.empty() ? 0 : rows[r].start;
		x = wordsX() + (pos > start ? nvgTextBounds(vg, 0.f, 0.f, text.c_str() + start, text.c_str() + pos, nullptr) : 0.f);
		x = std::min(x, wordsX() + wordsW());
	}
	/** The caret, or the selection, drawn on the rows, the stock field's
	colours and shapes, under the words. */
	void drawCaret(NVGcontext* vg) {
		float asc = 0.f, desc = 0.f, lh = 0.f;
		nvgTextMetrics(vg, &asc, &desc, &lh);
		const int a = std::min(cursor, selection), b = std::max(cursor, selection);
		int ra = 0, rb = 0;
		float xa = 0.f, xb = 0.f;
		caretAt(vg, a, ra, xa);
		caretAt(vg, b, rb, xb);
		const float top = baseline() - lh - desc;
		const float ya = top + ra * lh, yb = top + rb * lh;
		const float x0 = wordsX(), x1 = wordsX() + wordsW();
		nvgBeginPath(vg);
		if (a == b) {
			nvgFillColor(vg, nvgRGBf(0.337, 0.502, 0.761));
			nvgRect(vg, xa - 1, ya, 2, lh + 1);
		} else {
			NVGcolor lit = color;
			lit.a = 0.5;
			nvgFillColor(vg, lit);
			if (ra == rb) {
				nvgRect(vg, xa - 1, ya, xb - xa + 1, lh + 1);
			} else {
				nvgRect(vg, xa - 1, ya, x1 - xa + 1, lh + 1);
				nvgRect(vg, x0, yb, xb - x0 + 1, lh + 1);
				if (rb - ra > 1)
					nvgRect(vg, x0, ya + lh, wordsW(), (rb - ra - 1) * lh + 1);
			}
		}
		nvgFill(vg);
	}
	/** The stock field's drawing, its caret drawn on the rows instead: the
	stock caret stands on blendish's own reckoning of the row, which put a
	caret after a closing newline at the end of the line before it. */
	void drawLayer(const DrawArgs& args, int layer) override {
		nvgScissor(args.vg, RECT_ARGS(args.clipBox));
		std::shared_ptr<window::Font> f = layer == 1 ? APP->window->loadFont(fontPath) : nullptr;
		if (f && f->handle >= 0) {
			nvgSave(args.vg);
			font(args.vg);
			breakRows(args.vg);
			if (this == APP->event->selectedWidget)
				drawCaret(args.vg);
			nvgRestore(args.vg);
			bndSetFont(f->handle);
			// The words alone: blendish draws no caret where its end is
			// before its start.
			bndIconLabelCaret(args.vg, textOffset.x, textOffset.y, box.size.x - 2 * textOffset.x,
			                  box.size.y - 2 * textOffset.y, -1, color, FONT_SIZE, text.c_str(), color, 0, -1);
			bndSetFont(APP->window->uiFont->handle);
		}
		Widget::drawLayer(args, layer);
		nvgResetScissor(args.vg);
	}
	/** A key that walks the caret over the rows (`Hand::caret`), or false
	for Rack's own: up and down keep their column over a run of them. */
	bool walk(const SelectKeyEvent& e) {
		note::Move m;
		bool stretch;
		if (!Hand::caret(e, m, stretch))
			return false;
		ensureRows();
		if (cursor != goalAt || (m != note::UP && m != note::DOWN))
			goal = -1;
		cursor = note::move(text, rows, cursor, m, goal);
		goalAt = m == note::UP || m == note::DOWN ? cursor : -1;
		if (!stretch)
			selection = cursor;
		return true;
	}
};
/** The glass, the stock screen's face and its lit bevel, with the field's
words over them: everything at a scene's level is drawn in one pass, which
asks for no lit layer, so the glass draws both halves itself. */
struct NoteGlass : Widget {
	void draw(const DrawArgs& args) override {
		const Rect r(Vec(0.f, 0.f), box.size);
		paintGlassFace(args.vg, r, true, mm2px(NAME_Y));
		paintGlassLit(args.vg, r);
		Widget::drawLayer(args, 1);
	}
};
struct NoteSheet : Widget {
	int64_t id = -1;
	bool peek = false; // read off a hovered name: no hand reaches it
	// The keyboard wanted at the RIG's next step (`RigWidget::step`): a
	// sheet opened by a click is opened inside the press, and Rack selects
	// whatever took the press once its handlers are done, which is the
	// button and not the field.
	bool take = false;
	std::string from;
	json_t* fromJ = nullptr; // the RIG's state as it opened, for the step of undo
	ZoomWidget* zoom = nullptr;
	NoteField* field = nullptr;
	~NoteSheet() {
		if (fromJ)
			json_decref(fromJ);
		if (noteOpen == this)
			noteOpen = nullptr;
		if (notePeek == this)
			notePeek = nullptr;
	}
	/** Stood against the rig's name screen, or false when that is gone. */
	bool follow();
	void step() override {
		if (!follow() || (peek && !peekWanted()))
			close();
		Widget::step();
	}
	bool peekWanted();
	/** Gone, the edit, if there was one, pushed as one step of undo. */
	void close() {
		if (!visible)
			return;
		visible = false;
		Rig* r = rigOf(id);
		if (!peek && r && r->note != from) {
			pushRigChange(r, fromJ, "edit rig note");
			fromJ = nullptr;
		}
		requestDelete();
	}
	// Written, it takes the hand as any opaque widget does; read, none.
	// What the field under the pointer took stays the field's: consuming
	// over it made the sheet the press's target, so Rack selected the sheet
	// and the field lost the keyboard at a click, and a drag went to the
	// sheet and selected nothing. Only what nothing under it took, the glass's air, is
	// stopped here, so it falls through to no module below.
	template <class E>
	void stop(const E& e) {
		if (!e.isConsumed())
			e.consume(this);
	}
	void onHover(const HoverEvent& e) override {
		if (peek)
			return;
		Widget::onHover(e);
		stop(e);
	}
	void onButton(const ButtonEvent& e) override {
		if (peek)
			return;
		Widget::onButton(e);
		stop(e);
	}
	void onHoverScroll(const HoverScrollEvent& e) override {
		if (peek)
			return;
		Widget::onHoverScroll(e);
		stop(e);
	}
	void onDragHover(const DragHoverEvent& e) override {
		if (peek)
			return;
		Widget::onDragHover(e);
		stop(e);
	}
};
void NoteField::onSelectKey(const SelectKeyEvent& e) {
	if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ESCAPE && sheet) {
		sheet->close();
		e.consume(this);
		return;
	}
	if ((e.action == GLFW_PRESS || e.action == GLFW_REPEAT) && walk(e)) {
		e.consume(this);
		return;
	}
	LedDisplayTextField::onSelectKey(e);
}

/** The note of `rig` on its glass, to write in or, `peek`, to read. */
NoteSheet* openNote(Rig* rig, bool peek) {
	NoteSheet* s = new NoteSheet;
	s->id = rig->id;
	s->peek = peek;
	if (!peek) {
		s->from = rig->note;
		s->fromJ = rig->stateJson();
	}
	s->zoom = new ZoomWidget;
	s->addChild(s->zoom);
	NoteGlass* glass = new NoteGlass;
	glass->box.size = mm2px(Vec(NOTE_W, NAME_H));
	s->zoom->addChild(glass);
	NoteField* field = new NoteField;
	field->id = rig->id;
	field->sheet = s;
	field->multiline = true;
	field->box.size = glass->box.size;
	field->textOffset = Vec(mm2px(NOTE_INSET), GLASS_BEVEL + mm2px(NOTE_INSET));
	field->placeholder = peek ? "" : "A note";
	field->text = rig->note;
	field->cursor = field->selection = (int)rig->note.size();
	glass->addChild(field);
	s->field = field;
	if (!s->follow()) {
		delete s;
		return nullptr;
	}
	APP->scene->addChild(s);
	s->take = !peek;
	return s;
}
/** Open the note of `rig` to write in, or, open already, close it. */
void toggleNote(Rig* rig) {
	if (notePeek)
		notePeek->close();
	if (noteOpen) {
		const bool mine = noteOpen->id == rig->id;
		noteOpen->close();
		if (mine)
			return;
	}
	noteOpen = openNote(rig, false);
}

struct RigPanel : Panel {
	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		drawPlate(args, this, "RIG");
		// On the column every module's jacks stand on, the last three rows.
		drawCaption(vg, RIG_CX, seqCapY(4), "SCENE", INK);
		drawCaption(vg, RIG_CX, seqCapY(5), "PREV", INK);
		drawCaption(vg, RIG_CX, seqCapY(6), "NEXT", INK);
	}
};

struct RigWidget : MemberWidget {
	NameScreen* nameScreen = nullptr; // where the note's glass stands
	Rig* rig = nullptr;

	/** The molecule: every module of ours bonded to my right, as the engine
	runs it, across a GAP, ended by a bypassed module or anything else ending
	it. Worked
	out from where the panels stand, as `runOf` is, the GAPs in it. The
	scenes are the members' alone (`moleculeModules`). */
	std::vector<ModuleWidget*> molecule() { return moleculeOf(this); }
	/** The members of the molecule, what a scene keeps: a GAP holds nothing
	to recall. */
	std::vector<Module*> moleculeModules() {
		std::vector<Module*> out;
		for (ModuleWidget* mw : molecule())
			if (isFamily(mw->module))
				out.push_back(mw->module);
		return out;
	}

	// ---- the scenes

	/** The RIG's state as it was, `oldJ`, and as it is now, one step of undo
	named `name`; `oldJ` is the step's to keep. */
	void pushChange(json_t* oldJ, const char* name) { pushRigChange(rig, oldJ, name); }
	/** INSERT: a new scene after the one showing, the molecule stored into it
	and shown, as the right arrow would move to it, one step of undo. Nothing
	is reset: the molecule is already the scene it moves to. */
	void insert() {
		if (!rig || rig->sceneCount() >= MAX_SCENES)
			return;
		json_t* oldJ = rig->stateJson();
		rig->insertScene(rig->shownForUi(), moleculeModules());
		pushChange(oldJ, "insert scene");
	}
	/** REPLACE: the molecule written over the scene showing, one step of undo. */
	void replace() {
		if (!rig || rig->sceneCount() == 0)
			return;
		json_t* oldJ = rig->stateJson();
		rig->storeScene(rig->shownForUi(), moleculeModules());
		pushChange(oldJ, "replace scene");
	}
	/** A step of undo for a scene's recall, its params and scene settings
	put back, into `h`. */
	static void pushMoved(history::ComplexAction* h, const std::vector<Rig::Moved>& moved) {
		for (const Rig::Moved& m : moved)
			pushSceneValue(h, m.module, (size_t)m.param, m.from, m.to);
	}
	/** A scene recalled by the hand, an arrow or a pick in the menu: the
	molecule set to it here and now, one step of undo (the engine's recalls, by
	SCENE, PREV and NEXT, are the patch performing and no step). */
	void pickScene(int at) {
		if (!rig || relaxed(rig->patchedNow))
			return;
		const int was = rig->shownForUi();
		std::vector<Rig::Moved> moved;
		if (!rig->handPick(at, moleculeModules(), &moved) || at >= rig->sceneCount() || (moved.empty() && at == was))
			return;
		history::ComplexAction* h = new history::ComplexAction;
		h->name = "recall scene";
		pushMoved(h, moved);
		SceneShown* sh = new SceneShown;
		sh->moduleId = rig->id;
		sh->from = was;
		sh->to = at;
		h->push(sh);
		APP->history->push(h);
	}
	/** COPY's scene, this RIG's own clipboard: never saved, gone with the
	panel. */
	rig::Scene clip;
	bool clipFull = false;
	void copy() {
		if (!rig || rig->sceneCount() == 0)
			return;
		clip = rig->sceneAt(rig->shownForUi());
		clipFull = true;
	}
	/** PASTE: the scene copied set in as a new scene after the one showing,
	shown, and the molecule set to it and reset, as any change of scene by the
	hand resets it, one step of undo (copy scene 1, go to 5, PASTE, and 6 is
	scene 1 again). With shift held and a scene showing, the copy is written
	over that scene instead (`pastesOver`), recalled and reset the same way,
	so a full RIG can still take it. While SCENE is patched the scene goes in
	and nothing is recalled or reset (`Rig::restartsOn`). */
	void paste() {
		if (!rig || !clipFull)
			return;
		const bool over = pastesOver();
		if (!over && rig->sceneCount() >= MAX_SCENES)
			return;
		json_t* oldJ = rig->stateJson();
		const int at = over ? rig->shownForUi() : rig->insertGiven(rig->shownForUi(), clip);
		if (over)
			rig->storeGiven(at, clip);
		history::ComplexAction* h = new history::ComplexAction;
		h->name = over ? "paste over scene" : "paste scene";
		RigChange* c = new RigChange;
		c->name = h->name;
		c->moduleId = rig->id;
		c->oldJ = oldJ;
		c->newJ = rig->stateJson();
		h->push(c);
		const std::vector<Module*> w = moleculeModules();
		std::vector<Rig::Moved> moved;
		rig->handPick(at, w, &moved, false);
		if (Rig::restartsOn(Rig::ACT_PASTE, relaxed(rig->patchedNow)))
			Rig::resetFromUi(w);
		pushMoved(h, moved);
		APP->history->push(h);
	}
	/** Whether PASTE writes over the scene showing: shift held, and a scene
	there to write over. */
	bool pastesOver() {
		return rig && rig->sceneCount() > 0 && Hand::held(G_RIG_PASTE_OVER);
	}
	/** DELETE: the scene showing taken out and the one that slides into its
	place recalled, the molecule reset as a change of scene resets it, one step
	of undo; the last one gone leaves the molecule as it stands. While SCENE is
	patched only the scene goes, the jack being in charge of the molecule. */
	void deleteShown() {
		if (!rig || rig->sceneCount() == 0)
			return;
		json_t* oldJ = rig->stateJson();
		rig->deleteScene(rig->shownForUi());
		history::ComplexAction* h = new history::ComplexAction;
		h->name = "delete scene";
		RigChange* c = new RigChange;
		c->name = h->name;
		c->moduleId = rig->id;
		c->oldJ = oldJ;
		c->newJ = rig->stateJson();
		h->push(c);
		if (rig->sceneCount() > 0 && !relaxed(rig->patchedNow)) {
			const std::vector<Module*> w = moleculeModules();
			std::vector<Rig::Moved> moved;
			rig->handPick(rig->shownForUi(), w, &moved, false);
			if (Rig::restartsOn(Rig::ACT_DELETE, relaxed(rig->patchedNow)))
				Rig::resetFromUi(w);
			pushMoved(h, moved);
		}
		APP->history->push(h);
	}

	// ---- the rig

	/** The rig held, as its name screen reads it: the file's name without
	its folder or extension, or nothing. */
	std::string name() {
		return rig && !rig->file.empty() ? system::getStem(rig->file) : std::string();
	}

	/** The place the hand last chose by its icon, or none, in which case the
	rig held says where it lives; with none held, the factory while it has any
	rig and the user's folder otherwise. A view, not the RIG's state: no undo,
	never saved, and a load hands it back to the rig held. */
	int placeChosen = -1;
	int placeShowing() {
		if (placeChosen >= 0)
			return placeChosen;
		const std::string f = rig ? rig->file : std::string();
		if (!f.empty()) {
			if (within(f, factoryFolder()))
				return FACTORY;
			if (within(f, userFolder()))
				return USER;
			return ELSEWHERE;
		}
		std::vector<std::string> dirs, files;
		listFolder(factoryFolder(), dirs, files);
		return dirs.empty() && files.empty() ? USER : FACTORY;
	}
	/** A place's folder; elsewhere is the folder of the rig held. */
	std::string placeFolder(int place) {
		if (place == FACTORY)
			return factoryFolder();
		if (place == USER)
			return userFolder();
		return rig && !rig->file.empty() ? system::getDirectory(rig->file) : userFolder();
	}
	/** The icons lit and crossed and the arrows able, as the place showing and
	the folders on disk say: asked a few times a second, and at once after
	anything here that moves the place (`placesDirty`). */
	bool placesDirty = true;
	void refreshPlaces() {
		const int showing = placeShowing();
		const bool gone = userMissing();
		for (int k = 0; k < 2; k++)
			placeLit[k] = showing == (k ? USER : FACTORY);
		userGone = gone;
		for (int k = 0; k < 2; k++)
			canStep[k] = !stepTarget(k ? 1 : -1).empty();
	}
	/** An icon's click: that place shown, for the arrows and the menu. */
	void showPlace(int place) {
		placeChosen = place;
		placesDirty = true;
	}
	/** Where the file dialog opens: the folder of the rig held, while it
	stands in the place showing (a subfolder of it, say), or the place's own
	folder. */
	std::string stepDir() {
		const std::string dir = placeFolder(placeShowing());
		if (rig && !rig->file.empty() && within(rig->file, dir) && system::isDirectory(system::getDirectory(rig->file)))
			return system::getDirectory(rig->file);
		return dir;
	}
	/** The rigs the arrows step through: the whole place showing, its own
	rigs first and then its folders', one after another (`gatherRigs`); a rig
	opened from elsewhere, its own folder and what stands in it. */
	std::vector<std::string> stepFiles() {
		std::vector<std::string> files;
		gatherRigs(placeFolder(placeShowing()), files);
		return files;
	}
	/** The rig an arrow would load: the next or the previous in the place,
	round and round, or nothing where the only rig is the one held. */
	std::string stepTarget(int by) {
		const std::vector<std::string> files = stepFiles();
		if (files.empty())
			return std::string();
		const int n = (int)files.size();
		const std::string from = rig ? rig->file : std::string();
		const int at = (int)(std::find(files.begin(), files.end(), from) - files.begin());
		const int to = stepRound(at, by, n);
		return files[to] == from ? std::string() : files[to];
	}
	/** An arrow: that rig loaded at once, everything built afresh (there is
	no LOAD, the arrows load). */
	void stepRig(int by) {
		const std::string to = stepTarget(by);
		if (!to.empty())
			load(to);
	}

	/** The molecule written down: Rack's selection file, each module as Rack
	writes it with its place counted from the first, the cables between
	them, the cables between them and this RIG (`rigCables`), the scenes, and
	the rig's note. */
	json_t* rigJson() {
		const std::vector<ModuleWidget*> w = molecule();
		json_t* rootJ = json_object();
		json_t* modulesJ = json_array();
		std::set<int64_t> ids;
		const float x0 = w.empty() ? 0.f : w[0]->box.pos.x;
		// Each module written as the scene showing holds it, where it does.
		json_t* shownJ = sceneToJson(rig->sceneAt(rig->shownForUi()));
		for (ModuleWidget* mw : w) {
			json_t* mJ = mw->toJson();
			sceneIntoParams(mJ, json_object_get(shownJ, std::to_string(mw->module->id).c_str()));
			// Its neighbours by id are the patch's, never the rig's.
			json_object_del(mJ, "leftModuleId");
			json_object_del(mJ, "rightModuleId");
			json_t* posJ = json_array();
			json_array_append_new(posJ, json_integer(gridUnits(mw->box.pos.x - x0)));
			json_array_append_new(posJ, json_integer(0));
			json_object_set_new(mJ, "pos", posJ);
			json_array_append_new(modulesJ, mJ);
			ids.insert(mw->module->id);
		}
		json_decref(shownJ);
		json_object_set_new(rootJ, "modules", modulesJ);
		// The cables between the molecule's modules, as Rack's selection file
		// keeps them; and apart, under a key of ours, those between the
		// molecule and this RIG, a Seq's EOC into NEXT, the RIG's end named `RIG_END`. They stand
		// apart so Rack's own Import selection, which builds no RIG, reads the
		// file as a plain selection.
		json_t* cablesJ = json_array();
		json_t* rigCablesJ = json_array();
		for (CableWidget* cw : APP->scene->rack->getCompleteCables()) {
			engine::Cable* c = cw->cable;
			if (!c || !c->inputModule || !c->outputModule)
				continue;
			const bool outIn = ids.count(c->outputModule->id), inIn = ids.count(c->inputModule->id);
			const bool outRig = c->outputModule == rig, inRig = c->inputModule == rig;
			if (!(outIn && inIn) && !(outIn && inRig) && !(outRig && inIn))
				continue;
			const bool mine = outRig || inRig;
			json_t* cJ = json_object();
			json_object_set_new(cJ, "id", json_integer(c->id));
			json_object_set_new(cJ, "outputModuleId", json_integer(outRig ? RIG_END : c->outputModule->id));
			json_object_set_new(cJ, "outputId", json_integer(c->outputId));
			json_object_set_new(cJ, "inputModuleId", json_integer(inRig ? RIG_END : c->inputModule->id));
			json_object_set_new(cJ, "inputId", json_integer(c->inputId));
			json_object_set_new(cJ, "color", json_string(color::toHexString(cw->color).c_str()));
			json_array_append_new(mine ? rigCablesJ : cablesJ, cJ);
		}
		json_object_set_new(rootJ, "cables", cablesJ);
		json_object_set_new(rootJ, "rigCables", rigCablesJ);
		rig->lock.lock();
		json_object_set_new(rootJ, "scenes", scenesToJson(rig->scenes, &ids));
		rig->lock.unlock();
		json_object_set_new(rootJ, "scene", json_integer(rig->shownForUi()));
		json_object_set_new(rootJ, "note", json_stringn(rig->note.c_str(), rig->note.size()));
		return rootJ;
	}

	/** A module's kind as a rig file names it, plugin and model: what SAVE
	compares, so two CVs swapped are the same make-up. */
	static std::string kindOf(ModuleWidget* mw) { return mw->model->plugin->slug + "/" + mw->model->slug; }
	static std::vector<std::string> kindsOf(const std::vector<ModuleWidget*>& w) {
		std::vector<std::string> out;
		for (ModuleWidget* mw : w)
			out.push_back(kindOf(mw));
		return out;
	}
	static std::set<int64_t> idsOf(const std::vector<ModuleWidget*>& w) {
		std::set<int64_t> out;
		for (ModuleWidget* mw : w)
			out.insert(mw->module->id);
		return out;
	}
	/** Each module's params and what it saves beside them, as a rig with no
	scenes compares them (`saveWanted`). */
	static std::vector<std::vector<float>> paramsOf(const std::vector<ModuleWidget*>& w) {
		std::vector<std::vector<float>> out;
		for (ModuleWidget* mw : w) {
			std::vector<float> ps;
			for (engine::Param& p : mw->module->params)
				ps.push_back(p.getValue());
			out.push_back(ps);
		}
		return out;
	}
	static std::vector<std::string> dataOf(const std::vector<ModuleWidget*>& w) {
		std::vector<std::string> out;
		for (ModuleWidget* mw : w)
			out.push_back(Rig::settingsData(mw->module));
		return out;
	}
	/** The file holds the molecule `w` as it stands now. */
	void markSaved(const std::vector<ModuleWidget*>& w) {
		rig->markClean(kindsOf(w), rig->scenesOn(idsOf(w)), rig->note, paramsOf(w), dataOf(w));
	}
	/** The rig file read for SAVE where the RIG does not know what it holds:
	its kinds in place order and its scenes keyed by place, read again only
	when its path or its size changes. */
	std::string heldPath;
	int64_t heldSize = -1;
	bool heldRead = false;
	std::vector<std::string> heldKinds;
	std::vector<rig::Scene> heldScenes;
	std::string heldNote;
	std::vector<std::vector<float>> heldParams;
	std::vector<std::string> heldData;
	/** The last comparison that failed: the file's path, the rig files
	written by then (`rigFilesWritten`) and the molecule's make-up. While none
	of them has changed it fails again without the file being looked at, so
	a molecule that is not the file's costs no stat in every frame; a file
	edited by hand outside Rack is seen once the path or the molecule changes. */
	bool failedKnown = false;
	std::string failedPath;
	uint64_t failedWrites = 0;
	std::vector<std::string> failedKinds;
	bool cleanFromFile(const std::vector<ModuleWidget*>& w) {
		const std::vector<std::string> kinds = kindsOf(w);
		if (failedKnown && failedPath == rig->file && failedWrites == rigFilesWritten() && failedKinds == kinds)
			return false;
		failedKnown = !compareWithFile(w, kinds);
		if (failedKnown) {
			failedPath = rig->file;
			failedWrites = rigFilesWritten();
			failedKinds = kinds;
		}
		return !failedKnown;
	}
	bool compareWithFile(const std::vector<ModuleWidget*>& w, const std::vector<std::string>& kinds) {
		if (!system::isFile(rig->file))
			return false;
		const int64_t size = system::getFileSize(rig->file);
		if (rig->file != heldPath || size != heldSize) {
			heldPath = rig->file;
			heldSize = size;
			heldRead = false;
			heldKinds.clear();
			heldScenes.clear();
			heldParams.clear();
			heldData.clear();
			if (json_t* rootJ = loadJson(rig->file)) {
				const std::vector<json_t*> ms = fileModules(rootJ);
				const std::set<int64_t> named = namedIds(ms);
				std::map<int64_t, int64_t> place;
				for (json_t* one : ms) {
					const char* p = json_string_value(json_object_get(one, "plugin"));
					const char* m = json_string_value(json_object_get(one, "model"));
					int64_t id;
					if (namedId(one, named, id))
						place[id] = (int64_t)heldKinds.size();
					heldKinds.push_back(std::string(p ? p : "") + "/" + (m ? m : ""));
					// Its params by their ids, and what it saves beside them,
					// written as `settingsData` writes it.
					std::vector<float> ps;
					size_t q;
					json_t* pJ;
					json_array_foreach(json_object_get(one, "params"), q, pJ) {
						const int at = (int)json_integer_value(json_object_get(pJ, "id"));
						if (at < 0 || at > 4096)
							continue;
						if ((int)ps.size() <= at)
							ps.resize(at + 1, 0.f);
						ps[at] = (float)json_number_value(json_object_get(pJ, "value"));
					}
					heldParams.push_back(ps);
					json_t* dJ = json_object_get(one, "data");
					char* text = dJ ? json_dumps(dJ, JSON_COMPACT | JSON_SORT_KEYS | JSON_REAL_PRECISION(9)) : nullptr;
					heldData.push_back(text ? text : "");
					free(text);
				}
				heldScenes = scenesFromJson(json_object_get(rootJ, "scenes"), &place);
				heldNote = Rig::noteOf(rootJ);
				heldRead = true;
				json_decref(rootJ);
			}
		}
		if (!heldRead || heldKinds != kinds)
			return false;
		std::map<int64_t, int64_t> toLive;
		for (size_t k = 0; k < w.size(); k++)
			toLive[(int64_t)k] = w[k]->module->id;
		std::vector<rig::Scene> held;
		for (const rig::Scene& sc : heldScenes) {
			rig::Scene one;
			for (const auto& kv : sc)
				one[toLive[kv.first]] = kv.second;
			held.push_back(one);
		}
		rig->markClean(heldKinds, held, heldNote, heldParams, heldData);
		return true;
	}
	/** Whether SAVE is lit, the rig changed from the file it came from: a
	different make-up, the modules' kinds
	left to right, so CVs moved among themselves or a Seq moved and moved back
	are no change; or different scenes stored, or a different note. Where
	there are scenes, a param, a step, a cable or the scene showing is using
	the rig, not changing it; with none, a param or a setting is (`settingsDiffer`). With no file, anything at
	all to save. What the file holds is kept in the RIG (`markClean`), taken at
	a load and a save; where it is not known, a patch made before it was kept
	or a file handed in, it is read from the file once (`cleanFromFile`), the
	file's modules matched to the molecule's by place. */
	bool saveWanted() {
		if (!rig)
			return false;
		const std::vector<ModuleWidget*> w = molecule();
		if (rig->file.empty())
			return !w.empty() || rig->sceneCount() > 0 || !rig->note.empty();
		if (!rig->cleanKnown && !cleanFromFile(w))
			return true;
		if (kindsOf(w) != rig->cleanModels)
			return true;
		if (rig->note != rig->cleanNote || rig->scenesOn(idsOf(w)) != rig->cleanScenes)
			return true;
		return rig->cleanScenes.empty() && settingsDiffer(w);
	}
	/** A rig with no scenes has its file as its one scene: a param off what the file holds, or a
	module's menu setting, is a change, as a param off the scene showing
	makes REPLACE breathe where there are scenes. A param another plugin has
	mapped (MIDI-MAP, a uMAP) is driven from outside and never counts, and no
	jack of ours moves a param, so modulation lights nothing. */
	bool settingsDiffer(const std::vector<ModuleWidget*>& w) {
		// Kept from before the settings were (a patch saved earlier): read
		// from the file once, as a RIG not knowing its file reads it.
		if (rig->cleanParams.empty() && !w.empty())
			cleanFromFile(w);
		if (rig->cleanParams.size() != w.size())
			return false; // a file not read, or of another make-up: nothing to tell
		for (size_t k = 0; k < w.size(); k++) {
			Module* m = w[k]->module;
			const std::vector<float>& held = rig->cleanParams[k];
			for (size_t i = 0; i < m->params.size() && i < held.size(); i++)
				if (m->params[i].getValue() != held[i] && !APP->engine->getParamHandle(m->id, (int)i))
					return true;
			const std::string d = Rig::settingsData(m);
			if (!d.empty() && k < rig->cleanData.size() && d != rig->cleanData[k])
				return true;
		}
		return false;
	}

	/** A rig written to `path`, never inside the factory's folder, which
	an update replaces. */
	bool writeTo(std::string path) {
		if (string::lowercase(system::getExtension(path)) != std::string(".") + RIG_EXT)
			path += std::string(".") + RIG_EXT;
		if (within(path, factoryFolder())) {
			WARN("RIG: %s is in the factory's folder, which a RIG never writes", path.c_str());
			return false;
		}
		system::createDirectories(system::getDirectory(path));
		json_t* rootJ = rigJson();
		const bool wrote = saveJson(rootJ, path, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		rigFilesWritten()++;
		json_decref(rootJ);
		if (!wrote) {
			WARN("RIG: could not write %s", path.c_str());
			return false;
		}
		json_t* oldJ = rig->stateJson();
		markSaved(molecule());
		if (path != rig->file) {
			// Saving under a new name is no change to the molecule, and the
			// name it holds is the RIG's own state, which a step of undo puts
			// back.
			rig->file = path;
			pushChange(oldJ, "save rig as");
		}
		else
			json_decref(oldJ);
		saveDiffers = false;
		placeChosen = -1;
		placesDirty = true;
		return true;
	}
	/** SAVE: always Rack's own save dialog, a new save each time, opening in
	the user's folder (made then if it is not there yet) under a name no rig in
	it has yet; the same name in the same folder overwrites, which the dialog
	asks about. A factory rig, changed, is so saved as a copy of its own. With
	shift held, the dialog opens on the rig's own file instead, so a click
	more overwrites it, the system asking first. */
	void save() {
		if (!rig)
			return;
		if (Hand::held(G_RIG_OVERWRITE) && overwritable())
			saveAs(system::getDirectory(rig->file), system::getFilename(rig->file));
		else
			saveAs();
	}
	/** Whether SAVE with shift has a file to open on: one the rig came from
	that is still there and not the factory's. */
	bool overwritable() {
		return rig && !rig->file.empty() && !within(rig->file, factoryFolder()) && system::exists(rig->file);
	}
	/** A name for a new save in `dir`: the rig held's, where `dir` has no rig
	of that name, else numbered on past any taken. */
	std::string freshName(const std::string& dir) {
		const std::string stem = rig && !rig->file.empty() ? system::getStem(rig->file) : std::string("Untitled");
		if (!rig || rig->file.empty() || !within(rig->file, dir))
			if (!system::exists(system::join(dir, stem + "." + RIG_EXT)))
				return stem + "." + RIG_EXT;
		std::string base = stem;
		// "song 3" is numbered on from 3, not given "song 3 2".
		const size_t sp = stem.find_last_of(' ');
		int n = 1;
		if (sp != std::string::npos && sp + 1 < stem.size() &&
		    stem.find_first_not_of("0123456789", sp + 1) == std::string::npos) {
			base = stem.substr(0, sp);
			n = std::atoi(stem.c_str() + sp + 1);
		}
		for (int k = n + 1; k < 10000; k++) {
			const std::string name = string::f("%s %d.%s", base.c_str(), k, RIG_EXT);
			if (!system::exists(system::join(dir, name)))
				return name;
		}
		return stem + "." + RIG_EXT;
	}
	/** The folder SAVE's dialog opens in: the user's, or the default while
	one set from the menu is missing; made if it is not there yet. */
	std::string saveFolder() {
		const std::string dir = userMissing() ? defaultUserFolder() : userFolder();
		system::createDirectories(dir);
		return dir;
	}
	void saveAs() {
		const std::string dir = saveFolder();
		saveAs(dir, freshName(dir));
	}
	void saveAs(const std::string& dir, const std::string& name) {
		// The test rig answers the dialog itself, once (`dialogAnswer`).
		std::string path = takeAnswer();
		const bool asked = path.empty();
		if (asked) {
			osdialog_filters* filters = osdialog_filters_parse("Atomic rig (.vcvs):vcvs");
			char* pathC = osdialog_file(OSDIALOG_SAVE, dir.c_str(), name.c_str(), filters);
			osdialog_filters_free(filters);
			if (!pathC)
				return;
			path = pathC;
			std::free(pathC);
		}
		if (within(path, factoryFolder())) {
			WARN("RIG: refused a save into the factory's folder, %s", path.c_str());
			if (asked)
				osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK,
				                 "Factory rigs are replaced on every update, so nothing saves there. "
				                 "Save to your own folder instead.");
			return;
		}
		writeTo(path);
	}
	/** "Set user folder...", from the right-click menu: Rack's own folder
	dialog, its pick the user folder for every RIG from now on. The factory's
	folder is refused. */
	void chooseFolderDialog() {
		std::string path = takeAnswer();
		if (path.empty()) {
			const std::string dir = system::isDirectory(userFolder()) ? userFolder() : asset::user("");
			char* pathC = osdialog_file(OSDIALOG_OPEN_DIR, dir.c_str(), nullptr, nullptr);
			if (!pathC)
				return;
			path = pathC;
			std::free(pathC);
		}
		if (within(path, factoryFolder())) {
			WARN("RIG: the factory's folder cannot be the user folder");
			return;
		}
		chooseFolder(path);
		placeChosen = USER;
		placesDirty = true;
	}
	/** A click on the name: the rigs in the place showing as a menu, the one
	held ticked and its folders as submenus, a pick loading one at once, and
	Rack's own file dialog the last item for a rig kept anywhere else. */
	void nameMenu() {
		if (!rig)
			return;
		const int place = placeShowing();
		const std::string dir = placeFolder(place);
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(place == FACTORY ? std::string("Factory rigs")
		                               : place == USER ? std::string("Your rigs")
		                                               : "Rigs in " + system::getFilename(dir)));
		if (place == USER && userMissing())
			menu->addChild(createMenuLabel("The folder is missing"));
		else
			appendFolder(menu, dir);
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuItem("Open a rig file...", "", whileStanding(this, [](RigWidget* w) { w->openDialog(); })));
	}
	/** Rack's own file dialog, from the name's menu: a rig opened from
	anywhere and loaded at once, browsed then in its own folder. */
	void openDialog() {
		std::string path = takeAnswer();
		if (path.empty()) {
			std::string dir = stepDir();
			if (!system::isDirectory(dir))
				dir = asset::user("");
			osdialog_filters* filters = osdialog_filters_parse("Atomic rig (.vcvs):vcvs");
			char* pathC = osdialog_file(OSDIALOG_OPEN, dir.c_str(), nullptr, filters);
			osdialog_filters_free(filters);
			if (!pathC)
				return;
			path = pathC;
			std::free(pathC);
		}
		if (isRigFile(path))
			load(path);
	}

	/** A rig loaded: the molecule taken away, cables and all, and the file's
	built in its place from my right edge, its cables between its own
	modules and between them and me, and its scenes, as one step of undo. A cable from the old molecule
	to a module outside it goes with the old molecule (Rack keeps no cable to a
	module that is not there); a module whose plugin is not installed is left
	out, and the rest closes up; one the file gives no id of its own is built
	with no cable or scene, which could name it only by guessing (`namedIds`). */
	void load(const std::string& path) {
		if (!rig)
			return;
		json_error_t err;
		json_t* rootJ = loadJson(path, &err);
		if (!rootJ) {
			WARN("RIG: could not read %s: %s", path.c_str(), err.text);
			return;
		}
		RackWidget* rack = APP->scene->rack;
		history::ComplexAction* h = new history::ComplexAction;
		h->name = "load rig";
		json_t* oldJ = rig->stateJson();

		for (ModuleWidget* mw : molecule())
			removeWithHistory(h, mw);

		const std::vector<json_t*> ms = fileModules(rootJ);
		const std::set<int64_t> named = namedIds(ms);

		// The scene showing, which each module is built as.
		json_t* scenesJ = json_object_get(rootJ, "scenes");
		const int shownAt = (int)json_integer_value(json_object_get(rootJ, "scene"));
		json_t* shownJ = json_array_get(scenesJ, std::min(std::max(shownAt, 0), Rig::lastOf((int)json_array_size(scenesJ))));
		const std::map<ModuleWidget*, Vec> was = standing();
		std::map<int64_t, int64_t> ids;
		std::vector<ModuleWidget*> added;
		float x = box.pos.x + box.size.x;
		for (json_t* oneJ : ms) {
			const char* pluginSlug = json_string_value(json_object_get(oneJ, "plugin"));
			const char* modelSlug = json_string_value(json_object_get(oneJ, "model"));
			plugin::Model* model = pluginSlug && modelSlug ? plugin::getModel(pluginSlug, modelSlug) : nullptr;
			if (!model) {
				WARN("RIG: %s has %s/%s, which is not installed", path.c_str(), pluginSlug ? pluginSlug : "?",
				     modelSlug ? modelSlug : "?");
				continue;
			}
			engine::Module* m = model->createModule();
			// Its state loaded before the engine has it, as Rack's own paste
			// does, so no lock is needed; its id and its links are its own.
			json_t* copyJ = json_deep_copy(oneJ);
			int64_t fileId;
			const bool isNamed = namedId(oneJ, named, fileId);
			json_t* valuesJ = isNamed ? json_object_get(shownJ, std::to_string(fileId).c_str()) : nullptr;
			sceneIntoParams(copyJ, valuesJ);
			json_object_del(copyJ, "id");
			json_object_del(copyJ, "leftModuleId");
			json_object_del(copyJ, "rightModuleId");
			try {
				m->fromJson(copyJ);
			}
			catch (Exception& e) {
				WARN("RIG: %s", e.what());
			}
			sceneIntoSettings(m, valuesJ);
			json_decref(copyJ);
			APP->engine->addModule(m);
			ModuleWidget* nw = model->createModuleWidget(m);
			rack->addModule(nw);
			rack->setModulePosForce(nw, Vec(x, box.pos.y));
			x = nw->box.pos.x + nw->box.size.x;
			if (isNamed)
				ids[fileId] = m->id;
			added.push_back(nw);
		}
		pushMoves(h, was);
		for (ModuleWidget* nw : added) {
			history::ModuleAdd* came = new history::ModuleAdd;
			came->setModule(nw);
			h->push(came);
		}

		// The file's cables, and the ones between its molecule and the RIG
		// that loads it, this RIG standing for `RIG_END`; a RIG's jack already
		// taken by a cable from outside the molecule keeps it, since an input
		// holds one cable.
		std::vector<json_t*> cs;
		size_t i;
		json_t* cJ;
		json_array_foreach(json_object_get(rootJ, "cables"), i, cJ) cs.push_back(cJ);
		json_array_foreach(json_object_get(rootJ, "rigCables"), i, cJ) cs.push_back(cJ);
		std::map<int64_t, int64_t> ends = ids;
		ends[RIG_END] = rig->id;
		for (json_t* cableJ : cs) {
			const int64_t o = json_integer_value(json_object_get(cableJ, "outputModuleId"));
			const int64_t in = json_integer_value(json_object_get(cableJ, "inputModuleId"));
			if (!ends.count(o) || !ends.count(in))
				continue;
			engine::Module* om = APP->engine->getModule(ends[o]);
			engine::Module* im = APP->engine->getModule(ends[in]);
			const int oid = (int)json_integer_value(json_object_get(cableJ, "outputId"));
			const int iid = (int)json_integer_value(json_object_get(cableJ, "inputId"));
			if (!om || !im || oid < 0 || oid >= (int)om->outputs.size() || iid < 0 || iid >= (int)im->inputs.size())
				continue;
			if (im == rig && rack->getTopCable(getInput(iid))) {
				WARN("RIG: its input %d holds a cable from outside the rig, which stays", iid);
				continue;
			}
			engine::Cable* c = new engine::Cable;
			c->outputModule = om;
			c->outputId = oid;
			c->inputModule = im;
			c->inputId = iid;
			APP->engine->addCable(c);
			CableWidget* cw = new CableWidget;
			cw->setCable(c);
			const char* colJ = json_string_value(json_object_get(cableJ, "color"));
			cw->color = colJ ? color::fromHexString(colJ) : rack->getNextCableColor();
			rack->addCable(cw);
			history::CableAdd* ca = new history::CableAdd;
			ca->setCable(cw);
			h->push(ca);
		}

		// The scenes, their ids turned into the new modules', the file, and
		// the rig's note, none where the file has none.
		json_t* stateJ = json_object();
		json_object_set(stateJ, "scenes", json_object_get(rootJ, "scenes") ? json_object_get(rootJ, "scenes") : json_array());
		json_object_set_new(stateJ, "scene", json_integer(json_integer_value(json_object_get(rootJ, "scene"))));
		json_object_set_new(stateJ, "file", json_string(path.c_str()));
		const std::string note = Rig::noteOf(rootJ);
		json_object_set_new(stateJ, "note", json_stringn(note.c_str(), note.size()));
		// Built afresh and reset, as the bracket's RESET resets it, even where the
		// same modules stand again; an undo of it puts the state
		// back and resets nothing.
		rig->setState(stateJ, &ids, Rig::restartsOn(Rig::ACT_OPEN, false));
		json_decref(stateJ);
		// What the file holds is what now stands.
		rig->markClean(kindsOf(added), rig->scenesOn(idsOf(added)), rig->note, paramsOf(added), dataOf(added));
		saveDiffers = false;
		json_decref(rootJ);
		RigChange* c = new RigChange;
		c->name = "load rig";
		c->moduleId = rig->id;
		c->oldJ = oldJ;
		c->newJ = rig->stateJson();
		h->push(c);
		APP->history->push(h);
		placeChosen = -1;
		placesDirty = true;
		resetGroundMarks();
	}

	/** The folder as Rack's own menus: its rigs, the one held ticked, and
	its folders as submenus, each reaching me through `whileStanding`, since
	the menu can outlive me. */
	void appendFolder(ui::Menu* menu, const std::string& dir) {
		std::vector<std::string> dirs, files;
		listFolder(dir, dirs, files);
		for (const std::string& d : dirs)
			menu->addChild(createSubmenuItem(system::getFilename(d), "",
			                                 whileStanding(this, [d](RigWidget* w, ui::Menu* sub) { w->appendFolder(sub, d); })));
		for (const std::string& f : files)
			menu->addChild(createCheckMenuItem(system::getStem(f), "",
			                                   whileStanding(this, [f](RigWidget* w) { return w->rig && w->rig->file == f; }),
			                                   whileStanding(this, [f](RigWidget* w) { w->load(f); })));
		if (dirs.empty() && files.empty())
			menu->addChild(createMenuLabel("No rigs here yet"));
	}
	void appendContextMenu(ui::Menu* menu) override {
		if (!rig)
			return;
		menu->addChild(new ui::MenuSeparator);
		// Where VCV Rack has a convention the menu follows it: a plugin's
		// folder opened in the system's file browser, a folder set through the
		// system's own dialog.
		const std::string folder = userFolder();
		menu->addChild(createSubmenuItem("Load factory rig", "", whileStanding(this, [](RigWidget* w, ui::Menu* sub) {
			                                 w->appendFolder(sub, factoryFolder());
		                                 })));
		if (!userMissing())
			menu->addChild(createSubmenuItem("Load your rig", "", whileStanding(this, [folder](RigWidget* w, ui::Menu* sub) {
				                                 w->appendFolder(sub, folder);
			                                 })));
		menu->addChild(createMenuItem("Set your folder...", system::getFilename(folder),
		                              whileStanding(this, [](RigWidget* w) { w->chooseFolderDialog(); })));
		if (!chosenFolder().empty())
			menu->addChild(createMenuItem("Use the default folder", "", whileStanding(this, [](RigWidget* w) {
				                              chooseFolder(std::string());
				                              w->placeChosen = USER;
				                              w->placesDirty = true;
			                              })));
		if (!userMissing())
			menu->addChild(createMenuItem("Open your folder", "", [=]() {
				system::createDirectories(folder);
				system::openDirectory(folder);
			}));
		// The scenes are the screen's, to step and to stroke through. What
		// only the menu does is recall the scene showing over what the hand
		// has done since, which neither the arrows nor a stroke ever do, the
		// opposite of REPLACE: one entry however many scenes there are.
		if (rig->sceneCount() > 0) {
			menu->addChild(new ui::MenuSeparator);
			const int shown = rig->shownForUi();
			menu->addChild(createMenuItem(string::f("Recall scene %d", shown + 1), "",
			                              whileStanding(this, [shown](RigWidget* w) { w->pickScene(shown); }),
			                              relaxed(rig->patchedNow) || !sceneDiffers));
		}
		MemberWidget::appendContextMenu(menu);
	}

	void step() override {
		if (noteOpen && noteOpen->take && rig && noteOpen->id == rig->id) {
			noteOpen->take = false;
			APP->event->setSelectedWidget(noteOpen->field);
		}
		// The ids the rack has, for the scenes written down to keep.
		if (rig && APP->scene && APP->scene->rack) {
			rig->live.clear();
			for (ModuleWidget* o : APP->scene->rack->getModules())
				if (o->module)
					rig->live.insert(o->module->id);
			rig->liveKnown = true;
			rig->sceneRGB.store(cableColorOn(getInput(Rig::SCENE_INPUT)), std::memory_order_relaxed);
			// A place shown, a rig loaded or saved, at once.
			if (placesDirty) {
				placesDirty = false;
				refreshPlaces();
			}
			// The molecule against the file, every frame, the file itself read
			// only where the RIG does not know what it holds.
			saveDiffers = saveWanted();
			// Every control's words for the state on screen (Words.hpp): SAVE
			// what a click does now, shift held or not, its shift-click only
			// where there is a file of the user's to overwrite; a part with
			// nothing to work on says why in its name and has no lesson.
			const int count = rig->sceneCount();
			const std::vector<Module*> members = moleculeModules();
			lone = members.empty();
			rigSeqs = false;
			for (Module* m : members)
				rigSeqs = rigSeqs || (m->model && m->model->slug == "SEQ");
			const bool patched = relaxed(rig->patchedNow);
			const bool canOverwrite = !lone && overwritable();
			const bool over = canOverwrite && Hand::held(G_RIG_OVERWRITE);
			retipParam(rig, Rig::SAVE_PARAM, {lone ? (std::string(RIG_SAVE_NAME) + RIG_NOTHING_RIGHT).c_str() : over ? "Overwrite" : RIG_SAVE_NAME,
			           lone ? ""
			           : over ? RIG_OVERWRITE_LESSON
			           : rig->file.empty() ? RIG_SAVE_NEW_LESSON
			           : count > 0 ? RIG_SAVE_SCENES_LESSON : RIG_SAVE_PLAIN_LESSON,
			           canOverwrite ? Gestures{&G_RIG_OVERWRITE} : Gestures{}});
			const bool full = count >= MAX_SCENES;
			retipParam(rig, Rig::INSERT_PARAM, {lone ? (std::string("Insert scene") + RIG_NOTHING_RIGHT).c_str() : full ? "Insert scene: full" : "Insert scene",
			           lone ? "" : full ? RIG_FULL_LESSON : count > 0 ? RIG_INSERT_LESSON : RIG_INSERT_FIRST_LESSON});
			// PASTE's shift-click is said only where there is a scene to
			// paste over, and a full RIG takes it still.
			const bool pasteOver = clipFull && count > 0;
			const bool overHeld = pasteOver && pastesOver();
			retipParam(rig, Rig::PASTE_PARAM, {overHeld ? "Paste over scene" : full ? "Paste scene: full" : clipFull ? "Paste scene" : "Paste scene: nothing copied",
			           overHeld ? rigEnd(RIG_PASTE_OVER_LESSON, rigSeqs).c_str() : full ? RIG_FULL_LESSON : !clipFull ? ""
			           : rigEnd(count > 0 ? RIG_PASTE_LESSON : RIG_PASTE_FIRST_LESSON, rigSeqs).c_str(),
			           pasteOver ? Gestures{&G_RIG_PASTE_OVER} : Gestures{}});
			const char* none = count > 0 ? "" : RIG_NO_SCENES;
			retipParam(rig, Rig::REPLACE_PARAM, {(std::string("Replace scene") + none).c_str(), count > 0 ? RIG_REPLACE_LESSON : ""});
			retipParam(rig, Rig::COPY_PARAM, {(std::string("Copy scene") + none).c_str(), count > 0 ? RIG_COPY_LESSON : ""});
			retipParam(rig, Rig::DELETE_PARAM, {(std::string("Delete scene") + none).c_str(),
			           count == 0 ? "" : patched ? RIG_DELETE_PATCHED_LESSON : count == 1 ? RIG_DELETE_LAST_LESSON
			                                                                       : rigEnd(RIG_DELETE_LESSON, rigSeqs).c_str()});
			// PREV and NEXT are heard and do nothing while SCENE is patched.
			retipIn(rig, Rig::PREV_INPUT, {"Previous scene", patched ? RIG_TRIGGERS_UNHEARD : rigEnd(RIG_PREV_LESSON, rigSeqs).c_str()});
			retipIn(rig, Rig::NEXT_INPUT, {"Next scene", patched ? RIG_TRIGGERS_UNHEARD : rigEnd(RIG_NEXT_LESSON, rigSeqs).c_str()});
			retipIn(rig, Rig::SCENE_INPUT, {"Scene", (std::string(RIG_SCENE_IN_LESSON) + (rigSeqs ? RIG_SEQS_PLAY_ON : "")).c_str()});
			// The molecule against the scene showing, every frame too: a few
			// params compared, and REPLACE then breathes by this frame's
			// molecule rather than one up to six frames old, which a traced
			// frame drew differently by how many frames a test had stepped.
			sceneDiffers = rig->sceneCount() > 0 && rig->differs(moleculeModules());
			// Whether the folder is still there, a few times a second, the
			// disk being read.
			if (++differCount >= 6) {
				differCount = 0;
				refreshPlaces();
			}
		}
		MemberWidget::step();
	}

	bool placeLit[2] = {}; // the factory's icon and the user's, lit as `refreshPlaces` says
	bool userGone = false;  // the user's folder set and gone, likewise
	int differCount = 0;
	bool sceneDiffers = false; // the molecule is not the scene showing: REPLACE breathes
	bool lone = false;         // nothing of ours to my right: INSERT and SAVE have nothing to keep, and go faint
	bool rigSeqs = false;      // a SEQ stands to my right, which a recall restarts: the lessons say so only then
	bool saveDiffers = false;  // nor what the rig file holds: SAVE is lit
	bool canStep[2] = {}; // whether each rig arrow would load anything, asked with the folder, not every frame
	/** A flat strip of the column, the panel's width, as a Clock's RUN
	stands on its mode strip (`addStripButton`). */
	void addGround(float y, float h) {
		HeadStrip* ground = new HeadStrip;
		ground->box.pos = mm2px(Vec(0.f, y));
		ground->box.size = mm2px(Vec(RIG_W, h));
		addChild(ground);
	}
	/** A button on it, one row tall, the whole width or a half of it; or,
	`onGlass`, standing on a section's glass, square and lit as a Trg's ring
	is, held off only an edge it shares with the glass's bevel. */
	template <class B = RigButton>
	B* addButton(int paramId, float y, const char* word, int half = -1, bool onGlass = false) {
		const float w = half < 0 ? RIG_W : RIG_W / 2.f, x = half > 0 ? RIG_W / 2.f : 0.f;
		B* b = createParam<B>(mm2px(Vec(x, y)), rig, paramId);
		b->box.size = mm2px(Vec(w, STRIP_H));
		b->strip = !onGlass;
		b->text = word;
		b->px = HEAD_STRIP_PX;
		addChild(b);
		return b;
	}
	/** The rig: the two places, and its name between two arrows on a glass
	whose foot SAVE and the note's button stand on. */
	void addRigName(Rig* module) {
		NameScreen* nameS = createWidget<NameScreen>(mm2px(Vec(0.f, NAME_Y)));
		nameS->box.size = mm2px(Vec(RIG_W, NAME_H));
		nameS->owner = this;
		nameScreen = nameS;
		addChild(nameS);
		placeLit[0] = !module;
		for (int k = 0; k < 2; k++) {
			// The factory at the left, the user's own folder at the right.
			const int place = k ? USER : FACTORY;
			GlyphButton* icon = addGlyphButton(nameS, Vec(k * RIG_W / 2.f, 0.f), Vec(RIG_W / 2.f, FOLDER_H),
			                                   [=](NVGcontext* vg, float cx, float cy, NVGcolor col) {
				                                   const Vec at(cx - mm2px(PLACE_ICON_W) / 2.f, mm2px(ARROW_AIR));
				                                   const Vec size = mm2px(Vec(PLACE_ICON_W, FOLDER_INK));
				                                   if (k)
					                                   paintFolder(vg, at, size, col, userGone);
				                                   else
					                                   paintFactory(vg, at, size, col);
			                                   });
			icon->act = [=]() {
				if (rig)
					showPlace(place);
			};
			icon->lit = [=]() { return placeLit[k]; };
			icon->ink = [=]() { return k && userGone ? SCHEME_RED : SCHEME_YELLOW; };
			icon->tipOf = [=]() {
				return std::string(!k ? "Factory rigs"
				                   : userGone ? "Your rigs: the folder is missing, set another in the right-click menu"
				                              : "Your rigs");
			};
			icon->lesson = k ? RIG_USER_LESSON : RIG_FACTORY_LESSON;
		}
		for (int side = 0; side < 2; side++) {
			// The next rig under the folder, the previous over SAVE.
			const int by = side ? -1 : 1;
			GlyphButton* a = addStepArrow(nameS, Vec(0.f, side ? DOWN_Y : UP_Y), Vec(RIG_W, ARROW_H), side ? ARROW_DOWN : ARROW_UP);
			a->act = [=]() { stepRig(by); };
			a->tipOf = [=]() { return std::string(by > 0 ? "Next rig" : "Previous rig"); };
			a->lesson = by > 0 ? RIG_STEP_NEXT_LESSON : RIG_STEP_PREV_LESSON;
			a->can = [=]() { return !module || canStep[by > 0]; };
		}
	}
	/** SAVE on the name glass's foot. */
	void addSave(Rig* module) {
		RigButton* saveB = addButton(Rig::SAVE_PARAM, SAVE_Y, "SAVE", -1, true);
		saveB->onPress = [=]() { save(); };
		// SAVED, dark, while the rig is the file; SAVE, lit, once it is not.
		saveB->lit = [=]() { return module && saveDiffers && !lone; };
		saveB->live = [=]() { return !module || !lone; };
		saveB->textOf = [=]() { return std::string(!module || saveDiffers ? "SAVE" : "SAVED"); };
		// The note's button, carved off SAVE's row at its left, and a faint
		// rule between the two, as between COPY and PASTE.
		saveB->box.pos.x += mm2px(NOTE_BTN_W);
		saveB->box.size.x -= mm2px(NOTE_BTN_W);
		saveB->ruleLeft = true;
	}
	/** The note's button, in the room carved off SAVE's row at its left. */
	void addNoteButton(Rig* module) {
		auto written = [=]() { return module && !module->note.empty(); };
		GlyphButton* note = addGlyphButton(nameScreen, Vec(0.f, SAVE_Y - NAME_Y), Vec(NOTE_BTN_W, STRIP_H),
		                                   [=](NVGcontext* vg, float cx, float cy, NVGcolor col) {
			                                   paintStickyNote(vg, cx, cy, NOTE_GLYPH, written(), col);
		                                   });
		note->ink = [=]() { return written() ? NOTE_PAPER : NOTE_EMPTY; };
		note->lit = [=]() { return module && noteOpen && noteOpen->id == module->id; };
		note->act = [=]() {
			if (module)
				toggleNote(module);
		};
		note->tipOf = [=]() {
			if (module && noteOpen && noteOpen->id == module->id)
				return std::string("Close the note");
			return std::string(written() ? "Edit the note" : "Add a note");
		};
		note->lesson = RIG_NOTE_LESSON;
	}
	/** The scene: its number between two arrows, a stroke on it walking the
	scenes. */
	void addScene(Rig* module) {
		SceneScreen* scene = createWidget<SceneScreen>(mm2px(Vec(0.f, SCENE_Y)));
		scene->box.size = mm2px(Vec(RIG_W, HEAD_SCREEN_H));
		scene->module = module;
		addChild(scene);
		for (int side = 0; side < 2; side++) {
			GlyphButton* a = addStepArrow(scene, Vec(side ? RIG_W - ARROW_W : 0.f, 0.f), Vec(ARROW_W, HEAD_SCREEN_H),
			                            side ? ARROW_RIGHT : ARROW_LEFT);
			a->act = [=]() {
				// Round the scenes there are, the last's next scene one
				// (`stepRound`); with one scene it lands on itself and does
				// nothing, never recalling the scene already showing over what
				// the hand has done since.
				if (!rig)
					return;
				const int to = rig->handTarget(side ? 1 : -1);
				if (to != rig->shownForUi())
					pickScene(to);
			};
			// The lesson with the name, for the state on screen: none while
			// the jack chooses or there is no other scene to go to.
			a->tipOf = [=]() {
				const int n = rig ? relaxed(rig->countNow) : 2;
				const bool jack = rig && relaxed(rig->patchedNow);
				a->lesson = jack || n < 2 ? "" : rigEnd(side ? RIG_NEXT_LESSON : RIG_PREV_LESSON, rigSeqs);
				return std::string(side ? "Next scene" : "Previous scene") + (jack ? RIG_JACK_CHOOSES : n == 0 ? RIG_NO_SCENES : "");
			};
			a->can = [=]() {
				if (!rig)
					return true;
				if (relaxed(rig->patchedNow))
					return false; // the jack wins while it is patched
				return relaxed(rig->countNow) > 1; // round and round, while there is another
			};
		}
		SceneDrag* d = createWidget<SceneDrag>(mm2px(Vec(ARROW_W, 0.f)));
		d->box.size = mm2px(Vec(RIG_W - 2.f * ARROW_W, HEAD_SCREEN_H));
		d->owner = this;
		scene->addChild(d);
	}
	/** The hand's four rows on one ground: REPLACE first, INSERT under it with
	the arrow after its word, COPY and PASTE halves of a row, DELETE. */
	void addHand(Rig* module) {
		// Whether there is a scene for a button to work on.
		auto anyScene = [=]() { return !module || relaxed(module->countNow) > 0; };
		addGround(HAND_Y, HAND_ROWS * STRIP_H);
		RigButton* replaceB = addButton(Rig::REPLACE_PARAM, HAND_Y, "REPLACE");
		replaceB->onPress = [=]() { replace(); };
		replaceB->live = anyScene;
		replaceB->breathes = [=]() { return sceneDiffers; };
		RigButton* insertB = addButton(Rig::INSERT_PARAM, HAND_Y + STRIP_H, "INSERT");
		insertB->onPress = [=]() { insert(); };
		insertB->arrowAfter = true;
		insertB->live = [=]() { return !module || !lone; };
		IconButton* copyB = addButton<IconButton>(Rig::COPY_PARAM, HAND_Y + 2.f * STRIP_H, "COPY", 0);
		copyB->onPress = [=]() { copy(); };
		copyB->live = anyScene;
		IconButton* pasteB = addButton<IconButton>(Rig::PASTE_PARAM, HAND_Y + 2.f * STRIP_H, "PASTE", 1);
		pasteB->paste = true;
		pasteB->ruleLeft = true;
		pasteB->onPress = [=]() { paste(); };
		pasteB->live = [=]() { return !module || clipFull; };
		RigButton* deleteB = addButton(Rig::DELETE_PARAM, HAND_Y + 3.f * STRIP_H, "DELETE");
		deleteB->onPress = [=]() { deleteShown(); };
		deleteB->live = anyScene;
	}
	/** SCENE, PREV and NEXT on the column. */
	void addJacks(Rig* module) {
		addInput(createInputCentered<InPort>(mm2px(Vec(RIG_CX, seqRowY(4))), module, Rig::SCENE_INPUT));
		addInput(createInputCentered<InPort>(mm2px(Vec(RIG_CX, seqRowY(5))), module, Rig::PREV_INPUT));
		addInput(createInputCentered<InPort>(mm2px(Vec(RIG_CX, seqRowY(6))), module, Rig::NEXT_INPUT));
	}
	/** The panel top to foot, each stage adding its widgets in the order they
	are drawn and found in the tree. */
	RigWidget(Rig* module) {
		rig = module;
		RigPanel* panel = new RigPanel;
		open(module, panel);
		addRigName(module);
		addSave(module);
		addNoteButton(module);
		addScene(module);
		addHand(module);
		addJacks(module);
	}
};

/** The rig's note shows on its glass while the pointer rests on the name,
when something is written and it is not open to write in already. */
void NameScreen::onEnter(const EnterEvent& e) {
	Hovered::onEnter(e);
	Rig* r = owner ? owner->rig : nullptr;
	if (r && !r->note.empty() && !(noteOpen && noteOpen->id == r->id)) {
		if (notePeek)
			notePeek->close();
		notePeek = openNote(r, true);
	}
}
void NameScreen::onLeave(const LeaveEvent& e) {
	Hovered::onLeave(e);
	if (notePeek && owner && owner->rig && notePeek->id == owner->rig->id)
		notePeek->close();
}

bool NoteSheet::follow() {
	Rig* r = rigOf(id);
	RigWidget* w = r ? dynamic_cast<RigWidget*>(APP->scene->rack->getModule(id)) : nullptr;
	if (!w || !w->nameScreen)
		return false;
	const float z = w->nameScreen->getAbsoluteZoom();
	const Vec at = w->nameScreen->getAbsoluteOffset(Vec(0.f, 0.f));
	// One pixel under the name's own glass, so no seam opens between the two
	// at any zoom.
	const Vec size = mm2px(Vec(NOTE_W, NAME_H));
	zoom->setZoom(z);
	zoom->box.size = size.mult(z);
	box.size = zoom->box.size;
	box.pos = Vec(at.x - box.size.x + 1.f, at.y);
	return true;
}
bool NoteSheet::peekWanted() {
	RigWidget* w = dynamic_cast<RigWidget*>(APP->scene->rack->getModule(id));
	return w && w->nameScreen && w->nameScreen->hovered && !(noteOpen && noteOpen->id == id);
}

/** The words for the state on screen, the lesson set with them: what a scene
is, and, while a drag has another scene to go to, what the drag does. */
const Hinted* SceneDrag::hinted() {
	static Hinted drag;
	if (drag.gestures.empty()) {
		drag.gestures = {&G_RIG_SCENE_DRAG};
		drag.hint = hintOf(drag.gestures);
	}
	if (!owner || !owner->rig || relaxed(owner->rig->patchedNow) || owner->rig->sceneCount() < 2)
		return nullptr;
	return &drag;
}

std::string SceneDrag::hoverTip() {
	lesson = RIG_SCENE_IS;
	if (!owner || !owner->rig)
		return "Scene";
	if (relaxed(owner->rig->patchedNow))
		return std::string("Scene") + RIG_JACK_CHOOSES;
	const int n = owner->rig->sceneCount();
	if (n == 0)
		return owner->lone ? std::string("Scene") + RIG_NOTHING_RIGHT : "No scenes yet: INSERT stores the first";
	if (n > 1)
		lesson += owner->rigSeqs ? RIG_SCENE_DRAG_SEQS : RIG_SCENE_DRAG;
	return string::f("Scene %d of %d", owner->rig->shownForUi() + 1, n);
}

void SceneDrag::onDragStart(const DragStartEvent& e) {
	dragging = false;
	if (e.button != GLFW_MOUSE_BUTTON_LEFT || !owner || !owner->rig)
		return;
	Rig* rig = owner->rig;
	if (relaxed(rig->patchedNow) || rig->sceneCount() == 0)
		return;
	dragging = true;
	from = at = rig->shownForUi();
	dragAccum = 0.f;
	moved = false;
	strokeBegins();
	strokeMolecule.clear();
	before.clear();
	for (Module* m : owner->moleculeModules()) {
		strokeMolecule.push_back(HeldModule(m));
		Member* rm = sceneMember(m);
		std::vector<float> vs(sceneValues(m, rm));
		for (size_t i = 0; i < vs.size(); i++)
			vs[i] = sceneValue(m, rm, i);
		before.push_back(vs);
	}
	if (settings::knobMode <= settings::KNOB_MODE_LINEAR)
		APP->window->cursorLock();
}

void SceneDrag::strokeMove(const DragMoveEvent& e) {
	if (!dragging || e.button != GLFW_MOUSE_BUTTON_LEFT)
		return;
	Rig* rig = owner->rig;
	const float dy = strokeDelta(e);
	moved = moved || strayed();
	dragAccum += dy;
	const int took = (int)stepsTaken();
	if (took == 0)
		return;
	// Up is a later scene. At either end the travel past it is dropped, so a
	// stroke past the last turns back at once (the over-drag rule).
	const int last = Rig::lastOf(rig->sceneCount());
	const int to = std::min(std::max(at - took, 0), last);
	if (to == 0 || to == last)
		dragAccum = 0.f;
	if (to == at)
		return;
	at = to;
	rig->handPick(at, standing(), nullptr, false);
}

void SceneDrag::onDragEnd(const DragEndEvent& e) {
	if (!dragging || e.button != GLFW_MOUSE_BUTTON_LEFT)
		return;
	dragging = false;
	if (settings::knobMode <= settings::KNOB_MODE_LINEAR)
		APP->window->cursorUnlock();
	Rig* rig = owner->rig;
	// The stroke one step of undo, from the params, the scene settings and
	// the scene it began on, and the molecule reset once, where it lands. A stroke that comes back to
	// its own scene has still recalled it over the hand's edits on the way, so
	// it is nothing only when nothing moved.
	history::ComplexAction* h = new history::ComplexAction;
	h->name = "recall scene";
	for (size_t k = 0; k < strokeMolecule.size() && k < before.size(); k++) {
		Module* m = strokeMolecule[k].get();
		if (!m)
			continue;
		Member* rm = sceneMember(m);
		const size_t n = sceneValues(m, rm);
		for (size_t i = 0; i < n && i < before[k].size(); i++) {
			const float now = sceneValue(m, rm, i);
			if (now != before[k][i])
				pushSceneValue(h, m, i, before[k][i], now);
		}
	}
	if (at == from && h->isEmpty()) {
		delete h;
		return;
	}
	if (Rig::restartsOn(Rig::ACT_PICK, relaxed(rig->patchedNow)))
		Rig::resetFromUi(standing());
	SceneShown* sh = new SceneShown;
	sh->moduleId = rig->id;
	sh->from = from;
	sh->to = at;
	h->push(sh);
	APP->history->push(h);
}

void NameScreen::onButton(const ButtonEvent& e) {
	// The arrows at the ends are children, and hear their clicks first.
	Screen::onButton(e);
	if (e.isConsumed())
		return;
	if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && owner && owner->rig) {
		owner->nameMenu();
		e.consume(this);
	}
}

void NameScreen::drawLayer(const DrawArgs& args, int layer) {
	Screen::drawLayer(args, layer);
	if (layer != 1)
		return;
	std::string text = owner && owner->rig ? owner->name() : std::string();
	const bool none = text.empty();
	if (none)
		text = "no rig";
	std::shared_ptr<Font> font = plateFont();
	if (!font)
		return;
	NVGcontext* vg = args.vg;
	nvgFontFaceId(vg, font->handle);
	nvgFontSize(vg, NAME_PX);
	nvgTextLetterSpacing(vg, 0.3f);
	nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
	// Upright, read from the foot up as the Qnt's words are, in the room
	// between its two arrows: one line in the middle where it fits, else up
	// to as many side by side as the screen's width holds, the first at the
	// left, balanced about the middle (`layName`).
	const float top = mm2px(UP_Y + ARROW_H), foot = mm2px(DOWN_Y);
	const float room = foot - top - mm2px(1.f);
	if (text != laidText || laidLines.empty()) {
		const int most = std::max(1, std::min(4, (int)((box.size.x - mm2px(NAME_SIDE_AIR) * 2.f) / NAME_PITCH)));
		laidText = text;
		laidLines = layName(text, room, most, [&](const std::string& l) {
			float b[4];
			return nvgTextBounds(vg, 0.f, 0.f, l.c_str(), nullptr, b);
		});
	}
	const float a = none ? 0.35f : hovered ? 1.f : 0.85f;
	nvgFillColor(vg, color::alpha(SCHEME_YELLOW, a));
	const int n = (int)laidLines.size();
	for (int k = 0; k < n; k++) {
		nvgSave(vg);
		nvgTranslate(vg, box.size.x / 2.f + (k - (n - 1) / 2.f) * NAME_PITCH, (top + foot) / 2.f);
		nvgRotate(vg, -M_PI / 2.f);
		nvgText(vg, 0.f, 0.f, laidLines[k].c_str(), nullptr);
		nvgRestore(vg);
	}
}

} // namespace

Model* modelRig = createModel<Rig, RigWidget>("RIG");
