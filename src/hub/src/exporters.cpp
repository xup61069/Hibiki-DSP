#include "hibiki/exporters.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace hibiki {

bool validate_peq_filter(const PeqFilterV1& filter) noexcept {
    return std::isfinite(filter.frequency_hz) && filter.frequency_hz >= 10.0 &&
           filter.frequency_hz <= 48000.0 && std::isfinite(filter.gain_db) &&
           filter.gain_db >= -44.0 && filter.gain_db <= 24.0 && std::isfinite(filter.q) &&
           filter.q >= 0.1 && filter.q <= 100.0;
}

std::string export_equalizer_apo(const std::span<const PeqFilterV1> filters) {
    std::ostringstream output;
    output << "# Hibiki DSP PEQ export (input coefficients supplied by user)\n"
           << "Preamp: -1 dB\n";
    std::size_t index = 1;
    for (const auto& filter : filters) {
        if (validate_peq_filter(filter)) {
            output << std::fixed << std::setprecision(3) << "Filter " << index++ << ": ON PK Fc "
                   << filter.frequency_hz << " Hz Gain " << filter.gain_db << " dB Q " << filter.q
                   << "\n";
        }
    }
    return output.str();
}

std::string export_camilladsp_yaml(const std::span<const PeqFilterV1> filters) {
    std::ostringstream output;
    output << "# Hibiki DSP PEQ export\nfilters:\n";
    for (const auto& filter : filters) {
        if (validate_peq_filter(filter)) {
            output << "  - type: Peaking\n    freq: " << std::fixed << std::setprecision(3)
                   << filter.frequency_hz << "\n    gain: " << filter.gain_db << "\n    q: "
                   << filter.q << "\n";
        }
    }
    return output.str();
}

std::string export_rew_filter_list(const std::span<const PeqFilterV1> filters) {
    std::ostringstream output;
    output << "Filter\tType\tFreq (Hz)\tGain (dB)\tQ\n";
    std::size_t index = 1;
    for (const auto& filter : filters) {
        if (validate_peq_filter(filter)) {
            output << index++ << "\tPK\t" << std::fixed << std::setprecision(3)
                   << filter.frequency_hz << "\t" << filter.gain_db << "\t" << filter.q << "\n";
        }
    }
    return output.str();
}

std::string export_hibiki_profile(const std::span<const PeqFilterV1> filters) {
    std::ostringstream output;
    output << "{\n  \"schema_version\": 1,\n  \"filters\": [\n";
    bool first = true;
    for (const auto& filter : filters) {
        if (!validate_peq_filter(filter)) {
            continue;
        }
        if (!first) {
            output << ",\n";
        }
        first = false;
        output << std::fixed << std::setprecision(3)
               << "    {\"type\": \"peaking\", \"frequency_hz\": " << filter.frequency_hz
               << ", \"gain_db\": " << filter.gain_db << ", \"q\": " << filter.q << "}";
    }
    output << "\n  ]\n}\n";
    return output.str();
}

namespace {

void write_u16(std::vector<std::uint8_t>& output, const std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void write_u32(std::vector<std::uint8_t>& output, const std::uint32_t value) {
    for (std::uint32_t shift = 0; shift < 32; shift += 8) {
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

}  // namespace

std::vector<std::uint8_t> export_wav_f32_ir(const std::span<const float> interleaved_samples,
                                           const std::uint32_t sample_rate,
                                           const std::uint16_t channels) {
    constexpr std::uint64_t kRiffBodyBytes = 36U;
    constexpr std::uint64_t kWavHeaderBytes = 44U;
    constexpr std::uint64_t kFloatBytes = sizeof(float);
    constexpr auto kMaxU32 = std::numeric_limits<std::uint32_t>::max();

    if (interleaved_samples.empty() || sample_rate == 0 || channels == 0 || channels > 8) {
        return {};
    }
    if (interleaved_samples.size() % channels != 0) {
        return {};
    }

    // Keep all byte-count additions widened until each serialized field and
    // the vector reserve size have been proven representable.
    if (interleaved_samples.size() > std::numeric_limits<std::uint64_t>::max() ||
        static_cast<std::uint64_t>(interleaved_samples.size()) >
            (std::numeric_limits<std::uint64_t>::max() - kWavHeaderBytes) / kFloatBytes) {
        return {};
    }
    const auto data_bytes_wide = static_cast<std::uint64_t>(interleaved_samples.size()) * kFloatBytes;
    const auto riff_bytes_wide = kRiffBodyBytes + data_bytes_wide;
    const auto output_bytes_wide = kWavHeaderBytes + data_bytes_wide;
    if (data_bytes_wide > kMaxU32 || riff_bytes_wide > kMaxU32 ||
        output_bytes_wide > std::numeric_limits<std::size_t>::max()) {
        return {};
    }
    const auto data_bytes = static_cast<std::uint32_t>(data_bytes_wide);
    const auto riff_bytes = static_cast<std::uint32_t>(riff_bytes_wide);
    const auto byte_rate_wide = static_cast<std::uint64_t>(sample_rate) * channels * sizeof(float);
    if (byte_rate_wide > kMaxU32) {
        return {};
    }
    const auto byte_rate = static_cast<std::uint32_t>(byte_rate_wide);
    const auto block_align = static_cast<std::uint16_t>(channels * sizeof(float));
    std::vector<std::uint8_t> output;
    output.reserve(static_cast<std::size_t>(output_bytes_wide));
    const auto append_tag = [&output](const char* tag) {
        output.insert(output.end(), tag, tag + 4);
    };
    append_tag("RIFF");
    write_u32(output, riff_bytes);
    append_tag("WAVE");
    append_tag("fmt ");
    write_u32(output, 16U);
    write_u16(output, 3U); // IEEE float
    write_u16(output, channels);
    write_u32(output, sample_rate);
    write_u32(output, byte_rate);
    write_u16(output, block_align);
    write_u16(output, 32U);
    append_tag("data");
    write_u32(output, data_bytes);
    const auto* raw = reinterpret_cast<const std::uint8_t*>(interleaved_samples.data());
    output.insert(output.end(), raw, raw + data_bytes);
    return output;
}

}  // namespace hibiki
