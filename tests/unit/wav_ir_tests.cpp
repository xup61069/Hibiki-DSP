// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/exporters.hpp"
#include "hibiki/wav_ir.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

void write_u16(std::vector<std::uint8_t>& bytes, const std::size_t offset,
               const std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

void write_u32(std::vector<std::uint8_t>& bytes, const std::size_t offset,
               const std::uint32_t value) {
    for (std::size_t shift = 0U; shift < 32U; shift += 8U) {
        bytes[offset + (shift / 8U)] = static_cast<std::uint8_t>(value >> shift);
    }
}

// Build a canonical little-endian PCM file with a 44-byte canonical header.
std::vector<std::uint8_t> make_pcm_wav(const std::uint16_t format,
                                       const std::uint16_t channels,
                                       const std::uint32_t sample_rate,
                                       const std::uint16_t bits_per_sample,
                                       const std::vector<std::uint8_t>& payload) {
    const auto block_align =
        static_cast<std::uint16_t>(channels * static_cast<std::uint16_t>(bits_per_sample / 8U));
    std::vector<std::uint8_t> wav(44U + payload.size());
    wav[0] = 'R'; wav[1] = 'I'; wav[2] = 'F'; wav[3] = 'F';
    wav[8] = 'W'; wav[9] = 'A'; wav[10] = 'V'; wav[11] = 'E';
    wav[12] = 'f'; wav[13] = 'm'; wav[14] = 't'; wav[15] = ' ';
    write_u32(wav, 16U, 16U);
    write_u16(wav, 20U, format);
    write_u16(wav, 22U, channels);
    write_u32(wav, 24U, sample_rate);
    write_u32(wav, 28U, sample_rate * block_align);
    write_u16(wav, 32U, block_align);
    write_u16(wav, 34U, bits_per_sample);
    wav[36] = 'd'; wav[37] = 'a'; wav[38] = 't'; wav[39] = 'a';
    write_u32(wav, 40U, static_cast<std::uint32_t>(payload.size()));
    for (std::size_t index = 0U; index < payload.size(); ++index) {
        wav[44U + index] = payload[index];
    }
    write_u32(wav, 4U, static_cast<std::uint32_t>(36U + payload.size()));
    return wav;
}

}  // namespace

int main() {
    using hibiki::decode_ir_wav_v1;
    using hibiki::export_wav_f32_ir;
    using hibiki::IrConvolverV1;
    using hibiki::IrPhaseMode;
    using hibiki::IrPhaseResolutionV1;
    using hibiki::IrPhasePolicyV1;
    using hibiki::prepare_ir_convolver_from_wav_v1;
    using hibiki::resolve_ir_phase_policy;

    const auto linear_resolution = resolve_ir_phase_policy(
        IrPhasePolicyV1{1U, IrPhaseMode::LinearPhase, 0.5});

    // ---- decode: supported bit depths and value scaling --------------------
    {
        const std::vector<std::uint8_t> pcm16_payload{
            0xff, 0x7f,          // 32767 -> +0.999969...
            0x00, 0x80};         // -32768 -> -1.0
        const auto pcm16 = decode_ir_wav_v1(make_pcm_wav(1U, 1U, 48000U, 16U, pcm16_payload));
        CHECK(pcm16.valid && pcm16.data.frames() == 2U &&
              std::abs(pcm16.data.interleaved_samples[0] - (32767.0F / 32768.0F)) < 1e-5F &&
              std::abs(pcm16.data.interleaved_samples[1] + 1.0F) < 1e-6F);

        const std::vector<std::uint8_t> pcm24_payload{
            0xff, 0xff, 0x7f,    // 8388607 -> +0.99999988...
            0x00, 0x00, 0x80};   // -8388608 -> -1.0
        const auto pcm24 = decode_ir_wav_v1(make_pcm_wav(1U, 1U, 48000U, 24U, pcm24_payload));
        CHECK(pcm24.valid && pcm24.data.frames() == 2U &&
              std::abs(pcm24.data.interleaved_samples[0] - (8388607.0F / 8388608.0F)) < 1e-6F &&
              std::abs(pcm24.data.interleaved_samples[1] + 1.0F) < 1e-6F);

        const std::vector<std::uint8_t> pcm32_payload{0x00, 0x00, 0x00, 0xc0};
        const auto pcm32 = decode_ir_wav_v1(make_pcm_wav(1U, 1U, 48000U, 32U, pcm32_payload));
        CHECK(pcm32.valid && std::abs(pcm32.data.interleaved_samples[0] + 0.5F) < 1e-9F);
    }

    // ---- decode: odd-size chunks need one padding byte ---------------------
    {
        const float source[] = {0.5F, 0.25F};
        const auto base_wav = export_wav_f32_ir(source, 48000U, 1U);
        // Insert an unknown odd-sized chunk between the format and data
        // chunks; the parser must skip it plus its pad byte and still find
        // the data chunk at its new location.
        constexpr std::string_view kOddChunk{"junk"};
        std::vector<std::uint8_t> grown(base_wav.begin(), base_wav.begin() + 36);
        const std::size_t junk_offset = grown.size();
        grown.insert(grown.end(), kOddChunk.begin(), kOddChunk.end());
        grown.insert(grown.end(), 8U, 0U);  // size, payload and pad slots
        write_u32(grown, junk_offset + 4U, 3U);
        grown[junk_offset + 8U] = 0x7fU;    // single payload byte
        // bytes 9..10 are payload padding and byte 11 is the RIFF pad byte
        grown.insert(grown.end(), base_wav.begin() + 36, base_wav.end());
        write_u32(grown, 4U, static_cast<std::uint32_t>(grown.size() - 8U));
        const auto decoded = decode_ir_wav_v1(grown);
        CHECK(decoded.valid && decoded.data.sample_rate == 48000U &&
              decoded.data.frames() == 2U && decoded.data.interleaved_samples.size() == 2U &&
              std::abs(decoded.data.interleaved_samples[0] - 0.5F) < 1e-6F);
    }

    // ---- decode: malformed headers and containers fail closed --------------
    {
        const float source[] = {1.0F, 2.0F};
        const auto wav = export_wav_f32_ir(source, 48000U, 1U);

        auto short_header = wav;
        short_header.resize(12U);
        CHECK(!decode_ir_wav_v1(short_header).valid);
        auto tiny = wav;
        tiny.resize(11U);
        CHECK(!decode_ir_wav_v1(tiny).valid);

        auto wave_tag = wav;
        wave_tag[9] = 'X';
        CHECK(!decode_ir_wav_v1(wave_tag).valid);

        auto truncated_chunk = wav;
        write_u32(truncated_chunk, 36U, 20U);
        CHECK(!decode_ir_wav_v1(truncated_chunk).valid);

        auto trailing_garbage = wav;
        trailing_garbage.resize(wav.size() - 4U);
        write_u32(trailing_garbage, 4U, static_cast<std::uint32_t>(trailing_garbage.size() - 8U));
        write_u32(trailing_garbage, 40U, 4U);
        CHECK(decode_ir_wav_v1(trailing_garbage).valid);

        auto missing_data = wav;
        missing_data[37] = 'D';
        CHECK(!decode_ir_wav_v1(missing_data).valid);

        auto duplicate_data = wav;
        duplicate_data.resize(wav.size() + 8U);
        duplicate_data[wav.size() + 0U] = 'd';
        duplicate_data[wav.size() + 1U] = 'a';
        duplicate_data[wav.size() + 2U] = 't';
        duplicate_data[wav.size() + 3U] = 'a';
        write_u32(duplicate_data, wav.size() + 4U, 0U);
        write_u32(duplicate_data, 4U, static_cast<std::uint32_t>(duplicate_data.size() - 8U));
        CHECK(!decode_ir_wav_v1(duplicate_data).valid);

        auto small_fmt = wav;
        write_u32(small_fmt, 16U, 15U);
        CHECK(!decode_ir_wav_v1(small_fmt).valid);

        auto duplicate_fmt = wav;
        duplicate_fmt.resize(wav.size() + 24U);
        duplicate_fmt[wav.size() + 0U] = 'f';
        duplicate_fmt[wav.size() + 1U] = 'm';
        duplicate_fmt[wav.size() + 2U] = 't';
        duplicate_fmt[wav.size() + 3U] = ' ';
        write_u32(duplicate_fmt, wav.size() + 4U, 16U);
        for (std::size_t index = 0U; index < 16U; ++index) {
            duplicate_fmt[wav.size() + 8U + index] = wav[20U + index];
        }
        write_u32(duplicate_fmt, 4U, static_cast<std::uint32_t>(duplicate_fmt.size() - 8U));
        CHECK(!decode_ir_wav_v1(duplicate_fmt).valid);
    }

    // ---- decode: unsupported layouts and alignment -------------------------
    {
        const std::vector<std::uint8_t> stereo_payload(8U);
        auto nine_channels = make_pcm_wav(1U, 9U, 48000U, 16U, stereo_payload);
        write_u16(nine_channels, 32U, 18U);
        CHECK(!decode_ir_wav_v1(nine_channels).valid);

        auto slow_rate = make_pcm_wav(1U, 1U, 8000U, 16U, {0x00, 0x00});
        write_u32(slow_rate, 24U, 7999U);
        write_u32(slow_rate, 28U, 15998U);
        CHECK(!decode_ir_wav_v1(slow_rate).valid);

        auto fast_rate = make_pcm_wav(1U, 1U, 192000U, 16U, {0x00, 0x00});
        write_u32(fast_rate, 24U, 192001U);
        write_u32(fast_rate, 28U, 384002U);
        CHECK(!decode_ir_wav_v1(fast_rate).valid);

        const std::vector<std::uint8_t> quad_payload(8U);
        auto wrong_align = make_pcm_wav(1U, 4U, 48000U, 16U, quad_payload);
        write_u16(wrong_align, 32U, 7U);
        CHECK(!decode_ir_wav_v1(wrong_align).valid);

        const std::vector<std::uint8_t> mono_payload{0x01, 0x00, 0x02, 0x00, 0x03, 0x00};
        auto ragged_data = make_pcm_wav(1U, 1U, 48000U, 16U, mono_payload);
        write_u32(ragged_data, 40U, 5U);
        CHECK(!decode_ir_wav_v1(ragged_data).valid);

        auto zero_frames = make_pcm_wav(1U, 1U, 48000U, 16U, {});
        CHECK(!decode_ir_wav_v1(zero_frames).valid);

        auto empty_bytes = make_pcm_wav(1U, 1U, 48000U, 16U, {0x00, 0x00});
        empty_bytes.resize(44U);
        write_u32(empty_bytes, 4U, 36U);
        CHECK(!decode_ir_wav_v1(empty_bytes).valid);
    }

    // ---- decode: frame bounds ----------------------------------------------
    {
        const std::vector<float> frames(16U, 0.125F);
        const auto wav = export_wav_f32_ir(frames, 48000U, 1U);
        CHECK(!decode_ir_wav_v1(wav, 0U).valid);
        CHECK(!decode_ir_wav_v1(wav, 15U).valid);
        const auto exact_bound = decode_ir_wav_v1(wav, 16U);
        CHECK(exact_bound.valid && exact_bound.data.frames() == 16U);
    }

    // ---- prepare: validation and broadcast ---------------------------------
    {
        const float source[] = {0.5F, 0.25F, 0.125F};
        const auto wav = export_wav_f32_ir(source, 48000U, 1U);
        const auto decoded = decode_ir_wav_v1(wav);
        CHECK(decoded.valid && decoded.diagnostic == "ok");

        auto mismatched_schema = decoded.data;
        mismatched_schema.schema_version = 2U;
        IrConvolverV1 schema_convolver;
        CHECK(!prepare_ir_convolver_from_wav_v1(schema_convolver, mismatched_schema,
                                                linear_resolution));

        hibiki::IrWavDataV1 ragged{};
        ragged.schema_version = 1U;
        ragged.sample_rate = 48000U;
        ragged.channels = 2U;
        ragged.interleaved_samples = {1.0F, 0.5F, 0.25F};
        IrConvolverV1 ragged_convolver;
        CHECK(ragged.interleaved_samples.size() % ragged.channels != 0U);
        CHECK(!prepare_ir_convolver_from_wav_v1(ragged_convolver, ragged, linear_resolution));

        auto bad_rate = decoded.data;
        bad_rate.sample_rate = 192001U;
        IrConvolverV1 rate_convolver;
        CHECK(!prepare_ir_convolver_from_wav_v1(rate_convolver, bad_rate, linear_resolution));

        IrConvolverV1 too_many_channels;
        CHECK(!prepare_ir_convolver_from_wav_v1(too_many_channels, decoded.data,
                                                linear_resolution, 9U));

        IrConvolverV1 invalid_phase;
        IrPhaseResolutionV1 unresolved{};
        unresolved.mode = IrPhaseMode::LinearPhase;
        CHECK(!prepare_ir_convolver_from_wav_v1(invalid_phase, decoded.data, unresolved));

        const auto bypass_resolution = resolve_ir_phase_policy(
            IrPhasePolicyV1{1U, IrPhaseMode::Bypass, 0.0});
        IrConvolverV1 bypass_convolver;
        CHECK(!prepare_ir_convolver_from_wav_v1(bypass_convolver, decoded.data,
                                                bypass_resolution));

        IrConvolverV1 broadcast;
        CHECK(prepare_ir_convolver_from_wav_v1(broadcast, decoded.data, linear_resolution, 8U));
        const auto broadcast_status = broadcast.status();
        CHECK(broadcast_status.valid && broadcast_status.channels == 8U &&
              broadcast_status.kernel_channels == 1U && broadcast_status.taps == 3U);

        const std::vector<float> stereo_samples{1.0F, 0.5F, 0.25F, -0.125F};
        const auto stereo_wav = export_wav_f32_ir(stereo_samples, 48000U, 2U);
        const auto stereo_decoded = decode_ir_wav_v1(stereo_wav);
        CHECK(stereo_decoded.valid);
        IrConvolverV1 stereo_mismatch;
        CHECK(!prepare_ir_convolver_from_wav_v1(stereo_mismatch, stereo_decoded.data,
                                                linear_resolution, 3U));

        IrConvolverV1 stereo_broadcast;
        CHECK(prepare_ir_convolver_from_wav_v1(stereo_broadcast, stereo_decoded.data,
                                               linear_resolution));
        const auto stereo_status = stereo_broadcast.status();
        CHECK(stereo_status.valid && stereo_status.channels == 2U &&
              stereo_status.kernel_channels == 2U);
    }

    // ---- prepare: strength-zero linear phase keeps the source kernel ------
    {
        const float source[] = {1.0F, 0.0F};
        const auto wav = export_wav_f32_ir(source, 48000U, 1U);
        const auto decoded = decode_ir_wav_v1(wav);
        CHECK(decoded.valid);
        const auto zero_strength = resolve_ir_phase_policy(
            IrPhasePolicyV1{1U, IrPhaseMode::LinearPhase, 0.0});
        IrConvolverV1 convolver;
        CHECK(prepare_ir_convolver_from_wav_v1(convolver, decoded.data, zero_strength));
        const auto status = convolver.status();
        CHECK(status.valid && status.taps == 2U && !status.uses_fir &&
              status.declared_delay_ms == 0.0);
    }

    std::fputs("wav_ir_tests passed\n", stdout);
    return 0;
}
