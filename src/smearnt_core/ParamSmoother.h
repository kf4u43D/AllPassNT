#pragma once

#include <cstdint>

class ParamSmoother {
public:
    ParamSmoother() = default;

    void init(float sampleRate, float timeMs = 5.0f) {
        sampleRate_ = (sampleRate > 1.0f) ? sampleRate : 48000.0f;
        setTimeMs(timeMs);
        current_ = 0.0f;
        target_ = 0.0f;
    }

    void setTimeMs(float timeMs) {
        float t = (timeMs > 0.01f) ? timeMs : 0.01f;
        float tau = t * 0.001f * sampleRate_;
        coeff_ = (tau > 1.0f) ? (1.0f - (1.0f / tau)) : 0.0f;
        if (coeff_ < 0.0f) coeff_ = 0.0f;
        if (coeff_ > 0.9999f) coeff_ = 0.9999f;
    }

    void setTarget(float v) { target_ = v; }

    void setImmediate(float v) {
        target_ = v;
        current_ = v;
    }

    float getNextValue() {
        current_ += (target_ - current_) * (1.0f - coeff_);
        return current_;
    }

    float getCurrent() const { return current_; }

private:
    float sampleRate_ = 48000.0f;
    float coeff_ = 0.0f;
    float current_ = 0.0f;
    float target_ = 0.0f;
};
