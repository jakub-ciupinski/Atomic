// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Tree.hpp"
#include "Manifest.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>

namespace rig {
namespace tree {

/** Has this node claimed a cartridge: is there a block behind it at all. */
static bool claimsABlock(Node* n) {
	const std::vector<SlotDecl>& decls = n->declarations();
	for (size_t k = 0; k < decls.size() && k < n->slots.size(); k++)
		if (decls[k].claims && n->slots[k].filled() && (n->slots[k].node->traits & T_CARTRIDGE))
			return true;
	return false;
}

void build(const std::vector<Node*>& rig) {
	for (size_t i = 0; i < rig.size(); i++) {
		Node* n = rig[i];
		n->beforeParse();
		n->place = (int)i;
		n->heard = 0;
		n->wouldHeard = 0;
		n->traits = 0;
		n->parents.clear();
		n->claimers.clear();
		n->also.clear();
		// Room for every edge a node can have, each to another node of the
		// rig once, made here and kept, so a parse of a rig no longer than
		// the last and the fine group after it allocate nothing.
		n->parents.reserve(rig.size());
		n->claimers.reserve(rig.size());
		n->also.reserve(rig.size());
		n->reached.reserve(rig.size());
		n->slots.assign(n->declarations().size(), Slot());
		n->dirty = true;
		n->always = false;
	}
	// Left to right, each node settled before the next searches: a node's
	// traits may depend on what it adopted, and a search only ever tests
	// nodes to its left, which are final by then.
	for (size_t i = 0; i < rig.size(); i++) {
		Node* n = rig[i];
		n->traits = n->announce();
		const std::vector<SlotDecl>& decls = n->declarations();
		size_t unfilled = decls.size();
		for (int d = 1; unfilled > 0 && (int)i - d >= 0; d++) {
			Node* c = rig[i - d];
			for (size_t k = 0; k < decls.size(); k++) {
				if (n->slots[k].filled() || n->slots[k].stopped)
					continue;
				const SlotDecl& decl = decls[k];
				if (decl.reach > 0 && d > decl.reach)
					continue;
				// A stop trait ends the search whatever else the node plays:
				// a Poly is a reader, which its block slot wants, and the
				// previous Poly is where its bundle stops.
				if (c->traits & decl.stopAt) {
					n->slots[k].stopped = true;
					unfilled--;
					continue;
				}
				if (!(c->traits & decl.trait)) {
					// Passed over for want of a trait it would play on its
					// other row: what this slot would have told it, had the
					// search stopped here (`wouldHear`).
					if ((c->latentTraits() & decl.trait) && !(c->claimed() && !decl.acceptsClaimed))
						c->wouldHeard |= decl.tells;
					continue;
				}
				if (c->claimed() && !decl.acceptsClaimed)
					continue;
				n->slots[k].node = c;
				n->slots[k].distance = d;
				// The parent edges are read edges, and they are what a change
				// travels along; every slot is one. And once: a node that
				// fills several of my slots (an Atn's left neighbour is its
				// peer, its window and its signal) is one parent, not three,
				// or a walk over the edges branches at every hop and a block
				// of a dozen takes a second a sample.
				if (std::find(c->parents.begin(), c->parents.end(), n) == c->parents.end())
					c->parents.push_back(n);
				// A chained slot reads everything behind what filled it along
				// the same slot: the last CRV of a block says every member's
				// arc, so each of them has to wake it.
				if (decl.chain)
					for (Node* m = c->slots.size() > k ? c->slots[k].node : nullptr; m;
					     m = m->slots.size() > k ? m->slots[k].node : nullptr)
						if (std::find(m->parents.begin(), m->parents.end(), n) == m->parents.end())
							m->parents.push_back(n);
				// What the slot says to what it found, told now, while the build
				// is still going, so a node further right hears it before it
				// searches.
				if (decl.claims)
					c->claimers.push_back(n);
				if (const uint32_t says = decl.tells | (decl.claims ? TOLD_CLAIMED : 0))
					Node::hear(c, says);
				unfilled--;
			}
		}
		const uint32_t announced = n->traits;
		n->settle();
		// The manifest is the law: settle may add the traits the row allows
		// it to forward and nothing else, and may take none away.
		if (n->manifestId() >= 0) {
			const manifest::Spec& sp = manifest::spec((manifest::Id)n->manifestId());
			assert((n->traits & announced) == announced);
			assert((n->traits & ~(announced | sp.forwards)) == 0);
			// And a row that forwards stands in front of what it walks: a
			// downward write travelling on a forwarded trait has to reach
			// the block, so a forwarder holding one must answer with it.
			if (sp.forwards && claimsABlock(n))
				assert(n->reachOf(sp.forwards) > 0);
			(void)sp;
		}
		(void)announced;
	}
	// Who is at every wake whatever the gate says: see `atEveryWake`. It is
	// asked last, over the finished parse, because a claim is made by the
	// node to the RIGHT of the one it claims and a node asked in the middle
	// of the walk has not been claimed yet, so it would answer as though
	// nothing held it.
	for (int pass = 0; pass < 3; pass++)
		for (Node* n : rig)
			n->parsed(Left{rig, n->place}, pass);
	for (Node* n : rig)
		n->always = n->atEveryWake();
	// And what those nodes can reach, which is what has to be cleared and
	// published for them. A write into another node travels along the slots:
	// the block a playhead walks, the window chain behind each member of it,
	// the driver. So the closure of the slots
	// below every node that is at every wake is a superset of everything any
	// of them writes into, and a node outside it is one no hand ever reaches:
	// it is never cleared and never published, and a hundred quantizers
	// standing in a line cost nothing at all while one of them is driven.
	// Clearing more than a writer writes is safe, since every writer is at
	// every wake and fills it in again; clearing less is not, which is why
	// this is a closure and not a list.
	for (Node* n : rig)
		n->touch = n->always;
	for (size_t i = rig.size(); i-- > 0;) {
		Node* n = rig[i];
		if (!n->touch)
			continue;
		for (const Slot& s : n->slots)
			if (s.filled())
				s.node->touch = true; // every child stands to the left, so one pass does it
		for (Node* c : n->also)
			c->touch = true; // and so does everything it reads past its slots, a Poly's bundle
	}
}

void Node::hear(Node* to, uint32_t what) {
	// How my window is played is the nearest holder's word: the parse goes
	// left to right, so the first to say it stands nearest, and a farther
	// holder reaching past it is not heard on it (tree item n1).
	if (to->heard & TOLD_PLAYED)
		what &= ~TOLD_PLAYED;
	to->heard |= what;
	const std::vector<SlotDecl>& decls = to->declarations();
	for (size_t k = 0; k < decls.size() && k < to->slots.size(); k++)
		if (const uint32_t on = what & decls[k].relays)
			if (Node* c = to->slots[k].node)
				hear(c, on);
}

bool anyDirty(const std::vector<Node*>& rig) {
	for (Node* n : rig)
		if (n->dirty)
			return true;
	return false;
}

bool gating = true;
thread_local long long wakes = 0; // each engine thread its own count: two rigs' hosts may wake at once, and only the bench, on its one thread, reads them
thread_local long long runs = 0;
bool checkQuiet = false;
long long quietBroken = 0;
thread_local bool fineMoved = false;

void Node::setFine(bool f) {
	if (f != fine) {
		fine = f;
		fineMoved = true;
	}
}

/** The cluster `i` is in, halving the way as it goes. */
static size_t rootOf(std::vector<size_t>& up, size_t i) {
	while (up[i] != i)
		i = up[i] = up[up[i]];
	return i;
}

void fineGroup(const std::vector<Node*>& rig, FineScratch& scratch) {
	// What a node can write into: what it reaches through its slots and the
	// reads it drew of its own, itself among them, but never through a
	// reader slot or into a Clock, which are the colours' order and the
	// count, read at the parse and on a wake respectively and written
	// through by nobody. Through them a Seq reached every block to its
	// left, and every Seq on a Clock was one cluster. A lambda handed
	// itself rather than a std::function, which allocated on every wake
	// that worked the group out, and into `reached`, whose room the parse
	// made (`build`).
	auto reach = [](auto& self, Node* n, std::vector<Node*>& out) -> void {
		if (std::find(out.begin(), out.end(), n) != out.end())
			return;
		out.push_back(n);
		const std::vector<SlotDecl>& decls = n->declarations();
		for (size_t k = 0; k < n->slots.size(); k++) {
			const Slot& s = n->slots[k];
			// A slot that looks for a source as well as a reader, an OUT
			// Atn's signal, is content and not the colours' order: it is
			// reached, as a Mix reaches what it gathers.
			if (!s.filled() || (s.node->traits & T_CLOCK) ||
			    (k < decls.size() && (decls[k].trait & T_READER) && !(decls[k].trait & T_SOURCE)))
				continue;
			self(self, s.node, out);
		}
		for (Node* c : n->also)
			self(self, c, out);
	};
	std::vector<Node*>& always = scratch.always;
	always.clear();
	for (Node* n : rig) {
		n->fineRun = n->fineReach = false;
		n->reached.clear();
		if (n->always) {
			always.push_back(n);
			reach(reach, n, n->reached);
		}
	}
	// The writers that share a member, a lane, a key, a gathered colour,
	// in one cluster: what one clears the others must write again.
	const size_t na = always.size();
	std::vector<size_t>& up = scratch.up;
	up.resize(na);
	for (size_t i = 0; i < na; i++)
		up[i] = i;
	auto root = [&up](size_t i) { return rootOf(up, i); };
	for (size_t i = 0; i < na; i++)
		for (size_t j = i + 1; j < na; j++)
			for (Node* m : always[i]->reached)
				if ((m->traits & T_CARTRIDGE) &&
				    std::find(always[j]->reached.begin(), always[j]->reached.end(), m) != always[j]->reached.end()) {
					up[root(i)] = root(j);
					break;
				}
	std::vector<int>& size = scratch.size;
	std::vector<char>& in = scratch.in;
	size.assign(na, 0);
	in.assign(na, false);
	for (size_t i = 0; i < na; i++)
		size[root(i)]++;
	// The group: every cluster with a reader that plays the fraction; and
	// every cluster of more than one that reaches another reader at every
	// wake (a Clock aside, whose count on such a wake has not moved), since
	// that one may move and wake part of the cluster, and the rest of it
	// would not be there to write again what was cleared. A lone writer
	// woken that way is run on its own, its members cleared as it comes
	// to them (`evaluate`).
	for (size_t i = 0; i < na; i++) {
		// Only a Seq sits a wake for the fraction out, the one reader whose
		// cost such a wake was made for. Every other node keeps every wake: a
		// Mix beside a Seq over CRVs that sat one out would move a sample
		// late. A node with no row of the manifest (a test's stub) keeps
		// every wake.
		const int id = always[i]->manifestId();
		bool join = always[i]->fine || id < 0 || manifest::spec((manifest::Id)id).reads != READ_SEQ;
		if (size[root(i)] > 1)
			for (Node* m : always[i]->reached)
				if (m != always[i] && m->always && !(m->traits & T_CLOCK))
					join = true;
		if (join)
			in[root(i)] = true;
	}
	// And every cluster that reads a member of the group, to a fixed
	// point: a Mix or a Poly gathering a fine Seq reads its jack as it is
	// on every wake, which moves within a step where nothing the gate
	// compares does, and left out it would hear that a sample late.
	for (bool grew = true; grew;) {
		grew = false;
		for (size_t i = 0; i < na; i++) {
			if (in[root(i)])
				continue;
			for (Node* m : always[i]->reached)
				if (m != always[i] && m->always) {
					const size_t j = std::find(always.begin(), always.end(), m) - always.begin();
					if (j < na && in[root(j)]) {
						in[root(i)] = grew = true;
						break;
					}
				}
		}
	}
	for (size_t i = 0; i < na; i++) {
		always[i]->fineRun = in[root(i)];
		if (always[i]->fineRun)
			for (Node* m : always[i]->reached)
				m->fineReach = true;
	}
}

/** For `checkQuiet`: `step` run on `n`, and whether it changed a byte of
the node, which a step the host would have left out must not. */
template <typename F>
static void quietOrBroken(Node* n, const char* what, F step) {
	const size_t size = n->footprint();
	if (size == 0) {
		step();
		return;
	}
	const unsigned char* at = static_cast<const unsigned char*>(dynamic_cast<const void*>(n));
	std::vector<unsigned char> before(at, at + size);
	step();
	if (std::memcmp(before.data(), at, size) != 0) {
		if (quietBroken++ < 5)
			fprintf(stderr, "checkQuiet: a %s left out would have moved %s %d\n", what, n->nodeName().c_str(), n->place + 1);
	}
}

/** One wake.

The first pass drops everything a reader wrote last time, over the whole rig
and not only over the part of it that is about to run. That is what settles
the one question the gate would otherwise raise: a lane cleared and not
written again is a screen gone grey. It is answered by the rule in `build`
instead: a node that writes behind it is at every wake, gate or no gate,
so every field cleared here has its writer at the table to fill it in again.
The corollary is a rule for `clearReads` itself: it may drop only what
another node wrote, never what the node's own `evaluate` wrote, because a
node the gate leaves out does not run to write it a second time.

The second pass is the rig in order, and it is where the change travels. A
node runs if something it reads moved, or if it writes behind it, or if the
gate is off; then it is asked whether what it hands rightward moved, and only
then are the nodes that read it marked. Every parent stands to the right of
every child, so a mark made here always lands ahead of the pass and is never
made too late.

The third hands the screens the lot, at once, which is the three-pass rule.

A node a writer can reach that neither ran nor was written into has nothing
new for either end: its reads were cleared already and nothing has been put
back, so the first pass leaves it alone, and what it would publish is what it
published last, so the third does too; else a block of CRVs walked by a Seq
that wakes every sample publishes every member every sample for the one the
playhead stands on. Every write into another node says so through `written`,
which is what keeps this exact rather than hopeful. */
void evaluate(const std::vector<Node*>& rig, const Module::ProcessArgs& args, bool fineOnly) {
	wakes++;
	// On a wake for the fraction alone, what the fine group cannot reach
	// keeps what its readers, sitting it out, wrote into it.
	const bool all = !fineOnly || !gating;
	for (Node* n : rig) {
		const bool mine = all || n->fineReach || !n->holds;
		if (!gating || (mine && (n->dirty || (n->touch && n->holds)))) {
			n->clearReads();
			n->moved |= n->holds;
			n->holds = false;
		}
		else if (checkQuiet && n->touch && !n->holds)
			quietOrBroken(n, "clear", [n] { n->clearReads(); });
	}
	for (Node* n : rig) {
		if (!n->dirty && !(n->always && (all || n->fineRun)) && gating)
			continue;
		if (!all && n->always && !n->fineRun)
			// A lone writer outside the group, woken by something it reads:
			// what it wrote last time is cleared now, as the first pass
			// would have, so it writes it afresh rather than twice.
			for (Node* m : n->reached)
				if (m->holds) {
					m->clearReads();
					m->moved = true;
					m->holds = false;
				}
		n->dirty = false;
		n->ranNow = true;
		runs++;
		n->fineWake = !all;
		n->evaluate(args);
		if (!n->published())
			continue; // the change dies here
		for (Node* p : n->parents)
			p->dirty = true;
	}
	for (Node* n : rig) {
		if (!n->ranNow && !(n->touch && (n->moved || n->publishesLive))) {
			if (checkQuiet && n->touch)
				quietOrBroken(n, "publish", [n] { n->publishReads(); });
			continue;
		}
		n->ranNow = false;
		n->moved = false;
		n->publishReads();
	}
}

std::string print(const std::vector<Node*>& rig) {
	std::string out;
	for (Node* n : rig) {
		out += string::f("%d %s", n->place + 1, n->nodeName().c_str());
		if (n->claimed()) {
			out += " (claimed by";
			for (Node* c : n->claimers)
				out += string::f(" %s %d", c->nodeName().c_str(), c->place + 1);
			out += ")";
		}
		out += ": " + traitNames(n->traits);
		const std::vector<SlotDecl>& decls = n->declarations();
		for (size_t k = 0; k < decls.size(); k++) {
			const Slot& s = n->slots[k];
			out += string::f("; %s ", decls[k].name);
			if (!s.filled())
				out += "-";
			else
				out += string::f("%s %d at %d%s", s.node->nodeName().c_str(), s.node->place + 1, s.distance,
				                 decls[k].claims ? ", claimed" : "");
		}
		out += "\n";
	}
	return out;
}

} // namespace tree
} // namespace rig
