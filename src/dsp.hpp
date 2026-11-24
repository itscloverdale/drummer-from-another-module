#pragma once
// Drummer from Another Module: voice and sequencer DSP.
// Plain C++ with no Rack dependency, so tests/render.cpp can drive it offline.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dfam {

constexpr int OVERSAMPLE = 4;
constexpr int STEPS = 8;
constexpr float FREQ_C4 = 261.6256f;
constexpr float TWO_PI = 6.28318530718f;
constexpr float LN100 = 4.60517018599f;

inline float clampf(float x, float lo, float hi) {
	return std::min(std::max(x, lo), hi);
}

// Pade approximant, exact at +-3 and clamped beyond, so the ladder stays bounded.
inline float fastTanh(float x) {
	x = clampf(x, -3.f, 3.f);
	float x2 = x * x;
	return x * (27.f + x2) / (27.f + 9.f * x2);
}

// Knob (0..1) to physical units. Shared by the module and the offline test.
namespace map {
inline float decaySeconds(float k) {
	return 0.01f * std::pow(1000.f, clampf(k, 0.f, 1.f)); // 10 ms .. 10 s to -40 dB
}
inline float cutoffHz(float k) {
	return 20.f * std::pow(1000.f, k); // 20 Hz .. 20 kHz
}
inline float tempoHz(float k) {
	return 0.7f * std::pow(1000.f, k); // 0.7 .. 700 Hz, as on the hardware
}
inline float resonanceK(float k) {
	return 5.f * clampf(k, 0.f, 1.f); // LP self-oscillates above 3 o'clock (k = 4)
}
constexpr float VCO_EG_OCT = 5.f;    // VCO EG AMOUNT at +-100%
constexpr float VCF_EG_OCT = 7.f;    // VCF EG AMOUNT at +-100%
constexpr float FM_OCT = 4.f;        // 1->2 FM AMOUNT at 100%, per unit of VCO 1
constexpr float VCF_MOD_OCT = 4.f;   // NOISE/VCF MOD at 100%, per unit of source
constexpr float ATTACK_FAST = 0.001f;
constexpr float ATTACK_SLOW = 0.1f;
// Higher velocity lengthens decays slightly (DFAM manual).
inline float velocityDecayScale(float peak) {
	return 0.85f + 0.3f * peak;
}
} // namespace map

struct Noise {
	uint32_t state = 0x9E3779B9u;
	float next() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return (int32_t)state * (1.f / 2147483648.f);
	}
};

inline float polyBlep(float t, float dt) {
	if (t < dt) {
		t /= dt;
		return t + t - t * t - 1.f;
	}
	if (t > 1.f - dt) {
		t = (t - 1.f) / dt;
		return t * t + t + t + 1.f;
	}
	return 0.f;
}

struct Vco {
	float phase = 0.f;

	// Advances one oversampled tick. On wrap, wrapFrac is how far past the wrap
	// the phase landed, as a fraction of this tick (used to place hard sync).
	bool advance(float dt, float& wrapFrac) {
		phase += dt;
		if (phase >= 1.f) {
			phase -= std::floor(phase);
			wrapFrac = phase / dt;
			return true;
		}
		return false;
	}

	void syncTo(float wrapFrac, float dt) {
		phase = wrapFrac * dt;
	}

	float out(bool square, float dt) const {
		if (!square)
			return 1.f - 4.f * std::fabs(phase - 0.5f);
		float y = phase < 0.5f ? 1.f : -1.f;
		y += polyBlep(phase, dt);
		float p2 = phase + 0.5f;
		if (p2 >= 1.f)
			p2 -= 1.f;
		y -= polyBlep(p2, dt);
		return y;
	}
};

// Four saturating one-pole stages with global feedback (Huovilainen style).
// High-pass is the binomial mix of the stage outputs.
struct Ladder {
	float s[4] = {};
	float t[4] = {};

	float process(float x, float g, float k, bool highpass) {
		float in = fastTanh(x - k * s[3]);
		s[0] += g * (in - t[0]);
		t[0] = fastTanh(s[0]);
		s[1] += g * (t[0] - t[1]);
		t[1] = fastTanh(s[1]);
		s[2] += g * (t[1] - t[2]);
		t[2] = fastTanh(s[2]);
		s[3] += g * (t[2] - t[3]);
		t[3] = fastTanh(s[3]);
		if (highpass)
			return in - 4.f * t[0] + 6.f * t[1] - 4.f * t[2] + t[3];
		return s[3];
	}
};

// Instant attack, exponential decay. The peak is the step's velocity.
struct DecayEnv {
	float v = 0.f;
	float peak = 0.f;
	void trigger(float p) {
		v = peak = p;
	}
	float process(float dt, float decayTime) {
		v *= std::exp(-dt * LN100 / (decayTime * map::velocityDecayScale(peak)));
		return v;
	}
};

// Linear attack (FAST/SLOW), then exponential decay.
struct AttackDecayEnv {
	float v = 0.f;
	float peak = 0.f;
	bool attacking = false;
	void trigger(float p) {
		peak = p;
		attacking = true;
	}
	float process(float dt, float attackTime, float decayTime) {
		if (attacking) {
			v += dt / attackTime;
			if (v >= peak) {
				v = std::max(peak, v - dt / attackTime);
				attacking = false;
			}
		}
		else {
			v *= std::exp(-dt * LN100 / (decayTime * map::velocityDecayScale(peak)));
		}
		return v;
	}
};

struct Biquad {
	float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
	float z1 = 0.f, z2 = 0.f;
	void setLowpass(float fc, float fs, float q) {
		float w0 = TWO_PI * fc / fs;
		float c = std::cos(w0);
		float alpha = std::sin(w0) / (2.f * q);
		float a0 = 1.f + alpha;
		b0 = (1.f - c) / 2.f / a0;
		b1 = (1.f - c) / a0;
		b2 = b0;
		a1 = -2.f * c / a0;
		a2 = (1.f - alpha) / a0;
	}
	float process(float x) {
		float y = b0 * x + z1;
		z1 = b1 * x - a1 * y + z2;
		z2 = b2 * x - a2 * y;
		return y;
	}
};

// 8th-order Butterworth low-pass, then keep every OVERSAMPLE-th sample.
struct Decimator {
	Biquad bq[4];
	void init(float fsBase) {
		static const float Q[4] = {0.50980f, 0.60134f, 0.89998f, 2.56292f};
		for (int i = 0; i < 4; i++)
			bq[i].setLowpass(0.42f * fsBase, fsBase * OVERSAMPLE, Q[i]);
	}
	float process(const float* x) {
		float y = 0.f;
		for (int n = 0; n < OVERSAMPLE; n++) {
			y = x[n];
			for (auto& b : bq)
				y = b.process(y);
		}
		return y;
	}
};

// Control values in physical units, resolved by the caller once per sample.
struct VoiceParams {
	float vco1Oct = 0.f;      // octaves from C4, before EG and FM
	float vco2Oct = 0.f;
	float vco1EgOct = 0.f;    // octaves at full envelope
	float vco2EgOct = 0.f;
	bool vco1Square = false;
	bool vco2Square = false;
	float fmOct = 0.f;        // VCO 2 octaves per unit of VCO 1
	bool hardSync = false;    // VCO 2 resets on each VCO 1 cycle
	float level1 = 0.f;
	float levelNoise = 0.f;   // noise, or EXT AUDIO when patched
	float level2 = 0.f;
	float cutoffHz = 1000.f;
	float vcfEgOct = 0.f;
	float vcfModOct = 0.f;    // octaves per unit of VCF MOD source
	float resonance = 0.f;    // ladder feedback k
	bool highpass = false;
	float vcoDecay = 0.2f;    // seconds
	float vcfDecay = 0.2f;
	float vcaDecay = 0.3f;
	float vcaAttack = map::ATTACK_FAST;
	float volume = 1.f;

	bool extAudioConnected = false;
	float extAudio = 0.f;     // normalised, +-1 = +-5 V
	bool vcfModConnected = false;
	float vcfMod = 0.f;       // normalised, +-1 = +-5 V
	float vcaCv = 0.f;        // summed with the VCA EG; 1 = +8 V
};

struct VoiceOut {
	float audio = 0.f;   // post-VCA, 1 = 5 V, soft-limited to 8 V
	float vco1 = 0.f;
	float vco2 = 0.f;
	float vcoEnv = 0.f;  // 0..1
	float vcfEnv = 0.f;
	float vcaEnv = 0.f;
};

struct Voice {
	Vco vco1, vco2;
	Ladder ladder;
	Noise noise;
	DecayEnv vcoEg, vcfEg;
	AttackDecayEnv vcaEg;
	Decimator decAudio, decVco1, decVco2;
	float fsBase = 48000.f;

	void setSampleRate(float fs) {
		fsBase = fs;
		decAudio.init(fs);
		decVco1.init(fs);
		decVco2.init(fs);
	}

	void trigger(float velocity) {
		float peak = clampf(velocity, 0.f, 1.f);
		vcoEg.trigger(peak);
		vcfEg.trigger(peak);
		vcaEg.trigger(peak);
	}

	VoiceOut process(const VoiceParams& p) {
		const float dtBase = 1.f / fsBase;
		const float fsOs = fsBase * OVERSAMPLE;
		const float maxHz = 0.45f * fsOs;
		const float maxCutoff = std::min(24000.f, 0.2f * fsOs);

		VoiceOut o;
		o.vcoEnv = vcoEg.process(dtBase, p.vcoDecay);
		o.vcfEnv = vcfEg.process(dtBase, p.vcfDecay);
		o.vcaEnv = vcaEg.process(dtBase, p.vcaAttack, p.vcaDecay);
		// VCA CV sums with the envelope and the VCA clips at full gain.
		float vca = clampf(o.vcaEnv + p.vcaCv, 0.f, 1.f);

		float f1 = clampf(FREQ_C4 * std::exp2(p.vco1Oct + p.vco1EgOct * o.vcoEnv), 0.1f, maxHz);
		float dt1 = f1 / fsOs;
		float vco2BaseOct = p.vco2Oct + p.vco2EgOct * o.vcoEnv;
		float cutoffBaseOct = std::log2(clampf(p.cutoffHz, 1.f, 30000.f)) + p.vcfEgOct * o.vcfEnv;

		float bufAudio[OVERSAMPLE], buf1[OVERSAMPLE], buf2[OVERSAMPLE];
		for (int n = 0; n < OVERSAMPLE; n++) {
			float wrapFrac = 0.f;
			bool wrapped = vco1.advance(dt1, wrapFrac);
			float s1 = vco1.out(p.vco1Square, dt1);

			float f2 = clampf(FREQ_C4 * std::exp2(vco2BaseOct + p.fmOct * s1), 0.1f, maxHz);
			float dt2 = f2 / fsOs;
			float unused;
			vco2.advance(dt2, unused);
			if (p.hardSync && wrapped)
				vco2.syncTo(wrapFrac, dt2);
			float s2 = vco2.out(p.vco2Square, dt2);

			float nz = noise.next();
			float src3 = p.extAudioConnected ? p.extAudio : nz;
			float mix = p.level1 * s1 + p.levelNoise * src3 + p.level2 * s2;

			float modSrc = p.vcfModConnected ? p.vcfMod : nz;
			float fc = clampf(std::exp2(cutoffBaseOct + p.vcfModOct * modSrc), 8.f, maxCutoff);
			float g = 1.f - std::exp(-TWO_PI * fc / fsOs);
			float y = ladder.process(mix, g, p.resonance, p.highpass);

			bufAudio[n] = y * vca * p.volume;
			buf1[n] = s1;
			buf2[n] = s2;
		}
		// Output gain with a soft ceiling at 8 V (1 = 5 V).
		o.audio = 1.6f * fastTanh(1.25f * decAudio.process(bufAudio));
		o.vco1 = decVco1.process(buf1);
		o.vco2 = decVco2.process(buf2);
		return o;
	}
};

// 8-step sequencer with an internal clock. After reset, the next advance plays step 1.
struct Sequencer {
	int step = 0;
	bool pendingFirst = true;
	float phase = 0.f;

	int advance() {
		if (pendingFirst)
			pendingFirst = false;
		else
			step = (step + 1) % STEPS;
		return step;
	}
	void reset() {
		step = 0;
		pendingFirst = true;
		phase = 0.f;
	}
	// Returns true when the internal clock ticks.
	bool clock(float dt, float rateHz) {
		phase += dt * rateHz;
		if (phase >= 1.f) {
			phase -= std::floor(phase);
			return true;
		}
		return false;
	}
	// Makes the next clock() call tick, so starting plays a step at once.
	void armImmediate() {
		phase = 1.f;
	}
};

} // namespace dfam
