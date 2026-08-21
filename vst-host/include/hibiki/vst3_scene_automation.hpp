#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "hibiki/vst3_worker_lane.hpp"

namespace hibiki {

constexpr std::size_t kVst3SceneAutomationMaxEntriesV1 = 16U;
constexpr std::size_t kVst3SceneAutomationMaxIdBytesV1 = 64U;

struct Vst3SceneAutomationBindingV1 {
    std::string scene_id;
    std::uint64_t lane_token{0U};
    std::string timeline_id;
};

enum class Vst3SceneAutomationResultV1 : std::uint8_t {
    ok,
    invalid_argument,
    capacity_exhausted,
    missing_timeline,
    missing_lane,
    scene_not_active,
    not_bound,
    lane_not_ready,
    busy,
    worker_failed,
};

// Control-plane store/scheduler for SceneProfile's stable automation IDs.
// It owns no worker or audio buffer. Scene activation is a validate-all then
// apply operation; block processing has one in-flight request per lane and
// rejects re-entry instead of silently queueing unbounded work.
class Vst3SceneAutomationSchedulerV1 final {
public:
    Vst3SceneAutomationSchedulerV1() noexcept;

    [[nodiscard]] bool prepare(
        std::span<Vst3WorkerLaneSessionV1* const> lanes) noexcept;
    void clear() noexcept;

    [[nodiscard]] bool upsert_timeline(
        std::string_view timeline_id,
        const Vst3ParameterTimelineSnapshotV1& snapshot);
    [[nodiscard]] bool remove_timeline(std::string_view timeline_id) noexcept;
    [[nodiscard]] bool bind_scene(
        std::string_view scene_id,
        std::uint64_t lane_token,
        std::string_view timeline_id);
    [[nodiscard]] Vst3SceneAutomationResultV1 activate_scene(
        std::string_view scene_id);

    [[nodiscard]] Vst3SceneAutomationResultV1 process_lane_block(
        std::string_view scene_id,
        std::uint64_t lane_token,
        std::uint64_t request_id,
        std::uint64_t block_start,
        std::uint32_t frames,
        std::span<const float> input,
        std::span<float> output);

    [[nodiscard]] std::string_view active_scene() const noexcept { return active_scene_; }
    [[nodiscard]] std::size_t lane_count() const noexcept { return lane_count_; }
    [[nodiscard]] std::size_t timeline_count() const noexcept { return timeline_count_; }
    [[nodiscard]] std::size_t binding_count() const noexcept { return binding_count_; }

private:
    struct TimelineSlot {
        bool occupied{false};
        std::string id;
        Vst3ParameterTimelineSnapshotV1 snapshot{};
    };
    struct BindingSlot {
        bool occupied{false};
        Vst3SceneAutomationBindingV1 binding{};
    };

    [[nodiscard]] std::size_t find_lane(std::uint64_t lane_token) const noexcept;
    [[nodiscard]] std::size_t find_timeline(std::string_view timeline_id) const noexcept;
    [[nodiscard]] bool valid_id(std::string_view id) const noexcept;

    std::array<Vst3WorkerLaneSessionV1*, kVst3SceneAutomationMaxEntriesV1> lanes_{};
    std::array<std::atomic_flag, kVst3SceneAutomationMaxEntriesV1> busy_{};
    std::array<TimelineSlot, kVst3SceneAutomationMaxEntriesV1> timelines_{};
    std::array<BindingSlot, kVst3SceneAutomationMaxEntriesV1> bindings_{};
    std::string active_scene_;
    std::size_t lane_count_{0U};
    std::size_t timeline_count_{0U};
    std::size_t binding_count_{0U};
};

}  // namespace hibiki
