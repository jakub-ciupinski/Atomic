// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <rack.hpp>
#include <atomic>
#include <cmath>
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
keeping no channel count for it. */
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
	/** Once a sample, after the jacks are written: every sample seen, the
	light worked out on the look. */
	void tickJacks(Module* m, const Module::ProcessArgs& args) {
		const bool look = (args.frame % JACK_LOOK) == 0;
		if (look) {
			const int n = std::min(glowingJacks(m), (int)m->outputs.size());
			if (n != glowJacks) {
				glow.reset(new std::atomic<float>[3 * n]);
				glowHeld.reset(new rig::Held[3 * n]);
				glowWidth.reset(new int[n]);
				glowPeak.reset(new Peak[n]);
				for (int i = 0; i < 3 * n; i++)
					glow[i].store(0.f, std::memory_order_relaxed);
				glowJacks = n;
			}
			// The width is the look's to decide, for the samples up to the
			// next: a jack changing width shows it a look later at most.
			for (int j = 0; j < n; j++) {
				const int ch = (int)m->outputs[j].getChannels();
				glowWidth[j] = std::max(1, ch > 0 ? ch : channelsMeant(j));
			}
		}
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
		if (!look)
			return;
		const float dt = (float)JACK_LOOK * args.sampleTime;
		for (int j = 0; j < glowJacks; j++) {
			engine::Output& o = m->outputs[j];
			Peak& p = glowPeak[j];
			const float want[3] = {p.hi / 10.f, -p.lo / 10.f, std::sqrt(p.v2) / 10.f};
			p = Peak();
			rig::Held* h = &glowHeld[3 * j];
			for (int k = 0; k < 3; k++) {
				const float lit = h[k].look(want[k], dt, 1.f, rig::JACK_FALL_SECONDS);
				glow[3 * j + k].store(lit, std::memory_order_relaxed);
				// Rack's plug lights run red, green, blue: below, above, polyphonic.
				o.plugLights[k == 0 ? 1 : k == 1 ? 0 : 2].value = lit;
			}
		}
	}
};

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
