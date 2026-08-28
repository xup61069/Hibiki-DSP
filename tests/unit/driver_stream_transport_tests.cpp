// SPDX-License-Identifier: Apache-2.0

#include "hibiki/driver_stream_ring_v1.h"
#include "hibiki/driver_stream_transport_v1.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

#define CHECK(expr)                                                             \
    do {                                                                        \
        if (!(expr)) {                                                          \
            std::fputs("FAILED: " #expr "\n", stderr);                          \
            return 1;                                                           \
        }                                                                       \
    } while (false)

int main() {
    const float driver_samples[] = {0.25F, -0.25F, 0.5F, -0.5F};
    std::array<std::uint8_t, 128> driver_packet{};
    std::size_t driver_packet_bytes = 0U;
    CHECK(hibiki_driver_stream_packet_encode_v1(
        driver_packet.data(), driver_packet.size(), HIBIKI_DRIVER_STREAM_RENDER_V1,
        42U, "8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10", 2U, 48000U, 2U,
        HIBIKI_DRIVER_STREAM_FLAG_DISCONTINUITY_V1, 9U, driver_samples,
        &driver_packet_bytes) == 1 && driver_packet_bytes == 96U);
    CHECK(hibiki_driver_stream_packet_validate_v1(driver_packet.data(), driver_packet_bytes) == 1);

    hibiki_driver_stream_packet_header_v1 header{};
    std::memcpy(&header, driver_packet.data(), sizeof(header));
    CHECK(header.size_bytes == 96U);
    CHECK(header.abi_version == HIBIKI_DRIVER_STREAM_TRANSPORT_ABI_V1);
    CHECK(header.packet_type == HIBIKI_DRIVER_STREAM_RENDER_V1);
    CHECK(header.sequence == 42U);
    CHECK(std::string_view(header.endpoint_guid) == "8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10");
    CHECK(header.channels == 2U && header.sample_rate == 48000U && header.frames == 2U);
    CHECK(header.flags == HIBIKI_DRIVER_STREAM_FLAG_DISCONTINUITY_V1);
    CHECK(header.generation == 9U);

    const std::uint8_t* payload = nullptr;
    std::size_t payload_bytes = 0U;
    CHECK(hibiki_driver_stream_packet_payload_v1(
        driver_packet.data(), driver_packet_bytes, &payload, &payload_bytes) == 1);
    CHECK(payload != nullptr && payload_bytes == 16U);
    const auto* decoded_samples = reinterpret_cast<const float*>(payload);
    CHECK(decoded_samples[0] == 0.25F && decoded_samples[1] == -0.25F &&
          decoded_samples[2] == 0.5F && decoded_samples[3] == -0.5F);

    // Rejection: zero sequence
    std::array<std::uint8_t, 128> reject_packet{};
    reject_packet.fill(0xA5U);
    std::size_t reject_bytes = 123U;
    CHECK(hibiki_driver_stream_packet_encode_v1(
        reject_packet.data(), reject_packet.size(), HIBIKI_DRIVER_STREAM_RENDER_V1,
        0U, "8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10", 2U, 48000U, 2U,
        HIBIKI_DRIVER_STREAM_FLAG_DISCONTINUITY_V1, 9U, driver_samples,
        &reject_bytes) == 0 && reject_bytes == 0U);
    CHECK(std::all_of(reject_packet.begin(), reject_packet.end(),
                      [](std::uint8_t v) { return v == 0xA5U; }));

    // Rejection: zero generation
    reject_packet.fill(0xA5U);
    reject_bytes = 123U;
    CHECK(hibiki_driver_stream_packet_encode_v1(
        reject_packet.data(), reject_packet.size(), HIBIKI_DRIVER_STREAM_RENDER_V1,
        42U, "8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10", 2U, 48000U, 2U,
        HIBIKI_DRIVER_STREAM_FLAG_DISCONTINUITY_V1, 0U, driver_samples,
        &reject_bytes) == 0 && reject_bytes == 0U);
    CHECK(std::all_of(reject_packet.begin(), reject_packet.end(),
                      [](std::uint8_t v) { return v == 0xA5U; }));

    // Rejection: invalid packet type
    reject_packet.fill(0xA5U);
    reject_bytes = 123U;
    CHECK(hibiki_driver_stream_packet_encode_v1(
        reject_packet.data(), reject_packet.size(), 99U,
        42U, "8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10", 2U, 48000U, 2U,
        HIBIKI_DRIVER_STREAM_FLAG_DISCONTINUITY_V1, 9U, driver_samples,
        &reject_bytes) == 0 && reject_bytes == 0U);

    // Rejection: invalid channel count
    reject_packet.fill(0xA5U);
    reject_bytes = 123U;
    CHECK(hibiki_driver_stream_packet_encode_v1(
        reject_packet.data(), reject_packet.size(), HIBIKI_DRIVER_STREAM_RENDER_V1,
        42U, "8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10", 3U, 48000U, 2U,
        HIBIKI_DRIVER_STREAM_FLAG_DISCONTINUITY_V1, 9U, driver_samples,
        &reject_bytes) == 0 && reject_bytes == 0U);

    // Rejection: invalid sample rate
    reject_packet.fill(0xA5U);
    reject_bytes = 123U;
    CHECK(hibiki_driver_stream_packet_encode_v1(
        reject_packet.data(), reject_packet.size(), HIBIKI_DRIVER_STREAM_RENDER_V1,
        42U, "8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10", 2U, 22050U, 2U,
        HIBIKI_DRIVER_STREAM_FLAG_DISCONTINUITY_V1, 9U, driver_samples,
        &reject_bytes) == 0 && reject_bytes == 0U);

    // Rejection: invalid flags
    reject_packet.fill(0xA5U);
    reject_bytes = 123U;
    CHECK(hibiki_driver_stream_packet_encode_v1(
        reject_packet.data(), reject_packet.size(), HIBIKI_DRIVER_STREAM_RENDER_V1,
        42U, "8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10", 2U, 48000U, 2U,
        0x80000000U, 9U, driver_samples, &reject_bytes) == 0 && reject_bytes == 0U);

    
    // Max freshness values are valid
    std::array<std::uint8_t, 128> max_packet{};
    std::size_t max_bytes = 0U;
    constexpr auto max_freshness = (std::numeric_limits<std::uint64_t>::max)();
    CHECK(hibiki_driver_stream_packet_encode_v1(
        max_packet.data(), max_packet.size(), HIBIKI_DRIVER_STREAM_RENDER_V1,
        max_freshness, "8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10", 2U, 48000U, 2U,
        HIBIKI_DRIVER_STREAM_FLAG_DISCONTINUITY_V1, max_freshness, driver_samples,
        &max_bytes) == 1 && max_bytes == 96U);
    CHECK(hibiki_driver_stream_packet_validate_v1(max_packet.data(), max_bytes) == 1);

    // Capture packet with silence flag
    std::array<std::uint8_t, 128> capture_packet{};
    std::size_t capture_bytes = 0U;
    const float capture_samples[] = {0.0F, 0.0F};
    CHECK(hibiki_driver_stream_packet_encode_v1(
        capture_packet.data(), capture_packet.size(), HIBIKI_DRIVER_STREAM_CAPTURE_V1,
        1U, "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee", 2U, 44100U, 1U,
        HIBIKI_DRIVER_STREAM_FLAG_SILENCE_V1, 1U, capture_samples,
        &capture_bytes) == 1 && capture_bytes == 88U);
    CHECK(hibiki_driver_stream_packet_validate_v1(capture_packet.data(), capture_bytes) == 1);

    // Truncated packet is rejected
    CHECK(hibiki_driver_stream_packet_validate_v1(driver_packet.data(), 79U) == 0);
    CHECK(hibiki_driver_stream_packet_payload_v1(
        driver_packet.data(), 79U, &payload, &payload_bytes) == 0);

    // Size mismatch is rejected
    CHECK(hibiki_driver_stream_packet_validate_v1(driver_packet.data(),
                                                   driver_packet_bytes + 1U) == 0);

    // Null pointers are rejected
    CHECK(hibiki_driver_stream_packet_validate_v1(nullptr, 128U) == 0);
    CHECK(hibiki_driver_stream_packet_payload_v1(
        nullptr, 128U, &payload, &payload_bytes) == 0);

    // GUID without NUL within capacity is rejected on validate
    auto bad_guid_packet = driver_packet;
    hibiki_driver_stream_packet_header_v1 bad_guid_header{};
    std::memcpy(&bad_guid_header, driver_packet.data(), sizeof(bad_guid_header));
    std::memset(bad_guid_packet.data() +
                    offsetof(hibiki_driver_stream_packet_header_v1, endpoint_guid),
                'X', HIBIKI_DRIVER_STREAM_ENDPOINT_GUID_CAPACITY_V1);
    CHECK(hibiki_driver_stream_packet_validate_v1(bad_guid_packet.data(),
                                                   driver_packet_bytes) == 0);

    // Non-finite samples are rejected before packet output is mutated.
    const float nonfinite_values[] = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
    for (const float nonfinite : nonfinite_values) {
        const float nonfinite_samples[] = {0.25F, nonfinite, 0.5F, -0.5F};
        std::array<std::uint8_t, 128> untouched_packet{};
        untouched_packet.fill(0xA5U);
        std::size_t untouched_bytes = 123U;
        CHECK(hibiki_driver_stream_packet_encode_v1(
                  untouched_packet.data(), untouched_packet.size(),
                  HIBIKI_DRIVER_STREAM_RENDER_V1, 42U,
                  "8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10", 2U, 48000U, 2U, 0U, 9U,
                  nonfinite_samples, &untouched_bytes) == 0 &&
              untouched_bytes == 0U &&
              std::all_of(untouched_packet.begin(), untouched_packet.end(),
                          [](std::uint8_t value) { return value == 0xA5U; }));
    }

    // The shared-memory ring revalidates a published slot before returning it.
    std::vector<std::uint64_t> ring_storage(
        (sizeof(hibiki_driver_stream_ring_v1) + sizeof(std::uint64_t) - 1U) /
        sizeof(std::uint64_t));
    auto* ring = reinterpret_cast<hibiki_driver_stream_ring_v1*>(ring_storage.data());
    const std::size_t ring_bytes = ring_storage.size() * sizeof(std::uint64_t);
    CHECK(hibiki_driver_stream_ring_init_v1(ring, ring_bytes, 2U, 48000U) ==
          HIBIKI_DRIVER_STREAM_RING_OK_V1);
    auto corrupted_packet = driver_packet;
    const float infinity = std::numeric_limits<float>::infinity();
    std::memcpy(corrupted_packet.data() + HIBIKI_DRIVER_STREAM_HEADER_BYTES_V1,
                &infinity, sizeof(infinity));
    CHECK(hibiki_driver_stream_ring_push_v1(
              ring, ring_bytes, corrupted_packet.data(), driver_packet_bytes) ==
              HIBIKI_DRIVER_STREAM_RING_REJECTED_V1 &&
          ring->producer_sequence == 0U && ring->overrun_count == 0U);
    CHECK(hibiki_driver_stream_ring_push_v1(
              ring, ring_bytes, driver_packet.data(), driver_packet_bytes) ==
          HIBIKI_DRIVER_STREAM_RING_OK_V1);

    auto* slot = &ring->slots[0];
    std::memcpy(slot->packet + HIBIKI_DRIVER_STREAM_HEADER_BYTES_V1,
                &infinity, sizeof(infinity));
    std::array<std::uint8_t, HIBIKI_DRIVER_STREAM_RING_SLOT_CAPACITY_BYTES_V1> popped_packet{};
    popped_packet.fill(0xA5U);
    std::size_t popped_bytes = 123U;
    std::uint32_t silence = 0U;
    CHECK(hibiki_driver_stream_ring_pop_v1(
              ring, ring_bytes, popped_packet.data(), popped_packet.size(), &popped_bytes,
              &silence) == HIBIKI_DRIVER_STREAM_RING_REJECTED_V1 &&
          popped_bytes == 0U && silence == 0U && ring->consumer_sequence == 0U &&
          std::all_of(popped_packet.begin(), popped_packet.end(),
                      [](std::uint8_t value) { return value == 0xA5U; }));

    std::memcpy(slot->packet, driver_packet.data(), driver_packet_bytes);
    popped_bytes = 0U;
    CHECK(hibiki_driver_stream_ring_pop_v1(
              ring, ring_bytes, popped_packet.data(), popped_packet.size(), &popped_bytes,
              &silence) == HIBIKI_DRIVER_STREAM_RING_OK_V1 &&
          popped_bytes == driver_packet_bytes && silence == 0U &&
          ring->consumer_sequence == 1U &&
          std::memcmp(popped_packet.data(), driver_packet.data(), driver_packet_bytes) == 0);

    return 0;
}
