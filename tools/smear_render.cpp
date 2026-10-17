#include "widesmearnt_core/SmearCore.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

constexpr double kPi = 3.1415926535897932384626433832795;

struct Options {
    std::string output = "build/smear-render.wav";
    std::string source = "impulse";
    int sampleRate = 48000;
    double durationSeconds = 2.0;
    int bitDepth = 24;
    double frequencyPercent = 50.0;
    double resonancePercent = 50.0;
    double stagesPercent = 50.0;
    double distributionPercent = 50.0;
    double feedbackPercent = 20.0;
    double sweepStartHz = 20.0;
    double sweepEndHz = 20000.0;
    double amplitude = 0.8;
    bool failOnClip = false;
};

void printUsage(const char* program) {
    std::cout
        << "Offline stereo renderer for SmearCore\n\n"
        << "Usage:\n  " << program << " [options]\n\n"
        << "Options:\n"
        << "  -o, --output PATH          Output WAV (default: build/smear-render.wav)\n"
        << "      --source TYPE          impulse or sweep (default: impulse)\n"
        << "      --sample-rate HZ       8000..384000 (default: 48000)\n"
        << "      --duration SECONDS     Positive output duration (default: 2)\n"
        << "      --bit-depth BITS       16 or 24 (default: 24)\n"
        << "      --frequency PERCENT    0..100 (default: 50)\n"
        << "      --resonance PERCENT    0..100 (default: 50)\n"
        << "      --stages PERCENT       0..100 (default: 50)\n"
        << "      --distribution PERCENT 0..100 (default: 50)\n"
        << "      --feedback PERCENT     0..100 (default: 20)\n"
        << "      --sweep-start HZ       Sweep start frequency (default: 20)\n"
        << "      --sweep-end HZ         Sweep end frequency (default: 20000)\n"
        << "      --amplitude LEVEL      Input peak, 0..1 (default: 0.8)\n"
        << "      --fail-on-clip         Return an error on clipping or non-finite output\n"
        << "  -h, --help                 Show this help\n\n"
        << "The source is sent identically to both inputs. Output is 100% wet.\n";
}

std::string lowerAscii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

double parseDouble(const std::string& text, const std::string& option) {
    std::size_t consumed = 0;
    double value = 0.0;
    try {
        value = std::stod(text, &consumed);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid value for " + option + ": " + text);
    }
    if (consumed != text.size() || !std::isfinite(value)) {
        throw std::runtime_error("invalid value for " + option + ": " + text);
    }
    return value;
}

int parseInt(const std::string& text, const std::string& option) {
    std::size_t consumed = 0;
    long value = 0;
    try {
        value = std::stol(text, &consumed, 10);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid value for " + option + ": " + text);
    }
    if (consumed != text.size() || value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) {
        throw std::runtime_error("invalid value for " + option + ": " + text);
    }
    return static_cast<int>(value);
}

std::string takeValue(int& index, int argc, char** argv,
                      const std::string& option, const std::string& inlineValue,
                      bool hasInlineValue) {
    if (hasInlineValue) {
        if (inlineValue.empty()) {
            throw std::runtime_error("missing value for " + option);
        }
        return inlineValue;
    }
    if (index + 1 >= argc) {
        throw std::runtime_error("missing value for " + option);
    }
    ++index;
    return argv[index];
}

Options parseOptions(int argc, char** argv, bool& showHelp) {
    Options options;
    showHelp = false;

    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        std::string inlineValue;
        const std::size_t equals = argument.find('=');
        if (equals != std::string::npos) {
            inlineValue = argument.substr(equals + 1);
            argument.resize(equals);
        }
        const std::string option = lowerAscii(argument);

        if (option == "-h" || option == "--help") {
            if (equals != std::string::npos) {
                throw std::runtime_error("option does not accept a value: " + argument);
            }
            showHelp = true;
            continue;
        }

        if (option == "--fail-on-clip") {
            if (equals != std::string::npos) {
                throw std::runtime_error("option does not accept a value: " + argument);
            }
            options.failOnClip = true;
            continue;
        }

        const bool knownOption = option == "-o" || option == "--output" ||
            option == "--source" || option == "--sample-rate" ||
            option == "--duration" || option == "--bit-depth" ||
            option == "--frequency" || option == "--resonance" ||
            option == "--stages" || option == "--distribution" ||
            option == "--feedback" || option == "--sweep-start" ||
            option == "--sweep-end" || option == "--amplitude";
        if (!knownOption) {
            throw std::runtime_error("unknown option: " + argument);
        }

        const std::string value = takeValue(i, argc, argv, argument, inlineValue,
                                          equals != std::string::npos);
        if (option == "-o" || option == "--output") {
            options.output = value;
        } else if (option == "--source") {
            options.source = lowerAscii(value);
        } else if (option == "--sample-rate") {
            options.sampleRate = parseInt(value, argument);
        } else if (option == "--duration") {
            options.durationSeconds = parseDouble(value, argument);
        } else if (option == "--bit-depth") {
            options.bitDepth = parseInt(value, argument);
        } else if (option == "--frequency") {
            options.frequencyPercent = parseDouble(value, argument);
        } else if (option == "--resonance") {
            options.resonancePercent = parseDouble(value, argument);
        } else if (option == "--stages") {
            options.stagesPercent = parseDouble(value, argument);
        } else if (option == "--distribution") {
            options.distributionPercent = parseDouble(value, argument);
        } else if (option == "--feedback") {
            options.feedbackPercent = parseDouble(value, argument);
        } else if (option == "--sweep-start") {
            options.sweepStartHz = parseDouble(value, argument);
        } else if (option == "--sweep-end") {
            options.sweepEndHz = parseDouble(value, argument);
        } else if (option == "--amplitude") {
            options.amplitude = parseDouble(value, argument);
        }
    }
    return options;
}

void validatePercent(double value, const char* name) {
    if (value < 0.0 || value > 100.0) {
        throw std::runtime_error(std::string(name) + " must be between 0 and 100");
    }
}

std::uint64_t validateAndGetFrameCount(const Options& options) {
    if (options.output.empty()) {
        throw std::runtime_error("output path must not be empty");
    }
    if (options.source != "impulse" && options.source != "sweep") {
        throw std::runtime_error("source must be impulse or sweep");
    }
    if (options.sampleRate < 8000 || options.sampleRate > 384000) {
        throw std::runtime_error("sample rate must be between 8000 and 384000 Hz");
    }
    if (!(options.durationSeconds > 0.0)) {
        throw std::runtime_error("duration must be positive");
    }
    if (options.bitDepth != 16 && options.bitDepth != 24) {
        throw std::runtime_error("bit depth must be 16 or 24");
    }
    validatePercent(options.frequencyPercent, "frequency");
    validatePercent(options.resonancePercent, "resonance");
    validatePercent(options.stagesPercent, "stages");
    validatePercent(options.distributionPercent, "distribution");
    validatePercent(options.feedbackPercent, "feedback");
    if (options.amplitude < 0.0 || options.amplitude > 1.0) {
        throw std::runtime_error("amplitude must be between 0 and 1");
    }
    if (options.source == "sweep") {
        const double nyquist = 0.5 * static_cast<double>(options.sampleRate);
        if (!(options.sweepStartHz > 0.0) || !(options.sweepEndHz > options.sweepStartHz)) {
            throw std::runtime_error("sweep frequencies must satisfy 0 < start < end");
        }
        if (options.sweepEndHz >= nyquist) {
            throw std::runtime_error("sweep end must be below the Nyquist frequency");
        }
    }

    const std::uint64_t bytesPerFrame = 2U * static_cast<std::uint64_t>(options.bitDepth / 8);
    const std::uint64_t maxDataBytes = std::numeric_limits<std::uint32_t>::max() - 36U;
    const long double exactFrames = static_cast<long double>(options.durationSeconds) *
                                    static_cast<long double>(options.sampleRate);
    const long double roundedFrames = std::floor(exactFrames + 0.5L);
    // Validate against the much smaller RIFF limit before converting to an
    // integer; very large durations can otherwise overflow that conversion.
    if (!std::isfinite(roundedFrames) ||
        roundedFrames > static_cast<long double>(maxDataBytes / bytesPerFrame)) {
        throw std::runtime_error("render is too large for a standard RIFF/WAV file");
    }
    if (roundedFrames < 1.0L) {
        throw std::runtime_error("duration is shorter than one sample");
    }
    return static_cast<std::uint64_t>(roundedFrames);
}

void writeU16(std::ostream& stream, std::uint16_t value) {
    stream.put(static_cast<char>(value & 0xffU));
    stream.put(static_cast<char>((value >> 8U) & 0xffU));
}

void writeU32(std::ostream& stream, std::uint32_t value) {
    stream.put(static_cast<char>(value & 0xffU));
    stream.put(static_cast<char>((value >> 8U) & 0xffU));
    stream.put(static_cast<char>((value >> 16U) & 0xffU));
    stream.put(static_cast<char>((value >> 24U) & 0xffU));
}

void writeWavHeader(std::ostream& stream, const Options& options, std::uint32_t dataBytes) {
    constexpr std::uint16_t channels = 2;
    const std::uint16_t bytesPerSample = static_cast<std::uint16_t>(options.bitDepth / 8);
    const std::uint16_t blockAlign = static_cast<std::uint16_t>(channels * bytesPerSample);
    const std::uint32_t byteRate = static_cast<std::uint32_t>(options.sampleRate) * blockAlign;

    stream.write("RIFF", 4);
    writeU32(stream, 36U + dataBytes);
    stream.write("WAVE", 4);
    stream.write("fmt ", 4);
    writeU32(stream, 16U);
    writeU16(stream, 1U);
    writeU16(stream, channels);
    writeU32(stream, static_cast<std::uint32_t>(options.sampleRate));
    writeU32(stream, byteRate);
    writeU16(stream, blockAlign);
    writeU16(stream, static_cast<std::uint16_t>(options.bitDepth));
    stream.write("data", 4);
    writeU32(stream, dataBytes);
}

float sanitizeAndClip(float sample, std::uint64_t& clippedSamples) {
    if (!std::isfinite(sample)) {
        ++clippedSamples;
        return 0.0f;
    }
    if (sample > 1.0f) {
        ++clippedSamples;
        return 1.0f;
    }
    if (sample < -1.0f) {
        ++clippedSamples;
        return -1.0f;
    }
    return sample;
}

void writePcmSample(std::ostream& stream, float input, int bitDepth,
                    std::uint64_t& clippedSamples) {
    const float sample = sanitizeAndClip(input, clippedSamples);
    if (bitDepth == 16) {
        const double scale = (sample < 0.0f) ? 32768.0 : 32767.0;
        const std::int32_t value = static_cast<std::int32_t>(std::lround(sample * scale));
        writeU16(stream, static_cast<std::uint16_t>(static_cast<std::int16_t>(value)));
        return;
    }

    const double scale = (sample < 0.0f) ? 8388608.0 : 8388607.0;
    const std::int32_t value = static_cast<std::int32_t>(std::llround(sample * scale));
    const std::uint32_t bits = static_cast<std::uint32_t>(value);
    stream.put(static_cast<char>(bits & 0xffU));
    stream.put(static_cast<char>((bits >> 8U) & 0xffU));
    stream.put(static_cast<char>((bits >> 16U) & 0xffU));
}

double sweepInput(std::uint64_t frame, std::uint64_t totalFrames,
                  const Options& options, double& phase) {
    const double position = totalFrames > 1U
        ? static_cast<double>(frame) / static_cast<double>(totalFrames - 1U)
        : 0.0;
    // Interpolate logarithms instead of forming a ratio that can overflow
    // even when both positive endpoint frequencies are valid and finite.
    const double logStart = std::log(options.sweepStartHz);
    const double logEnd = std::log(options.sweepEndHz);
    const double frequency = std::exp(logStart + (logEnd - logStart) * position);
    const double value = std::sin(phase);
    phase += 2.0 * kPi * frequency / static_cast<double>(options.sampleRate);
    if (phase >= 2.0 * kPi) {
        phase = std::fmod(phase, 2.0 * kPi);
    }

    const std::uint64_t fadeFrames = std::min<std::uint64_t>(
        totalFrames / 2U, static_cast<std::uint64_t>(options.sampleRate / 200));
    double envelope = 1.0;
    if (fadeFrames > 0U && frame < fadeFrames) {
        envelope = 0.5 - 0.5 * std::cos(kPi * static_cast<double>(frame) /
                                        static_cast<double>(fadeFrames));
    } else if (fadeFrames > 0U && frame >= totalFrames - fadeFrames) {
        const std::uint64_t remaining = totalFrames - 1U - frame;
        envelope = 0.5 - 0.5 * std::cos(kPi * static_cast<double>(remaining) /
                                        static_cast<double>(fadeFrames));
    }
    return value * envelope;
}

int render(const Options& options) {
    const std::uint64_t frames = validateAndGetFrameCount(options);
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(
        frames * 2U * static_cast<std::uint64_t>(options.bitDepth / 8));

    std::ofstream output(options.output.c_str(), std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot open output file: " + options.output);
    }
    writeWavHeader(output, options, dataBytes);

    SmearCore core;
    core.init(static_cast<float>(options.sampleRate));
    core.setFrequency(static_cast<float>(options.frequencyPercent * 0.01));
    core.setResonance(static_cast<float>(options.resonancePercent * 0.01));
    core.setStages(static_cast<float>(options.stagesPercent * 0.01));
    core.setDistributionMorph(static_cast<float>(options.distributionPercent * 0.01));
    core.setFeedback(static_cast<float>(options.feedbackPercent * 0.01));

    float discardLeft = 0.0f;
    float discardRight = 0.0f;
    const int warmupFrames = options.sampleRate / 2;
    for (int i = 0; i < warmupFrames; ++i) {
        core.processSample(0.0f, 0.0f, discardLeft, discardRight);
    }

    std::uint64_t clippedSamples = 0U;
    double peak = 0.0;
    double phase = 0.0;
    for (std::uint64_t frame = 0; frame < frames; ++frame) {
        double source = 0.0;
        if (options.source == "impulse") {
            source = (frame == 0U) ? 1.0 : 0.0;
        } else {
            source = sweepInput(frame, frames, options, phase);
        }
        const float input = static_cast<float>(source * options.amplitude);
        float left = 0.0f;
        float right = 0.0f;
        core.processSample(input, input, left, right);
        peak = std::max(peak, std::fabs(static_cast<double>(left)));
        peak = std::max(peak, std::fabs(static_cast<double>(right)));
        writePcmSample(output, left, options.bitDepth, clippedSamples);
        writePcmSample(output, right, options.bitDepth, clippedSamples);
    }

    output.flush();
    if (!output) {
        throw std::runtime_error("failed while writing output file: " + options.output);
    }

    std::cout << "Rendered " << frames << " frames to " << options.output << "\n"
              << "Format: stereo PCM, " << options.sampleRate << " Hz, "
              << options.bitDepth << " bit\n"
              << "Source: " << options.source << ", peak: " << std::fixed
              << std::setprecision(6) << peak << ", clipped samples: "
              << clippedSamples << "\n";
    if (options.failOnClip && clippedSamples != 0U) {
        throw std::runtime_error("render produced clipped or non-finite samples");
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        bool showHelp = false;
        const Options options = parseOptions(argc, argv, showHelp);
        if (showHelp) {
            printUsage(argv[0]);
            return 0;
        }
        return render(options);
    } catch (const std::exception& error) {
        std::cerr << "smear_render: " << error.what() << "\n"
                  << "Use --help for usage.\n";
        return 1;
    }
}
