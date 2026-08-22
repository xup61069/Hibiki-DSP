#include "hibiki/vst3_timeline_editor.hpp"

#include <algorithm>
#include <cmath>

namespace hibiki {
namespace {

bool valid_edit_event(const Vst3ParameterTimelineEventV1& event) noexcept {
    return std::isfinite(event.normalized_value) && event.normalized_value >= 0.0 &&
           event.normalized_value <= 1.0;
}

bool comes_before(const Vst3ParameterTimelineEventV1& left,
                  const Vst3ParameterTimelineEventV1& right) noexcept {
    if (left.sample_position != right.sample_position) {
        return left.sample_position < right.sample_position;
    }
    return left.parameter_id < right.parameter_id;
}

bool knows_parameter(const Vst3ParameterTimelineSnapshotV1& snapshot,
                     std::uint32_t parameter_id) noexcept {
    const auto count = static_cast<std::size_t>(snapshot.event_count);
    for (std::size_t index = 0U; index < count; ++index) {
        if (snapshot.events[index].parameter_id == parameter_id) return true;
    }
    return false;
}

std::size_t unique_parameter_count(const Vst3ParameterTimelineSnapshotV1& snapshot) noexcept {
    std::array<std::uint32_t, 16U> parameter_ids{};
    std::size_t unique_parameters = 0U;
    const auto count = static_cast<std::size_t>(snapshot.event_count);
    for (std::size_t index = 0U; index < count; ++index) {
        const auto parameter_id = snapshot.events[index].parameter_id;
        bool known = false;
        for (std::size_t prior = 0U; prior < unique_parameters; ++prior) {
            known = known || parameter_ids[prior] == parameter_id;
        }
        if (!known) {
            if (unique_parameters >= parameter_ids.size()) break;
            parameter_ids[unique_parameters++] = parameter_id;
        }
    }
    return unique_parameters;
}

}  // namespace

bool Vst3TimelineEditorV1::reset(const Vst3ParameterTimelineSnapshotV1& published) noexcept {
    if (draft_active_) return false;
    if (!validate_vst3_parameter_timeline_v1(published)) return false;
    published_ = published;
    draft_ = {};
    undo_stack_ = {};
    redo_stack_ = {};
    undo_count_ = 0U;
    redo_count_ = 0U;
    return true;
}

void Vst3TimelineEditorV1::clear_history() noexcept {
    undo_stack_ = {};
    redo_stack_ = {};
    undo_count_ = 0U;
    redo_count_ = 0U;
}

bool Vst3TimelineEditorV1::undo() noexcept {
    if (draft_active_ || undo_count_ == 0U) return false;
    // Push the current published state onto the bounded redo stack, evicting
    // the oldest entry when full.
    if (redo_count_ == kVst3TimelineEditorMaxHistoryV1) {
        for (std::size_t index = 1U; index < redo_stack_.size(); ++index) {
            redo_stack_[index - 1U] = redo_stack_[index];
        }
        --redo_count_;
    }
    redo_stack_[redo_count_++] = published_;
    published_ = undo_stack_[--undo_count_];
    undo_stack_[undo_count_] = {};
    return true;
}

bool Vst3TimelineEditorV1::redo() noexcept {
    if (draft_active_ || redo_count_ == 0U) return false;
    if (undo_count_ == kVst3TimelineEditorMaxHistoryV1) {
        for (std::size_t index = 1U; index < undo_stack_.size(); ++index) {
            undo_stack_[index - 1U] = undo_stack_[index];
        }
        --undo_count_;
    }
    undo_stack_[undo_count_++] = published_;
    published_ = redo_stack_[--redo_count_];
    redo_stack_[redo_count_] = {};
    return true;
}

bool Vst3TimelineEditorV1::begin_edit() noexcept {
    if (draft_active_) return false;
    if (!validate_vst3_parameter_timeline_v1(published_)) return false;
    draft_ = published_;
    draft_active_ = true;
    return true;
}

bool Vst3TimelineEditorV1::discard() noexcept {
    if (!draft_active_) return false;
    draft_ = {};
    draft_active_ = false;
    return true;
}

bool Vst3TimelineEditorV1::commit() noexcept {
    if (!draft_active_) return false;
    if (!validate_vst3_parameter_timeline_v1(draft_)) return false;
    // Push the pre-commit published state onto the bounded undo stack,
    // evicting the oldest entry when full; a new commit branches history and
    // therefore clears the redo stack.
    if (undo_count_ == kVst3TimelineEditorMaxHistoryV1) {
        for (std::size_t index = 1U; index < undo_stack_.size(); ++index) {
            undo_stack_[index - 1U] = undo_stack_[index];
        }
        --undo_count_;
    }
    undo_stack_[undo_count_++] = published_;
    redo_count_ = 0U;
    redo_stack_ = {};
    published_ = draft_;
    draft_ = {};
    draft_active_ = false;
    return true;
}

bool Vst3TimelineEditorV1::upsert(const Vst3ParameterTimelineEventV1& event) noexcept {
    if (!draft_active_) return false;
    if (!valid_edit_event(event)) return false;
    auto count = static_cast<std::size_t>(draft_.event_count);
    for (std::size_t index = 0U; index < count; ++index) {
        auto& existing = draft_.events[index];
        if (existing.parameter_id == event.parameter_id &&
            existing.sample_position == event.sample_position) {
            existing.normalized_value = event.normalized_value;
            return validate_vst3_parameter_timeline_v1(draft_);
        }
    }
    if (count >= kVst3TimelineMaxEventsV1) return false;
    if (!knows_parameter(draft_, event.parameter_id) &&
        unique_parameter_count(draft_) >= 16U) {
        return false;
    }
    std::size_t insert_at = count;
    while (insert_at > 0U && comes_before(event, draft_.events[insert_at - 1U])) {
        draft_.events[insert_at] = draft_.events[insert_at - 1U];
        --insert_at;
    }
    draft_.events[insert_at] = event;
    draft_.event_count = static_cast<std::uint32_t>(count + 1U);
    if (!validate_vst3_parameter_timeline_v1(draft_)) {
        (void)remove_at(insert_at);
        return false;
    }
    return true;
}

bool Vst3TimelineEditorV1::remove_at(const std::size_t index) noexcept {
    if (!draft_active_) return false;
    const auto count = static_cast<std::size_t>(draft_.event_count);
    if (index >= count) return false;
    for (std::size_t cursor = index + 1U; cursor < count; ++cursor) {
        draft_.events[cursor - 1U] = draft_.events[cursor];
    }
    draft_.events[count - 1U] = {};
    draft_.event_count = static_cast<std::uint32_t>(count - 1U);
    return true;
}

bool Vst3TimelineEditorV1::set_value_at(const std::size_t index,
                                        const double normalized_value) noexcept {
    if (!draft_active_) return false;
    if (!(normalized_value >= 0.0 && normalized_value <= 1.0)) return false;
    if (!std::isfinite(normalized_value)) return false;
    if (index >= static_cast<std::size_t>(draft_.event_count)) return false;
    draft_.events[index].normalized_value = normalized_value;
    return validate_vst3_parameter_timeline_v1(draft_);
}

}  // namespace hibiki
