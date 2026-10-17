#include <distingnt/api.h>

#include "SmearNTParameters.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Inspect the real adapter's factory and exercise native preset parameter arrays.
// This host-side check does not execute the disting NT firmware preset loader.
namespace {
constexpr int kPluginParameters = kNumParameters;
constexpr int kLegacyPluginParameters = 11;
constexpr int kCommonParameters = 1;
constexpr int kFramesPerBlock = 24;
constexpr int kSampleRate = 48000;

std::string quote(const char* text) {
    std::string result = "\"";
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        switch (*p) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (*p < 0x20) {
                char escaped[7];
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", unsigned(*p));
                result += escaped;
            } else {
                result += static_cast<char>(*p);
            }
        }
    }
    result += '"';
    return result;
}

int fail(const char* message, int code) {
    std::printf("{\"pass\":false,\"error\":%s}\n", quote(message).c_str());
    return code;
}

bool parseInt16(const char* text, int16_t& parsed) {
    const char* digits = text;
    if (*digits == '-') ++digits;
    if (!*digits) return false;
    for (const char* p = digits; *p; ++p)
        if (*p < '0' || *p > '9') return false;
    errno = 0;
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (errno == ERANGE || !end || *end || value < -32768 || value > 32767)
        return false;
    parsed = static_cast<int16_t>(value);
    return true;
}

struct Stats {
    double peak = 0.0;
    double energy = 0.0;
    uint64_t samples = 0;
    uint64_t nonzero = 0;
    bool finite = true;
    bool noClip = true;

    void add(float value) {
        ++samples;
        if (!std::isfinite(value)) { finite = false; return; }
        const double magnitude = std::fabs(static_cast<double>(value));
        peak = std::max(peak, magnitude);
        energy += static_cast<double>(value) * value;
        if (magnitude > 1.0) noClip = false;
        if (value != 0.0f) ++nonzero;
    }

    double rms() const { return samples ? std::sqrt(energy / samples) : 0.0; }
};

void printStats(const char* name, const std::array<Stats, 2>& channels) {
    std::printf("\"%s\":{\"peak_left\":%.12g,\"peak_right\":%.12g,"
                "\"rms_left\":%.12g,\"rms_right\":%.12g,"
                "\"nonzero_left\":%llu,\"nonzero_right\":%llu,"
                "\"samples_per_channel\":%llu}",
                name, channels[0].peak, channels[1].peak,
                channels[0].rms(), channels[1].rms(),
                static_cast<unsigned long long>(channels[0].nonzero),
                static_cast<unsigned long long>(channels[1].nonzero),
                static_cast<unsigned long long>(channels[0].samples));
}
} // namespace

int main(int argc, char** argv) {
    if (NT_globals.sampleRate != kSampleRate || NT_globals.maxFramesPerStep < kFramesPerBlock)
        return fail("Unexpected host shim sample rate or frame limit", 2);
    const uintptr_t apiVersion = pluginEntry(kNT_selector_version, 0);
    if (pluginEntry(kNT_selector_numFactories, 0) != 1)
        return fail("Expected exactly one factory", 2);
    const auto* factory = reinterpret_cast<const _NT_factory*>(
        pluginEntry(kNT_selector_factoryInfo, 0));
    if (!factory || !factory->calculateRequirements || !factory->construct ||
        !factory->parameterChanged || !factory->step)
        return fail("Incomplete factory callbacks", 2);
    _NT_algorithmRequirements requirements{};
    factory->calculateRequirements(requirements, nullptr);
    if (requirements.numParameters != kPluginParameters || !requirements.sram ||
        requirements.dram || requirements.dtc || requirements.itc || factory->numSpecifications)
        return fail("Unexpected factory requirements", 2);

    const std::size_t storageWords =
        (requirements.sram + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t);
    std::vector<std::max_align_t> storage(storageWords);
    _NT_algorithmMemoryPtrs memory{};
    memory.sram = reinterpret_cast<uint8_t*>(storage.data());
    _NT_algorithm* algorithm = factory->construct(memory, requirements, nullptr);
    if (!algorithm || !algorithm->parameters)
        return fail("Construction failed", 2);

    if (argc == 1) {
        char guid[5]{};
        for (unsigned i = 0; i < 4; ++i)
            guid[i] = static_cast<char>((factory->guid >> (8 * i)) & 0xff);
        std::printf("{\"guid\":%s,\"guid_numeric\":%u,\"api_version\":%llu,"
                    "\"factory_name\":%s,\"num_specifications\":%u,"
                    "\"num_parameters\":%u,\"sram_bytes_host\":%u,"
                    "\"common_parameter_offset\":%d,\"parameters\":[",
                    quote(guid).c_str(), factory->guid,
                    static_cast<unsigned long long>(apiVersion),
                    quote(factory->name).c_str(), factory->numSpecifications,
                    requirements.numParameters, requirements.sram, kCommonParameters);
        for (int index = 0; index < kPluginParameters; ++index) {
            const _NT_parameter& p = algorithm->parameters[index];
            std::printf("%s{\"plugin_index\":%d,\"native_index\":%d,"
                        "\"name\":%s,\"min\":%d,\"max\":%d,"
                        "\"default\":%d,\"unit\":%u,\"scaling\":%u}",
                        index ? "," : "", index, index + kCommonParameters,
                        quote(p.name).c_str(), p.min, p.max, p.def, p.unit, p.scaling);
        }
        std::printf("]}\n");
        return 0;
    }
    const int providedParameters = argc - 1;
    if (providedParameters != kLegacyPluginParameters + kCommonParameters &&
        providedParameters != kPluginParameters + kCommonParameters)
        return fail("Expected Bypass plus either 11 legacy or all current plugin parameters", 3);

    std::array<int16_t, kPluginParameters + kCommonParameters> values{};
    // Existing native presets store only the original prefix. Model the host's
    // loading behavior by retaining factory defaults for every appended field.
    for (int index = 0; index < kPluginParameters; ++index)
        values[index + kCommonParameters] = algorithm->parameters[index].def;
    for (int index = 0; index < providedParameters; ++index) {
        int16_t parsed = 0;
        if (!parseInt16(argv[index + 1], parsed))
            return fail("Parameter is not a strict int16 integer", 3);
        if (index == 0) {
            if (parsed != 0) return fail("Bypass must be 0", 3);
        } else {
            const _NT_parameter& p = algorithm->parameters[index - 1];
            if (parsed < p.min || parsed > p.max)
                return fail("Parameter outside effective factory range", 3);
        }
        values[index] = parsed;
    }
    // Strict bank routing: inputs 1/2, physical outputs 1/2 (busses 13/14), Replace.
    if (values[1] != 1 || values[2] != 2 || values[3] != 13 ||
        values[4] != 1 || values[5] != 14 || values[6] != 1)
        return fail("Expected native routing 1/2 to 13/14 with both output modes Replace", 3);
    algorithm->vIncludingCommon = values.data();
    algorithm->v = values.data() + kCommonParameters;
    for (int parameter = 0; parameter < kPluginParameters; ++parameter)
        factory->parameterChanged(algorithm, parameter);

    std::array<Stats, 2> total{}, impulse{}, oscillating{};
    bool inputsPreserved = true;
    bool otherBussesPreserved = true;
    bool replaceVerified = true;
    const auto render = [&](int sampleCount, int phase, std::array<Stats, 2>* phaseStats) {
        std::vector<float> busses(static_cast<std::size_t>(kNT_lastBus) * kFramesPerBlock);
        for (int block = 0; block < sampleCount; block += kFramesPerBlock) {
            for (int bus = 1; bus <= kNT_lastBus; ++bus)
                for (int frame = 0; frame < kFramesPerBlock; ++frame)
                    busses[static_cast<std::size_t>(bus - 1) * kFramesPerBlock + frame] =
                        0.001f * static_cast<float>(bus);
            for (int frame = 0; frame < kFramesPerBlock; ++frame) {
                const int sample = block + frame;
                float left = 0.0f;
                float right = 0.0f;
                if (phase == 1) {
                    left = sample == 0 ? 0.2f : 0.0f;
                    right = sample == 6 ? -0.2f : 0.0f;
                } else if (phase == 2) {
                    left = 0.13f * std::sin(0.025f * sample) +
                           0.07f * std::sin(0.061f * sample);
                    right = 0.11f * std::sin(0.039f * sample) +
                            0.09f * std::sin(0.097f * sample);
                }
                busses[frame] = left;
                busses[kFramesPerBlock + frame] = right;
                busses[12 * kFramesPerBlock + frame] = 0.37f;
                busses[13 * kFramesPerBlock + frame] = -0.31f;
            }
            const std::vector<float> before = busses;
            factory->step(algorithm, busses.data(), kFramesPerBlock / 4);
            for (int bus = 1; bus <= kNT_lastBus; ++bus) {
                if (bus == 13 || bus == 14) continue;
                for (int frame = 0; frame < kFramesPerBlock; ++frame) {
                    const std::size_t index = static_cast<std::size_t>(bus - 1) *
                                              kFramesPerBlock + frame;
                    if (busses[index] != before[index]) {
                        otherBussesPreserved = false;
                        if (bus == 1 || bus == 2) inputsPreserved = false;
                    }
                }
            }
            for (int channel = 0; channel < 2; ++channel) {
                for (int frame = 0; frame < kFramesPerBlock; ++frame) {
                    const float output = busses[(12 + channel) * kFramesPerBlock + frame];
                    total[channel].add(output);
                    if (phaseStats) (*phaseStats)[channel].add(output);
                    if (phase == 0 && output != 0.0f) replaceVerified = false;
                }
            }
        }
    };
    render(12000, 0, nullptr);       // 250 ms silence settles every smoother.
    render(kSampleRate, 1, &impulse);
    render(kSampleRate, 2, &oscillating);
    render(24000, 3, nullptr);      // 500 ms tail, included in finite/clip checks.
    const bool finite = total[0].finite && total[1].finite;
    const bool noClip = total[0].noClip && total[1].noClip;
    const bool nonzero = impulse[0].nonzero && impulse[1].nonzero &&
                         oscillating[0].nonzero && oscillating[1].nonzero;
    const bool pass = finite && noClip && nonzero && inputsPreserved &&
                      otherBussesPreserved && replaceVerified;
    std::printf("{\"pass\":%s,\"sample_rate\":%d,\"frames_per_block\":%d,"
                "\"finite\":%s,\"no_clip\":%s,\"inputs_preserved\":%s,"
                "\"other_busses_preserved\":%s,\"routing_replace\":%s,\"nonzero\":%s,"
                "\"peak_left\":%.12g,\"peak_right\":%.12g,"
                "\"rms_left\":%.12g,\"rms_right\":%.12g,"
                "\"nonzero_left\":%llu,\"nonzero_right\":%llu,",
                pass ? "true" : "false", kSampleRate, kFramesPerBlock,
                finite ? "true" : "false", noClip ? "true" : "false",
                inputsPreserved ? "true" : "false", otherBussesPreserved ? "true" : "false",
                replaceVerified ? "true" : "false", nonzero ? "true" : "false",
                total[0].peak, total[1].peak, total[0].rms(), total[1].rms(),
                static_cast<unsigned long long>(total[0].nonzero),
                static_cast<unsigned long long>(total[1].nonzero));
    printStats("impulse", impulse);
    std::printf(",");
    printStats("oscillating", oscillating);
    std::printf("}\n");
    return pass ? 0 : 4;
}
