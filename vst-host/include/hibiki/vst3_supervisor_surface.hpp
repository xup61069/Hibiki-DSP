#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_timeline_editor.hpp"
#include "hibiki/vst3_timeline_file_store.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace hibiki {

// Supervisor-side editing surface composing one Vst3TimelineEditorV1 with one
// non-owned Vst3TimelineFileStoreV1 handle. It gives a future supervisor UI a
// single fail-closed control-plane entry point for selection-aware timeline
// editing: drafts never reach the store, saves require a committed published
// snapshot, and dirty state is derived by comparing the published snapshot
// against the last loaded/saved baseline rather than tracked by hand. The
// store stays a single-writer control-plane object; the surface owns no
// worker, audio buffer or file handle and never runs on the RT thread.
class Vst3TimelineSupervisorSurfaceV1 final {
public:
    Vst3TimelineSupervisorSurfaceV1() noexcept = default;

    // Attach the one non-owned store handle. Refused while any store is
    // already attached; call detach first. Attaching starts with no
    // selection.
    [[nodiscard]] bool attach(Vst3TimelineFileStoreV1& store) noexcept;

    // Detach and drop selection plus all editor state. Refused while an edit
    // session is open so unsaved drafts cannot be silently lost.
    [[nodiscard]] bool detach() noexcept;

    [[nodiscard]] bool is_attached() const noexcept { return store_ != nullptr; }

    // Bounded sorted listing of currently stored IDs, forwarded from the
    // attached store. Fails closed with invalid_argument while detached.
    [[nodiscard]] Vst3TimelineStoreStatusV1 refresh_ids(
        std::span<std::string> destination,
        std::size_t& count);

    // Load one stored timeline and adopt it as the editor baseline. Refused
    // while detached or while an edit session is open. A failed load keeps the
    // previous selection and baseline untouched; a successful load replaces
    // both and clears editor history.
    [[nodiscard]] bool select(std::string_view id);

    [[nodiscard]] bool has_selection() const noexcept { return selected_size_ != 0U; }

    [[nodiscard]] std::string_view selected_id() const noexcept {
        return std::string_view(selected_id_.data(), selected_size_);
    }

    // Editing forwards; every mutating call fails closed while detached or
    // unselected, then defers to the editor's own transaction rules.
    [[nodiscard]] bool begin_edit();
    [[nodiscard]] bool discard();
    [[nodiscard]] bool commit();
    [[nodiscard]] bool upsert(const Vst3ParameterTimelineEventV1& event);
    [[nodiscard]] bool remove_at(std::size_t index);
    [[nodiscard]] bool set_value_at(std::size_t index, double normalized_value);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();

    // Bounded history introspection forwarded from the composed editor.
    // These are surface-level: the editor is always valid (just empty while
    // detached/unselected), so they never fail and never require attach or
    // selection. clear_history() drops both stacks without touching any
    // published snapshot, baseline or open draft.
    [[nodiscard]] bool can_undo() const noexcept { return editor_.can_undo(); }
    [[nodiscard]] bool can_redo() const noexcept { return editor_.can_redo(); }
    [[nodiscard]] std::size_t undo_depth() const noexcept { return editor_.undo_depth(); }
    [[nodiscard]] std::size_t redo_depth() const noexcept { return editor_.redo_depth(); }
    void clear_history() noexcept;

    // Persist the current published snapshot under the selected ID through the
    // store's atomic save path. Requires a committed state (no open draft); a
    // successful save re-baselines dirty tracking.
    [[nodiscard]] Vst3TimelineStoreStatusV1 save_selected();

    // Derived dirty state: true when a selection exists and its published
    // snapshot differs from the last loaded/saved baseline. An open draft is
    // reported by editor().has_edit_session(), not by this flag.
    [[nodiscard]] bool is_dirty() const noexcept;

    [[nodiscard]] const Vst3TimelineEditorV1& editor() const noexcept { return editor_; }

    // Most recent store status observed by select/save_selected/refresh_ids.
    [[nodiscard]] Vst3TimelineStoreStatusV1 last_store_status() const noexcept {
        return last_store_status_;
    }

private:
    static bool snapshots_equal(
        const Vst3ParameterTimelineSnapshotV1& left,
        const Vst3ParameterTimelineSnapshotV1& right) noexcept;

    [[nodiscard]] bool ready_for_editing() const noexcept {
        return store_ != nullptr && selected_size_ != 0U;
    }

    Vst3TimelineFileStoreV1* store_{nullptr};
    Vst3TimelineEditorV1 editor_{};
    Vst3ParameterTimelineSnapshotV1 baseline_{};
    std::array<char, kVst3TimelineStoreMaxIdBytesV1 + 1U> selected_id_{};
    std::size_t selected_size_{0U};
    Vst3TimelineStoreStatusV1 last_store_status_{Vst3TimelineStoreStatusV1::ok};
};

}  // namespace hibiki
