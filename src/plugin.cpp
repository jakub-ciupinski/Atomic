// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugin.hpp"
#include "Rate.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;
	// The Seqs' window, read once (window.json says what it is).
	rig::loadWindowMs(asset::plugin(p, "window.json"));

	// In the order they always had, the dev modules (the Makefile's
	// ATOMIC_DEV) among them.
#ifdef ATOMIC_DEV
	p->addModel(modelNudge);
#endif
	p->addModel(modelSeq);
	p->addModel(modelClock);
	p->addModel(modelQnt);
	p->addModel(modelCV);
	p->addModel(modelAtn);
#ifdef ATOMIC_DEV
	p->addModel(modelHarness);
#endif
	p->addModel(modelTrg);
	p->addModel(modelPoly);
	p->addModel(modelMix);
	p->addModel(modelCrv);
	p->addModel(modelRig);
	p->addModel(modelGap);
	keepManualMode();
}
