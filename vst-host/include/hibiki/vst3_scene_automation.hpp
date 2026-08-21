#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "hibiki/vst3_timeline_editor.hpp"
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

// Immutable read-only copy of one stored Scene binding. Views snapshot at call
// time; they are not handles that track later mutations.
struct Vst3SceneAutomationBindingViewV1 {
    std::string scene_id;
    std::uint64_t lane_token{0U};
    std::string timeline_id;
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

    // Bounded edit transaction against one stored timeline slot. The control
    // plane is single-writer: no locking is added and the editor must be used
    // from the same thread that owns the scheduler. Edits target existing
    // stored timelines only; new timelines are created via upsert_timeline.
    [[nodiscard]] bool begin_timeline_edit(std::string_view timeline_id);
    [[nodiscard]] Vst3TimelineEditorV1* editing_timeline() noexcept {
        return edit_active_ ? &edit_editor_ : nullptr;
    }
    [[nodiscard]] bool commit_timeline_edit();
    [[nodiscard]] bool cancel_timeline_edit() noexcept;

    // Read-only view of one stored timeline snapshot; null when unknown.
    [[nodiscard]] const Vst3ParameterTimelineSnapshotV1* timeline_snapshot(
        std::string_view timeline_id) const noexcept;

    // Bounded introspection: sorted stored timeline IDs and immutable binding
    // views copied into caller-owned storage. An undersized destination fails
    // closed with count zeroed instead of producing partial output.
    [[nodiscard]] bool timeline_ids(
        std::span<std::string> destination,
        std::size_t& count) const;
    [[nodiscard]] bool binding_views(
        std::span<Vst3SceneAutomationBindingViewV1> destination,
        std::size_t& count) const;
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
    Vst3TimelineEditorV1 edit_editor_{};
    std::string active_scene_;
    std::size_t lane_count_{0U};
    std::size_t timeline_count_{0U};
    std::size_t binding_count_{0U};
    // kVst3SceneAutomationMaxEntriesV1 doubles as the "no edit slot" sentinel.
    std::size_t edit_slot_{kVst3SceneAutomationMaxEntriesV1};
    bool edit_active_{false};
};

}  // namespace hibiki
