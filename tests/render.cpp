// Offline render test for the DSP core. Build and run with tests/run.sh.
// Renders a few patches through the sequencer, checks they are finite, bounded
// and behave like a percussion voice, and writes WAVs to tests/out/.
#include "../src/dsp.hpp"
#include <cstdio>
#include <string>
#include <vector>

using namespace dfam;

static int failures = 0;
#define CHECK(cond, ...) \
	do { \
		if (!(cond)) { \
			failures++; \
			std::printf("  FAIL: "); \
			std::printf(__VA_ARGS__); \
			std::printf("\n"); \
		} \
	} while (0)

static void writeWav(const std::string& path, const std::vector<float>& x, int fs) {
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f)
		return;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	uint32_t bytes = x.size() * 2;
	std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16); u16(1); u16(1); u32(fs); u32(fs * 2); u16(2); u16(16);
	std::fwrite("data", 1, 4, f); u32(bytes);
	for (float s : x) {
		int16_t v = (int16_t)(clampf(s / 5.f, -1.f, 1.f) * 32767.f); // +-5 V full scale
		std::fwrite(&v, 2, 1, f);
	}
	std::fclose(f);
}

struct Patch {
	std::string name;
	VoiceParams p;
	float pitchV[STEPS] = {};
	float vel[STEPS] = {1, 1, 1, 1, 1, 1, 1, 1};
	int pitchMode = 2; // 2 = VCO 1&2, 1 = off, 0 = VCO 2
	float tempoHz = 4.f;
};

struct Stats {
	bool finite = true;
	float peak = 0.f;
	std::vector<float> stepRms; // RMS of each step's first half vs second half
	std::vector<float> stepTailRms;
};

static Stats render(const Patch& patch, float fs, float seconds, const char* wavPath) {
	Voice v;
	v.setSampleRate(fs);
	Sequencer seq;
	seq.armImmediate();
	std::vector<float> out;
	int n = (int)(fs * seconds);
	int samplesPerStep = (int)(fs / patch.tempoHz);
	Stats st;
	double headSum = 0, tailSum = 0;
	int posInStep = 0;
	for (int i = 0; i < n; i++) {
		if (seq.clock(1.f / fs, patch.tempoHz)) {
			if (i > 0) {
				st.stepRms.push_back(std::sqrt(headSum / (samplesPerStep / 8)));
				st.stepTailRms.push_back(std::sqrt(tailSum / (samplesPerStep / 8)));
			}
			headSum = tailSum = 0;
			posInStep = 0;
			int s = seq.advance();
			v.trigger(patch.vel[s]);
		}
		VoiceParams p = patch.p;
		float pv = patch.pitchV[seq.step];
		if (patch.pitchMode == 2)
			p.vco1Oct += pv;
		if (patch.pitchMode != 1)
			p.vco2Oct += pv;
		VoiceOut o = v.process(p);
		float y = o.audio * 5.f;
		if (!std::isfinite(y) || !std::isfinite(o.vco1) || !std::isfinite(o.vco2))
			st.finite = false;
		st.peak = std::max(st.peak, std::fabs(y));
		if (posInStep < samplesPerStep / 8)
			headSum += y * y;
		else if (posInStep >= samplesPerStep * 7 / 8)
			tailSum += y * y;
		posInStep++;
		out.push_back(y);
	}
	if (wavPath)
		writeWav(wavPath, out, (int)fs);
	return st;
}

static Patch kick() {
	Patch k;
	k.name = "kick";
	k.p.vco1Oct = -2.5f;
	k.p.vco1EgOct = 0.5f * map::VCO_EG_OCT;
	k.p.level1 = 0.8f;
	k.p.cutoffHz = map::cutoffHz(0.55f);
	k.p.vcfEgOct = 0.3f * map::VCF_EG_OCT;
	k.p.resonance = map::resonanceK(0.1f);
	k.p.vcoDecay = map::decaySeconds(0.25f);
	k.p.vcfDecay = map::decaySeconds(0.3f);
	k.p.vcaDecay = map::decaySeconds(0.3f);
	k.p.volume = 0.7f;
	return k;
}

int main() {
	std::system("mkdir -p tests/out");
	const float rates[] = {44100.f, 48000.f, 96000.f};

	std::vector<Patch> patches;
	patches.push_back(kick());

	Patch snare = kick();
	snare.name = "snare";
	snare.p.vco1Oct = -0.5f;
	snare.p.levelNoise = 0.9f;
	snare.p.highpass = true;
	snare.p.cutoffHz = map::cutoffHz(0.45f);
	snare.p.vcaDecay = map::decaySeconds(0.25f);
	patches.push_back(snare);

	Patch metal = kick();
	metal.name = "fm-sync-metal";
	metal.p.vco1Oct = 0.3f;
	metal.p.vco2Oct = 1.7f;
	metal.p.vco1Square = true;
	metal.p.vco2Square = true;
	metal.p.level2 = 1.f;
	metal.p.fmOct = 1.f * map::FM_OCT;
	metal.p.hardSync = true;
	metal.p.vco2EgOct = -1.f * map::VCO_EG_OCT;
	metal.p.vcfModOct = 0.5f * map::VCF_MOD_OCT;
	metal.p.levelNoise = 0.4f;
	patches.push_back(metal);

	Patch bass = kick();
	bass.name = "sequenced-bass";
	bass.p.vco1Oct = -1.f;
	bass.p.vco1EgOct = 0.f;
	bass.p.level2 = 0.6f;
	bass.p.vco2Oct = -0.98f;
	bass.p.vco2Square = true;
	float notes[STEPS] = {0, 0, 1, 0, 0.583f, 0, 0.25f, -0.417f};
	for (int i = 0; i < STEPS; i++)
		bass.pitchV[i] = notes[i];
	bass.tempoHz = 6.f;
	patches.push_back(bass);

	Patch worst = metal;
	worst.name = "worst-case";
	worst.p.level1 = worst.p.level2 = worst.p.levelNoise = 1.f;
	worst.p.resonance = map::resonanceK(1.f);
	worst.p.cutoffHz = map::cutoffHz(1.f);
	worst.p.vcfEgOct = map::VCF_EG_OCT;
	worst.p.vcfModOct = map::VCF_MOD_OCT;
	worst.p.vco1Oct = 5.f;
	worst.p.vco1EgOct = map::VCO_EG_OCT;
	worst.p.vcaCv = 1.f;
	worst.p.volume = 1.f;
	worst.tempoHz = 700.f;
	patches.push_back(worst);

	Patch worstHp = worst;
	worstHp.name = "worst-case-hp";
	worstHp.p.highpass = true;
	worstHp.p.cutoffHz = map::cutoffHz(0.f);
	worstHp.p.vcfEgOct = -map::VCF_EG_OCT;
	patches.push_back(worstHp);

	for (float fs : rates) {
		for (const Patch& pt : patches) {
			std::string wav = "tests/out/" + pt.name + "-" + std::to_string((int)fs) + ".wav";
			Stats st = render(pt, fs, 4.f, fs == 48000.f ? wav.c_str() : nullptr);
			std::printf("%-16s %6.0f Hz  peak %5.2f V\n", pt.name.c_str(), fs, st.peak);
			CHECK(st.finite, "%s @%g: non-finite output", pt.name.c_str(), fs);
			CHECK(st.peak <= 8.f, "%s @%g: peak %.2f V above the 8 V ceiling", pt.name.c_str(), fs, st.peak);
			if (pt.name.rfind("worst", 0) != 0) {
				CHECK(st.peak > 1.f, "%s @%g: peak %.2f V too quiet", pt.name.c_str(), fs, st.peak);
				// Each hit should be loud at its start and decayed by its end.
				for (size_t i = 0; i < st.stepRms.size(); i++)
					CHECK(st.stepRms[i] > 4.f * st.stepTailRms[i] + 1e-6f,
						"%s @%g step %zu: head %.3f not well above tail %.3f",
						pt.name.c_str(), fs, i, st.stepRms[i], st.stepTailRms[i]);
			}
		}
	}

	// Silence when velocity is zero: the VCA EG peak is the velocity.
	{
		Patch q = kick();
		for (float& v : q.vel)
			v = 0.f;
		Stats st = render(q, 48000.f, 1.f, nullptr);
		CHECK(st.peak < 1e-3f, "zero velocity: peak %.4f V, expected silence", st.peak);
	}

	// The kick's pitch sweep: VCO 1 starts high and falls towards its base pitch.
	{
		Voice v;
		v.setSampleRate(48000.f);
		VoiceParams p = kick().p;
		v.trigger(1.f);
		// Cycles per second over the first 20 ms vs the last 200 ms of a 1 s hit.
		int crossingsEarly = 0, crossingsLate = 0;
		float prev = 0.f;
		for (int i = 0; i < 48000; i++) {
			VoiceOut o = v.process(p);
			if (prev < 0.f && o.vco1 >= 0.f) {
				if (i < 960) crossingsEarly++;
				else if (i >= 48000 - 9600) crossingsLate++;
			}
			prev = o.vco1;
		}
		float hzEarly = crossingsEarly / 0.02f, hzLate = crossingsLate / 0.2f;
		std::printf("kick sweep: ~%.0f Hz at the hit, ~%.0f Hz at rest\n", hzEarly, hzLate);
		CHECK(hzEarly > 2.f * hzLate, "pitch sweep: %.0f Hz early vs %.0f Hz late", hzEarly, hzLate);
	}

	// Self-oscillation: high resonance with no input keeps ringing after a hit.
	{
		Voice v;
		v.setSampleRate(48000.f);
		VoiceParams p;
		p.level1 = 0.f;
		p.resonance = map::resonanceK(1.f);
		p.cutoffHz = 1000.f;
		p.vcaCv = 1.f;
		v.ladder.s[0] = 0.1f; // kick it
		float tail = 0.f;
		for (int i = 0; i < 48000; i++) {
			VoiceOut o = v.process(p);
			if (i > 47000) tail = std::max(tail, std::fabs(o.audio));
		}
		CHECK(tail > 0.1f, "self-oscillation: tail %.3f, expected sustained ringing", tail);
	}

	// Sequencer: first advance after reset is step 0, then wraps through 8.
	{
		Sequencer s;
		int seen[10];
		for (int i = 0; i < 10; i++) seen[i] = s.advance();
		CHECK(seen[0] == 0 && seen[7] == 7 && seen[8] == 0 && seen[9] == 1, "sequencer order");
		s.reset();
		CHECK(s.advance() == 0, "advance after reset");
	}

	std::printf(failures ? "\n%d FAILED\n" : "\nall checks passed\n", failures);
	return failures ? 1 : 0;
}
