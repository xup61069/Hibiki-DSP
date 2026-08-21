#include "hibiki/wav_ir.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace hibiki {
namespace {

std::uint16_t read_u16(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0U]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1U]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0U]) |
           (static_cast<std::uint32_t>(bytes[1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3U]) << 24U);
}

bool tag_is(const std::uint8_t* bytes, const char* tag) noexcept {
    return bytes[0U] == static_cast<std::uint8_t>(tag[0]) &&
           bytes[1U] == static_cast<std::uint8_t>(tag[1]) &&
           bytes[2U] == static_cast<std::uint8_t>(tag[2]) &&
           bytes[3U] == static_cast<std::uint8_t>(tag[3]);
}

float pcm24_to_float(const std::uint8_t* bytes) noexcept {
    std::int32_t value = static_cast<std::int32_t>(bytes[0U]) |
                         (static_cast<std::int32_t>(bytes[1U]) << 8U) |
                         (static_cast<std::int32_t>(bytes[2U]) << 16U);
    if ((value & 0x00800000) != 0) value |= static_cast<std::int32_t>(0xff000000U);
    return static_cast<float>(static_cast<double>(value) / 8388608.0);
}

bool finite_samples(const std::vector<float>& samples) noexcept {
    return std::all_of(samples.begin(), samples.end(),
                       [](const float value) { return std::isfinite(value); });
}

IrWavDecodeResultV1 failure(std::string diagnostic) noexcept {
    IrWavDecodeResultV1 result{};
    result.diagnostic = std::move(diagnostic);
    return result;
}

}  // namespace

IrWavDecodeResultV1 decode_ir_wav_v1(const std::span<const std::uint8_t> bytes,
                                     const std::size_t max_frames) noexcept {
    try {
        if (bytes.size() < 12U || bytes.size() > kMaxIrWavBytesV1 || max_frames == 0U ||
            max_frames > kMaxRealtimeIrTapsV1 || !tag_is(bytes.data(), "RIFF") ||
            !tag_is(bytes.data() + 8U, "WAVE")) {
            return failure("IR WAV header, size or bound is invalid");
        }
        const auto riff_payload_bytes = static_cast<std::size_t>(read_u32(bytes.data() + 4U));
        if (riff_payload_bytes < 4U || riff_payload_bytes > bytes.size() - 8U) {
            return failure("IR WAV RIFF container length is invalid");
        }
        const auto riff_end = 8U + riff_payload_bytes;

        bool have_fmt = false;
        bool have_data = false;
        std::uint16_t format = 0U;
        std::uint16_t channels = 0U;
        std::uint16_t bits_per_sample = 0U;
        std::uint16_t block_align = 0U;
        std::uint32_t sample_rate = 0U;
        std::size_t data_offset = 0U;
        std::size_t data_bytes = 0U;
        std::size_t offset = 12U;
        while (offset < riff_end) {
            if (riff_end - offset < 8U) return failure("IR WAV chunk header is truncated");
            const auto chunk_bytes = static_cast<std::size_t>(read_u32(bytes.data() + offset + 4U));
            const auto payload_offset = offset + 8U;
            if (chunk_bytes > riff_end - payload_offset) {
                return failure("IR WAV chunk extends beyond file");
            }
            if (tag_is(bytes.data() + offset, "fmt ")) {
                if (have_fmt || chunk_bytes < 16U) return failure("IR WAV fmt chunk is invalid");
                format = read_u16(bytes.data() + payload_offset);
                channels = read_u16(bytes.data() + payload_offset + 2U);
                sample_rate = read_u32(bytes.data() + payload_offset + 4U);
                block_align = read_u16(bytes.data() + payload_offset + 12U);
                bits_per_sample = read_u16(bytes.data() + payload_offset + 14U);
                have_fmt = true;
            } else if (tag_is(bytes.data() + offset, "data")) {
                if (have_data) return failure("IR WAV contains duplicate data chunks");
                data_offset = payload_offset;
                data_bytes = chunk_bytes;
                have_data = true;
            }
            const auto padded = chunk_bytes + (chunk_bytes & 1U);
            if (padded > riff_end - payload_offset) return failure("IR WAV chunk padding is invalid");
            offset = payload_offset + padded;
        }

        const auto bytes_per_sample = static_cast<std::size_t>(bits_per_sample / 8U);
        const bool float32 = format == 3U && bits_per_sample == 32U;
        const bool signed_pcm = format == 1U && (bits_per_sample == 16U || bits_per_sample == 24U ||
                                                  bits_per_sample == 32U);
        if (!have_fmt || !have_data || channels == 0U || channels > 8U || sample_rate < 8000U ||
            sample_rate > 192000U || (!float32 && !signed_pcm) || bytes_per_sample == 0U ||
            block_align != static_cast<std::uint16_t>(channels * bytes_per_sample) ||
            data_bytes == 0U || data_bytes % block_align != 0U) {
            return failure("IR WAV format, channel layout or sample alignment is unsupported");
        }
        const auto frames = data_bytes / block_align;
        if (frames == 0U || frames > max_frames ||
            frames > std::numeric_limits<std::size_t>::max() / channels) {
            return failure("IR WAV exceeds the bounded tap count");
        }

        IrWavDecodeResultV1 result{};
        result.data.schema_version = 1U;
        result.data.sample_rate = sample_rate;
        result.data.channels = channels;
        result.data.interleaved_samples.resize(frames * channels);
        const auto* raw = bytes.data() + data_offset;
        for (std::size_t index = 0U; index < result.data.interleaved_samples.size(); ++index) {
            const auto* sample = raw + (index * bytes_per_sample);
            float value = 0.0F;
            if (float32) {
                std::uint32_t bits = read_u32(sample);
                value = std::bit_cast<float>(bits);
            } else if (bits_per_sample == 16U) {
                const auto value16 = static_cast<std::int16_t>(read_u16(sample));
                value = static_cast<float>(static_cast<double>(value16) / 32768.0);
            } else if (bits_per_sample == 24U) {
                value = pcm24_to_float(sample);
            } else {
                const auto value32 = static_cast<std::int32_t>(read_u32(sample));
                value = static_cast<float>(static_cast<double>(value32) / 2147483648.0);
            }
            result.data.interleaved_samples[index] = value;
        }
        if (!finite_samples(result.data.interleaved_samples)) {
            return failure("IR WAV contains a non-finite sample");
        }
        result.valid = true;
        result.diagnostic = "ok";
        return result;
    } catch (const std::bad_alloc&) {
        return failure("IR WAV allocation failed within the control-plane bound");
    } catch (...) {
        return failure("IR WAV decode failed");
    }
}

bool prepare_ir_convolver_from_wav_v1(IrConvolverV1& convolver,
                                      const IrWavDataV1& data,
                                      const IrPhaseResolutionV1& phase,
                                      const std::uint32_t render_channels) noexcept {
    try {
        const auto target_channels = render_channels == 0U ? data.channels : render_channels;
        if (data.schema_version != 1U || data.sample_rate < 8000U || data.sample_rate > 192000U ||
            data.channels == 0U || data.channels > 8U || data.frames() == 0U ||
            target_channels == 0U || target_channels > 8U ||
            (data.channels != 1U && data.channels != target_channels) ||
            data.frames() > kMaxRealtimeIrTapsV1 ||
            data.interleaved_samples.size() != data.frames() * data.channels ||
            !std::all_of(data.interleaved_samples.begin(), data.interleaved_samples.end(),
                         [](const float value) { return std::isfinite(value); })) {
            return false;
        }
        std::vector<float> channel_major(data.interleaved_samples.size());
        for (std::size_t frame = 0U; frame < data.frames(); ++frame) {
            for (std::uint16_t channel = 0U; channel < data.channels; ++channel) {
                channel_major[static_cast<std::size_t>(channel) * data.frames() + frame] =
                    data.interleaved_samples[frame * data.channels + channel];
            }
        }
        const auto transformed = build_ir_phase_kernel_v1(
            channel_major, data.frames(), data.channels, data.sample_rate, phase);
        if (!transformed.valid ||
            (phase.mode == IrPhaseMode::Bypass && transformed.resolution.mode == IrPhaseMode::Bypass)) {
            return false;
        }
        return convolver.prepare(transformed.channel_major, transformed.taps,
                                 transformed.kernel_channels, target_channels, data.sample_rate,
                                 phase);
    } catch (...) {
        return false;
    }
}

}  // namespace hibiki
