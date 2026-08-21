#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/audio_engine.hpp"
#include "hibiki/driver_control_transport_v1.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace hibiki {

struct DriverEndpointStateV1 final {
    std::array<char, HIBIKI_ENDPOINT_GUID_CAPACITY> endpoint_guid{};
    std::array<char, HIBIKI_ENDPOINT_GUID_CAPACITY> event_context_guid{};
    std::uint32_t channel_count{0U};
    std::uint32_t sample_rate{0U};
    std::uint32_t frames_per_buffer{0U};
    std::int32_t requested_db_q16_16{0};
    std::int32_t safety_ceiling_db_q16_16{0};
    std::int32_t effective_db_q16_16{0};
    bool mute{false};
    std::uint64_t generation{0U};
    std::uint32_t actuator{HIBIKI_ACTUATOR_INTERNAL_DSP};
};

[[nodiscard]] bool decode_driver_endpoint_state_packet_v1(
    std::span<const std::uint8_t> packet,
    DriverEndpointStateV1& state,
    std::uint16_t& message_type,
    std::uint64_t& request_id) noexcept;

enum class DriverVolumeSyncResultV1 : std::uint8_t {
    Applied,
    IgnoredSelf,
    StaleGeneration,
    Invalid,
};

// Control-plane adapter for state notifications emitted by a future MS-PL
// WaveRT driver. It mirrors the WindowsVolumeLink semantics without linking
// driver code into the GPL engine: only the versioned Apache packet crosses
// the boundary, and all calls remain off the RT thread.
class DriverVolumeLinkV1 final {
public:
    static constexpr std::size_t kMaxIgnoredContexts = 8U;

    DriverVolumeLinkV1() noexcept = default;

    [[nodiscard]] bool add_ignored_event_context(std::string_view context) noexcept;
    void clear_ignored_event_contexts() noexcept;

    [[nodiscard]] DriverVolumeSyncResultV1 apply(
        AudioEngineModel& engine,
        std::string_view output_group,
        const DriverEndpointStateV1& state) const noexcept;

private:
    [[nodiscard]] bool is_ignored(std::string_view context) const noexcept;

    std::array<std::array<char, HIBIKI_ENDPOINT_GUID_CAPACITY>, kMaxIgnoredContexts>
        ignored_contexts_{};
    std::size_t ignored_context_count_{0U};
};

}  // namespace hibiki
