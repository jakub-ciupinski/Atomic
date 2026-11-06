// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Member.hpp"
#include "Arc.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

/** The cartridge: a member of its kind's block (a CV, a Qnt, an Atn, a
Trg or a CRV) with the peer chain, the evaluate template (prepare, act, shape,
publishJack), who reads its block, the Poly's feed, the preview hold, and
the published half its screens read. */

namespace rig {

/** The most playheads one block registers (`addLane`): each a lane with
its colour, its light and its halo. */
constexpr int MAX_LANES = 7;

/** What the Seqs walking a member route into it (`MemberFace::route`), kept a
lane each, so every Seq's word stands until that Seq changes it: two Seqs
walking one block of Qnts or Atns, one with a cable in its IN and one
without, wrote one shared switch, and whichever woke last won it, so the
gated rig (only the first Seq woken) and the ungated one (both, the second
last) parted. What the member plays is the
sum of the lanes routing it, added in lane order so it never depends on
which Seq woke first, as many voices as the widest, a mono lane held across
the others' voices as an Atn holds a mono route; one lane alone is that
lane's volts exactly. Cleared at every parse, where the lanes are dealt
again. */
struct Routed {
	uint8_t on = 0; // a bit per lane routing me
	int laneN[MAX_LANES] = {};
	float laneV[MAX_LANES][PORT_MAX_CHANNELS] = {};
	int n = 1;                     // the sum's voices
	float v[PORT_MAX_CHANNELS] = {}; // the sum
	bool any() const { return on != 0; }
	/** Lane `lane`'s word, `to` routing `k` voices of `volts` or not;
	whether the sum changed. A Seq past the last lane routes nothing, as it
	stands on nothing it could light. */
	bool take(int lane, bool to, const float* volts, int k) {
		if (lane < 0 || lane >= MAX_LANES)
			return false;
		k = clamp(k, 1, PORT_MAX_CHANNELS);
		const uint8_t bit = (uint8_t)(1u << lane);
		if (to == ((on & bit) != 0) && (!to || (k == laneN[lane] && std::equal(volts, volts + k, laneV[lane]))))
			return false;
		on = to ? (uint8_t)(on | bit) : (uint8_t)(on & ~bit);
		laneN[lane] = k;
		std::copy(volts, volts + k, laneV[lane]);
		n = 1;
		for (int l = 0; l < MAX_LANES; l++)
			if (on & (1u << l))
				n = std::max(n, laneN[l]);
		bool first = true;
		for (int l = 0; l < MAX_LANES; l++) {
			if (!(on & (1u << l)))
				continue;
			for (int c = 0; c < n; c++) {
				const float x = laneN[l] == 1 ? laneV[l][0] : c < laneN[l] ? laneV[l][c] : 0.f;
				v[c] = first ? x : v[c] + x;
			}
			first = false;
		}
		return true;
	}
	void clear() { on = 0; }
};

/** A cartridge: a member of its kind's block, the peer chain; acting when
nobody has claimed it, which is shaping the window of what stands to its right
if it is a shaper, and whatever its own jack does with its own cable. It never
takes anything arriving from its left as its input: the rig carries no signal.
Subclasses prepare, shape, act and publish their jack. */
struct Cartridge : Member, MemberFace {
	manifest::Id row; // my row of the manifest, which an Atn in OUT changes at the parse
	float out = 0.f;     // what it makes of nothing arriving, for its jack
	// For the screen, written once per sample so a draw never sees halves.
	// Both of these are read by screens, so they follow the three-pass rule:
	// worked out in the wake as plain fields, handed over in publishReads.
	// Stored as they were computed, they would run a pass ahead of the colour
	// and the lanes a screen reads beside them, and a frame drawn in between
	// would see a module un-dim before the playhead that un-dimmed it.
	bool actingWork = true;
	std::atomic<bool> actingNow{true};
	int shownColorWork = -1; // the same, as this wake's publish worked it out
	std::atomic<int> shownColorNow{-1};
	std::atomic<uint32_t> readersPacked{0}; // count in the low 4 bits, then 4 bits per colour
	// The playhead lanes over my whole block: how many Seqs walk it, which
	// colour each lane wears, and which lanes are standing on me right now.
	// Count in the low 4 bits, then a lit bit per lane from bit 4, then a
	// 4-bit colour per lane from bit 11. Stable across the block, so every
	// member of it divides its screen the same way.
	std::atomic<uint64_t> lanesPacked{0};
	// And how strongly each lane's playhead stands here, where a Seq in Fade
	// weighs two members of the block (`weigh`): nine bits a lane from bit
	// 9 x lane, how far it is dimmed in 255ths, nought full, and whether its
	// halo is dimmed with its line. Nought, every lane full, is every
	// playhead undimmed. Two words, so published with the lanes
	// under a generation counter, odd while being written, whenever either
	// this wake's or the last's is not nought (`packLanes`).
	std::atomic<uint64_t> weightsPacked{0};
	std::atomic<uint32_t> lanesGen{0};
	uint64_t weightsWas = 0;
	uint64_t lanesSeen = 0, weightsSeen = 0; // the UI's last whole copy
	// The cable in the cartridge's own jack, read every sample so an
	// audio-rate signal gets through; what a lone member makes of it is
	// the member's business.
	JackWatch cv;
	/** Whether I carry a signal of my own as a Seq's step, or only my
	setting: a cable in my jack, or what an Atn's lit arrow takes. What a
	Seq's OUT says it gives over Atns (`Seq::atnCarry`). */
	virtual bool carriesSignal() { return cv.live(); }
	// The mouse held on the screen, for a Seq's preview.
	std::atomic<bool> touched{false};
	/** A hand on one of my screens, from the UI: held for a Seq's preview,
	and the rig woken to hear it. */
	void touch() {
		touched.store(true, std::memory_order_relaxed);
		wake();
	}
	// The pointer resting on a Qnt, for a Seq's preview alone: a Qnt
	// previews on hover, but a hover is not a hand on it, so a Seq in
	// ONCE, which hears a hand alone, does not sound it.
	std::atomic<bool> hovered{false};
	void hover() {
		hovered.store(true, std::memory_order_relaxed);
		wake();
	}
	int64_t holdUntil = -1; // frame until which the hold lasts
	int64_t hoverUntil = -1; // and the hover

	// The window and the Poly slots, found by name once: a Trg has no window,
	// so the index is the kind's and not the family's, and minus one where
	// the kind has none.
	int sWindow = -1, sPoly = -1;
	explicit Cartridge(manifest::Id id) : row(id) {
		sWindow = slotNamed("window");
		sPoly = slotNamed("poly");
	}
	/** Whether `poly` is the Poly that feeds me: the nearest to my left. */
	bool fedBy(const Member* poly) { return sPoly >= 0 && at(sPoly) == poly; }

	/** A press on a screen holds for a tenth of a second, and the moment it
	ends is a change like any other: the only thing here the rig cannot see
	as a jack or a param, so it is the only thing the hold subscribes for.
	The press itself arrives as a poke from the widget that wrote it. */
	bool wantsTick() override { return holdUntil >= 0 || hoverUntil >= 0; }
	bool everySample(const ProcessArgs& args) override {
		bool ended = false;
		if (holdUntil >= 0 && args.frame >= holdUntil) {
			holdUntil = -1;
			ended = true;
		}
		if (hoverUntil >= 0 && args.frame >= hoverUntil) {
			hoverUntil = -1;
			ended = true;
		}
		return ended;
	}
	/** The readers' colours, unpacked for a draw. Returns how many. */
	int readers(int* cols) {
		uint32_t p = relaxed(readersPacked);
		int n = p & 0xf;
		for (int i = 0; i < n; i++)
			cols[i] = (p >> (4 + 4 * i)) & 0xf;
		return n;
	}
	/** The playheads to draw on this member, line and halo apart: every
	lane lit here or anchored here, in lane order, with whether its line
	stands here (the step playing) and whether its halo does (the walk's own
	step, before a phase turn). With no turn the two are the same lanes, both
	on. Returns how many. */
	int playheadLanes(int* cols, bool* line, bool* halo, float* lineK = nullptr, float* haloK = nullptr) {
		// A whole copy of the lanes and their weights, or the last one.
		for (int attempt = 0; attempt < 4; attempt++) {
			const uint32_t g0 = lanesGen.load(std::memory_order_acquire);
			if (g0 & 1)
				continue;
			const uint64_t lp = relaxed(lanesPacked), wp = relaxed(weightsPacked);
			std::atomic_thread_fence(std::memory_order_acquire);
			if (relaxed(lanesGen) == g0) {
				lanesSeen = lp;
				weightsSeen = wp;
				break;
			}
		}
		const uint64_t p = lanesSeen, wts = weightsSeen;
		const int n = (int)(p & 0xf);
		int m = 0;
		for (int i = 0; i < n; i++) {
			const bool l = (p >> (4 + i)) & 1, h = (p >> (39 + i)) & 1;
			if (!l && !h)
				continue;
			cols[m] = (int)((p >> (11 + 4 * i)) & 0xf);
			line[m] = l;
			halo[m] = h;
			// The strength: full, exactly one, where nothing dims it.
			const int dim = (int)((wts >> (9 * i)) & 0xff);
			const float k = dim == 0 ? 1.f : (float)(255 - dim) / 255.f;
			if (lineK)
				lineK[m] = k;
			if (haloK)
				haloK[m] = (wts >> (9 * i + 8)) & 1 ? k : 1.f;
			m++;
		}
		return m;
	}
	// ---- The graph ---------------------------------------------------------
	// Two slots every kind but the Trg has: the peer of my own kind, claimed,
	// a block being a chain of them at distance one; and the shaper my window
	// arrives through. A member claimed by its peer or by a playhead stands
	// down, passing the window through; the rightmost of an unwalked block
	// acts. The peer is the first slot of every kind; the window is found by
	// name, because a Trg has none.
	enum { S_PEER = 0 };
	ROW_SLOT_IS(M_QNT, S_PEER, "peer");
	ROW_SLOT_IS(M_CV, S_PEER, "peer");
	ROW_SLOT_IS(M_ATN, S_PEER, "peer");
	ROW_SLOT_IS(M_TRG, S_PEER, "peer");
	ROW_SLOT_IS(M_CRV, S_PEER, "peer");
	ROW_SLOT_IS(M_OUT, S_PEER, "peer"); // a lit Atn: its peer makes it one of a block a Seq walks
	ROW_SLOT_IS(M_VCA, S_PEER, "peer");
	Mapping winIn, winOut;
	int nlanes = 0;      // playheads walking my block, registered at build
	int laneColor[MAX_LANES] = {};
	bool laneLit[MAX_LANES] = {};
	bool laneBase[MAX_LANES] = {}; // the walk's own step before a phase turn, for the halo
	uint8_t laneDim[MAX_LANES] = {}; // a fade's weight, how far dimmed in 255ths, nought full (`weigh`)
	bool laneDimHalo[MAX_LANES] = {}; // and the halo dimmed with the line
	Routed routed; // what the Seqs walking me route into me, a lane each
	// What this wake makes of the screens, kept plain until it is published.
	// Two colours and not one: `colorWork` is what the cartridge made of the
	// signal reaching it, which is its own and survives a wake it sits out,
	// and `colorRead` is the colour of whoever read it, which is a reader's
	// and is dropped at the top of every wake for the readers to write
	// afresh. The gate is why they are two: a cartridge the gate leaves out
	// does not run to write its own back.
	uint32_t readersWork = 0;
	int colorWork = -1;
	int colorRead = -1;
	/** The colour a member shows with nobody reading it and nothing of the
	rig's passing through it: the unread yellow for most, and a Qnt's own,
	one a Qnt counted along the rig, so quantizers standing side by side
	say at a glance that they are not one thing. */
	virtual int restColor() const { return -1; }
	bool heldNow = false; // the mouse is held on me, for a preview
	bool hoveredNow = false; // the pointer rests on me, a Qnt, for a preview alone

	const std::vector<tree::SlotDecl>& declarations() const override { return manifest::decls(row); }
	uint32_t announce() override { return manifest::spec(row).traits; }
	int manifestId() const override { return (int)row; }

	/** The peer touching me on the left, or null: the block is the chain of
	these, the slot reaching no farther than one. */
	Cartridge* peer() { return static_cast<Cartridge*>(at(S_PEER)); }
	/** The block a reader walks, from its rightmost member `right` along the
	peer chain, at most `most` of it, leftmost first. */
	static void blockFrom(Member* right, int most, std::vector<Cartridge*>& out) {
		out.clear();
		for (Cartridge* c = static_cast<Cartridge*>(right); c && (int)out.size() < most; c = c->peer())
			out.push_back(c);
		std::reverse(out.begin(), out.end());
	}
	/** Who uses my window or my value, as they have said (`usedBy`), kept
	by who they are; before any has spoken since the parse, the parse's word
	that something reads my window or takes my value. */
	struct User {
		const void* by;
		bool on;
	};
	static constexpr int MAX_USERS = 16;
	User users[MAX_USERS];
	int nusers = 0;
	void usedBy(const void* by, bool on) override {
		int i = 0;
		while (i < nusers && users[i].by != by)
			i++;
		if (i < nusers && users[i].on == on)
			return;
		if (i == nusers) {
			if (nusers == MAX_USERS)
				return;
			nusers++;
		}
		users[i] = {by, on};
		written();
		poke.store(true, std::memory_order_relaxed);
	}
	bool usedNow() const {
		if (nusers == 0)
			return (told() & (tree::TOLD_WINDOW | tree::TOLD_BLOCK)) != 0;
		for (int i = 0; i < nusers; i++)
			if (users[i].on)
				return true;
		return false;
	}
	int useTold = -1; // what I last told my own window's child, as a holder
	void settle() override {
		nusers = 0;
		useTold = -1;
		nlanes = 0;
		routed.clear();
		// Whoever fed me is settling too, and feeds me again if still there.
		feedSeen = false;
		unfeed();
	}
	/** A Poly with a cable in IN sets the members to its right from that
	cable, the mirror of `bundle`: as many channels as the member would
	bundle, taken in rig order, each set as though it had arrived in the
	member's own jack. Returns how many were taken. The Poly stands to my left
	and runs first, and I read it through my poly slot, so a change in its
	cable wakes me in the same pass and nothing needs poking. The default is
	the one jack most members have; a Trg takes its gates. An Atn takes a
	channel counted among the Atns alone, and shapes the windows of the
	members after it besides, so a channel fed to a CV after it is scaled as
	its own jack would be. Pulled, not pushed: at the top of my evaluate I ask
	the Poly for its channels (`feedOut`) and take mine, at the offset the
	members it feeds before me add up to (`fedBefore`, found at the parse,
	their widths read live since a Trg's rings can change without one).
	Nothing writes into a node to its right. */
	virtual int feed(const float* v, int n) {
		if (n < 1)
			return 0;
		fedJack().feed(v[0]);
		return 1;
	}
	virtual void unfeed() { fedJack().unfeed(); }
	/** Whether a Poly feeds my jack, as the last wake left it: what a screen
	or a tooltip reads, never the jack's own working `fed`, which the audio
	thread writes as it pulls the feed. */
	std::atomic<bool> fedNow{false};
	std::atomic<float> fedVNow{0.f};
	/** A cable in my jack or a Poly feeding it, for the screens: the cable
	is Rack's port, the feed the published one. */
	bool jackLiveNow() { return relaxed(fedNow) || fedJack().plugged(); }
	/** And the voltage my jack holds, the same way (`JackWatch::get`). */
	float jackNow() {
		JackWatch& j = fedJack();
		return j.plugged() ? j.port->getVoltage() : relaxed(fedNow) ? relaxed(fedVNow) : 0.f;
	}
	/** The one jack a Poly's channel stands in for. */
	virtual JackWatch& fedJack() { return cv; }
	/** How many of a Poly's channels I take: the one jack, or a Trg's rings. */
	virtual int feedWidth() { return 1; }
	/** The param that says a hand has set my Poly's channel aside, a CV's
	OWN, minus one where nothing can be: a new cable in my Poly's IN puts it
	back to nought (`PolyWidget::giveBack`). */
	virtual int setAsideParam() { return -1; }
	/** A Poly offers me channels this wake, whether I follow them or set
	them aside by hand (`pullFeed`), and the same as the last wake published
	it, for the screens. */
	bool feedSeen = false;
	std::atomic<bool> feedSeenNow{false};
	bool offeredNow() { return relaxed(feedSeenNow); }
	/** Fed by a Poly and following it, for a screen: the feed published and
	no cable of my own over it; a member set aside by hand never is, its feed
	being refused. A Trg says so of its rings. */
	virtual bool followingNow() { return relaxed(fedNow) && !fedJack().plugged(); }
	std::vector<Cartridge*> fedBefore; // the members my Poly feeds before me, in rig order
	int feedAt = -1; // the first channel of the Poly I took this wake, minus one for none
	bool feederCabled = false; // my Poly has a cable in IN this wake, whether or not it reached me
	void pullFeed() {
		takeFeed();
		letGoOfHand();
	}
	/** A member set aside by hand (`setAsideParam`) whose channels go, its
	Poly's cable pulled, its channels fewer than it reaches or the Poly gone,
	lets go of the hand: the param goes back to nought, posted (`postWrite`)
	as the feed's own let-go is, so the channels returning are followed again
	and nothing remembers a feed that is not there. Only once the member has
	been seen set aside over a live feed, so a patch loading, its cables
	arriving a wake after its modules, keeps the hand. The values it plays
	stay as the hand left them. */
	bool handOverFeed = false;
	void letGoOfHand() {
		const int own = setAsideParam();
		const bool setAside = own >= 0 && params[own].getValue() > 0.5f;
		if (setAside && feedSeen)
			handOverFeed = true;
		else if (setAside && handOverFeed) {
			handOverFeed = false;
			postWrite(own, 0.f, "lose the feed", feederCabled);
		}
		else
			handOverFeed = false;
	}
	/** A cable let go leaves its param where it held it: the travel it last
	gave is written into the param, one undo, as the engine writes anything
	(`postWrite`), and read in the param's place until the write lands. The
	param moving off where it stood at the letting go, the write landing or a
	hand, ends that. A Poly's feed let go, its cable pulled, the Poly gone or
	my channel gone from it, is a cable of mine let go too, as though the Poly
	were sixteen unseen cables, so the param keeps what the channel last gave;
	while the feed is still there my hand set it aside, and the stroke stands.
	A CV's fader and a CRV's start keep what their cable left alike. */
	struct CableLeft {
		bool wasLive = false, wasFedOnly = false;
		float liveTravel = 0.5f, letGo = -1.f, letGoFrom = 0.f;
		/** The travel the param is owed, read in its place until it lands. */
		bool owed() const { return letGo >= 0.f; }
		/** The cable holds the param, or what it left is owed: no glide. */
		bool holding() const { return wasLive || owed(); }
	};
	void keepWhatTheCableLeft(CableLeft& k, int param, bool live) {
		const float p = params[param].getValue();
		if (live) {
			k.wasLive = true;
			k.wasFedOnly = !cv.plugged();
			k.letGo = -1.f;
			return;
		}
		if (k.wasLive && k.wasFedOnly && feedSeen) {
			k.wasLive = false;
			k.letGo = -1.f;
		}
		else if (k.wasLive) {
			k.wasLive = false;
			k.letGo = k.liveTravel;
			k.letGoFrom = p;
			// A feed whose Poly still has its cable lost my channel: the
			// patch performing, not a hand pulling a cable.
			const bool performed = k.wasFedOnly && feederCabled;
			postWrite(param, k.letGo, k.wasFedOnly ? "lose the feed" : "keep the value", performed);
		}
		else if (k.letGo >= 0.f && p != k.letGoFrom)
			k.letGo = -1.f;
	}
	void takeFeed() {
		if (sPoly < 0)
			return;
		Member* p = at(sPoly);
		const float* v = nullptr;
		// The cable alone, offered to the CVs and Trgs, which count among
		// themselves; a Poly's channels are taken by no Atn but the OUT
		// strips after it, readers.
		const int n = p ? p->feedOut(v) : -1;
		const bool cabled = n >= 0;
		int k = 0;
		for (Cartridge* c : fedBefore)
			k += c->feedWidth();
		feedAt = -1;
		feederCabled = cabled;
		feedSeen = n >= 0 && k < n && feedWidth() > 0;
		if (feedSeen) {
			feedAt = k;
			feed(v + k, n - k);
		}
		else
			unfeed();
	}
	const Mapping* windowOut() override { return (traits & tree::T_SHAPER) ? &winOut : nullptr; }
	/** Whose window I take: my window slot's, the nearest shaper to my left,
	whatever it serves. An Atn replaces what the Atn before it left
	(`shapeAtn`) and the window passes through a Qnt, so there is nothing to
	look past. */
	Member* windowChild() override { return at(sWindow); }

	// ---- Who reads my block: what the block was told. A reader's slot tells
	// the rightmost member what it is to the reader (a Seq's steps), a Poly
	// or a Mix tells every member it gathers, and the peer slot passes its
	// walkers on down the block (`tree::TOLD_RELAYED`).
	/** Whether anything reads my block at all, a Poly included. */
	bool readByAny() const { return (told() & tree::TOLD_BLOCK) != 0; }
	void parsed(const tree::Left& left, int pass) override {
		if (pass == 0) {
			fedBefore.clear();
			if (Member* p = at(sPoly))
				for (int i = p->place + 1; i < place; i++) {
					Cartridge* c = dynamic_cast<Cartridge*>(left[i]);
					if (c && c->fedBy(p))
						fedBefore.push_back(c);
				}
		}
	}
	/** The window arriving: my window source's lane, or plain. */
	const Mapping& windowArriving() {
		static const Mapping plain;
		Member* c = windowSource();
		const Mapping* m = c ? c->laneOut() : nullptr;
		return m ? *m : plain;
	}
	bool holding() override { return heldNow; }
	bool previewing() override { return heldNow || hoveredNow; }
	int addLane(int col, bool across) override {
		if (nlanes >= MAX_LANES)
			return -1;
		laneColor[nlanes] = col;
		laneLit[nlanes] = false;
		laneBase[nlanes] = false;
		return nlanes++;
	}
	/** Whether a playhead is standing on me this wake: a lane of mine is
	lit. A claimed member is not acting on the rig, but the one a playhead
	stands on is the one the rig is hearing, so it shows live. */
	bool underPlayhead() const {
		return ((relaxed(lanesPacked) >> 4) & 0x7f) != 0;
	}
	/** What every screen of a member asks before it dims itself: acting on
	the rig, or standing under a playhead. One rule, one place. */
	bool liveNow() const { return relaxed(actingNow) || underPlayhead(); }
	void packLanes() {
		uint64_t lanes = 0, weights = 0;
		for (int i = 0; i < nlanes; i++) {
			lanes |= (uint64_t)clamp(laneColor[i], 0, MAX_COLORS - 1) << (11 + 4 * i);
			if (laneLit[i])
				lanes |= (uint64_t)1 << (4 + i);
			if (laneBase[i])
				lanes |= (uint64_t)1 << (39 + i);
			weights |= ((uint64_t)laneDim[i] | (laneDimHalo[i] ? 0x100u : 0u)) << (9 * i);
		}
		lanes |= (uint64_t)nlanes;
		// Nothing weighed now or last time: the lanes alone, one word.
		if (weights == 0 && weightsWas == 0) {
			lanesPacked.store(lanes, std::memory_order_relaxed);
			return;
		}
		lanesGen.fetch_add(1, std::memory_order_release); // odd: writing
		std::atomic_thread_fence(std::memory_order_release);
		lanesPacked.store(lanes, std::memory_order_relaxed);
		weightsPacked.store(weights, std::memory_order_relaxed);
		lanesGen.fetch_add(1, std::memory_order_release); // even: whole
		weightsWas = weights;
	}
	/** A Seq in Fade weighing its playhead on me (`Seq::soundStep`): lane
	`lane` stands here at strength `k`, nought to one, its halo too where
	`halo`. Unweighed, a lane is full. */
	void weigh(int lane, float k, bool halo) {
		written();
		if (lane < 0 || lane >= nlanes)
			return;
		laneDim[lane] = (uint8_t)(255 - (int)std::lround(clamp(k, 0.f, 1.f) * 255.f));
		laneDimHalo[lane] = halo;
	}
	void clearReads() override {
		for (int i = 0; i < nlanes; i++) {
			laneLit[i] = laneBase[i] = false;
			laneDim[i] = 0;
			laneDimHalo[i] = false;
		}
		readersWork = 0;
		colorRead = -1;
	}
	/** Everything a screen of mine reads, handed over at once. See
	`publishReads` in Tree.hpp for why it is not done as it is worked out. */
	void publishReads() override {
		packLanes();
		readersPacked.store(readersWork, std::memory_order_relaxed);
		shownColorWork = colorRead >= 0 ? colorRead : colorWork >= 0 ? colorWork : restColor();
		shownColorNow.store(shownColorWork, std::memory_order_relaxed);
		actingNow.store(actingWork, std::memory_order_relaxed);
		fedNow.store(fedJack().fed, std::memory_order_relaxed);
		fedVNow.store(fedJack().fedV, std::memory_order_relaxed);
		feedSeenNow.store(feedSeen, std::memory_order_relaxed);
	}
	/** A reader's colour on me: the leftmost, the first to write, owns it. */
	void readBy(int col) override {
		written();
		uint32_t p = readersWork;
		int n = p & 0xf;
		if (n < 7)
			p |= (uint32_t)clamp(col, 0, MAX_COLORS - 1) << (4 + 4 * n++);
		readersWork = (p & ~0xfu) | (uint32_t)n;
		if (colorRead < 0)
			colorRead = col;
	}
	void read(int lane, int col, int value) override {
		readBy(col);
		if (lane >= 0 && lane < nlanes)
			laneLit[lane] = true;
	}
	void anchor(int lane, int sub) override {
		written();
		if (lane >= 0 && lane < nlanes)
			laneBase[lane] = true;
	}
	const tree::Node* quantOf() override { return winIn.quantNode; }

	/** The subclass's state from the window arriving: a Qnt's scale, a CV's
	map. */
	virtual void prepare(const Mapping& win, const ProcessArgs& args) {}
	/** The window transform of a shaper; nothing for a source. */
	virtual void shape(Mapping& m) {}
	/** The same on a fresh window, as though nothing stood to my left: an
	Atn's range laid again by a keyboard of the same lane. */
	virtual void shapeAfresh(Mapping& m) { shape(m); }
	/** What I make of `s` as a reader's step, or for a source the value put
	on it, returning what the jack shows, in semitones. The only signal a
	member is ever handed is a reader's cable, through `sound`; on its own
	wake it is handed none, and a transforming member returns what nought
	would become. */
	virtual float act(Signal& s, int color, int* value) { return 0.f; }
	/** The jack, from `out` and whatever the subclass keeps. */
	virtual void publishJack() {}

	int channelsOut(float* v) override { return bundle(v); }
	/** Whether a reader has claimed the block I stand in: a Seq took me, or took a peer whose chain runs back through me. Standing in a
	block claims every member but the last for the peer chain whether or not
	anything reads the block, and that is the difference this asks about. */
	bool claimedByReader() const { return tree::walked(told()); }

	void evaluate(const ProcessArgs& args) override {
		pullFeed();
		winIn = windowArriving();
		prepare(winIn, args);
		// A press on the screen holds for a moment, for a Seq's preview.
		if (touched.exchange(false))
			holdUntil = args.frame + (int64_t)(0.1f * args.sampleRate);
		heldNow = holdUntil >= 0 && args.frame < holdUntil;
		if (!heldNow)
			holdUntil = -1;
		if (hovered.exchange(false))
			hoverUntil = args.frame + (int64_t)(0.1f * args.sampleRate);
		hoveredNow = hoverUntil >= 0 && args.frame < hoverUntil;
		if (!hoveredNow)
			hoverUntil = -1;
		// What I make of nothing arriving, for my jack; and, acting, the
		// window I hand to my right.
		const bool acting = acts();
		Signal none;
		int value = -1;
		out = act(none, -1, &value);
		winOut = winIn;
		if (acting && shapes())
			shape(winOut);
		actingWork = acting;
		colorWork = -1;
		publishJack();
	}
	/** Whether an acting member shapes the window at all this wake: every one
	does, whatever is in its jacks, since a cable never takes a module out of
	the rig. Kept as the one place a member could say
	otherwise, which only a statement from what holds it may justify. */
	virtual bool shapes() { return true; }
	/** Whether I act, my jack my own and the window mine to shape: unclaimed,
	or claimed only by what says its claim leaves me acting (an Atn beside a
	VCA, `Atn::acts`). */
	virtual bool acts() { return !claimed(); }
	/** The one rule for a Qnt with a cable in its own jack: the jack is what
	the member makes of the cable, and the screen lights in the first colour.
	A cable is an act, so a Qnt claimed by its block or walked by a Seq still
	quantizes its own cable, never sitting deaf with a cable in it. */
	bool ownCable() {
		if (!cv.live())
			return false;
		colorWork = std::max(restColor(), 0);
		actingWork = true;
		return true;
	}
	/** A reader's step. A transforming member is handed the reader's cable
	as `sig`, or sounds its own cable where the reader has none: a Seq over
	a block of Atns each fed a signal is a sequential switch, the step being
	that Atn's cable at that Atn's top. A source sets itself from its cable
	in `act` already. */
	void sound(int sub, const Signal& sig, int col, Sound& o) override {
		o.window = winIn;
		shape(o.window);
		// A quantizer a reader walks hands on the reader's colour with its
		// scale, for the boxes of the steps it quantizes.
		if (traits & tree::T_QNT)
			o.window.quantColor = col;
		o.signal = sig;
		if (!o.signal.present && cv.live() && !(traits & tree::T_SOURCE))
			o.signal.setVolts(cv.get(), col, Mapping());
		o.semis = act(o.signal, col, &o.value);
		// A Qnt handed a signal sounds the note it snapped the signal to,
		// which `quantStage` made an integer and returned as a float, so the
		// round is exact; never its own note, which under a cable in ROUTE
		// would light C whatever played.
		o.note = (traits & tree::T_QNT) && o.signal.present ? noteActed(o.semis) : litSemis(sub);
		o.band = litBand(sub);
		if (o.band != NO_BAND)
			o.volts = o.signal.present ? o.signal.voltsNow() : o.semis / 12.f;
		if (o.signal.present)
			o.signal.color = col;
	}
	/** The whole semitone `act` snapped to, which a quantizer detuned by
	cents cannot give back by rounding what it sounds. */
	virtual int noteActed(float semis) { return (int)std::round(semis); }
	int bundle(float* v) override {
		v[0] = out / 12.f;
		return 1;
	}
	int litSemis(int sub) override { return (int)std::round(out); }
};

} // namespace rig
