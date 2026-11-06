// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Tree.hpp"
#include <vector>

/** The manifest: what every module of the rig plays and what it wants,
in one table, as data.

This file is the single source. A module does not restate its traits or its
slots anywhere; it names its row here with ROW_MANIFEST and the tree reads
that row. So the table below is not a description of the code that could
drift from it, it is the code: change a line here and the parse changes,
and there is nowhere else for a module to disagree.

Five columns carry the guarantee. `traits` is what the module announces
before anything beside it is looked at, one bit a trait. `forwards` is the
only traits it is allowed to add at settle once its slots are filled, and
nought means it may add none: a Seq wears what it walks, everything else is
what it announced. Forwarding is a duty as well as a licence: a node that
wears a trait because it walks a block has to stand in front of all of that
block, which it says with `behind()` and the tree asserts on every parse
(`reachOf`). `reads` is what kind of reader it is, a Seq walking, a Poly
bundling, a Mix summing or a lit Atn taking one signal, or none: the reach
rules ask a block what kind of reader reads it, and the answer comes from
here rather than from a virtual each reader overrides. `slots` is what it
wants to its left, in the order they are filled and indexed, each naming a
trait to find, whether filling it claims what was found, whether one already
claimed will do, how far the search may reach, nought being any distance and
one the module touching, and whether the whole chain behind what fills it is
read too, which draws the read edges a CRV's chain needs. And each slot says
what it tells the node it fills (`tells`, a `tree::Told`), and which of what
its own node is told it passes on down (`relays`, a peer slot passing on who
reads the block): the only way a node learns anything of what holds it, so
the graph is configured top down and a node never reads its parents.

What is guaranteed, and what is not. The build enforces the manifest: a
module cannot look for a trait it has not declared, claim what it did not say
it claims, or wear a trait it did not announce or forward: the tree asserts
the last of these on every parse, and the headless tests check it over every
rig they build. What a module then does with what it found, once wired, is
its own business and lives in its evaluate. The table says who may talk to
whom; it does not say what they say.

The Table module draws this table on a panel, and it reads these rows,
so what is on the panel in Rack is what the build ran. */

namespace rig {
namespace manifest {

using tree::SlotDecl;

/** One row a module, the five cartridge kinds first; a cartridge is handed
its row by its constructor (`Cartridge(M_CV)`). */
enum Id {
	M_QNT,
	M_CV,
	M_ATN,
	M_TRG,
	M_CRV,
	M_SEQ,
	M_POLY,
	M_MIX,
	M_CLOCK,
	M_OUT,
	M_VCA,
	M_COUNT
};

/** What a module is to a gatherer that takes it: a Poly bundles and a Mix sums
modules of one face. The numbers are the manifest's rows, which is what a
gatherer's choice and its strip store, and the triggers of a Seq walking Trgs,
a face no module is, stand past them. */
enum Face : int8_t {
	F_NONE = -1,
	F_CV = M_CV,
	F_ATN = M_ATN,
	F_CRV = M_CRV,
	F_SEQ = M_SEQ,
	F_POLY = M_POLY,
	F_MIX = M_MIX,
	F_OUT = M_OUT,
	F_TRIG = M_COUNT,
	F_COUNT
};
// A face plus one is packed in four bits (`packOffer`, Gather.hpp).
static_assert(F_COUNT + 1 < 16, "a gatherer packs a face plus one in four bits");

const int MAX_SLOTS = 8;

struct Spec {
	const char* name;    // the row's name, as the family calls it
	const char* modules; // the modules in the browser that are this row
	const char* blurb;   // what it is to the row, in a line
	uint32_t traits;     // announced before anything is adopted
	uint32_t forwards;   // the only traits settle may add; nought for none
	Reading reads;       // what kind of reader it is, if one: a Seq walks, a Poly bundles, a Mix sums, a lit Atn takes one signal
	Face gives;          // what I am to a gatherer that takes me as its product; F_NONE if none takes me
	Face walked;         // what a Seq walking my block is to a gatherer; F_NONE where no Seq walks me
	int nslots;
	SlotDecl slots[MAX_SLOTS];
};

/** Where a lane ends: a window slot never looks
past a block of CVs or CRVs, so what shapes a block stands in its own gap,
between it and the block before it. */
constexpr uint32_t LANE_ENDS = tree::T_CV | tree::T_CRV;

/** What a Seq walks: a block of cartridges, and the Atns among them lit or
not, a lit Atn being a reader.
A lit Atn walked is a step of its own, what its arrow takes, scaled. A Seq's
slot and no cartridge's, so CARTRIDGE_MAY_FIND is untouched. */
constexpr uint32_t SEQ_WALKS = tree::T_CARTRIDGE | tree::T_ATN;

constexpr Spec SPECS[M_COUNT] = {
	{"Qnt", "Qnt", "The scale: it opens a lane for the block of CVs to its right, and quantizes a cable in its own jack.",
	 tree::T_CARTRIDGE | tree::T_QNT | tree::T_STAGE | tree::T_SHAPER, 0, READ_ANY, F_NONE, F_SEQ, 3,
	 {{"peer", tree::T_QNT, true, false, 1, 0, false, 0, tree::TOLD_RELAYED},
	  // Any shaper to its left, past sources and everything else: the window
	  // passes through a Qnt, which adds its scale and spends nothing. What
	  // it holds is told nothing of how it is played: that is the CV's word
	  // (`Member::heldMode`), which reaches past me at the wake, not here.
	  // Never past a block of CVs or CRVs: a Qnt opens a lane, and what stood
	  // before the block before it is another lane's.
	  {"window", tree::T_SHAPER, false, true, 0, LANE_ENDS, false, tree::TOLD_WINDOW},
	  // The lane before, the nearest CV to my left: where my window slot
	  // finds nothing and a block of CVs holds my gap (`TOLD_LANE_Q`), a lane
	  // of my own mode keeps its range.
	  {"lane", tree::T_CV, false, true}}},

	{"CV", "CV", "A fader: a source of volts.",
	 tree::T_CARTRIDGE | tree::T_CV | tree::T_SOURCE, 0, READ_ANY, F_CV, F_SEQ, 4,
	 {{"peer", tree::T_CV, true, false, 1, 0, false, 0, tree::TOLD_RELAYED},
	  // The shaper in my own gap, never past a block of CVs or CRVs: a member
	  // of a block finds its peer first and stops, and takes its peer's
	  // window through the kin slot below.
	  {"window", tree::T_SHAPER, false, true, 0, LANE_ENDS, false, tree::TOLD_WINDOW | tree::TOLD_PITCH},
	  // The nearest Poly to my left, not claimed: with a cable in IN it
	  // sets me from my channel of it, as though in my own jack.
	  {"poly", tree::T_BUNDLE, false, true},
	  // My kin: the nearest CV to my left, past CRVs and anything else, my
	  // peer in a block. Where no Qnt stands in my gap I take its window, an
	  // Atn in my gap overriding the range (`TOLD_LANE`).
	  {"kin", tree::T_CV, false, true}}},

	{"Atn", "Atn", "The reach of the window of the block of CVs or CRVs to its right.",
	 tree::T_CARTRIDGE | tree::T_ATN | tree::T_STAGE | tree::T_SHAPER,
	 // Nothing to a gatherer: an Atn with its arrow unlit and IN empty
	 // carries no signal, so a Poly or a Mix passes it over as though it
	 // were not there, parting no group, a configurator having nothing to
	 // mix. Lit (`M_OUT`) or cabled (`M_VCA`) it is a row of its own that
	 // gives, one kind to a gatherer either way, the lit arrow standing for a
	 // cable.
	 0, READ_ANY, F_NONE, F_SEQ, 3,
	 // The peer is the Atn touching me, which makes a block a Seq can walk:
	 // a shaping Atn's, so a lit or cabled one, a router, ends it, since a
	 // Seq walks the run of one kind nearest it.
	 {{"peer", tree::T_ATN, true, false, 1, tree::T_ROUTER, false, 0, tree::TOLD_RELAYED},
	  // An Atn's window slot says so with a word of its own, so an Atn the
	  // next Atn overrides can say who took its place. Never past a block of
	  // CVs or CRVs.
	  {"window", tree::T_SHAPER, false, true, 0, LANE_ENDS, false, tree::TOLD_ATN_WINDOW},
	  // No poly slot: a Poly's channels are taken by the lit Atns after it,
	  // which are readers, and an Atn shapes the window to its right
	  // whatever stands to its left.
	  // The lane I continue: the nearest CV to my left. Read only while my
	  // window slot found nothing and the block of CVs holding my gap says it
	  // has no Qnt there (`TOLD_LANE`), so CV.. ATN CV.. lays my range on
	  // the first block's scheme. A window, which configures: never the CV's
	  // volts.
	  {"lane", tree::T_CV, false, true}}},

	{"Trg", "Trg",
	 "Up to sixteen triggers: the gates of the Seq to its right, one a step, or, alone, toggles and pulse buttons on a poly out.",
	 tree::T_CARTRIDGE | tree::T_TRG, 0, READ_ANY, F_NONE, F_TRIG, 2,
	 // No restart chain and no threshold slot: the bracket's RESET restarts a
	 // rig, and a Trg's jack counts high at Rack's volt always.
	 {{"peer", tree::T_TRG, true, false, 1, 0, false, 0, tree::TOLD_RELAYED},
	  {"poly", tree::T_BUNDLE, false, true}}},

	// A curve: no LINK and no bend screen, each a bend or, of the spline
	// type, a stretch of the spline through the block's joined points. The last of a block reads its
	// whole peer chain and says every member's arc (`chain`), and its peer
	// slot tells each member it is not the last (`TOLD_CHAINED`).
	{"Crv", "CRV", "A start point, an end point and the curve between, a bend or a spline through the block where its points are joined: a step of a Seq, or a shape a ramp plays.",
	 tree::T_CARTRIDGE | tree::T_CRV | tree::T_SOURCE, 0, READ_ANY, F_CRV, F_SEQ, 3,
	 {{"peer", tree::T_CRV, true, false, 1, 0, true, tree::TOLD_CHAINED, tree::TOLD_RELAYED},
	  // A CRV plays its window as volts and is never quantized, so it tells
	  // what shapes it so: an Atn in front of it is a gain in volts though a
	  // Qnt stands further left. Never past a block of CVs or CRVs, and a
	  // Qnt in my gap is nothing to me (`Crv::windowSource`).
	  {"window", tree::T_SHAPER, false, true, 0, LANE_ENDS, false, tree::TOLD_WINDOW | tree::TOLD_PLAIN},
	  // My kin, the nearest CRV to my left: with no Atn in my gap I borrow
	  // its window.
	  {"kin", tree::T_CRV, false, true}}},

	{"Seq", "Seq",
	 "The playhead: it claims a block, and wears what it walks.",
	 tree::T_READER | tree::T_RESTART,
	 tree::T_SHAPER, READ_SEQ, F_SEQ, F_NONE, 4,
	 // No restart slot: the bracket's RESET restarts every node that plays
	 // T_RESTART and walks no chain (Engine.cpp).
	 // A Poly ends the block's search (T_ARP): the nearest thing a Seq can
	 // sequence being a Poly, it plays the Poly's channels, so SEQ POLY SEQ
	 // is an arpeggio of the left Seq's chord, never two playheads, and CV
	 // CV POLY SEQ an arpeggio of the bundle. A block of Atns, lit or not,
	 // is walked and ends it first, so POLY ATN ATN SEQ walks the two
	 // strips and never arpeggiates the Poly (`SEQ_WALKS`).
	 {{"block", SEQ_WALKS, true, true, 0, tree::T_ARP, false, tree::TOLD_STEPS},
	  {"clock", tree::T_CLOCK, false, true, 0, 0, false, tree::TOLD_COUNTED},
	  {"reader", tree::T_READER, false, true},
	  // The Poly I arpeggiate: the one that ended my block's search, found
	  // past other readers as a block is, so POLY SEQ SEQ is two playheads
	  // over one Poly as CV CV SEQ SEQ is two over one block. Never past a
	  // cartridge or a lit Atn, which would be my block. Not claimed, so any
	  // number of Seqs share it.
	  {"arp", tree::T_ARP, false, true, 0, SEQ_WALKS}}},

	{"Poly", "Poly", "A reader that bundles the mono outputs of one kind to its left, those no Poly before it took, into one polyphonic jack.",
	 tree::T_READER | tree::T_BUNDLE | tree::T_ARP, 0, READ_POLY, F_POLY, F_NONE, 1,
	 // What it bundles is found by no slot: the kind of the nearest module it
	 // can bundle that no Poly to its left has, and every module of that kind
	 // to its left no Poly has, gathered over the finished parse
	 // (`Poly::parsed`).
	 {{"reader", tree::T_READER, false, true}}},

	// The Poly's mirror: it gathers to its left exactly as a Poly does, from
	// the same pool (`gather`, Gather.hpp), and sums what it gathers into one
	// voltage. It offers nothing to its right through the rig, so it is no
	// T_BUNDLE: a CV or an Atn after it takes nothing of it, and a master
	// fader is an Atn cabled from its jack ("there are cases where this could
	// do more harm than good").
	{"Mix", "MIX", "A reader that sums the mono outputs of one kind to its left, those no Poly or Mix before it took, into one jack, each row at its own level.",
	 tree::T_READER, 0, READ_MIX, F_MIX, F_NONE, 1,
	 {{"reader", tree::T_READER, false, true}}},

	{"Clock", "CLK", "The count: ticks for every playhead to its right, and a restart the bracket's RESET starts.",
	 tree::T_CLOCK | tree::T_RESTART, 0, READ_ANY, F_NONE, F_NONE, 2,
	 // The reader or Clock before it, for the colour its group opens with.
	 {{"before", tree::T_READER | tree::T_CLOCK, false, true},
	  // The nearest Clock to its left, at any distance: with nothing in my
	  // RESET I take its resets as my own (`Clock::hearLeft`), so one
	  // trigger into the first Clock restarts the rig, as the bracket's SYNC
	  // does. A child's face, its count, read as a Seq reads its Clock's;
	  // a GAP ends the rig, and so severs it.
	  {"clock", tree::T_CLOCK, false, true}}},

	// An Atn whose arrow is lit: a row of its own and not a cartridge with
	// a wider slot, since what it finds to its left is its content, which no
	// cartridge may take (CARTRIDGE_MAY_FIND). It shapes nothing to its right.
	// Still an Atn of a block (T_ATN), which a Seq walks, a step of its own
	// (T_SOURCE): what it makes of what it takes. Chosen on the arrow, never
	// detected at the parse: the hand guesses it once, when the Atn first
	// stands in a rig, and turns it to the one job a new place offers whenever
	// the Atn bonds with new neighbours (`AtnWidget::bond`).
	{"Out", "Atn",
	 "An Atn with its arrow lit: a VCA of the nearest signal to its left, a Seq, a CV, a CRV or a Mix, past other lit Atns, scaled and offset onto its own jack; after a Poly, a channel strip of one of its channels.",
	 tree::T_READER | tree::T_ATN | tree::T_SOURCE | tree::T_ROUTER, 0, READ_OUT, F_ATN, F_SEQ, 3,
	 // The peer, a router touching me, lit or cabled, so routers touching
	 // are one block a Seq walks and a shaping Atn ends it; telling the Atn
	 // it holds that it shapes nothing in its place (`TOLD_PEER_UNSHAPED`).
	 {{"peer", tree::T_ATN, true, false, 1, tree::T_SHAPER, false, tree::TOLD_PEER_UNSHAPED, tree::TOLD_RELAYED},
	  // The signal: the nearest source or reader, at any distance, claimed or
	  // not, so CV CV SEQ OUT reads the Seq and CV OUT the CV; a lit Atn there
	  // that is no strip hands me its own signal (`Atn::parsed`). A Poly there,
	  // or a lit Atn that is a strip of one, makes me a strip of that Poly,
	  // one channel of it, the next after the strip I read (`Atn::parsed`).
	  {"signal", tree::T_READER | tree::T_SOURCE, false, true},
	  // The reader before it, for the colours' order, as a Mix's.
	  {"reader", tree::T_READER, false, true}}},

	// An Atn with a cable in IN: a VCA of that cable onto its own jack and
	// nothing else, so it shapes nothing to its right, which goes on as
	// though it were not there. A cartridge still, an Atn of a block a Seq can walk as
	// a sequential switch and a Mix sums, but no shaper: no window slot of
	// anything to its right finds it.
	{"Vca", "Atn",
	 "An Atn with a cable in IN: a VCA of that cable onto its own jack, shaping nothing to its right.",
	 tree::T_CARTRIDGE | tree::T_ATN | tree::T_STAGE | tree::T_ROUTER, 0, READ_ANY, F_ATN, F_SEQ, 2,
	 // The peer, a router touching me, as a lit Atn's, telling it that I
	 // shape nothing in its place (`TOLD_PEER_UNSHAPED`); a shaping Atn
	 // ends the block.
	 {{"peer", tree::T_ATN, true, false, 1, tree::T_SHAPER, false, tree::TOLD_PEER_UNSHAPED, tree::TOLD_RELAYED},
	  // The shaper before me, for the unit of my own offset and nothing
	  // else: it tells what it finds nothing, so a Qnt or an Atn whose
	  // window only I would take is as unused as with me gone.
	  {"window", tree::T_SHAPER, false, true, 0, LANE_ENDS, false, 0}}},
};

constexpr const Spec& spec(Id id) { return SPECS[id]; }

/** The rig's grammar, held at compile time. A cartridge
standing in the bond is a modifier: what it finds to its left configures it,
and nothing it finds is content for it to transform. So a cartridge's slots
may only look for the traits below, each of them something that says what the
cartridge is or does, never something whose output it takes as its input. A
stage, a Qnt or an Atn transforming a signal arriving from its left, made
every cartridge both a prefix and a postfix and left each module to guess
which it was; a reader (a Seq, a Poly, a Mix, a lit Atn) is the only thing that takes
what stands to its left as its content. A cable in a module's own jack is its
own business and takes it out of nothing, but for an Atn's IN, which takes it out of the window (`M_VCA`).

Adding a trait to this list is a design decision and not a way to make a build
pass: a new slot must configure its cartridge rather than feed it. */
constexpr uint32_t CARTRIDGE_MAY_FIND =
    tree::KIND_BITS     // the peer: my own kind, which makes a block;
                        // and the kin, a CV's or a CRV's own kind at
                        // any distance, and an Atn's lane, a CV: their
                        // windows, never their volts
    | tree::T_SHAPER  // the window my travel is read in
    | tree::T_BUNDLE; // a Poly whose cable sets me, as a cable in my own jack would
// T_VALUE, T_RESTART and T_READER are not on it: no cartridge takes a
// threshold or restarts a chain.
constexpr bool grammarHolds(int id = 0, int slot = 0) {
	return id >= M_COUNT ? true
	       : !(SPECS[id].traits & tree::T_CARTRIDGE) || slot >= SPECS[id].nslots
	           ? grammarHolds(id + 1, 0)
	           : (SPECS[id].slots[slot].trait & ~CARTRIDGE_MAY_FIND) == 0 && grammarHolds(id, slot + 1);
}
static_assert(grammarHolds(), "a cartridge looks for something it would transform rather than be configured by: "
                              "a cartridge modifies what stands to its right and never takes its left as input");

/** The top-down rule, the half of it a table can hold. A statement travels from a parent to a child and on down only along a
claim, the edge that makes a block: a slot that does not claim may tell what
it finds, but may not pass on what its own node was told, or a statement could
leave the block it was made for. The other half, that no module can read who
holds it, is the compiler's: the in-edges are private to the tree
(`HolderProbe`, Member.hpp). */
constexpr bool relaysAlongClaims(int id = 0, int slot = 0) {
	return id >= M_COUNT ? true
	       : slot >= SPECS[id].nslots
	           ? relaysAlongClaims(id + 1, 0)
	           : (SPECS[id].slots[slot].relays == 0 || SPECS[id].slots[slot].claims) &&
	                 (SPECS[id].slots[slot].relays & ~tree::TOLD_BLOCK) == 0 && relaysAlongClaims(id, slot + 1);
}
static_assert(relaysAlongClaims(), "a slot passes a statement on down an edge that is not a claim, or passes on "
                                   "more than who reads the block: a statement travels top down, along claims only");

/** The faces, the half a table can hold: a Seq walks cartridges and lit
Atns (`SEQ_WALKS`), so every row it walks says what walking it gives and
nothing else does; and a gatherer is itself gathered, for submixes. */
constexpr bool facesHold(int id = 0) {
	return id >= M_COUNT ? true
	       : ((SPECS[id].traits & SEQ_WALKS) ? SPECS[id].walked != F_NONE : SPECS[id].walked == F_NONE) &&
	             (!(SPECS[id].reads == READ_POLY || SPECS[id].reads == READ_MIX) || SPECS[id].gives != F_NONE) &&
	             facesHold(id + 1);
}
static_assert(facesHold(), "a row's faces: walked on every cartridge and no other row, and a gatherer gatherable");

/** Two names the same, for a static_assert. */
constexpr bool sameName(const char* a, const char* b) {
	return *a == *b && (*a == '\0' || sameName(a + 1, b + 1));
}
/** Where the slot called `name` sits in a row's declarations, or minus one:
the check that a module's slot enum still names what it indexes. */
constexpr int slotIndex(Id id, const char* name, int i = 0) {
	return i >= SPECS[id].nslots ? -1
	                             : (sameName(SPECS[id].slots[i].name, name) ? i
	                                                                       : slotIndex(id, name, i + 1));
}

/** The row's slots as the tree asks for them, built once from the table. */
const std::vector<SlotDecl>& decls(Id id);

} // namespace manifest
} // namespace rig

/** What a module writes instead of its traits and its slots: the row of the
manifest it is. Nothing else in the module says what it plays or wants. */
#define ROW_MANIFEST(ID)                                                                  \
	const std::vector<rig::tree::SlotDecl>& declarations() const override {                \
		return rig::manifest::decls(rig::manifest::ID);                                    \
	}                                                                                      \
	uint32_t announce() override { return rig::manifest::spec(rig::manifest::ID).traits; }  \
	int manifestId() const override { return (int)rig::manifest::ID; }                     \
	MEMBER_FOOTPRINT

/** A slot enum checked against the manifest at compile time: the name has to
sit at that index, so a slot inserted above it stops the build here rather
than quietly re-pointing every use of the index. */
#define ROW_SLOT_IS(ID, INDEX, NAME)                                                      \
	static_assert(rig::manifest::slotIndex(rig::manifest::ID, NAME) == (INDEX),            \
	              "slot enum out of step with the manifest: " NAME)
