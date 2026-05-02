#include "AllpassNetwork.h"

void AllpassNetwork::init(float sampleRate) {
    sampleRate_ = (sampleRate > 100.0f) ? sampleRate : 48000.0f;
    lastTaps_.fill(0.0f);
    lastOutput_ = 0.0f;
    for (auto& s : sections_) {
        s.init(sampleRate_);
        s.setPole(200.0f, 0.7f);
    }
}

void AllpassNetwork::setAllpassFrequencies(const float* freqsHz, float radius) {
    for (int i = 0; i < kNumSections; ++i) {
        sections_[i].setPole(freqsHz[i], radius);
    }
}

float AllpassNetwork::crossfadeTaps(float stagesNorm) const {
    if (stagesNorm < 0.0f) stagesNorm = 0.0f;
    if (stagesNorm > 1.0f) stagesNorm = 1.0f;
    float idx = stagesNorm * float(kNumTaps - 1);
    int a = (idx < 0.0f) ? 0 : int(idx);
    if (a > kNumTaps - 2) a = kNumTaps - 2;
    int b = a + 1;
    float frac = idx - float(a);
    float va = lastTaps_[a];
    float vb = lastTaps_[b];
    return va + (vb - va) * frac;
}

float AllpassNetwork::processSample(float x, float stagesNorm) {
    float y = x;
    int tapCursor = 0;
    for (int i = 0; i < kNumSections; ++i) {
        y = sections_[i].process(y);
        if (tapCursor < kNumTaps && i == tapIndices_[tapCursor]) {
            lastTaps_[tapCursor] = y;
            ++tapCursor;
        }
    }
    lastOutput_ = crossfadeTaps(stagesNorm);
    return lastOutput_;
}
