#include "AllpassSection.h"

void AllpassSection::init(float sampleRate) {
    sampleRate_ = (sampleRate > 100.0f) ? sampleRate : 48000.0f;
    a1_ = 0.0f;
    a2_ = 0.0f;
    x1_ = x2_ = y1_ = y2_ = 0.0f;
}

void AllpassSection::setPole(float freqHz, float radius) {
    float freq = (freqHz > 1.0f) ? freqHz : 1.0f;
    float r = radius;
    if (r < 0.1f) r = 0.1f;
    // Clamp to keep poles well inside unit circle; conservative to avoid blowups.
    if (r > 0.9f) r = 0.9f;
    float w = 2.0f * 3.14159265358979323846f * freq / sampleRate_;
    float cosw = cosf(w);
    if (cosw > 1.0f) cosw = 1.0f;
    if (cosw < -1.0f) cosw = -1.0f;
    a1_ = -2.0f * r * cosw;
    a2_ = r * r;
    // Keep state to preserve continuity across retuning
}

float AllpassSection::process(float x) {
    // Direct Form I biquad with allpass coefficients:
    // b0 = a2, b1 = a1, b2 = 1, a0 = 1, a1 = a1_, a2 = a2_
    float y = a2_ * x + a1_ * x1_ + x2_ - a1_ * y1_ - a2_ * y2_;
    x2_ = x1_;
    x1_ = x;
    y2_ = y1_;
    y1_ = y;
    return y;
}
