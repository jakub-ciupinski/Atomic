// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "plugin.hpp"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

/** The tree: the rig parsed once into a graph, evaluated in rig order.

A node is a module as the graph sees it. It announces the traits it plays,
one bit each, and declares the slots it wants: a slot names a trait, says
whether filling it claims the node found, and whether a node already claimed
will do. The build runs strictly left to right, one node at a time; each
node searches leftward one module at a time, testing every candidate against
its unfilled declarations, and is settled, its slots filled and its traits
final, before the next node begins. Every slot records the distance in
modules to what filled it, and policy about that distance belongs to whoever
reads the slot; the one exception is a declaration's reach, which says how
far a search may go for it at all, so that a peer slot means the like module
touching and claims nothing farther off, and its stop, traits that end the
search for it empty-handed, so that a Poly's bundle reaches back to the
previous Poly and no further. A claim is a flag, not invisibility: it changes what a later
search that does not accept claimed nodes finds, and it is what tells a
cartridge it stands down for the reader that took it.

Nothing looks right, so rig order is an evaluation order: every child stands
to the left of every parent. Evaluation is one pass over the rig, children
before parents: a change marks its own node dirty, that node is evaluated
when the pass reaches it, and only if what it hands rightward actually moved
are the nodes that read it marked in turn. That is the gate, and it is why a
phase arriving at audio rate into a Seq that floors it to the step it was
already on costs one node a sample rather than a hundred. A face is how a trait is read
and written, a small interface a node returns from a getter, null by
default; a node that forwards, a Seq, returns the face of the member under
its playhead, so the face is asked for at use and never cached. The faces
themselves are declared with the rig's value types, in Signal.hpp. */

namespace rig {

/** What kind of reader a node is, the `reads` column of the manifest: a Seq
walks, a Poly bundles, a Mix sums, an OUT Atn takes the one signal to its
left, and everything else reads no block (READ_ANY). A shaper serves
everything to its right, whatever reads it. */
enum Reading : int8_t { READ_ANY = -1, READ_SEQ, READ_POLY, READ_MIX, READ_OUT };

/** The object's size, for `Node::footprint`: one line in every module of
the rig, the members' and those ROW_MANIFEST writes. */
#define MEMBER_FOOTPRINT \
	size_t footprint() const override { return sizeof(*this); }

namespace tree {

enum Trait : uint32_t {
	T_CARTRIDGE = 1u << 0, // a playhead can walk it, a Poly can bundle it
	T_BUNDLE = 1u << 1,    // a Poly: the channels of its cable, for the members to its right
	T_CV = 1u << 2,        // the kinds, for the peer slot alone (the curve, T_CRV, is bit 16)
	T_QNT = 1u << 3,
	T_ATN = 1u << 4,
	T_TRG = 1u << 5,
	T_SOURCE = 1u << 6,  // a step of its own: a reader walking it takes the value it holds
	T_STAGE = 1u << 7,   // a step that transforms: a reader walking it hands it the reader's cable
	T_SHAPER = 1u << 8,  // shapes the window a CV is read in
	T_ARP = 1u << 9,     // a Poly: the nearest thing a Seq to its right can sequence, it plays its channels and walks nothing past it
	T_ROUTER = 1u << 10, // an Atn carrying a signal of its own, lit or cabled: where a block of shaping Atns ends
	// Bit 11 is free.
	T_CLOCK = 1u << 12,
	T_RESTART = 1u << 13, // the bracket's RESET starts it again
	T_READER = 1u << 14,  // a Seq, a Poly, a Mix or an OUT Atn: numbered for colour
	// Bit 15 is free.
	T_CRV = 1u << 16, // the fifth kind, a curve, for the peer slot alone
	T_LAST = T_CRV
};
/** The five kinds together: what a cartridge's peer slot looks for, and what
a Seq asks of the first member of the block it walks. */
constexpr uint32_t KIND_BITS = T_CV | T_QNT | T_ATN | T_TRG | T_CRV;
/** What a parent tells a child: one bit a statement, the other half of a
slot. A slot says what its node looks for to its left; its `tells` says what
the node it fills is told by being found there, and that is the only way a
node learns anything about what holds it. A node never reads who holds it, its
in-edges, its claimers or its readers, and never looks right: those are
private to the tree, which delivers every statement in one place
(`Node::hear`), so the graph is configured top down, parents configuring
children and never the other way round. A
statement made to a member is passed on down its block by the peer slot's
`relays`, so every member of a walked block hears what the reader said to the
rightmost. */
enum Told : uint32_t {
	TOLD_CLAIMED = 1u << 0, // spoken for: a slot that claims has found me; said by the tree of every claim
	TOLD_STEPS = 1u << 1,   // a Seq walks the block I stand in: I am a sequence's steps, read in my window
	// The Atn holding me as its peer shapes nothing: a VCA, an Atn with a
	// cable in IN, or a lit Atn, a reader. Its claim makes me no overridden
	// member, so unless a reader walks the block I shape the window as if it
	// were not there.
	TOLD_PEER_UNSHAPED = 1u << 2,
	TOLD_BUNDLED = 1u << 3, // a Poly bundles me, or a Mix sums me: the two share one pool
	TOLD_COUNTED = 1u << 4, // a reader takes my count
	// Bit 5 is free.
	TOLD_WINDOW = 1u << 6,  // a window slot holds me: something to my right reads its window through me
	TOLD_ATN_WINDOW = 1u << 7, // the same, said by an Atn's window slot: the next Atn holds me, and takes my window's place
	// The block of CVs whose gap I stand in has no Qnt there: where my own
	// window slot finds nothing, my window is the lane of the block of CVs
	// before it (an Atn's `lane` slot). Said over the finished parse by that
	// block's leftmost CV to every shaper of its gap (`CV::parsed`), since a
	// window slot never claims and so passes nothing on.
	TOLD_LANE = 1u << 8,
	// The same block's gap has a Qnt in it: a Qnt told it whose window slot
	// found nothing starts from the lane of the block of CVs before, and
	// keeps its range where the lane's mode is its own.
	// An Atn told it starts from nothing, the Qnt to its right laying it.
	TOLD_LANE_Q = 1u << 9,
	TOLD_CHAINED = 1u << 10, // a CRV holds me as its peer: I am not the last of my block, and the last says what my arc is
	// What holds my window plays it as: a CRV plays volts and a CV lands on
	// notes, so an Atn in front of a CRV shapes in volts though a scale
	// arrives from its left, and one in front of a CV in notes. Said by the
	// holder, never worked out by the Atn from what stands to its left; where
	// two of them hold one Atn, a CV, or a CRV, the nearest's word stands
	// (`Node::hear`), so in ATN CV QNT CV the CV beside the Atn says how it
	// is played and the Qnt past it is not heard on it: the closest wins.
	TOLD_PLAIN = 1u << 11,  // a CRV's window slot holds me: my window is played as volts
	TOLD_PITCH = 1u << 12,  // a CV's window slot holds me: my window is played as notes when a scale is in reach
	TOLD_PLAYED = TOLD_PLAIN | TOLD_PITCH,
	// Who reads the block, which is every member's business and not only the
	// rightmost's.
	TOLD_BLOCK = TOLD_STEPS | TOLD_BUNDLED,
	// What a block's peer chain passes on from its rightmost member: who
	// walks the block, a Seq, whose slot tells the rightmost alone. Not a
	// Poly's statement, which it makes to every member it takes, and which
	// passed on would mark a member taken that it left for the next Poly, the
	// seventeenth CV of a block.
	TOLD_RELAYED = TOLD_STEPS,
};
/** A reader that claims its block, a Seq, has taken mine. */
inline bool walked(uint32_t told) { return (told & TOLD_STEPS) != 0; }

/** Every trait's name and what it means, for a printed parse and the Table,
which draws them as a plugin of its own and links no engine: inline, so. */
struct TraitWords {
	uint32_t bit;
	const char* name;
	const char* blurb;
};
inline constexpr TraitWords TRAIT_WORDS[] = {
	{T_CARTRIDGE, "CARTRIDGE", "a playhead can walk it, a Poly can bundle it"},
	{T_BUNDLE, "BUNDLE", "a Poly: the channels of its cable, for the members to its right"},
	{T_CV, "CV", "a fader, for the peer slot alone"},
	{T_QNT, "QNT", "a scale, for the peer slot alone"},
	{T_ATN, "ATN", "a top, for the peer slot alone"},
	{T_TRG, "TRG", "gates, for the peer slot alone"},
	{T_SOURCE, "SOURCE", "a step of its own: a reader walking it takes its value"},
	{T_STAGE, "STAGE", "a step that transforms the reader's cable"},
	{T_SHAPER, "SHAPER", "shapes the window a CV is read in"},
	{T_ARP, "ARP", "a Poly: the nearest thing a Seq to its right can sequence, it plays its channels and walks nothing past it"},
	{T_ROUTER, "ROUTER", "an Atn carrying a signal of its own, lit or cabled: where a block of shaping Atns ends"},
	{T_CRV, "CRV", "a curve, for the peer slot alone"},
	{T_CLOCK, "CLOCK", "the count every playhead to its right walks"},
	{T_RESTART, "RESTART", "the bracket's RESET starts it again"},
	{T_READER, "READER", "a Seq, a Poly, a Mix or an OUT Atn: numbered for colour"},
};
inline const TraitWords* wordsOf(uint32_t bit) {
	for (const TraitWords& t : TRAIT_WORDS)
		if (t.bit == bit)
			return &t;
	return nullptr;
}

inline const char* traitName(uint32_t bit) {
	const TraitWords* t = wordsOf(bit);
	return t ? t->name : "?";
}

/** What a trait means, in a line, for the Table module's panel. */
inline const char* traitBlurb(uint32_t bit) {
	const TraitWords* t = wordsOf(bit);
	return t ? t->blurb : "";
}

inline std::string traitNames(uint32_t traits) {
	std::string s;
	for (uint32_t bit = 1; bit <= T_LAST; bit <<= 1)
		if (traits & bit) {
			if (!s.empty())
				s += " ";
			s += traitName(bit);
		}
	return s;
}

struct SlotDecl {
	const char* name;
	uint32_t trait;
	bool claims;
	bool acceptsClaimed;
	int reach = 0; // the farthest distance that fills it; nought for any. A peer is one: the like module touching.
	uint32_t stopAt = 0; // traits that end the search unfilled, whatever else the node plays: a Poly's bundle stops at the previous Poly
	bool chain = false;  // and read the whole chain behind what fills it: the node's own slot of this index, followed
	uint32_t tells = 0;  // what the node filling it is told (`Told`); a claim adds TOLD_CLAIMED of itself
	uint32_t relays = 0; // of what this node is told, what it passes on to the node filling it: a claiming slot's only
};

struct Node;
/** The nodes standing to one node's left, and only those: what `parsed` is
handed, so a node going over the finished parse can look only where the
grammar lets it, at what it stands in front of. */
struct Left {
	const std::vector<Node*>& rig;
	int end; // the node's own place: everything before it
	int size() const { return end; }
	Node* operator[](int i) const {
		assert(i >= 0 && i < end);
		return rig[i];
	}
};
struct Slot {
	Node* node = nullptr;
	int distance = 0;
	bool stopped = false; // the search for it met its stop first
	bool filled() const { return node != nullptr; }
};

/** What `fineGroup` works in: the nodes at every wake and their clusters,
kept by the host and sized when it bonds (`reserve`), so working the group
out again on a wake allocates nothing on the audio thread. */
struct FineScratch {
	std::vector<Node*> always;
	std::vector<size_t> up;
	std::vector<int> size;
	std::vector<char> in;
	void reserve(size_t n) {
		always.reserve(n);
		up.reserve(n);
		size.reserve(n);
		in.reserve(n);
	}
};

struct Node {
	uint32_t traits = 0;
	std::vector<Slot> slots; // parallel to declarations()
	int place = -1; // index in the rig
	bool dirty = true;
	bool always = false; // at every wake whatever the gate says: see atEveryWake()
	bool touch = false;  // a node at every wake can reach it, so its reads are cleared: see build()
	bool ranNow = false; // it evaluated in the wake being run
	// Whether another node has written into me since my reads were last
	// cleared (`holds`), and whether what I would publish may differ from
	// what I last published (`moved`): see `written` and `evaluate`.
	bool holds = false;
	bool moved = true;
	// Published at every wake that reaches me, written into or not: what I
	// publish is worked out afresh from a param or a jack as I publish it
	// (an Atn's offset, from its knob and its CV), so the host may not
	// leave it out.
	bool publishesLive = false;
	// A reader at every wake that plays its writer's ramp (`fine`, said by
	// the node, `fineMoved` set when it changes), and what a wake asked
	// only for such readers runs and clears (`fineGroup`).
	bool fine = false;
	bool fineRun = false, fineReach = false;
	// Whether the wake running me, gated, was asked for the fraction alone:
	// set by the host as it runs me, so a reader can tell that nothing but
	// its own ramp can have moved since it last ran.
	bool fineWake = false;
	std::vector<Node*> reached; // an at-every-wake node's reach, itself among it, as `fineGroup` found it
	void setFine(bool f);

	virtual ~Node() {}
	/** The slots wanted, in the order they are filled and indexed. */
	virtual const std::vector<SlotDecl>& declarations() const {
		static const std::vector<SlotDecl> none;
		return none;
	}
	/** Asked at the top of every parse, before anything is announced or
	searched: the one moment a node may change which row of the manifest it
	is, an Atn set to OUT (`declarations`, `announce` and `manifestId` all
	follow what it decided here, and nothing else may move them). */
	virtual void beforeParse() {}
	/** The traits played before anything is adopted. */
	virtual uint32_t announce() { return 0; }
	/** Called once the slots are filled: the place to add forwarded traits
	and to register with what was adopted. The traits are final after it. */
	virtual void settle() {}
	// A wake is the host running the three passes below over the rig once,
	// on a sample its gate lets through (Tree.cpp, "One wake").
	/** The host's first pass over the rig: drop everything a reader wrote
	last time, lanes, colours, lit keys, so the readers can write afresh. */
	virtual void clearReads() {}
	/** The host's second pass: read the children through their faces,
	publish, write leftward where the rules allow. */
	virtual void evaluate(const Module::ProcessArgs& args) {}
	/** The host's third pass: hand the screens what this wake made of them,
	all at once. Nothing a screen reads may be cleared in the first pass and
	written in the second, because a frame drawn between the two sees the
	gap, a flicker a running clock would put on a Qnt, on an Atn and on the
	Clock's own ring. What a reader writes is kept in plain fields through
	the wake and published here, so a screen sees either the last wake whole
	or this one whole and never the space between them. */
	virtual void publishReads() {}
	virtual std::string nodeName() = 0;
	/** Which row of the manifest this node is, or minus one for a node with
	no row (a test's stub). Written by ROW_MANIFEST, never by hand. */
	virtual int manifestId() const { return -1; }
	/** How many nodes this one stands in front of for `trait`: the block a
	forwarder walks, which every write travelling on a trait it forwards has
	to reach. Nought for a node that forwards nothing. */
	virtual int reachOf(uint32_t trait) { return 0; }

	/** What the nodes holding me have told me, every statement they made
	(`Told`): the one thing a node knows of what stands to its right. A
	slot's statement is told as the slot fills, during the build; a Poly's
	in pass nought of `parsed`, so read it from pass one on, except by a
	Poly, whose own pass nought comes after every Poly to its left has told
	(pass nought goes over the rig left to right), which is how a Poly passes
	what one before it bundled. */
	uint32_t told() const { return heard; }
	/** Spoken for: a slot that claims has found me. */
	bool claimed() const { return (heard & TOLD_CLAIMED) != 0; }
	/** The traits I would play on the other row I may stand on, which no
	search finds me by now: an Atn lit or cabled is no shaper, and would be
	one unlit and unpatched. Nought for every other node. */
	virtual uint32_t latentTraits() { return 0; }
	/** What the slots that passed me over for want of my latent traits would
	have told me had I played them (`latentTraits`), every statement as
	`told` has them but relayed nowhere: a search that reached me and went on
	past, so I would have been the one it filled. Never a slot filled and
	never an edge, only a word from the right, so an Atn whose arrow is lit
	still knows whether something to its right would read its window. */
	uint32_t wouldHear() const { return wouldHeard; }

	const Slot& slot(int i) const { return slots[i]; }
	Node* child(int i) const { return slots[i].node; }
	int distance(int i) const { return slots[i].distance; }
	/** A change here: this node needs evaluating.

	It marks nothing above it: the walk upward happens in the evaluation, one
	node at a time and only where something actually moved (see `evaluate` and
	`published`). Marking every ancestor before anything is worked out would
	make a jack moving at audio rate re-evaluate the whole rig forty-eight
	thousand times a second. */
	void markDirty() { dirty = true; }
	/** Another node has written into me in this wake: a lane, a playhead,
	a reader's colour, a lit key, an arc told, a use said. Every such write
	calls it, because it is the only way the host learns that a node it
	did not run has anything new to clear or to publish. A write that
	forgets it leaves a screen a wake stale or lit after its playhead has
	gone, which the quiet check (`checkQuiet`) catches. */
	void written() { holds = moved = true; }
	/** The size of the whole object, for the quiet check that a clear or a
	publish the host leaves out would have changed nothing: written by
	MEMBER_FOOTPRINT, which ROW_MANIFEST and every member says, nought for a
	node that says neither (a test's stub). */
	virtual size_t footprint() const { return 0; }
	/** Whether this node is at every wake of the rig, gate or no gate. Asked
	once, at the parse, and there are exactly two reasons to say yes.

	One: the node writes into the nodes behind it (a playhead's colour on
	the member it stands on, a lane, a lit key) and all of that is cleared
	for the whole rig at the top of every wake, so every hand that makes such
	a write has to be at the table to make it again.

	Two: the node keeps state that advances with the count rather than with
	its own inputs: a Clock's count. Sitting a
	wake out would not give it the wrong answer later so much as a different
	one, and a different one is indistinguishable from a bug.

	It is not a speed a module claims, and it is not a column in the
	manifest. It is a short list: the Seq, the Poly, the Mix, the OUT Atn and the Clock. */
	virtual bool atEveryWake() { return false; }
	/** Called over the finished parse, every node once a pass, with the
	nodes to its left and no others: pass nought for what a node gathers
	there and tells (a Poly's bundle), pass one for what it makes of what it
	has been told (a Clock a reader takes), pass two for edges that follow.
	A node may tell, and may draw read edges of its own (`alsoReads`), never
	fill slots. */
	virtual void parsed(const Left& left, int pass) {}
	/** The gate. Asked straight after `evaluate`: has anything this node
	hands rightward actually moved since the last wake? False, and the change
	dies here and nothing that reads this node is woken. True by default, so
	a node that has not said what it publishes never gates. */
	virtual bool published() { return true; }

protected:
	/** A statement no slot carries, made over the finished parse: a Poly
	telling every member of its bundle, which reaches past its one slot. To
	the left only, and delivered as a slot's statement is (`hear`). */
	void tell(Node* child, uint32_t what) {
		assert(child && child->place < place);
		hear(child, what);
	}
	/** I read `child` outside my slots, so a change there wakes me: a Poly's
	bundle past its slot, a Qnt reading its run's CLEAR through its peer. An
	edge of mine, drawn from me to my left; it tells the child nothing. */
	void alsoReads(Node* child) {
		assert(child && child->place < place);
		if (std::find(child->parents.begin(), child->parents.end(), this) == child->parents.end()) {
			child->parents.push_back(this);
			also.push_back(child);
		}
	}
	/** A gatherer's members, each read outside my slots and told it is
	taken (`TOLD_BUNDLED`), after the gathering and not during it: the Poly's
	and the Mix's one way of taking a pool (src/Gather.hpp). */
	template <class T>
	void takeGathered(const std::vector<T*>& members) {
		for (T* m : members) {
			alsoReads(m);
			tell(m, TOLD_BUNDLED);
		}
	}

private:
	// The in-edges are the tree's alone: what a change travels along, and
	// who claimed me, for the printed parse. No module can name them, which
	// is the top-down rule held by the compiler (`HolderProbe`, Member.hpp).
	uint32_t heard = 0;
	uint32_t wouldHeard = 0; // see `wouldHear`
	std::vector<Node*> parents;  // nodes whose slots hold this one, and those that `alsoReads` it
	std::vector<Node*> claimers; // those of them that claimed it, leftmost first
	// The out-edges `alsoReads` drew, mine to my left: a node at every wake
	// reaches them as it reaches its slots, so what it writes into them is
	// cleared and published (`build`), a Poly's members beyond its slots
	// among them.
	std::vector<Node*> also;
	/** The one place a statement lands: on `to`, and on down every slot of
	its that relays it. */
	static void hear(Node* to, uint32_t what);
	friend void build(const std::vector<Node*>& rig);
	friend void evaluate(const std::vector<Node*>& rig, const Module::ProcessArgs& args, bool fineOnly);
	friend std::string print(const std::vector<Node*>& rig);
	friend void fineGroup(const std::vector<Node*>& rig, FineScratch& scratch);
	friend struct Inspect; // a headless test's window on the edges, defined outside the plugin
};

/** The parse: slots filled, claims made, traits settled, left to right. */
void build(const std::vector<Node*>& rig);
/** Whether anything in the rig needs evaluating. */
bool anyDirty(const std::vector<Node*>& rig);
/** One evaluation of the rig, children before parents: clear the reads,
then evaluate the nodes that need it in rig order, passing the change on to
whoever reads a node whose answer moved, and hand the screens the lot. */
void evaluate(const std::vector<Node*>& rig, const Module::ProcessArgs& args, bool fineOnly = false);
/** The gate, off: every node evaluated whether or not anything it reads
moved. The headless tests run one copy of every patch each way and require
them to agree sample for sample; nothing else ever turns it off. */
extern bool gating;
/** Two counters the bench reads: wakes of the rig and evaluations of a
single node. Nothing but the bench looks at them, and nothing turns on
them; they are how a number like "one node a sample instead of a hundred"
is checked rather than claimed. A thread's own, since two rigs' hosts may
wake at once on two engine threads. */
extern thread_local long long wakes;
extern thread_local long long runs;
/** The check on the quiet nodes (the third pass of `evaluate`): the
host clears and publishes every node it reaches, the quiet ones included,
and counts in `quietBroken` every clear or publish it would have left out
that changed a byte of the node. Nothing but the headless tests turns it
on. */
extern bool checkQuiet;
extern long long quietBroken;
/** Set whenever a node's `fine` changes, so the host whose wake it is knows
to work its fine group out again. A `fine` changes only inside its rig's
`evaluate`, which one thread runs whole, so the thread's own flag is that
rig's: cleared by its host before the wake and read after, it rings no other
rig, where a plugin-wide serial would make every rig in the patch rework
its group for one Seq's glide. */
extern thread_local bool fineMoved;
/** The fine group: the readers at every wake that play their writer's ramp,
and every reader at every wake that shares anything but a Clock with them,
read or written, to a fixed point; `fineRun` on those, and `fineReach` on
everything they reach. A wake asked only for the fraction runs the group and
leaves every other reader where it stood: nothing they read has moved. */
void fineGroup(const std::vector<Node*>& rig, FineScratch& scratch);
/** The parse as text a person can check by eye, one line per node. */
std::string print(const std::vector<Node*>& rig);

} // namespace tree
} // namespace rig
