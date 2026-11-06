// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Engine.hpp"
#include <atomic>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

/** The RIG's engine: scenes of the molecule standing to its right, stored by
each module's identity and recalled on the sample.

The molecule is the bond to the RIG's right: every module of ours touching the
one before it, across a GAP, up to a bypassed module, which splits it as it
splits the engine's rig (`inRig`). The RIG itself is no member of the rig. It derives from Module and not
from Member, so the parse never sees it: it reads nothing and tells
nothing, it stands outside every block, and the run to its right starts
afresh exactly as it would beside any module that is not ours. Nothing in
the manifest names it, which is what standing outside the grammar means.

A scene is every param of every module of the molecule and the settings of
its menu that change its sound (`Member::sceneSettings`), keyed by the
module's id, so a module added to the molecule has no stored value and keeps its own, and
one taken away drops out (pruned whenever the scenes are written down, against
the modules the rack still has). The engine recalls a scene by setting those
params itself, on the sample the scene is chosen or the SCENE jack moves.
That breaks the rule that the engine never sets a param
(`postWrite`) on purpose and in the one way the rule allows: a recall is the
patch performing on the params, as a Seq's REC take is, not a hand's gesture,
so it is no step of undo and goes through no ring. Every module it moves is
woken, as the hand's widget wakes it (`Member::wake`).

Headless, so a test drives it with no Rack: the UI's half (storing,
deleting, the file) is the same methods, called on the UI thread in Rack and
on the test's own thread there. The bank is shared under a spin lock the audio thread only ever
tries, so a recall that finds it held waits for the next sample rather than
for the UI. */

namespace rig {

/** A scene: a module's values by its id, its params first and after them its
scene settings (`Member::sceneSettings`, a Seq's BLEP, Oversampling and
rate's mode, a Clock's PPQN and BLEP), so value `i` is param `i` while there is
one. A scene stored before the settings were part of a scene stops
at the params, and a recall leaves the settings alone. */
using Scene = std::map<int64_t, std::vector<float>>;

/** How many values `m` has in a scene, value `i` of them, setting it, and
whether it is whole (taken from the nearer scene in a morph). A param is
whole where Rack calls it snapped (`snapEnabled`). Setting one is a store
and nothing more, so the audio thread's recall may call it. */
inline Member* sceneMember(Module* m) { return dynamic_cast<Member*>(m); }
inline size_t sceneValues(Module* m, Member* rm) {
	return m->params.size() + (rm ? (size_t)rm->sceneSettings() : 0);
}
inline float sceneValue(Module* m, Member* rm, size_t i) {
	return i < m->params.size() ? m->params[i].getValue() : rm->sceneSetting((int)(i - m->params.size()));
}
/** Value `i` as `m` can hold it: a param inside its quantity's range, whole
where it snaps, and its default when the value is no number; a setting as
given, each setting keeping its own range on its own path, or NAN, to be left
as it stands, when it is no number. A scene is whatever a file held, edited by
hand or saved before a range narrowed, and a value past the range reaches a
cable as a voltage no knob can give, or as a NaN every module downstream
carries on. */
inline float sceneHeld(Module* m, size_t i, float v) {
	if (i >= m->params.size())
		return std::isfinite(v) ? v : NAN;
	ParamQuantity* pq = i < m->paramQuantities.size() ? m->paramQuantities[i] : nullptr;
	if (!pq)
		return std::isfinite(v) ? v : 0.f;
	if (!std::isfinite(v))
		return pq->getDefaultValue();
	v = math::clamp(v, pq->getMinValue(), pq->getMaxValue());
	return pq->snapEnabled ? std::round(v) : v;
}
inline void setSceneValue(Module* m, Member* rm, size_t i, float v) {
	v = sceneHeld(m, i, v);
	if (std::isnan(v))
		return;
	if (i < m->params.size())
		m->params[i].setValue(v);
	else
		rm->setSceneSetting((int)(i - m->params.size()), v);
}
/** Value `i` set by the hand on the UI thread: a param through the engine,
as a knob sets it, so a smoothing Rack has in flight on it stops rather than
carrying it on past the recall; a setting as `setSceneValue` stores it. With
no engine, as in a headless test, it is stored. */
inline void handSetSceneValue(Module* m, Member* rm, size_t i, float v) {
	if (i < m->params.size() && APP && APP->engine)
		APP->engine->setParamValue(m, (int)i, sceneHeld(m, i, v));
	else
		setSceneValue(m, rm, i, v);
}
inline bool sceneValueWhole(Module* m, Member* rm, size_t i) {
	if (i < m->params.size())
		return i < m->paramQuantities.size() && m->paramQuantities[i] && m->paramQuantities[i]->snapEnabled;
	return rm->sceneSettingWhole((int)(i - m->params.size()));
}

/** A lock the audio thread only tries and the UI thread holds for a copy's
length. */
struct SpinLock {
	std::atomic_flag held = ATOMIC_FLAG_INIT;
	bool tryLock() { return !held.test_and_set(std::memory_order_acquire); }
	void lock() {
		while (held.test_and_set(std::memory_order_acquire))
			std::this_thread::yield();
	}
	void unlock() { held.clear(std::memory_order_release); }
};

/** A scene's value as JSON, and back. A value that is no number is written
as null rather than dropped, which jansson's real would do, shifting every
value after it onto the next param; read back, null or anything else that is
no number is NAN, which `sceneHeld` turns into a param's default. */
inline json_t* sceneNumberJson(float v) { return std::isfinite(v) ? json_real(v) : json_null(); }
inline float sceneNumberOf(json_t* xJ) {
	return json_is_number(xJ) && std::isfinite(json_number_value(xJ)) ? (float)json_number_value(xJ) : NAN;
}

inline json_t* sceneToJson(const Scene& s, const std::set<int64_t>* keep = nullptr) {
	json_t* sJ = json_object();
	for (const auto& kv : s) {
		if (keep && !keep->count(kv.first))
			continue;
		json_t* vJ = json_array();
		for (float v : kv.second)
			json_array_append_new(vJ, sceneNumberJson(v));
		json_object_set_new(sJ, std::to_string(kv.first).c_str(), vJ);
	}
	return sJ;
}

/** A scene from JSON, its ids passed through `remap` when one is given (a
rig file's ids are the ids its modules had when it was saved). */
inline Scene sceneFromJson(json_t* sJ, const std::map<int64_t, int64_t>* remap = nullptr) {
	Scene s;
	const char* key;
	json_t* vJ;
	json_object_foreach(sJ, key, vJ) {
		int64_t id = std::strtoll(key, nullptr, 10);
		if (remap) {
			auto it = remap->find(id);
			if (it == remap->end())
				continue;
			id = it->second;
		}
		std::vector<float> vs;
		size_t i;
		json_t* xJ;
		json_array_foreach(vJ, i, xJ) vs.push_back(sceneNumberOf(xJ));
		s[id] = vs;
	}
	return s;
}

inline json_t* scenesToJson(const std::vector<Scene>& scenes, const std::set<int64_t>* keep = nullptr) {
	json_t* aJ = json_array();
	for (const Scene& s : scenes)
		json_array_append_new(aJ, sceneToJson(s, keep));
	return aJ;
}
/** The most scenes a RIG holds: what the two cells of its screen hold, decimal
to 99 and hex past it to FF (`drawCount`). INSERT and PASTE stop there, and a file holding more
is read to it. */
const int MAX_SCENES = 255;
inline std::vector<Scene> scenesFromJson(json_t* aJ, const std::map<int64_t, int64_t>* remap = nullptr) {
	std::vector<Scene> out;
	size_t i;
	json_t* sJ;
	json_array_foreach(aJ, i, sJ) {
		if ((int)out.size() >= MAX_SCENES)
			break;
		out.push_back(sceneFromJson(sJ, remap));
	}
	return out;
}

/** What a scene stores of value `i`: the value as the molecule plays and
shows it, so a param a cable holds is stored as the cable shows it, never
as the param standing hidden under it. */
inline float sceneShown(Module* m, Member* rm, size_t i) {
	float shown;
	if (rm && i < m->params.size() && rm->heldByCable((int)i, shown))
		return shown;
	return sceneValue(m, rm, i);
}
/** Whether value `i` is the hand's to set, a recall's to move: not while a
cable holds it. */
inline bool sceneFree(Module* m, Member* rm, size_t i) {
	float shown;
	return !(rm && i < m->params.size() && rm->heldByCable((int)i, shown));
}

/** The settings of the molecule as it stands: every param of every module,
as it plays, and its scene settings after them. */
inline Scene captureScene(const std::vector<Module*>& molecule) {
	Scene s;
	for (Module* m : molecule) {
		Member* rm = sceneMember(m);
		std::vector<float> vs(sceneValues(m, rm));
		for (size_t i = 0; i < vs.size(); i++)
			vs[i] = sceneShown(m, rm, i);
		s[m->id] = vs;
	}
	return s;
}

/** A file dialog's answer given by the test rig rather than a hand, taken
once by the RIG's SAVE and cleared: the test harness sets it, the
way it holds shift (`heldShift`), since a script has no dialog to type in.
Nothing in the plugin sets it, and it ships in the release form as it is,
always empty there, so SAVE is one code path in both forms. */
inline std::string& dialogAnswer() {
	static std::string answer;
	return answer;
}

struct RigCore : Module, Bonder {
	/** The hand's buttons, each answered by the UI the moment it is
	pressed; the engine reads nothing of them. */
	enum ParamId {
		SAVE_PARAM,    // the molecule to a new rig file
		REPLACE_PARAM, // the molecule into the scene showing (`storeScene`)
		INSERT_PARAM,  // a new scene after the one showing, the molecule stored into it (`insertScene`)
		COPY_PARAM,    // the scene showing kept aside, the RIG's own and never saved
		PASTE_PARAM,   // and set in after the scene showing (`insertGiven`)
		DELETE_PARAM,  // the scene showing taken out (`deleteScene`)
		PARAMS_LEN
	};
	enum InputId {
		SCENE_INPUT,
		PREV_INPUT,
		NEXT_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	// Shared, under `bank`'s lock: the scenes, and the UI's word on which one
	// shows after a change to them.
	SpinLock lock;
	std::vector<Scene> scenes;
	int wantCurrent = 0;
	bool wantReapply = false; // a whole new bank: the SCENE jack, if patched, applies itself again
	bool wantRestart = false; // a rig loaded: the molecule reset, once, as the bracket's RESET does; the engine clears it
	std::atomic<uint32_t> generation{1}; // moved by the UI after every change to the above

	// The UI thread's own: the rig file this RIG holds, the rig's note, and
	// the ids the rack has, for pruning (filled by the widget each frame;
	// never, headless). The note is the rig's, saved in its file as its
	// scenes are, and an edit of it is a change to the rig.
	std::string file;
	std::string note;
	std::set<int64_t> live;
	bool liveKnown = false;
	/** What the rig file holds, as SAVE asks it: the molecule's make-up, its modules' plugin and model slugs left to
	right, and the scenes stored, by the ids the modules have here. Taken when a
	rig is loaded or saved and kept with the RIG's state, so an undo, a redo and
	a patch reopened keep it; unknown, the widget works it out from the file. A
	module's params, its steps and its cables are using the rig, not changing
	it, and are not here; nor is the scene showing. The note is. */
	bool cleanKnown = false;
	std::vector<std::string> cleanModels;
	std::vector<Scene> cleanScenes;
	std::string cleanNote;
	/** And, for a rig with no scenes, whose file is its one scene: each module's params and what it
	saves beside them (`settingsData`), by place, as the file holds them. */
	std::vector<std::vector<float>> cleanParams;
	std::vector<std::string> cleanData;

	// The engine's own, read by nothing else.
	uint32_t seenGen = 0;
	int count = 0;    // scenes, as of the last generation adopted
	int current = 0;  // the scene showing, nought based, and nought while there are none
	bool pending = false; // a recall asked for and not yet made (the bank was held)
	float lastPos = NAN;  // where the SCENE jack last put the molecule, nought based, or none
	bool resetDue = false;        // a scene change whose reset waits for its recall
	bool restartPending = false;  // a load's reset, waiting for a molecule to reset
	dsp::SchmittTrigger prevTrig, nextTrig;

	// Published at the end of every sample for the screen.
	std::atomic<int> currentNow{0}, countNow{0};
	std::atomic<bool> patchedNow{false};
	std::atomic<uint32_t> adoptedNow{0}; // the generation the engine has taken
	std::atomic<float> posNow{0.f};
	std::atomic<uint32_t> sceneRGB{0}; // the cable in my SCENE jack's colour, packed, the widget's to write

	RigCore() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		// Two tiers (Tips.hpp): the name at once, the lesson after a rest
		// (Words.hpp, where every RIG control's lessons are kept together),
		// here as a RIG is born, with no file, no scene and nothing copied;
		// the widget says them again for the state on screen every frame.
		configButton<WithHint<SwitchQuantity>>(SAVE_PARAM, RIG_SAVE_NAME);
		retipParam(this, SAVE_PARAM, {RIG_SAVE_NAME, RIG_SAVE_NEW_LESSON, Gestures{}});
		configButton(INSERT_PARAM, "Insert scene");
		retipParam(this, INSERT_PARAM, {"Insert scene", RIG_INSERT_FIRST_LESSON});
		configButton(REPLACE_PARAM, "Replace scene");
		retipParam(this, REPLACE_PARAM, {"Replace scene: no scenes yet", ""});
		configButton(COPY_PARAM, "Copy scene");
		retipParam(this, COPY_PARAM, {"Copy scene: no scenes yet", ""});
		configButton<WithHint<SwitchQuantity>>(PASTE_PARAM, "Paste scene");
		retipParam(this, PASTE_PARAM, {"Paste scene: nothing copied", "", Gestures{}});
		configButton(DELETE_PARAM, "Delete scene");
		retipParam(this, DELETE_PARAM, {"Delete scene: no scenes yet", ""});
		configInput(SCENE_INPUT, "Scene");
		retipIn(this, SCENE_INPUT, {"Scene", RIG_SCENE_IN_LESSON});
		configInput(PREV_INPUT, "Previous scene");
		retipIn(this, PREV_INPUT, {"Previous scene", rigEnd(RIG_PREV_LESSON, false).c_str()});
		configInput(NEXT_INPUT, "Next scene");
		retipIn(this, NEXT_INPUT, {"Next scene", rigEnd(RIG_NEXT_LESSON, false).c_str()});
	}

	/** The last scene there is, nought based, or nought while there are
	none: the scene showing never stands past it. */
	static int lastOf(int n) { return n > 0 ? n - 1 : 0; }
	/** The scene nearer `pos`, nought based, of `n`. */
	static int nearer(float pos) { return (int)std::floor(pos + 0.5f); }

	/** The molecule, in the engine: every module of ours bonded to my right,
	across a GAP, up to a bypassed one; `f` hears the members only, a GAP
	holding nothing a scene keeps. The audio thread's own links; the UI walks
	where the panels stand (`RigWidget::molecule`). */
	template <typename F>
	void eachInMolecule(F f) {
		int guard = 0;
		for (Module* m = rightExpander.module; m && guard < 4096; m = m->rightExpander.module, guard++) {
			if (inRig(m)) {
				f(m);
				continue;
			}
			if (!isBonder(m) || m->isBypassed() || !bondsToLeft(m))
				break;
		}
	}

	/** A scene change resets the molecule, as the bracket's RESET does (Bracket.hpp):
	every member asks it, and the rig's host restarts every node a reset
	restarts, on the sample it reads the ask. Set on the sample of the recall,
	so the rig hears the new settings and the reset together. */
	void resetMolecule() {
		eachInMolecule([](Module* m) { static_cast<Member*>(m)->resetAsked.store(true, std::memory_order_relaxed); });
	}

	/** Whether an edge at NEXT or PREV on this sample is the pulse a start of
	my molecule gave: a member's rig started or restarted (`startedAt`, its
	first sample, the bracket's RESET, a change of scene, a rig loaded) less than
	one trigger ago. A start gives an EOC, which a user values for keeping
	other gear in step, so the pulse stays on its jack and only this RIG
	declines to step on it. Why a window and not the cause itself: a cable
	carries a voltage and not where it came from, and the pulse may reach me
	through any module of the patch, so nothing on this side can know which
	edge a start made. What is exact is its length: every trigger the plugin
	puts out stands high `pulseFrames` samples, counted in integer frames, so
	an edge rising inside that many frames of a start is one the start could
	have made, and one rising later is not. Read only on an edge, so the
	members' atomics cost nothing on a quiet sample. */
	bool echoOfStart(const ProcessArgs& args) {
		const int64_t within = pulseFrames(args.sampleRate);
		bool echo = false;
		eachInMolecule([&](Module* m) {
			const int64_t at = static_cast<Member*>(m)->startedAt.load(std::memory_order_relaxed);
			if (at != Member::NEVER_STARTED && args.frame >= at && args.frame - at <= within)
				echo = true;
		});
		return echo;
	}

	/** Scene `a` and scene `b` mixed by `t`, nought being `a` exactly: a
	continuous param drawn between the two, a count or a switch (`snapEnabled`,
	Rack's own word for a param that is a whole number) taken from the nearer.
	A module or a param one scene lacks takes the nearer scene's value where
	that scene has it, and is left alone where it has not. False, and nothing
	moved, when the UI holds the bank; the next sample tries again. */
	bool recall(int a, int b, float t) {
		if (!lock.tryLock())
			return false;
		const int n = (int)scenes.size();
		if (a < 0 || a >= n) {
			lock.unlock();
			return true;
		}
		b = std::min(std::max(b, 0), n - 1);
		const Scene& A = scenes[a];
		const Scene& B = scenes[b];
		const bool nearA = t < 0.5f;
		eachInMolecule([&](Module* m) {
			const Scene::const_iterator ia = A.find(m->id), ib = B.find(m->id);
			const std::vector<float>* va = ia != A.end() ? &ia->second : nullptr;
			const std::vector<float>* vb = ib != B.end() ? &ib->second : nullptr;
			if (!va && !vb)
				return;
			bool moved = false;
			Member* rm = static_cast<Member*>(m);
			const size_t n = sceneValues(m, rm);
			for (size_t i = 0; i < n; i++) {
				const bool hasA = va && i < va->size(), hasB = vb && i < vb->size();
				float to;
				if (hasA && hasB)
					to = sceneValueWhole(m, rm, i) ? (nearA ? (*va)[i] : (*vb)[i]) : (*va)[i] + ((*vb)[i] - (*va)[i]) * t;
				else if (nearA ? hasA : hasB)
					to = nearA ? (*va)[i] : (*vb)[i];
				else
					continue;
				to = sceneHeld(m, i, to);
				if (std::isnan(to))
					continue;
				if (sceneValue(m, rm, i) != to) {
					// The patch performing on the param, as a REC take does:
					// no gesture of the hand, so no undo and no `postWrite`.
					// A setting past the params is an atomic stored, no more.
					setSceneValue(m, rm, i, to);
					moved = true;
				}
			}
			if (moved)
				rm->land(); // a recall is heard at once, never glided
		});
		lock.unlock();
		return true;
	}

	void process(const ProcessArgs& args) override {
		// The UI's word, taken whole or not at all.
		const uint32_t g = generation.load(std::memory_order_acquire);
		if (g != seenGen && lock.tryLock()) {
			count = (int)scenes.size();
			current = std::min(std::max(wantCurrent, 0), lastOf(count));
			if (wantReapply)
				lastPos = NAN;
			if (wantRestart) {
				restartPending = true;
				wantRestart = false;
			}
			pending = false;
			seenGen = g;
			lock.unlock();
		}
		bool nextEdge = nextTrig.process(inputs[NEXT_INPUT].getVoltage(), 0.1f, 1.f);
		bool prevEdge = prevTrig.process(inputs[PREV_INPUT].getVoltage(), 0.1f, 1.f);
		// A start's own pulse is no step: a change of scene resets the
		// molecule, the reset gives its Seqs' EOC, and an EOC cabled back here
		// would step on again at once. The edge is still taken by its trigger, so it re-arms.
		if ((nextEdge || prevEdge) && echoOfStart(args))
			nextEdge = prevEdge = false;
		const bool patched = inputs[SCENE_INPUT].isConnected();
		float pos = (float)current;
		bool recalling = false; // a recall asked for this sample and not made, the bank being held
		// A load's reset waits for the molecule the load built to be bonded.
		bool bonded = false;
		if (restartPending)
			eachInMolecule([&](Module*) { bonded = true; });
		if (restartPending && bonded) {
			restartPending = false;
			resetDue = true;
		}
		if (patched) {
			// The jack wins while it is patched: the triggers are heard and
			// do nothing (and the hand's arrows ask nothing, `handPick`). Below one volt is scene one and above
			// the last scene is the last; there is no scene nought.
			pending = false;
			if (count > 0) {
				pos = clamp(inputs[SCENE_INPUT].getVoltage() - 1.f, 0.f, (float)(count - 1));
				if (!(pos == lastPos)) {
					// The CV is continuous, so its floor is the knife-edge it is
					// meant to be: a whole volt is that scene with nothing mixed.
					const int a = (int)std::floor(pos);
					if (recall(a, a + 1, pos - (float)a))
						lastPos = pos;
					else
						recalling = true;
				}
				// The jack never resets the molecule, a jump from one whole volt
				// to another included (`restartsOn`).
				const int to = nearer(pos);
				current = to;
			}
		}
		else {
			lastPos = NAN;
			int to = current;
			// The triggers step through the scenes there are, round and round,
			// so a Seq's EOC can step a song.
			if (count > 0) {
				if (nextEdge)
					to = (to + 1) % count;
				if (prevEdge)
					to = (to + count - 1) % count;
			}
			if (to != current) {
				current = to;
				pending = true;
				resetDue = restartsOn(ACT_STEP, patched);
			}
			if (pending && recall(current, current, 0.f))
				pending = false;
			recalling = pending;
			pos = (float)current;
		}
		if (resetDue && !recalling) {
			resetMolecule();
			resetDue = false;
		}
		currentNow.store(current, std::memory_order_relaxed);
		countNow.store(count, std::memory_order_relaxed);
		patchedNow.store(patched, std::memory_order_relaxed);
		posNow.store(pos, std::memory_order_relaxed);
		adoptedNow.store(seenGen, std::memory_order_release);
	}

	// The UI's half, on the UI thread (on a headless test's own, there).

	/** Moved after every change to the bank, which the engine takes on the
	next sample it can. */
	void changed(int show, bool reapply, bool restart = false) {
		wantCurrent = show;
		wantReapply = reapply;
		wantRestart = wantRestart || restart; // until the engine has taken it
		generation.fetch_add(1, std::memory_order_release);
	}
	/** REPLACE: the molecule's settings written over scene `at`. False, and
	nothing stored, where there is no scene `at` (INSERT makes the first). */
	bool storeScene(int at, const std::vector<Module*>& molecule) {
		return storeGiven(at, captureScene(molecule));
	}
	/** And PASTE's with shift: `s` written over scene `at`, which stays
	shown; the molecule is the UI's to recall (`handPick`). False, and
	nothing stored, where there is no scene `at`. */
	bool storeGiven(int at, Scene s) {
		lock.lock();
		const bool there = at >= 0 && at < (int)scenes.size();
		if (there) {
			scenes[at] = std::move(s);
			changed(at, false);
		}
		lock.unlock();
		return there;
	}
	/** INSERT: a new scene after scene `after`, holding the molecule's settings,
	and shown, the scenes after it renumbered on; with none, scene one. The
	molecule is already the scene it now shows, so nothing is recalled and
	nothing reset. Where it went. */
	int insertScene(int after, const std::vector<Module*>& molecule) {
		return insertGiven(after, captureScene(molecule));
	}
	/** And PASTE's: `s` as a new scene after scene `after`, shown, the scenes
	after it renumbered on; with none, scene one. The molecule is the UI's to
	recall (`handPick`). Where it went; with the RIG full (`MAX_SCENES`),
	nothing set in and minus one. */
	int insertGiven(int after, Scene s) {
		lock.lock();
		const int n = (int)scenes.size();
		if (n >= MAX_SCENES) {
			lock.unlock();
			return -1;
		}
		const int at = n == 0 ? 0 : std::min(std::max(after + 1, 0), n);
		scenes.insert(scenes.begin() + at, std::move(s));
		changed(at, false);
		lock.unlock();
		return at;
	}
	/** Scene `at` as it is stored, for COPY; empty where there is none. */
	Scene sceneAt(int at) {
		lock.lock();
		Scene s = at >= 0 && at < (int)scenes.size() ? scenes[at] : Scene();
		lock.unlock();
		return s;
	}
	/** Scene `at` gone, the ones after it renumbered, as a list is; the
	scene showing stays the one it was, or the one that slid into its place,
	or the new last where the last went. */
	void deleteScene(int at) {
		const int shown = shownForUi();
		lock.lock();
		if (at >= 0 && at < (int)scenes.size()) {
			scenes.erase(scenes.begin() + at);
			const int show = shown > at ? shown - 1 : shown;
			changed(std::min(show, lastOf((int)scenes.size())), false);
		}
		lock.unlock();
	}
	/** The scene showing set to `at`, with nothing recalled: a scene the hand
	has just recalled itself (`handPick`), or one an undo puts back. */
	void show(int at) {
		lock.lock();
		changed(std::min(std::max(at, 0), lastOf((int)scenes.size())), false);
		lock.unlock();
	}
	/** A value a hand's recall moved, for its step of undo: a param, or past
	the module's params one of its scene settings. */
	struct Moved {
		Module* module;
		int param;
		float from, to;
	};
	/** The scene showing as the UI knows it: what the UI last asked for,
	until the engine has taken it, and what the engine shows after. So two
	clicks inside one frame each step from the last. */
	int shownForUi() {
		lock.lock();
		const bool taken = adoptedNow.load(std::memory_order_acquire) == generation.load(std::memory_order_relaxed);
		const int at = taken ? relaxed(currentNow) : wantCurrent;
		lock.unlock();
		return at;
	}
	/** Where the hand's arrow goes from the scene showing: `by` along, round
	the scenes there are, the last's next scene one (`stepRound`); with none,
	nought, which recalls nothing. */
	int handTarget(int by) {
		return std::max(stepRound(shownForUi(), by, sceneCount()), 0);
	}
	/** What a scene action is, for the one rule of which of them restart the
	molecule (`restartsOn`). */
	enum SceneAct {
		ACT_PICK,    // a recall by the hand: the arrows, the LCD's stroke, the name's menu
		ACT_STEP,    // a recall by a trigger at NEXT or PREV
		ACT_JACK,    // a recall through the SCENE jack, its morph
		ACT_OPEN,    // a rig opened from its file
		ACT_REPLACE, // the molecule stored over the scene showing
		ACT_INSERT,  // the molecule stored as a new scene
		ACT_PASTE,   // the scene copied set in as a new scene, or over the one showing, and recalled
		ACT_DELETE,  // the scene showing taken out, the next recalled
	};
	/** Which scene actions restart the molecule, every SEQ and CLK in it, as
	the bracket's RESET does, and which leave the clocks running: said here
	and nowhere else. A recall by the hand, NEXT or PREV, and a rig opened
	restart, so a new scene starts on its step one. The SCENE jack never
	does: it morphs the settings and the clocks run on, a scene's steps
	changed under a running pattern. With SCENE patched the jack is in charge
	and the buttons recall nothing (`handPick`), so PASTE and DELETE put
	their scene in or take it out and restart nothing. REPLACE and INSERT
	store the molecule as it stands and recall nothing, so they never
	restart. */
	static bool restartsOn(SceneAct act, bool patched) {
		switch (act) {
			case ACT_STEP:
			case ACT_OPEN: return true;
			case ACT_PICK:
			case ACT_PASTE:
			case ACT_DELETE: return !patched;
			case ACT_JACK:
			case ACT_REPLACE:
			case ACT_INSERT: return false;
		}
		return false;
	}
	/** The molecule reset, as the bracket's RESET resets it, from the UI thread as
	the bracket asks it: a scene changed by the hand. A bypassed module is no
	member and is skipped. */
	static void resetFromUi(const std::vector<Module*>& molecule) {
		for (Module* m : molecule)
			if (Member* rm = dynamic_cast<Member*>(m))
				if (!m->isBypassed())
					rm->resetAsked.store(true, std::memory_order_relaxed);
	}
	/** The hand's recall, on the UI thread: scene `at` set on the molecule here
	and now, every param it moved written into `moved` for one step of undo,
	and the scene shown without the engine recalling it again. A hand's
	gesture, so it is the UI's to make (the engine's own recalls, by SCENE,
	PREV and NEXT, are the patch performing and no step of undo). Nothing
	while SCENE is patched: the jack is in charge. A scene changed resets the
	molecule unless `reset` says the caller will (a stroke on the screen resets
	once, at its end). */
	bool handPick(int at, const std::vector<Module*>& molecule, std::vector<Moved>* moved, bool reset = true) {
		if (relaxed(patchedNow))
			return false;
		const int was = shownForUi();
		lock.lock();
		if (at >= 0 && at < (int)scenes.size()) {
			const Scene& sc = scenes[at];
			for (Module* m : molecule) {
				const Scene::const_iterator it = sc.find(m->id);
				if (it == sc.end())
					continue;
				bool any = false;
				Member* rm = sceneMember(m);
				const size_t n = sceneValues(m, rm);
				for (size_t i = 0; i < n && i < it->second.size(); i++) {
					const float from = sceneValue(m, rm, i), to = it->second[i];
					if (from == to)
						continue;
					handSetSceneValue(m, rm, i, to);
					if (moved)
						moved->push_back({m, (int)i, from, to});
					any = true;
				}
				if (any && rm)
					rm->land(); // a recall is heard at once, never glided
			}
			// A scene changed by the hand resets the molecule, as the bracket's
			// RESET does, from the UI thread as the bracket asks it; the same
			// scene picked again is no change.
			if (at != was && reset && restartsOn(ACT_PICK, false))
				resetFromUi(molecule);
		}
		changed(std::min(std::max(at, 0), lastOf((int)scenes.size())), false);
		lock.unlock();
		return true;
	}
	/** Whether the molecule differs from the scene showing: a param or a
	scene setting off its stored value, or a module the scene does not hold. False while there are
	no scenes, there being nothing to differ from. A value a cable holds is
	not counted: a recall cannot move it, and a cable moving would keep the
	button breathing. */
	bool differs(const std::vector<Module*>& molecule) {
		const int at = relaxed(currentNow);
		bool d = false;
		lock.lock();
		if (at >= 0 && at < (int)scenes.size()) {
			const Scene& sc = scenes[at];
			for (Module* m : molecule) {
				const Scene::const_iterator it = sc.find(m->id);
				if (it == sc.end()) {
					d = true;
					break;
				}
				Member* rm = sceneMember(m);
				const size_t n = sceneValues(m, rm);
				for (size_t i = 0; i < n && i < it->second.size() && !d; i++)
					d = sceneFree(m, rm, i) && sceneValue(m, rm, i) != it->second[i];
				if (d)
					break;
			}
		}
		lock.unlock();
		return d;
	}
	int sceneCount() {
		lock.lock();
		const int n = (int)scenes.size();
		lock.unlock();
		return n;
	}

	/** What the RIG holds, as JSON: the scenes, pruned to the modules the
	rack still has where the UI has said which those are, the scene showing,
	the rig file and the rig's note. */
	json_t* stateJson() {
		json_t* rootJ = json_object();
		lock.lock();
		json_object_set_new(rootJ, "scenes", scenesToJson(scenes, liveKnown ? &live : nullptr));
		lock.unlock();
		json_object_set_new(rootJ, "scene", json_integer(shownForUi()));
		json_object_set_new(rootJ, "file", json_string(file.c_str()));
		json_object_set_new(rootJ, "note", json_stringn(note.c_str(), note.size()));
		if (cleanKnown) {
			json_t* cJ = json_object();
			json_t* msJ = json_array();
			for (const std::string& m : cleanModels)
				json_array_append_new(msJ, json_string(m.c_str()));
			json_object_set_new(cJ, "models", msJ);
			json_object_set_new(cJ, "scenes", scenesToJson(cleanScenes));
			json_object_set_new(cJ, "note", json_stringn(cleanNote.c_str(), cleanNote.size()));
			json_t* sJ = json_array();
			for (size_t k = 0; k < cleanParams.size(); k++) {
				json_t* oneJ = json_object();
				json_t* pJ = json_array();
				for (float v : cleanParams[k])
					json_array_append_new(pJ, sceneNumberJson(v));
				json_object_set_new(oneJ, "params", pJ);
				json_object_set_new(oneJ, "data", json_string(k < cleanData.size() ? cleanData[k].c_str() : ""));
				json_array_append_new(sJ, oneJ);
			}
			json_object_set_new(cJ, "settings", sJ);
			json_object_set_new(rootJ, "clean", cJ);
		}
		return rootJ;
	}
	/** The file as SAVE asks it, from now: `models`, `held` and `heldNote`
	are what it holds. */
	void markClean(const std::vector<std::string>& models, const std::vector<Scene>& held, const std::string& heldNote,
	               const std::vector<std::vector<float>>& params = {}, const std::vector<std::string>& data = {}) {
		cleanKnown = true;
		cleanModels = models;
		cleanScenes = held;
		cleanNote = heldNote;
		cleanParams = params;
		cleanData = data;
	}
	/** What `m` saves beside its params, as SAVE compares it: its JSON,
	compact, its keys in order, its reals as a rig file writes them; none
	where it is no setting (`Member::dataIsSetting`, a Crv's end as drawn). */
	static std::string settingsData(Module* m) {
		if (Member* rm = dynamic_cast<Member*>(m))
			if (!rm->dataIsSetting())
				return std::string();
		json_t* dJ = m->dataToJson();
		if (!dJ)
			return std::string();
		char* text = json_dumps(dJ, JSON_COMPACT | JSON_SORT_KEYS | JSON_REAL_PRECISION(9));
		json_decref(dJ);
		std::string out = text ? text : "";
		free(text);
		return out;
	}
	/** The scenes stored, each cut to the modules `ids` names. */
	std::vector<Scene> scenesOn(const std::set<int64_t>& ids) {
		std::vector<Scene> out;
		lock.lock();
		for (const Scene& sc : scenes) {
			Scene one;
			for (const auto& kv : sc)
				if (ids.count(kv.first))
					one.insert(kv);
			out.push_back(one);
		}
		lock.unlock();
		return out;
	}
	/** And back: a whole new bank, which the SCENE jack, if patched, applies
	again; `restart`, for a rig loaded, resets the molecule once it stands. */
	void setState(json_t* rootJ, const std::map<int64_t, int64_t>* remap = nullptr, bool restart = false) {
		std::vector<Scene> got = scenesFromJson(json_object_get(rootJ, "scenes"), remap);
		const int show = (int)json_integer_value(json_object_get(rootJ, "scene"));
		lock.lock();
		scenes = std::move(got);
		changed(std::min(std::max(show, 0), lastOf((int)scenes.size())), true, restart);
		lock.unlock();
		const std::string was = file;
		if (json_t* fJ = json_object_get(rootJ, "file"))
			file = json_string_value(fJ) ? json_string_value(fJ) : "";
		note = noteOf(rootJ);
		// The same file held still holds what it held: a step of undo taken
		// before a save would otherwise hand back what the file held before
		// it was written.
		if (cleanKnown && file == was)
			return;
		json_t* cJ = json_object_get(rootJ, "clean");
		cleanKnown = json_is_object(cJ);
		cleanModels.clear();
		cleanScenes.clear();
		cleanNote.clear();
		cleanParams.clear();
		cleanData.clear();
		if (cleanKnown) {
			cleanNote = noteOf(cJ);
			size_t i;
			json_t* mJ;
			json_array_foreach(json_object_get(cJ, "settings"), i, mJ) {
				std::vector<float> ps;
				size_t q;
				json_t* vJ;
				json_array_foreach(json_object_get(mJ, "params"), q, vJ) ps.push_back(sceneNumberOf(vJ));
				cleanParams.push_back(ps);
				const char* d = json_string_value(json_object_get(mJ, "data"));
				cleanData.push_back(d ? d : "");
			}
			json_array_foreach(json_object_get(cJ, "models"), i, mJ) cleanModels.push_back(json_string_value(mJ) ? json_string_value(mJ) : "");
			cleanScenes = scenesFromJson(json_object_get(cJ, "scenes"), remap);
		}
	}
	/** The note `j` holds, a rig file's or the RIG's state, or none. */
	static std::string noteOf(json_t* j) {
		json_t* nJ = json_object_get(j, "note");
		return json_is_string(nJ) ? std::string(json_string_value(nJ), json_string_length(nJ)) : std::string();
	}
	json_t* dataToJson() override { return stateJson(); }
	/** State is taken only while the RIG is being built, before the engine
	has it: a patch loading, a rig file building one, a paste, a duplicate, an
	undo of a delete, all of which hand a module its JSON before
	`Engine::addModule`, as Rack's own loaders do. Once it stands, JSON handed
	to it (a VCV preset loaded or pasted onto it, another plugin calling
	fromJson) is ignored, but for Initialize's own undo and redo (`onReset`), so nothing but the RIG's own hand and
	its own undo ever replaces what it holds, the RIG's state being
	defended against presets. The RIG's undo goes through `setState`, never here. */
	bool built = false;
	void onAdd(const AddEvent& e) override { built = true; }
	void onRemove(const RemoveEvent& e) override { built = false; }
	void dataFromJson(json_t* rootJ) override {
		if (built && !wasReset(rootJ)) {
			INFO("RIG %lld: state handed to a RIG already standing is ignored", (long long)id);
			return;
		}
		setState(rootJ);
	}
	/** Initialize makes the RIG one fresh from the browser: no scenes, no note, no rig file. Its undo
	and its redo are Rack's own, the RIG's whole state handed back through
	`dataFromJson`, so the state as it stood before and after are kept here
	and taken from a RIG that stands; anything else handed to it is still
	ignored, and anything equal to them would leave it as either did. */
	std::vector<json_t*> resetStates;
	bool wasReset(json_t* rootJ) {
		for (json_t* j : resetStates)
			if (json_equal(j, rootJ))
				return true;
		return false;
	}
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		wipe();
	}
	/** All Initialize does but the buttons' params, which Rack's own reset
	puts back and a headless test, with no Rack, cannot. */
	void wipe() {
		resetStates.push_back(stateJson());
		lock.lock();
		scenes.clear();
		changed(0, true);
		lock.unlock();
		file.clear();
		note.clear();
		cleanKnown = false;
		cleanModels.clear();
		cleanScenes.clear();
		cleanNote.clear();
		cleanParams.clear();
		cleanData.clear();
		resetStates.push_back(stateJson());
	}
	~RigCore() {
		for (json_t* j : resetStates)
			json_decref(j);
	}
};

} // namespace rig
