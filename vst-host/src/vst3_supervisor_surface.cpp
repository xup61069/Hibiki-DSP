#include "hibiki/vst3_supervisor_surface.hpp"

namespace hibiki {

bool Vst3TimelineSupervisorSurfaceV1::snapshots_equal(
    const Vst3ParameterTimelineSnapshotV1& left,
    const Vst3ParameterTimelineSnapshotV1& right) noexcept {
    if (left.schema_version != right.schema_version) return false;
    if (left.event_count != right.event_count) return false;
    const auto count = static_cast<std::size_t>(left.event_count);
    for (std::size_t index = 0U; index < count; ++index) {
        const auto& a = left.events[index];
        const auto& b = right.events[index];
        if (a.parameter_id != b.parameter_id) return false;
        if (a.sample_position != b.sample_position) return false;
        if (a.normalized_value != b.normalized_value) return false;
    }
    return true;
}

bool Vst3TimelineSupervisorSurfaceV1::attach(
    Vst3TimelineFileStoreV1& store) noexcept {
    if (store_ != nullptr) return false;
    store_ = &store;
    editor_ = {};
    baseline_ = {};
    selected_id_ = {};
    selected_size_ = 0U;
    last_store_status_ = Vst3TimelineStoreStatusV1::ok;
    return true;
}

bool Vst3TimelineSupervisorSurfaceV1::detach() noexcept {
    if (store_ == nullptr) return false;
    // An open draft represents unsaved intent; dropping the surface must not
    // discard it silently. The caller commits or discards explicitly first.
    if (editor_.has_edit_session()) return false;
    store_ = nullptr;
    editor_ = {};
    baseline_ = {};
    selected_id_ = {};
    selected_size_ = 0U;
    last_store_status_ = Vst3TimelineStoreStatusV1::ok;
    return true;
}

Vst3TimelineStoreStatusV1 Vst3TimelineSupervisorSurfaceV1::refresh_ids(
    std::span<std::string> destination,
    std::size_t& count) {
    count = 0U;
    if (store_ == nullptr) return Vst3TimelineStoreStatusV1::invalid_argument;
    const auto status = store_->list_ids(destination, count);
    last_store_status_ = status;
    return status;
}

bool Vst3TimelineSupervisorSurfaceV1::select(const std::string_view id) {
    if (store_ == nullptr) return false;
    if (editor_.has_edit_session()) return false;
    Vst3ParameterTimelineSnapshotV1 loaded{};
    const auto status = store_->load(id, loaded);
    last_store_status_ = status;
    if (status != Vst3TimelineStoreStatusV1::ok) return false;
    if (!editor_.reset(loaded)) return false;
    baseline_ = loaded;
    selected_id_ = {};
    if (id.size() > kVst3TimelineStoreMaxIdBytesV1) return false;
    for (std::size_t index = 0U; index < id.size(); ++index) {
        selected_id_[index] = id[index];
    }
    selected_size_ = id.size();
    return true;
}

bool Vst3TimelineSupervisorSurfaceV1::begin_edit() {
    if (!ready_for_editing()) return false;
    return editor_.begin_edit();
}

bool Vst3TimelineSupervisorSurfaceV1::discard() {
    if (!ready_for_editing()) return false;
    return editor_.discard();
}

bool Vst3TimelineSupervisorSurfaceV1::commit() {
    if (!ready_for_editing()) return false;
    return editor_.commit();
}

bool Vst3TimelineSupervisorSurfaceV1::upsert(const Vst3ParameterTimelineEventV1& event) {
    if (!ready_for_editing()) return false;
    return editor_.upsert(event);
}

bool Vst3TimelineSupervisorSurfaceV1::remove_at(const std::size_t index) {
    if (!ready_for_editing()) return false;
    return editor_.remove_at(index);
}

bool Vst3TimelineSupervisorSurfaceV1::set_value_at(
    const std::size_t index,
    const double normalized_value) {
    if (!ready_for_editing()) return false;
    return editor_.set_value_at(index, normalized_value);
}

bool Vst3TimelineSupervisorSurfaceV1::undo() {
    if (!ready_for_editing()) return false;
    return editor_.undo();
}

bool Vst3TimelineSupervisorSurfaceV1::redo() {
    if (!ready_for_editing()) return false;
    return editor_.redo();
}

// Drops both history stacks without touching any published snapshot, baseline
// or open draft. Surface-level: the editor is always valid (just empty while
// detached/unselected), so this is safe to call at any time.
void Vst3TimelineSupervisorSurfaceV1::clear_history() noexcept {
    editor_.clear_history();
}

Vst3TimelineStoreStatusV1 Vst3TimelineSupervisorSurfaceV1::save_selected() {
    if (!ready_for_editing()) return Vst3TimelineStoreStatusV1::invalid_argument;
    if (editor_.has_edit_session()) return Vst3TimelineStoreStatusV1::invalid_argument;
    const auto& snapshot = editor_.published();
    if (!validate_vst3_parameter_timeline_v1(snapshot)) {
        return Vst3TimelineStoreStatusV1::invalid_argument;
    }
    const auto status =
        store_->save(std::string_view(selected_id_.data(), selected_size_), snapshot);
    last_store_status_ = status;
    if (status == Vst3TimelineStoreStatusV1::ok) {
        baseline_ = snapshot;
    }
    return status;
}

bool Vst3TimelineSupervisorSurfaceV1::remove_selected() {
    // An open draft represents unsaved intent; deleting the selected
    // timeline must not discard it silently. The caller commits or discards
    // explicitly first.
    if (!ready_for_editing() || editor_.has_edit_session()) return false;
    const auto status =
        store_->remove(std::string_view(selected_id_.data(), selected_size_));
    last_store_status_ = status;
    if (status != Vst3TimelineStoreStatusV1::ok) return false;
    editor_ = {};
    baseline_ = {};
    selected_id_ = {};
    selected_size_ = 0U;
    return true;
}

bool Vst3TimelineSupervisorSurfaceV1::is_dirty() const noexcept {
    if (!ready_for_editing()) return false;
    return !snapshots_equal(editor_.published(), baseline_);
}

}  // namespace hibiki
