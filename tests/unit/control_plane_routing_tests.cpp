// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/control_payloads.hpp"
#include "hibiki/control_service.hpp"
#include "hibiki/control_status.hpp"
#include "hibiki/device_catalog_snapshot.hpp"
#include "hibiki/ipc.hpp"
#include "hibiki/session_catalog.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::ControlCommandQueueV1;
using hibiki::ControlCommandV1;
using hibiki::decode_control_command_v1;
using hibiki::ControlPlaneHandlerContextV1;
using hibiki::ControlStatusSnapshotStoreV1;
using hibiki::ControlStatusSnapshotV1;
using hibiki::DeviceCatalogSnapshotStoreV1;
using hibiki::EqVisualSnapshotStoreV1;
using hibiki::EqVisualSnapshotV1;
using hibiki::handle_control_frame_v1;
using hibiki::enqueue_control_command_v1;
using hibiki::IpcFrameV1;
using hibiki::IpcMessageType;
using hibiki::SessionCatalogRouteStateV1;
using hibiki::SessionCatalogSnapshotStoreV1;
using hibiki::SessionCatalogSnapshotV1;

IpcFrameV1 make_request(const IpcMessageType type, const std::uint64_t request_id) {
    IpcFrameV1 frame;
    frame.header.type = type;
    frame.header.request_id = request_id;
    return frame;
}

bool sink_accept(const ControlCommandV1& command, void* const context) noexcept {
    (void)command;
    if (context != nullptr) {
        *static_cast<bool*>(context) = true;
    }
    return true;
}

bool sink_reject(const ControlCommandV1& command, void* const context) noexcept {
    (void)command;
    (void)context;
    return false;
}

bool publish_status(ControlStatusSnapshotStoreV1& store) {
    ControlStatusSnapshotV1 snapshot{};
    snapshot.sequence = 3U;
    snapshot.volume.requested_db = -6.0;
    snapshot.volume.safety_ceiling_db = -12.0;
    snapshot.volume.effective_db = -12.0;
    snapshot.volume.generation = 4U;
    return store.publish(snapshot);
}

bool publish_eq_visual(EqVisualSnapshotStoreV1& store) {
    EqVisualSnapshotV1 visual{};
    visual.sequence = 7U;
    visual.source = 1U;
    visual.points[0] = {31.0, -4.0};
    visual.points[1] = {120.0, 2.0};
    visual.points[2] = {1000.0, 0.0};
    visual.points[3] = {8000.0, 1.5};
    return store.publish(visual);
}

bool publish_session_catalog(SessionCatalogSnapshotStoreV1& store,
                             std::size_t& published_bytes) {
    SessionCatalogSnapshotV1 snapshot{};
    snapshot.sequence = 12U;
    snapshot.generation = 1U;
    snapshot.entry_count = 1U;
    auto& entry = snapshot.entries[0];
    entry.handle = (1ULL << 32U) | 1ULL;
    entry.active = 1U;
    entry.route_state = SessionCatalogRouteStateV1::Ready;
    entry.flags = 1U;
    entry.requested_db_q16_16 = -622592;
    const std::string_view name{"DJMAX"};
    const std::string_view app{"game.exe"};
    const std::string_view lane{"game"};
    const std::string_view output{"main"};
    entry.name_bytes = static_cast<std::uint16_t>(name.size());
    entry.app_bytes = static_cast<std::uint16_t>(app.size());
    entry.lane_bytes = static_cast<std::uint16_t>(lane.size());
    entry.output_bytes = static_cast<std::uint16_t>(output.size());
    std::memcpy(entry.name.data(), name.data(), name.size());
    std::memcpy(entry.app.data(), app.data(), app.size());
    std::memcpy(entry.lane.data(), lane.data(), lane.size());
    std::memcpy(entry.output.data(), output.data(), output.size());
    const bool published = store.publish(snapshot);
    published_bytes = hibiki::kSessionCatalogSnapshotHeaderBytesV1 +
                      hibiki::kSessionCatalogSnapshotEntryBytesV1;
    return published;
}

}  // namespace

int main() {
    // ---- queue: FIFO across distinct commands --------------------------------
    {
        ControlCommandQueueV1 queue;
        ControlCommandV1 first{};
        first.type = IpcMessageType::Hello;
        first.request_id = 11U;
        ControlCommandV1 second{};
        second.type = IpcMessageType::DeviceSwitch;
        second.request_id = 22U;
        CHECK(queue.try_push(first));
        CHECK(queue.try_push(second));
        ControlCommandV1 popped{};
        CHECK(queue.try_pop(popped) && popped.request_id == 11U &&
              popped.type == IpcMessageType::Hello);
        CHECK(queue.try_pop(popped) && popped.request_id == 22U &&
              popped.type == IpcMessageType::DeviceSwitch);
        CHECK(!queue.try_pop(popped));
        CHECK(queue.dropped() == 0U);
    }

    // ---- queue: overflow drop accounting and ring wraparound -----------------
    {
        ControlCommandQueueV1 queue;
        for (std::size_t index = 0U; index < ControlCommandQueueV1::kCapacity; ++index) {
            ControlCommandV1 command{};
            command.type = IpcMessageType::Hello;
            command.request_id = static_cast<std::uint64_t>(index);
            CHECK(queue.try_push(command));
        }
        ControlCommandV1 overflow{};
        overflow.type = IpcMessageType::Hello;
        overflow.request_id = 9999U;
        CHECK(!queue.try_push(overflow) && queue.dropped() == 1U);
        // Drain two slots then push again: the ring must accept new commands at
        // the wrapped head position without losing order of the remainder.
        ControlCommandV1 popped{};
        CHECK(queue.try_pop(popped) && popped.request_id == 0U);
        CHECK(queue.try_pop(popped) && popped.request_id == 1U);
        CHECK(queue.try_pop(popped) && popped.request_id == 2U);
        ControlCommandV1 wrapped{};
        wrapped.request_id = 10000U;
        CHECK(queue.try_push(wrapped));
        // The wrapped entry sits behind every surviving original command.
        for (std::uint64_t id = 3U; id < ControlCommandQueueV1::kCapacity; ++id) {
            CHECK(queue.try_pop(popped) && popped.request_id == id);
        }
        CHECK(queue.try_pop(popped) && popped.request_id == 10000U);
        CHECK(!queue.try_pop(popped));
    }

    // ---- queue adapter: null context fails closed ----------------------------
    {
        ControlCommandV1 command{};
        command.type = IpcMessageType::Hello;
        CHECK(!enqueue_control_command_v1(command, nullptr));
        ControlCommandQueueV1 queue;
        CHECK(enqueue_control_command_v1(command, &queue));
    }

    // ---- router: ack path, request_id preserved ------------------------------
    bool command_accepted = false;
    ControlPlaneHandlerContextV1 ack_context{sink_accept, &command_accepted};
    auto scene_frame = make_request(IpcMessageType::SceneApply, 41U);
    std::array<std::uint8_t, hibiki::kSceneApplyPayloadBytesV1> scene_payload{};
    CHECK(hibiki::encode_scene_apply_payload_v1("game", "main", scene_payload));
    scene_frame.payload.assign(scene_payload.begin(), scene_payload.end());
    ControlCommandV1 decoded_probe{};
    CHECK(decode_control_command_v1(scene_frame, decoded_probe) &&
          decoded_probe.type == IpcMessageType::SceneApply &&
          decoded_probe.request_id == 41U);
    IpcFrameV1 response;
    CHECK(handle_control_frame_v1(scene_frame, response, &ack_context) &&
          command_accepted && response.header.type == IpcMessageType::Ack &&
          response.header.request_id == 41U);

    // ---- router: null context and null sink fail closed ----------------------
    {
        const auto probe = make_request(IpcMessageType::Hello, 42U);
        IpcFrameV1 error_response;
        CHECK(handle_control_frame_v1(probe, error_response, nullptr) &&
              error_response.header.type == IpcMessageType::Error &&
              error_response.header.request_id == 42U);
        bool rejected = false;
        ControlPlaneHandlerContextV1 reject_context{sink_reject, &rejected};
        CHECK(handle_control_frame_v1(probe, error_response, &reject_context) &&
              !rejected && error_response.header.type == IpcMessageType::Error &&
              error_response.header.request_id == 42U);
    }

    // ---- router: decode failure returns Error -------------------------------
    {
        // SceneApply requires a valid payload; an empty payload must decode-fail.
        const auto malformed = make_request(IpcMessageType::SceneApply, 43U);
        bool sink_called = false;
        ControlPlaneHandlerContextV1 decode_fail_context{sink_accept, &sink_called};
        IpcFrameV1 error_response;
        CHECK(handle_control_frame_v1(malformed, error_response, &decode_fail_context) &&
              !sink_called && error_response.header.type == IpcMessageType::Error &&
              error_response.header.request_id == 43U);
    }

    // ---- router: catalog request success and missing-store fallback ----------
    {
        DeviceCatalogSnapshotStoreV1 store;
        // Publish a minimal one-entry snapshot through the publisher path used by
        // contract tests; the store itself only needs any valid snapshot here.
        hibiki::DeviceCatalogSnapshotEntryV1 entry{};
        const std::string_view endpoint{"render"};
        const std::string_view display{"Render device"};
        entry.endpoint_id_bytes = static_cast<std::uint16_t>(endpoint.size());
        entry.display_name_bytes = static_cast<std::uint16_t>(display.size());
        std::memcpy(entry.endpoint_id.data(), endpoint.data(), endpoint.size());
        std::memcpy(entry.display_name.data(), display.data(), display.size());
        entry.flow = 0U;
        entry.availability = 0U;
        entry.flags = 1U;
        entry.channels = 2U;
        entry.sample_rate = 48000U;
        entry.buffer_frames = 128U;
        entry.last_sequence = 21U;
        std::array<std::uint8_t, hibiki::kDeviceCatalogSnapshotPayloadBytesV1> payload{};
        std::size_t payload_bytes = 0U;
        CHECK(hibiki::encode_device_catalog_snapshot_v1(
                  std::span<const hibiki::DeviceCatalogSnapshotEntryV1>(&entry, 1U),
                  21U, payload, payload_bytes));
        CHECK(store.publish(std::span<const std::uint8_t>(payload.data(), payload_bytes), 21U));
        bool accepted = false;
        ControlPlaneHandlerContextV1 catalog_context{
            sink_accept, &accepted,
            hibiki::device_catalog_snapshot_reply_v1, &store};
        const auto catalog_request = make_request(IpcMessageType::DeviceCatalogRequest, 51U);
        CHECK(handle_control_frame_v1(catalog_request, response, &catalog_context) &&
              accepted &&
              response.header.type == IpcMessageType::DeviceCatalogSnapshot &&
              response.header.request_id == 51U && response.payload.size() == payload_bytes);

        ControlPlaneHandlerContextV1 no_store_context{sink_accept, &accepted,
                                                      nullptr, nullptr};
        CHECK(handle_control_frame_v1(catalog_request, response, &no_store_context) &&
              response.header.type == IpcMessageType::Error &&
              response.header.request_id == 51U);
    }

    // ---- router: status request success and missing-store fallback -----------
    {
        ControlStatusSnapshotStoreV1 store;
        bool accepted = false;

        // Missing store first: request must fail closed with Error.
        ControlPlaneHandlerContextV1 no_status_context{sink_accept, &accepted,
                                                       nullptr, nullptr,
                                                       nullptr, nullptr};
        const auto status_request = make_request(IpcMessageType::ControlStatusRequest, 61U);
        CHECK(handle_control_frame_v1(status_request, response, &no_status_context) &&
              response.header.type == IpcMessageType::Error &&
              response.header.request_id == 61U);

        publish_status(store);
        std::size_t expected_bytes = hibiki::kControlStatusSnapshotHeaderBytesV1;
        ControlPlaneHandlerContextV1 status_context{
            sink_accept, &accepted, nullptr, nullptr,
            hibiki::control_status_snapshot_reply_v1, &store};
        CHECK(handle_control_frame_v1(status_request, response, &status_context) &&
              accepted &&
              response.header.type == IpcMessageType::ControlStatusSnapshot &&
              response.header.request_id == 61U && !response.payload.empty());
        (void)expected_bytes;
    }

    // ---- router: session catalog request success ----------------------------
    {
        SessionCatalogSnapshotStoreV1 store;
        std::size_t published_bytes = 0U;
        publish_session_catalog(store, published_bytes);
        bool accepted = false;
        ControlPlaneHandlerContextV1 session_context{
            sink_accept, &accepted, nullptr, nullptr, nullptr, nullptr,
            hibiki::session_catalog_snapshot_reply_v1, &store};
        const auto session_request = make_request(IpcMessageType::SessionCatalogRequest, 62U);
        CHECK(handle_control_frame_v1(session_request, response, &session_context) &&
              accepted &&
              response.header.type == IpcMessageType::SessionCatalogSnapshot &&
              response.header.request_id == 62U &&
              response.payload.size() == published_bytes);
    }

    // ---- router: eq visual request success and missing-store fallback -------
    {
        EqVisualSnapshotStoreV1 store;
        bool accepted = false;

        // Missing store first.
        ControlPlaneHandlerContextV1 no_eq_context{
            sink_accept, &accepted, nullptr, nullptr, nullptr, nullptr,
            nullptr, nullptr, nullptr, nullptr};
        const auto eq_request = make_request(IpcMessageType::EqVisualSnapshotRequest, 63U);
        CHECK(handle_control_frame_v1(eq_request, response, &no_eq_context) &&
              response.header.type == IpcMessageType::Error &&
              response.header.request_id == 63U);

        publish_eq_visual(store);
        std::size_t expected_bytes =
            hibiki::kEqVisualSnapshotHeaderBytesV1 + 4U * hibiki::kEqVisualSnapshotPointBytesV1;
        ControlPlaneHandlerContextV1 eq_context{
            sink_accept, &accepted, nullptr, nullptr, nullptr, nullptr,
            nullptr, nullptr, hibiki::eq_visual_snapshot_reply_v1, &store};
        CHECK(handle_control_frame_v1(eq_request, response, &eq_context) && accepted &&
              response.header.type == IpcMessageType::EqVisualSnapshot &&
              response.header.request_id == 63U &&
              response.payload.size() == expected_bytes);
    }

    return 0;
}
