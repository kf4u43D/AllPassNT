#pragma once

#include "AllpassSection.h"
#include <array>
#include <cstddef>
#include <cstdint>

class AllpassNetwork {
public:
    static constexpr int kNumSections = 24;
    static constexpr int kNumTaps = 6;
    static constexpr int kCoefficientRampSamples = 32;

    AllpassNetwork() = default;

    void init(float sampleRate);
    void setAllpassFrequencies(const float* freqsHz, float radius);
    float processSample(float x, float stagesNorm);

    float getLastTap(int tap) const {
        return (tap >= 0 && tap < kNumTaps)
            ? lastTaps_[static_cast<std::size_t>(tap)]
            : 0.0f;
    }
    float getLastOutput() const { return lastOutput_; }
    float getSectionA1(int section) const {
        return (section >= 0 && section < kNumSections)
            ? sections_[static_cast<std::size_t>(section)].getA1()
            : 0.0f;
    }
    float getSectionA2(int section) const {
        return (section >= 0 && section < kNumSections)
            ? sections_[static_cast<std::size_t>(section)].getA2()
            : 0.0f;
    }
    int getCoefficientRampRemaining() const { return coefficientRampRemaining_; }

private:
    float crossfadeTaps(float stagesNorm) const;

    std::array<AllpassSection, kNumSections> sections_{};
    std::array<int, kNumTaps> tapIndices_{{3, 7, 11, 15, 19, kNumSections - 1}};
    std::array<float, kNumTaps> lastTaps_{{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
    float lastOutput_ = 0.0f;
    float sampleRate_ = 48000.0f;
    int coefficientRampRemaining_ = 0;
    bool receivedFrequencies_ = false;
};
