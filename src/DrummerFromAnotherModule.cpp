#include "plugin.hpp"
#include "dsp.hpp"
#include "layout.hpp"

struct DecayQuantity : ParamQuantity {
	float getDisplayValue() override {
		return dfam::map::decaySeconds(getValue());
	}
	void setDisplayValue(float s) override {
		setValue(std::log(std::max(s, 0.001f) / 0.01f) / std::log(1000.f));
	}
	std::string getUnit() override {
		return " s";
	}
};

struct CutoffQuantity : ParamQuantity {
	float getDisplayValue() override {
		return dfam::map::cutoffHz(getValue());
	}
	void setDisplayValue(float hz) override {
		setValue(std::log(std::max(hz, 1.f) / 20.f) / std::log(1000.f));
	}
	std::string getUnit() override {
		return " Hz";
	}
};

struct TempoQuantity : ParamQuantity {
	float getDisplayValue() override {
		return dfam::map::tempoHz(getValue());
	}
	void setDisplayValue(float hz) override {
		setValue(std::log(std::max(hz, 0.01f) / 0.7f) / std::log(1000.f));
	}
	std::string getDisplayValueString() override {
		float hz = getDisplayValue();
		return string::f("%.2f Hz (%.0f BPM in 16ths)", hz, hz * 15.f);
	}
};

struct DrummerFromAnotherModule : Module {
	enum ParamId {
		TRIGGER_BUTTON_PARAM,
		VCO_DECAY_PARAM,
		SEQ_PITCH_MOD_PARAM,
		VCO1_EG_AMT_PARAM,
		VCO1_FREQ_PARAM,
		VCO1_WAVE_PARAM,
		FM_AMT_PARAM,
		VCO2_EG_AMT_PARAM,
		VCO2_FREQ_PARAM,
		VCO2_WAVE_PARAM,
		HARD_SYNC_PARAM,
		TEMPO_PARAM,
		RUN_PARAM,
		ADVANCE_BUTTON_PARAM,
		VCO1_LEVEL_PARAM,
		NOISE_LEVEL_PARAM,
		VCO2_LEVEL_PARAM,
		CUTOFF_PARAM,
		RESONANCE_PARAM,
		VCF_MODE_PARAM,
		VCF_DECAY_PARAM,
		VCF_EG_AMT_PARAM,
		NOISE_VCF_MOD_PARAM,
		VCA_ATTACK_PARAM,
		VCA_DECAY_PARAM,
		VOLUME_PARAM,
		ENUMS(PITCH_PARAMS, dfam::STEPS),
		ENUMS(VEL_PARAMS, dfam::STEPS),
		PARAMS_LEN
	};
	enum InputId {
		TRIGGER_INPUT,
		VCA_CV_INPUT,
		VELOCITY_INPUT,
		VCA_DECAY_INPUT,
		EXT_AUDIO_INPUT,
		VCF_DECAY_INPUT,
		NOISE_LEVEL_INPUT,
		VCO_DECAY_INPUT,
		VCF_MOD_INPUT,
		VCO1_CV_INPUT,
		FM_AMT_INPUT,
		VCO2_CV_INPUT,
		TEMPO_INPUT,
		RUN_STOP_INPUT,
		ADV_CLOCK_INPUT,
		RESET_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		VCA_OUTPUT,
		VCA_EG_OUTPUT,
		VCF_EG_OUTPUT,
		VCO_EG_OUTPUT,
		VCO1_OUTPUT,
		VCO2_OUTPUT,
		TRIGGER_OUTPUT,
		VELOCITY_OUTPUT,
		PITCH_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		ENUMS(STEP_LIGHTS, dfam::STEPS),
		RUN_LIGHT,
		LIGHTS_LEN
	};

	enum PitchMode { PITCH_VCO2 = 0, PITCH_OFF = 1, PITCH_BOTH = 2 };

	dfam::Voice voice;
	dfam::Sequencer seq;

	dsp::SchmittTrigger triggerButton, triggerInput;
	dsp::SchmittTrigger advanceButton, advClockInput;
	dsp::SchmittTrigger runStopInput, resetInput;
	dsp::PulseGenerator triggerPulse;
	bool wasRunning = false;

	// Rack extras in the context menu. The defaults match the hardware.
	float pitchRangeV = 5.f;
	bool quantizePitch = false;

	DrummerFromAnotherModule() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configButton(TRIGGER_BUTTON_PARAM, "Trigger envelopes");
		configParam<DecayQuantity>(VCO_DECAY_PARAM, 0.f, 1.f, 0.25f, "VCO decay");
		configSwitch(SEQ_PITCH_MOD_PARAM, 0.f, 2.f, 2.f, "Sequencer pitch mod", {"VCO 2", "Off", "VCO 1 & 2"});
		configParam(VCO1_EG_AMT_PARAM, -1.f, 1.f, 0.5f, "VCO 1 EG amount", "%", 0.f, 100.f);
		configParam(VCO1_FREQ_PARAM, -5.f, 5.f, -2.5f, "VCO 1 frequency", " Hz", 2.f, dfam::FREQ_C4);
		configSwitch(VCO1_WAVE_PARAM, 0.f, 1.f, 0.f, "VCO 1 wave", {"Triangle", "Square"});
		configParam(FM_AMT_PARAM, 0.f, 1.f, 0.f, "1→2 FM amount", "%", 0.f, 100.f);
		configParam(VCO2_EG_AMT_PARAM, -1.f, 1.f, 0.f, "VCO 2 EG amount", "%", 0.f, 100.f);
		configParam(VCO2_FREQ_PARAM, -5.f, 5.f, 0.f, "VCO 2 frequency", " Hz", 2.f, dfam::FREQ_C4);
		configSwitch(VCO2_WAVE_PARAM, 0.f, 1.f, 1.f, "VCO 2 wave", {"Triangle", "Square"});
		configSwitch(HARD_SYNC_PARAM, 0.f, 1.f, 0.f, "Hard sync (VCO 2 to VCO 1)", {"Off", "On"});

		configParam<TempoQuantity>(TEMPO_PARAM, 0.f, 1.f, 0.3527f, "Tempo"); // 8 Hz = 120 BPM in 16ths
		configSwitch(RUN_PARAM, 0.f, 1.f, 0.f, "Run/stop", {"Stopped", "Running"});
		configButton(ADVANCE_BUTTON_PARAM, "Advance");

		configParam(VCO1_LEVEL_PARAM, 0.f, 1.f, 0.8f, "VCO 1 level", "%", 0.f, 100.f);
		configParam(NOISE_LEVEL_PARAM, 0.f, 1.f, 0.f, "Noise / ext level", "%", 0.f, 100.f);
		configParam(VCO2_LEVEL_PARAM, 0.f, 1.f, 0.f, "VCO 2 level", "%", 0.f, 100.f);
		configParam<CutoffQuantity>(CUTOFF_PARAM, 0.f, 1.f, 0.55f, "Cutoff");
		configParam(RESONANCE_PARAM, 0.f, 1.f, 0.1f, "Resonance", "%", 0.f, 100.f);
		configSwitch(VCF_MODE_PARAM, 0.f, 1.f, 0.f, "VCF mode", {"Low pass", "High pass"});
		configParam<DecayQuantity>(VCF_DECAY_PARAM, 0.f, 1.f, 0.3f, "VCF decay");
		configParam(VCF_EG_AMT_PARAM, -1.f, 1.f, 0.3f, "VCF EG amount", "%", 0.f, 100.f);
		configParam(NOISE_VCF_MOD_PARAM, 0.f, 1.f, 0.f, "Noise / VCF mod", "%", 0.f, 100.f);
		configSwitch(VCA_ATTACK_PARAM, 0.f, 1.f, 0.f, "VCA EG attack", {"Fast (1 ms)", "Slow (100 ms)"});
		configParam<DecayQuantity>(VCA_DECAY_PARAM, 0.f, 1.f, 0.3f, "VCA decay");
		configParam(VOLUME_PARAM, 0.f, 1.f, 0.7f, "Volume", "%", 0.f, 100.f);

		for (int i = 0; i < dfam::STEPS; i++) {
			configParam(PITCH_PARAMS + i, -1.f, 1.f, 0.f, string::f("Step %d pitch", i + 1));
			configParam(VEL_PARAMS + i, 0.f, 1.f, 0.75f, string::f("Step %d velocity", i + 1), "%", 0.f, 100.f);
		}

		configInput(TRIGGER_INPUT, "Trigger (fires envelopes, no advance)");
		configInput(VCA_CV_INPUT, "VCA CV (sums with VCA EG, 0-8 V)");
		configInput(VELOCITY_INPUT, "Velocity (replaces step velocity, 0-5 V)");
		configInput(VCA_DECAY_INPUT, "VCA decay CV");
		configInput(EXT_AUDIO_INPUT, "External audio (replaces noise in the mixer)");
		configInput(VCF_DECAY_INPUT, "VCF decay CV");
		configInput(NOISE_LEVEL_INPUT, "Noise / ext level CV");
		configInput(VCO_DECAY_INPUT, "VCO decay CV");
		configInput(VCF_MOD_INPUT, "VCF mod (replaces noise as the cutoff mod source)");
		configInput(VCO1_CV_INPUT, "VCO 1 pitch (1 V/oct)");
		configInput(FM_AMT_INPUT, "1→2 FM amount CV");
		configInput(VCO2_CV_INPUT, "VCO 2 pitch (1 V/oct)");
		configInput(TEMPO_INPUT, "Tempo (1 V/oct)");
		configInput(RUN_STOP_INPUT, "Run/stop (trigger toggles)");
		configInput(ADV_CLOCK_INPUT, "Advance/clock (patching stops the internal clock)");
		configInput(RESET_INPUT, "Reset (next clock plays step 1)");

		configOutput(VCA_OUTPUT, "VCA (audio)");
		configOutput(VCA_EG_OUTPUT, "VCA EG");
		configOutput(VCF_EG_OUTPUT, "VCF EG");
		configOutput(VCO_EG_OUTPUT, "VCO EG");
		configOutput(VCO1_OUTPUT, "VCO 1");
		configOutput(VCO2_OUTPUT, "VCO 2");
		configOutput(TRIGGER_OUTPUT, "Trigger");
		configOutput(VELOCITY_OUTPUT, "Velocity (0-5 V)");
		configOutput(PITCH_OUTPUT, "Pitch");

		configBypass(EXT_AUDIO_INPUT, VCA_OUTPUT);
		updatePitchQuantities();
	}

	void updatePitchQuantities() {
		for (int i = 0; i < dfam::STEPS; i++) {
			ParamQuantity* pq = paramQuantities[PITCH_PARAMS + i];
			pq->unit = " V";
			pq->displayMultiplier = pitchRangeV;
			pq->snapEnabled = false;
		}
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		voice.setSampleRate(e.sampleRate);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		seq.reset();
		pitchRangeV = 5.f;
		quantizePitch = false;
		updatePitchQuantities();
	}

	float stepPitchV(int step) {
		float v = params[PITCH_PARAMS + step].getValue() * pitchRangeV;
		if (quantizePitch)
			v = std::round(v * 12.f) / 12.f;
		return v;
	}

	float velocityV() {
		if (inputs[VELOCITY_INPUT].isConnected())
			return inputs[VELOCITY_INPUT].getVoltage();
		return params[VEL_PARAMS + seq.step].getValue() * 5.f;
	}

	void fireStep() {
		triggerPulse.trigger(1e-3f);
		voice.trigger(velocityV() / 5.f);
	}

	float knobPlusCv(int param, int input) {
		return params[param].getValue() + inputs[input].getVoltage() / 10.f;
	}

	void process(const ProcessArgs& args) override {
		// Transport. RUN/STOP toggles from the button or a trigger; stopping keeps the step.
		if (runStopInput.process(inputs[RUN_STOP_INPUT].getVoltage(), 0.1f, 1.f))
			params[RUN_PARAM].setValue(params[RUN_PARAM].getValue() > 0.5f ? 0.f : 1.f);
		bool running = params[RUN_PARAM].getValue() > 0.5f;
		bool externalClock = inputs[ADV_CLOCK_INPUT].isConnected();
		if (running && !wasRunning)
			seq.armImmediate();
		wasRunning = running;

		if (resetInput.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f))
			seq.reset();

		bool advance = advanceButton.process(params[ADVANCE_BUTTON_PARAM].getValue());
		if (advClockInput.process(inputs[ADV_CLOCK_INPUT].getVoltage(), 0.1f, 1.f))
			advance = true;
		if (running && !externalClock) {
			float rate = dfam::map::tempoHz(params[TEMPO_PARAM].getValue())
				* std::exp2(inputs[TEMPO_INPUT].getVoltage());
			if (seq.clock(args.sampleTime, std::min(rate, 0.25f * args.sampleRate)))
				advance = true;
		}
		if (advance) {
			seq.advance();
			fireStep();
		}

		// TRIGGER fires the envelopes at the current step without advancing.
		bool trig = triggerButton.process(params[TRIGGER_BUTTON_PARAM].getValue());
		if (triggerInput.process(inputs[TRIGGER_INPUT].getVoltage(), 0.1f, 1.f))
			trig = true;
		if (trig && !advance)
			voice.trigger(velocityV() / 5.f);

		// Voice controls.
		using namespace dfam;
		float pitchV = stepPitchV(seq.step);
		int pitchMode = (int)std::round(params[SEQ_PITCH_MOD_PARAM].getValue());

		VoiceParams p;
		p.vco1Oct = params[VCO1_FREQ_PARAM].getValue() + inputs[VCO1_CV_INPUT].getVoltage()
			+ (pitchMode == PITCH_BOTH ? pitchV : 0.f);
		p.vco2Oct = params[VCO2_FREQ_PARAM].getValue() + inputs[VCO2_CV_INPUT].getVoltage()
			+ (pitchMode != PITCH_OFF ? pitchV : 0.f);
		p.vco1EgOct = params[VCO1_EG_AMT_PARAM].getValue() * map::VCO_EG_OCT;
		p.vco2EgOct = params[VCO2_EG_AMT_PARAM].getValue() * map::VCO_EG_OCT;
		p.vco1Square = params[VCO1_WAVE_PARAM].getValue() > 0.5f;
		p.vco2Square = params[VCO2_WAVE_PARAM].getValue() > 0.5f;
		p.fmOct = clampf(knobPlusCv(FM_AMT_PARAM, FM_AMT_INPUT), 0.f, 1.f) * map::FM_OCT;
		p.hardSync = params[HARD_SYNC_PARAM].getValue() > 0.5f;

		p.level1 = params[VCO1_LEVEL_PARAM].getValue();
		p.levelNoise = clampf(knobPlusCv(NOISE_LEVEL_PARAM, NOISE_LEVEL_INPUT), 0.f, 1.f);
		p.level2 = params[VCO2_LEVEL_PARAM].getValue();
		p.extAudioConnected = inputs[EXT_AUDIO_INPUT].isConnected();
		p.extAudio = inputs[EXT_AUDIO_INPUT].getVoltage() / 5.f;

		p.cutoffHz = map::cutoffHz(params[CUTOFF_PARAM].getValue());
		p.resonance = map::resonanceK(params[RESONANCE_PARAM].getValue());
		p.highpass = params[VCF_MODE_PARAM].getValue() > 0.5f;
		p.vcfEgOct = params[VCF_EG_AMT_PARAM].getValue() * map::VCF_EG_OCT;
		p.vcfModOct = params[NOISE_VCF_MOD_PARAM].getValue() * map::VCF_MOD_OCT;
		p.vcfModConnected = inputs[VCF_MOD_INPUT].isConnected();
		p.vcfMod = inputs[VCF_MOD_INPUT].getVoltage() / 5.f;

		p.vcoDecay = map::decaySeconds(knobPlusCv(VCO_DECAY_PARAM, VCO_DECAY_INPUT));
		p.vcfDecay = map::decaySeconds(knobPlusCv(VCF_DECAY_PARAM, VCF_DECAY_INPUT));
		p.vcaDecay = map::decaySeconds(knobPlusCv(VCA_DECAY_PARAM, VCA_DECAY_INPUT));
		p.vcaAttack = params[VCA_ATTACK_PARAM].getValue() > 0.5f ? map::ATTACK_SLOW : map::ATTACK_FAST;
		p.vcaCv = std::max(inputs[VCA_CV_INPUT].getVoltage(), 0.f) / 8.f;
		p.volume = params[VOLUME_PARAM].getValue();

		VoiceOut o = voice.process(p);

		outputs[VCA_OUTPUT].setVoltage(o.audio * 5.f);
		outputs[VCO1_OUTPUT].setVoltage(o.vco1 * 5.f);
		outputs[VCO2_OUTPUT].setVoltage(o.vco2 * 5.f);
		outputs[VCO_EG_OUTPUT].setVoltage(o.vcoEnv * 8.f);
		outputs[VCF_EG_OUTPUT].setVoltage(o.vcfEnv * 8.f);
		outputs[VCA_EG_OUTPUT].setVoltage(o.vcaEnv * 8.f);
		outputs[TRIGGER_OUTPUT].setVoltage(triggerPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[VELOCITY_OUTPUT].setVoltage(params[VEL_PARAMS + seq.step].getValue() * 5.f);
		outputs[PITCH_OUTPUT].setVoltage(pitchV);

		for (int i = 0; i < dfam::STEPS; i++)
			lights[STEP_LIGHTS + i].setBrightnessSmooth(i == seq.step ? 1.f : 0.f, args.sampleTime);
		lights[RUN_LIGHT].setBrightness(running ? 1.f : 0.f);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "step", json_integer(seq.step));
		json_object_set_new(root, "pitchRangeV", json_real(pitchRangeV));
		json_object_set_new(root, "quantizePitch", json_boolean(quantizePitch));
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* j = json_object_get(root, "step"))
			seq.step = clamp((int)json_integer_value(j), 0, dfam::STEPS - 1);
		if (json_t* j = json_object_get(root, "pitchRangeV"))
			pitchRangeV = json_number_value(j);
		if (json_t* j = json_object_get(root, "quantizePitch"))
			quantizePitch = json_boolean_value(j);
		updatePitchQuantities();
	}
};

static Vec at(const float (&mm)[2]) {
	return mm2px(Vec(mm[0], mm[1]));
}

// Component artwork is generated by tools/make_panel.py into res/components/.
static std::shared_ptr<window::Svg> partSvg(const std::string& name) {
	return Svg::load(asset::plugin(pluginInstance, "res/components/" + name + ".svg"));
}

// Large knob: static skirt, rotating flutes and pointer, static aluminium cap on top.
struct MoogKnob : app::SvgKnob {
	MoogKnob() {
		minAngle = -0.83f * M_PI;
		maxAngle = 0.83f * M_PI;
		setSvg(partSvg("KnobBig_fg"));
		widget::SvgWidget* bg = new widget::SvgWidget;
		bg->setSvg(partSvg("KnobBig_bg"));
		fb->addChildBelow(bg, tw);
		widget::SvgWidget* cap = new widget::SvgWidget;
		cap->setSvg(partSvg("KnobBig_cap"));
		fb->addChildAbove(cap, tw);
	}
};

// Small knurled knob used by the mixer and the sequencer.
struct MoogSmallKnob : app::SvgKnob {
	MoogSmallKnob() {
		minAngle = -0.83f * M_PI;
		maxAngle = 0.83f * M_PI;
		setSvg(partSvg("KnobSmall_fg"));
		widget::SvgWidget* bg = new widget::SvgWidget;
		bg->setSvg(partSvg("KnobSmall_bg"));
		fb->addChildBelow(bg, tw);
	}
};

// Bat toggles. Frame index is the param value, so frame 0 is the bottom position.
struct BatToggle2 : app::SvgSwitch {
	BatToggle2() {
		shadow->opacity = 0.f;
		addFrame(partSvg("Toggle_down"));
		addFrame(partSvg("Toggle_up"));
	}
};

// For VCA EG, which reads FAST on top while the param keeps 0 = fast.
struct BatToggle2Flipped : app::SvgSwitch {
	BatToggle2Flipped() {
		shadow->opacity = 0.f;
		addFrame(partSvg("Toggle_up"));
		addFrame(partSvg("Toggle_down"));
	}
};

struct BatToggle3 : app::SvgSwitch {
	BatToggle3() {
		shadow->opacity = 0.f;
		addFrame(partSvg("Toggle_down"));
		addFrame(partSvg("Toggle_mid"));
		addFrame(partSvg("Toggle_up"));
	}
};

struct MoogButton : app::SvgSwitch {
	MoogButton(const std::string& colour, bool latching) {
		momentary = !latching;
		latch = latching;
		addFrame(partSvg("Button_" + colour + "_0"));
		addFrame(partSvg("Button_" + colour + "_1"));
	}
};
struct GreyButton : MoogButton {
	GreyButton() : MoogButton("grey", false) {}
};
struct BlackButton : MoogButton {
	BlackButton() : MoogButton("black", false) {}
};
struct RedLatchButton : MoogButton {
	RedLatchButton() : MoogButton("red", true) {}
};

struct HexJack : app::SvgPort {
	HexJack() {
		setSvg(partSvg("Jack"));
	}
};

// Red LED with a dim lens when off, like the hardware's step LEDs.
struct DfamLed : MediumLight<RedLight> {
	DfamLed() {
		bgColor = nvgRGB(0x4e, 0x18, 0x1c);
		borderColor = nvgRGBA(0, 0, 0, 0x70);
	}
};

struct DrummerFromAnotherModuleWidget : ModuleWidget {
	using M = DrummerFromAnotherModule;

	DrummerFromAnotherModuleWidget(M* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/DrummerFromAnotherModule.svg")));

		using namespace layout;
		for (const auto& screw : SCREW)
			addChild(createWidgetCentered<ScrewBlack>(at(screw)));

		addParam(createParamCentered<MoogKnob>(at(VCO_DECAY), module, M::VCO_DECAY_PARAM));
		addParam(createParamCentered<BatToggle3>(at(SEQ_PITCH_MOD), module, M::SEQ_PITCH_MOD_PARAM));
		addParam(createParamCentered<MoogKnob>(at(VCO1_EG_AMT), module, M::VCO1_EG_AMT_PARAM));
		addParam(createParamCentered<MoogKnob>(at(VCO1_FREQ), module, M::VCO1_FREQ_PARAM));
		addParam(createParamCentered<BatToggle2>(at(VCO1_WAVE), module, M::VCO1_WAVE_PARAM));
		addParam(createParamCentered<MoogKnob>(at(FM_AMT), module, M::FM_AMT_PARAM));
		addParam(createParamCentered<BatToggle2>(at(HARD_SYNC), module, M::HARD_SYNC_PARAM));
		addParam(createParamCentered<MoogKnob>(at(VCO2_EG_AMT), module, M::VCO2_EG_AMT_PARAM));
		addParam(createParamCentered<MoogKnob>(at(VCO2_FREQ), module, M::VCO2_FREQ_PARAM));
		addParam(createParamCentered<BatToggle2>(at(VCO2_WAVE), module, M::VCO2_WAVE_PARAM));

		addParam(createParamCentered<MoogSmallKnob>(at(VCO1_LEVEL), module, M::VCO1_LEVEL_PARAM));
		addParam(createParamCentered<MoogSmallKnob>(at(NOISE_LEVEL), module, M::NOISE_LEVEL_PARAM));
		addParam(createParamCentered<MoogSmallKnob>(at(VCO2_LEVEL), module, M::VCO2_LEVEL_PARAM));

		addParam(createParamCentered<BatToggle2>(at(VCF_MODE), module, M::VCF_MODE_PARAM));
		addParam(createParamCentered<MoogKnob>(at(CUTOFF), module, M::CUTOFF_PARAM));
		addParam(createParamCentered<MoogKnob>(at(RESONANCE), module, M::RESONANCE_PARAM));
		addParam(createParamCentered<BatToggle2Flipped>(at(VCA_ATTACK), module, M::VCA_ATTACK_PARAM));
		addParam(createParamCentered<MoogKnob>(at(VOLUME), module, M::VOLUME_PARAM));
		addParam(createParamCentered<MoogKnob>(at(VCF_DECAY), module, M::VCF_DECAY_PARAM));
		addParam(createParamCentered<MoogKnob>(at(VCF_EG_AMT), module, M::VCF_EG_AMT_PARAM));
		addParam(createParamCentered<MoogKnob>(at(NOISE_VCF_MOD), module, M::NOISE_VCF_MOD_PARAM));
		addParam(createParamCentered<MoogKnob>(at(VCA_DECAY), module, M::VCA_DECAY_PARAM));

		addParam(createParamCentered<MoogKnob>(at(TEMPO), module, M::TEMPO_PARAM));
		addParam(createParamCentered<GreyButton>(at(TRIGGER_BUTTON), module, M::TRIGGER_BUTTON_PARAM));
		addParam(createParamCentered<RedLatchButton>(at(RUN), module, M::RUN_PARAM));
		addChild(createLightCentered<DfamLed>(at(RUN_LIGHT), module, M::RUN_LIGHT));
		addParam(createParamCentered<BlackButton>(at(ADVANCE_BUTTON), module, M::ADVANCE_BUTTON_PARAM));

		for (int i = 0; i < dfam::STEPS; i++) {
			addChild(createLightCentered<DfamLed>(at(STEP_LIGHT[i]), module, M::STEP_LIGHTS + i));
			addParam(createParamCentered<MoogSmallKnob>(at(PITCH_KNOB[i]), module, M::PITCH_PARAMS + i));
			addParam(createParamCentered<MoogSmallKnob>(at(VEL_KNOB[i]), module, M::VEL_PARAMS + i));
		}

		addInput(createInputCentered<HexJack>(at(TRIGGER_INPUT), module, M::TRIGGER_INPUT));
		addInput(createInputCentered<HexJack>(at(VCA_CV_INPUT), module, M::VCA_CV_INPUT));
		addInput(createInputCentered<HexJack>(at(VELOCITY_INPUT), module, M::VELOCITY_INPUT));
		addInput(createInputCentered<HexJack>(at(VCA_DECAY_INPUT), module, M::VCA_DECAY_INPUT));
		addInput(createInputCentered<HexJack>(at(EXT_AUDIO_INPUT), module, M::EXT_AUDIO_INPUT));
		addInput(createInputCentered<HexJack>(at(VCF_DECAY_INPUT), module, M::VCF_DECAY_INPUT));
		addInput(createInputCentered<HexJack>(at(NOISE_LEVEL_INPUT), module, M::NOISE_LEVEL_INPUT));
		addInput(createInputCentered<HexJack>(at(VCO_DECAY_INPUT), module, M::VCO_DECAY_INPUT));
		addInput(createInputCentered<HexJack>(at(VCF_MOD_INPUT), module, M::VCF_MOD_INPUT));
		addInput(createInputCentered<HexJack>(at(VCO1_CV_INPUT), module, M::VCO1_CV_INPUT));
		addInput(createInputCentered<HexJack>(at(FM_AMT_INPUT), module, M::FM_AMT_INPUT));
		addInput(createInputCentered<HexJack>(at(VCO2_CV_INPUT), module, M::VCO2_CV_INPUT));
		addInput(createInputCentered<HexJack>(at(TEMPO_INPUT), module, M::TEMPO_INPUT));
		addInput(createInputCentered<HexJack>(at(RUN_STOP_INPUT), module, M::RUN_STOP_INPUT));
		addInput(createInputCentered<HexJack>(at(ADV_CLOCK_INPUT), module, M::ADV_CLOCK_INPUT));
		addInput(createInputCentered<HexJack>(at(RESET_INPUT), module, M::RESET_INPUT));

		addOutput(createOutputCentered<HexJack>(at(VCA_OUTPUT), module, M::VCA_OUTPUT));
		addOutput(createOutputCentered<HexJack>(at(VCA_EG_OUTPUT), module, M::VCA_EG_OUTPUT));
		addOutput(createOutputCentered<HexJack>(at(VCF_EG_OUTPUT), module, M::VCF_EG_OUTPUT));
		addOutput(createOutputCentered<HexJack>(at(VCO_EG_OUTPUT), module, M::VCO_EG_OUTPUT));
		addOutput(createOutputCentered<HexJack>(at(VCO1_OUTPUT), module, M::VCO1_OUTPUT));
		addOutput(createOutputCentered<HexJack>(at(VCO2_OUTPUT), module, M::VCO2_OUTPUT));
		addOutput(createOutputCentered<HexJack>(at(TRIGGER_OUTPUT), module, M::TRIGGER_OUTPUT));
		addOutput(createOutputCentered<HexJack>(at(VELOCITY_OUTPUT), module, M::VELOCITY_OUTPUT));
		addOutput(createOutputCentered<HexJack>(at(PITCH_OUTPUT), module, M::PITCH_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		M* module = getModule<M>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Rack extras"));
		menu->addChild(createIndexSubmenuItem("Step pitch range",
			{"±5 V (hardware)", "±2 V", "±1 V"},
			[=]() { return module->pitchRangeV >= 5.f ? 0 : module->pitchRangeV >= 2.f ? 1 : 2; },
			[=](int i) {
				module->pitchRangeV = (i == 0) ? 5.f : (i == 1) ? 2.f : 1.f;
				module->updatePitchQuantities();
			}));
		menu->addChild(createBoolPtrMenuItem("Quantize step pitch to semitones", "", &module->quantizePitch));
	}
};

Model* modelDrummerFromAnotherModule = createModel<DrummerFromAnotherModule, DrummerFromAnotherModuleWidget>("DrummerFromAnotherModule");
