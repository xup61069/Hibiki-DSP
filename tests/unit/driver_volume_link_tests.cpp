// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/driver_control_bridge.hpp"

#include "hibiki/driver_control_transport_v1.h"

#include "hibiki/audio_engine.hpp"

#include <cstdio>
#include <cmath>
#include <array>
#include <span>
#include <cstring>
#include <string>

#define CHECK(expr) do { if (!(expr)) { std::fputs("FAILED: " #expr "\n", stderr); return 1; } } while (false)

namespace {

using hibiki::DriverEndpointStateV1;
using hibiki::DriverVolumeLinkV1;
using hibiki::DriverVolumeSyncResultV1;
using hibiki::AudioEngineModel;

struct EncodedPacket {
    std::array<std::uint8_t, HIBIKI_DRIVER_CONTROL_ENDPOINT_STATE_PACKET_BYTES_V1> bytes{};
    std::size_t size{0U};
};

EncodedPacket encode_packet(const DriverEndpointStateV1& state,
                            const std::uint16_t message_type =
                                HIBIKI_DRIVER_VOLUME_NOTIFICATION,
                            const std::uint64_t request_id = 77U)
{
    hibiki_driver_endpoint_state_v1 raw{};
    std::memcpy(raw.endpoint_guid, state.endpoint_guid.data(),
                state.endpoint_guid.size());
    std::memcpy(raw.event_context_guid, state.event_context_guid.data(),
                state.event_context_guid.size());
    raw.channel_count = state.channel_count;
    raw.sample_rate = state.sample_rate;
    raw.frames_per_buffer = state.frames_per_buffer;
    raw.requested_db_q16_16 = state.requested_db_q16_16;
    raw.safety_ceiling_db_q16_16 = state.safety_ceiling_db_q16_16;
    raw.effective_db_q16_16 = state.effective_db_q16_16;
    raw.mute = state.mute ? 1U : 0U;
    raw.generation = state.generation;
    raw.actuator = state.actuator;
    EncodedPacket packet;
    hibiki_driver_endpoint_state_packet_encode_v1(
        packet.bytes.data(), packet.bytes.size(), message_type, request_id, &raw,
        &packet.size);
    return packet;
}

DriverEndpointStateV1 valid_state()
{
    DriverEndpointStateV1 state{};
    std::memcpy(state.endpoint_guid.data(), "{D7A2}-render", 13U);
    std::memcpy(state.event_context_guid.data(), "{EVT}-external", 15U);
    state.channel_count = 2U;
    state.sample_rate = 48000U;
    state.frames_per_buffer = 480U;
    state.requested_db_q16_16 = -18 * 65536;
    state.safety_ceiling_db_q16_16 = 0;
    state.effective_db_q16_16 = -6 * 65536;
    state.mute = false;
    state.generation = 5U;
    state.actuator = HIBIKI_ACTUATOR_INTERNAL_DSP;
    return state;
}

}  // namespace

int main()
{
    // ---- decode round trip and truncated packet rejection ---------------------
    {
        const auto packet = encode_packet(valid_state());
        CHECK(packet.size == HIBIKI_DRIVER_CONTROL_ENDPOINT_STATE_PACKET_BYTES_V1);
        DriverEndpointStateV1 decoded{};
        std::uint16_t message_type = 0U;
        std::uint64_t request_id = 0U;
        CHECK(hibiki::decode_driver_endpoint_state_packet_v1(
                  std::span<const std::uint8_t>(packet.bytes.data(), packet.size),
                  decoded, message_type, request_id));
        CHECK(message_type == HIBIKI_DRIVER_VOLUME_NOTIFICATION);
        CHECK(request_id == 77U);
        CHECK(decoded.channel_count == 2U && decoded.sample_rate == 48000U &&
              decoded.frames_per_buffer == 480U && decoded.generation == 5U);
        CHECK(decoded.requested_db_q16_16 == -18 * 65536);
        CHECK(!decoded.mute);
        CHECK(std::string_view(decoded.event_context_guid.data()) == "{EVT}-external");

        CHECK(!hibiki::decode_driver_endpoint_state_packet_v1(
                  std::span<const std::uint8_t>(packet.bytes.data(), packet.size - 1U),
                  decoded, message_type, request_id));
    }

    // ---- apply rejects malformed states ----------------------------------------
    {
        DriverVolumeLinkV1 link;
        AudioEngineModel engine;
        auto state = valid_state();

        auto broken = state;
        broken.endpoint_guid[0] = '\0';
        CHECK(link.apply(engine, "main", broken) ==
              DriverVolumeSyncResultV1::Invalid);

        broken = state;
        broken.channel_count = 3U;
        CHECK(link.apply(engine, "main", broken) ==
              DriverVolumeSyncResultV1::Invalid);

        for (const std::uint32_t bad_rate :
             {8000U, 22050U, 44101U, 384000U}) {
            broken = state;
            broken.sample_rate = bad_rate;
            CHECK(link.apply(engine, "main", broken) ==
                  DriverVolumeSyncResultV1::Invalid);
        }

        broken = state;
        broken.frames_per_buffer = 0U;
        CHECK(link.apply(engine, "main", broken) ==
              DriverVolumeSyncResultV1::Invalid);
        broken = state;
        broken.frames_per_buffer = 4097U;
        CHECK(link.apply(engine, "main", broken) ==
              DriverVolumeSyncResultV1::Invalid);

        broken = state;
        broken.generation = 0U;
        CHECK(link.apply(engine, "main", broken) ==
              DriverVolumeSyncResultV1::Invalid);

        broken = state;
        broken.actuator = HIBIKI_ACTUATOR_STRICT_DIRECT + 1U;
        CHECK(link.apply(engine, "main", broken) ==
              DriverVolumeSyncResultV1::Invalid);

        for (const std::int32_t bad_db : {-145 * 65536, 13 * 65536}) {
            broken = state;
            broken.requested_db_q16_16 = bad_db;
            CHECK(link.apply(engine, "main", broken) ==
                  DriverVolumeSyncResultV1::Invalid);
        }

        CHECK(link.apply(engine, "", state) == DriverVolumeSyncResultV1::Invalid);
    }

    // ---- generation semantics ---------------------------------------------------
    {
        DriverVolumeLinkV1 link;
        AudioEngineModel engine;
        auto state = valid_state();
        state.requested_db_q16_16 = -12 * 65536;
        CHECK(link.apply(engine, "main", state) == DriverVolumeSyncResultV1::Applied);
        CHECK(std::abs(engine.volume("main").requested_db + 12.0) < 0.001);

        state.generation = 4U;
        CHECK(link.apply(engine, "main", state) ==
              DriverVolumeSyncResultV1::StaleGeneration);
    }

    // ---- ignored event contexts --------------------------------------------------
    {
        DriverVolumeLinkV1 link;
        AudioEngineModel engine;

        CHECK(link.add_ignored_event_context("{self}"));
        CHECK(link.add_ignored_event_context("{self}"));  // idempotent

        auto state = valid_state();
        std::memcpy(state.event_context_guid.data(), "{self}\0padding", 14U);
        CHECK(link.apply(engine, "main", state) ==
              DriverVolumeSyncResultV1::IgnoredSelf);

        std::memcpy(state.event_context_guid.data(), "{other}", 8U);
        state.generation = 9U;
        CHECK(link.apply(engine, "main", state) == DriverVolumeSyncResultV1::Applied);

        CHECK(!link.add_ignored_event_context(""));
        CHECK(!link.add_ignored_event_context(std::string(40U, 'x')));

        // Capacity is measured on a fresh link: {self} already occupies one
        // slot on this instance.
        DriverVolumeLinkV1 capacity_link;
        for (std::size_t index = 0; index < DriverVolumeLinkV1::kMaxIgnoredContexts; ++index) {
            const auto context = "ctx-" + std::to_string(index);
            CHECK(capacity_link.add_ignored_event_context(context));
        }
        CHECK(!capacity_link.add_ignored_event_context("one-too-many"));

        link.clear_ignored_event_contexts();
        CHECK(link.apply(engine, "main", state) == DriverVolumeSyncResultV1::Applied);
    }

    return 0;
}
