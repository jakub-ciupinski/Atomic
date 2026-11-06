// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/** A CRV's two points, the start and the end of its arc, and the face's
painting of them. A part of Crv.cpp in a file of its own: Crv.cpp includes it
once, inside its anonymous namespace, where it stands on the Crv, ArcZone and
ArcFace above it, and it is not a header to include anywhere else (a painter
outside that namespace loses internal linkage, and under the SDK's unsafe
math can draw a grey level apart). */

/** One end of the arc, set by pointing as a CV's fader is: press and the
point goes to the pointer, a stroke carried sideways setting the same end of
every CRV it crosses; shift lands it on a whole volt of the window, command
scales every other point of its kind in the block (below), and command with
shift sets every one of them to the volts it lands on. A double click joins a
point to its neighbour's, the neighbour coming to it so the point stays under
the pointer: an end brings the next start to it, a start brings the end
before; the first's start and the last's end join round the block, which is
how a block loops. Joined, the two are one point until a double click parts
them again, anywhere in its column, the shared point staying where it was:
pressed or dragged on either side it is held whole, and under the pointer it
is a diamond, never one half of it. Alone, the point is a small triangle at the edge. */
struct ArcPoint : Hovered<LevelParam> {
	ArcFace* face = nullptr;
	bool end = false;
	Crv* crv() { return paramModule<Crv>(this); }
	bool sharedMove = false;  // this stroke, begun on my joined point, moves the shared point
	/** Whether my point is joined to its neighbour's. */
	bool joinedHere(Crv* m) { return end ? !m->endUsed() : relaxed(m->joinedInNow); }
	/** A sweep hands the stroke on to the next point by nearness: a start or
	a diamond to the module whose left edge is nearer the pointer, an end to
	the one whose right edge is, so the hand-over comes half way across a
	module rather than at its far edge. */
	float sweepShift() override {
		ModuleWidget* mw = getAncestorOfType<ModuleWidget>();
		const float w = mw ? mw->box.size.x : 0.f;
		return end && !sharedMove ? -w / 2.f : w / 2.f;
	}
	/** A diamond's sweep acts on starts, the diamonds it passes and the
	starts standing alone alike. */
	int lane() override { return end && !sharedMove ? 1 : 0; }
	/** The parameter this stroke sets: the next start, the shared point,
	when a joined end is dragged from inside the diamond. */
	engine::ParamQuantity* target() {
		if (sharedMove && end) {
			Crv* m = crv();
			Crv* R = m ? besideIn(this, m, 1) : nullptr;
			if (R)
				return R->paramQuantities[Crv::START_PARAM];
		}
		return getParamQuantity();
	}
	FineStroke held;
	/** A joined point is one point, held whole, so a start its IN holds
	locks both its halves: a start by its own IN, an end joined to the next
	start (the first's, round the block) by that start's, as the words say it
	(`endHeldUi`). The start's double click is refused with its drag, the
	end's still parts and joins. */
	bool locked() override {
		Crv* m = crv();
		if (!m)
			return false;
		return end ? !m->endUsed() && relaxed(m->endHeldUi) != 0 : m->startHeld();
	}
	/** Command held from the start of a stroke: every other point of my kind
	in the block, the starts (diamonds among them) for a start and the ends not
	joined for an end, scaled about nought volts as I am moved, as command on a
	shape moves every shape of the block; with shift too, each set to the
	whole volts I land on, so a block's points of a kind meet at the first
	move. Each with
	its volts at the stroke's start, held by id (`HeldModule`). */
	struct Scaled {
		HeldModule crv;
		int param;
		float volts;
	};
	std::vector<Scaled> group;
	float groupFrom = 0.f;
	static float voltsAt(Crv* c, float t) {
		return Mapping::placed(t * 2.f - 1.f, relaxed(c->loNow), relaxed(c->hiNow));
	}
	void gatherGroup(Crv* m, engine::ParamQuantity* pq) {
		group.clear();
		groupFrom = voltsAt(m, pq->getValue());
		for (Crv* c : blockOf(this, m)) {
			const int param = end ? Crv::END_PARAM : Crv::START_PARAM;
			if (c->paramQuantities[param] == pq || (end ? !c->endUsed() : c->startHeld()))
				continue;
			swept.note(c, param);
			group.push_back({HeldModule(c), param, voltsAt(c, c->params[param].getValue())});
		}
	}
	void scaleGroup(Crv* m, float x) {
		// Nought volts exactly is a point that cannot be scaled from: the
		// others stay where they are until it leaves it.
		if (group.empty() || groupFrom == 0.f)
			return;
		const float ratio = voltsAt(m, x) / groupFrom;
		for (const Scaled& g : group) {
			Crv* c = g.crv.as<Crv>();
			if (!c)
				continue;
			const float lo = relaxed(c->loNow), hi = relaxed(c->hiNow);
			if (hi == lo)
				continue;
			c->params[g.param].setValue(clamp((g.volts * ratio - lo) / (hi - lo), 0.f, 1.f));
			c->touch();
		}
	}
	void matchGroup(Crv* m, float x) {
		const float v = voltsAt(m, x);
		for (const Scaled& g : group) {
			Crv* c = g.crv.as<Crv>();
			if (!c)
				continue;
			const float lo = relaxed(c->loNow), hi = relaxed(c->hiNow);
			if (hi == lo)
				continue;
			c->params[g.param].setValue(clamp((v - lo) / (hi - lo), 0.f, 1.f));
			c->touch();
		}
	}
	void setFromY(float y) override {
		engine::ParamQuantity* pq = target();
		Crv* m = crv();
		if (!pq || !m)
			return;
		// A sweep from another screen crossing a joined end leaves it
		// joined; the next start it is joined to is the sweep's to set.
		if (end && !m->endUsed() && !sharedMove)
			return;
		const ArcZone z(box.size);
		float x = z.travel(y);
		// The press itself scales nothing: a command double click joins the
		// block, and its presses, which bring the point to the pointer, would
		// scale every other point of the kind on the way. The group moves
		// from the first move on. Command is the block here, not the fine
		// setting it is on a CV.
		const bool press = held.take(strokeNo(), y, pq->getValue(), false);
		const bool lands = end ? Hand::among(G_CRV_END_VOLTS) : Hand::among(G_CRV_START_VOLTS);
		if (press) {
			group.clear();
			if (end ? Hand::among(G_CRV_SCALE_ENDS) : Hand::among(G_CRV_SCALE_STARTS))
				gatherGroup(m, pq);
		}
		if (lands) {
			const float lo = relaxed(m->loNow), hi = relaxed(m->hiNow);
			if (hi != lo)
				x = clamp((std::round(Mapping::placed(x * 2.f - 1.f, lo, hi)) - lo) / (hi - lo), 0.f, 1.f);
		}
		pq->setValue(x);
		m->touch();
		if (!press) {
			if (lands)
				matchGroup(m, x);
			else
				scaleGroup(m, x);
		}
		activePoint() = this;
		if (sharedMove)
			if (Crv* R = besideIn(this, m, 1))
				R->touch();
	}
	/** The points a double click may touch, as they stood before its first
	press: that press moves the point, the shared one when it is joined, and
	the double click puts it back before it parts or joins, or the point
	parted would stand where the press put it. Held by id, since an undo may
	come between the two presses (`HeldModule`). */
	struct Before {
		HeldModule crv;
		float start, end, link;
	};
	Before before[3] = {};
	int beforeStroke = -1;
	void noteBefore(Crv* m) {
		Crv* cs[3] = {besideIn(this, m, -1), m, besideIn(this, m, 1)};
		for (int i = 0; i < 3; i++)
			before[i] = cs[i] ? Before{HeldModule(cs[i]), cs[i]->params[Crv::START_PARAM].getValue(),
			                           cs[i]->params[Crv::END_PARAM].getValue(), cs[i]->params[Crv::LINK_PARAM].getValue()}
			                  : Before{HeldModule(), 0.f, 0.f, 0.f};
		beforeStroke = strokeNo();
	}
	void onButton(const ButtonEvent& e) override {
		Crv* m = crv();
		const bool press = e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS;
		if (press)
			strokeNo()++;
		// The first press of what may be a double click, by LevelParam's own
		// measure: the points as they stand.
		if (press && m && firstPress(system::getTime()))
			noteBefore(m);
		// A locked press moves nothing and pushes nothing, so a double click
		// after it goes back to the point as it stands and takes back no
		// undo step of an earlier stroke.
		if (press && locked()) {
			if (engine::ParamQuantity* pq = getParamQuantity())
				firstStart = pq->getValue();
			pushed = StepKey();
		}
		// A joined point is held whole wherever it is pressed: the stroke
		// moves the shared point and parts nothing. A joined end's shared
		// point is the next start, with the next start's undo in the stroke.
		const bool joined = press && m && joinedHere(m);
		if (joined && end) {
			Crv* R = besideIn(this, m, 1);
			if (R) {
				const float was = R->params[Crv::START_PARAM].getValue();
				sharedMove = true;
				LevelParam::onButton(e);
				swept.was.clear();
				swept.noteAt(R, Crv::START_PARAM, was);
				R->wake();
				return;
			}
		}
		if (joined)
			sharedMove = true;
		LevelParam::onButton(e);
	}
	/** The diamond closing a block's loop, the last's end joined round to the
	first's start, held from the end's side: the sweep looks for the start to
	my right, and there is none, the shared point standing at the block's
	other end, so the stroke sets it itself. */
	void strokeMove(const DragMoveEvent& e) override {
		LevelParam::strokeMove(e);
		Crv* m = crv();
		if (!sharedMove || !end || !m || locked() || e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		const std::vector<Crv*> b = blockOf(this, m);
		if (b.size() >= 2 && b.back() == m)
			setFromY(pointer().y);
	}
	void onDragEnd(const DragEndEvent& e) override {
		// A locked point's stroke changes nothing but by its double click,
		// which is still one undo.
		if (locked() && e.button == GLFW_MOUSE_BUTTON_LEFT)
			if (engine::ParamQuantity* pq = getParamQuantity())
				pushed = StepKey::of(swept.push("change " + pq->getLabel()));
		LevelParam::onDragEnd(e);
		sharedMove = false;
		activePoint() = nullptr;
	}
	void onDoubleClick(const DoubleClickEvent& e) override {
		e.consume(this);
		Crv* m = crv();
		engine::ParamQuantity* pq = getParamQuantity();
		if (!m || !pq || (!end && locked()))
			return;
		doubleClicked();
		spent = true; // the rest of the stroke leaves the point where the double click put it
		pq->setValue(firstStart);
		// What the two presses did to the points about me is put back first,
		// with the gesture's undo going back to it, so the double click
		// parts or joins the point as it stood: a joined point parted
		// anywhere in its column, the shared point left where it was.
		if (strokeNo() - beforeStroke <= 2)
			for (const Before& b : before)
				if (Crv* c = b.crv.as<Crv>()) {
					swept.noteAt(c, Crv::START_PARAM, b.start);
					swept.noteAt(c, Crv::END_PARAM, b.end);
					swept.noteAt(c, Crv::LINK_PARAM, b.link);
					c->params[Crv::START_PARAM].setValue(b.start);
					c->params[Crv::END_PARAM].setValue(b.end);
					c->params[Crv::LINK_PARAM].setValue(b.link);
				}
		beforeStroke = -1;
		Crv* other = besideIn(this, m, end ? 1 : -1);
		if (!other)
			return;
		Crv* L = end ? m : other;
		Crv* R = end ? other : m;
		swept.note(R, Crv::LINK_PARAM);
		swept.note(R, Crv::START_PARAM);
		swept.note(L, Crv::END_PARAM);
		// Parted, the end stays at the shared point as it stood in force, the
		// volts IN holds where it holds the start; joined, a start IN holds
		// does not come to the end, the cable having the shared point.
		if (R->params[Crv::LINK_PARAM].getValue() > 0.5f) {
			L->params[Crv::END_PARAM].setValue(R->startInForce());
			R->params[Crv::LINK_PARAM].setValue(0.f);
		}
		else {
			// The neighbour comes to the point clicked: an end brings the next
			// start to it; a start, joined, is the shared point already.
			if (end && !R->startHeld())
				R->params[Crv::START_PARAM].setValue(L->params[Crv::END_PARAM].getValue());
			R->params[Crv::LINK_PARAM].setValue(1.f);
		}
		L->wake();
		R->wake();
		// With command held, every other join of the block made the same, so
		// a block becomes one continuous loop or falls apart into separate
		// arcs at a stroke.
		const bool join = R->params[Crv::LINK_PARAM].getValue() > 0.5f;
		if (!(join ? Hand::among(G_CRV_JOIN_ALL) : Hand::among(G_CRV_SPLIT_ALL)))
			return;
		const std::vector<Crv*> b = blockOf(this, m);
		const int n = (int)b.size();
		for (int k = 0; k < n && n >= 2; k++) {
			Crv* Rk = b[k];
			Crv* Lk = b[(k + n - 1) % n];
			if (Rk == R)
				continue;
			const bool linked = Rk->params[Crv::LINK_PARAM].getValue() > 0.5f;
			if (linked == join)
				continue;
			swept.note(Rk, Crv::LINK_PARAM);
			swept.note(Lk, Crv::END_PARAM);
			if (!join)
				Lk->params[Crv::END_PARAM].setValue(Rk->startInForce());
			Rk->params[Crv::LINK_PARAM].setValue(join ? 1.f : 0.f);
			Lk->wake();
			Rk->wake();
		}
	}
	void onEnter(const EnterEvent& e) override {
		hoveredPoint() = this;
		Hovered::onEnter(e);
	}
	void onLeave(const LeaveEvent& e) override {
		if (hoveredPoint() == this)
			hoveredPoint() = nullptr;
		Hovered::onLeave(e);
	}
	~ArcPoint() override {
		if (activePoint() == this)
			activePoint() = nullptr;
		if (hoveredPoint() == this)
			hoveredPoint() = nullptr;
	}
	/** Whether my half of a diamond is lit from my joined neighbour's side,
	its point under the pointer. */
	bool partOfDiamond(Crv* m) {
		if (!joinedHere(m))
			return false;
		Crv* partner = besideIn(this, m, end ? 1 : -1);
		ArcPoint* h = hoveredPoint();
		if (h && h != this && h->end != end && h->crv() == partner)
			return true;
		return false;
	}
	/** Whether I am the other half of `p`'s diamond. */
	bool partnerOf(ArcPoint* p) {
		Crv* m = crv();
		if (!p || p == this || p->end == end || !m || !joinedHere(m))
			return false;
		return besideIn(this, m, end ? 1 : -1) == p->crv();
	}
	/** My triangle, on my screen's edge at my point, while it is
	under the pointer, held, or half of a diamond lit from beside; painted by
	the face under the curve, so every triangle stands below the line and no
	diamond's halves stand one over it and one under. */
	void paintTriangle(NVGcontext* vg) {
		Crv* m = crv();
		if (!m)
			return;
		// While a stroke is under way only the point it is setting is lit,
		// and its other half where the stroke began on a diamond; otherwise
		// a point shows under the pointer, to say it can be taken.
		ArcPoint* d = dynamic_cast<ArcPoint*>(APP->event->getDraggedWidget());
		bool lit;
		if (d) {
			ArcPoint* act = activePoint() ? activePoint() : d;
			const bool diamond = d->sharedMove;
			lit = this == act || (diamond && partnerOf(act));
		}
		else
			lit = hovered || partOfDiamond(m);
		if (!lit)
			return;
		Arc a;
		if (!m->snapshot(a))
			return;
		const ArcZone z(box.size);
		const float y = z.y(end ? a.b : a.a);
		const float x = box.pos.x + (end ? box.size.x : 0.f), s = end ? -1.f : 1.f;
		const float tw = mm2px(1.3f), th = mm2px(1.3f);
		paintArrow(vg, x + s * tw / 2.f, y, end ? ARROW_LEFT : ARROW_RIGHT, color::alpha(color::WHITE, 0.9f), tw, th);
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		LevelParam::drawLayer(args, layer);
		if (layer != 1)
			return;
		Crv* m = crv();
		if (!m)
			return;
		const bool dragged = APP->event->getDraggedWidget() == this;
		if (face) {
			const int mine = end ? 2 : 1;
			if (hovered || dragged)
				face->showPoint = mine;
			else if (face->showPoint == mine)
				face->showPoint = 0;
		}
	}
};

inline void ArcFace::paintPointTriangles(NVGcontext* vg) {
	if (startPt)
		startPt->paintTriangle(vg);
	if (endPt)
		endPt->paintTriangle(vg);
}
