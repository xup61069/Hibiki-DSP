// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/exporters.hpp"
#include "hibiki/wav_ir.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::PeqFilterV1;

PeqFilterV1 make_filter(const double frequency_hz, const double gain_db, const double q) {
    return PeqFilterV1{frequency_hz, gain_db, q};
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes, const std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

std::uint16_t read_u16(const std::vector<std::uint8_t>& bytes, const std::size_t offset) {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset]) |
                                      static_cast<std::uint16_t>(static_cast<std::uint16_t>(
                                          bytes[offset + 1])
                                          << 8U));
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

int main() {
    // validate: nominal filter is accepted.
    CHECK(hibiki::validate_peq_filter(make_filter(1000.0, 3.5, 1.41)));

    // validate: frequency bounds [10, 48000] are inclusive.
    CHECK(hibiki::validate_peq_filter(make_filter(10.0, 0.0, 1.0)));
    CHECK(hibiki::validate_peq_filter(make_filter(48000.0, 0.0, 1.0)));
    CHECK(!hibiki::validate_peq_filter(make_filter(9.999, 0.0, 1.0)));
    CHECK(!hibiki::validate_peq_filter(make_filter(48000.001, 0.0, 1.0)));

    // validate: gain bounds [-44, 24] are inclusive.
    CHECK(hibiki::validate_peq_filter(make_filter(1000.0, -44.0, 1.0)));
    CHECK(hibiki::validate_peq_filter(make_filter(1000.0, 24.0, 1.0)));
    CHECK(!hibiki::validate_peq_filter(make_filter(1000.0, -44.001, 1.0)));
    CHECK(!hibiki::validate_peq_filter(make_filter(1000.0, 24.001, 1.0)));

    // validate: q bounds [0.1, 100] are inclusive.
    CHECK(hibiki::validate_peq_filter(make_filter(1000.0, 0.0, 0.1)));
    CHECK(hibiki::validate_peq_filter(make_filter(1000.0, 0.0, 100.0)));
    CHECK(!hibiki::validate_peq_filter(make_filter(1000.0, 0.0, 0.099)));
    CHECK(!hibiki::validate_peq_filter(make_filter(1000.0, 0.0, 100.001)));

    // validate: non-finite values in every field are rejected.
    {
        const auto nan = std::numeric_limits<double>::quiet_NaN();
        const auto inf = std::numeric_limits<double>::infinity();
        CHECK(!hibiki::validate_peq_filter(make_filter(nan, 0.0, 1.0)));
        CHECK(!hibiki::validate_peq_filter(make_filter(inf, 0.0, 1.0)));
        CHECK(!hibiki::validate_peq_filter(make_filter(1000.0, nan, 1.0)));
        CHECK(!hibiki::validate_peq_filter(make_filter(1000.0, inf, 1.0)));
        CHECK(!hibiki::validate_peq_filter(make_filter(1000.0, 0.0, nan)));
        CHECK(!hibiki::validate_peq_filter(make_filter(1000.0, 0.0, inf)));
    }

    // Equalizer APO: exact line format with three decimal places.
    {
        const std::vector<PeqFilterV1> filters{
            make_filter(100.0, 4.5, 0.707),
            make_filter(2000.0, -6.0, 2.0),
        };
        const auto output = hibiki::export_equalizer_apo(filters);
        CHECK(output ==
              "# Hibiki DSP PEQ export (input coefficients supplied by user)\n"
              "Preamp: -1 dB\n"
              "Filter 1: ON PK Fc 100.000 Hz Gain 4.500 dB Q 0.707\n"
              "Filter 2: ON PK Fc 2000.000 Hz Gain -6.000 dB Q 2.000\n");
    }

    // Equalizer APO: invalid filters are skipped and numbering stays dense.
    {
        const auto inf = std::numeric_limits<double>::infinity();
        const std::vector<PeqFilterV1> filters{
            make_filter(9.0, 1.0, 1.0),
            make_filter(120.0, 2.0, 1.0),
            make_filter(1500.0, inf, 1.0),
            make_filter(3000.0, -3.0, 0.5),
        };
        const auto output = hibiki::export_equalizer_apo(filters);
        CHECK(output.find("Filter 1: ON PK Fc 120.000") != std::string::npos);
        CHECK(output.find("Filter 2: ON PK Fc 3000.000") != std::string::npos);
        CHECK(!contains(output, "Fc 9."));
        CHECK(!contains(output, "Fc 1500."));
    }

    // Equalizer APO: empty input keeps only the header and preamp lines.
    {
        const auto output = hibiki::export_equalizer_apo({});
        CHECK(output ==
              "# Hibiki DSP PEQ export (input coefficients supplied by user)\n"
              "Preamp: -1 dB\n");
    }

    // CamillaDSP YAML: field order and three-decimal formatting.
    {
        const std::vector<PeqFilterV1> filters{make_filter(80.0, 6.0, 0.5)};
        const auto output = hibiki::export_camilladsp_yaml(filters);
        CHECK(output ==
              "# Hibiki DSP PEQ export\n"
              "filters:\n"
              "  - type: Peaking\n"
              "    freq: 80.000\n"
              "    gain: 6.000\n"
              "    q: 0.500\n");
    }

    // CamillaDSP YAML: invalid filters are skipped without breaking output.
    {
        const std::vector<PeqFilterV1> filters{
            make_filter(200.0, 1.0, 1.0),
            make_filter(40000.0, 30.0, 1.0),
            make_filter(400.0, 2.0, 1.0),
        };
        const auto output = hibiki::export_camilladsp_yaml(filters);
        CHECK(output.find("freq: 200.000") != std::string::npos);
        CHECK(output.find("freq: 400.000") != std::string::npos);
        CHECK(!contains(output, "freq: 40000.000"));
    }

    // REW: header uses tabs and numbering counts only valid entries.
    {
        const std::vector<PeqFilterV1> filters{
            make_filter(31.5, -2.5, 1.2),
            make_filter(0.0, 3.0, 1.0),
            make_filter(125.0, 1.5, 0.8),
        };
        const auto output = hibiki::export_rew_filter_list(filters);
        CHECK(output ==
              "Filter\tType\tFreq (Hz)\tGain (dB)\tQ\n"
              "1\tPK\t31.500\t-2.500\t1.200\n"
              "2\tPK\t125.000\t1.500\t0.800\n");
    }

    // Hibiki profile JSON: schema version and comma placement follow valid items.
    {
        const std::vector<PeqFilterV1> filters{
            make_filter(63.0, 2.0, 1.0),
            make_filter(8000.0, 0.05, 99.999),
        };
        const auto output = hibiki::export_hibiki_profile(filters);
        CHECK(output ==
              "{\n"
              "  \"schema_version\": 1,\n"
              "  \"filters\": [\n"
              "    {\"type\": \"peaking\", \"frequency_hz\": 63.000, "
              "\"gain_db\": 2.000, \"q\": 1.000},\n"
              "    {\"type\": \"peaking\", \"frequency_hz\": 8000.000, "
              "\"gain_db\": 0.050, \"q\": 99.999}\n"
              "  ]\n"
              "}\n");
    }

    // Hibiki profile JSON: invalid-only input still emits a parseable empty list.
    {
        const std::vector<PeqFilterV1> filters{make_filter(-1.0, 0.0, 1.0)};
        const auto output = hibiki::export_hibiki_profile(filters);
        CHECK(output ==
              "{\n  \"schema_version\": 1,\n  \"filters\": [\n\n  ]\n}\n");
    }

    // WAV IR: empty samples fail closed.
    CHECK(hibiki::export_wav_f32_ir({}, 48000U, 2U).empty());

    // WAV IR: zero sample rate fails closed.
    {
        const std::vector<float> samples{0.0F};
        CHECK(hibiki::export_wav_f32_ir(samples, 0U, 1U).empty());
    }

    // WAV IR: zero or oversized channel count fails closed.
    {
        const std::vector<float> samples{0.0F, 0.0F};
        CHECK(hibiki::export_wav_f32_ir(samples, 48000U, 0U).empty());
        CHECK(hibiki::export_wav_f32_ir(samples, 48000U, 9U).empty());
    }

    // WAV IR: byte rate overflow at u32 boundary fails closed.
    {
        const std::vector<float> samples{0.0F};
        const auto huge_rate = std::numeric_limits<std::uint32_t>::max() / 8U + 1U;
        CHECK(hibiki::export_wav_f32_ir(samples, huge_rate, 8U).empty());
    }

    // WAV IR: exact RIFF layout for a stereo buffer and round-trip decode.
    {
        const std::vector<float> samples{0.25F, -0.5F, 1.0F, -1.0F};
        const auto wav = hibiki::export_wav_f32_ir(samples, 44100U, 2U);
        CHECK(wav.size() == 44U + samples.size() * sizeof(float));
        CHECK(wav[0] == 'R' && wav[1] == 'I' && wav[2] == 'F' && wav[3] == 'F');
        CHECK(read_u32(wav, 4) == 36U + samples.size() * sizeof(float));
        CHECK(wav[8] == 'W' && wav[9] == 'A' && wav[10] == 'V' && wav[11] == 'E');
        CHECK(wav[12] == 'f' && wav[13] == 'm' && wav[14] == 't' && wav[15] == ' ');
        CHECK(read_u32(wav, 16) == 16U);
        CHECK(read_u16(wav, 20) == 3U);
        CHECK(read_u16(wav, 22) == 2U);
        CHECK(read_u32(wav, 24) == 44100U);
        CHECK(read_u32(wav, 28) == 44100U * 2U * sizeof(float));
        CHECK(read_u16(wav, 32) == 2U * sizeof(float));
        CHECK(read_u16(wav, 34) == 32U);
        CHECK(wav[36] == 'd' && wav[37] == 'a' && wav[38] == 't' && wav[39] == 'a');
        CHECK(read_u32(wav, 40) == samples.size() * sizeof(float));
        const std::uint32_t raw_bits = read_u32(wav, 44);
        static_assert(sizeof(float) == sizeof(std::uint32_t), "float32 required");
        float decoded_sample = 0.0F;
        std::memcpy(&decoded_sample, &raw_bits, sizeof(decoded_sample));
        CHECK(std::fabs(static_cast<double>(decoded_sample) - 0.25) < 0.01);

        const auto decoded = hibiki::decode_ir_wav_v1(wav);
        CHECK(decoded.valid);
        CHECK(decoded.data.schema_version == 1U);
        CHECK(decoded.data.sample_rate == 44100U);
        CHECK(decoded.data.channels == 2U);
        CHECK(decoded.data.interleaved_samples.size() == samples.size());
        for (std::size_t i = 0; i < samples.size(); ++i) {
            CHECK(decoded.data.interleaved_samples[i] == samples[i]);
        }
    }

    return 0;
}
