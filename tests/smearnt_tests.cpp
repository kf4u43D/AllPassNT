#include <distingnt/api.h>

#include "SmearNTParameters.h"
#include "smearnt_core/AllpassNetwork.h"
#include "smearnt_core/AllpassSection.h"
#include "smearnt_core/ParamSmoother.h"
#include "smearnt_core/SmearCore.h"
#include "modulation_tests.h"
#include "stereo_tests.h"

#include <array>
#include <cmath>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

int gFailures = 0;

void check(bool condition, const char* expression, const char* test, int line) {
    if (!condition) {
        std::fprintf(stderr, "FAIL [%s:%d] %s\n", test, line, expression);
        ++gFailures;
    }
}

#define CHECK(testName, expression) check((expression), #expression, (testName), __LINE__)

bool nearlyEqual(float a, float b, float tolerance = 1.0e-5f) {
    return std::fabs(a - b) <= tolerance;
}

bool juryStable(float a1, float a2) {
    return (1.0f + a1 + a2) > 0.0f &&
           (1.0f - a1 + a2) > 0.0f &&
           a2 > -1.0f && a2 < 1.0f;
}

void testParamSmoother() {
    const char* test = "ParamSmoother";
    ParamSmoother smoother;
    smoother.init(48000.0f, 10.0f);
    smoother.setImmediate(0.0f);
    smoother.setTarget(1.0f);

    float previous = smoother.getCurrent();
    for (int i = 0; i < 4800; ++i) {
        const float value = smoother.getNextValue();
        CHECK(test, std::isfinite(value));
        CHECK(test, value >= previous);
        CHECK(test, value <= 1.0f);
        previous = value;
    }
    CHECK(test, previous > 0.999f);

    smoother.init(0.0f, 0.0f);
    smoother.setImmediate(0.25f);
    CHECK(test, nearlyEqual(smoother.getNextValue(), 0.25f));

    smoother.setTarget(NAN);
    CHECK(test, nearlyEqual(smoother.getNextValue(), 0.25f));

    smoother.init(48000.0f, 20.0f);
    smoother.setImmediate(1.0f);
    smoother.setTarget(0.0f);
    for (int i = 0; i < 200000; ++i) smoother.getNextValue();
    CHECK(test, smoother.getCurrent() == 0.0f);

    // Nonzero targets must also settle exactly: a fixed absolute threshold
    // below float precision can otherwise leave the smoother stuck forever.
    const float sampleRates[] = { 8000.0f, 48000.0f, 96000.0f, 384000.0f };
    for (float sampleRate : sampleRates) {
        smoother.init(sampleRate, 20.0f);
        smoother.setTarget(1.0f);
        previous = 0.0f;
        for (int i = 0; i < 200000; ++i) {
            const float value = smoother.getNextValue();
            CHECK(test, value >= previous && value <= 1.0f);
            previous = value;
        }
        CHECK(test, smoother.getCurrent() == 1.0f);

        smoother.setTarget(0.25f);
        for (int i = 0; i < 200000; ++i) {
            const float value = smoother.getNextValue();
            CHECK(test, value <= previous && value >= 0.25f);
            previous = value;
        }
        CHECK(test, smoother.getCurrent() == 0.25f);
    }
}

double impulseEnergy(AllpassSection& section, int numSamples) {
    double energy = 0.0;
    for (int i = 0; i < numSamples; ++i) {
        const float output = section.process(i == 0 ? 1.0f : 0.0f);
        if (!std::isfinite(output)) {
            return -1.0;
        }
        energy += static_cast<double>(output) * static_cast<double>(output);
    }
    return energy;
}

void testAllpassSection() {
    const char* test = "AllpassSection";
    AllpassSection section;
    section.init(48000.0f);
    section.setPole(2500.0f, 0.8f);
    const double energy = impulseEnergy(section, 32768);
    CHECK(test, energy > 0.0);
    CHECK(test, std::fabs(energy - 1.0) < 1.0e-4);

    section.init(-1.0f);
    section.setPole(-100.0f, 5.0f);
    CHECK(test, impulseEnergy(section, 32768) > 0.0);
}

void testFastCosApproximation() {
    const char* test = "Fast cosine";
    constexpr int numIntervals = 1 << 20;
    constexpr float maxAngle = 0.9f * 3.14159265358979323846f;
    double maxError = 0.0;

    for (int i = 0; i <= numIntervals; ++i) {
        const float angle = maxAngle * static_cast<float>(i) /
                            static_cast<float>(numIntervals);
        const float approximation = AllpassSection::fastCos(angle);
        const double reference = std::cos(static_cast<double>(angle));
        const double error = std::fabs(static_cast<double>(approximation) - reference);
        if (error > maxError) maxError = error;
        CHECK(test, std::isfinite(approximation));
        CHECK(test, approximation >= -1.000001f && approximation <= 1.000001f);
    }

    // Dense float evaluation covers the complete coefficient-design domain.
    CHECK(test, maxError < 2.0e-7);
}

void testCoefficientInterpolation() {
    const char* test = "Coefficient interpolation";
    constexpr float sampleRate = 48000.0f;
    float startFrequencies[AllpassNetwork::kNumSections];
    float targetFrequencies[AllpassNetwork::kNumSections];
    for (int i = 0; i < AllpassNetwork::kNumSections; ++i) {
        startFrequencies[i] = 200.0f;
        targetFrequencies[i] = 0.45f * sampleRate;
    }

    AllpassNetwork network;
    network.init(sampleRate);
    network.setAllpassFrequencies(startFrequencies, 0.6f);
    CHECK(test, network.getCoefficientRampRemaining() == 0);
    const float startA1 = network.getSectionA1(0);
    const float startA2 = network.getSectionA2(0);

    AllpassSection target;
    target.init(sampleRate);
    target.setPole(targetFrequencies[0], 0.8f);
    const float targetA1 = target.getA1();
    const float targetA2 = target.getA2();

    network.setAllpassFrequencies(targetFrequencies, 0.8f);
    CHECK(test, network.getCoefficientRampRemaining() ==
                    AllpassNetwork::kCoefficientRampSamples);

    float previousA1 = startA1;
    float previousA2 = startA2;
    for (int step = 1; step <= AllpassNetwork::kCoefficientRampSamples; ++step) {
        const float output = network.processSample(step == 1 ? 1.0f : 0.0f, 1.0f);
        const float a1 = network.getSectionA1(0);
        const float a2 = network.getSectionA2(0);
        const float fraction = static_cast<float>(step) /
                               static_cast<float>(AllpassNetwork::kCoefficientRampSamples);
        const float expectedA1 = startA1 + (targetA1 - startA1) * fraction;
        const float expectedA2 = startA2 + (targetA2 - startA2) * fraction;

        CHECK(test, std::isfinite(output));
        CHECK(test, nearlyEqual(a1, expectedA1, 2.0e-6f));
        CHECK(test, nearlyEqual(a2, expectedA2, 2.0e-6f));
        CHECK(test, juryStable(a1, a2));
        CHECK(test, (1.0f + a1 + a2) >= 0.039f);
        CHECK(test, (1.0f - a1 + a2) >= 0.039f);
        CHECK(test, std::fabs((a1 - previousA1) -
                              (targetA1 - startA1) /
                                  static_cast<float>(AllpassNetwork::kCoefficientRampSamples)) <
                        3.0e-7f);
        CHECK(test, std::fabs((a2 - previousA2) -
                              (targetA2 - startA2) /
                                  static_cast<float>(AllpassNetwork::kCoefficientRampSamples)) <
                        3.0e-7f);
        CHECK(test, network.getCoefficientRampRemaining() ==
                        AllpassNetwork::kCoefficientRampSamples - step);
        previousA1 = a1;
        previousA2 = a2;
    }

    CHECK(test, nearlyEqual(network.getSectionA1(0), targetA1, 2.0e-6f));
    CHECK(test, nearlyEqual(network.getSectionA2(0), targetA2, 2.0e-6f));
    const float settledA1 = network.getSectionA1(0);
    const float settledA2 = network.getSectionA2(0);
    for (int i = 0; i < 128; ++i) {
        CHECK(test, std::isfinite(network.processSample(0.0f, 1.0f)));
        CHECK(test, network.getSectionA1(0) == settledA1);
        CHECK(test, network.getSectionA2(0) == settledA2);
    }

    // A new target received during a ramp must start from the current
    // coefficients, discard the former trajectory, and land exactly on the
    // replacement target after a fresh 32-sample ramp.
    AllpassNetwork retargeted;
    retargeted.init(sampleRate);
    float initialFrequencies[AllpassNetwork::kNumSections];
    float abandonedFrequencies[AllpassNetwork::kNumSections];
    float replacementFrequencies[AllpassNetwork::kNumSections];
    for (int i = 0; i < AllpassNetwork::kNumSections; ++i) {
        initialFrequencies[i] = 300.0f + 10.0f * static_cast<float>(i);
        abandonedFrequencies[i] = 18000.0f - 50.0f * static_cast<float>(i);
        replacementFrequencies[i] = 1200.0f + 25.0f * static_cast<float>(i);
    }
    retargeted.setAllpassFrequencies(initialFrequencies, 0.35f);
    retargeted.setAllpassFrequencies(abandonedFrequencies, 0.85f);
    for (int i = 0; i < 11; ++i) {
        CHECK(test, std::isfinite(retargeted.processSample(0.0f, 0.5f)));
    }
    CHECK(test, retargeted.getCoefficientRampRemaining() == 21);
    const float midpointA1 = retargeted.getSectionA1(0);
    const float midpointA2 = retargeted.getSectionA2(0);

    AllpassSection replacementTarget;
    replacementTarget.init(sampleRate);
    replacementTarget.setPole(replacementFrequencies[0], 0.55f);
    const float replacementA1 = replacementTarget.getA1();
    const float replacementA2 = replacementTarget.getA2();

    retargeted.setAllpassFrequencies(replacementFrequencies, 0.55f);
    CHECK(test, retargeted.getCoefficientRampRemaining() ==
                    AllpassNetwork::kCoefficientRampSamples);
    CHECK(test, retargeted.getSectionA1(0) == midpointA1);
    CHECK(test, retargeted.getSectionA2(0) == midpointA2);

    for (int step = 1; step < AllpassNetwork::kCoefficientRampSamples; ++step) {
        CHECK(test, std::isfinite(retargeted.processSample(0.0f, 0.5f)));
        const float fraction = static_cast<float>(step) /
                               static_cast<float>(AllpassNetwork::kCoefficientRampSamples);
        CHECK(test, nearlyEqual(retargeted.getSectionA1(0),
                                midpointA1 + (replacementA1 - midpointA1) * fraction,
                                2.0e-6f));
        CHECK(test, nearlyEqual(retargeted.getSectionA2(0),
                                midpointA2 + (replacementA2 - midpointA2) * fraction,
                                2.0e-6f));
        CHECK(test, retargeted.getCoefficientRampRemaining() ==
                        AllpassNetwork::kCoefficientRampSamples - step);
    }
    CHECK(test, retargeted.getSectionA1(0) != replacementA1);
    CHECK(test, retargeted.getSectionA2(0) != replacementA2);
    CHECK(test, std::isfinite(retargeted.processSample(0.0f, 0.5f)));
    CHECK(test, retargeted.getCoefficientRampRemaining() == 0);
    CHECK(test, retargeted.getSectionA1(0) == replacementA1);
    CHECK(test, retargeted.getSectionA2(0) == replacementA2);
}

double networkImpulseEnergy(float stages) {
    AllpassNetwork network;
    network.init(48000.0f);
    float frequencies[AllpassNetwork::kNumSections];
    for (int i = 0; i < AllpassNetwork::kNumSections; ++i) {
        frequencies[i] = 300.0f + static_cast<float>(i) * 350.0f;
    }
    network.setAllpassFrequencies(frequencies, 0.75f);

    double energy = 0.0;
    for (int i = 0; i < 32768; ++i) {
        const float output = network.processSample(i == 0 ? 1.0f : 0.0f, stages);
        if (!std::isfinite(output)) {
            return -1.0;
        }
        energy += static_cast<double>(output) * static_cast<double>(output);
    }
    return energy;
}

void testAllpassNetwork() {
    const char* test = "AllpassNetwork";
    const double firstTapEnergy = networkImpulseEnergy(0.0f);
    const double lastTapEnergy = networkImpulseEnergy(1.0f);
    CHECK(test, std::fabs(firstTapEnergy - 1.0) < 1.0e-4);
    CHECK(test, std::fabs(lastTapEnergy - 1.0) < 1.0e-4);

    AllpassNetwork network;
    network.init(48000.0f);
    CHECK(test, std::isfinite(network.processSample(1.0f, NAN)));
    network.setAllpassFrequencies(nullptr, 0.7f);
}

void testSmearCoreReinitialization() {
    const char* test = "SmearCore reinitialization";
    SmearCore reused;
    reused.init(48000.0f);
    const float sampleRates[] = { 48000.0f, 96000.0f, 8000.0f, 384000.0f };
    for (float sampleRate : sampleRates) {
        // Include live feedback history before resetting the same instance.
        float oldLeft = 0.0f;
        float oldRight = 0.0f;
        for (int i = 0; i < 128; ++i) {
            reused.processSample(i == 0 ? 1.0f : 0.0f, 0.0f, oldLeft, oldRight);
        }
        reused.init(sampleRate);
        SmearCore fresh;
        fresh.init(sampleRate);

        for (int i = 0; i < 2048; ++i) {
            const float input = i == 0 ? 1.0f : 0.0f;
            float reusedLeft = 0.0f;
            float reusedRight = 0.0f;
            float freshLeft = 0.0f;
            float freshRight = 0.0f;
            reused.processSample(input, -input, reusedLeft, reusedRight);
            fresh.processSample(input, -input, freshLeft, freshRight);
            CHECK(test, reusedLeft == freshLeft);
            CHECK(test, reusedRight == freshRight);
        }
    }
}

void testSmearCoreSilenceAndStability() {
    const char* test = "SmearCore stability";
    SmearCore core;
    core.init(48000.0f);

    for (int i = 0; i < 256; ++i) {
        float left = 1.0f;
        float right = 1.0f;
        core.processSample(0.0f, 0.0f, left, right);
        CHECK(test, left == 0.0f);
        CHECK(test, right == 0.0f);
    }

    core.setFrequency(2.0f);
    core.setResonance(2.0f);
    core.setStages(-1.0f);
    core.setDistributionMorph(2.0f);
    core.setFeedback(2.0f);

    double outputEnergy = 0.0;
    double stereoDifference = 0.0;
    for (int i = 0; i < 65536; ++i) {
        float left = 0.0f;
        float right = 0.0f;
        const float input = (i == 0) ? 1.0f : 0.0f;
        core.processSample(input, input, left, right);
        CHECK(test, std::isfinite(left));
        CHECK(test, std::isfinite(right));
        CHECK(test, std::fabs(left) < 16.0f);
        CHECK(test, std::fabs(right) < 16.0f);
        outputEnergy += static_cast<double>(left) * left + static_cast<double>(right) * right;
        stereoDifference += std::fabs(static_cast<double>(left) - right);
    }
    CHECK(test, outputEnergy > 0.0);
    CHECK(test, stereoDifference > 1.0e-3);

    float left = 1.0f;
    float right = 1.0f;
    core.setStages(NAN);
    core.setFrequency(INFINITY);
    core.processSample(NAN, -INFINITY, left, right);
    CHECK(test, std::isfinite(left));
    CHECK(test, std::isfinite(right));
}

void testExtremeFiniteRecovery() {
    const char* test = "Extreme finite recovery";
    const float maximum = std::numeric_limits<float>::max();

    // Repeated adversarial finite input is clamped at the complete-core
    // boundary. It must remain finite, recover bounded normal operation, and
    // then decay to a finite silence.
    SmearCore core;
    core.init(48000.0f);
    core.setFrequency(1.0f);
    core.setResonance(1.0f);
    core.setStages(1.0f);
    core.setDistributionMorph(1.0f);
    core.setFeedback(1.0f);
    for (int i = 0; i < 2048; ++i) {
        float left = 0.0f;
        float right = 0.0f;
        core.processSample(maximum, maximum, left, right);
        CHECK(test, std::isfinite(left));
        CHECK(test, std::isfinite(right));
    }

    double recoveryEnergy = 0.0;
    float settledPeak = 0.0f;
    for (int i = 0; i < 8192; ++i) {
        const float inputL = 0.1f * std::sin(0.017f * static_cast<float>(i));
        const float inputR = 0.1f * std::cos(0.013f * static_cast<float>(i));
        float left = 0.0f;
        float right = 0.0f;
        core.processSample(inputL, inputR, left, right);
        CHECK(test, std::isfinite(left));
        CHECK(test, std::isfinite(right));
        if (i >= 7168) {
            recoveryEnergy += static_cast<double>(left) * left;
            recoveryEnergy += static_cast<double>(right) * right;
            const float samplePeak = (std::fabs(left) > std::fabs(right))
                ? std::fabs(left) : std::fabs(right);
            if (samplePeak > settledPeak) settledPeak = samplePeak;
        }
    }
    CHECK(test, recoveryEnergy > 1.0e-6);
    CHECK(test, settledPeak < 16.0f);

    float silenceTailPeak = 0.0f;
    for (int i = 0; i < 65536; ++i) {
        float left = 0.0f;
        float right = 0.0f;
        core.processSample(0.0f, 0.0f, left, right);
        CHECK(test, std::isfinite(left));
        CHECK(test, std::isfinite(right));
        if (i >= 64512) {
            const float samplePeak = (std::fabs(left) > std::fabs(right))
                ? std::fabs(left) : std::fabs(right);
            if (samplePeak > silenceTailPeak) silenceTailPeak = samplePeak;
        }
    }
    CHECK(test, silenceTailPeak < 1.0e-4f);
}

void testSmearCoreBlockAndInPlace() {
    const char* test = "SmearCore block";
    const int numSamples = 1024;
    std::vector<float> inputLeft(numSamples);
    std::vector<float> inputRight(numSamples);
    for (int i = 0; i < numSamples; ++i) {
        inputLeft[i] = 0.25f * std::sin(0.013f * static_cast<float>(i));
        inputRight[i] = 0.25f * std::cos(0.017f * static_cast<float>(i));
    }

    std::vector<float> separateLeft(numSamples);
    std::vector<float> separateRight(numSamples);
    SmearCore separate;
    separate.init(48000.0f);
    separate.processBlock(inputLeft.data(), inputRight.data(),
                          separateLeft.data(), separateRight.data(), numSamples);

    std::vector<float> inPlaceLeft = inputLeft;
    std::vector<float> inPlaceRight = inputRight;
    SmearCore inPlace;
    inPlace.init(48000.0f);
    inPlace.processBlock(inPlaceLeft.data(), inPlaceRight.data(),
                         inPlaceLeft.data(), inPlaceRight.data(), numSamples);

    for (int i = 0; i < numSamples; ++i) {
        CHECK(test, nearlyEqual(inPlaceLeft[i], separateLeft[i]));
        CHECK(test, nearlyEqual(inPlaceRight[i], separateRight[i]));
    }

    // Invalid buffers are deliberately ignored by the block API.
    separate.processBlock(nullptr, inputRight.data(), separateLeft.data(),
                          separateRight.data(), numSamples);
}

void testSupportedSampleRates() {
    const char* test = "SmearCore sample rates";
    const float sampleRates[] = { 8000.0f, 32000.0f, 44100.0f, 48000.0f,
                                  88200.0f, 96000.0f, 192000.0f, 384000.0f };
    for (std::size_t rate = 0; rate < sizeof(sampleRates) / sizeof(sampleRates[0]); ++rate) {
        SmearCore core;
        core.init(sampleRates[rate]);
        core.setFrequency(1.0f);
        core.setResonance(1.0f);
        core.setStages(1.0f);
        core.setDistributionMorph(1.0f);
        core.setFeedback(1.0f);
        double energy = 0.0;
        for (int i = 0; i < 16384; ++i) {
            float left = 0.0f;
            float right = 0.0f;
            const float input = (i == 0) ? 1.0f : 0.0f;
            core.processSample(input, input, left, right);
            CHECK(test, std::isfinite(left));
            CHECK(test, std::isfinite(right));
            energy += static_cast<double>(left) * left + static_cast<double>(right) * right;
        }
        CHECK(test, energy > 0.0);
    }
}

struct AlgorithmInstance {
    std::vector<std::max_align_t> storage;
    _NT_algorithm* algorithm;
};

AlgorithmInstance makeAlgorithm(const _NT_factory& factory,
                                const _NT_algorithmRequirements& requirements) {
    const std::size_t words =
        (requirements.sram + sizeof(std::max_align_t) - 1U) / sizeof(std::max_align_t);
    AlgorithmInstance instance = { std::vector<std::max_align_t>(words), nullptr };
    _NT_algorithmMemoryPtrs memory = {};
    memory.sram = reinterpret_cast<uint8_t*>(instance.storage.data());
    instance.algorithm = factory.construct(memory, requirements, nullptr);
    return instance;
}

enum { kLegacyParameterCount = 11, kAdapterParameterCount = kNumParameters,
       kAdapterTestFrames = 64 };

void configureAdapter(const _NT_factory& factory,
                      _NT_algorithm* algorithm,
                      int16_t (&values)[kAdapterParameterCount]) {
    if (!algorithm) return;
    // Existing regression cases specify the original preset prefix. Supply the
    // host's declared defaults for newly appended controls before rendering.
    for (int p = kLegacyParameterCount; p < kAdapterParameterCount; ++p)
        values[p] = algorithm->parameters[p].def;
    algorithm->vIncludingCommon = values;
    algorithm->v = values;
    for (int p = 0; p < kAdapterParameterCount; ++p) {
        factory.parameterChanged(algorithm, p);
    }
}

std::vector<float> makeAdapterInput() {
    std::vector<float> busses(
        static_cast<std::size_t>(kNT_lastBus) * kAdapterTestFrames, 0.0f);
    for (int i = 0; i < kAdapterTestFrames; ++i) {
        busses[static_cast<std::size_t>(i)] =
            0.2f * std::sin(0.11f * static_cast<float>(i)) + (i == 0 ? 1.0f : 0.0f);
        busses[static_cast<std::size_t>(kAdapterTestFrames + i)] =
            0.15f * std::cos(0.07f * static_cast<float>(i)) - (i == 1 ? 0.5f : 0.0f);
    }
    return busses;
}

std::vector<float> renderAdapter(const _NT_factory& factory,
                                 const _NT_algorithmRequirements& requirements,
                                 int16_t (&values)[kAdapterParameterCount],
                                 std::vector<float> busses) {
    AlgorithmInstance instance = makeAlgorithm(factory, requirements);
    if (!instance.algorithm) return busses;
    configureAdapter(factory, instance.algorithm, values);
    factory.step(instance.algorithm, busses.data(), kAdapterTestFrames / 4);
    return busses;
}

std::vector<float> renderAdapterFourFrames(
    const _NT_factory& factory,
    const _NT_algorithmRequirements& requirements,
    int16_t (&values)[kAdapterParameterCount],
    std::vector<float> busses) {
    AlgorithmInstance instance = makeAlgorithm(factory, requirements);
    if (!instance.algorithm) return busses;
    configureAdapter(factory, instance.algorithm, values);
    factory.step(instance.algorithm, busses.data(), 1);
    return busses;
}

std::vector<float> makeFourFrameBusses() {
    const int numFrames = 4;
    std::vector<float> busses(
        static_cast<std::size_t>(kNT_lastBus) * numFrames, 0.0f);
    for (int bus = 1; bus <= kNT_lastBus; ++bus) {
        for (int frame = 0; frame < numFrames; ++frame) {
            busses[static_cast<std::size_t>(bus - 1) * numFrames + frame] =
                0.01f * static_cast<float>(bus) +
                0.001f * static_cast<float>(frame + 1);
        }
    }
    busses[0] = 1.0f;
    busses[1] = -0.25f;
    busses[2] = 0.5f;
    busses[3] = -0.75f;
    busses[4] = -0.5f;
    busses[5] = 0.75f;
    busses[6] = -1.0f;
    busses[7] = 0.25f;
    return busses;
}

float busSampleForFrames(const std::vector<float>& busses,
                         int bus,
                         int frame,
                         int numFrames) {
    return busses[static_cast<std::size_t>(bus - 1) * numFrames +
                   static_cast<std::size_t>(frame)];
}

float busSample(const std::vector<float>& busses, int bus, int frame) {
    return busses[static_cast<std::size_t>(bus - 1) * kAdapterTestFrames +
                   static_cast<std::size_t>(frame)];
}

void testAdapterContract() {
    const char* test = "NT adapter";
    CHECK(test, pluginEntry(kNT_selector_version, 0) == kNT_apiVersionCurrent);
    CHECK(test, pluginEntry(kNT_selector_numFactories, 0) == 1U);
    CHECK(test, pluginEntry(kNT_selector_factoryInfo, 1) == 0U);

    const _NT_factory* factory = reinterpret_cast<const _NT_factory*>(
        pluginEntry(kNT_selector_factoryInfo, 0));
    CHECK(test, factory != nullptr);
    if (!factory) {
        return;
    }
    CHECK(test, factory->calculateRequirements != nullptr);
    CHECK(test, factory->construct != nullptr);
    CHECK(test, factory->parameterChanged != nullptr);
    CHECK(test, factory->step != nullptr);
    CHECK(test, factory->midiRealtime != nullptr);
    CHECK(test, factory->guid == NT_MULTICHAR('S', 'M', 'N', 'T'));
    CHECK(test, std::strcmp(factory->name, "SmearNT") == 0);
    CHECK(test, std::strcmp(factory->description,
                            "Spectral smear, synced modulation and spectral stereo") == 0);
    CHECK(test, factory->tags == (kNT_tagEffect | kNT_tagFilterEQ));
    CHECK(test, factory->numSpecifications == 0U);
    CHECK(test, factory->specifications == nullptr);
    CHECK(test, factory->calculateStaticRequirements == nullptr);
    CHECK(test, factory->initialise == nullptr);

    _NT_algorithmRequirements requirements = {};
    factory->calculateRequirements(requirements, nullptr);
    CHECK(test, requirements.numParameters == kNumParameters);
    CHECK(test, kParamInL == 0 && kParamFreq == 6 && kParamFb == 10);
    CHECK(test, kParamStereoSpread == kLegacyParameterCount);
    CHECK(test, kParamRoute1Source == 32 && kRouteCount == 8 && kRouteStride == 3);
    CHECK(test, requirements.sram >= sizeof(_NT_algorithm));
    CHECK(test, requirements.dram == 0U);
    CHECK(test, requirements.dtc == 0U);
    CHECK(test, requirements.itc == 0U);

    AlgorithmInstance instance = makeAlgorithm(*factory, requirements);
    CHECK(test, instance.algorithm != nullptr);
    if (!instance.algorithm) {
        return;
    }
    CHECK(test, instance.algorithm->parameters != nullptr);
    CHECK(test, instance.algorithm->parameterPages != nullptr);
    CHECK(test, instance.algorithm->parameterPages->numPages == 15U);

    const char* expectedNames[kLegacyParameterCount] = {
        "In L", "In R", "Out L", "Out L mode", "Out R", "Out R mode",
        "Frequency", "Resonance", "Stages", "Distribution", "Feedback"
    };
    const int expectedDefaults[kAdapterParameterCount] = {
        1, 2, 13, 0, 14, 0, 50, 50, 50, 50, 20,
        0, 0, 250, 0, 120, 0, 0, 0, 4,
        0, 15, 100, 0, 0, 0, 17, 100, 0, 90, 0, 0
    };
    const int expectedMinimums[kLegacyParameterCount] = {
        1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0
    };
    const int expectedMaximums[kLegacyParameterCount] = {
        kNT_lastBus, kNT_lastBus, kNT_lastBus, 1, kNT_lastBus, 1,
        100, 100, 100, 100, 100
    };
    const int expectedUnits[kLegacyParameterCount] = {
        kNT_unitAudioInput, kNT_unitAudioInput,
        kNT_unitAudioOutput, kNT_unitOutputMode,
        kNT_unitAudioOutput, kNT_unitOutputMode,
        kNT_unitPercent, kNT_unitPercent, kNT_unitPercent,
        kNT_unitPercent, kNT_unitPercent
    };
    bool parameterSeen[kAdapterParameterCount] = {};
    for (int p = 0; p < kLegacyParameterCount; ++p) {
        CHECK(test, std::strcmp(instance.algorithm->parameters[p].name, expectedNames[p]) == 0);
        CHECK(test, instance.algorithm->parameters[p].min == expectedMinimums[p]);
        CHECK(test, instance.algorithm->parameters[p].max == expectedMaximums[p]);
        CHECK(test, instance.algorithm->parameters[p].def == expectedDefaults[p]);
        CHECK(test, instance.algorithm->parameters[p].unit == expectedUnits[p]);
        CHECK(test, instance.algorithm->parameters[p].scaling == kNT_scalingNone);
        CHECK(test, instance.algorithm->parameters[p].enumStrings == nullptr);
    }
    for (int p = kLegacyParameterCount; p < kAdapterParameterCount; ++p) {
        const _NT_parameter& parameter = instance.algorithm->parameters[p];
        CHECK(test, parameter.name != nullptr && parameter.name[0] != '\0');
        CHECK(test, parameter.def == expectedDefaults[p]);
        CHECK(test, parameter.min <= parameter.def && parameter.def <= parameter.max);
        CHECK(test, parameter.scaling == kNT_scalingNone);
    }
    CHECK(test, instance.algorithm->parameters[kParamStereoMotion].min == -100);
    CHECK(test, instance.algorithm->parameters[kParamMod1Amount].min == -100);
    CHECK(test, instance.algorithm->parameters[kParamMod2Offset].min == -100);
    CHECK(test, instance.algorithm->parameters[kParamMod1Rate].max == 19);
    CHECK(test, instance.algorithm->parameters[kParamMod2Rate].enumStrings != nullptr);
    CHECK(test, std::strcmp(instance.algorithm->parameters[kParamMod1Rate].enumStrings[19],
                            "16 bars") == 0);
    for (int route = 0; route < kRouteCount; ++route) {
        const int p = kParamRoute1Source + route * kRouteStride;
        CHECK(test, instance.algorithm->parameters[p].max == kSourceCV2);
        CHECK(test, instance.algorithm->parameters[p + 1].max == kDestinationCrossover);
        CHECK(test, instance.algorithm->parameters[p + 2].min == -100);
        CHECK(test, instance.algorithm->parameters[p + 2].max == 100);
    }

    const char* expectedPageNames[] = {
        "Main", "Routing", "Stereo", "Clock", "Mod 1", "Mod 2", "CV inputs",
        "Route 1", "Route 2", "Route 3", "Route 4", "Route 5", "Route 6", "Route 7", "Route 8"
    };
    const uint8_t expectedPageSizes[] = { 5, 6, 3, 6, 5, 5, 2, 3, 3, 3, 3, 3, 3, 3, 3 };
    const uint8_t expectedPageParameters[][6] = {
        { 6, 7, 8, 9, 10, 0 },
        { 0, 1, 2, 3, 4, 5 },
        { 11, 12, 13, 0, 0, 0 },
        { 14, 15, 16, 17, 18, 19 },
        { 20, 21, 22, 23, 24, 0 },
        { 25, 26, 27, 28, 29, 0 },
        { 30, 31, 0, 0, 0, 0 },
        { 32, 33, 34, 0, 0, 0 }, { 35, 36, 37, 0, 0, 0 },
        { 38, 39, 40, 0, 0, 0 }, { 41, 42, 43, 0, 0, 0 },
        { 44, 45, 46, 0, 0, 0 }, { 47, 48, 49, 0, 0, 0 },
        { 50, 51, 52, 0, 0, 0 }, { 53, 54, 55, 0, 0, 0 },
    };
    for (uint32_t page = 0; page < instance.algorithm->parameterPages->numPages; ++page) {
        const _NT_parameterPage& definition = instance.algorithm->parameterPages->pages[page];
        CHECK(test, std::strcmp(definition.name, expectedPageNames[page]) == 0);
        CHECK(test, definition.numParams == expectedPageSizes[page]);
        CHECK(test, definition.group == 0);
        CHECK(test, definition.unused[0] == 0);
        CHECK(test, definition.unused[1] == 0);
        CHECK(test, definition.params != nullptr);
        for (uint8_t item = 0; item < definition.numParams; ++item) {
            const uint8_t parameter = definition.params[item];
            CHECK(test, parameter == expectedPageParameters[page][item]);
            CHECK(test, parameter < kAdapterParameterCount);
            if (parameter < kAdapterParameterCount) {
                CHECK(test, !parameterSeen[parameter]);
                parameterSeen[parameter] = true;
            }
        }
    }
    for (int p = 0; p < kAdapterParameterCount; ++p) CHECK(test, parameterSeen[p]);

    int16_t values[kAdapterParameterCount] = { 1, 2, 13, 1, 14, 1, 50, 50, 50, 50, 20 };
    configureAdapter(*factory, instance.algorithm, values);

    const int numFrames = 64;
    std::vector<float> busses(static_cast<std::size_t>(kNT_lastBus) * numFrames, 0.0f);
    float* inputLeft = busses.data();
    float* inputRight = busses.data() + numFrames;
    float* outputLeft = busses.data() + 12 * numFrames;
    float* outputRight = busses.data() + 13 * numFrames;
    inputLeft[0] = 1.0f;
    inputRight[0] = 1.0f;
    factory->step(instance.algorithm, busses.data(), numFrames / 4);

    double outputEnergy = 0.0;
    for (int i = 0; i < numFrames; ++i) {
        CHECK(test, std::isfinite(outputLeft[i]));
        CHECK(test, std::isfinite(outputRight[i]));
        outputEnergy += static_cast<double>(outputLeft[i]) * outputLeft[i];
        outputEnergy += static_cast<double>(outputRight[i]) * outputRight[i];
    }
    CHECK(test, outputEnergy > 0.0);

    // In add mode, a silent fresh instance must preserve the existing bus signal.
    AlgorithmInstance addInstance = makeAlgorithm(*factory, requirements);
    int16_t addValues[kAdapterParameterCount] = { 1, 2, 13, 0, 14, 0, 50, 50, 50, 50, 20 };
    configureAdapter(*factory, addInstance.algorithm, addValues);
    std::vector<float> addBusses(static_cast<std::size_t>(kNT_lastBus) * numFrames, 0.0f);
    float* addLeft = addBusses.data() + 12 * numFrames;
    float* addRight = addBusses.data() + 13 * numFrames;
    for (int i = 0; i < numFrames; ++i) {
        addLeft[i] = 0.25f;
        addRight[i] = -0.5f;
    }
    factory->step(addInstance.algorithm, addBusses.data(), numFrames / 4);
    for (int i = 0; i < numFrames; ++i) {
        CHECK(test, nearlyEqual(addLeft[i], 0.25f));
        CHECK(test, nearlyEqual(addRight[i], -0.5f));
    }

    factory->parameterChanged(instance.algorithm, -1);
    factory->parameterChanged(instance.algorithm, 99);
    factory->step(instance.algorithm, nullptr, numFrames / 4);
    factory->step(instance.algorithm, busses.data(), 0);
    factory->step(instance.algorithm, busses.data(), -1);
    factory->step(instance.algorithm, busses.data(), INT_MAX);
}

void testAdapterModeMatrixAndDestinations() {
    const char* test = "NT adapter mode matrix";
    const int numFrames = 4;
    const int referenceOutL = 17;
    const int referenceOutR = 29;
    const _NT_factory* factory = reinterpret_cast<const _NT_factory*>(
        pluginEntry(kNT_selector_factoryInfo, 0));
    CHECK(test, factory != nullptr);
    if (!factory) return;

    _NT_algorithmRequirements requirements = {};
    factory->calculateRequirements(requirements, nullptr);
    const std::vector<float> input = makeFourFrameBusses();

    int16_t referenceValues[kAdapterParameterCount] =
        { 1, 2, referenceOutL, 1, referenceOutR, 1,
          50, 50, 50, 50, 20 };
    const std::vector<float> reference =
        renderAdapterFourFrames(*factory, requirements, referenceValues, input);

    double wetEnergy = 0.0;
    for (int frame = 0; frame < numFrames; ++frame) {
        const float wetL = busSampleForFrames(reference, referenceOutL, frame, numFrames);
        const float wetR = busSampleForFrames(reference, referenceOutR, frame, numFrames);
        CHECK(test, std::isfinite(wetL));
        CHECK(test, std::isfinite(wetR));
        wetEnergy += static_cast<double>(wetL) * wetL;
        wetEnergy += static_cast<double>(wetR) * wetR;
    }
    CHECK(test, wetEnergy > 1.0e-6);

    // Cover Add/Add, Replace/Add, Add/Replace, and Replace/Replace with
    // non-zero wet signals and non-zero pre-existing destination signals.
    for (int modePair = 0; modePair < 4; ++modePair) {
        const int replaceL = modePair & 1;
        const int replaceR = (modePair >> 1) & 1;
        int16_t values[kAdapterParameterCount] =
            { 1, 2, referenceOutL, static_cast<int16_t>(replaceL),
              referenceOutR, static_cast<int16_t>(replaceR),
              50, 50, 50, 50, 20 };
        const std::vector<float> rendered =
            renderAdapterFourFrames(*factory, requirements, values, input);

        for (int frame = 0; frame < numFrames; ++frame) {
            const float wetL =
                busSampleForFrames(reference, referenceOutL, frame, numFrames);
            const float wetR =
                busSampleForFrames(reference, referenceOutR, frame, numFrames);
            const float previousL =
                busSampleForFrames(input, referenceOutL, frame, numFrames);
            const float previousR =
                busSampleForFrames(input, referenceOutR, frame, numFrames);
            CHECK(test,
                  nearlyEqual(busSampleForFrames(rendered, referenceOutL,
                                                 frame, numFrames),
                              replaceL ? wetL : previousL + wetL));
            CHECK(test,
                  nearlyEqual(busSampleForFrames(rendered, referenceOutR,
                                                 frame, numFrames),
                              replaceR ? wetR : previousR + wetR));
        }

        for (int bus = 1; bus <= kNT_lastBus; ++bus) {
            if (bus == referenceOutL || bus == referenceOutR) continue;
            for (int frame = 0; frame < numFrames; ++frame) {
                CHECK(test,
                      busSampleForFrames(rendered, bus, frame, numFrames) ==
                          busSampleForFrames(input, bus, frame, numFrames));
            }
        }
    }

    // Destination addressing must not affect the DSP. Exercise two distinct,
    // non-default outputs, including the final valid bus.
    const int alternateOutL = 37;
    const int alternateOutR = kNT_lastBus;
    int16_t alternateValues[kAdapterParameterCount] =
        { 1, 2, alternateOutL, 1, alternateOutR, 1,
          50, 50, 50, 50, 20 };
    const std::vector<float> alternate =
        renderAdapterFourFrames(*factory, requirements, alternateValues, input);
    for (int frame = 0; frame < numFrames; ++frame) {
        CHECK(test,
              nearlyEqual(busSampleForFrames(alternate, alternateOutL,
                                             frame, numFrames),
                          busSampleForFrames(reference, referenceOutL,
                                             frame, numFrames)));
        CHECK(test,
              nearlyEqual(busSampleForFrames(alternate, alternateOutR,
                                             frame, numFrames),
                          busSampleForFrames(reference, referenceOutR,
                                             frame, numFrames)));
    }
    for (int bus = 1; bus <= kNT_lastBus; ++bus) {
        if (bus == alternateOutL || bus == alternateOutR) continue;
        for (int frame = 0; frame < numFrames; ++frame) {
            CHECK(test,
                  busSampleForFrames(alternate, bus, frame, numFrames) ==
                      busSampleForFrames(input, bus, frame, numFrames));
        }
    }
}

void testAdapterStepGuards() {
    const char* test = "NT adapter step guards";
    const _NT_factory* factory = reinterpret_cast<const _NT_factory*>(
        pluginEntry(kNT_selector_factoryInfo, 0));
    CHECK(test, factory != nullptr);
    if (!factory) return;

    _NT_algorithmRequirements requirements = {};
    factory->calculateRequirements(requirements, nullptr);
    AlgorithmInstance guarded = makeAlgorithm(*factory, requirements);
    AlgorithmInstance clean = makeAlgorithm(*factory, requirements);
    CHECK(test, guarded.algorithm != nullptr);
    CHECK(test, clean.algorithm != nullptr);
    if (!guarded.algorithm || !clean.algorithm) return;

    int16_t values[kAdapterParameterCount] =
        { 1, 2, 17, 1, 29, 1, 80, 70, 60, 40, 35 };
    configureAdapter(*factory, guarded.algorithm, values);
    configureAdapter(*factory, clean.algorithm, values);

    const std::vector<float> pristine = makeFourFrameBusses();
    std::vector<float> untouched = pristine;
    factory->step(guarded.algorithm, nullptr, 1);
    factory->step(guarded.algorithm, untouched.data(), 0);
    for (std::size_t i = 0; i < untouched.size(); ++i) {
        CHECK(test, untouched[i] == pristine[i]);
    }
    factory->step(guarded.algorithm, untouched.data(), -1);
    for (std::size_t i = 0; i < untouched.size(); ++i) {
        CHECK(test, untouched[i] == pristine[i]);
    }
    factory->step(guarded.algorithm, untouched.data(), INT_MAX);
    for (std::size_t i = 0; i < untouched.size(); ++i) {
        CHECK(test, untouched[i] == pristine[i]);
    }

    // Allocate the complete rejected request so this regression also fails
    // safely if the host maximum-frame guard is accidentally removed.
    const int oversizedFramesBy4 =
        static_cast<int>(NT_globals.maxFramesPerStep / 4U) + 1;
    const std::vector<float> oversizedPristine(
        static_cast<std::size_t>(oversizedFramesBy4) * 4U * kNT_lastBus, 0.25f);
    std::vector<float> oversized = oversizedPristine;
    factory->step(guarded.algorithm, oversized.data(), oversizedFramesBy4);
    for (std::size_t i = 0; i < oversized.size(); ++i) {
        CHECK(test, oversized[i] == oversizedPristine[i]);
    }

    // The first subsequent valid block must match a clean instance, proving
    // that none of the rejected calls advanced the DSP state or smoothers.
    std::vector<float> guardedOutput = pristine;
    std::vector<float> cleanOutput = pristine;
    factory->step(guarded.algorithm, guardedOutput.data(), 1);
    factory->step(clean.algorithm, cleanOutput.data(), 1);
    for (std::size_t i = 0; i < guardedOutput.size(); ++i) {
        CHECK(test, guardedOutput[i] == cleanOutput[i]);
    }
}

void testAdapterMonoInputAndInstanceIsolation() {
    const char* test = "NT adapter instance isolation";
    const int numFrames = 4;
    const int outL = 17;
    const int outR = 29;
    const _NT_factory* factory = reinterpret_cast<const _NT_factory*>(
        pluginEntry(kNT_selector_factoryInfo, 0));
    CHECK(test, factory != nullptr);
    if (!factory) return;

    _NT_algorithmRequirements requirements = {};
    factory->calculateRequirements(requirements, nullptr);

    std::vector<float> duplicatedInput = makeFourFrameBusses();
    for (int frame = 0; frame < numFrames; ++frame) {
        duplicatedInput[static_cast<std::size_t>(numFrames + frame)] =
            duplicatedInput[static_cast<std::size_t>(frame)];
    }
    int16_t separateInputs[kAdapterParameterCount] =
        { 1, 2, outL, 1, outR, 1, 50, 50, 50, 50, 20 };
    int16_t monoInput[kAdapterParameterCount] =
        { 1, 1, outL, 1, outR, 1, 50, 50, 50, 50, 20 };
    const std::vector<float> separate = renderAdapterFourFrames(
        *factory, requirements, separateInputs, duplicatedInput);
    const std::vector<float> mono = renderAdapterFourFrames(
        *factory, requirements, monoInput, duplicatedInput);
    for (int frame = 0; frame < numFrames; ++frame) {
        CHECK(test,
              busSampleForFrames(mono, outL, frame, numFrames) ==
                  busSampleForFrames(separate, outL, frame, numFrames));
        CHECK(test,
              busSampleForFrames(mono, outR, frame, numFrames) ==
                  busSampleForFrames(separate, outR, frame, numFrames));
    }

    AlgorithmInstance first = makeAlgorithm(*factory, requirements);
    AlgorithmInstance second = makeAlgorithm(*factory, requirements);
    CHECK(test, first.algorithm != nullptr);
    CHECK(test, second.algorithm != nullptr);
    if (!first.algorithm || !second.algorithm) return;

    int16_t instanceValues[kAdapterParameterCount] =
        { 1, 2, outL, 1, outR, 1, 85, 65, 75, 35, 40 };
    configureAdapter(*factory, first.algorithm, instanceValues);
    configureAdapter(*factory, second.algorithm, instanceValues);
    std::vector<float> firstOutput = makeFourFrameBusses();
    std::vector<float> secondOutput = firstOutput;
    factory->step(first.algorithm, firstOutput.data(), 1);
    factory->step(second.algorithm, secondOutput.data(), 1);
    for (std::size_t i = 0; i < firstOutput.size(); ++i) {
        CHECK(test, firstOutput[i] == secondOutput[i]);
    }

    // Continue both states with the same next block. Processing one instance
    // first must not perturb the state of the other.
    std::vector<float> firstContinuation = makeFourFrameBusses();
    std::vector<float> secondContinuation = firstContinuation;
    for (int frame = 0; frame < numFrames; ++frame) {
        firstContinuation[static_cast<std::size_t>(frame)] *= -0.75f;
        secondContinuation[static_cast<std::size_t>(frame)] *= -0.75f;
    }
    factory->step(first.algorithm, firstContinuation.data(), 1);
    factory->step(second.algorithm, secondContinuation.data(), 1);
    for (std::size_t i = 0; i < firstContinuation.size(); ++i) {
        CHECK(test, firstContinuation[i] == secondContinuation[i]);
    }
}

void testAdapterRoutingEdges() {
    const char* test = "NT adapter routing";
    const _NT_factory* factory = reinterpret_cast<const _NT_factory*>(
        pluginEntry(kNT_selector_factoryInfo, 0));
    CHECK(test, factory != nullptr);
    if (!factory) return;

    _NT_algorithmRequirements requirements = {};
    factory->calculateRequirements(requirements, nullptr);
    std::vector<float> input = makeAdapterInput();

    int16_t referenceValues[kAdapterParameterCount] =
        { 1, 2, 13, 1, 14, 1, 50, 50, 50, 50, 20 };
    const std::vector<float> reference =
        renderAdapter(*factory, requirements, referenceValues, input);

    int16_t sameReplaceValues[kAdapterParameterCount] =
        { 1, 2, 13, 1, 13, 1, 50, 50, 50, 50, 20 };
    const std::vector<float> sameReplace =
        renderAdapter(*factory, requirements, sameReplaceValues, input);
    for (int i = 0; i < kAdapterTestFrames; ++i) {
        const float expected = 0.5f * (busSample(reference, 13, i) +
                                       busSample(reference, 14, i));
        CHECK(test, nearlyEqual(busSample(sameReplace, 13, i), expected));
    }

    std::vector<float> addInput = input;
    for (int i = 0; i < kAdapterTestFrames; ++i) {
        addInput[static_cast<std::size_t>(12 * kAdapterTestFrames + i)] = 0.25f;
    }
    int16_t sameAddValues[kAdapterParameterCount] =
        { 1, 2, 13, 0, 13, 0, 50, 50, 50, 50, 20 };
    const std::vector<float> sameAdd =
        renderAdapter(*factory, requirements, sameAddValues, addInput);
    for (int i = 0; i < kAdapterTestFrames; ++i) {
        const float expected = 0.25f + 0.5f * (busSample(reference, 13, i) +
                                               busSample(reference, 14, i));
        CHECK(test, nearlyEqual(busSample(sameAdd, 13, i), expected));
    }

    int16_t mixedModeValues[kAdapterParameterCount] =
        { 1, 2, 13, 0, 13, 1, 50, 50, 50, 50, 20 };
    const std::vector<float> mixedMode =
        renderAdapter(*factory, requirements, mixedModeValues, addInput);
    for (int i = 0; i < kAdapterTestFrames; ++i) {
        const float expected = 0.5f * (busSample(reference, 13, i) +
                                       busSample(reference, 14, i));
        CHECK(test, nearlyEqual(busSample(mixedMode, 13, i), expected));
    }

    int16_t reverseMixedModeValues[kAdapterParameterCount] =
        { 1, 2, 13, 1, 13, 0, 50, 50, 50, 50, 20 };
    const std::vector<float> reverseMixedMode =
        renderAdapter(*factory, requirements, reverseMixedModeValues, addInput);
    for (int i = 0; i < kAdapterTestFrames; ++i) {
        const float expected = 0.5f * (busSample(reference, 13, i) +
                                       busSample(reference, 14, i));
        CHECK(test, nearlyEqual(busSample(reverseMixedMode, 13, i), expected));
    }

    int16_t aliasValues[kAdapterParameterCount] =
        { 1, 2, 1, 1, 2, 1, 50, 50, 50, 50, 20 };
    const std::vector<float> alias =
        renderAdapter(*factory, requirements, aliasValues, input);
    int16_t crossAliasValues[kAdapterParameterCount] =
        { 1, 2, 2, 1, 1, 1, 50, 50, 50, 50, 20 };
    const std::vector<float> crossAlias =
        renderAdapter(*factory, requirements, crossAliasValues, input);
    for (int i = 0; i < kAdapterTestFrames; ++i) {
        CHECK(test, nearlyEqual(busSample(alias, 1, i), busSample(reference, 13, i)));
        CHECK(test, nearlyEqual(busSample(alias, 2, i), busSample(reference, 14, i)));
        CHECK(test, nearlyEqual(busSample(crossAlias, 2, i), busSample(reference, 13, i)));
        CHECK(test, nearlyEqual(busSample(crossAlias, 1, i), busSample(reference, 14, i)));
    }

    int16_t invalidBusValues[kAdapterParameterCount] =
        { 0, 99, 0, 1, 99, 1, 50, 50, 50, 50, 20 };
    const std::vector<float> clamped =
        renderAdapter(*factory, requirements, invalidBusValues, input);
    for (int i = 0; i < kAdapterTestFrames; ++i) {
        CHECK(test, nearlyEqual(busSample(clamped, 13, i), busSample(reference, 13, i)));
        CHECK(test, nearlyEqual(busSample(clamped, 14, i), busSample(reference, 14, i)));
    }

    int16_t extremeInvalidBusValues[kAdapterParameterCount] =
        { -1, INT16_MAX, -1, 1, INT16_MAX, 1, 50, 50, 50, 50, 20 };
    const std::vector<float> extremeClamped =
        renderAdapter(*factory, requirements, extremeInvalidBusValues, input);
    for (int i = 0; i < kAdapterTestFrames; ++i) {
        CHECK(test,
              nearlyEqual(busSample(extremeClamped, 13, i),
                          busSample(reference, 13, i)));
        CHECK(test,
              nearlyEqual(busSample(extremeClamped, 14, i),
                          busSample(reference, 14, i)));
    }

    std::vector<float> lastBusInput(
        static_cast<std::size_t>(kNT_lastBus) * kAdapterTestFrames, 0.0f);
    lastBusInput[static_cast<std::size_t>(kNT_lastBus - 2) * kAdapterTestFrames] = 1.0f;
    lastBusInput[static_cast<std::size_t>(kNT_lastBus - 1) * kAdapterTestFrames] = -1.0f;
    int16_t lastBusValues[kAdapterParameterCount] =
        { kNT_lastBus - 1, kNT_lastBus, 13, 1, 14, 1, 50, 50, 50, 50, 20 };
    const std::vector<float> lastBus =
        renderAdapter(*factory, requirements, lastBusValues, lastBusInput);
    double energy = 0.0;
    for (int i = 0; i < kAdapterTestFrames; ++i) {
        energy += static_cast<double>(busSample(lastBus, 13, i)) * busSample(lastBus, 13, i);
        energy += static_cast<double>(busSample(lastBus, 14, i)) * busSample(lastBus, 14, i);
    }
    CHECK(test, energy > 0.0);
}

void testAdapterModulationRouting() {
    const char* test = "NT adapter modulation routes";
    const auto* factory = reinterpret_cast<const _NT_factory*>(
        pluginEntry(kNT_selector_factoryInfo, 0));
    CHECK(test, factory != nullptr);
    if (!factory) return;
    _NT_algorithmRequirements requirements{};
    factory->calculateRequirements(requirements, nullptr);
    AlgorithmInstance defaults = makeAlgorithm(*factory, requirements);
    CHECK(test, defaults.algorithm != nullptr);
    if (!defaults.algorithm) return;

    using Values = std::array<int16_t, kAdapterParameterCount>;
    Values base{};
    for (int p = 0; p < kAdapterParameterCount; ++p)
        base[p] = defaults.algorithm->parameters[p].def;
    base[kParamOutLMode] = base[kParamOutRMode] = 1;
    base[kParamCV1Bus] = 3;
    base[kParamCV2Bus] = 4;
    std::vector<float> input = makeAdapterInput();
    const auto setControlBus = [](std::vector<float>& busses, int bus, float value) {
        for (int i = 0; i < kAdapterTestFrames; ++i)
            busses[static_cast<std::size_t>(bus - 1) * kAdapterTestFrames + i] = value;
    };
    setControlBus(input, 3, 2.5f);
    setControlBus(input, 4, 5.0f);
    const auto render = [&](Values values, std::vector<float> busses,
                            int blockFrames = kAdapterTestFrames) {
        AlgorithmInstance instance = makeAlgorithm(*factory, requirements);
        instance.algorithm->vIncludingCommon = values.data();
        instance.algorithm->v = values.data();
        for (int p = 0; p < kAdapterParameterCount; ++p)
            factory->parameterChanged(instance.algorithm, p);
        for (int offset = 0; offset < kAdapterTestFrames; offset += blockFrames) {
            std::vector<float> block(static_cast<std::size_t>(kNT_lastBus) * blockFrames);
            for (int bus = 0; bus < kNT_lastBus; ++bus)
                for (int frame = 0; frame < blockFrames; ++frame)
                    block[static_cast<std::size_t>(bus) * blockFrames + frame] =
                        busses[static_cast<std::size_t>(bus) * kAdapterTestFrames + offset + frame];
            factory->step(instance.algorithm, block.data(), blockFrames / 4);
            for (int bus = 0; bus < kNT_lastBus; ++bus)
                for (int frame = 0; frame < blockFrames; ++frame)
                    busses[static_cast<std::size_t>(bus) * kAdapterTestFrames + offset + frame] =
                        block[static_cast<std::size_t>(bus) * blockFrames + frame];
        }
        return busses;
    };
    const auto compare = [&](const std::vector<float>& actual,
                             const std::vector<float>& expected) {
        for (int bus : { 13, 14 }) {
            for (int frame = 0; frame < kAdapterTestFrames; ++frame) {
                const float value = busSample(actual, bus, frame);
                CHECK(test, std::isfinite(value));
                CHECK(test, nearlyEqual(value, busSample(expected, bus, frame), 2.0e-6f));
            }
        }
    };
    const auto difference = [&](const std::vector<float>& left,
                                const std::vector<float>& right) {
        double result = 0.0;
        for (int bus : { 13, 14 })
            for (int frame = 0; frame < kAdapterTestFrames; ++frame)
                result += std::fabs(busSample(left, bus, frame) - busSample(right, bus, frame));
        return result;
    };
    const std::vector<float> unmodulated = render(base, input);
    Values reference = base;
    reference[kParamFreq] = 75;
    const std::vector<float> highFrequency = render(reference, input);
    reference[kParamFreq] = 25;
    const std::vector<float> lowFrequency = render(reference, input);
    Values routed = base;
    routed[kParamRoute1Source] = kSourceCV1;
    routed[kParamRoute1Destination] = kDestinationFrequency;
    routed[kParamRoute1Amount] = 50;
    const std::vector<float> positive = render(routed, input);
    compare(positive, highFrequency); // 2.5 V / 5 V * 50% = +25%.
    CHECK(test, difference(positive, unmodulated) > 1.0e-5);
    routed[kParamRoute1Amount] = -50;
    compare(render(routed, input), lowFrequency);
    routed[kParamRoute1Source] = kSourceCV2;
    routed[kParamRoute1Amount] = 25;
    compare(render(routed, input), highFrequency);
    routed[kParamRoute1Source] = kSourceOff;
    compare(render(routed, input), unmodulated);

    routed[kParamRoute1Source] = kSourceCV1;
    routed[kParamRoute1Amount] = 25;
    for (float voltage : { 5.0f, 50.0f }) {
        std::vector<float> controls = input;
        setControlBus(controls, 3, voltage);
        compare(render(routed, controls), highFrequency);
    }
    std::vector<float> negativeCV = input;
    setControlBus(negativeCV, 3, -5.0f);
    compare(render(routed, negativeCV), lowFrequency);
    for (float voltage : { NAN, INFINITY, -INFINITY }) {
        std::vector<float> controls = input;
        setControlBus(controls, 3, voltage);
        compare(render(routed, controls), unmodulated);
    }
    routed[kParamCV1Bus] = 0;
    compare(render(routed, input), unmodulated);
    routed[kParamCV1Bus] = 3;

    // Contributions must sum before clamping. Opposite full-scale routes cancel.
    routed[kParamRoute1Amount] = 100;
    routed[kParamRoute1Source + kRouteStride] = kSourceCV1;
    routed[kParamRoute1Destination + kRouteStride] = kDestinationFrequency;
    routed[kParamRoute1Amount + kRouteStride] = -100;
    compare(render(routed, input), unmodulated);
    routed[kParamRoute1Amount + kRouteStride] = 0;
    routed[kParamRoute1Source] = INT16_MAX;
    compare(render(routed, input), unmodulated);
    routed[kParamRoute1Source] = kSourceCV1;
    routed[kParamRoute1Destination] = INT16_MAX;
    compare(render(routed, input), unmodulated);

    // Each modulator's signed attenuation and offset feed the same route matrix.
    for (int mod = 0; mod < 2; ++mod) {
        Values modulation = base;
        const int p = kParamMod1Shape + mod * 5;
        modulation[p] = 4; // Square is +1 throughout this short render.
        modulation[p + 2] = -50;
        modulation[p + 3] = 100;
        modulation[p + 4] = 0;
        modulation[kParamRoute1Source] = static_cast<int16_t>(kSourceMod1 + mod);
        modulation[kParamRoute1Destination] = kDestinationFrequency;
        modulation[kParamRoute1Amount] = 50;
        compare(render(modulation, input), highFrequency);
    }

    // Exercise all destination bindings against their equivalent manual value.
    const int targetParameter[kDestinationCount] = {
        kParamFreq, kParamRes, kParamStages, kParamDist, kParamFb,
        kParamStereoSpread, kParamStereoMotion, kParamStereoCrossover
    };
    for (int destination = 0; destination < kDestinationCount; ++destination) {
        Values manual = base;
        manual[kParamStereoSpread] = 40;
        Values modulation = manual;
        const int p = targetParameter[destination];
        manual[p] = static_cast<int16_t>(manual[p] +
            (destination == kDestinationCrossover ? 980 : 25));
        modulation[kParamRoute1Source] = kSourceCV2;
        modulation[kParamRoute1Destination] = static_cast<int16_t>(destination);
        modulation[kParamRoute1Amount] = 25;
        compare(render(modulation, input), render(manual, input));
    }

    // A CV bus may also be a Replace output: sample its original value first.
    Values alias = base;
    alias[kParamCV1Bus] = 13;
    alias[kParamRoute1Source] = kSourceCV1;
    alias[kParamRoute1Destination] = kDestinationFrequency;
    alias[kParamRoute1Amount] = 25;
    std::vector<float> aliasInput = input;
    setControlBus(aliasInput, 13, 5.0f);
    compare(render(alias, aliasInput), highFrequency);

    // Control cadence and the musical clock must persist across host blocks.
    Values animated = base;
    animated[kParamStereoSpread] = 80;
    animated[kParamMod2Rate] = 0;
    animated[kParamRoute1Source] = kSourceCV1;
    animated[kParamRoute1Destination] = kDestinationFrequency;
    animated[kParamRoute1Amount] = 30;
    animated[kParamRoute1Source + kRouteStride] = kSourceMod2;
    animated[kParamRoute1Destination + kRouteStride] = kDestinationMotion;
    animated[kParamRoute1Amount + kRouteStride] = 50;
    std::vector<float> changingCV = input;
    for (int frame = 0; frame < kAdapterTestFrames; ++frame)
        changingCV[2 * kAdapterTestFrames + frame] =
            -5.0f + 10.0f * static_cast<float>(frame) / kAdapterTestFrames;
    const std::vector<float> fullBlock = render(animated, changingCV);
    compare(render(animated, changingCV, 4), fullBlock);
    compare(render(animated, changingCV, 16), fullBlock);

    // Exercise the real factory callback and CV step wiring together. Two
    // 24-PPQN pulses at 120 BPM cross the square LFO's half-cycle at 1/64.
    const auto renderClock = [&](int clockSource, bool secondPulse, bool stop,
                                 bool manualLow = false) {
        AlgorithmInstance instance = makeAlgorithm(*factory, requirements);
        Values values = base;
        values[kParamClockSource] = static_cast<int16_t>(clockSource);
        values[kParamClockCV] = 3;
        values[kParamClockPPQN] = 3; // 24 PPQN, matching MIDI F8.
        values[kParamMod1Shape] = 4;
        values[kParamMod1Rate] = 0;
        values[kParamRoute1Source] = manualLow ? kSourceOff : kSourceMod1;
        values[kParamRoute1Destination] = kDestinationFrequency;
        values[kParamRoute1Amount] = 25;
        if (manualLow) values[kParamFreq] = 25;
        instance.algorithm->vIncludingCommon = values.data();
        instance.algorithm->v = values.data();
        for (int p = 0; p < kAdapterParameterCount; ++p)
            factory->parameterChanged(instance.algorithm, p);
        if (clockSource == 2) factory->midiRealtime(instance.algorithm, 0xfa);
        std::vector<float> silence(static_cast<std::size_t>(kNT_lastBus) * 4, 0.0f);
        for (int sample = 0; sample <= 1000; sample += 4) {
            const bool pulse = sample == 0 || (sample == 1000 && secondPulse);
            if (clockSource == 2 && pulse)
                factory->midiRealtime(instance.algorithm, 0xf8);
            if (clockSource == 2 && stop && sample == 1000)
                factory->midiRealtime(instance.algorithm, 0xfc);
            silence[8] = clockSource == 1 && pulse ? 5.0f : 0.0f;
            factory->step(instance.algorithm, silence.data(), 1);
        }
        if (stop) {
            silence[8] = 0.0f;
            for (int sample = 0; sample < 12000; sample += 4)
                factory->step(instance.algorithm, silence.data(), 1);
        }
        std::vector<float> audio = makeAdapterInput();
        // Keep the clock low through the audio block; CV1/2 aren't routed here.
        setControlBus(audio, 3, 0.0f);
        factory->step(instance.algorithm, audio.data(), kAdapterTestFrames / 4);
        return audio;
    };
    const std::vector<float> midiClocked = renderClock(2, true, false);
    const std::vector<float> cvClocked = renderClock(1, true, false);
    compare(midiClocked, cvClocked);
    CHECK(test, difference(midiClocked, renderClock(2, false, false)) > 1.0e-5);
    CHECK(test, difference(cvClocked, renderClock(1, false, false)) > 1.0e-5);
    compare(renderClock(2, true, true), renderClock(2, true, true, true));
}

} // namespace

int main() {
    testParamSmoother();
    testAllpassSection();
    testFastCosApproximation();
    testCoefficientInterpolation();
    testAllpassNetwork();
    testSmearCoreReinitialization();
    testSmearCoreSilenceAndStability();
    testExtremeFiniteRecovery();
    testSmearCoreBlockAndInPlace();
    testSupportedSampleRates();
    testAdapterContract();
    testAdapterModeMatrixAndDestinations();
    testAdapterStepGuards();
    testAdapterMonoInputAndInstanceIsolation();
    testAdapterRoutingEdges();
    testAdapterModulationRouting();
    gFailures += runModulationTests();
    gFailures += runStereoTests();

    if (gFailures != 0) {
        std::fprintf(stderr, "%d test assertion(s) failed.\n", gFailures);
        return 1;
    }
    std::printf("All SmearNT tests passed.\n");
    return 0;
}
