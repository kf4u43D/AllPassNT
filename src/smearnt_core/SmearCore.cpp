#include "SmearCore.h"
#include <cmath>

namespace {
float absf(float x) {
    return (x < 0.0f) ? -x : x;
}
}

void SmearCore::init(float sampleRate) {
    sampleRate_ = (sampleRate > 100.0f) ? sampleRate : 48000.0f;

    freqSm_.init(sampleRate_, 10.0f);
    resSm_.init(sampleRate_, 10.0f);
    stagesSm_.init(sampleRate_, 5.0f);
    distSm_.init(sampleRate_, 20.0f);
    fbSm_.init(sampleRate_, 20.0f);

    freqSm_.setImmediate(0.5f);
    resSm_.setImmediate(0.5f);
    stagesSm_.setImmediate(0.5f);
    distSm_.setImmediate(0.5f);
    fbSm_.setImmediate(0.2f);

    left_.init(sampleRate_);
    right_.init(sampleRate_);

    computeBaseDistributions();
    updateCoefficients(0.5f, 0.5f, 0.5f);

    feedbackStateL_ = 0.0f;
    feedbackStateR_ = 0.0f;
    lastCoeffFreq_ = 0.5f;
    lastCoeffRes_ = 0.5f;
    lastCoeffDist_ = 0.5f;
    coeffUpdateCounter_ = 0;
}

void SmearCore::setFrequency(float norm) { freqSm_.setTarget(clamp01(norm)); }
void SmearCore::setResonance(float norm) { resSm_.setTarget(clamp01(norm)); }
void SmearCore::setStages(float norm) { stagesSm_.setTarget(clamp01(norm)); }
void SmearCore::setDistributionMorph(float norm) { distSm_.setTarget(clamp01(norm)); }
void SmearCore::setFeedback(float norm) { fbSm_.setTarget(clamp01(norm)); }

float SmearCore::clamp01(float x) const {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

float SmearCore::saturate(float x) const {
    const float limit = 2.0f;
    return (limit * x) / (limit + absf(x));
}

void SmearCore::computeBaseDistributions() {
    float nyquist = 0.45f * sampleRate_;
    for (int i = 0; i < AllpassNetwork::kNumSections; ++i) {
        float frac = float(i) / float(AllpassNetwork::kNumSections - 1);
        float linear = kMinFreq + frac * (kMaxFreq - kMinFreq);
        distLinear_[i] = (linear < nyquist) ? linear : nyquist;

        float logfHz = kMinFreq * expf(logf(kMaxFreq / kMinFreq) * frac);
        distLog_[i] = (logfHz < nyquist) ? logfHz : nyquist;

        float clusterFrac = frac;
        float clusterHz;
        if (clusterFrac < 0.4f) {
            float inner = clusterFrac / 0.4f;
            clusterHz = kMinFreq + inner * inner * 1500.0f;
        } else if (clusterFrac < 0.8f) {
            float inner = (clusterFrac - 0.4f) / 0.4f;
            clusterHz = 2000.0f + inner * inner * 6000.0f;
        } else {
            float inner = (clusterFrac - 0.8f) / 0.2f;
            clusterHz = 8000.0f + inner * 7000.0f;
        }
        if (clusterHz > nyquist) clusterHz = nyquist;
        if (clusterHz < kMinFreq) clusterHz = kMinFreq;
        distCluster_[i] = clusterHz;
    }
}

void SmearCore::computeMorphFrequencies(float freqNorm, float distNorm, float* dst) {
    // Shift distribution by up to +/-2 octaves around center using log2 scaling.
    float shiftOct = (freqNorm - 0.5f) * 4.0f; // -2..+2 octaves
    float freqWarp = expf(shiftOct * 0.6931471805599453f); // 2^(shiftOct)
    float nyquist = 0.45f * sampleRate_;

    float mix = (distNorm < 0.5f) ? distNorm * 2.0f : (distNorm - 0.5f) * 2.0f;
    const float* baseA = (distNorm < 0.5f) ? distLinear_.data() : distLog_.data();
    const float* baseB = (distNorm < 0.5f) ? distLog_.data() : distCluster_.data();

    for (int i = 0; i < AllpassNetwork::kNumSections; ++i) {
        float f = baseA[i] + (baseB[i] - baseA[i]) * mix;
        f *= freqWarp;
        if (f > nyquist) f = nyquist;
        if (f < kMinFreq) f = kMinFreq;
        dst[i] = f;
    }
}

void SmearCore::updateCoefficients(float freqNorm, float resNorm, float distNorm) {
    if (absf(freqNorm - lastCoeffFreq_) < kCoeffChangeThreshold &&
        absf(resNorm - lastCoeffRes_) < kCoeffChangeThreshold &&
        absf(distNorm - lastCoeffDist_) < kCoeffChangeThreshold) {
        return;
    }

    computeMorphFrequencies(freqNorm, distNorm, currentFreqs_.data());
    const float nyquist = 0.45f * sampleRate_;
    for (int i = 0; i < AllpassNetwork::kNumSections; ++i) {
        const float offset = (i & 1) ? 1.0125f : 0.9875f;
        float f = currentFreqs_[i] * offset;
        if (f > nyquist) f = nyquist;
        if (f < kMinFreq) f = kMinFreq;
        rightFreqs_[i] = f;
    }
    float radius = kMinRadius + (kMaxRadius - kMinRadius) * resNorm;
    left_.setAllpassFrequencies(currentFreqs_.data(), radius);
    right_.setAllpassFrequencies(rightFreqs_.data(), radius);
    lastCoeffFreq_ = freqNorm;
    lastCoeffRes_ = resNorm;
    lastCoeffDist_ = distNorm;
}

void SmearCore::processBlock(const float* inL, const float* inR,
                             float* outL, float* outR,
                             int numSamples) {
    if (!inL || !inR || !outL || !outR) return;

    for (int i = 0; i < numSamples; ++i) {
        processSample(inL[i], inR[i], outL[i], outR[i]);
    }
}

void SmearCore::processSample(float inL, float inR, float& outL, float& outR) {
    const float freqNorm = freqSm_.getNextValue();
    const float resNorm = resSm_.getNextValue();
    const float distNorm = distSm_.getNextValue();
    const float stages = stagesSm_.getNextValue();
    const float fb = fbSm_.getNextValue() * kMaxFeedback;

    // Refresh coefficients periodically to allow smooth sweeps without heavy per-sample cost.
    if (coeffUpdateCounter_ == 0) {
        updateCoefficients(freqNorm, resNorm, distNorm);
    }
    coeffUpdateCounter_ = (coeffUpdateCounter_ + 1) & (kCoeffUpdateInterval - 1);

    const float yL = left_.processSample(inL + feedbackStateL_, stages);
    const float yR = right_.processSample(inR + feedbackStateR_, stages);

    feedbackStateL_ = saturate(yL) * fb;
    feedbackStateR_ = saturate(yR) * fb;

    outL = yL;
    outR = yR;
}
