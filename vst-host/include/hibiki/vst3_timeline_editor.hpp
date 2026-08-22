#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_parameter_timeline.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace hibiki {

constexpr std::size_t kVst3TimelineEditorMaxHistoryV1 = 8U;

// Supervisor-side bounded editing transaction for parameter timelines. The
// editor owns no worker, audio buffer or file handle; it only moves validated
// snapshots between a published state and a private draft copy on the control
// plane. Commit publishes only snapshots accepted by the existing timeline
// validator, so an invalid draft can never reach Scene automation. A bounded
// undo/redo history of published snapshots gives a future supervisor UI
// standard editing safety using fixed storage only.
class Vst3TimelineEditorV1 final {
public:
    Vst3TimelineEditorV1() noexcept = default;

    // Adopt an externally produced snapshot as the new published baseline.
    // Refused while an edit session is active and for snapshots that fail the
    // shared timeline contract; the previous baseline is preserved verbatim.
    // Clears both history stacks: a new baseline invalidates prior history.
    [[nodiscard]] bool reset(const Vst3ParameterTimelineSnapshotV1& published) noexcept;

    [[nodiscard]] bool has_edit_session() const noexcept { return draft_active_; }

    // Start one editing session from a verified copy of the published
    // snapshot. A second begin_edit fails closed instead of resetting the
    // draft silently.
    [[nodiscard]] bool begin_edit() noexcept;

    // Drop the draft and keep the published snapshot unchanged.
    [[nodiscard]] bool discard() noexcept;

    // Publish the draft only after it passes the shared timeline contract.
    // On failure the draft is kept so the caller can repair or discard it.
    [[nodiscard]] bool commit() noexcept;

    // Insert or replace one event. At most one value survives per
    // (parameter_id, sample_position); a matching event is replaced in place
    // and never duplicated. New events keep the canonical sorted order and
    // respect both the 256-event and 16-unique-parameter limits.
    [[nodiscard]] bool upsert(const Vst3ParameterTimelineEventV1& event) noexcept;

    [[nodiscard]] bool remove_at(std::size_t index) noexcept;

    // Replace only the normalized value of one drafted event; the ordering
    // key (parameter_id, sample_position) stays untouched.
    [[nodiscard]] bool set_value_at(std::size_t index, double normalized_value) noexcept;

    [[nodiscard]] const Vst3ParameterTimelineSnapshotV1& published() const noexcept {
        return published_;
    }

    // Non-owning view of the draft; null while no edit session is active.
    [[nodiscard]] const Vst3ParameterTimelineSnapshotV1* draft() const noexcept {
        return draft_active_ ? &draft_ : nullptr;
    }

    // Bounded history: every successful commit pushes the previous published
    // snapshot onto the undo stack (oldest evicted when full) and clears the
    // redo stack. undo/redo move the published state between stack entries,
    // refuse while an edit session is active, and never touch a draft.
    [[nodiscard]] bool can_undo() const noexcept { return undo_count_ != 0U; }
    [[nodiscard]] bool can_redo() const noexcept { return redo_count_ != 0U; }
    [[nodiscard]] std::size_t undo_depth() const noexcept { return undo_count_; }
    [[nodiscard]] std::size_t redo_depth() const noexcept { return redo_count_; }
    void clear_history() noexcept;
    [[nodiscard]] bool undo() noexcept;
    [[nodiscard]] bool redo() noexcept;

private:
    Vst3ParameterTimelineSnapshotV1 published_{};
    Vst3ParameterTimelineSnapshotV1 draft_{};
    bool draft_active_{false};
    std::array<Vst3ParameterTimelineSnapshotV1, kVst3TimelineEditorMaxHistoryV1> undo_stack_{};
    std::array<Vst3ParameterTimelineSnapshotV1, kVst3TimelineEditorMaxHistoryV1> redo_stack_{};
    std::size_t undo_count_{0U};
    std::size_t redo_count_{0U};
};

}  // namespace hibiki
