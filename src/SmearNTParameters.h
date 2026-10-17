#pragma once

// Append parameters: the first eleven indices are stored in existing NT presets.
enum SmearParameter {
    kParamInL, kParamInR, kParamOutL, kParamOutLMode, kParamOutR, kParamOutRMode,
    kParamFreq, kParamRes, kParamStages, kParamDist, kParamFb,
    kParamStereoSpread, kParamStereoMotion, kParamStereoCrossover,
    kParamClockSource, kParamTempo, kParamClockCV, kParamResetCV,
    kParamClockPPQN, kParamBeatsPerBar,
    kParamMod1Shape, kParamMod1Rate, kParamMod1Amount, kParamMod1Offset, kParamMod1Phase,
    kParamMod2Shape, kParamMod2Rate, kParamMod2Amount, kParamMod2Offset, kParamMod2Phase,
    kParamCV1Bus, kParamCV2Bus,
    kParamRoute1Source, kParamRoute1Destination, kParamRoute1Amount,
    kNumParameters = kParamRoute1Source + 8 * 3
};

enum { kRouteCount = 8, kRouteStride = 3, kDestinationCount = 8 };
enum SmearModSource { kSourceOff, kSourceMod1, kSourceMod2, kSourceCV1, kSourceCV2 };
enum SmearModDestination {
    kDestinationFrequency, kDestinationResonance, kDestinationStages,
    kDestinationDistribution, kDestinationFeedback, kDestinationSpread,
    kDestinationMotion, kDestinationCrossover
};

static_assert(kParamFb == 10 && kParamStereoSpread == 11,
              "Keep the legacy preset parameter prefix unchanged");
