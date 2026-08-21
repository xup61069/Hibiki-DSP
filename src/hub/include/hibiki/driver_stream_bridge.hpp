#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>

#include "hibiki/driver_stream_transport_v1.h"

namespace hibiki {

struct DriverStreamLaneBlockV1 {
    const float* interleaved{nullptr};
    std::array<char, HIBIKI_DRIVER_STREAM_ENDPOINT_GUID_CAPACITY_V1> endpoint_guid{};
    std::uint32_t channels{0U};
    std::uint32_t sample_rate{0U};
    std::uint32_t frames{0U};
    std::uint64_t sequence{0U};
    std::uint64_t generation{0U};
    std::uint32_t flags{0U};
    std::uint16_t packet_type{0U};
};

// Copies one validated driver packet into caller-owned Float32 storage. The
// bridge never allocates or waits and rejects non-finite samples before a lane
// can enter the graph.
[[nodiscard]] bool decode_driver_stream_packet_v1(
    std::span<const std::uint8_t> packet,
    std::span<float> sample_storage,
    DriverStreamLaneBlockV1& block) noexcept;

}  // namespace hibiki
