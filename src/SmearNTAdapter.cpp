// SmearNT adapter for disting NT, API v13.
// Uses the canonical DSP core located in src/smearnt_core/.
#include <new>
#include <cstddef>
#include <distingnt/api.h>
#include "smearnt_core/SmearCore.h"

struct _smearntAlgorithm : public _NT_algorithm {
    SmearCore core;
};

enum {
    kParamInL,
    kParamInR,
    kParamOutL,
    kParamOutLMode,
    kParamOutR,
    kParamOutRMode,
    kParamFreq,
    kParamRes,
    kParamStages,
    kParamDist,
    kParamFb,
};

static const _NT_parameter parameters[] = {
    NT_PARAMETER_AUDIO_INPUT("In L", 1, 1)
    NT_PARAMETER_AUDIO_INPUT("In R", 1, 2)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Out L", 1, 13)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Out R", 1, 14)
    { .name = "Frequency", .min = 0, .max = 100, .def = 50, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr },
    { .name = "Resonance", .min = 0, .max = 100, .def = 50, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr },
    { .name = "Stages", .min = 0, .max = 100, .def = 50, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr },
    { .name = "Distribution", .min = 0, .max = 100, .def = 50, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr },
    { .name = "Feedback", .min = 0, .max = 100, .def = 20, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr },
};

static const uint8_t pageMain[]    = { kParamFreq, kParamRes, kParamStages, kParamDist, kParamFb };
static const uint8_t pageRouting[] = { kParamInL, kParamInR, kParamOutL, kParamOutLMode, kParamOutR, kParamOutRMode };

static const _NT_parameterPage pages[] = {
    { .name = "Main",    .numParams = ARRAY_SIZE(pageMain),    .params = pageMain },
    { .name = "Routing", .numParams = ARRAY_SIZE(pageRouting), .params = pageRouting },
};

static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages),
    .pages = pages,
};

static void calculateRequirements(_NT_algorithmRequirements& req, const int32_t* specifications) {
    (void)specifications;
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_smearntAlgorithm);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

static _NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req, const int32_t* specifications) {
    (void)req;
    (void)specifications;
    _smearntAlgorithm* alg = new (ptrs.sram) _smearntAlgorithm();
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;
    // Initialise core with host sample rate.
    alg->core.init(static_cast<float>(NT_globals.sampleRate));
    return alg;
}

static void parameterChanged(_NT_algorithm* self, int p) {
    _smearntAlgorithm* alg = (_smearntAlgorithm*)self;
    switch (p) {
    case kParamFreq:   alg->core.setFrequency(alg->v[kParamFreq] * 0.01f); break;
    case kParamRes:    alg->core.setResonance(alg->v[kParamRes] * 0.01f); break;
    case kParamStages: alg->core.setStages(alg->v[kParamStages] * 0.01f); break;
    case kParamDist:   alg->core.setDistributionMorph(alg->v[kParamDist] * 0.01f); break;
    case kParamFb:     alg->core.setFeedback(alg->v[kParamFb] * 0.01f); break;
    default: break;
    }
}

static int clampBus(int bus, int fallback) {
    if (bus < 1) return fallback;
    if (bus > kNT_lastBus) return fallback;
    return bus;
}

static void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    _smearntAlgorithm* alg = (_smearntAlgorithm*)self;
    const int numFrames = numFramesBy4 * 4;
    if (!busFrames || numFrames <= 0) return;

    const int inLBus = clampBus(alg->v[kParamInL], 1);
    const int inRBus = clampBus(alg->v[kParamInR], 2);
    const int outLBus = clampBus(alg->v[kParamOutL], 13);
    const int outRBus = clampBus(alg->v[kParamOutR], 14);

    float* inL = busFrames + (inLBus - 1) * numFrames;
    float* inR = busFrames + (inRBus - 1) * numFrames;
    float* outL = busFrames + (outLBus - 1) * numFrames;
    float* outR = busFrames + (outRBus - 1) * numFrames;

    // Ensure parameters up to date each block.
    parameterChanged(self, kParamFreq);
    parameterChanged(self, kParamRes);
    parameterChanged(self, kParamStages);
    parameterChanged(self, kParamDist);
    parameterChanged(self, kParamFb);

    const bool replaceL = (alg->v[kParamOutLMode] != 0);
    const bool replaceR = (alg->v[kParamOutRMode] != 0);
    for (int i = 0; i < numFrames; ++i) {
        const float dryL = inL[i];
        const float dryR = inR[i];
        const float previousOutL = outL[i];
        const float previousOutR = outR[i];
        float wetL = 0.0f;
        float wetR = 0.0f;

        alg->core.processSample(dryL, dryR, wetL, wetR);

        if (outL == outR) {
            const bool replace = replaceL || replaceR;
            const float wet = 0.5f * (wetL + wetR);
            outL[i] = replace ? wet : (previousOutL + wet);
        } else {
            outL[i] = replaceL ? wetL : (previousOutL + wetL);
            outR[i] = replaceR ? wetR : (previousOutR + wetR);
        }
    }
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('S', 'M', 'N', 'T'),
    .name = "SmearNT",
    .description = "Spectral smear allpass network",
    .numSpecifications = 0,
    .specifications = nullptr,
    .calculateStaticRequirements = nullptr,
    .initialise = nullptr,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = parameterChanged,
    .step = step,
    .draw = nullptr,
    .midiRealtime = nullptr,
    .midiMessage = nullptr,
    .tags = kNT_tagEffect | kNT_tagFilterEQ,
    .hasCustomUi = nullptr,
    .customUi = nullptr,
    .setupUi = nullptr,
    .serialise = nullptr,
    .deserialise = nullptr,
    .midiSysEx = nullptr,
    .parameterUiPrefix = nullptr,
    .parameterString = nullptr,
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
    case kNT_selector_version: return kNT_apiVersionCurrent;
    case kNT_selector_numFactories: return 1;
    case kNT_selector_factoryInfo: return (data == 0) ? (uintptr_t)&factory : 0;
    }
    return 0;
}
