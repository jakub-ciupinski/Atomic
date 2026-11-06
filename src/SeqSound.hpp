// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/** A Seq's sounding, what the playhead sounds where it stands: `soundStep`
and the voices past the first (`soundVoices`). A part of Seq.cpp in a file of
its own: Seq.cpp includes it once, inside `struct Seq`, and it is not a
header to include anywhere else. */

	/** The voices past the first, where the member stood on carries a chord
	on a cable: the cable in my IN jack a transforming member is handed, routed
	into the Atn stood on with that Atn's own cable, or the member's own cable
	where I hand it none. Each voice is the member's own act on it, so a step
	sounds what the member alone makes of the chord, through the window an Atn
	before it shapes: a sequencer tied to cartridges inherits their polyphony.
	A source, a CV or a Trg,
	holds one value a step and stays mono. */
	void soundVoices(Cartridge* m, int sub, const Signal& sig, bool routing, bool transforms, int color, Sound& snd,
	                 bool light = true) {
		if (snd.type != Sound::VOLTS || (m->traits & tree::T_SOURCE))
			return;
		const bool own = m->cv.live();
		int n = 1;
		if (routing)
			n = std::max(in.voices(), own ? m->cv.voices() : 1);
		else if (sig.present && transforms)
			n = in.voices();
		else if (!sig.present && own)
			n = m->cv.voices();
		if (n < 2)
			return;
		QuantFace* qf = light ? m->quantFace() : nullptr; // a fade's last step lights nothing
		snd.voices = n;
		for (int c = 1; c < n; c++) {
			const float v = routing ? in.getVoice(c) + (own ? m->cv.getVoice(c) : 0.f)
			                : sig.present ? in.getVoice(c)
			                              : m->cv.getVoice(c);
			Signal s;
			s.setVolts(v, color, Mapping());
			Sound o;
			m->sound(sub, s, color, o);
			snd.voice[c] = o.band != NO_BAND && o.semis == 12.f * o.volts ? o.volts : o.semis / 12.f;
			if (qf && o.band != NO_BAND)
				qf->lightBand(o.band, color);
			else if (qf && o.value >= 0)
				qf->light(o.note, color);
		}
	}
	/** What the playhead sounds where it stands: the IN jack handed to
	the member stood on, the member's sound, and the notes lit on the Qnt it
	went through. */
	Sound soundStep(const ThisWake& w, const ProcessArgs& args) {
		const bool alone = w.alone, gates = w.gates, wide = w.wide, poly = w.poly;
		// The IN jack, normalled to the IN jack of the member the playhead
		// stands on: a member that transforms gets it as the signal it
		// transforms; a member that holds takes it as the step lands, and so
		// does my own memory. It is the only signal a member is ever handed.
		Signal sig;
		const bool transforms = !alone && (chain[0]->traits & tree::T_STAGE);
		// A block of Atns holds nothing, a lit one first in it included,
		// which is a step of its own but takes no REC (`Atn::sound`).
		const bool holds = !alone && (gates || ((chain[0]->traits & tree::T_SOURCE) && !w.overAtn));
		// Over a block of Atns the jack is ROUTE, the router: it goes into
		// whichever Atn the playhead stands on, added to that Atn's own
		// cable, and so out of that Atn's jack and out of mine alike.
		const bool routing = w.overAtn && in.live();
		routeWork = routing;
		if (in.live() && transforms && !routing)
			sig.setVolts(in.get(), color, Mapping());
		const bool taking = in.live() && holds && w.takes && !poly;
		recWork = in.live() && holds && !poly;

		Sound snd;
		stoodWork = nullptr;
		fadeTo = nullptr;
		fadeBy = 0.f;
		gateMask = -1;
		gateWide = wide;
		if (alone) {
			soundArp(w, snd);
			return snd;
		}
		// The member the step falls in, and the step within it.
		const int at = std::min(block.indexAt(w.sounding), block.size() - 1);
		const int within = w.sounding + block.skip - block.start[at];
		Cartridge* stood = chain[at];
		// The member sounded: the one stood on, or in PREVIEW the one
		// chosen by hand.
		Cartridge* chosenMember = stood;
		if (preview() && !gates)
			for (Cartridge* c : chain)
				if (mode() == MODE_PREVIEW ? c->previewing() : c->holding())
					chosenMember = c;
		float fadeA = 0.f;
		Cartridge* fadeNext = fadeOf(stood, chosenMember, fadeA, w);
		routeAll(routing || (transforms && in.live()), stood, fadeNext, fadeA);
		const int rings = gates ? stood->stepsOf(false) : 1;
		const int sub = gates ? (wide ? -1 : within) : 0;
		recordInto(stood, sub, rings, taking, holds, w, args);
		stoodWork = chosenMember;
		// Routed, what I sound is what the Atn's own jack makes: the
		// router and its own cable together.
		if (routing)
			sig.setVolts(in.get() + (chosenMember->cv.live() ? chosenMember->cv.get() : 0.f), color, Mapping());
		chosenMember->standsAt(lane, w.along);
		chosenMember->sound(sub, sig, color, snd); // `snd` fresh, as `sound` asks
		// As that Atn's own jack gives it, so the two agree.
		if (routing)
			snd.semis = 12.f * sane(snd.semis / 12.f);
		soundVoices(chosenMember, sub, sig, routing, transforms, color, snd);
		chosenMember->read(lane, color, snd.value);
		// And whether I struck it, a step begun there and its trigger
		// fired, for a Trg's bloom.
		if (w.fires)
			chosenMember->struck(lane, sub, args.frame + fireFrames(args, w.reaimed), w.began, w.reaimed);
		Cartridge* anchored = anchorOf(chosenMember, sub, w);
		if (fadeNext)
			weighFade(stood, fadeNext, fadeA, anchored);
		lightNotes(chosenMember, snd);
		if (snd.type == Sound::TRIG) {
			gateMask = snd.mask;
			gateRings = rings;
		}
		return snd;
	}
	/** Alone, the step is a channel of ARP, read where the playhead stands
	and never taken; with nothing in the jack, nought. */
	void soundArp(const ThisWake& w, Sound& snd) {
		const int chans = w.chans;
		const int c = clamp(w.sounding, 0, chans - 1);
		snd.semis = chans > 0 ? (arpV ? arpV[c] : inputs[IN_INPUT].getVoltage(c)) * 12.f : 0.f;
		snd.signal.set(snd.semis, color);
		snd.window = Mapping();
		// My playhead on the Poly I arpeggiate, on the channel sounding,
		// as it stands on a member I walk: the line there, and the halo
		// where the walk itself stands, the two parted by a PHASE turn.
		if (arpPoly && chans > 0) {
			const int b = clamp(basePos, 0, chans - 1);
			const bool parted = w.phased && b != c;
			arpPoly->arpStands(c, color, true, !parted);
			if (parted)
				arpPoly->arpStands(b, color, false, true);
		}
	}
	/** Fade over Atns: this Atn and the next in the walk weighed by how far
	through the step the place stands, the turned place where a turn moves
	it, the writer's ramp under a plain clock (`writeJack`). The next by the
	walk, so reverse fades backward, pendulum turns at its ends and the last
	step fades into the first; ONCE's last step has no next and holds. What
	has a single member to sound steps aside, as it does for the Interpolate
	lamp: a member chosen by hand, a step held on the step screen, an armed
	walk playing on the step it left. The next is weighed in from the step's
	first sample, at nought there, so a jack's width is the wider of the two
	throughout and never drops back on a step's start. One pair and one
	weight for OUT, the router and the playheads alike: the next, or nullptr
	where there is none, and its weight in `fadeA`. */
	Cartridge* fadeOf(Cartridge* stood, Cartridge* chosenMember, float& fadeA, const ThisWake& w) {
		if (w.fade && !w.silent && !w.holding && chosenMember == stood && count > 1 &&
		    !(w.once && !w.turning && w.tPlace + 1 >= cycle)) {
			Cartridge* m2 = chain[std::min(block.indexAt(stepAt(w.tPlace + 1, count, dir)), block.size() - 1)];
			if (m2 != stood) {
				fadeA = clamp((float)w.tAlong, 0.f, 1.f);
				return m2;
			}
		}
		return nullptr;
	}
	/** The router, into the member stood on and out of every other: added
	to an Atn's own cable, quantized by a Qnt, so its jack says what I sound.
	In Fade it pans, the member stood on taking one less the weight and the
	next the weight, so each Atn's own jack carries its share as OUT weighs
	them. */
	void routeAll(bool routes, Cartridge* stood, Cartridge* fadeNext, float fadeA) {
		float rv[PORT_MAX_CHANNELS], rvNext[PORT_MAX_CHANNELS];
		const int rn = routes ? std::min(in.voices(), PORT_MAX_CHANNELS) : 1;
		for (int c = 0; c < rn; c++) {
			const float v = routes ? in.getVoice(c) : 0.f;
			rv[c] = fadeNext ? (1.f - fadeA) * v : v;
			rvNext[c] = fadeA * v;
		}
		for (Cartridge* c : chain)
			if (routes && fadeNext && c == fadeNext)
				c->route(lane, true, rvNext, rn);
			else
				c->route(lane, routes && c == stood, rv, rn);
	}
	/** A take into the member `into`, ring or step `at`, sounded at once. */
	void takeInto(Cartridge* into, int at, const Taken& tk, const ProcessArgs& args) {
		into->take(at, tk);
		into->evaluate(args); // it publishes anew, so I sound it at once
		// And it is a change the gate has to see: the host runs it
		// next wake, asks `published`, and wakes whoever reads it,
		// the last CRV of a block that says every arc among them.
		// Evaluated here alone, nothing would read it until the param
		// sweep found the take a control period later, and the gated
		// engine would part from the ungated for those samples. The
		// poke is the wake asked for; this is the node that changed.
		into->markDirty();
		into->written();
		poke.store(true, std::memory_order_relaxed);
	}
	/** Halfway through a step a pulse played by hand is the next step's,
	early rather than late: the step after this one in the walk, lit at
	once, the channels `rose`. */
	void lightAhead(int rose, const ThisWake& w, const ProcessArgs& args) {
		const bool wide = w.wide;
		const int next = stepAt(w.tPlace + 1, count, dir);
		const int at2 = std::min(block.indexAt(next), block.size() - 1);
		Cartridge* m2 = chain[at2];
		const int sub2 = wide ? -1 : next + block.skip - block.start[at2];
		Taken tk;
		tk.light = true;
		tk.n = wide ? std::min(m2->stepsOf(false), (int)TRG_RINGS) : 1;
		for (int c = 0; c < tk.n; c++)
			tk.v[c] = (rose >> c) & 1 ? TRIG_HIGH : 0.f;
		takeInto(m2, sub2, tk, args);
		if (m2 != aheadMember || sub2 != aheadSub)
			aheadMask = 0;
		aheadMember = m2;
		aheadSub = sub2;
		aheadMask |= rose;
	}
	/** REC: the IN jack taken into the step stood on as the step lands, and
	over Trgs latched through the step. */
	void recordInto(Cartridge* stood, int sub, int rings, bool taking, bool holds, const ThisWake& w,
	                const ProcessArgs& args) {
		const bool gates = w.gates, wide = w.wide, poly = w.poly;
		// Halfway is on the step's length the writer knows, so a walk with no
		// period yet, a step held, picked or armed, or a walk turned at
		// PHASE, keeps every pulse in its own step (`lightAhead`).
		const bool aheadable = gates && drive == DRIVE_TIME && !w.halt && !w.phased && !w.silent && !w.armed;
		if (taking) {
			Taken tk;
			tk.n = wide ? rings : 1;
			for (int c = 0; c < tk.n; c++)
				tk.v[c] = inputs[IN_INPUT].getVoltage(c);
			// What was played ahead into this step stays lit.
			if (gates && stood == aheadMember && sub == aheadSub)
				for (int c = 0; c < tk.n; c++)
					if ((aheadMask >> c) & 1)
						tk.v[c] = std::max(tk.v[c], TRIG_HIGH);
			aheadMember = nullptr;
			aheadMask = 0;
			takeInto(stood, sub, tk, args);
			// Over Trgs a take latches: the
			// landing clears the ring where the jack is low, and a pulse
			// arriving in the first half of the step after it lights the
			// ring, so a trigger a sample behind the clock is not lost;
			// one in the second half is the next step's (`lightAhead`). Armed here for this step on
			// this member, the channels already high counted as heard.
			latchOn = gates;
			latchMember = stood;
			latchSub = sub;
			latchN = tk.n;
			for (int c = 0; c < tk.n; c++)
				latchHigh[c] = inputs[IN_INPUT].getVoltage(c) >= TRIG_HIGH;
		}
		else if (w.landed || !(in.live() && holds && !poly) || !gates || stood != latchMember || sub != latchSub) {
			latchOn = false;
			aheadMember = nullptr;
			aheadMask = 0;
		}
		else if (latchOn) {
			// Within the step: a channel rising through the threshold
			// lights its ring, and only lights it, in the first half of
			// the step, and the next step's in the second; a channel
			// falling through the low one is ready to be heard again.
			const bool late = aheadable && w.tAlong >= 0.5;
			Taken tk;
			tk.n = latchN;
			tk.light = true;
			bool rose = false;
			int ahead = 0;
			for (int c = 0; c < latchN; c++) {
				const float v = inputs[IN_INPUT].getVoltage(c);
				if (!latchHigh[c] && v >= TRIG_HIGH) {
					latchHigh[c] = true;
					if (late)
						ahead |= 1 << c;
					else {
						rose = true;
						tk.v[c] = TRIG_HIGH;
					}
				}
				else if (latchHigh[c] && v <= TRIG_LOW)
					latchHigh[c] = false;
			}
			if (rose)
				takeInto(stood, sub, tk, args);
			if (ahead)
				lightAhead(ahead, w, args);
		}
	}
	/** The halo where the walk itself stands, before a phase turn carried
	the playhead on; with no turn, where the line is. Within a Trg that is a
	ring, so a turn that keeps to one Trg still parts the two. And on a CRV,
	where on its curve: a turn inside one CRV parts the halo from the light
	there too. The member the halo stands on. */
	Cartridge* anchorOf(Cartridge* chosenMember, int sub, const ThisWake& w) {
		Cartridge* anchored = chosenMember;
		int anchoredSub = sub;
		if (w.phased && basePos != w.sounding) {
			const int bat = std::min(block.indexAt(basePos), block.size() - 1);
			anchored = chain[bat];
			anchoredSub = w.gates ? (w.wide ? -1 : basePos + block.skip - block.start[bat]) : 0;
		}
		anchored->anchor(lane, anchoredSub);
		anchored->anchorAt(lane, w.phased ? baseAlongWork : -1.f);
		return anchored;
	}
	/** Fade over Atns: OUT is the pair weighed (`writeJack`), the mix at
	nought this Atn exactly, and each shows the playhead at its weight, the
	halo with the line where the two stand together, the halo left where a
	phase turn parted it. */
	void weighFade(Cartridge* stood, Cartridge* fadeNext, float fadeA, Cartridge* anchored) {
		fadeTo = fadeNext;
		fadeBy = fadeA;
		if (fadeA > 0.f) {
			const bool whole = anchored == stood;
			stood->weigh(lane, 1.f - fadeA, whole);
			fadeNext->read(lane, color, -1);
			if (whole)
				fadeNext->anchor(lane, 0);
			fadeNext->weigh(lane, fadeA, whole);
		}
	}
	/** The note sounded, lit in my colour on the quantizer it went through:
	a source's on the Qnt its window runs through. A Qnt's own chord, a Seq
	walking a block of Qnts, lights nothing: my playhead over the Qnt
	already says which one is sounding, and its keys stay the neutral whites
	and greys, so the only colours on a keyboard are the notes a sequencer
	further on picks out of it. */
	void lightNotes(Cartridge* chosenMember, const Sound& snd) {
		if (!(chosenMember->traits & tree::T_SOURCE) && snd.type == Sound::CHORD)
			return;
		Member* q = (chosenMember->traits & tree::T_SOURCE)
		                   ? const_cast<Member*>(static_cast<const Member*>(chosenMember->quantOf()))
		                   : chosenMember;
		QuantFace* qf = q ? q->quantFace() : nullptr;
		if (qf && snd.type == Sound::CHORD) {
			const Scale& sc = snd.chord;
			for (int k = sc.base; k < sc.base + sc.period(); k++) // the notes where they stand: a roll's under C4 are no key of it folded up
				if (sc.has(k))
					qf->light(k, color);
			if (sc.spread && sc.extra)
				qf->light(sc.base + sc.period(), color);
		}
		else if (qf && snd.type == Sound::VOLTS && snd.band != NO_BAND)
			qf->lightBand(snd.band, color); // a divider's band by its own integer
		else if (qf && snd.type == Sound::VOLTS && ((chosenMember->traits & tree::T_SOURCE) || snd.value >= 0))
			qf->light(snd.note, color);
	}
