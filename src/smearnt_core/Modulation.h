#pragma once

#include <cmath>
#include <cstdint>

namespace smearnt {

enum class ModClockSource { Internal = 0, Cv = 1, Midi = 2 };

// One musical timeline drives every modulator. Clock pulses anchor the timeline
// to a monotonically advancing pulse count; they never restart an LFO cycle.
// This header owns no memory and uses only single-precision arithmetic.
class ModulationTransport {
public:
    static constexpr int kRateCount = 20;
    static constexpr int kShapeCount = 5;

    void prepare(float sampleRate) {
        sampleRate_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0f && sampleRate <= 384000.0f)
            ? sampleRate : 48000.0f;
        updateInternalStep();
        resetClock();
    }

    void setClockSource(ModClockSource source) {
        if (source != ModClockSource::Cv && source != ModClockSource::Midi)
            source = ModClockSource::Internal;
        if (source == source_) return;
        source_ = source;
        resetClock();
    }

    void setTempoBpm(float bpm) {
        if (!std::isfinite(bpm)) return;
        internalBpm_ = clamp(bpm, 20.0f, 300.0f);
        updateInternalStep();
    }

    void setCvPpqn(int pulsesPerQuarter) {
        if (pulsesPerQuarter != 1 && pulsesPerQuarter != 2 &&
            pulsesPerQuarter != 4 && pulsesPerQuarter != 24)
            pulsesPerQuarter = 1;
        if (cvPpqn_ == pulsesPerQuarter) return;
        cvPpqn_ = pulsesPerQuarter;
        if (source_ == ModClockSource::Cv) resetClock();
    }

    void setBeatsPerBar(int beats) {
        beatsPerBar_ = beats < 1 ? 1 : (beats > 12 ? 12 : beats);
        // 48 bars is a common multiple of all straight, dotted and triplet
        // periods and the longest 16-bar period, including odd meters.
        const uint32_t wrap = wrapBeats();
        beatCount_ %= wrap;
        pulsePosition_ %= wrap * static_cast<uint32_t>(clockPpqn());
    }

    // Call once per audio sample, regardless of DSP block size. CV is volts.
    // Clock and reset use a 1 V / 0.5 V Schmitt trigger. Reset works for every
    // source; a held gate only resets once. Edges land exactly on beat zero.
    void processSample(float clockVolts = 0.0f, float resetVolts = 0.0f) {
        if (source_ == ModClockSource::Internal) {
            if (running_) advance(internalStep_);
        } else {
            if (clockSamples_ != UINT32_MAX) ++clockSamples_;
            if (running_ && havePeriod_) {
                if (static_cast<float>(clockSamples_) > timeoutSamples_) {
                    running_ = false;
                    externalTimedOut_ = true;
                } else if (!waitingForMidiPulse_) {
                    advance(externalStep_);
                }
            }
        }

        const bool resetEdge = rising(resetVolts, resetHigh_);
        const bool clockEdge = rising(clockVolts, clockHigh_);
        if (resetEdge) {
            resetPhase();
            havePositionPulse_ = true;
        }
        if (source_ == ModClockSource::Cv && clockEdge) externalPulse(resetEdge);
    }

    // Feed realtime bytes individually; F8 is 24 PPQN. FA resets and starts,
    // FB resumes the held musical position, and FC freezes it. Clock received
    // while stopped can still establish tempo, but cannot advance the phase.
    void midiRealtime(uint8_t message) {
        if (source_ != ModClockSource::Midi) return;
        switch (message) {
        case 0xf8:
            externalPulse();
            break;
        case 0xfa:
            resetPhase();
            havePositionPulse_ = false;
            haveTimingPulse_ = false;
            clockSamples_ = 0;
            running_ = true;
            midiTransportRunning_ = true;
            waitingForMidiPulse_ = true;
            externalTimedOut_ = false;
            break;
        case 0xfb:
            rebasePulsePosition();
            haveTimingPulse_ = false;
            clockSamples_ = 0;
            running_ = true;
            midiTransportRunning_ = true;
            waitingForMidiPulse_ = true;
            externalTimedOut_ = false;
            break;
        case 0xfc:
            running_ = false;
            midiTransportRunning_ = false;
            waitingForMidiPulse_ = false;
            externalTimedOut_ = false;
            break;
        default:
            break;
        }
    }

    float phaseForRate(int rateIndex, float offsetCycles = 0.0f) const {
        const float beats = static_cast<float>(beatCount_) + beatFraction_;
        return wrapPhase(beats / rateBeats(rateIndex, beatsPerBar_) + offsetCycles);
    }

    float tempoBpm() const {
        return source_ == ModClockSource::Internal || !havePeriod_
            ? internalBpm_ : externalBpm_;
    }

    bool running() const { return running_; }

    static const char* rateLabel(int index) {
        static const char* const labels[kRateCount] = {
            "1/64", "1/32T", "1/32", "1/16T", "1/32D", "1/16",
            "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4",
            "1/4D", "1/2", "1/2D", "1 bar", "2 bars", "4 bars",
            "8 bars", "16 bars"
        };
        return labels[validRate(index)];
    }

    // Durations are quarter-note beats. Bar lengths follow the selected meter.
    static float rateBeats(int index, int beatsPerBar = 4) {
        static const float durations[15] = {
            1.0f / 16.0f, 1.0f / 12.0f, 1.0f / 8.0f, 1.0f / 6.0f,
            3.0f / 16.0f, 1.0f / 4.0f, 1.0f / 3.0f, 3.0f / 8.0f,
            1.0f / 2.0f, 2.0f / 3.0f, 3.0f / 4.0f, 1.0f, 1.5f, 2.0f, 3.0f
        };
        index = validRate(index);
        if (index < 15) return durations[index];
        const int meter = beatsPerBar < 1 ? 1 : (beatsPerBar > 12 ? 12 : beatsPerBar);
        return static_cast<float>(meter * (1 << (index - 15)));
    }

    // Output is bipolar, -1..1. Apply each modulator's signed amount and offset
    // after this function, before distributing it to modulation destinations.
    static float waveform(int shape, float phase) {
        phase = wrapPhase(phase);
        switch (shape) {
        case 1: // triangle: zero, positive peak, zero, negative peak
            return phase < 0.25f ? phase * 4.0f
                : (phase < 0.75f ? 2.0f - phase * 4.0f : phase * 4.0f - 4.0f);
        case 2:
            return phase * 2.0f - 1.0f;
        case 3:
            return 1.0f - phase * 2.0f;
        case 4:
            return phase < 0.5f ? 1.0f : -1.0f;
        default: {
            // Fold to +/- pi/2, then evaluate an odd ninth-order polynomial.
            // Error is below 4e-6, with no sinf/other runtime dependency.
            float x = phase < 0.5f ? phase : phase - 1.0f;
            if (x > 0.25f) x = 0.5f - x;
            else if (x < -0.25f) x = -0.5f - x;
            x *= 6.2831853071795864769f;
            const float x2 = x * x;
            return clamp(x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f +
                x2 * (-1.0f / 5040.0f + x2 * (1.0f / 362880.0f))))), -1.0f, 1.0f);
        }
        }
    }

private:
    static float clamp(float value, float lo, float hi) {
        return value < lo ? lo : (value > hi ? hi : value);
    }

    static int validRate(int index) {
        return index < 0 ? 0 : (index >= kRateCount ? kRateCount - 1 : index);
    }

    static float wrapPhase(float phase) {
        if (!std::isfinite(phase) || phase < -1000000.0f || phase > 1000000.0f)
            return 0.0f;
        phase -= static_cast<float>(static_cast<int32_t>(phase));
        return phase < 0.0f ? phase + 1.0f : phase;
    }

    static bool rising(float volts, bool& high) {
        if (!std::isfinite(volts)) volts = 0.0f;
        if (high) {
            if (volts <= 0.5f) high = false;
            return false;
        }
        if (volts >= 1.0f) {
            high = true;
            return true;
        }
        return false;
    }

    int clockPpqn() const { return source_ == ModClockSource::Midi ? 24 : cvPpqn_; }
    uint32_t wrapBeats() const { return static_cast<uint32_t>(48 * beatsPerBar_); }

    void updateInternalStep() { internalStep_ = internalBpm_ / (60.0f * sampleRate_); }

    void resetPhase() {
        beatCount_ = 0;
        beatFraction_ = 0.0f;
        beatCompensation_ = 0.0f;
        pulsePosition_ = 0;
    }

    void resetClock() {
        resetPhase();
        clockSamples_ = 0;
        haveTimingPulse_ = false;
        havePositionPulse_ = false;
        havePeriod_ = false;
        clockHigh_ = false;
        resetHigh_ = false;
        running_ = source_ == ModClockSource::Internal;
        midiTransportRunning_ = false;
        waitingForMidiPulse_ = false;
        externalTimedOut_ = false;
        periodEstimate_ = 0.0f;
        externalStep_ = 0.0f;
        timeoutSamples_ = sampleRate_ * 2.0f;
    }

    void advance(float beatsPerSample) {
        // Compensated summation keeps tiny per-sample steps accurate, while
        // the integer quarter-note epoch preserves very long bar divisions.
        const float increment = beatsPerSample - beatCompensation_;
        const float sum = beatFraction_ + increment;
        beatCompensation_ = (sum - beatFraction_) - increment;
        beatFraction_ = sum;
        if (beatFraction_ >= 1.0f) {
            beatFraction_ -= 1.0f;
            if (++beatCount_ >= wrapBeats()) beatCount_ = 0;
        }
    }

    void rebasePulsePosition() {
        // Resume on the next musical tick after the held interpolated phase.
        // Reusing the last received tick would rewind the timeline after a
        // mid-tick Stop or after several seconds of clock timeout prediction.
        const uint32_t ppqn = static_cast<uint32_t>(clockPpqn());
        uint32_t subPulse = static_cast<uint32_t>(beatFraction_ * static_cast<float>(ppqn));
        if (subPulse >= ppqn) subPulse = ppqn - 1;
        pulsePosition_ = beatCount_ * ppqn + subPulse;
        havePositionPulse_ = true;
    }

    void externalPulse(bool resetAtThisPulse = false) {
        const int ppqn = clockPpqn();
        const float samples = static_cast<float>(clockSamples_);
        const float minSamples = sampleRate_ * (60.0f / 300.0f) / static_cast<float>(ppqn);
        const float maxSamples = sampleRate_ * (60.0f / 20.0f) / static_cast<float>(ppqn);
        if (haveTimingPulse_) {
            // MIDI callbacks can be quantized to audio block boundaries. A
            // 20% interval margin admits 24/64-frame jitter at 300 BPM without
            // mistaking valid early ticks for glitches. CV edges are sampled
            // directly and retain the tighter one-sample tolerance.
            const bool midi = source_ == ModClockSource::Midi;
            const float earliest = midi ? minSamples * 0.8f : minSamples;
            const float latest = midi ? maxSamples * 1.2f : maxSamples;
            if (samples < earliest - 1.0f) return;
            if (samples <= latest + 1.0f) {
                if (midi && havePeriod_)
                    periodEstimate_ += (samples - periodEstimate_) * 0.125f;
                else
                    periodEstimate_ = samples;
                const float period = clamp(periodEstimate_, minSamples, maxSamples);
                externalStep_ = 1.0f / (period * static_cast<float>(ppqn));
                externalBpm_ = 60.0f * sampleRate_ * externalStep_;
                timeoutSamples_ = period * 2.5f;
                if (timeoutSamples_ < sampleRate_ * 2.0f)
                    timeoutSamples_ = sampleRate_ * 2.0f;
                havePeriod_ = true;
            } else {
                havePeriod_ = false;
                periodEstimate_ = 0.0f;
            }
        }
        haveTimingPulse_ = true;
        clockSamples_ = 0;
        if (source_ == ModClockSource::Cv) running_ = true;
        else if (midiTransportRunning_) running_ = true;
        if (!running_) return;

        if (externalTimedOut_ && !resetAtThisPulse) rebasePulsePosition();
        externalTimedOut_ = false;
        waitingForMidiPulse_ = false;

        if (havePositionPulse_ && !resetAtThisPulse) {
            const uint32_t wrap = wrapBeats() * static_cast<uint32_t>(ppqn);
            if (++pulsePosition_ >= wrap) pulsePosition_ = 0;
        } else {
            havePositionPulse_ = true;
        }
        const uint32_t divisor = static_cast<uint32_t>(ppqn);
        beatCount_ = pulsePosition_ / divisor;
        beatFraction_ = static_cast<float>(pulsePosition_ % divisor) / static_cast<float>(ppqn);
        beatCompensation_ = 0.0f;
    }

    float sampleRate_ = 48000.0f;
    float internalBpm_ = 120.0f;
    float internalStep_ = 120.0f / (60.0f * 48000.0f);
    float externalBpm_ = 120.0f;
    float externalStep_ = 0.0f;
    float periodEstimate_ = 0.0f;
    float timeoutSamples_ = 96000.0f;
    float beatFraction_ = 0.0f;
    float beatCompensation_ = 0.0f;
    uint32_t beatCount_ = 0;
    uint32_t pulsePosition_ = 0;
    uint32_t clockSamples_ = 0;
    ModClockSource source_ = ModClockSource::Internal;
    int cvPpqn_ = 1;
    int beatsPerBar_ = 4;
    bool running_ = true;
    bool midiTransportRunning_ = false;
    bool waitingForMidiPulse_ = false;
    bool externalTimedOut_ = false;
    bool haveTimingPulse_ = false;
    bool havePositionPulse_ = false;
    bool havePeriod_ = false;
    bool clockHigh_ = false;
    bool resetHigh_ = false;
};

} // namespace smearnt
