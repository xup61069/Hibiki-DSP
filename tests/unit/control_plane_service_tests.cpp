// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/control_payloads.hpp"
#include "hibiki/control_service.hpp"
#include "hibiki/control_status.hpp"
#include "hibiki/device_catalog_snapshot.hpp"
#include "hibiki/ipc.hpp"
#include "hibiki/session_catalog.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::ControlCommandV1;
using hibiki::ControlCommandQueueV1;
using hibiki::ControlPlaneHandlerContextV1;
using hibiki::ControlStatusSnapshotStoreV1;
using hibiki::DeviceCatalogSnapshotEntryV1;
using hibiki::DeviceCatalogSnapshotStoreV1;
using hibiki::EqVisualSnapshotStoreV1;
using hibiki::IpcFrameV1;
using hibiki::IpcMessageType;
using hibiki::SessionCatalogSnapshotStoreV1;
using hibiki::handle_control_frame_v1;
using hibiki::kDeviceCatalogSnapshotPayloadBytesV1;
using hibiki::kIpcMagicV1;
using hibiki::kIpcVersionV1;

template <typename LengthT, std::size_t N>
void set_bounded_text(std::array<char, N>& field, LengthT& length,
                      const std::string_view value)
{
    const auto bounded = value.size() > N ? N : value.size();
    std::copy_n(value.begin(), bounded, field.begin());
    length = static_cast<LengthT>(bounded);
}

[[nodiscard]] IpcFrameV1 make_request(const IpcMessageType type,
                                      const std::uint64_t request_id,
                                      std::vector<std::uint8_t> payload = {})
{
    auto frame = IpcFrameV1{};
    frame.header.magic = kIpcMagicV1;
    frame.header.version = kIpcVersionV1;
    frame.header.type = type;
    frame.header.payload_bytes = static_cast<std::uint32_t>(payload.size());
    frame.header.request_id = request_id;
    frame.payload = std::move(payload);
    return frame;
}

[[nodiscard]] bool expect_error(const IpcFrameV1& response,
                                const std::uint64_t request_id)
{
    return response.header.type == IpcMessageType::Error &&
           response.header.request_id == request_id &&
           response.payload.empty();
}

[[nodiscard]] bool expect_ack(const IpcFrameV1& response,
                              const std::uint64_t request_id)
{
    return response.header.type == IpcMessageType::Ack &&
           response.header.request_id == request_id &&
           response.payload.empty();
}


struct SinkRecord final {
    bool called{false};
    IpcMessageType type{IpcMessageType::Error};
    std::uint64_t request_id{0U};
};

bool recording_sink(const ControlCommandV1& command, void* context) noexcept
{
    auto* record = static_cast<SinkRecord*>(context);
    record->called = true;
    record->type = command.type;
    record->request_id = command.request_id;
    return true;
}

bool failing_sink(const ControlCommandV1& /*command*/, void* /*context*/) noexcept
{
    return false;
}

bool wrong_type_reply(IpcFrameV1& response, void* /*context*/) noexcept
{
    response = {};
    response.header.type = IpcMessageType::Ack;
    response.payload.assign(1U, 0xAAU);
    return true;
}

bool false_reply(IpcFrameV1& /*response*/, void* /*context*/) noexcept
{
    return false;
}

[[nodiscard]] ControlCommandV1 make_hello(const std::uint64_t request_id)
{
    auto command = ControlCommandV1{};
    command.type = IpcMessageType::Hello;
    command.request_id = request_id;
    return command;
}

}  // namespace

int main()
{
    // ---- null context and unusable sinks fail closed ----------------------
    {
        auto response = IpcFrameV1{};
        const auto request = make_request(IpcMessageType::Hello, 900U);
        CHECK(handle_control_frame_v1(request, response, nullptr));
        CHECK(expect_error(response, 900U));

        auto context = ControlPlaneHandlerContextV1{};
        response = {};
        CHECK(handle_control_frame_v1(request, response, &context));
        CHECK(expect_error(response, 900U));

        context.sink = failing_sink;
        response = {};
        CHECK(handle_control_frame_v1(request, response, &context));
        CHECK(expect_error(response, 900U));
    }

    // ---- undecodable command payloads fail closed -------------------------
    {
        auto record = SinkRecord{};
        auto context = ControlPlaneHandlerContextV1{};
        context.sink = recording_sink;
        context.sink_context = &record;
        auto response = IpcFrameV1{};

        const auto graph_prepare = make_request(IpcMessageType::GraphPrepare, 901U);
        CHECK(handle_control_frame_v1(graph_prepare, response, &context));
        CHECK(expect_error(response, 901U));
        CHECK(!record.called);

        const auto hello_junk = make_request(IpcMessageType::Hello, 902U, {0x01U});
        CHECK(handle_control_frame_v1(hello_junk, response, &context));
        CHECK(expect_error(response, 902U));
        CHECK(!record.called);

        const auto scene_bad = make_request(IpcMessageType::SceneApply, 903U, {0x01U});
        CHECK(handle_control_frame_v1(scene_bad, response, &context));
        CHECK(expect_error(response, 903U));
        CHECK(!record.called);
    }

    // ---- truncated device switch payload fails closed ----------------------
    {
        auto record = SinkRecord{};
        auto context = ControlPlaneHandlerContextV1{};
        context.sink = recording_sink;
        context.sink_context = &record;
        auto response = IpcFrameV1{};
        const auto device_full =
            hibiki::encode_device_switch_payload_v1("endpoint-x", 2U, 48000U, 256U, 904U);
        auto truncated = std::vector<std::uint8_t>{};
        std::copy(device_full.begin(), device_full.begin() + 8,
                  std::back_inserter(truncated));
        const auto device_bad =
            make_request(IpcMessageType::DeviceSwitch, 904U, std::move(truncated));
        CHECK(handle_control_frame_v1(device_bad, response, &context));
        CHECK(expect_error(response, 904U));
        CHECK(!record.called);
    }

    // ---- decodable commands reach the sink and return Ack -----------------
    {
        auto record = SinkRecord{};
        auto context = ControlPlaneHandlerContextV1{};
        context.sink = recording_sink;
        context.sink_context = &record;
        auto response = IpcFrameV1{};

        CHECK(handle_control_frame_v1(make_request(IpcMessageType::Hello, 1U),
                                      response, &context));
        CHECK(expect_ack(response, 1U));
        CHECK(record.called);
        CHECK(record.type == IpcMessageType::Hello);
        CHECK(record.request_id == 1U);

        CHECK(handle_control_frame_v1(make_request(IpcMessageType::GraphCommit, 2U),
                                      response, &context));
        CHECK(expect_ack(response, 2U));

        CHECK(handle_control_frame_v1(make_request(IpcMessageType::GraphRollback, 3U),
                                      response, &context));
        CHECK(expect_ack(response, 3U));

        const auto note = hibiki::VolumeNotificationV1{-60.0, false, 21U};
        const auto legacy = hibiki::encode_volume_notification_payload_v1(note);
        auto legacy_payload = std::vector<std::uint8_t>{};
        std::copy(legacy.begin(), legacy.end(), std::back_inserter(legacy_payload));
        CHECK(handle_control_frame_v1(
            make_request(IpcMessageType::VolumeNotification, 4U, std::move(legacy_payload)),
            response, &context));
        CHECK(expect_ack(response, 4U));

        const auto grouped = hibiki::encode_grouped_volume_notification_payload_v1("main", note);
        auto grouped_payload = std::vector<std::uint8_t>{};
        std::copy(grouped.begin(), grouped.end(), std::back_inserter(grouped_payload));
        CHECK(handle_control_frame_v1(
            make_request(IpcMessageType::VolumeNotification, 5U, std::move(grouped_payload)),
            response, &context));
        CHECK(expect_ack(response, 5U));

        auto scene_payload = std::array<std::uint8_t, hibiki::kSceneApplyPayloadBytesV1>{};
        CHECK(hibiki::encode_scene_apply_payload_v1("scene-a", "main", scene_payload));
        auto scene_bytes = std::vector<std::uint8_t>{};
        std::copy(scene_payload.begin(), scene_payload.end(), std::back_inserter(scene_bytes));
        CHECK(handle_control_frame_v1(
            make_request(IpcMessageType::SceneApply, 6U, std::move(scene_bytes)),
            response, &context));
        CHECK(expect_ack(response, 6U));

        auto session_volume_in = hibiki::SessionVolumeCommandV1{};
        session_volume_in.handle = 7U;
        session_volume_in.requested_db_q16_16 = -131072;
        session_volume_in.mute = 0U;
        session_volume_in.catalog_sequence = 8U;
        const auto session_volume = hibiki::encode_session_volume_command_v1(session_volume_in);
        auto session_volume_bytes = std::vector<std::uint8_t>{};
        std::copy(session_volume.begin(), session_volume.end(),
                  std::back_inserter(session_volume_bytes));
        CHECK(handle_control_frame_v1(
            make_request(IpcMessageType::SessionVolumeCommand, 13U,
                         std::move(session_volume_bytes)),
            response, &context));
        CHECK(expect_ack(response, 13U));

        auto session_route_in = hibiki::SessionRouteCommandV1{};
        session_route_in.handle = 9U;
        session_route_in.catalog_sequence = 10U;
        set_bounded_text(session_route_in.lane, session_route_in.lane_bytes, "lane-game");
        set_bounded_text(session_route_in.output_group,
                         session_route_in.output_group_bytes, "main");
        const auto session_route = hibiki::encode_session_route_command_v1(session_route_in);
        auto session_route_bytes = std::vector<std::uint8_t>{};
        std::copy(session_route.begin(), session_route.end(),
                  std::back_inserter(session_route_bytes));
        CHECK(handle_control_frame_v1(
            make_request(IpcMessageType::SessionRouteCommand, 14U,
                         std::move(session_route_bytes)),
            response, &context));
        CHECK(expect_ack(response, 14U));

        auto ir_in = hibiki::IrPrepareCommandV1{};
        ir_in.mode = 1U;
        ir_in.strength_q16_16 = 32768;
        ir_in.expected_sample_rate = 48000U;
        ir_in.expected_channels = 2U;
        set_bounded_text(ir_in.path, ir_in.path_bytes, "ir.wav");
        const auto ir_cmd = hibiki::encode_ir_prepare_command_v1(ir_in);
        auto ir_bytes = std::vector<std::uint8_t>{};
        std::copy(ir_cmd.begin(), ir_cmd.end(), std::back_inserter(ir_bytes));
        CHECK(handle_control_frame_v1(
            make_request(IpcMessageType::IrPrepareCommand, 15U, std::move(ir_bytes)),
            response, &context));
        CHECK(expect_ack(response, 15U));

        const auto device_switch =
            hibiki::encode_device_switch_payload_v1("endpoint-x", 2U, 48000U, 256U, 11U);
        auto device_switch_bytes = std::vector<std::uint8_t>{};
        std::copy(device_switch.begin(), device_switch.end(),
                  std::back_inserter(device_switch_bytes));
        CHECK(handle_control_frame_v1(
            make_request(IpcMessageType::DeviceSwitch, 16U, std::move(device_switch_bytes)),
            response, &context));
        CHECK(expect_ack(response, 16U));

        auto rule_in = hibiki::SessionRouteRuleCommandV1{};
        rule_in.operation = hibiki::SessionRouteRuleOperationV1::Upsert;
        rule_in.enabled = 1U;
        rule_in.gain_owner = hibiki::SessionRouteRuleGainOwnerV1::WindowsSession;
        rule_in.catalog_sequence = 12U;
        set_bounded_text(rule_in.rule_id, rule_in.rule_id_bytes, "rule-a");
        set_bounded_text(rule_in.app_id, rule_in.app_id_bytes, "chrome");
        set_bounded_text(rule_in.lane, rule_in.lane_bytes, "lane-game");
        set_bounded_text(rule_in.output_group,
                         rule_in.output_group_bytes, "main");
        const auto rule_cmd = hibiki::encode_session_route_rule_command_v1(rule_in);
        auto rule_bytes = std::vector<std::uint8_t>{};
        std::copy(rule_cmd.begin(), rule_cmd.end(), std::back_inserter(rule_bytes));
        CHECK(handle_control_frame_v1(
            make_request(IpcMessageType::SessionRouteRuleCommand, 17U, std::move(rule_bytes)),
            response, &context));
        CHECK(expect_ack(response, 17U));
        CHECK(record.type == IpcMessageType::SessionRouteRuleCommand);
        CHECK(record.request_id == 17U);
    }

    // ---- snapshot requests without stores fail closed ---------------------
    {
        auto record = SinkRecord{};
        auto context = ControlPlaneHandlerContextV1{};
        context.sink = recording_sink;
        context.sink_context = &record;
        auto response = IpcFrameV1{};

        CHECK(handle_control_frame_v1(make_request(IpcMessageType::DeviceCatalogRequest, 21U),
                                      response, &context));
        CHECK(expect_error(response, 21U));
        CHECK(record.called);
        CHECK(record.request_id == 21U);

        record.called = false;
        CHECK(handle_control_frame_v1(make_request(IpcMessageType::ControlStatusRequest, 22U),
                                      response, &context));
        CHECK(expect_error(response, 22U));
        CHECK(record.called);

        record.called = false;
        CHECK(handle_control_frame_v1(make_request(IpcMessageType::SessionCatalogRequest, 23U),
                                      response, &context));
        CHECK(expect_error(response, 23U));
        CHECK(record.called);

        record.called = false;
        CHECK(handle_control_frame_v1(
            make_request(IpcMessageType::EqVisualSnapshotRequest, 24U), response, &context));
        CHECK(expect_error(response, 24U));
        CHECK(record.called);
    }

    // ---- reply callback failure falls back to Error -----------------------
    {
        auto record = SinkRecord{};
        auto context = ControlPlaneHandlerContextV1{};
        context.sink = recording_sink;
        context.sink_context = &record;
        context.snapshot_reply = false_reply;
        auto response = IpcFrameV1{};
        CHECK(handle_control_frame_v1(make_request(IpcMessageType::DeviceCatalogRequest, 25U),
                                      response, &context));
        CHECK(expect_error(response, 25U));
    }

    // ---- mismatched reply frame type falls back to Error ------------------
    {
        auto record = SinkRecord{};
        auto context = ControlPlaneHandlerContextV1{};
        context.sink = recording_sink;
        context.sink_context = &record;
        context.snapshot_reply = wrong_type_reply;
        context.status_reply = wrong_type_reply;
        context.session_catalog_reply = wrong_type_reply;
        context.eq_visual_reply = wrong_type_reply;
        auto response = IpcFrameV1{};

        CHECK(handle_control_frame_v1(make_request(IpcMessageType::DeviceCatalogRequest, 26U),
                                      response, &context));
        CHECK(expect_error(response, 26U));
        CHECK(handle_control_frame_v1(make_request(IpcMessageType::ControlStatusRequest, 27U),
                                      response, &context));
        CHECK(expect_error(response, 27U));
        CHECK(handle_control_frame_v1(make_request(IpcMessageType::SessionCatalogRequest, 28U),
                                      response, &context));
        CHECK(expect_error(response, 28U));
        CHECK(handle_control_frame_v1(
            make_request(IpcMessageType::EqVisualSnapshotRequest, 29U), response, &context));
        CHECK(expect_error(response, 29U));
    }

    // ---- published stores answer with rebound request ids -----------------
    {
        auto entry = DeviceCatalogSnapshotEntryV1{};
        set_bounded_text(entry.endpoint_id, entry.endpoint_id_bytes,
                         "{00000000-0000-0000-0000-000000000001}");
        set_bounded_text(entry.display_name, entry.display_name_bytes, "Speakers");
        entry.flow = 0U;
        entry.availability = 1U;
        entry.channels = 2U;
        entry.sample_rate = 48000U;
        entry.buffer_frames = 256U;
        const std::span<const DeviceCatalogSnapshotEntryV1> entries(&entry, 1U);
        auto catalog_payload =
            std::array<std::uint8_t, kDeviceCatalogSnapshotPayloadBytesV1>{};
        auto catalog_bytes = std::size_t{0U};
        CHECK(hibiki::encode_device_catalog_snapshot_v1(entries, 31U, catalog_payload,
                                                        catalog_bytes));

        auto device_store = DeviceCatalogSnapshotStoreV1{};
        CHECK(device_store.publish({catalog_payload.data(), catalog_bytes}, 31U));

        auto status_store = ControlStatusSnapshotStoreV1{};
        auto status_snapshot = hibiki::ControlStatusSnapshotV1{};
        status_snapshot.sequence = 32U;
        CHECK(status_store.publish(status_snapshot));

        auto session_store = SessionCatalogSnapshotStoreV1{};
        auto session_snapshot = hibiki::SessionCatalogSnapshotV1{};
        session_snapshot.sequence = 33U;
        session_snapshot.generation = 1U;
        CHECK(session_store.publish(session_snapshot));

        auto eq_store = EqVisualSnapshotStoreV1{};
        auto eq_snapshot = hibiki::EqVisualSnapshotV1{};
        eq_snapshot.sequence = 34U;
        eq_snapshot.source = 1U;
        constexpr double frequencies[] = {20.0, 200.0, 2000.0, 12000.0};
        constexpr double gains[] = {0.0, -3.0, 1.5, -6.0};
        for (std::size_t index = 0U; index < 4U; ++index) {
            eq_snapshot.points[index].frequency_hz = frequencies[index];
            eq_snapshot.points[index].gain_db = gains[index];
        }
        CHECK(eq_store.publish(eq_snapshot));

        auto record = SinkRecord{};
        auto context = ControlPlaneHandlerContextV1{};
        context.sink = recording_sink;
        context.sink_context = &record;
        context.snapshot_reply = hibiki::device_catalog_snapshot_reply_v1;
        context.snapshot_context = &device_store;
        context.status_reply = hibiki::control_status_snapshot_reply_v1;
        context.status_context = &status_store;
        context.session_catalog_reply = hibiki::session_catalog_snapshot_reply_v1;
        context.session_catalog_context = &session_store;
        context.eq_visual_reply = hibiki::eq_visual_snapshot_reply_v1;
        context.eq_visual_context = &eq_store;
        auto response = IpcFrameV1{};

        CHECK(handle_control_frame_v1(make_request(IpcMessageType::DeviceCatalogRequest, 35U),
                                      response, &context));
        CHECK(response.header.type == IpcMessageType::DeviceCatalogSnapshot);
        CHECK(response.header.request_id == 35U);
        CHECK(!response.payload.empty());

        CHECK(handle_control_frame_v1(make_request(IpcMessageType::ControlStatusRequest, 36U),
                                      response, &context));
        CHECK(response.header.type == IpcMessageType::ControlStatusSnapshot);
        CHECK(response.header.request_id == 36U);
        CHECK(!response.payload.empty());

        CHECK(handle_control_frame_v1(make_request(IpcMessageType::SessionCatalogRequest, 37U),
                                      response, &context));
        CHECK(response.header.type == IpcMessageType::SessionCatalogSnapshot);
        CHECK(response.header.request_id == 37U);
        CHECK(!response.payload.empty());

        CHECK(handle_control_frame_v1(
            make_request(IpcMessageType::EqVisualSnapshotRequest, 38U), response, &context));
        CHECK(response.header.type == IpcMessageType::EqVisualSnapshot);
        CHECK(response.header.request_id == 38U);
        CHECK(!response.payload.empty());
    }

    // ---- queue enforces capacity, drop accounting and FIFO across wrap ----
    {
        auto queue = ControlCommandQueueV1{};
        CHECK(queue.dropped() == 0U);
        CHECK(!hibiki::enqueue_control_command_v1(make_hello(0U), nullptr));

        for (std::uint64_t index = 1U; index <= 64U; ++index) {
            CHECK(hibiki::enqueue_control_command_v1(make_hello(index), &queue));
        }
        CHECK(!queue.try_push(make_hello(65U)));
        CHECK(queue.dropped() == 1U);

        auto popped = ControlCommandV1{};
        for (std::uint64_t index = 1U; index <= 64U; ++index) {
            CHECK(queue.try_pop(popped));
            CHECK(popped.type == IpcMessageType::Hello);
            CHECK(popped.request_id == index);
        }
        CHECK(!queue.try_pop(popped));

        for (std::uint64_t index = 65U; index <= 104U; ++index) {
            CHECK(queue.try_push(make_hello(index)));
        }
        for (std::uint64_t index = 65U; index <= 89U; ++index) {
            CHECK(queue.try_pop(popped));
            CHECK(popped.request_id == index);
        }
        for (std::uint64_t index = 105U; index <= 124U; ++index) {
            CHECK(queue.try_push(make_hello(index)));
        }
        CHECK(queue.dropped() == 1U);
        for (std::uint64_t index = 90U; index <= 124U; ++index) {
            CHECK(queue.try_pop(popped));
            CHECK(popped.request_id == index);
        }
        CHECK(!queue.try_pop(popped));
    }

    // ---- handler routes decoded commands into the queue -------------------
    {
        auto queue = ControlCommandQueueV1{};
        auto context = ControlPlaneHandlerContextV1{};
        context.sink = hibiki::enqueue_control_command_v1;
        context.sink_context = &queue;
        auto response = IpcFrameV1{};
        CHECK(handle_control_frame_v1(make_request(IpcMessageType::Hello, 41U),
                                      response, &context));
        CHECK(expect_ack(response, 41U));
        auto popped = ControlCommandV1{};
        CHECK(queue.try_pop(popped));
        CHECK(popped.request_id == 41U);
    }

    // ---- host start rejects invalid configuration without a live pipe -----
    {
        auto host = hibiki::ControlPlaneHostV1{};
        auto config = hibiki::IpcNamedPipeConfigV1{};
        config.pipe_name = L"";
        config.io_timeout_ms = 50U;
        CHECK(!host.start_with_queue(config));
        CHECK(!host.running());
        CHECK(!host.start(config, nullptr, nullptr));
        CHECK(!host.running());
    }

    std::fputs("control plane service tests passed\n", stdout);
    return 0;
}
