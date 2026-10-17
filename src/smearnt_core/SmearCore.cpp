#include "SmearCore.h"
#include <cmath>

namespace {
float absf(float x) {
    return (x < 0.0f) ? -x : x;
}

// Only used at initialization for a positive frequency ratio in [180, 8640].
// Avoid adding a firmware runtime dependency for this initialization helper.
float logFrequencyRatio(float ratio) {
    int octaves = 0;
    while (ratio >= 2.0f) {
        ratio *= 0.5f;
        ++octaves;
    }
    const float x = (ratio - 1.0f) / (ratio + 1.0f);
    const float x2 = x * x;
    const float series = x * (1.0f + x2 * (1.0f / 3.0f + x2 *
        (1.0f / 5.0f + x2 * (1.0f / 7.0f + x2 *
        (1.0f / 9.0f + x2 / 11.0f)))));
    return static_cast<float>(octaves) * 0.6931471805599453f + 2.0f * series;
}
}

void SmearCore::init(float sampleRate) {
    sampleRate_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0f && sampleRate <= 384000.0f)
        ? sampleRate
        : 48000.0f;

    freqSm_.init(sampleRate_, 10.0f);
    resSm_.init(sampleRate_, 10.0f);
    stagesSm_.init(sampleRate_, 5.0f);
    distSm_.init(sampleRate_, 20.0f);
    fbSm_.init(sampleRate_, 20.0f);
    spreadSm_.init(sampleRate_, 20.0f);
    motionSm_.init(sampleRate_, 10.0f);
    crossoverSm_.init(sampleRate_, 20.0f);

    freqSm_.setImmediate(0.5f);
    resSm_.setImmediate(0.5f);
    stagesSm_.setImmediate(0.5f);
    distSm_.setImmediate(0.5f);
    fbSm_.setImmediate(0.2f);
    spreadSm_.setImmediate(0.0f);
    motionSm_.setImmediate(0.0f);
    crossoverSm_.setImmediate(1.0f - expf(-6.283185307179586f * 250.0f / sampleRate_));
    stereoLowMid_ = 0.0f;
    stereoLowSide_ = 0.0f;
    stereoDecorrelatorState_ = 0.0f;
    stereoDecorrelatorCoefficient_ = -expf(-6.283185307179586f * 2000.0f / sampleRate_);
    stereoCrossoverHz_ = 250.0f;

    left_.init(sampleRate_);
    right_.init(sampleRate_);

    computeBaseDistributions();
    // Network initialization resets its poles, so cached parameter values
    // must not suppress coefficient design when this instance is reused.
    lastCoeffFreq_ = -1.0f;
    lastCoeffRes_ = -1.0f;
    lastCoeffDist_ = -1.0f;
    updateCoefficients(0.5f, 0.5f, 0.5f);

    feedbackStateL_ = 0.0f;
    feedbackStateR_ = 0.0f;
    coeffUpdateCounter_ = 0;
}

void SmearCore::setFrequency(float norm) { freqSm_.setTarget(clamp01(norm)); }
void SmearCore::setResonance(float norm) { resSm_.setTarget(clamp01(norm)); }
void SmearCore::setStages(float norm) { stagesSm_.setTarget(clamp01(norm)); }
void SmearCore::setDistributionMorph(float norm) { distSm_.setTarget(clamp01(norm)); }
void SmearCore::setFeedback(float norm) { fbSm_.setTarget(clamp01(norm)); }
void SmearCore::setStereoSpread(float norm) { spreadSm_.setTarget(clamp01(norm)); }
void SmearCore::setStereoMotion(float position) {
    if (!std::isfinite(position)) position = 0.0f;
    if (position < -1.0f) position = -1.0f;
    if (position > 1.0f) position = 1.0f;
    motionSm_.setTarget(position);
}
void SmearCore::setStereoCrossover(float hz) {
    if (!std::isfinite(hz)) hz = 250.0f;
    if (hz < 80.0f) hz = 80.0f;
    if (hz > 4000.0f) hz = 4000.0f;
    if (absf(hz - stereoCrossoverHz_) < 0.0001f) return;
    stereoCrossoverHz_ = hz;
    crossoverSm_.setTarget(1.0f - expf(-6.283185307179586f * hz / sampleRate_));
}

float SmearCore::clamp01(float x) const {
    if (!std::isfinite(x)) return 0.0f;
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

float SmearCore::saturate(float x) const {
    if (!std::isfinite(x)) return 0.0f;
    const float limit = 2.0f;
    // Divide first so extreme finite inputs cannot overflow limit * x.
    return limit * (x / (limit + absf(x)));
}

void SmearCore::computeBaseDistributions() {
    const float maxFreq = 0.45f * sampleRate_;
    const float span = maxFreq - kMinFreq;
    const float logRatio = logFrequencyRatio(maxFreq / kMinFreq);
    for (std::size_t i = 0; i < distLinear_.size(); ++i) {
        float frac = static_cast<float>(i) / static_cast<float>(AllpassNetwork::kNumSections - 1);
        distLinear_[i] = kMinFreq + frac * span;

        distLog_[i] = kMinFreq * expf(logRatio * frac);

        float clusterFrac = frac;
        float clusterHz;
        if (clusterFrac < 0.4f) {
            float inner = clusterFrac / 0.4f;
            clusterHz = kMinFreq + inner * inner * 0.1f * span;
        } else if (clusterFrac < 0.8f) {
            float inner = (clusterFrac - 0.4f) / 0.4f;
            clusterHz = kMinFreq + (0.1f + inner * inner * 0.45f) * span;
        } else {
            float inner = (clusterFrac - 0.8f) / 0.2f;
            clusterHz = kMinFreq + (0.55f + inner * 0.45f) * span;
        }
        if (clusterHz < kMinFreq) clusterHz = kMinFreq;
        distCluster_[i] = clusterHz;
    }
}

void SmearCore::computeMorphFrequencies(float freqNorm, float distNorm, float* dst) {
    // Sweep the whole distribution by +/-4 octaves, clipped to 20 Hz..0.45 Fs.
    float shiftOct = (freqNorm - 0.5f) * 8.0f;
    float freqWarp = expf(shiftOct * 0.6931471805599453f); // 2^(shiftOct)
    float nyquist = 0.45f * sampleRate_;

    float mix = (distNorm < 0.5f) ? distNorm * 2.0f : (distNorm - 0.5f) * 2.0f;
    const float* baseA = (distNorm < 0.5f) ? distLinear_.data() : distLog_.data();
    const float* baseB = (distNorm < 0.5f) ? distLog_.data() : distCluster_.data();

    for (std::size_t i = 0; i < currentFreqs_.size(); ++i) {
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
    for (std::size_t i = 0; i < currentFreqs_.size(); ++i) {
        const float offset = (i & 1U) ? 1.0125f : 0.9875f;
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
    if (!std::isfinite(inL)) inL = 0.0f;
    if (!std::isfinite(inR)) inR = 0.0f;
    if (inL > kMaxInputMagnitude) inL = kMaxInputMagnitude;
    if (inL < -kMaxInputMagnitude) inL = -kMaxInputMagnitude;
    if (inR > kMaxInputMagnitude) inR = kMaxInputMagnitude;
    if (inR < -kMaxInputMagnitude) inR = -kMaxInputMagnitude;

    const float freqNorm = freqSm_.getNextValue();
    const float resNorm = resSm_.getNextValue();
    const float distNorm = distSm_.getNextValue();
    const float stages = stagesSm_.getNextValue();
    const float fb = fbSm_.getNextValue() * kMaxFeedback;

    // Refresh coefficients periodically to allow smooth sweeps without heavy per-sample cost.
    if (coeffUpdateCounter_ == 0) {
        updateCoefficients(freqNorm, resNorm, distNorm);
    }
    static_assert((kCoeffUpdateInterval & (kCoeffUpdateInterval - 1)) == 0,
                  "kCoeffUpdateInterval must be a power of two");
    coeffUpdateCounter_ = (coeffUpdateCounter_ + 1) & (kCoeffUpdateInterval - 1);

    const float yL = left_.processSample(inL + feedbackStateL_, stages);
    const float yR = right_.processSample(inR + feedbackStateR_, stages);

    feedbackStateL_ = saturate(yL) * fb;
    feedbackStateR_ = saturate(yR) * fb;
    if (feedbackStateL_ > -1.0e-20f && feedbackStateL_ < 1.0e-20f) feedbackStateL_ = 0.0f;
    if (feedbackStateR_ > -1.0e-20f && feedbackStateR_ < 1.0e-20f) feedbackStateR_ = 0.0f;

    processStereo(yL, yR, outL, outR);
}

void SmearCore::processStereo(float inL, float inR, float& outL, float& outR) {
    const float spread = spreadSm_.getNextValue();
    const float motion = motionSm_.getNextValue();
    const float crossover = crossoverSm_.getNextValue();
    const float mid = 0.5f * inL + 0.5f * inR;
    const float side = 0.5f * inL - 0.5f * inR;
    stereoLowMid_ += crossover * (mid - stereoLowMid_);
    stereoLowSide_ += crossover * (side - stereoLowSide_);
    const float highMid = mid - stereoLowMid_;
    const float highSide = side - stereoLowSide_;

    // A stable first-order allpass produces a different HF phase even from
    // mono. At full spread the mono HF outputs become original/allpass pairs,
    // both at unity magnitude, rather than simply adding gain to both sides.
    const float decorated = stereoDecorrelatorCoefficient_ * highMid + stereoDecorrelatorState_;
    stereoDecorrelatorState_ = highMid - stereoDecorrelatorCoefficient_ * decorated;
    if (absf(stereoDecorrelatorState_) < 1.0e-20f) stereoDecorrelatorState_ = 0.0f;
    if (absf(stereoLowMid_) < 1.0e-20f) stereoLowMid_ = 0.0f;
    if (absf(stereoLowSide_) < 1.0e-20f) stereoLowSide_ = 0.0f;

    // Keep tracking crossover/decorrelator history while bypassed so enabling
    // spreading on a sustained signal does not start from stale filter state.
    if (spread == 0.0f) {
        outL = inL;
        outR = inR;
        return;
    }

    const float decorrelation = 0.5f * spread * (highMid - decorated);
    const float wideMid = highMid - decorrelation;
    const float wideSide = (1.0f + spread) * highSide + decorrelation;

    // Equal-power HF balance makes the signed direction independent of
    // inter-channel phase. Small-angle polynomials avoid audio-rate trig calls;
    // their error on [-pi/4, pi/4] is below four parts per million.
    const float angle = motion * spread * 0.7853981633974483f;
    const float a2 = angle * angle;
    const float sine = angle * (1.0f + a2 * (-1.0f / 6.0f +
        a2 * (1.0f / 120.0f - a2 / 5040.0f)));
    const float cosine = 1.0f + a2 * (-0.5f + a2 *
        (1.0f / 24.0f - a2 / 720.0f));
    const float lowSide = (1.0f - spread) * stereoLowSide_;
    outL = stereoLowMid_ + lowSide + (cosine - sine) * (wideMid + wideSide);
    outR = stereoLowMid_ - lowSide + (cosine + sine) * (wideMid - wideSide);
}
