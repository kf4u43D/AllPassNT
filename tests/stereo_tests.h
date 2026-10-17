#pragma once

#include "smearnt_core/AllpassSection.h"
#include "smearnt_core/SmearCore.h"
#include <cmath>
#include <cstdio>
#include <limits>

namespace smearnt_stereo_tests {

inline void check(bool condition, const char* expression, int line, int& failures) {
    if (!condition) {
        std::fprintf(stderr, "FAIL [Stereo DSP:%d] %s\n", line, expression);
        ++failures;
    }
}

#define STEREO_CHECK(expression) check((expression), #expression, __LINE__, failures)

inline bool stable(float a1, float a2) {
    return 1.0f + a1 + a2 > 0.0f && 1.0f - a1 + a2 > 0.0f &&
           a2 > -1.0f && a2 < 1.0f;
}

inline void testExtendedPoles(int& failures) {
    const float rates[] = { 8000.0f, 48000.0f, 384000.0f };
    for (float rate : rates) {
        const float frequencies[] = { 20.0f, 2000.0f, 0.45f * rate };
        for (float frequency : frequencies) {
            AllpassSection section;
            section.init(rate);
            section.setPole(frequency, 0.97f);
            STEREO_CHECK(stable(section.getA1(), section.getA2()));
            STEREO_CHECK(std::fabs(section.getA2() - 0.9409f) < 1.0e-6f);
            STEREO_CHECK(1.0f + section.getA1() + section.getA2() > 0.0008f);
            double energy = 0.0;
            bool finite = true;
            for (int i = 0; i < 32768; ++i) {
                const float value = section.process(i == 0 ? 1.0f : 0.0f);
                finite = finite && std::isfinite(value);
                energy += static_cast<double>(value) * value;
            }
            STEREO_CHECK(finite);
            STEREO_CHECK(std::fabs(energy - 1.0) < 0.001);

            section.setPoleTarget(frequency == 20.0f ? 0.45f * rate : 20.0f,
                                  0.97f, 32);
            bool stableRamp = true;
            for (int i = 0; i < 32; ++i) {
                section.advancePoleInterpolation(i == 31);
                stableRamp = stableRamp && stable(section.getA1(), section.getA2());
            }
            STEREO_CHECK(stableRamp);
        }
    }
}

inline void testBypassAndReset(int& failures) {
    SmearCore reference;
    SmearCore controlled;
    reference.init(48000.0f);
    controlled.init(48000.0f);
    bool exact = true;
    for (int i = 0; i < 16384; ++i) {
        if ((i & 31) == 0) {
            controlled.setStereoSpread(0.0f);
            controlled.setStereoMotion((i & 128) ? -1.0f : 1.0f);
            controlled.setStereoCrossover((i & 64) ? 80.0f : 4000.0f);
        }
        const float inL = 0.1f * std::sin(0.071f * static_cast<float>(i));
        const float inR = 0.1f * std::cos(0.037f * static_cast<float>(i));
        float aL, aR, bL, bR;
        reference.processSample(inL, inR, aL, aR);
        controlled.processSample(inL, inR, bL, bR);
        exact = exact && aL == bL && aR == bR;
    }
    STEREO_CHECK(exact);

    controlled.setStereoSpread(1.0f);
    for (int i = 0; i < 8192; ++i) {
        float left, right;
        controlled.processSample(0.1f, -0.1f, left, right);
    }
    reference.init(96000.0f);
    controlled.init(96000.0f);
    exact = true;
    for (int i = 0; i < 4096; ++i) {
        const float input = i == 0 ? 1.0f : 0.0f;
        float aL, aR, bL, bR;
        reference.processSample(input, input, aL, aR);
        controlled.processSample(input, input, bL, bR);
        exact = exact && aL == bL && aR == bR;
    }
    STEREO_CHECK(exact);
}

struct ToneEnergy {
    double left = 0.0;
    double right = 0.0;
    double side = 0.0;
};

inline ToneEnergy renderTone(float hz, bool mono, float spread, float motion) {
    SmearCore core;
    core.init(48000.0f);
    core.setFeedback(0.0f);
    core.setFrequency(1.0f);
    core.setDistributionMorph(0.0f);
    core.setStages(1.0f);
    core.setStereoSpread(spread);
    core.setStereoMotion(motion);
    core.setStereoCrossover(500.0f);
    ToneEnergy energy;
    for (int i = 0; i < 24576; ++i) {
        const float input = 0.1f * std::sin(6.283185307179586f * hz *
                                          static_cast<float>(i) / 48000.0f);
        float left, right;
        core.processSample(input, mono ? input : -input, left, right);
        if (i >= 8192) {
            energy.left += static_cast<double>(left) * left;
            energy.right += static_cast<double>(right) * right;
            const double side = 0.5 * (static_cast<double>(left) - right);
            energy.side += side * side;
        }
    }
    return energy;
}

inline void testSpectralStereo(int& failures) {
    const ToneEnergy lowOriginal = renderTone(30.0f, false, 0.0f, 0.0f);
    const ToneEnergy lowWide = renderTone(30.0f, false, 1.0f, 0.0f);
    STEREO_CHECK(lowOriginal.side > 1.0);
    STEREO_CHECK(lowWide.side < 0.1 * lowOriginal.side);

    const ToneEnergy highOriginal = renderTone(6000.0f, true, 0.0f, 0.0f);
    const ToneEnergy highWide = renderTone(6000.0f, true, 1.0f, 0.0f);
    STEREO_CHECK(highWide.side > highOriginal.side + 1.0);
    STEREO_CHECK(highWide.left + highWide.right <
                 1.3 * (highOriginal.left + highOriginal.right));

    const ToneEnergy movedLeft = renderTone(6000.0f, true, 1.0f, -1.0f);
    const ToneEnergy movedRight = renderTone(6000.0f, true, 1.0f, 1.0f);
    STEREO_CHECK(movedLeft.left > movedLeft.right);
    STEREO_CHECK(movedRight.right > movedRight.left);
}

inline void testMovingStability(int& failures) {
    const float rates[] = { 8000.0f, 48000.0f, 384000.0f };
    for (float rate : rates) {
        SmearCore core;
        core.init(rate);
        core.setResonance(1.0f);
        core.setFeedback(1.0f);
        core.setStereoSpread(1.0f);
        bool finite = true;
        float peak = 0.0f;
        for (int i = 0; i < 65536; ++i) {
            if ((i & 31) == 0) {
                const float ramp = static_cast<float>((i / 32) % 64) / 63.0f;
                core.setFrequency((i & 2048) ? 1.0f - ramp : ramp);
                core.setDistributionMorph((i & 512) ? 0.0f : 1.0f);
                core.setStages((i & 256) ? 0.0f : 1.0f);
                core.setStereoMotion((i & 128) ? -1.0f : 1.0f);
                core.setStereoCrossover((i & 1024) ? 80.0f : 4000.0f);
            }
            const float input = (i & 255) == 0 ? 0.5f :
                0.1f * std::sin(0.127f * static_cast<float>(i));
            float left, right;
            core.processSample(input, -0.7f * input, left, right);
            finite = finite && std::isfinite(left) && std::isfinite(right);
            const float magnitude = std::fmax(std::fabs(left), std::fabs(right));
            if (magnitude > peak) peak = magnitude;
        }
        STEREO_CHECK(finite);
        if (peak >= 128.0f) std::fprintf(stderr, "Moving DSP peak %.9g at %.0f Hz\n", peak, rate);
        STEREO_CHECK(peak < 128.0f);

        core.setFrequency(0.0f);
        core.setDistributionMorph(0.5f);
        core.setStages(1.0f);
        core.setStereoMotion(0.0f);
        float tailPeak = 0.0f;
        for (int i = 0; i < 65536; ++i) {
            float left, right;
            core.processSample(0.0f, 0.0f, left, right);
            finite = finite && std::isfinite(left) && std::isfinite(right);
            if (i >= 64512) tailPeak = std::fmax(tailPeak,
                std::fmax(std::fabs(left), std::fabs(right)));
        }
        STEREO_CHECK(finite);
        STEREO_CHECK(tailPeak < 0.001f);
    }
}

inline void testStereoExtremeRecovery(int& failures) {
    SmearCore core;
    core.init(48000.0f);
    core.setResonance(1.0f);
    core.setFrequency(1.0f);
    core.setFeedback(1.0f);
    core.setStages(1.0f);
    core.setStereoSpread(1.0f);
    core.setStereoMotion(1.0f);
    const float extreme = std::numeric_limits<float>::max();
    bool finite = true;
    for (int i = 0; i < 2048; ++i) {
        float left, right;
        core.processSample(extreme, -extreme, left, right);
        finite = finite && std::isfinite(left) && std::isfinite(right);
    }
    float peak = 0.0f;
    for (int i = 0; i < 16384; ++i) {
        float left, right;
        const float input = 0.1f * std::sin(0.017f * static_cast<float>(i));
        core.processSample(input, input, left, right);
        finite = finite && std::isfinite(left) && std::isfinite(right);
        if (i >= 15360) peak = std::fmax(peak,
            std::fmax(std::fabs(left), std::fabs(right)));
    }
    STEREO_CHECK(finite);
    STEREO_CHECK(peak < 16.0f);
    core.setStereoSpread(NAN);
    core.setStereoMotion(INFINITY);
    core.setStereoCrossover(NAN);
    float left, right;
    core.processSample(NAN, INFINITY, left, right);
    STEREO_CHECK(std::isfinite(left) && std::isfinite(right));
}

#undef STEREO_CHECK

} // namespace smearnt_stereo_tests

inline int runStereoTests() {
    int failures = 0;
    smearnt_stereo_tests::testExtendedPoles(failures);
    smearnt_stereo_tests::testBypassAndReset(failures);
    smearnt_stereo_tests::testSpectralStereo(failures);
    smearnt_stereo_tests::testMovingStability(failures);
    smearnt_stereo_tests::testStereoExtremeRecovery(failures);
    return failures;
}
