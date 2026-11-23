// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <rack.hpp>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include "Pulse.hpp"

using namespace rack;

extern Plugin* pluginInstance;
/** The manual links, kept in the panels' mode (plugin.cpp). */
void keepManualMode();

/** A value the audio thread publishes, read the one way everything reads
one: relaxed, the value a frame late at worst and never torn. */
template <typename T>
inline T relaxed(const std::atomic<T>& a) {
	return a.load(std::memory_order_relaxed);
}
/** What another thread left for this one, taken: the value standing in `a`,
with `rest` put in its place. Looked at first, since nearly every look finds
it at rest, and a look is a plain load where the exchange is a locked
instruction on x86, at every wake or every sample for a hand that comes a few
times a minute. Only the taker puts `rest` back, so what arrives after the
look is still there at the next and nothing left is lost. */
template <typename T>
inline T taken(std::atomic<T>& a, T rest, std::memory_order order = std::memory_order_relaxed) {
	return relaxed(a) == rest ? rest : a.exchange(rest, order);
}
/** The same of a flag raised: whether it was up, lowered. */
inline bool taken(std::atomic<bool>& flag, std::memory_order order = std::memory_order_relaxed) {
	return taken(flag, false, order);
}

/** The writer's side of a generation count over something wider than a
word: odd while this stands, what it guards being written, and even again
when it goes, the thing whole; a reader takes it between two looks at an even
count that agree, or tries again. One writer at a time, which is what a
generation count asks anyway, so the count goes up by a load and a store,
where an add in place is a locked instruction on x86, twice a writing. */
template <typename T>
struct Writing {
	explicit Writing(std::atomic<T>& gen) : gen(gen) {
		gen.store(relaxed(gen) + 1, std::memory_order_release); // odd: writing
		std::atomic_thread_fence(std::memory_order_release);
	}
	~Writing() { gen.store(relaxed(gen) + 1, std::memory_order_release); } // even: whole
	Writing(const Writing&) = delete;
	Writing& operator=(const Writing&) = delete;

private:
	std::atomic<T>& gen;
};

/** An answer kept from one asking to the next: `work` done when the question
is another than the last, and else the answer it gave, which for a `work`
that reads nothing but its question is the answer it would give. For what is
asked every sample of something that moves when a hand does, where the work
is a library call or a run of divisions. The asker's own: one thread's. */
template <typename Q, typename A>
struct Again {
	template <typename Work>
	const A& of(const Q& question, Work work) {
		if (!asked || !(question == last)) {
			answer = work(question);
			last = question;
			asked = true;
		}
		return answer;
	}

private:
	Q last{};
	A answer{};
	bool asked = false;
};

/** How many colours the rig has: the engine counts readers modulo this and
packs a colour in four bits, and the look paints each one (`playColor`). It
is here, under both, because neither may include the other. */
namespace rig {
const int MAX_COLORS = 15; // the palette; a colour past it comes round again, see `playColor`
}

/** What each output jack carries, kept by its module on the audio thread,
so a trigger is never missed, however short: a level for volts above nought,
one for below, and one for a polyphonic jack, each the volts out of ten,
held as a screen holds a transient (`rig::Held`, Pulse.hpp) over a jack's
own fall, `JACK_FALL_SECONDS`, rising at once and falling straight, so the
first frame after a trigger reads it near full wherever in the frame it fell.
Every sample is seen and the light is worked out on the look, every
`JACK_LOOK` samples, from the furthest the jack went since the last: read
only on the look, a trigger shorter than the look between two of them would
never be lit, a Seq's one-sample trigger among them, though the Atn reading
it hears it. The hold is a
plain field of the audio thread's and the light is stored where it is
measured, never loaded back. The same light goes into the jack's plug lights,
so a cable's plug, which covers the centre, blinks as the centre would: Rack
steps those with a fall of its own that loses two thirds in a thirtieth of a
second, and the module writes its own over it at every look.

This is the one light every output jack of the plugin has, ticked once a
sample by whoever writes the jacks, after they are written: a rig's host for
every member of its rig (`Member::process`), since it is the one thread
that writes them, and a module outside the family (Nudge, the dev-only test Harness) in
its own process. A jack with no cable reads as one channel, the engine
keeping no channel count for it.

Whoever ticks it says whether the jacks were written on this sample. A jack
nobody wrote stands where it stood, and seeing the same volts twice moves no
peak, so such a sample costs a test and no pass over the jacks; and a look
that finds nothing written since the last, and every light standing on what
its jack carries, would work out the lights it already has, so it leaves
them (`glowRest`). What is stored is, float for float, what a pass on every
sample would store, which the headless tests hold every module to
(`glowChecked`). */
struct JackGlow {
	/** Samples between two looks at the hold: a rig's control period, a
	light no eye can tell apart from one worked out every sample. */
	static const int JACK_LOOK = 32;
	int glowJacks = 0;
	std::unique_ptr<std::atomic<float>[]> glow; // three a jack: above, below, polyphonic
	std::unique_ptr<rig::Held[]> glowHeld;       // the same, the audio thread's own
	std::unique_ptr<int[]> glowWidth;            // each jack's channels, as the last look found them
	/** The furthest a jack went since the last look, kept every sample in
	the fewest operations: its highest and lowest volts on one channel, its
	largest sum of squares on several. The hold is worked from them on the
	look, so a sample between costs two comparisons a jack. */
	struct Peak {
		float hi = 0.f, lo = 0.f, v2 = 0.f;
	};
	std::unique_ptr<Peak[]> glowPeak;
	/** How the lights stand: written since the last look; or not written
	since it; or still, nothing written since a look that left every light
	standing on what its jack carries, when the peaks hold the standing volts
	and are kept, not cleared, and a look has nothing to work out. */
	enum GlowRest : uint8_t { GLOW_BUSY, GLOW_CALM, GLOW_STILL };
	GlowRest glowRest = GLOW_BUSY;
	float glowOf(int jack, int which) {
		return jack < glowJacks ? relaxed(glow[3 * jack + which]) : 0.f;
	}
	virtual ~JackGlow() {}
	/** How many channels my jack `jack` carries when it has no cable in it.
	Rack keeps an uncabled output at nought channels, whatever was asked of
	`setChannels`, so a module whose jack can be polyphonic says how wide it
	means it to be, and the jack glows blue before a cable is in it as after.
	Read on the audio thread, where the width was decided. */
	virtual int channelsMeant(int jack) { return 1; }
	/** How many of my outputs, from the first, have a centre to light: all
	of them, save where a module keeps jacks with no PJ301M drawn for them. */
	virtual int glowingJacks(Module* m) { return (int)m->outputs.size(); }
	/** The channels jack `j` carries now, as the light reads it: the
	engine's count under a cable, and with none the width the module means. */
	int widthNow(Module* m, int j) {
		const int ch = (int)m->outputs[j].getChannels();
		return std::max(1, ch > 0 ? ch : channelsMeant(j));
	}
	/** The light's arrays, sized to the jacks that glow: when the module
	joins the engine (`onAdd`), so the audio thread allocates nothing, and
	again by the look only if the count has moved since. They are whole from
	here, the widths with the rest: a module's first sample is the head of
	whatever block comes next, a look only where the block is a whole count
	of looks, and a width read before one was ever written is whatever the
	heap held, a count of channels the sum below would walk off the jack
	by. */
	void sizeGlow(Module* m) {
		const int n = std::min(glowingJacks(m), (int)m->outputs.size());
		if (n == glowJacks)
			return;
		glow.reset(new std::atomic<float>[3 * n]);
		glowHeld.reset(new rig::Held[3 * n]);
		glowWidth.reset(new int[n]);
		glowPeak.reset(new Peak[n]);
		for (int i = 0; i < 3 * n; i++)
			glow[i].store(0.f, std::memory_order_relaxed);
		for (int j = 0; j < n; j++)
			glowWidth[j] = widthNow(m, j);
		glowJacks = n;
		glowRest = GLOW_BUSY;
	}
	/** Once a sample, after the jacks are written: every sample seen, the
	light worked out on the look. `written` is whether anything wrote my
	jacks on this sample, true where nobody can say.

	The peaks are cleared by a look and filled from the sample after it, so
	that sample is seen whoever wrote, and the look's own, where a width may
	have moved; any other is seen only if written, the jacks standing as the
	last pass saw them. Still (`glowRest`), the peaks are not cleared at all,
	being what the first sample after the look would put back, unless that
	sample is written, when they are cleared for it as a look would have.
	What only a look does is kept out of line, so that the sample between
	two looks stays a few instructions where this is inlined. */
	void tickJacks(Module* m, const Module::ProcessArgs& args, bool written = true) {
		static_assert((JACK_LOOK & (JACK_LOOK - 1)) == 0, "the look's phase is the frame's low bits");
		const int phase = (int)(args.frame & (JACK_LOOK - 1));
		if (phase == 0)
			written = widthsMoved(m) || written;
		if (written) {
			if (glowRest == GLOW_STILL && phase == 1)
				for (int j = 0; j < glowJacks; j++)
					glowPeak[j] = Peak();
			glowRest = GLOW_BUSY;
			seeJacks(m);
		}
		else if (glowRest != GLOW_STILL && phase <= 1)
			seeJacks(m);
		if (phase == 0)
			lookJacks(m, args);
		if (glowChecked && !glowShadow)
			checkJacks(m, args);
	}
	/** The width is the look's to decide, for the samples up to the next: a
	jack changing width shows it a look later at most. Whether any moved,
	which is a jack written as far as its light goes. */
	__attribute__((noinline)) bool widthsMoved(Module* m) {
		sizeGlow(m);
		bool moved = false;
		for (int j = 0; j < glowJacks; j++) {
			const int width = widthNow(m, j);
			moved = moved || width != glowWidth[j];
			glowWidth[j] = width;
		}
		return moved;
	}
	/** One pass over the jacks: each peak taken as far as its jack stands. */
	void seeJacks(Module* m) {
		for (int j = 0; j < glowJacks; j++) {
			engine::Output& o = m->outputs[j];
			const int ch = glowWidth[j];
			Peak& p = glowPeak[j];
			if (ch == 1) {
				const float v = o.getVoltage();
				p.hi = std::max(p.hi, v);
				p.lo = std::min(p.lo, v);
			}
			else {
				float v2 = 0.f;
				for (int c = 0; c < ch; c++)
					v2 += o.getVoltage(c) * o.getVoltage(c);
				p.v2 = std::max(p.v2, v2);
			}
		}
	}
	/** The look: every light worked out from its jack's peak and stored,
	the plug lights with it. Still, the lights stand as they are stored and
	only the plug lights are written again, Rack stepping those itself
	between two looks. */
	__attribute__((noinline)) void lookJacks(Module* m, const Module::ProcessArgs& args) {
		if (glowRest == GLOW_STILL) {
			for (int j = 0; j < glowJacks; j++)
				for (int k = 0; k < 3; k++)
					plugLight(m->outputs[j], k, glowHeld[3 * j + k].mark);
			return;
		}
		const float dt = (float)JACK_LOOK * args.sampleTime;
		// Still from here if nothing was written since the last look and no
		// light is on its way down: the next look's peaks are then these and
		// its lights these.
		bool still = glowRest == GLOW_CALM;
		for (int j = 0; j < glowJacks; j++) {
			const Peak& p = glowPeak[j];
			const float want[3] = {p.hi / 10.f, -p.lo / 10.f, std::sqrt(p.v2) / 10.f};
			rig::Held* h = &glowHeld[3 * j];
			for (int k = 0; k < 3; k++) {
				const float lit = h[k].look(want[k], dt, 1.f, rig::JACK_FALL_SECONDS);
				still = still && lit == want[k];
				glow[3 * j + k].store(lit, std::memory_order_relaxed);
				plugLight(m->outputs[j], k, lit);
			}
		}
		glowRest = still ? GLOW_STILL : GLOW_CALM;
		if (!still)
			for (int j = 0; j < glowJacks; j++)
				glowPeak[j] = Peak();
	}
	/** Rack's plug lights run red, green, blue: below, above, polyphonic. */
	void plugLight(engine::Output& o, int k, float lit) {
		if (!glowShadow)
			o.plugLights[k == 0 ? 1 : k == 1 ? 0 : 2].value = lit;
	}

	/** The check, for the headless tests alone (`glowChecked`): beside me a
	second light of the same jacks, told on every sample that they were
	written, so it passes over them every sample and works every look out.
	After every look mine must be its, bit for bit, and the plug lights as
	well; `glowBroken` counts the looks where they are not. It is what
	holds every writer of a jack to saying so. */
	static inline bool glowChecked = false;
	static inline long long glowBroken = 0;
	bool glowShadow = false; // I am such a second light: no plug light is mine to write
	struct Shadow;
	std::unique_ptr<JackGlow> glowTwin;
	__attribute__((noinline)) void checkJacks(Module* m, const Module::ProcessArgs& args);
};
struct JackGlow::Shadow : JackGlow {
	JackGlow* of;
	Shadow(JackGlow* of) : of(of) { glowShadow = true; }
	int channelsMeant(int jack) override { return of->channelsMeant(jack); }
	int glowingJacks(Module* m) override { return of->glowingJacks(m); }
};
inline void JackGlow::checkJacks(Module* m, const Module::ProcessArgs& args) {
	if (!glowTwin) {
		glowTwin.reset(new Shadow(this));
		if (glowJacks > 0)
			glowTwin->sizeGlow(m); // sized as I am, or at its first look as I shall be
	}
	glowTwin->tickJacks(m, args);
	if (args.frame % JACK_LOOK != 0)
		return;
	bool same = glowTwin->glowJacks == glowJacks;
	for (int i = 0; same && i < 3 * glowJacks; i++) {
		const float mine = relaxed(glow[i]), its = relaxed(glowTwin->glow[i]);
		const float plug = m->outputs[i / 3].plugLights[i % 3 == 0 ? 1 : i % 3 == 1 ? 0 : 2].value;
		same = std::memcmp(&mine, &its, sizeof mine) == 0 && std::memcmp(&plug, &its, sizeof plug) == 0;
	}
	if (!same && glowBroken++ < 5)
		fprintf(stderr, "glowChecked: a light left alone differs from one worked every sample, at frame %lld\n", (long long)args.frame);
}

/** Every input jack of the plugin: the stock PJ301M, light or dark as Rack's
"Use dark panels if available" says, as the stock modules' are. */
using InPort = componentlibrary::ThemedPJ301MPort;

/** Every output jack of the plugin: the stock PJ301M, themed as an input is, its dark centre lit
with what the jack carries the way a cable's plug is: green for volts above
nought, red for below, blue for a polyphonic jack, the brightness the volts
out of ten, a trigger a short flash and a slow CV a steady glow, so a new
user sees what each jack does before a cable is in it. With no module
behind it, in a browser or a shot, the centre stays dark. */
struct OutPort : componentlibrary::ThemedPJ301MPort {
	void drawLayer(const DrawArgs& args, int layer) override {
		componentlibrary::ThemedPJ301MPort::drawLayer(args, layer);
		if (layer != 1 || !module)
			return;
		JackGlow* jg = dynamic_cast<JackGlow*>(module);
		if (!jg)
			return;
		const float g = clamp(jg->glowOf(portId, 0), 0.f, 1.f);
		const float r = clamp(jg->glowOf(portId, 1), 0.f, 1.f);
		const float b = clamp(jg->glowOf(portId, 2), 0.f, 1.f);
		const float a = std::max(g, std::max(r, b));
		if (a <= 0.01f)
			return;
		NVGcolor c = nvgRGBAf(0.f, 0.f, 0.f, 1.f);
		c = color::plus(c, color::mult(componentlibrary::SCHEME_GREEN, g / a));
		c = color::plus(c, color::mult(componentlibrary::SCHEME_RED, r / a));
		c = color::plus(c, color::mult(componentlibrary::SCHEME_BLUE, b / a));
		c.a = a;
		const Vec centre = box.size.div(2);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, centre.x, centre.y, 4.6f); // the hole in the PJ301M
		nvgFillColor(args.vg, c);
		nvgFill(args.vg);
	}
};

extern Model* modelSeq;
extern Model* modelClock;
extern Model* modelQnt;
extern Model* modelCV;
extern Model* modelAtn;
extern Model* modelTrg;
extern Model* modelPoly;
extern Model* modelMix;
extern Model* modelCrv;
extern Model* modelRig;
extern Model* modelGap;
// The dev modules, in a dev build only (the Makefile's ATOMIC_DEV).
#ifdef ATOMIC_DEV
extern Model* modelNudge;
extern Model* modelHarness;
#endif

/** The swap menu's modules (Bracket.hpp's SwapMenu): with Alt or Cmd held on a
module's title, these are what it can be replaced by, or have set in before or
after it, so a rig is built without the browser. Only the bonded modules, the
rig's own: never the Nudge or the Harness. One list, in
one place, in the order the menu shows: CV, CRV, TRG, ATN, QNT, then
CLK and SEQ, then the Mix and the Poly, then the RIG and the GAP, the
bonders. A RIG set in mid-rig parts the rig there: what stands to its
right becomes its molecule, and what stood to its left ends at its edge, as a
RIG bonds to nothing on its left. A GAP set in mid-rig parts the engine's rig
and leaves the bond whole. */
inline std::vector<Model*> swapCycle() {
	// In four sections: the cartridges, the sequencing, the gatherers and the
	// bonders, each a thin rule apart.
	return {modelCV, modelCrv, modelTrg, modelAtn, modelQnt, modelClock, modelSeq, modelMix, modelPoly, modelRig, modelGap};
}
/** Whether a section of the swap menu begins at item `i`. */
inline bool swapSectionAt(int i) { return i == 5 || i == 7 || i == 9; }
