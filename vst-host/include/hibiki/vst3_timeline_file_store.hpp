#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_parameter_timeline.hpp"
#include "hibiki/vst3_scene_automation.hpp"

#include <span>
#include <string>
#include <string_view>

namespace hibiki {

constexpr std::size_t kVst3TimelineStoreMaxEntriesV1 = 16U;
constexpr std::size_t kVst3TimelineStoreMaxIdBytesV1 = 64U;

enum class Vst3TimelineStoreStatusV1 : std::uint8_t {
    ok,
    invalid_argument,
    id_rejected,
    capacity_exhausted,
    not_found,
    io_error,
    parse_error,
};

[[nodiscard]] std::string_view to_string(Vst3TimelineStoreStatusV1 status) noexcept;

// Bounded per-timeline file store over the canonical vst3-parameter-timeline-v1
// document format: one "<id>.json" per timeline inside a single caller-chosen
// directory. Timeline IDs are restricted to a filename-safe subset so the
// ID-to-filename mapping stays bijective without any escaping layer. The store
// is a single-writer control-plane object; concurrent external writers are out
// of scope. Nothing here runs on the RT thread.
class Vst3TimelineFileStoreV1 final {
public:
    // Opens (creating if absent) the root directory. Fails closed when the
    // path is empty or exists as a non-directory.
    [[nodiscard]] bool open(const std::wstring& root) noexcept;
    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept { return !root_.empty(); }

    [[nodiscard]] Vst3TimelineStoreStatusV1 save(
        std::string_view id,
        const Vst3ParameterTimelineSnapshotV1& snapshot);
    [[nodiscard]] Vst3TimelineStoreStatusV1 load(
        std::string_view id,
        Vst3ParameterTimelineSnapshotV1& destination) const;
    [[nodiscard]] Vst3TimelineStoreStatusV1 remove(std::string_view id);

    // Deterministic bounded listing of currently stored IDs (sorted, without
    // the ".json" suffix). Returns false with io_error when the directory
    // cannot be scanned; destination is untouched on failure.
    [[nodiscard]] Vst3TimelineStoreStatusV1 list_ids(
        std::span<std::string> destination,
        std::size_t& count) const;

private:
    [[nodiscard]] static bool valid_id(std::string_view id) noexcept;

    std::wstring root_;
};

struct Vst3TimelineStoreSyncResultV1 {
    std::size_t loaded{0U};
    std::size_t skipped{0U};
};

// Loads every stored timeline from the store in its deterministic sorted order
// and publishes each validator-passing snapshot into the scheduler through
// upsert_timeline. A single failing entry (unreadable file, invalid document or
// a scheduler-side rejection such as a full catalog) is counted as skipped and
// never aborts the run nor disturbs previously stored entries. The result is
// zeroed before any work; an early failure leaves it zeroed.
[[nodiscard]] Vst3TimelineStoreStatusV1 sync_timeline_store_to_scheduler_v1(
    const Vst3TimelineFileStoreV1& store,
    Vst3SceneAutomationSchedulerV1& scheduler,
    Vst3TimelineStoreSyncResultV1& result);

}  // namespace hibiki
