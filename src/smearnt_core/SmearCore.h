#pragma once

#include "AllpassNetwork.h"
#include "ParamSmoother.h"
#include <array>
#include <cstdint>

class SmearCore {
public:
    SmearCore() = default;

    void init(float sampleRate);

    void setFrequency(float norm);         // 0..1
    void setResonance(float norm);         // 0..1
    void setStages(float norm);            // 0..1
    void setDistributionMorph(float norm); // 0..1
    void setFeedback(float norm);          // 0..1

    void processBlock(const float* inL, const float* inR,
                      float* outL, float* outR,
                      int numSamples);
    void processSample(float inL, float inR, float& outL, float& outR);

private:
    void computeBaseDistributions();
    void updateCoefficients(float freqNorm, float resNorm, float distNorm);
    void computeMorphFrequencies(float freqNorm, float distNorm, float* dst);
    float clamp01(float x) const;
    float saturate(float x) const;

    static constexpr float kMinFreq = 200.0f;
    static constexpr float kMaxFreq = 12000.0f;
    static constexpr float kMinRadius = 0.6f;
    static constexpr float kMaxRadius = 0.8f;
    static constexpr float kMaxFeedback = 0.6f;
    static constexpr float kCoeffChangeThreshold = 0.00001f;
    static constexpr int kCoeffUpdateInterval = 32;

    float sampleRate_ = 48000.0f;

    AllpassNetwork left_;
    AllpassNetwork right_;

    std::array<float, AllpassNetwork::kNumSections> distLinear_{};
    std::array<float, AllpassNetwork::kNumSections> distLog_{};
    std::array<float, AllpassNetwork::kNumSections> distCluster_{};
    std::array<float, AllpassNetwork::kNumSections> currentFreqs_{};
    std::array<float, AllpassNetwork::kNumSections> rightFreqs_{};

    ParamSmoother freqSm_;
    ParamSmoother resSm_;
    ParamSmoother stagesSm_;
    ParamSmoother distSm_;
    ParamSmoother fbSm_;

    float feedbackStateL_ = 0.0f;
    float feedbackStateR_ = 0.0f;
    float lastCoeffFreq_ = -1.0f;
    float lastCoeffRes_ = -1.0f;
    float lastCoeffDist_ = -1.0f;
    int coeffUpdateCounter_ = 0;
};
