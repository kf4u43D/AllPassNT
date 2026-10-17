#pragma once

#include "smearnt_core/Modulation.h"
#include <cmath>
#include <cstdio>
#include <limits>

namespace modulation_tests_detail {

inline bool phaseNear(float actual, float expected, float tolerance = 0.00002f) {
    const float delta = std::fabs(actual - expected);
    return delta < tolerance || std::fabs(delta - 1.0f) < tolerance;
}

inline void samples(smearnt::ModulationTransport& transport, int count) {
    for (int sample = 0; sample < count; ++sample) transport.processSample();
}

inline void cvPulseAfter(smearnt::ModulationTransport& transport, int interval) {
    samples(transport, interval - 1);
    transport.processSample(5.0f);
}

inline void midiPulseAfter(smearnt::ModulationTransport& transport, int interval) {
    samples(transport, interval);
    transport.midiRealtime(0xf8);
}

} // namespace modulation_tests_detail

inline int runModulationTests() {
    using smearnt::ModClockSource;
    using smearnt::ModulationTransport;
    using namespace modulation_tests_detail;
    int failures = 0;
    const auto check = [&failures](bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL [ModulationTransport] %s\n", message);
            ++failures;
        }
    };

    check(ModulationTransport::rateBeats(0) == 0.0625f, "1/64 duration");
    check(ModulationTransport::rateBeats(11) == 1.0f, "quarter-note duration");
    check(ModulationTransport::rateBeats(12) == 1.5f, "dotted quarter duration");
    check(std::fabs(ModulationTransport::rateBeats(9) - 2.0f / 3.0f) < 1.0e-6f,
          "triplet quarter duration");
    check(ModulationTransport::rateBeats(19, 3) == 48.0f, "16 bars in 3/4");
    check(ModulationTransport::rateBeats(99, 99) == 192.0f, "rate and meter bounds");

    ModulationTransport internal;
    internal.prepare(8000.0f);
    internal.setTempoBpm(120.0f);
    samples(internal, 8000);
    check(phaseNear(internal.phaseForRate(15), 0.5f), "120 BPM elapsed quarter notes");
    check(phaseNear(internal.phaseForRate(19), 0.03125f), "16-bar phase");
    check(phaseNear(internal.phaseForRate(15, 0.25f), 0.75f), "positive phase offset");
    check(phaseNear(internal.phaseForRate(15, -0.75f), 0.75f), "negative phase offset");
    internal.setTempoBpm(std::numeric_limits<float>::quiet_NaN());
    check(internal.tempoBpm() == 120.0f, "nonfinite tempo ignored");
    internal.setTempoBpm(999.0f);
    check(internal.tempoBpm() == 300.0f, "maximum internal tempo");
    internal.setTempoBpm(-1.0f);
    check(internal.tempoBpm() == 20.0f, "minimum internal tempo");

    // Several epoch wraps at high tempo expose fractional accumulation drift
    // and wraps that incorrectly shorten dotted, triplet or odd-meter bars.
    internal.prepare(8000.0f);
    internal.setTempoBpm(300.0f);
    internal.setBeatsPerBar(3);
    samples(internal, 614400); // 384 quarter notes, 8 complete 16-bar cycles.
    check(phaseNear(internal.phaseForRate(19), 0.0f), "long odd-meter bar cycles");
    check(phaseNear(internal.phaseForRate(4), 0.0f, 0.0002f), "dotted phase across epoch wrap");
    check(phaseNear(internal.phaseForRate(9), 0.0f, 0.0002f), "triplet phase across epoch wrap");

    ModulationTransport invalidRate;
    invalidRate.prepare(std::numeric_limits<float>::infinity());
    samples(invalidRate, 48000);
    check(phaseNear(invalidRate.phaseForRate(15), 0.5f), "invalid sample rate fallback");
    check(invalidRate.phaseForRate(15, std::numeric_limits<float>::infinity()) == 0.0f,
          "nonfinite phase offset guarded");

    for (int shape = 0; shape < ModulationTransport::kShapeCount; ++shape) {
        for (int step = -1000; step <= 2000; ++step) {
            const float phase = static_cast<float>(step) * 0.001f;
            const float value = ModulationTransport::waveform(shape, phase);
            check(std::isfinite(value) && value >= -1.0f && value <= 1.0f,
                  "waveform finite bipolar range");
            if (shape == 0) {
                check(std::fabs(value - std::sin(phase * 6.2831853071795864769f)) < 0.000006f,
                      "sine approximation");
            }
        }
    }
    check(ModulationTransport::waveform(0, 0.25f) > 0.99999f, "sine positive peak");
    check(ModulationTransport::waveform(0, 0.75f) < -0.99999f, "sine negative peak");
    check(ModulationTransport::waveform(1, 0.25f) == 1.0f, "triangle peak");

    ModulationTransport cv;
    cv.prepare(8000.0f);
    cv.setClockSource(ModClockSource::Cv);
    check(!cv.running(), "CV waits for an external clock");
    cv.processSample(5.0f);
    check(cv.phaseForRate(19) == 0.0f, "first CV pulse establishes origin");
    for (int pulse = 0; pulse < 32; ++pulse) cvPulseAfter(cv, 4000);
    check(phaseNear(cv.phaseForRate(19), 0.5f), "CV preserves multi-bar epoch");
    cvPulseAfter(cv, 4000);
    check(phaseNear(cv.phaseForRate(15), 0.25f), "CV pulse advances instead of resetting LFO");
    check(std::fabs(cv.tempoBpm() - 120.0f) < 0.001f, "CV tempo measurement");
    samples(cv, 1000);
    cv.processSample(0.0f, 5.0f);
    check(cv.phaseForRate(19) == 0.0f, "CV reset establishes exact zero");
    for (int sample = 0; sample < 1000; ++sample) cv.processSample(0.0f, 5.0f);
    check(cv.phaseForRate(19) > 0.0f, "held reset does not retrigger");
    cv.processSample();
    cv.processSample(5.0f, 5.0f);
    check(cv.phaseForRate(19) == 0.0f, "simultaneous CV clock and reset establishes zero");

    cv.prepare(8000.0f);
    cv.setCvPpqn(4);
    cv.processSample(5.0f);
    cvPulseAfter(cv, 1000);
    samples(cv, 500);
    check(phaseNear(cv.phaseForRate(15), 0.09375f), "4 PPQN pulse interpolation");
    for (int sample = 0; sample < 16001; ++sample) cv.processSample();
    check(!cv.running(), "CV timeout freezes external transport");
    const float stoppedCv = cv.phaseForRate(19);
    samples(cv, 8000);
    check(cv.phaseForRate(19) == stoppedCv, "CV timeout never falls back to internal tempo");
    cv.processSample(5.0f);
    check(cv.running(), "CV recovers when clock returns");
    check(cv.phaseForRate(19) >= stoppedCv && cv.phaseForRate(19) - stoppedCv <= 0.004f,
          "CV timeout recovery never rewinds the held epoch");

    // A continuously held clock is one edge; a brief bounce is rejected even
    // after the Schmitt trigger has re-armed.
    ModulationTransport edges;
    edges.prepare(8000.0f);
    edges.setClockSource(ModClockSource::Cv);
    edges.processSample(5.0f);
    for (int sample = 0; sample < 1000; ++sample) edges.processSample(5.0f);
    check(edges.phaseForRate(15) == 0.0f, "held clock is one pulse");
    edges.processSample(0.7f);
    edges.processSample(1.1f);
    check(edges.phaseForRate(15) == 0.0f, "Schmitt trigger requires low threshold");
    edges.processSample(0.4f);
    edges.processSample(5.0f);
    check(edges.phaseForRate(15) == 0.0f, "short pulse interval rejected");

    ModulationTransport midi;
    midi.prepare(8000.0f);
    midi.setClockSource(ModClockSource::Midi);
    midi.midiRealtime(0xf8);
    for (int pulse = 0; pulse < 8; ++pulse) midiPulseAfter(midi, 200);
    check(!midi.running() && midi.phaseForRate(19) == 0.0f,
          "MIDI clock while stopped only measures tempo");
    midi.midiRealtime(0xfa);
    midi.midiRealtime(0xf8);
    for (int pulse = 0; pulse < 48; ++pulse) midiPulseAfter(midi, 200);
    check(phaseNear(midi.phaseForRate(15), 0.5f), "MIDI 24 PPQN quarter-note count");
    check(phaseNear(midi.phaseForRate(19), 0.03125f), "MIDI multi-bar phase");
    check(std::fabs(midi.tempoBpm() - 100.0f) < 0.001f, "MIDI tempo measurement");
    midi.midiRealtime(0xfc);
    const float stoppedMidi = midi.phaseForRate(19);
    for (int pulse = 0; pulse < 48; ++pulse) midiPulseAfter(midi, 200);
    check(!midi.running() && midi.phaseForRate(19) == stoppedMidi, "MIDI Stop freezes phase");
    midi.midiRealtime(0xfb);
    check(midi.phaseForRate(19) == stoppedMidi, "MIDI Continue preserves phase");
    midiPulseAfter(midi, 200);
    check(phaseNear(midi.phaseForRate(19), (2.0f + 1.0f / 24.0f) / 64.0f),
          "MIDI Continue advances from held musical epoch");
    samples(midi, 16001);
    check(!midi.running(), "MIDI missing clock freezes interpolation");
    midi.midiRealtime(0xf8);
    check(midi.running(), "MIDI clock recovers without restarting transport");
    midi.midiRealtime(0xfa);
    check(midi.phaseForRate(19) == 0.0f && midi.running(), "MIDI Start resets phase");

    // NT can deliver MIDI on block boundaries. At 300 BPM, a 24- or 64-frame
    // block quantizes a 400-sample tick into several shorter/longer intervals.
    // Every real tick must still advance the same 16-bar musical epoch.
    const int midiHostBlocks[] = { 24, 64 };
    for (int hostBlock : midiHostBlocks) {
        ModulationTransport quantized;
        quantized.prepare(48000.0f);
        quantized.setClockSource(ModClockSource::Midi);
        quantized.midiRealtime(0xfa);
        quantized.midiRealtime(0xf8);
        int elapsed = 0;
        bool countedEveryPulse = true;
        for (int pulse = 1; pulse <= 1536; ++pulse) {
            const int delivery = ((pulse * 400 + hostBlock - 1) / hostBlock) * hostBlock;
            midiPulseAfter(quantized, delivery - elapsed);
            elapsed = delivery;
            const float expected = static_cast<float>(pulse % 1536) / 1536.0f;
            countedEveryPulse = countedEveryPulse && phaseNear(quantized.phaseForRate(19), expected);
        }
        check(countedEveryPulse, "block-quantized 300 BPM MIDI never drops clock pulses");
    }

    ModulationTransport duplicateClock;
    duplicateClock.prepare(48000.0f);
    duplicateClock.setClockSource(ModClockSource::Midi);
    duplicateClock.midiRealtime(0xfa);
    duplicateClock.midiRealtime(0xf8);
    for (int pulse = 0; pulse < 384; ++pulse) {
        midiPulseAfter(duplicateClock, 64); // stray early duplicate
        midiPulseAfter(duplicateClock, 336); // genuine tick, 400 samples total
    }
    check(phaseNear(duplicateClock.phaseForRate(19), 0.25f),
          "MIDI interval tolerance rejects early duplicate glitches");

    ModulationTransport resume;
    resume.prepare(48000.0f);
    resume.setClockSource(ModClockSource::Midi);
    resume.midiRealtime(0xfa);
    resume.midiRealtime(0xf8);
    for (int pulse = 0; pulse < 48; ++pulse) midiPulseAfter(resume, 400);
    samples(resume, 123); // stop between ticks, not exactly on a pulse
    resume.midiRealtime(0xfc);
    const float heldBetweenTicks = resume.phaseForRate(19);
    samples(resume, 48000);
    resume.midiRealtime(0xfb);
    samples(resume, 200);
    check(resume.phaseForRate(19) == heldBetweenTicks,
          "MIDI Continue holds the frozen phase until the next tick");
    resume.midiRealtime(0xf8);
    check(resume.phaseForRate(19) >= heldBetweenTicks &&
          resume.phaseForRate(19) - heldBetweenTicks <= 1.0f / 1536.0f + 0.00002f,
          "MIDI Continue never rewinds a position held between ticks");
    samples(resume, 96001);
    const float heldAfterTimeout = resume.phaseForRate(19);
    resume.midiRealtime(0xf8);
    check(resume.phaseForRate(19) >= heldAfterTimeout &&
          resume.phaseForRate(19) - heldAfterTimeout <= 1.0f / 1536.0f + 0.00002f,
          "MIDI timeout recovery never rewinds several beats");

    // Event timing, not the host's block boundaries, determines the phase.
    ModulationTransport single;
    ModulationTransport grouped;
    single.prepare(44100.0f);
    grouped.prepare(44100.0f);
    single.setTempoBpm(137.0f);
    grouped.setTempoBpm(137.0f);
    samples(single, 123457);
    int remaining = 123457;
    const int blockSizes[] = { 1, 7, 32, 63, 128, 511 };
    int block = 0;
    while (remaining > 0) {
        const int requested = blockSizes[block++ % 6];
        const int count = requested < remaining ? requested : remaining;
        samples(grouped, count);
        remaining -= count;
    }
    for (int rate = 0; rate < ModulationTransport::kRateCount; ++rate)
        check(single.phaseForRate(rate) == grouped.phaseForRate(rate), "block size invariance");
    return failures;
}
