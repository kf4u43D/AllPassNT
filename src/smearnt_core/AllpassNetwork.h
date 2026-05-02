#pragma once

#include "AllpassSection.h"
#include <array>
#include <cstdint>

class AllpassNetwork {
public:
    static constexpr int kNumSections = 24;
    static constexpr int kNumTaps = 6;

    AllpassNetwork() = default;

    void init(float sampleRate);
    void setAllpassFrequencies(const float* freqsHz, float radius);
    float processSample(float x, float stagesNorm);

    float getLastTap(int tap) const { return (tap >= 0 && tap < kNumTaps) ? lastTaps_[tap] : 0.0f; }
    float getLastOutput() const { return lastOutput_; }

private:
    float crossfadeTaps(float stagesNorm) const;

    std::array<AllpassSection, kNumSections> sections_{};
    std::array<int, kNumTaps> tapIndices_{{3, 7, 11, 15, 19, kNumSections - 1}};
    std::array<float, kNumTaps> lastTaps_{{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
    float lastOutput_ = 0.0f;
    float sampleRate_ = 48000.0f;
};
