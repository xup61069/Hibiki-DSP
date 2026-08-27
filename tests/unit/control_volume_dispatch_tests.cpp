// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/control_service.hpp"
#include "hibiki/engine_control.hpp"

#if defined(_WIN32)
#include "hibiki/windows_volume_broker.hpp"
#include "hibiki/windows_volume_link.hpp"
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using namespace hibiki;

ControlCommandV1 make_volume_command(const double requested_db,
                                     const bool mute = false,
                                     const std::uint64_t generation = 1U) noexcept {
    ControlCommandV1 command{};
    command.type = IpcMessageType::VolumeNotification;
    command.volume = VolumeNotificationV1{requested_db, mute, generation};
    return command;
}

ControlCommandV1 make_targeted_volume_command(const char* output_group,
                                              const double requested_db,
                                              const std::uint64_t generation = 1U) noexcept {
    ControlCommandV1 command = make_volume_command(requested_db, false, generation);
    command.has_volume_target = true;
    const auto output_group_bytes = std::char_traits<char>::length(output_group);
    command.volume_target.output_group_bytes =
        static_cast<std::uint8_t>(output_group_bytes);
    std::copy_n(output_group,
                output_group_bytes,
                command.volume_target.output_group.data());
    return command;
}

int test_engine_control_worker_volume_dispatch() {
    AudioEngineModel engine;
    EngineControlWorkerV1 worker(engine);

    CHECK(worker.consume(make_volume_command(-6.0)) ==
          EngineControlResultV1::Applied);
    CHECK(engine.volume_state("main").requested_db == -6.0);

    CHECK(worker.consume(make_volume_command(13.0)) ==
          EngineControlResultV1::Invalid);
    CHECK(worker.consume(make_volume_command(
              std::numeric_limits<double>::quiet_NaN())) ==
          EngineControlResultV1::Invalid);
    CHECK(worker.consume(make_targeted_volume_command("missing", -6.0)) ==
          EngineControlResultV1::Invalid);

    return 0;
}

#if defined(_WIN32)

WindowsVolumeNotificationSnapshotV1 make_snapshot(
    const double requested_db,
    const std::uint64_t generation) noexcept {
    WindowsVolumeNotificationSnapshotV1 snapshot{};
    snapshot.requested_db = requested_db;
    snapshot.generation = generation;
    return snapshot;
}

int test_windows_volume_link_validation() {
    AudioEngineModel engine;
    WindowsVolumeLinkV1 link;

    CHECK(link.apply(engine, "main", make_snapshot(-18.0, 1U)) ==
          WindowsVolumeSyncResultV1::Applied);
    CHECK(link.apply(engine, "main", make_snapshot(-18.0, 0U)) ==
          WindowsVolumeSyncResultV1::Invalid);
    CHECK(link.apply(engine,
                     "main",
                     make_snapshot(std::numeric_limits<double>::quiet_NaN(), 2U)) ==
          WindowsVolumeSyncResultV1::Invalid);
    CHECK(link.apply(engine, "main", make_snapshot(-145.0, 2U)) ==
          WindowsVolumeSyncResultV1::Invalid);
    CHECK(link.apply(engine, "main", make_snapshot(13.0, 2U)) ==
          WindowsVolumeSyncResultV1::Invalid);
    CHECK(link.apply(engine, "missing", make_snapshot(-18.0, 2U)) ==
          WindowsVolumeSyncResultV1::Invalid);

    return 0;
}

#endif  // defined(_WIN32)

int test_control_command_queue_fifo_and_capacity() {
    ControlCommandQueueV1 queue;
    ControlCommandV1 command{};
    command.type = IpcMessageType::Hello;

    constexpr std::uint64_t kFirstRequestId = 11U;
    for (std::uint64_t offset = 0U; offset < 3U; ++offset) {
        command.request_id = kFirstRequestId + offset;
        CHECK(queue.try_push(command));
    }

    for (std::uint64_t offset = 0U; offset < 3U; ++offset) {
        ControlCommandV1 received{};
        CHECK(queue.try_pop(received));
        CHECK(received.request_id == kFirstRequestId + offset);
    }
    CHECK(!queue.try_pop(command));

    command.request_id = 100U;
    for (std::size_t index = 0U; index < ControlCommandQueueV1::kCapacity; ++index) {
        CHECK(queue.try_push(command));
    }
    CHECK(!queue.try_push(command));
    CHECK(queue.dropped() == 1U);
    CHECK(!queue.try_push(command));
    CHECK(queue.dropped() == 2U);

    return 0;
}

}  // namespace

int main() {
    CHECK(test_engine_control_worker_volume_dispatch() == 0);
#if defined(_WIN32)
    CHECK(test_windows_volume_link_validation() == 0);
#endif
    CHECK(test_control_command_queue_fifo_and_capacity() == 0);
    std::fputs("control volume dispatch tests passed\n", stdout);
    return 0;
}
