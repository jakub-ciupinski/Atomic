// Atomic, a VCV Rack plugin. Copyright (C) 2026 Jakub Ciupinski.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "plugin.hpp"
#include <dsp/minblep.hpp>

/** A band-limited step, the edge a Seq puts on its jack under a Clock in
hertz (`Count::hertz`), its BLEP on. A step from one note to the next is a corner on
a sample, and at audio rate a corner is every harmonic up past the Nyquist
folded back as noise; MinBLEP adds, over the few samples after the edge, the
difference between that corner and a step with nothing above the Nyquist in
it, placed at the moment the edge fell between two samples. Minimum phase, so
it adds no latency: the edge is heard on the sample it falls in, as it was.
Rack's own `MinBlepGenerator` works its impulse out in every instance, and a
Seq holds one a channel, so the impulse here is one table for all of them. */

namespace rig {

struct Blep {
	static constexpr int Z = 16, O = 32; // zero-crossings, and the oversampling of the table
	static constexpr int LEN = 2 * Z;
	/** The step's impulse, worked out once. Two ones past its end, so an
	edge a whole frame back reads its last point and the one after it. */
	static const float* impulse() {
		static const std::vector<float> table = [] {
			std::vector<float> t(LEN * O + 2, 1.f);
			dsp::minBlepImpulse(Z, O, t.data());
			return t;
		}();
		return table.data();
	}
	float buf[LEN] = {};
	int pos = 0;
	int left = 0; // the samples still owed a correction, nought once it has played out

	/** A step of `x` at `p`, minus one to nought: how far before the
	current frame it fell, which is already the step's value. */
	void insert(float p, float x) {
		const float* imp = impulse();
		for (int j = 0; j < LEN; j++) {
			const float i = ((float)j - p) * O;
			buf[(pos + j) % LEN] += x * (math::interpolateLinear(imp, i) - 1.f);
		}
		left = LEN;
	}
	/** The correction for this frame, which the naive value is added to. */
	float process() {
		const float v = buf[pos];
		buf[pos] = 0.f;
		pos = (pos + 1) % LEN;
		if (left > 0)
			left--;
		return v;
	}
	void clear() {
		for (float& b : buf)
			b = 0.f;
		left = 0;
	}
};

/** The way back down from four times the rate, for a Seq oversampling at
audio rate: four points a frame in, one out, through a lowpass at seven
eighths of the Nyquist, which is what a curve bending faster than a sample can
follow needs and a BLEP alone cannot give it. Minimum phase: the magnitude of a 128-tap Blackman-Harris windowed sinc,
so its aliasing and its top, and about three frames of delay where the
linear-phase sinc had sixteen, which moved a Seq switched to it 31 degrees at
middle C against a stock VCO; the treble comes a few frames later still, and
an edge rings after itself only. The table is that sinc made minimum phase
through its real cepstrum, computed offline. A held value
comes out as exactly itself: once every point in the filter is the one value,
nothing is summed, so a step standing still under the Clock reads its own
volts. */
struct Decimate4 {
	static constexpr int Q = 32, TAPS = 4 * Q;
	// Computed offline: the linear-phase kernel turned minimum phase.
	static constexpr float KERNEL[TAPS] = {
	    5.5689033e-06f, 4.89416503e-05f, 0.000238531215f, 0.000844057925f,
	    0.00240617225f, 0.00582963527f, 0.0123881495f, 0.0235561949f,
	    0.0406099559f, 0.0640166503f, 0.0927474738f, 0.123763951f,
	    0.151981091f, 0.170947671f, 0.174283714f, 0.157617208f,
	    0.120467793f, 0.0673688985f, 0.00761386449f, -0.0466143488f,
	    -0.0834053722f, -0.0946916637f, -0.0790266065f, -0.0424765252f,
	    0.00290979495f, 0.042742879f, 0.0649065605f, 0.0635400258f,
	    0.0409106854f, 0.00642169416f, -0.0269366215f, -0.0473874844f,
	    -0.0486056036f, -0.0317810273f, -0.0047541582f, 0.0213644578f,
	    0.0366781f, 0.0362702384f, 0.0217793511f, 0.000257662323f,
	    -0.0190722779f, -0.0287100632f, -0.0257741875f, -0.012771566f,
	    0.00392514673f, 0.0170771715f, 0.0216725269f, 0.0167949376f,
	    0.00558258128f, -0.00652894974f, -0.0143607537f, -0.0151623267f,
	    -0.00950162966f, -0.000624374154f, 0.00726956555f, 0.0109565223f,
	    0.00945617156f, 0.00415870941f, -0.00210230205f, -0.00648690716f,
	    -0.0073916913f, -0.00497231722f, -0.00081222514f, 0.00299364888f,
	    0.00484482152f, 0.00426569115f, 0.00193351466f, -0.000815191615f,
	    -0.00269363576f, -0.00303243476f, -0.00197107407f, -0.000249352702f,
	    0.00123684771f, 0.00187031614f, 0.00153957674f, 0.000591505956f,
	    -0.000417529141f, -0.00101846592f, -0.00102705379f, -0.000569487158f,
	    4.14900048e-05f, 0.0004915347f, 0.00061111383f, 0.000422401704f,
	    8.57148377e-05f, -0.000207300801f, -0.000331135592f, -0.000270062987f,
	    -9.92786241e-05f, 7.29692735e-05f, 0.000165182861f, 0.000154919998f,
	    7.45340337e-05f, -1.86060979e-05f, -7.64689173e-05f, -8.12235595e-05f,
	    -4.57625252e-05f, 1.07887048e-06f, 3.31863396e-05f, 3.93178161e-05f,
	    2.43986696e-05f, 2.31482031e-06f, -1.37351372e-05f, -1.76952565e-05f,
	    -1.15250423e-05f, -1.6900391e-06f, 5.58746687e-06f, 7.46013004e-06f,
	    4.83026403e-06f, 6.40862518e-07f, -2.35053392e-06f, -2.97049527e-06f,
	    -1.72951855e-06f, 1.19524197e-08f, 1.073319e-06f, 1.06877469e-06f,
	    3.89745728e-07f, -2.65493376e-07f, -4.38465412e-07f, -1.91127199e-07f,
	    7.28071373e-08f, 1.07608989e-07f, 3.6319866e-08f, -4.06045962e-09f,
	    -2.18219165e-08f, -6.45742542e-10f, -1.12753926e-09f, 1.84299315e-09f,
	};
	static const float* kernel() { return KERNEL; }
	// The last TAPS points, twice over so a frame's window is one run.
	float buf[2 * TAPS] = {};
	int pos = 0;
	int same = 0; // how many points running, the newest back, are the newest's value
	/** The four points of a frame, oldest first; the frame's value. */
	float process(const float* x) {
		for (int j = 0; j < 4; j++) {
			same = buf[(pos + TAPS - 1) % TAPS] == x[j] ? std::min(same + 1, TAPS) : 1;
			buf[pos] = buf[pos + TAPS] = x[j];
			pos = (pos + 1) % TAPS;
		}
		if (same >= TAPS)
			return x[3];
		const float* k = kernel();
		const float* b = buf + pos; // oldest first, so the kernel newest first
		float o = 0.f;
		for (int i = 0; i < TAPS; i++)
			o += k[TAPS - 1 - i] * b[i];
		return o;
	}
	/** The frames until a value held from now comes out as itself. */
	int settling() const { return (TAPS - same + 3) / 4; }
	/** Filled with `v`, as if it had stood there for ever. */
	void fill(float v) {
		for (float& b : buf)
			b = v;
		same = TAPS;
	}
};

/** What a Seq hears at PHASE, brought up to four times the rate for its
Oversampling (`OVER_HZ_PARAM`), so the curves it reads between two samples are
read where the jack really was then, not on a straight line from one sample
to the next, whose corners folded through a fast curve were most of what
aliased (a fold rig went from -31 dB to -60 under 16 kHz). Eight samples in, four points a
frame out, through a minimum-phase lowpass at 0.95 of the jack's Nyquist:
about 1.6 frames late, a delay no filter that fills between samples can
avoid, since it must hear past the point it fills. The
volts are the jack's as they come, wrapped only after (`Seq::place`), and a
step of more than half a turn between two samples, ten volts being a whole
turn, is taken as the shorter way round: a phasor dropping from ten to nought
or from ten to minus ten, stands where it stood, the same place, rather than
being smeared into a sweep back through the whole cycle. The sum is of each sample's distance from the newest, unwrapped a sample
at a time, and each phase of the kernel sums to one, so a PHASE standing still
comes out as exactly itself and nothing is summed. The table is a
Blackman-Harris windowed sinc at 0.95 of the input's Nyquist, eight taps a
phase, made minimum phase through its real cepstrum, computed offline. */
struct PhaseUp4 {
	static constexpr int K = 8, TAPS = 4 * K;
	// Computed offline: the linear-phase kernel turned minimum phase, each
	// phase summing to one; 1.58 frames late at the bottom, 1.63 at a tenth
	// of the Nyquist.
	static constexpr float KERNEL[TAPS] = {
	    0.00123116127f, 0.00989759431f, 0.0422896444f, 0.124888383f,
	    0.281232072f, 0.506148995f, 0.744583883f, 0.900216104f,
	    0.88280109f, 0.669237869f, 0.331823727f, 0.00448814552f,
	    -0.194331184f, -0.226895213f, -0.143587186f, -0.0340166441f,
	    0.0346059085f, 0.047001669f, 0.0267501136f, 0.00407252187f,
	    -0.00616937237f, -0.00561388794f, -0.00183645054f, 0.000387507241f,
	    0.000635177621f, 0.000221587749f, -2.40264709e-05f, -3.60315871e-05f,
	    -4.85318836e-06f, 1.3860396e-06f, 2.95088909e-07f, 1.39659369e-08f,
	};
	double ring[K] = {}; // the last K volts heard, the newest at `pos`
	int pos = 0;
	int same = 0;       // how many, the newest back, are the newest's volts
	int64_t frame = -2; // the frame the newest was heard on
	/** The volts heard on `f`; a frame missed, or the first, and the
	filter stands on them as if they had stood there for ever. */
	void push(double v, int64_t f) {
		if (f != frame + 1) {
			for (double& r : ring)
				r = v;
			same = K;
		}
		else {
			same = ring[pos] == v ? std::min(same + 1, K) : 1;
			pos = (pos + 1) % K;
			ring[pos] = v;
		}
		frame = f;
	}
	/** Whether the points are the newest volts, every one. */
	bool settled() const { return same >= K; }
	/** The four points of the frame heard last, oldest first. */
	void points(double* out) const {
		double u[K];
		u[0] = 0.0;
		for (int k = 1; k < K; k++) {
			const double newer = ring[(pos - k + 1 + K) % K], older = ring[(pos - k + K) % K];
			u[k] = u[k - 1] - std::remainder(newer - older, 10.0);
		}
		for (int j = 0; j < 4; j++) {
			double o = 0.0;
			for (int k = 1; k < K; k++)
				o += (double)KERNEL[4 * k + j] * u[k];
			out[j] = ring[pos] + o;
		}
	}
	void forget() { frame = -2; }
};

} // namespace rig
