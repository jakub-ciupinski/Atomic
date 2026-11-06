// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine.hpp"

/** The rig, computed. One member does the work for all of them: the leftmost
module of the run, the host. It walks the expander pointers once
to learn who its members are, parses them into the tree, keeps both until
the topology epoch moves, and every sample decides whether anything changed.
When something did, it evaluates the tree in rig order and every member's
jacks, lights and screen atomics are written by that. Nothing is passed
between modules and nothing waits a sample: the rig resolves where it
stands.

Deciding whether anything changed is three things and no more, and none of
them asks a module how it behaves. The tick subscribers go first: the short
list of members with state of their own to advance, a clock's ramp or a
delay line, which is asked for afresh after every wake. Then the watch
table, one flat sweep of every number arriving from outside: every channel
of every connected jack every sample, every param every control period.
Then the pokes, for what the user interface has written behind the rig's
back. Anything the sweep marks is given its own sample of private work as
well, so a module that was not subscribed cannot miss an edge on a jack that
has only just moved. */

namespace rig {

static std::atomic<uint32_t> epoch{1};
static std::atomic<uint32_t> cables{1};

uint32_t topology() {
	return relaxed(epoch);
}
void bumpTopology() {
	epoch.fetch_add(1, std::memory_order_relaxed);
}
uint32_t wiring() {
	return relaxed(cables);
}
void bumpWiring() {
	cables.fetch_add(1, std::memory_order_relaxed);
}


/** The table, written down. A port's voltages are a plain array inside the
module and a param's value a plain float, so a pointer into either is good
for that module's life; only the membership and the cables can move them,
and each of those has an epoch of its own. */
void Host::buildWatch() {
	jacks.clear();
	knobs.clear();
	buttons.clear();
	counts.clear();
	liveCounts.clear();
	for (Member* m : members) {
		for (size_t i = 0; i < m->inputs.size(); i++) {
			Input& in = m->inputs[i];
			// Every jack's channel count, cabled or not: it is a byte, it is
			// swept on the control period, and it is what tells the table a
			// cable has arrived without trusting anybody to have said so. The
			// wiring epoch makes that immediate rather than true. A cabled
			// jack's count is swept every sample with its volts: a cable that
			// grows from one channel to eight carries its new channels from
			// the sample it grows, where a count swept with the params would
			// leave them unwatched for up to a control period, and the gated
			// rig would hear a Trg's new rings late and the ungated one not.
			// An empty jack's is swept with the params, which is what notices
			// a cable arriving.
			const int ch = (int)in.getChannels();
			(ch > 0 ? liveCounts : counts).push_back(ChanWatch{&in.channels, (uint8_t)ch, m});
			if (ch <= 0)
				continue; // no cable, no voltage to sweep, no cost
			for (int c = 0; c < std::min(ch, m->channelsHeard((int)i)); c++)
				jacks.push_back(Watch{&in.voltages[c], in.voltages[c], m});
		}
		for (size_t i = 0; i < m->params.size(); i++) {
			if (!m->watches((int)i))
				continue;
			ParamQuantity* q = i < m->paramQuantities.size() ? m->paramQuantities[i] : nullptr;
			const bool edge = q && !q->randomizeEnabled; // a momentary button: see Watch
			(edge ? buttons : knobs).push_back(Watch{&m->params[i].value, m->params[i].value, m});
		}
	}
}

/** Who wants the sample tick, asked afresh. */
void Host::retick() {
	for (Member* m : members)
		m->subscribed = false;
	tickers.clear();
	for (Member* m : members)
		if (m->wantsTick()) {
			m->subscribed = true;
			tickers.push_back(m);
		}
}

/** A member the sweep has found moving, marked once however many of its
numbers moved. */
static inline void stir(Member* m, std::vector<Member*>& stirs) {
	if (!m->stirred) {
		m->stirred = true;
		stirs.push_back(m);
	}
}
/** One table swept: every number that moved takes its new value and stirs
its owner. One loop, no virtual calls, nothing but the comparison. */
template <typename W>
static inline bool sweep(std::vector<W>& table, std::vector<Member*>& stirs, bool settings = false) {
	bool any = false;
	for (W& w : table) {
		const auto v = *w.at;
		if (v == w.last)
			continue;
		w.last = v;
		any = true;
		if (settings)
			w.owner->settingsMoved = true;
		stir(w.owner, stirs);
	}
	return any;
}

void Member::process(const ProcessArgs& args) {
	const bool slow = (args.frame % CONTROL_PERIOD) == 0;
	// Who hosts is settled by the pointers, so it is worked out once and kept
	// until the topology moves; a member's sample is then two loads and a
	// return.
	// A member whose setting is a change of the parse has its rig parsed
	// again, here and nowhere else: no bump, which would ring every other
	// rig. Only while the topology stands, since a member may have gone
	// since the epoch moved; when it has moved, `regrouped` asks the same.
	const uint32_t now = topology();
	if (now == knownEpoch) {
		if (slow && host)
			for (Member* m : host->members)
				if (m->parseStale()) {
					host->bond(this);
					poke.store(true, std::memory_order_relaxed);
					break;
				}
	}
	else {
		knownEpoch = now;
		// A bypassed module of ours is no member of any rig: it splits the
		// rig hard where it stands, two rigs that know nothing of each other
		// (`inRig`). Rack steps no bypassed module, so one is never the host.
		Module* left = leftExpander.module;
		// A module that has stopped hosting must not keep a vector of nodes
		// that may point at modules now deleted, so it keeps no rig at all.
		if (inRig(left))
			host.reset();
		else if (!host) {
			host.reset(new Host);
			host->bond(this);
		}
		else if (host->regrouped(this))
			host->bond(this);
		// Woken either way: a wake reads the neighbours as they stand (an
		// Atn alone or not), which a move beside the rig changes with its
		// parse unchanged.
		poke.store(true, std::memory_order_relaxed);
	}
	if (!host)
		return;
	host->sample(args, slow); // nobody else hosts this rig
	// Every member's jacks lit, by the host, after the rig is computed: the
	// host is the one thread that writes them, so it sees every sample they
	// carry, where a member looking at its own jacks in its own process, on
	// another of Rack's threads perhaps, could read them before or after
	// this sample's wake and miss a trigger one sample long (`JackGlow`).
	for (Member* m : host->members)
		m->tickJacks(m, args);
}

bool Host::regrouped(Member* self) {
	walked.clear();
	walked.push_back(self);
	for (Module* m = self->rightExpander.module; inRig(m); m = m->rightExpander.module)
		walked.push_back(static_cast<Member*>(m));
	if (walked != members)
		return true;
	for (Member* m : walked)
		if (m->parseOwed.load(std::memory_order_acquire) || m->parseStale())
			return true;
	return false;
}

void Host::bond(Member* self) {
	parses++;
	members.clear();
	nodes.clear();
	tickers.clear();
	jacks.clear();
	knobs.clear();
	buttons.clear();
	counts.clear();
	liveCounts.clear();
	knownWiring = 0; // whatever the cables are doing, the table is stale
	members.push_back(self);
	for (Module* m = self->rightExpander.module; inRig(m); m = m->rightExpander.module)
		members.push_back(static_cast<Member*>(m));
	// What was owed is paid by this parse; anything owed after it rings again.
	for (Member* m : members)
		m->parseOwed.exchange(false, std::memory_order_acquire);
	// The tree is parsed on the same trigger as the member list and kept as
	// long: the graph does not change shape while the patch plays, only
	// values move through it.
	nodes.assign(members.begin(), members.end());
	// Room for what the parse and the wakes after it work in, made here,
	// once for a rig no longer than before: a bond on a hand's change (an
	// ACROSS flipped) and the fine group worked out after it then allocate
	// nothing on the audio thread.
	Member::behindScratch().reserve(members.size());
	fine.reserve(members.size());
	stirs.reserve(members.size());
	tickers.reserve(members.size());
	restarts.reserve(members.size());
	tree::build(nodes);
	// A member whose row is decided by what the parse finds (an Atn standing
	// alone in its rig, `Atn::standaloneWanted`) asks for another at once, so
	// no wake plays the row it has just left; twice settles every such
	// member, a third is a guard.
	for (int again = 0; again < 3; again++) {
		bool stale = false;
		for (Member* m : members)
			stale = stale || m->parseStale();
		if (!stale)
			break;
		tree::build(nodes);
	}
	fineStale = true; // the group is worked out afresh on the next wake for it
	// What the bracket asks of the rig is the parse's, not the wake's: which
	// nodes a RESET restarts, and so whether there is anything to sync.
	restarts.clear();
	for (tree::Node* n : nodes)
		if (n->traits & tree::T_RESTART)
			restarts.push_back(static_cast<Member*>(n));
	lines.reserve(restarts.size());
	linesWas.reserve(restarts.size());
	linesKnown = false;
	bracketOff = bracketResettable = -1;
}

void Host::sample(const Module::ProcessArgs& args, bool slow) {
	const uint32_t wire = wiring();
	bool fresh = false;
	if (wire != knownWiring) {
		knownWiring = wire;
		buildWatch();
		retick();
		fresh = true;
	}

	bool wake = !ran;
	stirs.clear();
	// The subscribers first. What one of them advances can itself be a
	// number in the table, so the sweep below sees this sample's value and
	// there is no second mechanism here: being ticked is simply how some
	// entries get new values.
	// A wake asked only for a writer's ramp (`fineTick`) runs only the
	// readers that play it, and nothing else may have asked.
	bool coarse = false;
	for (Member* m : tickers)
		if (m->everySample(args)) {
			m->markDirty();
			wake = true;
			coarse = coarse || !m->fineTick;
		}
	// The table, and the buttons beside it, because a press is an edge and
	// an edge missed is a trigger that never happened.
	sweep(jacks, stirs);
	sweep(buttons, stirs, true);
	// A count that moved under a cable that stayed: other channels now.
	bool rewire = sweep(liveCounts, stirs);
	if (slow) {
		sweep(knobs, stirs, true);
		rewire |= sweep(counts, stirs);
	}
	for (Member* m : members)
		if (relaxed(m->poke) && m->poke.exchange(false, std::memory_order_relaxed)) {
			m->settingsMoved = true;
			stir(m, stirs);
		}
	// The bracket's RESET pressed: every node it restarts, nearest the right end
	// first.
	bool synced = false;
	for (Member* m : members)
		if (relaxed(m->resetAsked) && m->resetAsked.exchange(false, std::memory_order_relaxed))
			synced = true;
	// The frame the rig started on, its first sample or a reset taken, for a
	// RIG to my left telling a start's own pulse from the patch's
	// (`RigCore::echoOfStart`).
	if (synced || !ran)
		for (Member* m : members)
			m->startedAt.store(args.frame, std::memory_order_relaxed);
	if (synced)
		for (size_t i = nodes.size(); i-- > 0;)
			if (nodes[i]->traits & tree::T_RESTART) {
				Member* r = static_cast<Member*>(nodes[i]);
				r->onRestart();
				r->settingsMoved = true;
				stir(r, stirs);
			}
	// A Clock's RESET jack is the Clock's own business: it sends the reset to
	// the Seqs of its group in what it sends them (`Count::resets`), whatever
	// cable clocks them, in the wake its jack makes.
	if (rewire) {
		buildWatch();
		fresh = true;
	}
	// A table just written down reads every number as unchanged, because it
	// has only this moment written them down. So the first sample after one
	// is built stirs the whole rig: whatever was already standing in a jack
	// when the cable arrived is new to everybody, and the same is true of the
	// very first sample a rig ever computes.
	if (fresh || !ran)
		for (Member* m : members) {
			m->settingsMoved = true;
			stir(m, stirs);
		}
	// Everything the sweep marked: dirty, and given its own sample of
	// private work if it was not subscribed, so an edge arriving at a jack
	// of a sleeping module is seen on the sample it arrives.
	for (Member* m : stirs) {
		m->stirred = false;
		m->markDirty();
		wake = true;
		if (!m->subscribed)
			m->everySample(args);
	}
	if (slow)
		retick();
	if (!wake) {
		bracketLines(args); // a comparison owed by a wake that came before it was due
		return;
	}

	const bool fineOnly = ran && !coarse && !fresh && !synced && !slow && stirs.empty();
	if (fineOnly && fineStale) {
		fineStale = false;
		tree::fineGroup(nodes, fine);
	}
	ran = true;
	tree::fineMoved = false;
	tree::evaluate(nodes, args, fineOnly);
	if (tree::fineMoved)
		fineStale = true;
	linesOwed = true;
	bracketLines(args);
	// A wake is what changes who has state to advance: a clock told to run,
	// a delay given a cable, a pulse just fired.
	retick();
}

/** The SYNC on the bracket is only a light, so the walks are compared at most
every `LINES_MS` of samples rather than after every wake, which a rig of Seqs
at audio rate, landing on nearly every sample, felt: a walk that moved is told the members up to that much later.
The first wake after a parse and the first at a new sample rate compare at
once, and a comparison owed when it falls due is made on that sample, a wake
or not, since a walk moves only in a wake and stands as it was left. */
static constexpr int LINES_MS = 20;

void Host::bracketLines(const Module::ProcessArgs& args) {
	if (!linesOwed)
		return;
	if (args.sampleRate != linesRate) {
		linesRate = args.sampleRate;
		linesEvery = std::max(1, (int)std::lround(args.sampleRate) * LINES_MS / 1000);
	}
	else if (linesKnown && args.frame < linesDue)
		return;
	linesOwed = false;
	linesDue = args.frame + linesEvery;
	// Whether anything is off the line, for the bracket: two or more walks of
	// one count standing as no Sync could leave them (`inLine`). Told the
	// members only when it moves, and once after every parse.
	lines.clear();
	for (Member* r : restarts) {
		WalkLine l;
		if (r->lineOf(l))
			lines.push_back(l);
	}
	// `inLine` is a function of the walks alone, and a walk moves only when
	// it lands, so a wake that leaves every walk where it was keeps the last
	// answer rather than comparing every pair again, which a rig of nine
	// Seqs on a running Clock felt.
	if (!linesKnown || !sameWalks(lines, linesWas)) {
		linesOff = !inLine(lines);
		linesWas = lines;
		linesKnown = true;
	}
	const bool off = linesOff;
	const bool resettable = !restarts.empty();
	if ((int)off != bracketOff || (int)resettable != bracketResettable) {
		bracketOff = off;
		bracketResettable = resettable;
		for (Member* m : members) {
			m->rigOffNow.store(off, std::memory_order_relaxed);
			m->rigResettableNow.store(resettable, std::memory_order_relaxed);
		}
	}
}

} // namespace rig
