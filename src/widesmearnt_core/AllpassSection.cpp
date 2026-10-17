#include "AllpassSection.h"

void AllpassSection::init(float sampleRate) {
    sampleRate_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0f && sampleRate <= 384000.0f)
        ? sampleRate
        : 48000.0f;
    a1_ = 0.0f;
    a2_ = 0.0f;
    a1Step_ = 0.0f;
    a2Step_ = 0.0f;
    targetA1_ = 0.0f;
    targetA2_ = 0.0f;
    x1_ = x2_ = y1_ = y2_ = 0.0f;
}

float AllpassSection::fastCos(float radians) {
    // cos(x) = sin(pi/2 - x). Centering the domain this way covers
    // x in [0, 0.9*pi] without a range-reduction branch. The polynomial is
    // evaluated as an odd minimax-like sine approximation on [-pi/2, pi/2].
    constexpr float kHalfPi = 1.57079632679489661923f;
    constexpr float kHalfPiTail = -4.3711388286737928865e-8f;
    const float y = (kHalfPi - radians) + kHalfPiTail;
    const float y2 = y * y;

    float p = 2.6051077384181553e-6f;
    p = p * y2 - 1.9809017248917370e-4f;
    p = p * y2 + 8.3330497145652771e-3f;
    p = p * y2 - 1.6666658222675323e-1f;
    p = p * y2 + 1.0f;
    return y * p;
}

void AllpassSection::computePole(float freqHz, float radius,
                                 float& a1, float& a2) const {
    float freq = (std::isfinite(freqHz) && freqHz > 1.0f) ? freqHz : 1.0f;
    const float maxFreq = 0.45f * sampleRate_;
    if (freq > maxFreq) freq = maxFreq;
    float r = std::isfinite(radius) ? radius : 0.7f;
    if (r < 0.1f) r = 0.1f;
    // Keep a 3% radial margin. The smallest Jury margin is (1-r)^2,
    // at least 0.0009; linear interpolation stays inside the convex stability
    // triangle, including at the extended 20 Hz frequency limit.
    if (r > 0.97f) r = 0.97f;
    float w = 2.0f * 3.14159265358979323846f * freq / sampleRate_;
    float cosw = fastCos(w);
    if (cosw > 1.0f) cosw = 1.0f;
    if (cosw < -1.0f) cosw = -1.0f;
    a1 = -2.0f * r * cosw;
    a2 = r * r;
}

void AllpassSection::setPole(float freqHz, float radius) {
    computePole(freqHz, radius, a1_, a2_);
    targetA1_ = a1_;
    targetA2_ = a2_;
    a1Step_ = 0.0f;
    a2Step_ = 0.0f;
    // Keep state to preserve continuity across retuning
}

void AllpassSection::setPoleTarget(float freqHz, float radius, int numSamples) {
    if (numSamples <= 0) {
        setPole(freqHz, radius);
        return;
    }

    computePole(freqHz, radius, targetA1_, targetA2_);
    const float inverseSamples = 1.0f / static_cast<float>(numSamples);
    a1Step_ = (targetA1_ - a1_) * inverseSamples;
    a2Step_ = (targetA2_ - a2_) * inverseSamples;
}

void AllpassSection::advancePoleInterpolation(bool finalSample) {
    if (finalSample) {
        a1_ = targetA1_;
        a2_ = targetA2_;
        a1Step_ = 0.0f;
        a2Step_ = 0.0f;
    } else {
        a1_ += a1Step_;
        a2_ += a2Step_;
    }
}

float AllpassSection::process(float x) {
    // Direct Form I biquad with allpass coefficients:
    // b0 = a2, b1 = a1, b2 = 1, a0 = 1, a1 = a1_, a2 = a2_
    float y = a2_ * x + a1_ * x1_ + x2_ - a1_ * y1_ - a2_ * y2_;
    // Prevent a decaying IIR tail from remaining in the subnormal range.
    if (y > -1.0e-20f && y < 1.0e-20f) y = 0.0f;
    x2_ = x1_;
    x1_ = x;
    y2_ = y1_;
    y1_ = y;
    return y;
}
