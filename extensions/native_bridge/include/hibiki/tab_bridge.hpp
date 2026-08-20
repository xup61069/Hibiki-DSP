#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstddef>
#include <cstdint>
#include <span>

namespace hibiki {

enum class TabPacketError : std::uint8_t {
    None,
    Truncated,
    InvalidMagic,
    UnsupportedVersion,
    InvalidChannels,
    InvalidSampleRate,
    InvalidFrameCount,
    LengthMismatch,
    NonFiniteSample,
};

struct TabCapturePacketViewV1 {
    std::uint16_t channels{0};
    std::uint32_t frames{0};
    std::uint32_t sample_rate{0};
    const std::uint8_t* samples_bytes{nullptr};
    std::size_t sample_count{0};

    [[nodiscard]] float sample(std::size_t index) const noexcept;
};

[[nodiscard]] bool decode_tab_capture_packet_v1(
    std::span<const std::uint8_t> packet,
    TabCapturePacketViewV1& view,
    TabPacketError& error) noexcept;

}  // namespace hibiki
