// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/device_catalog_snapshot.hpp"
#include "hibiki/ipc.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::DeviceCatalogSnapshotEntryV1;
using hibiki::DeviceCatalogSnapshotPublisherV1;
using hibiki::DeviceCatalogSnapshotStoreV1;
using hibiki::IpcFrameV1;
using hibiki::kIpcMagicV1;
using hibiki::kIpcVersionV1;
using IpcMessageType = hibiki::IpcMessageType;
using hibiki::device_catalog_snapshot_reply_v1;
using hibiki::PhysicalDeviceCatalogV1;
using hibiki::PhysicalDeviceDescriptorV1;

[[nodiscard]] PhysicalDeviceDescriptorV1 make_descriptor(
    const std::string& endpoint_id,
    const std::string& display_name,
    const std::uint8_t flow = 0U) noexcept
{
    auto descriptor = PhysicalDeviceDescriptorV1{};
    descriptor.endpoint_id = endpoint_id;
    descriptor.display_name = display_name;
    descriptor.flow = static_cast<decltype(descriptor.flow)>(flow);
    return descriptor;
}

[[nodiscard]] DeviceCatalogSnapshotEntryV1 make_entry(
    const std::string& endpoint_id,
    const std::string& display_name,
    const std::uint8_t flow,
    const std::uint8_t availability,
    const std::uint16_t flags) noexcept
{
    auto entry = DeviceCatalogSnapshotEntryV1{};
    std::copy(endpoint_id.begin(), endpoint_id.end(), entry.endpoint_id.begin());
    std::copy(display_name.begin(), display_name.end(), entry.display_name.begin());
    entry.endpoint_id_bytes = static_cast<std::uint16_t>(endpoint_id.size());
    entry.display_name_bytes = static_cast<std::uint16_t>(display_name.size());
    entry.flow = flow;
    entry.availability = availability;
    entry.flags = flags;
    return entry;
}

[[nodiscard]] bool encode_two_entries(
    std::array<std::uint8_t, hibiki::kDeviceCatalogSnapshotPayloadBytesV1>& payload,
    std::size_t& payload_bytes,
    const std::uint64_t catalog_sequence) noexcept
{
    auto entry_render = make_entry(
        "endpoint-render", "Render Speakers", 0U, 0U, 1U);
    auto entry_capture = make_entry(
        "endpoint-capture", "Capture Microphone", 1U, 2U, 0U);
    entry_render.channels = 2U;
    entry_render.sample_rate = 48000U;
    entry_render.buffer_frames = 128U;
    entry_render.last_sequence = 91U;
    entry_capture.channels = 1U;
    entry_capture.sample_rate = 44100U;
    entry_capture.buffer_frames = 256U;
    entry_capture.last_sequence = 17U;

    DeviceCatalogSnapshotEntryV1 entries[2] = {entry_render, entry_capture};
    return hibiki::encode_device_catalog_snapshot_v1(
        entries, catalog_sequence, payload, payload_bytes);
}

}  // namespace

int main()
{
    // ---- publisher accepts an empty catalog as a valid empty snapshot ------
    {
        const auto catalog = PhysicalDeviceCatalogV1{};
        auto payload = std::array<std::uint8_t,
                                  hibiki::kDeviceCatalogSnapshotPayloadBytesV1>{};
        payload.fill(0xa5U);
        auto payload_bytes = std::size_t{999U};
        const DeviceCatalogSnapshotPublisherV1 publisher;
        CHECK(publisher.publish(catalog, 7U, payload, payload_bytes));
        CHECK(payload_bytes == hibiki::kDeviceCatalogSnapshotHeaderBytesV1);
    }

    // ---- a fully loaded catalog publishes; overflow stays in the catalog --
    {
        auto catalog = PhysicalDeviceCatalogV1{};
        for (std::size_t index = 0U;
             index < hibiki::kPhysicalDeviceCatalogCapacityV1; ++index) {
            CHECK(catalog.upsert(make_descriptor(
                      "endpoint-" + std::to_string(index),
                      "Display " + std::to_string(index))) ==
                  hibiki::PhysicalDeviceCatalogResultV1::Accepted);
        }
        CHECK(catalog.size() == hibiki::kPhysicalDeviceCatalogCapacityV1);
        CHECK(catalog.upsert(make_descriptor("endpoint-overflow", "Overflow Device")) ==
              hibiki::PhysicalDeviceCatalogResultV1::CapacityExceeded);
        CHECK(catalog.size() == hibiki::kPhysicalDeviceCatalogCapacityV1);

        auto payload = std::array<std::uint8_t,
                                  hibiki::kDeviceCatalogSnapshotPayloadBytesV1>{};
        auto payload_bytes = std::size_t{999U};
        const DeviceCatalogSnapshotPublisherV1 publisher;
        CHECK(publisher.publish(catalog, 7U, payload, payload_bytes));
        CHECK(payload_bytes ==
              hibiki::kDeviceCatalogSnapshotHeaderBytesV1 +
                  (hibiki::kDeviceCatalogSnapshotEntryBytesV1 *
                   hibiki::kPhysicalDeviceCatalogCapacityV1));
    }

    // ---- catalog rejects oversized endpoint and display strings ----------
    {
        const auto long_endpoint = std::string(
            static_cast<std::size_t>(hibiki::kDeviceSwitchEndpointMaxBytesV1) + 1U, 'e');
        const auto long_display = std::string(129U, 'n');
        auto catalog_endpoint = PhysicalDeviceCatalogV1{};
        auto catalog_display = PhysicalDeviceCatalogV1{};
        CHECK(catalog_endpoint.upsert(make_descriptor(long_endpoint, "Render Speakers")) ==
              hibiki::PhysicalDeviceCatalogResultV1::InvalidDescriptor);
        CHECK(catalog_display.upsert(make_descriptor("endpoint", long_display)) ==
              hibiki::PhysicalDeviceCatalogResultV1::InvalidDescriptor);
    }

    // ---- catalog validates complete printable UTF-8 metadata ------------
    {
        auto catalog = PhysicalDeviceCatalogV1{};
        CHECK(catalog.upsert(make_descriptor(
                  "endpoint-\xE5\xAE\xA2\xE5\xBB\xB3",
                  "\xE5\x96\x87\xE5\x95\xA6 Speakers")) ==
              hibiki::PhysicalDeviceCatalogResultV1::Accepted);
        CHECK(catalog.size() == 1U);
    }
    {
        auto catalog = PhysicalDeviceCatalogV1{};
        CHECK(catalog.upsert(make_descriptor(
                  "endpoint\xC3\x28", "Render Speakers")) ==
              hibiki::PhysicalDeviceCatalogResultV1::InvalidDescriptor);
        CHECK(catalog.size() == 0U);
    }
    {
        auto catalog = PhysicalDeviceCatalogV1{};
        CHECK(catalog.upsert(make_descriptor(
                  "endpoint-display-malformed", "\xE2\x82")) ==
              hibiki::PhysicalDeviceCatalogResultV1::InvalidDescriptor);
        CHECK(catalog.size() == 0U);
    }

    // ---- wire encoding fails closed on oversized declared strings --------
    {
        auto poisoned_endpoint = make_entry(
            "endpoint", "Render Speakers", 0U, 0U, 0U);
        poisoned_endpoint.endpoint_id_bytes =
            hibiki::kDeviceSwitchEndpointMaxBytesV1 + 1U;
        poisoned_endpoint.channels = 2U;
        poisoned_endpoint.sample_rate = 48000U;
        poisoned_endpoint.buffer_frames = 128U;
        auto poisoned_display = make_entry(
            "endpoint", "Render Speakers", 0U, 0U, 0U);
        poisoned_display.display_name_bytes = 129U;
        poisoned_display.channels = 2U;
        poisoned_display.sample_rate = 48000U;
        poisoned_display.buffer_frames = 128U;

        auto payload = std::array<std::uint8_t,
                                  hibiki::kDeviceCatalogSnapshotPayloadBytesV1>{};
        auto payload_bytes = std::size_t{999U};
        const DeviceCatalogSnapshotEntryV1 entries_endpoint[1] = {poisoned_endpoint};
        CHECK(!hibiki::encode_device_catalog_snapshot_v1(
            entries_endpoint, 7U, payload, payload_bytes));
        CHECK(payload_bytes == 0U);

        payload_bytes = std::size_t{999U};
        const DeviceCatalogSnapshotEntryV1 entries_display[1] = {poisoned_display};
        CHECK(!hibiki::encode_device_catalog_snapshot_v1(
            entries_display, 7U, payload, payload_bytes));
        CHECK(payload_bytes == 0U);

        payload_bytes = std::size_t{999U};
        CHECK(!hibiki::encode_device_catalog_snapshot_v1(
            entries_endpoint, 0U, payload, payload_bytes));
        CHECK(payload_bytes == 0U);
    }

    // ---- descriptor fields survive the publisher round-trip ---------------
    {
        auto catalog = PhysicalDeviceCatalogV1{};
        auto render = make_descriptor("endpoint-render", "Render Speakers", 0U);
        render.availability = hibiki::PhysicalDeviceAvailabilityV1::Active;
        render.channels = 2U;
        render.sample_rate = 48000U;
        render.buffer_frames = 128U;
        render.is_default = true;
        render.last_sequence = 91U;
        auto capture = make_descriptor("endpoint-capture", "Capture Microphone", 1U);
        capture.availability = hibiki::PhysicalDeviceAvailabilityV1::Unplugged;
        capture.channels = 1U;
        capture.sample_rate = 44100U;
        capture.buffer_frames = 256U;
        capture.is_default = false;
        capture.last_sequence = 17U;
        CHECK(catalog.upsert(render) == hibiki::PhysicalDeviceCatalogResultV1::Accepted);
        CHECK(catalog.upsert(capture) == hibiki::PhysicalDeviceCatalogResultV1::Accepted);

        auto payload = std::array<std::uint8_t,
                                  hibiki::kDeviceCatalogSnapshotPayloadBytesV1>{};
        auto payload_bytes = std::size_t{0U};
        const DeviceCatalogSnapshotPublisherV1 publisher;
        CHECK(publisher.publish(catalog, 42U, payload, payload_bytes));
        CHECK(payload_bytes ==
              hibiki::kDeviceCatalogSnapshotHeaderBytesV1 +
                  (std::size_t{2U} *
                   hibiki::kDeviceCatalogSnapshotEntryBytesV1));

        auto snapshot = hibiki::DeviceCatalogSnapshotV1{};
        CHECK(hibiki::decode_device_catalog_snapshot_v1(
            {payload.data(), payload_bytes}, snapshot));
        CHECK(snapshot.catalog_sequence == 42U);
        CHECK(snapshot.entry_count == 2U);
        const auto& decoded_render = snapshot.entries[0];
        const auto& decoded_capture = snapshot.entries[1];
        CHECK(std::string_view(decoded_render.endpoint_id.data(),
                               decoded_render.endpoint_id_bytes) == "endpoint-render");
        CHECK(std::string_view(decoded_render.display_name.data(),
                               decoded_render.display_name_bytes) == "Render Speakers");
        CHECK(decoded_render.flow == 0U);
        CHECK(decoded_render.availability == 0U);
        CHECK(decoded_render.flags == 1U);
        CHECK(decoded_render.channels == 2U);
        CHECK(decoded_render.sample_rate == 48000U);
        CHECK(decoded_render.buffer_frames == 128U);
        CHECK(decoded_render.last_sequence == 91U);
        CHECK(std::string_view(decoded_capture.endpoint_id.data(),
                               decoded_capture.endpoint_id_bytes) == "endpoint-capture");
        CHECK(std::string_view(decoded_capture.display_name.data(),
                               decoded_capture.display_name_bytes) == "Capture Microphone");
        CHECK(decoded_capture.flow == 1U);
        CHECK(decoded_capture.availability == 2U);
        CHECK(decoded_capture.flags == 0U);
        CHECK(decoded_capture.channels == 1U);
        CHECK(decoded_capture.sample_rate == 44100U);
        CHECK(decoded_capture.buffer_frames == 256U);
        CHECK(decoded_capture.last_sequence == 17U);

        auto zero_sequence = payload;
        std::fill(zero_sequence.begin() + 4U, zero_sequence.begin() + 12U,
                  std::uint8_t{0U});
        snapshot.entry_count = 2U;
        snapshot.catalog_sequence = 42U;
        CHECK(!hibiki::decode_device_catalog_snapshot_v1(
            {zero_sequence.data(), payload_bytes}, snapshot));
        CHECK(snapshot.entry_count == 0U && snapshot.catalog_sequence == 0U);
    }

    // ---- store enforces a non-zero strictly increasing sequence -----------
    {
        auto payload = std::array<std::uint8_t,
                                  hibiki::kDeviceCatalogSnapshotPayloadBytesV1>{};
        auto payload_bytes = std::size_t{0U};
        CHECK(encode_two_entries(payload, payload_bytes, 42U));

        auto store = DeviceCatalogSnapshotStoreV1{};
        CHECK(store.has_snapshot() == false);
        CHECK(store.sequence() == 0U);
        CHECK(!store.publish({}, 42U));
        CHECK(!store.publish({payload.data(), payload_bytes}, 0U));
        CHECK(store.has_snapshot() == false);
        CHECK(!store.publish({payload.data(), payload_bytes}, 43U));
        CHECK(store.has_snapshot() == false);
        CHECK(store.publish({payload.data(), payload_bytes}, 42U));
        CHECK(store.has_snapshot());
        CHECK(store.sequence() == 42U);
        CHECK(!store.publish({payload.data(), payload_bytes}, 42U));
        CHECK(!store.publish({payload.data(), payload_bytes}, 41U));
        CHECK(store.sequence() == 42U);

        CHECK(encode_two_entries(payload, payload_bytes, 43U));
        CHECK(store.publish({payload.data(), payload_bytes}, 43U));
        CHECK(store.sequence() == 43U);
    }

    // ---- store rejects malformed and sequence-mismatched frames -----------
    {
        auto payload = std::array<std::uint8_t,
                                  hibiki::kDeviceCatalogSnapshotPayloadBytesV1>{};
        auto mismatched = payload;
        auto payload_bytes = std::size_t{0U};
        CHECK(encode_two_entries(payload, payload_bytes, 42U));
        mismatched = payload;
        mismatched[4U] = 0x2aU;  // header sequence now disagrees with argument
        mismatched[5U] = 0x40U;

        auto store = DeviceCatalogSnapshotStoreV1{};
        CHECK(!store.publish(mismatched, 42U));
        CHECK(store.has_snapshot() == false);
        CHECK(store.publish({payload.data(), payload_bytes}, 42U));
        CHECK(store.sequence() == 42U);

        auto oversized = std::array<std::uint8_t,
                                    hibiki::kDeviceCatalogSnapshotPayloadBytesV1>{};
        auto oversized_bytes = std::size_t{0U};
        CHECK(encode_two_entries(oversized, oversized_bytes, 45U));
        oversized[0U] =
            static_cast<std::uint8_t>(hibiki::kDeviceCatalogSnapshotCapacityV1 + 1U);
        CHECK(!store.publish({oversized.data(), oversized_bytes}, 45U));
        CHECK(store.sequence() == 42U);
    }

    // ---- reply is fail-closed before publication and exact after it -------
    {
        auto payload = std::array<std::uint8_t,
                                  hibiki::kDeviceCatalogSnapshotPayloadBytesV1>{};
        auto payload_bytes = std::size_t{0U};
        CHECK(encode_two_entries(payload, payload_bytes, 46U));

        auto empty_store = DeviceCatalogSnapshotStoreV1{};
        auto response = IpcFrameV1{};
        response.header.magic = kIpcMagicV1;
        response.header.payload_bytes = 123U;
        CHECK(!empty_store.reply(response));
        CHECK(response.payload.empty());
        CHECK(response.header.payload_bytes == 0U);

        auto store = DeviceCatalogSnapshotStoreV1{};
        CHECK(store.publish({payload.data(), payload_bytes}, 46U));
        response = IpcFrameV1{};
        CHECK(store.reply(response));
        CHECK(response.header.magic == kIpcMagicV1);
        CHECK(response.header.version == kIpcVersionV1);
        CHECK(response.header.type == IpcMessageType::DeviceCatalogSnapshot);
        CHECK(response.header.payload_bytes == payload_bytes);
        CHECK(response.payload.size() == payload_bytes);
        CHECK(std::equal(payload.begin(),
                         payload.begin() + static_cast<std::ptrdiff_t>(payload_bytes),
                         response.payload.begin()));
    }

    // ---- callback adapter forwards only through a live store context -----
    {
        auto payload = std::array<std::uint8_t,
                                  hibiki::kDeviceCatalogSnapshotPayloadBytesV1>{};
        auto payload_bytes = std::size_t{0U};
        CHECK(encode_two_entries(payload, payload_bytes, 47U));

        auto response = IpcFrameV1{};
        CHECK(!device_catalog_snapshot_reply_v1(response, nullptr));
        auto empty_store = DeviceCatalogSnapshotStoreV1{};
        CHECK(!device_catalog_snapshot_reply_v1(response, &empty_store));
        auto store = DeviceCatalogSnapshotStoreV1{};
        CHECK(store.publish({payload.data(), payload_bytes}, 47U));
        CHECK(device_catalog_snapshot_reply_v1(response, &store));
        CHECK(response.header.type == IpcMessageType::DeviceCatalogSnapshot);
        CHECK(response.header.payload_bytes == payload_bytes);
    }

    // ---- decode rejects nonzero endpoint padding --------------------------
    {
        auto payload = std::array<std::uint8_t,
                                  hibiki::kDeviceCatalogSnapshotPayloadBytesV1>{};
        auto payload_bytes = std::size_t{0U};
        CHECK(encode_two_entries(payload, payload_bytes, 48U));
        auto corrupted = payload;
        corrupted[16U + 8U + 15U] = 1U;
        auto snapshot = hibiki::DeviceCatalogSnapshotV1{};
        CHECK(!decode_device_catalog_snapshot_v1(
            {corrupted.data(), payload_bytes}, snapshot));
    }

    std::fputs("device catalog snapshot tests passed\n", stdout);
    return 0;
}
