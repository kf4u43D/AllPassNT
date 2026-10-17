#pragma once

#include <cstdint>
#include <cmath>

class AllpassSection {
public:
    AllpassSection() = default;

    void init(float sampleRate);
    void setPole(float freqHz, float radius);
    void setPoleTarget(float freqHz, float radius, int numSamples);
    void advancePoleInterpolation(bool finalSample);
    float process(float x);

    static float fastCos(float radians);
    float getA1() const { return a1_; }
    float getA2() const { return a2_; }

private:
    void computePole(float freqHz, float radius, float& a1, float& a2) const;

    float sampleRate_ = 48000.0f;
    float a1_ = 0.0f;
    float a2_ = 0.0f;
    float a1Step_ = 0.0f;
    float a2Step_ = 0.0f;
    float targetA1_ = 0.0f;
    float targetA2_ = 0.0f;
    // Direct Form I state
    float x1_ = 0.0f;
    float x2_ = 0.0f;
    float y1_ = 0.0f;
    float y2_ = 0.0f;
};
