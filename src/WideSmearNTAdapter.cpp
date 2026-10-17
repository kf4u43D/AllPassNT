// WideSmearNT adapter for disting NT, API v13.
// Uses the canonical DSP core located in src/widesmearnt_core/.
#include <new>
#include <cstddef>
#include <climits>
#include <cmath>
#include <distingnt/api.h>
#include "WideSmearNTParameters.h"
#include "widesmearnt_core/SmearCore.h"
#include "widesmearnt_core/Modulation.h"

struct SmearntAlgorithm : public _NT_algorithm {
    SmearCore core;
    widesmearnt::ModulationTransport transport;
    unsigned controlCounter = 0;
};

static const char* const clockSources[] = { "Internal", "CV", "MIDI" };
static const char* const ppqnLabels[] = { "1", "2", "4", "24" };
static const int ppqnValues[] = { 1, 2, 4, 24 };
static const char* const shapes[] = { "Sine", "Triangle", "Rise", "Fall", "Square" };
static const char* const rates[] = {
    "1/64", "1/32T", "1/32", "1/16T", "1/32D", "1/16", "1/8T", "1/16D",
    "1/8", "1/4T", "1/8D", "1/4", "1/4D", "1/2", "1/2D",
    "1 bar", "2 bars", "4 bars", "8 bars", "16 bars"
};
static const char* const sources[] = { "Off", "Mod 1", "Mod 2", "CV 1", "CV 2" };
static const char* const destinations[] = {
    "Frequency", "Resonance", "Stages", "Distribution", "Feedback",
    "Spread", "LR motion", "Crossover"
};

#define SMEAR_PERCENT(n, lo, d) { .name = n, .min = lo, .max = 100, .def = d, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = nullptr }
#define SMEAR_ENUM(n, labels, d) { .name = n, .min = 0, .max = ARRAY_SIZE(labels) - 1, .def = d, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = labels }
#define SMEAR_ROUTE(n) SMEAR_ENUM("Route " n " source", sources, 0), SMEAR_ENUM("Route " n " target", destinations, 0), SMEAR_PERCENT("Route " n " amount", -100, 0)

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
    SMEAR_PERCENT("Stereo spread", 0, 0),
    SMEAR_PERCENT("LR motion", -100, 0),
    { .name = "Stereo crossover", .min = 80, .max = 4000, .def = 250, .unit = kNT_unitHz, .scaling = 0, .enumStrings = nullptr },
    SMEAR_ENUM("Clock source", clockSources, 0),
    { .name = "Tempo", .min = 20, .max = 300, .def = 120, .unit = kNT_unitBPM, .scaling = 0, .enumStrings = nullptr },
    NT_PARAMETER_CV_INPUT("Clock CV", 0, 0)
    NT_PARAMETER_CV_INPUT("Reset CV", 0, 0)
    SMEAR_ENUM("Clock PPQN", ppqnLabels, 0),
    { .name = "Beats per bar", .min = 1, .max = 12, .def = 4, .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr },
    SMEAR_ENUM("Mod 1 shape", shapes, 0),
    SMEAR_ENUM("Mod 1 cycle", rates, 15),
    SMEAR_PERCENT("Mod 1 amount", -100, 100),
    SMEAR_PERCENT("Mod 1 offset", -100, 0),
    { .name = "Mod 1 phase", .min = 0, .max = 359, .def = 0, .unit = kNT_unitDegrees, .scaling = 0, .enumStrings = nullptr },
    SMEAR_ENUM("Mod 2 shape", shapes, 0),
    SMEAR_ENUM("Mod 2 cycle", rates, 17),
    SMEAR_PERCENT("Mod 2 amount", -100, 100),
    SMEAR_PERCENT("Mod 2 offset", -100, 0),
    { .name = "Mod 2 phase", .min = 0, .max = 359, .def = 90, .unit = kNT_unitDegrees, .scaling = 0, .enumStrings = nullptr },
    NT_PARAMETER_CV_INPUT("CV 1 input", 0, 0)
    NT_PARAMETER_CV_INPUT("CV 2 input", 0, 0)
    SMEAR_ROUTE("1"), SMEAR_ROUTE("2"), SMEAR_ROUTE("3"), SMEAR_ROUTE("4"),
    SMEAR_ROUTE("5"), SMEAR_ROUTE("6"), SMEAR_ROUTE("7"), SMEAR_ROUTE("8"),
};

#undef SMEAR_PERCENT
#undef SMEAR_ENUM
#undef SMEAR_ROUTE

static_assert(ARRAY_SIZE(parameters) == kNumParameters,
              "Parameter indices are part of the persisted preset schema");
static_assert(ARRAY_SIZE(rates) == widesmearnt::ModulationTransport::kRateCount &&
              ARRAY_SIZE(shapes) == widesmearnt::ModulationTransport::kShapeCount,
              "Modulator menus must match the transport tables");
static_assert(kParamMod2Shape - kParamMod1Shape == 5,
              "Each modulator uses shape, cycle, amount, offset and phase");

static const uint8_t pageMain[]    = { kParamFreq, kParamRes, kParamStages, kParamDist, kParamFb };
static const uint8_t pageRouting[] = { kParamInL, kParamInR, kParamOutL, kParamOutLMode, kParamOutR, kParamOutRMode };
static const uint8_t pageStereo[] = { kParamStereoSpread, kParamStereoMotion, kParamStereoCrossover };
static const uint8_t pageClock[] = { kParamClockSource, kParamTempo, kParamClockCV, kParamResetCV, kParamClockPPQN, kParamBeatsPerBar };
static const uint8_t pageMod1[] = { kParamMod1Shape, kParamMod1Rate, kParamMod1Amount, kParamMod1Offset, kParamMod1Phase };
static const uint8_t pageMod2[] = { kParamMod2Shape, kParamMod2Rate, kParamMod2Amount, kParamMod2Offset, kParamMod2Phase };
static const uint8_t pageCV[] = { kParamCV1Bus, kParamCV2Bus };
static const uint8_t pageRoutes[kRouteCount][kRouteStride] = {
    {32,33,34}, {35,36,37}, {38,39,40}, {41,42,43},
    {44,45,46}, {47,48,49}, {50,51,52}, {53,54,55}
};

#define SMEAR_PAGE(n, p) { .name = n, .numParams = ARRAY_SIZE(p), .group = 0, .unused = { 0, 0 }, .params = p }

static const _NT_parameterPage pages[] = {
    { .name = "Main",    .numParams = ARRAY_SIZE(pageMain),    .group = 0, .unused = { 0, 0 }, .params = pageMain },
    { .name = "Routing", .numParams = ARRAY_SIZE(pageRouting), .group = 0, .unused = { 0, 0 }, .params = pageRouting },
    SMEAR_PAGE("Stereo", pageStereo),
    SMEAR_PAGE("Clock", pageClock),
    SMEAR_PAGE("Mod 1", pageMod1),
    SMEAR_PAGE("Mod 2", pageMod2),
    SMEAR_PAGE("CV inputs", pageCV),
    SMEAR_PAGE("Route 1", pageRoutes[0]), SMEAR_PAGE("Route 2", pageRoutes[1]),
    SMEAR_PAGE("Route 3", pageRoutes[2]), SMEAR_PAGE("Route 4", pageRoutes[3]),
    SMEAR_PAGE("Route 5", pageRoutes[4]), SMEAR_PAGE("Route 6", pageRoutes[5]),
    SMEAR_PAGE("Route 7", pageRoutes[6]), SMEAR_PAGE("Route 8", pageRoutes[7]),
};

#undef SMEAR_PAGE

static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages),
    .pages = pages,
};

static void calculateRequirements(_NT_algorithmRequirements& req, const int32_t* specifications) {
    (void)specifications;
    req.numParameters = kNumParameters;
    req.sram = sizeof(SmearntAlgorithm);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

static _NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req, const int32_t* specifications) {
    (void)req;
    (void)specifications;
    SmearntAlgorithm* alg = new (ptrs.sram) SmearntAlgorithm();
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;
    // Initialise core with host sample rate.
    alg->core.init(static_cast<float>(NT_globals.sampleRate));
    alg->transport.prepare(static_cast<float>(NT_globals.sampleRate));
    return alg;
}

static void parameterChanged(_NT_algorithm* self, int p) {
    SmearntAlgorithm* alg = static_cast<SmearntAlgorithm*>(self);
    switch (p) {
    case kParamFreq:   alg->core.setFrequency(alg->v[kParamFreq] * 0.01f); break;
    case kParamRes:    alg->core.setResonance(alg->v[kParamRes] * 0.01f); break;
    case kParamStages: alg->core.setStages(alg->v[kParamStages] * 0.01f); break;
    case kParamDist:   alg->core.setDistributionMorph(alg->v[kParamDist] * 0.01f); break;
    case kParamFb:     alg->core.setFeedback(alg->v[kParamFb] * 0.01f); break;
    case kParamStereoSpread: alg->core.setStereoSpread(alg->v[p] * 0.01f); break;
    case kParamStereoMotion: alg->core.setStereoMotion(alg->v[p] * 0.01f); break;
    case kParamStereoCrossover: alg->core.setStereoCrossover(static_cast<float>(alg->v[p])); break;
    case kParamClockSource:
        alg->transport.setClockSource(static_cast<widesmearnt::ModClockSource>(alg->v[p])); break;
    case kParamTempo: alg->transport.setTempoBpm(static_cast<float>(alg->v[p])); break;
    case kParamClockPPQN: {
        const int index = alg->v[p];
        alg->transport.setCvPpqn(ppqnValues[(index >= 0 && index < 4) ? index : 0]);
        break;
    }
    case kParamBeatsPerBar: alg->transport.setBeatsPerBar(alg->v[p]); break;
    default: break;
    }
}

static float clamp(float x, float lo, float hi) {
    if (!std::isfinite(x)) return 0.0f;
    return x < lo ? lo : (x > hi ? hi : x);
}

static float cvSample(const SmearntAlgorithm* alg, int parameter,
                      const float* buses, std::size_t stride, int frame) {
    const int bus = alg->v[parameter];
    if (bus < 1 || bus > kNT_lastBus) return 0.0f;
    return clamp(buses[static_cast<std::size_t>(bus - 1) * stride +
                       static_cast<std::size_t>(frame)], -100.0f, 100.0f);
}

static void applyModulation(SmearntAlgorithm* alg, float cv1, float cv2) {
    float source[5] = { 0.0f, 0.0f, 0.0f, clamp(cv1 * 0.2f, -1.0f, 1.0f),
                       clamp(cv2 * 0.2f, -1.0f, 1.0f) };
    // A shared musical timeline keeps long LFO periods phase locked.
    for (int mod = 0; mod < 2; ++mod) {
        const int p = kParamMod1Shape + mod * 5;
        const float phase = alg->transport.phaseForRate(alg->v[p + 1], alg->v[p + 4] / 360.0f);
        const float wave = widesmearnt::ModulationTransport::waveform(alg->v[p], phase);
        source[mod + 1] = clamp(wave * (alg->v[p + 2] * 0.01f) +
                                alg->v[p + 3] * 0.01f, -1.0f, 1.0f);
    }
    float value[kDestinationCount] = {
        alg->v[kParamFreq] * 0.01f, alg->v[kParamRes] * 0.01f,
        alg->v[kParamStages] * 0.01f, alg->v[kParamDist] * 0.01f,
        alg->v[kParamFb] * 0.01f, alg->v[kParamStereoSpread] * 0.01f,
        alg->v[kParamStereoMotion] * 0.01f,
        (alg->v[kParamStereoCrossover] - 80) / 3920.0f
    };
    for (int route = 0; route < kRouteCount; ++route) {
        const int p = kParamRoute1Source + route * kRouteStride;
        const int s = alg->v[p];
        const int d = alg->v[p + 1];
        if (s > kSourceOff && s <= kSourceCV2 && d >= 0 && d < kDestinationCount)
            value[d] += source[s] * (alg->v[p + 2] * 0.01f);
    }
    alg->core.setFrequency(clamp(value[kDestinationFrequency], 0.0f, 1.0f));
    alg->core.setResonance(clamp(value[kDestinationResonance], 0.0f, 1.0f));
    alg->core.setStages(clamp(value[kDestinationStages], 0.0f, 1.0f));
    alg->core.setDistributionMorph(clamp(value[kDestinationDistribution], 0.0f, 1.0f));
    alg->core.setFeedback(clamp(value[kDestinationFeedback], 0.0f, 1.0f));
    alg->core.setStereoSpread(clamp(value[kDestinationSpread], 0.0f, 1.0f));
    alg->core.setStereoMotion(clamp(value[kDestinationMotion], -1.0f, 1.0f));
    alg->core.setStereoCrossover(80.0f + 3920.0f * clamp(value[kDestinationCrossover], 0.0f, 1.0f));
}

static int clampBus(int bus, int fallback) {
    if (bus < 1) return fallback;
    if (bus > kNT_lastBus) return fallback;
    return bus;
}

static void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    SmearntAlgorithm* alg = static_cast<SmearntAlgorithm*>(self);
    if (!busFrames || numFramesBy4 <= 0 || numFramesBy4 > INT_MAX / 4) return;
    const int numFrames = numFramesBy4 * 4;
    if (static_cast<uint32_t>(numFrames) > NT_globals.maxFramesPerStep) return;
    const std::size_t busStride = static_cast<std::size_t>(numFrames);

    const int inLBus = clampBus(alg->v[kParamInL], 1);
    const int inRBus = clampBus(alg->v[kParamInR], 2);
    const int outLBus = clampBus(alg->v[kParamOutL], 13);
    const int outRBus = clampBus(alg->v[kParamOutR], 14);

    float* inL = busFrames + static_cast<std::size_t>(inLBus - 1) * busStride;
    float* inR = busFrames + static_cast<std::size_t>(inRBus - 1) * busStride;
    float* outL = busFrames + static_cast<std::size_t>(outLBus - 1) * busStride;
    float* outR = busFrames + static_cast<std::size_t>(outRBus - 1) * busStride;

    const bool replaceL = (alg->v[kParamOutLMode] != 0);
    const bool replaceR = (alg->v[kParamOutRMode] != 0);
    for (int i = 0; i < numFrames; ++i) {
        const float dryL = inL[i];
        const float dryR = inR[i];
        const float previousOutL = outL[i];
        const float previousOutR = outR[i];
        // Read all control inputs before writing any potentially aliased output.
        const float clock = cvSample(alg, kParamClockCV, busFrames, busStride, i);
        const float reset = cvSample(alg, kParamResetCV, busFrames, busStride, i);
        alg->transport.processSample(clock, reset);
        if (alg->controlCounter == 0) {
            const float cv1 = cvSample(alg, kParamCV1Bus, busFrames, busStride, i);
            const float cv2 = cvSample(alg, kParamCV2Bus, busFrames, busStride, i);
            applyModulation(alg, cv1, cv2);
        }
        alg->controlCounter = (alg->controlCounter + 1U) & 31U;
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

static void midiRealtime(_NT_algorithm* self, uint8_t byte) {
    static_cast<SmearntAlgorithm*>(self)->transport.midiRealtime(byte);
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('S', 'M', 'N', 'T'),
    .name = "WideSmearNT",
    .description = "Spectral smear, synced modulation and spectral stereo",
    .numSpecifications = 0,
    .specifications = nullptr,
    .calculateStaticRequirements = nullptr,
    .initialise = nullptr,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = parameterChanged,
    .step = step,
    .draw = nullptr,
    .midiRealtime = midiRealtime,
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
