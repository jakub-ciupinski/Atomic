// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Signal.hpp"
#include "Tree.hpp"
#include "Manifest.hpp"
#include "Writer.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

/** The rig module: the base every member of the family derives from, and
what the host does with it: the sweep and the tick, the watch tables, the
topology and wiring epochs, the gate, the faces a node plays, the colour
ranking and the downward writes. `Pulse`, `JackWatch` and `TrigCounter` are
the small machines a module holds on its jacks. */

namespace rig {

struct Member;

/** Whether a module is one of the family: every rig module derives from
Member, so membership is the type and nothing has to be listed. Defined
under Member. */
inline bool isFamily(Module* m);

/** A trigger on a jack: `TRIG_VOLTS` for `pulseFrames` (Pulse.hpp), ended by
everySample. Every pulse the family puts out is this one, so a Trg's button,
a Seq's TRIG and its trigger over a block of Trgs all have the same shape. */
struct Pulse {
	int64_t until = -1;
	void fire(Output& out, const Module::ProcessArgs& args) { fire(out, args, pulseFrames(args.sampleRate)); }
	/** And for `frames` samples, shorter where the next may come sooner
	(`triggerFrames`). */
	void fire(Output& out, const Module::ProcessArgs& args, int64_t frames) {
		out.setVoltage(TRIG_VOLTS);
		until = args.frame + frames;
	}
	/** Ends the pulse when its time is up. Returns true on the sample it ends. */
	bool tick(Output& out, const Module::ProcessArgs& args) {
		if (until < 0 || args.frame < until)
			return false;
		out.setVoltage(0.f);
		until = -1;
		return true;
	}
};

/** A jack a module keeps a handle on: whether a cable is in it and what it
carries, asked of the port itself rather than copied out of it. It keeps no
copy, because the watch table is what notices the jack has moved, so there is
nothing to refresh and nothing to fall behind: a module and a widget both read
the one number the engine wrote. Bound once, where the jack is configured, so
a module names its jack in one place and not at every use. */
struct JackWatch {
	Input* port = nullptr;
	// A value fed from the rig where no cable is: the Poly to my left with a
	// cable in IN sets me from my channel of it, as though it were in my own
	// jack. A cable in my own jack wins, so one channel of a bundle can be
	// replaced by plugging into that member.
	bool fed = false;
	float fedV = 0.f;
	void bind(Input& in) { port = &in; }
	bool plugged() const { return port && port->isConnected(); }
	bool live() const { return fed || plugged(); }
	float get() const { return plugged() ? port->getVoltage() : fed ? fedV : 0.f; }
	/** How many voices arrive: a cable's channels, one fed or empty. */
	int voices() const { return plugged() ? std::max(port->getChannels(), 1) : 1; }
	/** Voice `c`, a mono cable's one voice for every `c`, as Rack's own
	polyphonic modules read a mono cable against a poly one, and nought past
	the last voice of a polyphonic cable, where Rack keeps whatever a wider
	cable last left. */
	float getVoice(int c) const {
		if (!plugged())
			return fed ? fedV : 0.f;
		const int n = port->getChannels();
		return n <= 1 ? port->getVoltage(0) : c < n ? port->getVoltage(c) : 0.f;
	}
	/** Returns whether anything changed, so the owner can ask for a wake. */
	bool feed(float v) {
		const bool changed = !fed || fedV != v;
		fed = true;
		fedV = v;
		return changed;
	}
	bool unfeed() {
		const bool changed = fed;
		fed = false;
		return changed;
	}
};

/** The topology epoch, one counter for the whole plugin, bumped whenever a
module is added, removed, moved, bypassed or loaded. It is a doorbell, not a
verdict: when it moves every host walks its run again, and reparses only if
the run it finds is not the one it parsed, or a member owes a parse
(`Member::oweParse`), or a member's setting says so (`parseStale`). So a
module moved in one rig reparses that rig and no other; everything else is a
value change. */
uint32_t topology();
void bumpTopology();
/** The wiring epoch, the same idea one step down: bumped whenever a cable
arrives at or leaves any module of the plugin. The membership does not move
for a cable, but the watch table does, so the two have their own counters and
a patch being played bumps neither. */
uint32_t wiring();
void bumpWiring();

/** One number the host watches: where it lives, what it read there last, and
whose it is.

Detection is the same question for every module (did any number change),
asked of every jack every sample, so it is data rather than behaviour. At
bond the host walks its members and writes down a pointer to every channel of
every connected input and to every param, with the value standing there. An
unconnected jack is not in the table at all, which is why no module in the
rig ever asks whether its jack is connected: a jack with no cable costs
nothing because there is nothing to sweep.

The jacks are swept every sample in one flat loop with no virtual calls and
no branch beyond the comparison. The params are swept on the control period,
since only the user interface writes them, with one exception, which is the
difference between a setting and an edge. A knob is a setting: reading it a
thirty-second of a millisecond late costs nothing anybody can hear. A
momentary button is an edge, and an edge missed is a trigger that never
happened, so buttons keep a table of their own and are swept with the jacks.
Rack itself is what tells the two apart: `configButton` turns randomizing off
and nothing else does, so a module says which it meant when it configures the
param and never says it again. There are a handful of them in the whole
plugin, and the alternative was a Trg asking for the sample tick for the rest
of its life.

A channel count is a byte and not a float, so it keeps its own small table
beside them and is swept with the params: a cable arriving or leaving bumps
the wiring epoch and rebuilds everything, and a count moving under a cable
that stayed is rare enough to find within thirty-two samples. */
struct Watch {
	const float* at;
	float last;
	Member* owner;
};
struct ChanWatch {
	const uint8_t* at;
	uint8_t last;
	Member* owner;
};

/** What only the host of a rig holds: the rig it computes, re-derived when
the topology epoch moves, the same members as the tree sees them, the watch
table and the tick list. A module makes one on the first sample it hosts and
drops it the moment it stops, so no other member carries any of it, and its
sample is `sample` (Engine.cpp). */
struct Host {
	bool ran = false;       // computed at least once
	bool fineStale = true;  // a node's `fine` has moved since the fine group was worked out (`tree::fineMoved`)
	std::vector<Member*> members;
	std::vector<tree::Node*> nodes;
	std::vector<Member*> tickers;  // the members that asked for the tick
	std::vector<Member*> restarts; // the nodes a RESET restarts, the only ones with a mark on the roll
	std::vector<WalkLine> lines;      // and their walks, compared at most every `LINES_MS` (`inLine`)
	std::vector<WalkLine> linesWas;   // the walks the last comparison was made of, and its answer:
	bool linesKnown = false, linesOff = false; // a walk moves only at a landing, so most wakes ask again
	bool linesOwed = false;  // a wake since the last comparison
	int64_t linesDue = 0;    // the frame from which the next may be made
	int linesEvery = 0;      // frames between two, at the sample rate it was worked out at
	float linesRate = 0.f;
	int bracketOff = -1, bracketResettable = -1; // what the members were last told, minus one for not yet
	std::vector<Watch> jacks;          // every channel of every connected input
	std::vector<Watch> knobs;          // every param that is a setting
	std::vector<Watch> buttons;        // and every one that is a momentary button
	std::vector<ChanWatch> counts;     // every empty input's channel count, swept with the params
	std::vector<ChanWatch> liveCounts; // and every cabled one's, swept every sample
	std::vector<Member*> stirs;     // the members the sweep marked this sample
	uint32_t knownWiring = 0;          // the wiring the table was written at
	std::vector<Member*> walked;    // the run as `regrouped` last walked it, kept for its capacity
	tree::FineScratch fine;            // what `fineGroup` works in, sized at bond
	uint32_t parses = 0;               // times `bond` has run, which a test counts
	/** Whether the rig must be parsed afresh, asked when the topology epoch
	moves: the run walked again is not `members`, or one of it owes a parse
	or finds its setting moved since the last. Compares pointers and reads
	only modules standing now, so a member deleted since is never touched. */
	bool regrouped(Member* self);
	/** The rig parsed afresh, its leftmost member `self`: the members, the
	tree, and what the bracket asks of it. */
	void bond(Member* self);
	/** The watch table, written at bond and whenever a cable moves, and the
	tick list, asked afresh after every wake. */
	void buildWatch();
	void retick();
	/** One sample of the rig: the sweep, and the wake if anything moved. */
	void sample(const Module::ProcessArgs& args, bool slow);
	/** The bracket's SYNC: whether the walks stand off the line, told the
	members when it moves. */
	void bracketLines(const Module::ProcessArgs& args);
};

/** The grey a gatherer's row wears for a Poly's channel replaced on the way,
a CV taken over by hand or by its own cable, packed as a cable's colour is
(`gatheredRgb`): mid grey, a colour no cable is given by default. */
constexpr uint32_t REPLACED_RGB = 0x80808000u;

/** The base of every module in the rig. A rig is computed once per sample by
its leftmost non-bypassed member, the host; every other member's process()
returns at once and owns no rig state. The host computes only when something
changed: a control moved (looked at every few dozen samples), a tick or
trigger fell, a member asked for a wake, or the rig was rearranged.
Otherwise every jack holds its last value. */
struct Member : Module, JackGlow, tree::Node {
	std::atomic<bool> poke{true}; // something outside asks for a wake
	/** The bracket's RESET: whether anything in my rig is off the line a Sync
	would put it on, as a Trg's roll shows an empty ring, written by the host
	into every member after a wake; and a press of the word on the bracket,
	written by the UI into the member under it and taken by the host on its
	next sample. And whether my rig holds anything a Sync starts again, a Seq
	or a Clock, so the word is there to press whenever pressing it would do
	something. */
	std::atomic<bool> rigOffNow{false};
	std::atomic<bool> rigResettableNow{false};
	std::atomic<bool> resetAsked{false};
	/** The engine frame my rig last started or was restarted on, its first
	sample computed or a reset taken (the bracket's RESET, a RIG's change of
	scene or a rig loaded), or `NEVER_STARTED`. Written by my rig's host,
	read by the RIG to my left, which may run on another thread, two rigs'
	hosts running at once, so an atomic; a RIG asks it only on an edge at NEXT or PREV, to
	tell the pulse a start gives from a pulse of the patch's own
	(`RigCore::echoOfStart`). */
	static constexpr int64_t NEVER_STARTED = INT64_MIN;
	std::atomic<int64_t> startedAt{NEVER_STARTED};

	// ---- Writes the engine wants made to params on the user's behalf.
	// The engine never writes a param except where the patch itself is
	// performing on it: a recording (a Seq's REC into a fader or a ring) or a
	// transport (a reset from a jack moving a Seq's step). What it wants
	// written on a hand's behalf, a Qnt's side started afresh or its keyboard
	// opened when its octaves moved without a stroke, is posted here from the
	// audio thread and made on the UI thread by the widget's step
	// (`makeWrites`, Bracket.hpp), one undo action per drain, and by a headless
	// test host in its own steps. Written by the engine itself,
	// such a change could not be undone, and a stroke undone across nought
	// had its keys wiped again on the next wake. A write names the module
	// whose param it is, which is this one or one to its left, so a gesture
	// over several modules is one drain and one undo. It names it by the
	// engine's id, never a pointer: a module deleted between the post and
	// the drain is then a write skipped, not one made into freed memory.
	struct ParamWrite {
		int64_t module;
		int param;
		float value;
	};
	// The ring lives on the heap, made when the module is: two thousand
	// writes inline made every module thirty kilobytes wider and the rig a
	// third slower for the cache it spilled.
	static const int WRITES = 2048;
	std::vector<ParamWrite> writes = std::vector<ParamWrite>(WRITES);
	std::atomic<uint32_t> writeHead{0}, writeTail{0};
	std::atomic<const char*> writeName{"change"};
	/** Whether a write since the last drain was made on a hand's behalf
	rather than by the patch performing (`postWrite`'s `performed`). */
	std::atomic<bool> writeHand{false};
	/** The writes `postWrite` found no room for, counted so a decision lost
	in a full ring leaves a trace a test and a log can read. Stored by the
	engine thread alone, as `writeHead` is. */
	std::atomic<uint32_t> writesDropped{0};
	/** Audio thread: one write, named for the undo it will be part of.
	`performed` says the patch made it rather than a hand, as a REC take does:
	a Poly's feed losing a channel while its cable stays, which lets the CV it
	fed go. Such writes join one undo step for as long as nothing else is
	pushed (`makeWrites`), so a feed coming and going as the patch plays never
	buries the user's own strokes. */
	void postWrite(Module* target, int param, float value, const char* name, bool performed = false) {
		const uint32_t h = writeHead.load(std::memory_order_relaxed);
		const uint32_t t = writeTail.load(std::memory_order_acquire);
		if (h - t >= (uint32_t)WRITES) {
			// A drain a frame away and two thousand writes waiting: the hand
			// can ask again, and the loss is counted.
			writesDropped.store(writesDropped.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
			return;
		}
		writes[h % WRITES] = ParamWrite{target->id, param, value};
		writeName.store(name, std::memory_order_relaxed);
		if (!performed)
			writeHand.store(true, std::memory_order_relaxed);
		writeHead.store(h + 1, std::memory_order_release);
	}
	void postWrite(int param, float value, const char* name, bool performed = false) {
		postWrite(this, param, value, name, performed);
	}
	/** Audio thread, before the writes it names: they follow from the hand
	turning param `param` of mine, so they join the undo step that turn
	made while it is the last step and under a second old (`makeWrites`),
	and one undo takes back the turn and what it caused: a Seq turning
	poly on, which conforms its Trgs (`Seq::conform`). */
	std::atomic<int> writesFollow{-1};
	void postFollowing(int param) { writesFollow.store(param, std::memory_order_relaxed); }
	/** Whether any write waits: two plain loads, which is what a headless test
	asks every module every sample before it drains. */
	bool hasWrites() const {
		return writeHead.load(std::memory_order_relaxed) != writeTail.load(std::memory_order_relaxed);
	}
	/** UI thread, or a headless test: the writes since the last drain, in order, and
	their name; null with nothing waiting. `performed`, where asked, says
	whether every one of them was the patch performing. */
	const char* takeWrites(std::vector<ParamWrite>& out, bool* performed = nullptr) {
		out.clear();
		uint32_t t = writeTail.load(std::memory_order_relaxed);
		const uint32_t h = writeHead.load(std::memory_order_acquire);
		if (t == h)
			return nullptr;
		for (; t != h; t++)
			out.push_back(writes[t % WRITES]);
		writeTail.store(t, std::memory_order_release);
		const bool hand = writeHand.exchange(false, std::memory_order_relaxed);
		if (performed)
			*performed = !hand;
		return writeName.load(std::memory_order_relaxed);
	}
	bool subscribed = false;      // this module is on its host's tick list
	bool stirred = false;         // the sweep has already marked it this sample
	/** A setting of mine may have moved since I last looked: a param the
	sweep found, a poke, a RESET, a fresh table. Never a jack. A module whose
	`prepare` is a pure function of its settings keeps what it built until
	this says otherwise, and clears it (the Qnt's scale). */
	bool settingsMoved = true;
	uint32_t knownEpoch = 0; // the topology this module last looked at
	/** Something of mine the parse reads has changed in a way no param or
	cable says (added, loaded, bypassed, a gatherer's IN): my rig reparses on
	the next look, and no other rig does. Set by whichever thread made the
	change, before it bumps the epoch; cleared by my host's `bond`. A fresh
	module owes one. */
	std::atomic<bool> parseOwed{true};
	void oweParse() {
		parseOwed.store(true, std::memory_order_release);
		bumpTopology();
	}
	/** The rig I compute, while I am its host, and nothing while I am not. */
	std::unique_ptr<Host> host;

	static const int CONTROL_PERIOD = 32; // samples between looks at the controls

	/** Does this module have work of its own to do on every sample, whatever
	else the rig is doing: a clock's ramp, a delay line, a pulse ending, a
	light fading. It is asked afresh after every wake and on every control
	period, so a clock that is stopped leaves the list and an idle patch
	costs an empty loop. It is NOT a claim about speed: every module behaves
	as though its inputs could move at audio rate, and this asks only whether
	there is state inside that moves without them. */
	virtual bool wantsTick() { return false; }
	/** That work. Called on every sample while `wantsTick` holds, and also
	on any sample where a jack, a param or a poke of this module's own
	moved, so a module that is asleep never misses an edge on a jack that
	has only just arrived. Return true to wake the rig. */
	virtual bool everySample(const ProcessArgs& args) { return false; }
	/** Said by `everySample` beside a true: the wake is asked only so a
	reader that plays its own ramp has it every sample, a Seq over a block
	of CRVs or under a phase. A wake asked for nothing else runs only those
	readers and what shares a member with them (`tree::fineGroup`). */
	bool fineTick = false;

	/** Whether param `i` is worth the table's sweep: every setting is, by
	default. A module with hundreds of params of which only some are in play,
	a Qnt's keys outside the octaves it shows, says no to the rest and bumps
	the wiring epoch when the set changes, so the table is built again with
	the new set; a param left out is read when the set changes, never watched
	between. Without it a Qnt costs the sweep of two hundred and forty keys
	every control period. */
	virtual bool watches(int param) { return true; }
	/** Ask for a wake from outside the audio thread: a widget that has
	written an atomic the rig reads. */
	void wake() { poke.store(true, std::memory_order_relaxed); }
	/** A RIG's recall, by its jacks or by the hand: the params it has just
	set land at once, never glided (`Glide`), since a scene change is no stroke and resets the molecule on
	the sample it is heard. Called after the params are set, from whichever
	thread recalls; taken once by my own `everySample` (`landing`). */
	std::atomic<bool> landAsked{false};
	void land() {
		landAsked.store(true, std::memory_order_release);
		wake();
	}
	/** Whether a recall asked my glides to stand on their settings, taken
	once, on the engine's thread for my rig. */
	bool landing() { return relaxed(landAsked) && landAsked.exchange(false, std::memory_order_acquire); }

	// Who stands where is the walk's to find, so a move only rings; what a
	// module holds is not, so an add, a load or a bypass owes a parse.
	void onAdd(const AddEvent& e) override { oweParse(); }
	void onRemove(const RemoveEvent& e) override { bumpTopology(); }
	void onExpanderChange(const ExpanderChangeEvent& e) override { bumpTopology(); }
	void onBypass(const BypassEvent& e) override { oweParse(); }
	void onUnBypass(const UnBypassEvent& e) override { oweParse(); }
	/** Bypassed, Rack steps me through this and not `process`, and the host
	leaves me out of its rig, so nothing else ticks my jacks' light: it would
	hold where the last wake left it over a jack Rack has zeroed or routed.
	Ticked here it follows what the jack carries. Only the thread stepping a
	bypassed module reaches it, the host having let go of me. */
	void processBypass(const ProcessArgs& args) override {
		Module::processBypass(args);
		tickJacks(this, args);
	}
	void dataFromJson(json_t* rootJ) override { oweParse(); }
	/** Whether what I save beside my params is a setting, which a RIG with
	no scenes compares against its rig file: a menu's choice is; a Crv's end as it was drawn, which
	follows its neighbour, is not. */
	virtual bool dataIsSetting() { return true; }
	/** The settings of my menu that belong in a RIG's scene, being part of
	what the patch sounds like, as BLEP and Oversampling are: a scene holds my params and after them these, in this order (`rig::Scene`).
	Each is a float to the scene, and either whole, taken from the nearer
	scene in a morph as a snapped param is, or continuous, drawn between.
	`setSceneSetting` is called by a recall on the audio thread as well as the
	UI's, so it stores an atomic and nothing more. None by default: a look (a
	GAP's skin) or a drawing's memory (a Crv's end) is no sound. */
	virtual int sceneSettings() { return 0; }
	virtual float sceneSetting(int k) { return 0.f; }
	virtual void setSceneSetting(int k, float v) {}
	virtual bool sceneSettingWhole(int k) { return true; }
	/** Whether a cable holds param `param` now, so what plays and shows is
	not the param standing under it, with `shown` the value the param would
	have to be to show the same: a scene stores that, as the molecule plays
	at the storing, and does not count it as
	differing, since a recall cannot move a cable. None by default: only a
	CV's fader stands under a cable. The UI's, from what the last wake
	published. */
	virtual bool heldByCable(int param, float& shown) { return false; }
	// Rack's own reset is what puts every param back to its default, so it is
	// not skipped. A module with state of its own resets it and calls this.
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		poke.store(true);
	}
	// Randomize, the same way: what Rack's own does is every param's
	// `randomize`; ours is that, less what `rolled` keeps still.
	void onRandomize(const RandomizeEvent& e) override {
		for (size_t p = 0; p < paramQuantities.size(); p++) {
			engine::ParamQuantity* pq = paramQuantities[p];
			if (pq && pq->randomizeEnabled && rolled((int)p))
				pq->randomize();
		}
		poke.store(true);
	}
	/** Whether Randomize rolls a param: a module says no for the transport, a
	Clock's RUN and a Seq's mode. Asked here rather than by turning
	`randomizeEnabled` off, because that is how Rack says a param is a
	momentary button, and the rig sweeps those every sample (`Watch`). */
	virtual bool rolled(int param) { return true; }
	void onSampleRateChange(const SampleRateChangeEvent& e) override { poke.store(true); }
	// A cable is the one thing that changes what there is to watch, so it
	// moves the wiring epoch as well as asking for a wake.
	void onPortChange(const PortChangeEvent& e) override {
		poke.store(true);
		bumpWiring();
	}

	/** Serials come from one counter for the whole plugin, so no two modules
	in a patch ever number a restart or a press alike. */
	static uint32_t nextSerial() {
		static std::atomic<uint32_t> counter{1};
		return counter.fetch_add(1, std::memory_order_relaxed);
	}

	void process(const ProcessArgs& args) override; // Engine.cpp

	/** The tooltips, rewritten for what the module is now: asked by its
	widget every frame, on the UI thread, so a module that is three things by
	where it stands names only the one it is. */
	virtual void retipAll() {}

	/** The node's name in a printed parse: the model's, as the browser shows it. */
	std::string nodeName() override { return model ? model->name : std::string("?"); }

	// ---- The gate ----------------------------------------------------------
	// What this node handed rightward on its last wake, kept so this wake's
	// can be held against it. The rig has one vocabulary for what passes
	// between nodes and all of it is plain data, so the comparison is
	// mechanical and no module is asked to describe itself. What a module
	// owes is the other half of the bargain:
	// everything it hands rightward travels through one of these, or through
	// a `published` override that adds to this, as an Atn's jack does. A
	// value written sideways into a neighbour's field is invisible here and
	// the rig goes stale; there are none.
	Mapping goneWin;
	Count goneCount;
	int goneColor = -1;
	/** Whether `now` differs from what was handed on last time, which it
	then becomes. */
	template <typename T>
	static bool moved(T& gone, const T& now) {
		if (now == gone)
			return false;
		gone = now;
		return true;
	}
	bool published() override {
		// Compared where they stand, against empties that live as long as the
		// program, so no wake copies a window or a count.
		static const Mapping noWindow;
		static const Count noCount;
		bool any = false;
		// The lane, which is the window for a shaper and, for a CV or a CRV,
		// the window it reads, which the next block of its kind takes.
		const Mapping* w = laneOut();
		any |= moved(goneWin, w ? *w : noWindow);
		const Count* c = countOut();
		any |= moved(goneCount, c ? *c : noCount);
		any |= moved(goneColor, readerColor());
		return any;
	}

	// ---- The faces this node plays, null by default, asked for at use and
	// never cached. A node that forwards, a Seq, answers with the
	// member under its playhead.
	virtual const Mapping* windowOut() { return nullptr; }
	/** The window a node hands the next block of its lane: a shaper's window,
	and a CV's or a CRV's own, the window it reads, which the next block of
	its kind takes when no Qnt (for a CV) or no Atn (for a CRV) stands in its
	own gap. */
	virtual const Mapping* laneOut() { return windowOut(); }
	/** The window my own jack's signal was played in, for an OUT Atn reading
	it to know whether it carries notes and of which pattern: a CV's window,
	which is its lane, by default; a Seq's step's window where it sounds notes
	of one; nothing for a jack that is volts, a CRV's, a Poly's, a Mix's. */
	virtual const Mapping* playedOut() { return laneOut(); }
	/** A setting of mine that is a change of the parse has moved since the
	last one: an Atn's mode. The host asks every member on the control
	period, before it looks at the topology, so a rig and its twin reparse on
	the same sample whichever of them is stepped first. */
	virtual bool parseStale() { return false; }
	virtual QuantFace* quantFace() { return nullptr; }
	virtual const Count* countOut() { return nullptr; }
	/** Two statements a parent makes to a child that follow the parent's
	state, a wake late, where the manifest's
	`tells` are fixed by the parse. A reader holding me in its clock slot says
	whether it takes my count this wake or a cable of its own has it; a CV
	holding my window says how it plays it (`heldMode`). Each is written only
	when it changes, and wakes the child so its face follows. */
	virtual void countTaken(const void* by, bool on) {}
	/** How the CV whose window I shape plays it, which is what decides an
	Atn's mode and nothing else does: in volts, on a keyboard's notes, on a
	piano roll's, or on a volt divider's steps, its lines equal or the
	harmonic series (which the Atn's tooltips tell apart and nothing else
	does), the mode of the quantizer in
	the CV's own reach. Said by the CV down its window slot each wake and
	passed on through any shaper that only passes a window through (a Qnt,
	which neither knows nor asks what stands in front of it). The speaker
	gives its place, so where two CVs reach one Atn the nearer's word stands,
	as the parse's does (`Node::hear`). */
	enum HeldMode { HELD_UNSAID = -1, HELD_VOLTS = 0, HELD_KEYS, HELD_ROLL, HELD_DIVIDER, HELD_HARMONICS };
	virtual void heldMode(int place, int mode) {}
	/** And the same word's promise, made at the parse by every CV that will
	say it at its wakes, down the same road: what tells the child whether the
	word it had before the parse is still anybody's to say, so a reparse that
	moved nothing keeps it and sounds as though it had not happened. */
	virtual void heldModeOwed() {}
	/** The third: a holder of my window or my value says whether it uses it
	this wake, a Trg only as a gate to trigger, a Qnt only while what holds it
	uses its window in turn. */
	virtual void usedBy(const void* by, bool on) {}
	/** Saying it, from the holder's side: only when it changes, `last`
	being the holder's memory of what it said, -1 since the parse. */
	static void tellUse(Member* child, const void* by, bool on, int& last) {
		if (!child || (int)on == last)
			return;
		last = (int)on;
		child->usedBy(by, on);
	}
	/** A reader's colour, as it is drawn: see `rankColor`. */
	virtual int readerColor() { return -1; }

	/** The colours, as a count that never runs out: a Clock opens a group
	with the next colour after every one used to its left; the first Seq on
	that Clock wears the Clock's; each Seq after it on the same Clock takes
	the next colour after its own kind's last, so two playheads are two
	colours; and a Seq with no Clock opens a group of its own. A Poly and a
	Mix are colourless (`rankNone`). `colorRank`
	counts on for ever and is drawn modulo the palette (`playColor`). */
	int colorRank = -1, colorHigh = -1;
	Member* colorClock = nullptr; // the Clock whose group I am in
	Member* colorPrev = nullptr;  // the reader before me, whose rank I count on from
	/** What kind of reader I am, if I am one: a Seq walks, a Poly bundles, a
	Mix sums (`Reading`); READ_ANY for a node that reads no block. The
	manifest's `reads` column, so the Table shows it and no reader states it
	twice. */
	int readerKind() const {
		return manifestId() < 0 ? READ_ANY : manifest::spec((manifest::Id)manifestId()).reads;
	}
	/** The channels a Poly with a cable in IN offers the members to its
	right, and how many: minus one from anything that offers none. */
	virtual int feedOut(const float*& v) { return -1; }
	/** What a Poly's jack carries, its cable or its bundle, for a Seq
	arpeggiating it: minus one from anything else. */
	virtual int jackOut(const float*& v) { return -1; }
	/** The colour channel `c` of that came from: the module a Poly gathered
	it from, minus one where it is not known. A face, read by an OUT strip
	holding the Poly through its signal, a parent reading its child. */
	virtual int feedColor(int c) { return -1; }
	/** The colour of the cable channel `c` of my feed came in on, packed as
	a cable's colour is (`packRGB`), nought where none: what a CV following
	that channel wears. The engine's, read at the member's own wake. */
	virtual uint32_t feedRgb(int c) { return 0; }
	/** The colour a gatherer's row of me wears, packed the same way, nought
	for its own: a CV following a Poly's channel passes on the cable's, and
	one that has replaced it, by hand or by a cable of its own, the grey of
	a channel replaced (`REPLACED_RGB`). The engine's, read by the gatherer at its wake. */
	virtual uint32_t gatheredRgb() { return 0; }
	/** A Seq arpeggiating my jack stands on its channel `c`, in its colour
	`col`, with its line there (the step sounding) or its halo (the walk's
	own step under a PHASE turn), or both: a parent's statement, written into
	a Poly every wake as a playhead is into a member it walks, cleared at the
	top of the wake and published at its end. Nothing from anything else. */
	virtual void arpStands(int c, int col, bool line, bool halo) {}
	void rankColor(Member* prev, Member* clock) {
		int high = -1;
		if (prev)
			high = prev->colorHigh;
		if (clock)
			high = std::max(high, clock->colorHigh);
		int rank = -1;
		for (Member* p = prev; p; p = p->colorPrev)
			if (p->readerKind() == readerKind() && p->colorClock == clock) {
				rank = p->colorRank + 1;
				break;
			}
		if (rank < 0)
			rank = clock ? clock->colorRank : high + 1;
		colorRank = rank;
		colorClock = clock;
		colorPrev = prev;
		colorHigh = std::max(high, rank);
	}
	/** A colourless reader's: a Poly and a Mix take no colour and leave the
	count where the reader before them left it, so the readers to their right
	rank as though they were not there. */
	void rankNone(Member* prev) {
		colorRank = -1;
		colorHigh = prev ? prev->colorHigh : -1;
		colorClock = nullptr;
		colorPrev = prev;
	}
	/** A Clock's own: the next after everything to its left. */
	void rankClock(Member* before) {
		colorRank = (before ? before->colorHigh : -1) + 1;
		colorHigh = colorRank;
		colorClock = this;
		colorPrev = nullptr;
	}
	// What the bracket's RESET starts again: every node that plays T_RESTART
	// answers a press through `onRestart` (Engine.cpp), and through `lineOf`
	// gives its walk as the bracket's mark compares it, false for a node with
	// none or one left out as deliberate. The host asks it after every wake
	// and compares the walks of each count (`inLine`), for the bracket, and
	// nothing else reads it.
	virtual void onRestart() {}
	virtual bool lineOf(WalkLine& l) { return false; }

	/** How many channels of input `input` I read: all of them by default, one
	for a module that is mono by design, whose other channels the watch table
	then leaves alone, so a moving second channel never wakes it or resamples
	a jack it never reads; it is also the cost of a sixteen-voice cable in a
	mono jack. */
	virtual int channelsHeard(int input) const { return PORT_MAX_CHANNELS; }
	/** The shaper before this one in the window chain; a Seq answers with
	the member under its playhead. */
	virtual Member* windowChild() { return nullptr; }
	/** The node my own window is taken from: my window slot's, and nothing is
	looked past (an Atn replaces the Atn before it, and the window passes
	through a Qnt); a CV's, a CRV's and an Atn's own lane where that slot
	finds nothing. */
	virtual Member* windowSource() { return windowChild(); }
	/** A Poly's channels of me: my own output, every channel of it, in the
	volts the jack carries (a chord's notes, a bar of gates across Trgs), and
	how many; minus one from a module a Poly never bundles. */
	virtual int channelsOut(float* v) { return -1; }
	/** What I am to a gatherer that takes me: my row's `gives`, and none
	for a stub with no row. A Seq wears what it walks (`walked`). */
	virtual manifest::Face face() const {
		const int id = manifestId();
		return id < 0 ? manifest::F_NONE : manifest::spec((manifest::Id)id).gives;
	}

	// ---- What a forwarder stands in front of.
	// A node the manifest gives forwards to stands in front of a whole block
	// and wears what it walks, so anything travelling on a trait it forwards
	// has to reach every member of that block, not only the one under its
	// playhead. No downward write is left in the rig; `deliver`, the walk one
	// was made by, is gone, and the
	// tree still asserts the reach on every parse (`reachOf`).

	/** The nodes this one stands in front of, leftmost first: the block a
	forwarder walks. Overridden by the forwarder alone, and reached only for
	a trait the manifest says this row forwards. */
	virtual void behind(std::vector<Member*>& out) {}

	/** Does the manifest let this row wear traits it did not announce? */
	bool forwardsTrait(uint32_t trait) {
		return manifestId() >= 0 &&
		       (manifest::spec((manifest::Id)manifestId()).forwards & trait) != 0;
	}
	/** How wide this node's reach is for `trait`: what the tree asserts on
	every parse, so a forwarder that stands in front of a block it never
	passes a write to fails there and not in a patch. */
	int reachOf(uint32_t trait) override {
		if (!forwardsTrait(trait))
			return 0;
		std::vector<Member*>& more = behindScratch();
		more.clear();
		behind(more);
		return (int)more.size();
	}
	/** Where `reachOf` lists what a node stands in front of: the engine
	thread's own, read only inside the parse on that thread, and given room
	for the whole rig by the host as it bonds (`Host::bond`), so a parse
	allocates nothing for it. */
	static std::vector<Member*>& behindScratch() {
		thread_local std::vector<Member*> more;
		return more;
	}

	/** The slot the manifest calls `name`, or minus one when this row has
	no such slot: what lets two modules of one kind keep different slot
	lists without an index meaning one thing here and another there. */
	int slotNamed(const char* name) const {
		return manifestId() < 0 ? -1 : manifest::slotIndex((manifest::Id)manifestId(), name);
	}
	/** The module filling slot `i`, or null. The lower guard as well as the
	upper: `slotNamed` answers -1 for a row without that slot, and
	`at(slotNamed("window"))` on a row without one is a thing somebody will write. */
	Member* at(int i) {
		return i >= 0 && (int)slots.size() > i ? static_cast<Member*>(slots[i].node) : nullptr;
	}

	// ---- A reader's count: a Seq's.
	/** What the Clock in slot `clock` sends, else none. */
	const Count* countFrom(int clock) {
		Member* c = at(clock);
		const Count* cc = c ? c->countOut() : nullptr;
		return cc && cc->present ? cc : nullptr;
	}
};

inline bool isFamily(Module* m) {
	return dynamic_cast<Member*>(m) != nullptr;
}
/** Whether `m` stands in a rig: a member, not bypassed. A bypassed member
splits the rig hard where it stands, as a module of another plugin would:
the rig on either side is a parse of its own and knows nothing of the other,
unlike a GAP, which keeps one bracket. The
engine's rig, the hand's run (`runOf`) and a RIG's molecule all stop here. */
inline bool inRig(Module* m) {
	return isFamily(m) && !m->isBypassed();
}

/** The top-down rule, held by the compiler.
A module learns what holds it only from what it is told
(`tree::Node::told`), never by reading its parents: the in-edges, who holds
it and who claimed it, are private to the tree. This probe stands where any
module stands, deriving from Member, and asks whether it can name them;
it was written because a Trg once learned it was a keyboard's CLEAR by
walking its parents (`heldIn`), and a CV whether it was a wave's points by
asking its claimers what they were (`plainsBlock`). Making them reachable
again, public or protected, stops the build here. A node is never given a way
to look up its parents: what it wants to know is a statement the parent
should make. */
struct HolderProbe : Member {
	template <typename T>
	static auto parentsOf(T* t) -> decltype((void)t->parents, std::true_type());
	template <typename T>
	static std::false_type parentsOf(...);
	template <typename T>
	static auto claimersOf(T* t) -> decltype((void)t->claimers, std::true_type());
	template <typename T>
	static std::false_type claimersOf(...);
};
static_assert(!decltype(HolderProbe::parentsOf<HolderProbe>(nullptr))::value,
              "a module can read who holds it: the tree is top down");
static_assert(!decltype(HolderProbe::claimersOf<HolderProbe>(nullptr))::value,
              "a module can read who claimed it: the tree is top down");
/** A module of ours that bonds into a run as any member does, the bracket, the
ATOMIC bracket and the ground painted as one picture, but stands outside the
rig's grammar: it is no Member, so the engine's rig ends at its left and
starts afresh at its right, and the parse never reads it or tells it anything.
The RIG and the GAP are the two. Every bonder bonds to its right; whether it bonds to
its left is its own to say: the RIG does not, starting its molecule, and the
GAP does, the bond running on across it. Asked of the UI's runs (`runOf`) and
of the RIG's molecule, in the engine as in the UI, so on either thread. */
struct Bonder {
	virtual ~Bonder() {}
	virtual bool bondsLeft() { return false; }
};
inline bool isBonder(Module* m) {
	return dynamic_cast<Bonder*>(m) != nullptr;
}
/** Whether `m` bonds to what stands at its left: a member always does, and
a bonder as it says. */
inline bool bondsToLeft(Module* m) {
	Bonder* w = dynamic_cast<Bonder*>(m);
	return !w || w->bondsLeft();
}
/** Who stands in the drawn run: a member of a rig or a bonder, not bypassed.
The hand's run is the engine's (`inRig`), so it parts at a bonder where the
drawing does not: a fader's sweep never crosses a RIG into another rig. */
inline bool bondsIn(Module* m) {
	return (isFamily(m) || isBonder(m)) && !m->isBypassed();
}

/** Triggers at a jack: each an edge, which the Seq hands its writer as a
pulse and a Clock following a cable takes as a beat, and the period, the
frames between the last two edges, nought until there have been two (the clock
foundation). The count of edges starts afresh when the cable leaves. Watched
every sample; `watch` returns true on the sample a trigger lands, which wakes
the rig.

A trigger is the gate level, a volt, crossed upward, and nothing else: a
negative voltage is no trigger, as at any clock input in Rack (a fall counted
back rocked a Seq on a stock LFO's bipolar square).

Nothing else is read into the edges: every gap is the period, however long, so
a clock that pauses and resumes, or slows to half, is followed from the beat
after (one slow journey, never a coin toss on whether a gap was a pause); and
an early edge is the writer's to land at its journey's end.

Beside the whole frames, each edge is also placed inside its frame
(`after`) and the gap measured to that part of a frame (`fine`), for a Clock
in hertz following a cable, where the gap is the pitch: at 48 kHz a 1 kHz
square is 48 frames a cycle and one frame off is about 35 cents, so a period
in whole frames would make the pitch wander by that much from cycle to cycle.
The edge is where the straight line between the last sample and this one
crosses the volt, the trigger's own level; a sampled edge is known no better
than that without the samples after it, which would put a frame of latency
on every tick. Everything that counts in whole frames, a Seq's clock and a
Clock in BPM, reads `period` and `lastFrame`, which count whole frames. */
struct TrigCounter {
	dsp::SchmittTrigger trig;
	int64_t edges = 0;
	int64_t lastFrame = -1; // the frame of the last edge, -1 for none
	int64_t period = 0;     // frames between the last two, nought until there have been two
	int64_t periodBefore = 0; // and as it stood before the last edge (`resetWith`)
	bool skipGap = false;   // a reset fell in this gap, which is no tempo (`resetFell`)
	float was = 0.f;        // the last sample's volts, for where the edge fell between it and this one
	double after = 0.0;     // the part of the last edge's frame after the crossing, nought to one
	double fine = 0.0, fineBefore = 0.0; // `period` and `periodBefore` to that part of a frame

	/** The time an edge stood at, in frames: its frame's end less the part
	after it, so a time and a frame meet as a Clock's ticks do (`Count::after`). */
	double timeOf(int64_t frame, double part) const { return (double)frame + 1.0 - part; }
	bool watch(Input& in, int64_t frame) {
		if (!in.isConnected()) {
			edges = 0;
			lastFrame = -1;
			period = 0;
			fine = 0.0;
			skipGap = false;
			was = 0.f;
			return false;
		}
		const float v = in.getVoltage();
		const float before = was;
		was = v;
		if (!trig.process(v, TRIG_LOW, TRIG_HIGH))
			return false;
		// The crossing, by the line between the two samples: below the volt
		// before, at or over it now, so the denominator is never nought.
		const double part = v > before ? std::min(std::max((double)(v - TRIG_HIGH) / (double)(v - before), 0.0), 1.0) : 0.0;
		periodBefore = period;
		fineBefore = fine;
		if (lastFrame >= 0 && !skipGap) {
			period = frame - lastFrame;
			fine = timeOf(frame, part) - timeOf(lastFrame, after);
		}
		skipGap = false;
		lastFrame = frame;
		after = part;
		edges++;
		return true;
	}
	/** The edges as a driver would send them: each a pulse, and the cable
	always taken to be coming. */
	void count(Count& in) const {
		in.present = true;
		in.pulses = (uint32_t)edges;
		in.period = (double)period;
		in.running = true;
		in.lastFrame = lastFrame;
	}
	/** A reset fell in the gap now open: it leaves the tempo alone, and the
	gap is not measured, so stop, reset, start keeps the tempo it had (the
	clock foundation; a following Clock's `skipGap` is the same rule). A reset
	bound to the edge before it, inside the window, is that edge's, and its
	gap is measured as ever. */
	void resetFell() {
		if (lastFrame >= 0)
			skipGap = true;
	}
	/** A reset that came with the edge just counted and before it, as a
	Clock's RESET does (a trigger and a RESET together are a start): the gap
	that edge closed is the one the reset fell in, so the period stands as
	it was before it, and the gap that edge opens is measured. */
	void resetWith() {
		period = periodBefore;
		fine = fineBefore;
		skipGap = false;
	}
};

/** A scale's chord on a jack, one note a channel, and one silent channel
for a scale with no notes. Returns how many channels it meant, which an
uncabled jack does not keep (`JackGlow::channelsMeant`). */
inline int writeChord(Output& out, const Scale& scale) {
	float chord[NOTES];
	const int n = chordOf(scale, chord);
	out.setChannels(std::max(n, 1));
	for (int c = 0; c < std::max(n, 1); c++)
		out.setVoltage(c < n ? chord[c] : 0.f, c);
	return std::max(n, 1);
}

} // namespace rig
