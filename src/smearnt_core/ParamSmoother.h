#pragma once

#include <cstdint>
#include <cmath>

class ParamSmoother {
public:
    ParamSmoother() = default;

    void init(float sampleRate, float timeMs = 5.0f) {
        sampleRate_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0f && sampleRate <= 384000.0f)
            ? sampleRate
            : 48000.0f;
        setTimeMs(timeMs);
        current_ = 0.0f;
        target_ = 0.0f;
    }

    void setTimeMs(float timeMs) {
        const float t = (std::isfinite(timeMs) && timeMs > 0.01f) ? timeMs : 0.01f;
        float tau = t * 0.001f * sampleRate_;
        coeff_ = (tau > 1.0f) ? (1.0f - (1.0f / tau)) : 0.0f;
        if (coeff_ < 0.0f) coeff_ = 0.0f;
        if (coeff_ > 0.9999f) coeff_ = 0.9999f;
    }

    void setTarget(float v) {
        if (std::isfinite(v)) target_ = v;
    }

    void setImmediate(float v) {
        if (!std::isfinite(v)) v = 0.0f;
        target_ = v;
        current_ = v;
    }

    float getNextValue() {
        const float delta = target_ - current_;
        const float magnitude = (delta < 0.0f) ? -delta : delta;
        if (magnitude <= kSnapThreshold) {
            current_ = target_;
        } else {
            const float next = current_ + delta * (1.0f - coeff_);
            // Once the increment rounds away, further iterations cannot
            // approach a nonzero target. Finish the remaining tiny change.
            current_ = (next == current_) ? target_ : next;
        }
        return current_;
    }

    float getCurrent() const { return current_; }

private:
    static constexpr float kSnapThreshold = 1.0e-8f;

    float sampleRate_ = 48000.0f;
    float coeff_ = 0.0f;
    float current_ = 0.0f;
    float target_ = 0.0f;
};
