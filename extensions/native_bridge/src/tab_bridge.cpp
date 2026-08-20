// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/tab_bridge.hpp"

#include <cmath>
#include <cstring>

namespace hibiki {
namespace {

constexpr std::size_t kHeaderBytes = 16U;
constexpr std::size_t kMaxFrames = 4096U;

std::uint16_t read_u16(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

bool supported_channels(const std::uint16_t channels) noexcept {
    return channels == 1U || channels == 2U || channels == 6U || channels == 8U;
}

bool supported_rate(const std::uint32_t rate) noexcept {
    return rate == 44100U || rate == 48000U || rate == 96000U || rate == 192000U;
}

}  // namespace

float TabCapturePacketViewV1::sample(const std::size_t index) const noexcept {
    float value = 0.0F;
    if (samples_bytes == nullptr || index >= sample_count) return value;
    std::memcpy(&value, samples_bytes + index * sizeof(float), sizeof(float));
    return value;
}

bool decode_tab_capture_packet_v1(const std::span<const std::uint8_t> packet,
                                  TabCapturePacketViewV1& view,
                                  TabPacketError& error) noexcept {
    view = {};
    error = TabPacketError::None;
    if (packet.size() < kHeaderBytes) {
        error = TabPacketError::Truncated;
        return false;
    }
    const auto* raw = packet.data();
    if (raw[0] != 0x48U || raw[1] != 0x49U || raw[2] != 0x42U || raw[3] != 0x54U) {
        error = TabPacketError::InvalidMagic;
        return false;
    }
    if (read_u16(raw + 4U) != 1U) {
        error = TabPacketError::UnsupportedVersion;
        return false;
    }
    const auto channels = read_u16(raw + 6U);
    const auto frames = read_u32(raw + 8U);
    const auto rate = read_u32(raw + 12U);
    if (!supported_channels(channels)) {
        error = TabPacketError::InvalidChannels;
        return false;
    }
    if (!supported_rate(rate)) {
        error = TabPacketError::InvalidSampleRate;
        return false;
    }
    if (frames == 0U || frames > kMaxFrames) {
        error = TabPacketError::InvalidFrameCount;
        return false;
    }
    const auto sample_count = static_cast<std::size_t>(channels) * frames;
    if (sample_count > (SIZE_MAX - kHeaderBytes) / sizeof(float) ||
        packet.size() != kHeaderBytes + sample_count * sizeof(float)) {
        error = TabPacketError::LengthMismatch;
        return false;
    }
    const auto* samples = raw + kHeaderBytes;
    for (std::size_t index = 0; index < sample_count; ++index) {
        float value = 0.0F;
        std::memcpy(&value, samples + index * sizeof(float), sizeof(float));
        if (!std::isfinite(value)) {
            error = TabPacketError::NonFiniteSample;
            return false;
        }
    }
    view.channels = channels;
    view.frames = frames;
    view.sample_rate = rate;
    view.samples_bytes = samples;
    view.sample_count = sample_count;
    return true;
}

}  // namespace hibiki
