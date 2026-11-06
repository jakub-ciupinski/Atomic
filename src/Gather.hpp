// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Probe.hpp"
#include "Member.hpp"
#include "Stack.hpp"
#include <algorithm>
#include <vector>

/** What a Poly or a Mix gathers to its left: the two readers share one pool.

The kind is that of the nearest module to the gatherer's left that can be
gathered and that no gatherer to its left has taken already; the members are
every module of that kind to its left, at any distance, that none has taken,
the sixteen nearest (a Mix's fifteen), in rig order. Who takes first is the
parse's order, left to right: a gatherer further left has told its members
they are taken (`TOLD_BUNDLED`) before the next one looks. So POLY and MIX
share a rig out between them as two Polys do: SEQ SEQ CV CV MIX POLY sums the
CVs and bundles the Seqs. And a gatherer is itself gathered, for a submix,
once nothing else to its left is free: a Poly or a Mix takes the sources
first, and only when every one to its left is taken does it take the nearest
kind of gatherer, Polys combining their channels and Mixes their sums, so CV
CV MIX CV CV MIX POLY bundles the two sums and CV CV POLY CV CV CV POLY POLY
combines the two bundles, while SEQ SEQ ATN POLY POLY still shares the rig
out. A Mix never sums a gatherer that gathers nothing, no member and no cable
(`gathersNothing`): a sum of nothing adds nothing, and taking it would only
take its channel from the Poly to its right, so MIX MIX MIX POLY bundles three
Mixes, each a place for what will come into it. A
Poly takes an empty Mix as it takes a full one. */

namespace rig {

const int GATHER_MAX = 16;

/** A gatherer is what the manifest's `reads` column says bundles or sums,
so a new one declares it in its row and nowhere else. */
inline bool gatherer(int id) {
	if (id < 0 || id >= manifest::M_COUNT)
		return false;
	const Reading r = manifest::spec((manifest::Id)id).reads;
	return r == READ_POLY || r == READ_MIX;
}
/** The kinds a Poly bundles and a Mix sums are the manifest's faces
(`gives`, `walked`): a Seq, a CV, an Atn with a cable in IN, a CRV, an Atn
lit or cabled (one kind, the lit arrow standing for a cable), and a
Mix, a channel each, and a Poly, all its
channels one after the other, so Polys and Mixes gather each other for
submixes, a Poly of Polys combining and a Mix of them summing every channel.
Not an Atn carrying no signal, its arrow unlit and IN empty, which shapes
and gives nothing worth gathering, so a gatherer passes it as though it were
not there, parting no group (a configurator has nothing to mix);
not a Qnt, a chord already, nor a Clock, whose jack is a clock and not a
voice, nor a Trg, since gathering Trgs makes no sense, even for
triggers in sequence. A Seq wears what it walks: a Seq walking Trgs carries triggers and one walking
anything else carries volts, two kinds to a gatherer, so a Poly never
interleaves a voice's pitch and its gates in one jack. A Seq walking Qnts
carries chords and is a Seq like any other, so a melody and a chord
progression share one Poly rather than chords being a kind of their own.

The kind a gatherer reads off a module to its left is its face, minus one
for none. */
inline int faceOf(tree::Node* m) {
	return static_cast<Member*>(m)->face();
}
/** The words for a face: `word` the caption under the jack (OUT for none),
`plural` the tooltip's, `words` the strip's, the
plural's s in lower case since CRVS read as CRV5 from across the room,
and `name` a sentence's. */
struct FaceWords {
	int face;
	const char* word;
	const char* plural;
	const char* words;
	const char* name;
};
inline const FaceWords& faceWords(int f) {
	static const FaceWords NONE = {manifest::F_NONE, "OUT", "", "NONE", "nothing"};
	static const FaceWords ROWS[] = {
	    {manifest::F_SEQ, "SEQ", "SEQs' jacks", "SEQs", "SEQs"},
	    {manifest::F_TRIG, "TRIG", "SEQs' triggers", "TRIGs", "SEQs of triggers"},
	    {manifest::F_CV, "CV", "CVs' volts", "CVs", "CVs"},
	    {manifest::F_ATN, "ATN", "ATNs' outputs", "ATNs", "ATNs"},
	    {manifest::F_CRV, "CRV", "CRVs' curves", "CRVs", "CRVs"},
	    {manifest::F_MIX, "MIX", "MIXes' sums", "MIXes", "MIXes"},
	    {manifest::F_POLY, "POLY", "POLYs' channels", "POLYs", "POLYs"},
	};
	for (const FaceWords& w : ROWS)
		if (w.face == f)
			return w;
	return NONE;
}
/** What a gatherer is asked to gather, the choice on its strip, as the
one integer its param
holds: nought for the nearest kind, as a gatherer always took, or else a kind
and a group of it, `kind + 1 + GROUP_STEP * group`, group nought for every
free module of the kind and `g` for the g-th group of them from the left. A
group is the free modules of the kind standing side by side: anything else
between two of them, another kind, a taken one or a module that is no source
at all, begins a new group, all but an Atn carrying no signal, which no
gatherer sees (`unseen`), so CV CV SEQ CV CV POLY has two groups of CVs and
a Seq's own block stands between it and the next Seq. A choice whose kind or
group is not there gathers nothing, and its strip still names it. */
const int GROUP_STEP = 32;
struct Choice {
	int kind = -1, group = 0; // minus one for the nearest
	static Choice of(int code) {
		Choice c;
		if (code > 0) {
			c.kind = code % GROUP_STEP - 1;
			c.group = code / GROUP_STEP;
		}
		return c;
	}
	int code() const { return kind < 0 ? 0 : kind + 1 + GROUP_STEP * group; }
};
/** The choices a strip offers: the free kinds to the gatherer's left,
nearest first, each with how many groups of it there are, packed a byte an
entry, the kind plus one in the low half and the groups in the high, for the
strip. */
const int OFFER_MAX = 8;
inline uint64_t packOffer(int i, int kind, int groups) {
	return (uint64_t)(((kind + 1) & 0xf) | (std::min(groups, 15) << 4)) << (8 * i);
}
inline int offerKind(uint64_t o, int i) { return (int)((o >> (8 * i)) & 0xf) - 1; }
inline int offerGroups(uint64_t o, int i) { return (int)((o >> (8 * i + 4)) & 0xf); }

/** Free: a module a gatherer may take, one no gatherer to its left has. */
inline bool freeToGather(tree::Node* m) {
	return faceOf(m) != manifest::F_NONE && !(m->told() & tree::TOLD_BUNDLED);
}
inline bool gathersNothing(tree::Node* m);
/** Free for a gatherer that `sums` (a Mix) or bundles (a Poly): free, and to
a Mix not a gatherer that gathers nothing, whose sum would add nothing and
whose place in a Poly to its right it would only take. */
inline bool freeFor(tree::Node* m, bool sums) {
	return freeToGather(m) && !(sums && gathersNothing(m));
}
/** An Atn carrying no signal, its arrow unlit and IN empty: a configurator,
or standalone in its rig, a router of an empty IN (`Atn::standaloneParse`),
which a gatherer neither takes nor lets part a group. */
inline bool unseen(tree::Node* m) {
	return m->manifestId() == manifest::M_ATN || (m->manifestId() == manifest::M_VCA && faceOf(m) == manifest::F_NONE);
}
/** Each free module of `kind` to the left of `place`, its group from the
left, into `group`, nought for every other module; the count of groups
returned. */
inline int groupsOf(const tree::Left& left, int place, bool sums, int kind, int* group) {
	int g = 0;
	bool run = false;
	for (int i = 0; i < place; i++) {
		group[i] = 0;
		if (freeFor(left[i], sums) && faceOf(left[i]) == kind) {
			if (!run)
				g++;
			run = true;
			group[i] = g;
		}
		else if (!unseen(left[i]))
			run = false;
	}
	return g;
}
/** The offer, over the same parse: every free kind to the left, nearest
first. */
inline uint64_t offerOf(const tree::Left& left, int place, bool sums) {
	uint64_t o = 0;
	int n = 0, seen[OFFER_MAX];
	std::vector<int> group(place > 0 ? place : 1);
	for (int i = place - 1; i >= 0 && n < OFFER_MAX; i--) {
		if (!freeFor(left[i], sums))
			continue;
		const int k = faceOf(left[i]);
		if (std::find(seen, seen + n, k) != seen + n)
			continue;
		o |= packOffer(n, k, groupsOf(left, place, sums, k, group.data()));
		seen[n++] = k;
	}
	return o;
}
/** The kind the nearest choice comes to, the default: the nearest free
source's, and only where there is none the nearest free gatherer's; minus
one for none. */
inline int nearestKind(const tree::Left& left, int place, bool sums) {
	for (int pass = 0; pass < 2; pass++)
		for (int i = place - 1; i >= 0; i--)
			if (freeFor(left[i], sums) && gatherer(left[i]->manifestId()) == (pass == 1))
				return faceOf(left[i]);
	return -1;
}
/** The gathering, over the finished parse: the members into `chain`, leftmost
first, and their kind returned, a manifest row, or minus one for none. The
gatherer tells them afterwards, not while: a statement made to a member is
passed on down its block (`relays`), and would mark its peers taken before the
gatherer came to them. `most` is how many rows the gatherer has: a Mix gives
its sixteenth to the sum, and a member it had no row for would be taken from
the gatherer to its right for nothing. `choice` is the strip's: the nearest
kind, or a kind and a group of it. `sums` is whether the gatherer is a Mix
(`freeFor`). */
inline int gather(const tree::Left& left, int place, bool sums, std::vector<Member*>& chain, int most = GATHER_MAX,
                  Choice choice = Choice()) {
	chain.clear();
	const int kind = choice.kind < 0 ? nearestKind(left, place, sums) : choice.kind;
	if (kind < 0)
		return -1;
	std::vector<int> group(place > 0 ? place : 1);
	groupsOf(left, place, sums, kind, group.data());
	for (int i = place - 1; i >= 0 && (int)chain.size() < most; i--)
		if (group[i] > 0 && (choice.group == 0 || group[i] == choice.group))
			chain.insert(chain.begin(), static_cast<Member*>(left[i]));
	return kind;
}

/** The words for a choice: SEQs for every Seq, CVs 2 for the second group
of CVs; the nearest is named by the kind it came to. */
inline std::string choiceWord(Choice c, int nearest) {
	if (c.kind < 0)
		return faceWords(nearest).words;
	return c.group > 0 ? string::f("%s %d", faceWords(c.kind).words, c.group) : std::string(faceWords(c.kind).words);
}

/** A choice in a sentence's words, for the strip's tooltip: Seqs, CVs,
group 2; the nearest is named by the kind it came to. */
inline std::string choiceName(Choice c, int nearest) {
	if (c.kind < 0)
		return faceWords(nearest).name;
	return c.group > 0 ? string::f("%s, group %d", faceWords(c.kind).name, c.group) : std::string(faceWords(c.kind).name);
}

/** A gatherer's OUT, silent for want of what its strip asks, one wording
for a Poly and a Mix, `verb` what it does ("gather", "mix in"): nothing to
its left, no module of the kind, or no such group, the group named as the
strip shows it (`choiceWord`, "CVs 2"), the one word for it a player sees. */
inline std::string silentFor(Choice ch, const char* verb) {
	if (ch.kind < 0)
		return string::f("Silent: nothing to its left to %s, and IN is empty.", verb);
	if (ch.group > 0)
		return string::f("Silent: the strip asks for %s, which is not to its left.", choiceWord(ch, -1).c_str());
	return string::f("Silent: there are no %s to its left to %s.", faceWords(ch.kind).name, verb);
}

/** The choices a stroke or the strip's arrows step through, in order: the
nearest first, the default, then every kind offered, nearest first, all of
it and then each of its groups where it has more than one, the nearest kind's
all left out since the default is that already; and the choice standing
last where nothing answers it, so stepping from it never loses it unseen.
Codes, as the param holds them. */
inline std::vector<int> choiceCycle(uint64_t offer, int nearest, int now) {
	std::vector<int> c{0};
	for (int i = 0; i < OFFER_MAX; i++) {
		const int k = offerKind(offer, i);
		if (k < 0)
			break;
		Choice all;
		all.kind = k;
		if (k != nearest)
			c.push_back(all.code());
		const int groups = offerGroups(offer, i);
		for (int g = 1; groups > 1 && g <= groups; g++) {
			Choice one = all;
			one.group = g;
			c.push_back(one.code());
		}
	}
	if (std::find(c.begin(), c.end(), now) == c.end())
		c.push_back(now);
	return c;
}

/** What a gatherer publishes for its strip, apart from the template so the
strip is one widget for both: the offer, and the kind its choice came to. */
struct GatherReads {
	std::atomic<uint64_t> offerNow{0};
	std::atomic<int> gatheredNow{-1}; // the kind gathered, whether or not it yields a channel
	std::atomic<int> nearestNow{-1};  // the kind the nearest choice comes to, for the cycle
	std::atomic<bool> stripNow{false};
	/** Each row's source module, by id, minus one for a cable's: what the
	hover points at, a row's or the strip's every one. */
	std::atomic<int64_t> srcIdNow[GATHER_MAX];
	std::atomic<int> srcNNow{0};
	GatherReads() {
		for (auto& i : srcIdNow)
			i.store(-1, std::memory_order_relaxed);
	}
	/** The choices the strip steps through, as published. */
	std::vector<int> cycle(int now) { return choiceCycle(relaxed(offerNow), relaxed(nearestNow), now); }
	virtual ~GatherReads() {}
	/** The strip's param, which each gatherer places after its own. */
	virtual int choiceParam() = 0;
	/** Whether I gather nothing, no member and no cable in IN, as the parse
	left me: read by a gatherer to my right in the same parse, after mine. */
	virtual bool gathersNothing() = 0;
	/** The channel of my jack row `r` goes out on, counted from one, read
	on the UI thread from the params as they stand: a Poly's rows left on
	before it and itself, nought for a row left out; nought for a Mix, whose
	jack is one sum. The one count a Poly's row value and a gathered CV's
	lesson both read. */
	virtual int outChannel(int r) { return 0; }
};
/** A gatherer that gathers nothing, as its own parse left it, which ran
before that of any gatherer to its right; never a module that is no
gatherer. */
inline bool gathersNothing(tree::Node* m) {
	if (!gatherer(m->manifestId()))
		return false;
	GatherReads* g = dynamic_cast<GatherReads*>(static_cast<Member*>(m));
	return g && g->gathersNothing();
}

/** What a Poly and a Mix share: a source a row, the cables stacked in IN,
the peak meter over the rows and what the body is handed at the end of a
wake. `Self` names the two things they differ in, without a virtual call on
the audio thread: the level of a bar (`meterLevel`, a Mix's last bar being
its sum) and whether there is anything to name under the jack (`carries`).
`ROWS` is how many sources it has rows for, `BARS` how many bars it meters. */
template <class Self, int ROWS, int BARS>
struct Gatherer : Member, Probe, GatherReads {
	int kindWork = -1;            // the kind gathered, a manifest row, or minus one for none
	std::atomic<int> kindNow{-1}; // and for the caption and the tooltip
	bool fromJack = false;
	/** The cables stacked in IN, read one by one (Stack.hpp). */
	Stack stack;
	bool stacked = false;
	std::atomic<bool> stackedNow{false};
	std::atomic<uint32_t> rgbNow[ROWS];
	/** What came in, a source a row, before any was left out or levelled. */
	int srcN = 0;
	float srcV[ROWS] = {};
	int srcCol[ROWS] = {};
	int srcKind[ROWS] = {};
	uint32_t srcRgb[ROWS] = {}; // a member's own colour for its row, nought for mine (`gatheredRgb`)
	// For the body: each bar, its span packed in a word (`HeldSpan`), and a header with the count, the colour and
	// whether a cable is in charge.
	std::atomic<uint64_t> shown[BARS];
	std::atomic<uint32_t> header{0};
	/** The header, handed to the body at the end of the wake with the colour
	on the members, so a frame never sees the cable's new count over the
	block's old bars. */
	uint32_t headerWork = 0;
	// Each row's colour, four bits a row, nought for the gatherer's own: the
	// bars wear the colour of the Seq each came from.
	std::atomic<uint64_t> colorsNow{0};
	std::atomic<uint64_t> kindsNow{0}; // each row's source kind, four bits a row, for the hover
	/** Each row's stacked cable, counted from nought, four bits a row, for
the hover, while cables are stacked in IN. */
	int srcCable[ROWS] = {};
	std::atomic<uint64_t> cablesNow{0};
	/** Each row's source module, by id, minus one for a cable's, for
	`srcIdNow`. */
	int64_t srcId[GATHER_MAX] = {};
	/** The channels that came to me past my last row, left out, for the
	tooltip: a source gives every channel it has, a Seq's chord or a Poly's,
	so sixteen can be passed, and a gatherer shows what it dropped. */
	int dropped = 0;
	std::atomic<int> droppedNow{0};
	/** The words for them, a sentence of their own, nothing while none
	was; `noun` what a channel lands on, a Mix's fader or a Poly's row, so
	the count has a thing to count. */
	std::string droppedWords(const char* noun) const {
		const int d = relaxed(droppedNow);
		return d == 1  ? string::f("One channel past the %dth %s is left out.", ROWS, noun)
		       : d > 1 ? string::f("Channels past the %dth %s are left out.", ROWS, noun)
		               : std::string();
	}
	std::atomic<bool> fromJackNow{false};

	Gatherer() {
		for (int c = 0; c < BARS; c++)
			shown[c].store(HeldSpan().packed(), std::memory_order_relaxed);
		for (int c = 0; c < ROWS; c++)
			rgbNow[c].store(0, std::memory_order_relaxed);
	}
	Self& self() { return static_cast<Self&>(*this); }

	/** A cable in IN changes what I gather, nothing, so it is a change of
	the parse as well as of the wiring. */
	void onPortChange(const PortChangeEvent& e) override {
		Member::onPortChange(e);
		if (e.type == engine::Port::INPUT && e.portId == Self::IN_INPUT)
			this->oweParse();
	}
	std::vector<Member*> chain; // what I gather, leftmost first
	/** A gatherer colours everything it gathers, which is cleared for the
	whole rig every wake, so it is at every wake too. */
	bool atEveryWake() override { return true; }
	/** Colourless: I take no colour of the rig's and give my members none. */
	void settle() override { rankNone(at(Self::S_READER)); }
	/** The sources, gathered over the finished parse in pass nought, which
	goes over the rig left to right: the kind of the nearest module to my
	left that I can gather and no gatherer to my left has taken, and every
	module of that kind to my left none has taken, as many as I have rows
	for. Whether one is taken is what that gatherer told it (`TOLD_BUNDLED`),
	read off the module, which stands to my left as everything I read does:
	a gatherer further left ran its pass nought before mine, so the order of
	the parse is the precedence, and no module reads what holds it. Each
	member gets an edge from me, so its change wakes me, and is told it is
	taken: a statement no slot can carry, since the gathering is found by
	none. With a cable in IN, nothing, since the cable is what I gather. */
	void parsed(const tree::Left& left, int pass) override {
		if (pass != 0)
			return;
		chain.clear();
		choiceParsed = choiceCode();
		const bool sums = readerKind() == READ_MIX;
		offerWork = offerOf(left, place, sums);
		const bool jack = inputs[Self::IN_INPUT].isConnected();
		jackParsed = jack;
		nearestWork = jack ? -1 : nearestKind(left, place, sums);
		kindWork = jack ? -1 : gather(left, place, sums, chain, ROWS, Choice::of(choiceParsed));
		takeGathered(chain);
	}
	bool jackParsed = false; // whether the parse found a cable in IN
	bool gathersNothing() override { return chain.empty() && !jackParsed; }
	int choiceParam() override { return Self::GATHER_PARAM; }
	/** The strip's choice as it stands, an integer from its param. */
	int choiceCode() { return (int)std::lround(params[Self::GATHER_PARAM].getValue()); }
	int choiceParsed = 0; // the choice the parse gathered by
	int nearestWork = -1; // the kind the nearest choice comes to, for `nearestNow`
	/** A new choice on the strip is a new gathering, which the parse makes,
	so it is a change of the parse, asked by the host. */
	bool parseStale() override { return choiceCode() != choiceParsed; }
	/** The strip's tooltip: its name, beside which Rack writes the kind
	gathered now (`ChoiceQuantity`), and the stroke that changes it on its
	hint, only where there is another choice to change to. */
	void retipChoice() {
		const GatherStripWords& w = gatherStripWords(readerKind() == READ_MIX);
		const std::vector<int> c = cycle(choiceCode());
		// A group, the code's word, is said where the strip shows one or
		// steps to one: the kind of the choice made, else the first of the
		// cycle's.
		int grouped = -1;
		if (Choice::of(choiceCode()).group > 0)
			grouped = Choice::of(choiceCode()).kind;
		else
			for (int code : c)
				if (Choice::of(code).group > 0) {
					grouped = Choice::of(code).kind;
					break;
				}
		const std::string lesson =
		    grouped < 0 ? std::string() : string::f(GATHER_GROUP_WORDS, faceWords(grouped).name);
		retipParam(this, Self::GATHER_PARAM, {w.name, lesson.c_str(), c.size() > 1 ? Gestures{w.drag} : Gestures{}});
	}
	/** What the strip asks for, in a sentence's words, for a jack's
	tooltip: "the CVs", "group 2 of the CVs", or the kind gathered now for
	the nearest; empty where the nearest comes to nothing. */
	std::string askedWords() {
		const Choice c = Choice::of(choiceCode());
		const int kind = c.kind < 0 ? relaxed(nearestNow) : c.kind;
		if (kind < 0)
			return std::string();
		const std::string all = std::string("the ") + faceWords(kind).name;
		return c.group > 0 ? string::f("group %d of %s", c.group, all.c_str()) : all;
	}
	uint64_t offerWork = 0;

	/** What came in, a source a row, into `srcV` and its colour, kind and
	module beside it: the cable in IN, its stacked cables one by one, or
	else the members' channels in rig order, every channel past my last row
	counted in `dropped`. */
	void readSources() {
		fromJack = inputs[Self::IN_INPUT].isConnected();
		srcN = 0;
		dropped = 0;
		stacked = false;
		if (fromJack) {
			float each[Stack::MAX];
			const int k = stack.read(each);
			stacked = k >= 2;
			const int given = stacked ? k : inputs[Self::IN_INPUT].getChannels();
			srcN = clamp(given, 0, ROWS);
			// A sixteen-voice cable has one voice past a Mix's fifteen rows,
			// and the tooltip says so.
			dropped = std::max(given - ROWS, 0) + (stacked ? stack.past : 0);
			for (int c = 0; c < srcN; c++) {
				// A voice that is not a number is stopped on its own, the
				// others passed, so one bad voice never zeroes a whole sum.
				srcV[c] = sane(stacked ? each[c] : inputs[Self::IN_INPUT].getVoltage(c));
				srcCol[c] = -1;
				srcKind[c] = -1;
				srcId[c] = -1;
				srcRgb[c] = 0;
				srcCable[c] = stacked ? stack.cable(c) : 0;
			}
		}
		else {
			float got[2 * GATHER_MAX]; // a Poly member gives up to sixteen past the rows already had
			for (Member* m : chain) {
				// A chord gives every note, and a bar of gates across Trgs
				// every gate.
				const int k = m->channelsOut(got + srcN);
				if (k <= 0)
					continue;
				dropped += std::max(srcN + k - ROWS, 0);
				const int col = m->readerColor(); // a Seq's colour; a Mix is colourless
				// A CV fed from a Poly's cable: the cable's colour, or the
				// grey of a channel replaced.
				const uint32_t rgb = k == 1 ? m->gatheredRgb() : 0;
				for (int c = srcN; c < std::min(srcN + k, ROWS); c++) {
					srcV[c] = got[c];
					srcCol[c] = col;
					srcRgb[c] = rgb;
					srcKind[c] = faceOf(m);
					srcId[c] = m->id;
				}
				srcN = std::min(srcN + k, ROWS);
			}
		}
	}
	/** The header packed for the wake, and the levels seen. A gatherer
	colours none of its members: it has no colour to give them. */
	void markRead() {
		packHeader();
		seeLevels();
	}
	/** Where row `c` came from, for its tooltip, off what was published: a
	stacked cable by its place in the stacking and its channel where it has
	several ("Cable 2, channel 1"), the one cable's channel, or its source by
	kind and place, counted from the left among the modules gathered ("CV
	2"), and the channel of it where one gives several ("POLY 1, channel
	2"), a source's rows standing side by side (`readSources`), never the
	row's own number after the kind, which reads one POLY's two channels as
	two POLYs. A trigger is a SEQ's, walking its TRGs, so it is named by its
	SEQ, and always which trigger. */
	std::string rowFrom(int c, bool cables, bool jack, uint64_t kinds) {
		if (cables) {
			const uint64_t of = relaxed(cablesNow);
			const int rows = std::min(relaxed(srcNNow), ROWS);
			const int cable = (int)((of >> (4 * c)) & 0xf);
			int channel = 0, channels = 0;
			for (int r = 0; r < rows; r++)
				if ((int)((of >> (4 * r)) & 0xf) == cable) {
					channels++;
					channel += r <= c;
				}
			return channels > 1 ? string::f(GATHER_ROW_CABLE_CHANNEL, cable + 1, channel) : string::f(GATHER_ROW_CABLE, cable + 1);
		}
		if (jack)
			return string::f("Channel %d of the cable", c + 1);
		const int k = (int)((kinds >> (4 * c)) & 0xf);
		const int rows = std::min(relaxed(srcNNow), GATHER_MAX);
		const int64_t id = relaxed(srcIdNow[c]);
		int place = 0, channel = 0, channels = 0;
		for (int r = 0; r < rows; r++) {
			const int64_t at = relaxed(srcIdNow[r]);
			if (r <= c && (r == 0 || at != relaxed(srcIdNow[r - 1])))
				place++;
			if (at == id) {
				channels++;
				if (r <= c)
					channel++;
			}
		}
		if (k - 1 == manifest::F_TRIG)
			return string::f(GATHER_ROW_TRIGGER, std::max(place, 1), channel);
		const char* word = faceWords(k > 0 ? k - 1 : -1).word;
		return channels > 1 ? string::f("%s %d, channel %d", word, std::max(place, 1), channel)
		                    : string::f("%s %d", word, std::max(place, 1));
	}

	/** The header as the wake leaves it: the count, and whether a cable is
	in charge. */
	void packHeader() { headerWork = (uint32_t)srcN | (fromJack ? 1u << 24 : 0u); }

	/** The bars are a peak meter, not the value at the wake: a bar is a
	screen's hold on its row (`HeldSpan`, Pulse.hpp), a hold each side of
	nought, each jumping to anything further out and falling back over
	`SHOW_FALL_SECONDS`, so a millisecond's trigger from a Seq walking Trgs
	stands long enough to be seen, a slow wave reads as itself with a little
	lag on the way down, and audio reads as the span of its swing. Every
	level a wake leaves is seen (`seeLevels`), so a level that lasts a wake
	and no more, a trigger shorter than a control period, is caught between two
	looks; the looks are made at the control rate, outside any wake, the way
	an Atn's meter is, because a row that has gone quiet after a trigger never
	wakes again to let the bar fall. */
	float barOut(int c) override {
		float foot = 0.f, top = 0.f;
		if (c >= 0 && c < BARS)
			unpackSpan(relaxed(shown[c]), foot, top);
		return spanMark(foot, top);
	}
	bool barSpanOut(int c, float& foot, float& top) override {
		if (c < 0 || c >= BARS)
			return false;
		unpackSpan(relaxed(shown[c]), foot, top);
		return true;
	}
	bool wantsTick() override { return true; } // the meter is ticked every sample; it does the work on the control period
	HeldSpan held[BARS];
	/** A bar's level: a row a source, the ones left out of the jack too, so
	a row keeps its level whether it is in or out. `k` is the wake's count,
	not the jack's, since Rack keeps an uncabled output at nought channels.
	The screen's span is ten volts either way: past it a bar is full, and a
	peak of thousands held there would take minutes to fall, since the jacks
	have no rails. */
	float barLevel(int c) { return clamp(self().meterLevel(c, std::min(srcN, ROWS)), -10.f, 10.f); }
	float meterLevel(int c, int k) { return c < k ? srcV[c] : 0.f; }
	/** Every bar's level as this wake leaves it, for the next look. */
	void seeLevels() {
		for (int c = 0; c < BARS; c++)
			held[c].see(barLevel(c));
	}
	/** Stacked cables are read one by one, and the row is woken by the
	input's sum alone, which two sources moving against each other leave
	still; so while they are stacked I ask for a wake every sample. */
	bool everySample(const ProcessArgs& args) override {
		const bool wake = stacked;
		if (args.frame % CONTROL_PERIOD)
			return wake;
		const float dt = (float)CONTROL_PERIOD * args.sampleTime;
		for (int c = 0; c < BARS; c++) {
			held[c].look(barLevel(c), dt, 10.f);
			shown[c].store(held[c].packed(), std::memory_order_relaxed);
		}
		return wake;
	}
	/** A row's colour and its face, each plus one in four bits, nought for
	none: a colour past the palette, or a face past the faces, is packed as
	none. */
	static_assert(MAX_COLORS < 16 && manifest::F_COUNT < 16, "a colour or a face plus one fits four bits");
	void publishReads() override {
		uint64_t cols = 0;
		uint64_t kinds = 0;
		uint64_t cables = 0;
		for (int c = 0; c < std::min(srcN, ROWS); c++) {
			if (stacked)
				cables |= (uint64_t)(srcCable[c] & 0xf) << (4 * c);
			if (srcCol[c] >= 0 && srcCol[c] < MAX_COLORS)
				cols |= (uint64_t)(srcCol[c] + 1) << (4 * c);
			if (srcKind[c] >= 0 && srcKind[c] < manifest::F_COUNT)
				kinds |= (uint64_t)(srcKind[c] + 1) << (4 * c);
		}
		colorsNow.store(cols, std::memory_order_relaxed);
		kindsNow.store(kinds, std::memory_order_relaxed);
		cablesNow.store(cables, std::memory_order_relaxed);
		for (int c = 0; c < GATHER_MAX; c++)
			srcIdNow[c].store(c < srcN ? srcId[c] : -1, std::memory_order_relaxed);
		srcNNow.store(srcN, std::memory_order_relaxed);
		droppedNow.store(dropped, std::memory_order_relaxed);
		fromJackNow.store(fromJack, std::memory_order_relaxed);
		stackedNow.store(stacked, std::memory_order_relaxed);
		for (int c = 0; c < ROWS; c++)
			// One cable in IN: every row in its colour; stacked, each its own
			// cable's; gathered, whatever its member gave (`gatheredRgb`).
			rgbNow[c].store(c >= srcN ? 0 : stacked ? stack.rgb(c) : fromJack ? stack.first() : srcRgb[c],
			                std::memory_order_relaxed);
		header.store(headerWork, std::memory_order_relaxed);
		offerNow.store(offerWork, std::memory_order_relaxed);
		gatheredNow.store(kindWork, std::memory_order_relaxed);
		nearestNow.store(nearestWork, std::memory_order_relaxed);
		// The strip stands while there is something to choose among, or a
		// choice made, and no cable in charge.
		stripNow.store(!fromJack && (offerWork != 0 || choiceParsed != 0), std::memory_order_relaxed);
		// A gathering that yields nothing, a choice nothing answers, has
		// nothing to name, and the caption says OUT rather than naming what
		// it carries nothing of.
		kindNow.store(self().carries() || fromJack ? kindWork : -1, std::memory_order_relaxed);
	}
};

} // namespace rig
