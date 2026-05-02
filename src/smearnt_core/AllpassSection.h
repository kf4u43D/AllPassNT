#pragma once

#include <cstdint>
#include <cmath>

class AllpassSection {
public:
    AllpassSection() = default;

    void init(float sampleRate);
    void setPole(float freqHz, float radius);
    float process(float x);

private:
    float sampleRate_ = 48000.0f;
    float a1_ = 0.0f;
    float a2_ = 0.0f;
    // Direct Form I state
    float x1_ = 0.0f;
    float x2_ = 0.0f;
    float y1_ = 0.0f;
    float y2_ = 0.0f;
};
