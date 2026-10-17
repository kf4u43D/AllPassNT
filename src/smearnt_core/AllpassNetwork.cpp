#include "AllpassNetwork.h"

void AllpassNetwork::init(float sampleRate) {
    sampleRate_ = (sampleRate > 100.0f) ? sampleRate : 48000.0f;
    lastTaps_.fill(0.0f);
    lastOutput_ = 0.0f;
    coefficientRampRemaining_ = 0;
    receivedFrequencies_ = false;
    for (auto& s : sections_) {
        s.init(sampleRate_);
        s.setPole(200.0f, 0.7f);
    }
}

void AllpassNetwork::setAllpassFrequencies(const float* freqsHz, float radius) {
    if (!freqsHz) return;
    if (!receivedFrequencies_) {
        for (std::size_t i = 0; i < sections_.size(); ++i) {
            sections_[i].setPole(freqsHz[i], radius);
        }
        receivedFrequencies_ = true;
        coefficientRampRemaining_ = 0;
        return;
    }

    for (std::size_t i = 0; i < sections_.size(); ++i) {
        sections_[i].setPoleTarget(freqsHz[i], radius, kCoefficientRampSamples);
    }
    coefficientRampRemaining_ = kCoefficientRampSamples;
}

float AllpassNetwork::crossfadeTaps(float stagesNorm) const {
    if (!std::isfinite(stagesNorm)) stagesNorm = 0.0f;
    if (stagesNorm < 0.0f) stagesNorm = 0.0f;
    if (stagesNorm > 1.0f) stagesNorm = 1.0f;
    float idx = stagesNorm * float(kNumTaps - 1);
    int a = (idx < 0.0f) ? 0 : int(idx);
    if (a > kNumTaps - 2) a = kNumTaps - 2;
    int b = a + 1;
    float frac = idx - float(a);
    float va = lastTaps_[static_cast<std::size_t>(a)];
    float vb = lastTaps_[static_cast<std::size_t>(b)];
    return va + (vb - va) * frac;
}

float AllpassNetwork::processSample(float x, float stagesNorm) {
    if (coefficientRampRemaining_ > 0) {
        const bool finalSample = coefficientRampRemaining_ == 1;
        for (auto& section : sections_) {
            section.advancePoleInterpolation(finalSample);
        }
        --coefficientRampRemaining_;
    }

    float y = x;
    std::size_t tapCursor = 0;
    for (std::size_t i = 0; i < sections_.size(); ++i) {
        y = sections_[i].process(y);
        if (tapCursor < lastTaps_.size() && static_cast<int>(i) == tapIndices_[tapCursor]) {
            lastTaps_[tapCursor] = y;
            ++tapCursor;
        }
    }
    lastOutput_ = crossfadeTaps(stagesNorm);
    return lastOutput_;
}
