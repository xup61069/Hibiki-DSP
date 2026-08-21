// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_scene_automation.hpp"

#include <new>

namespace hibiki {

Vst3SceneAutomationSchedulerV1::Vst3SceneAutomationSchedulerV1() noexcept {
    for (auto& flag : busy_) flag.clear(std::memory_order_relaxed);
}

bool Vst3SceneAutomationSchedulerV1::valid_id(const std::string_view id) const noexcept {
    return !id.empty() && id.size() <= kVst3SceneAutomationMaxIdBytesV1 &&
           id.find('\0') == std::string_view::npos;
}

std::size_t Vst3SceneAutomationSchedulerV1::find_lane(
    const std::uint64_t lane_token) const noexcept {
    if (lane_token == 0U) return lane_count_;
    for (std::size_t index = 0U; index < lane_count_; ++index) {
        if (lanes_[index] != nullptr &&
            lanes_[index]->latency_lane_input().lane_token == lane_token) {
            return index;
        }
    }
    return lane_count_;
}

std::size_t Vst3SceneAutomationSchedulerV1::find_timeline(
    const std::string_view timeline_id) const noexcept {
    for (std::size_t index = 0U; index < timelines_.size(); ++index) {
        if (timelines_[index].occupied && timelines_[index].id == timeline_id) return index;
    }
    return timelines_.size();
}

bool Vst3SceneAutomationSchedulerV1::prepare(
    const std::span<Vst3WorkerLaneSessionV1* const> lanes) noexcept {
    if (lanes.empty() || lanes.size() > lanes_.size()) return false;
    std::array<Vst3WorkerLaneSessionV1*, kVst3SceneAutomationMaxEntriesV1> candidate{};
    for (std::size_t index = 0U; index < lanes.size(); ++index) {
        if (lanes[index] == nullptr || lanes[index]->latency_lane_input().lane_token == 0U) {
            return false;
        }
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (candidate[prior]->latency_lane_input().lane_token ==
                lanes[index]->latency_lane_input().lane_token) {
                return false;
            }
        }
        candidate[index] = lanes[index];
    }
    lanes_ = candidate;
    lane_count_ = lanes.size();
    active_scene_.clear();
    return true;
}

void Vst3SceneAutomationSchedulerV1::clear() noexcept {
    edit_editor_ = {};
    edit_active_ = false;
    edit_slot_ = kVst3SceneAutomationMaxEntriesV1;
    for (auto& slot : timelines_) {
        slot.occupied = false;
        slot.id.clear();
        slot.snapshot = {};
    }
    for (auto& slot : bindings_) {
        slot.occupied = false;
        slot.binding = {};
    }
    lanes_.fill(nullptr);
    lane_count_ = 0U;
    timeline_count_ = 0U;
    binding_count_ = 0U;
    active_scene_.clear();
    for (auto& flag : busy_) flag.clear(std::memory_order_release);
}

bool Vst3SceneAutomationSchedulerV1::upsert_timeline(
    const std::string_view timeline_id,
    const Vst3ParameterTimelineSnapshotV1& snapshot) {
    if (!valid_id(timeline_id) || !validate_vst3_parameter_timeline_v1(snapshot)) return false;
    auto index = find_timeline(timeline_id);
    try {
        if (index == timelines_.size()) {
            for (std::size_t candidate = 0U; candidate < timelines_.size(); ++candidate) {
                if (!timelines_[candidate].occupied) {
                    index = candidate;
                    break;
                }
            }
            if (index == timelines_.size()) return false;
            timelines_[index].id.assign(timeline_id.data(), timeline_id.size());
            timelines_[index].occupied = true;
            ++timeline_count_;
        }
        timelines_[index].snapshot = snapshot;
        return true;
    } catch (const std::bad_alloc&) {
        if (index < timelines_.size() && timelines_[index].occupied &&
            timelines_[index].snapshot.event_count == 0U && timelines_[index].id.empty()) {
            timelines_[index].occupied = false;
        }
        return false;
    }
}

bool Vst3SceneAutomationSchedulerV1::remove_timeline(
    const std::string_view timeline_id) noexcept {
    const auto index = find_timeline(timeline_id);
    if (index == timelines_.size()) return false;
    if (edit_active_ && edit_slot_ == index) return false;
    for (const auto& slot : bindings_) {
        if (slot.occupied && slot.binding.timeline_id == timeline_id) return false;
    }
    timelines_[index] = {};
    --timeline_count_;
    return true;
}

bool Vst3SceneAutomationSchedulerV1::begin_timeline_edit(
    const std::string_view timeline_id) {
    if (edit_active_) return false;
    if (!valid_id(timeline_id)) return false;
    const auto index = find_timeline(timeline_id);
    if (index == timelines_.size()) return false;
    if (!edit_editor_.reset(timelines_[index].snapshot)) return false;
    if (!edit_editor_.begin_edit()) {
        edit_editor_ = {};
        return false;
    }
    edit_slot_ = index;
    edit_active_ = true;
    return true;
}

bool Vst3SceneAutomationSchedulerV1::commit_timeline_edit() {
    if (!edit_active_) return false;
    if (!edit_editor_.commit()) return false;
    timelines_[edit_slot_].snapshot = edit_editor_.published();
    edit_editor_ = {};
    edit_active_ = false;
    edit_slot_ = kVst3SceneAutomationMaxEntriesV1;
    return true;
}

bool Vst3SceneAutomationSchedulerV1::cancel_timeline_edit() noexcept {
    if (!edit_active_) return false;
    (void)edit_editor_.discard();
    edit_editor_ = {};
    edit_active_ = false;
    edit_slot_ = kVst3SceneAutomationMaxEntriesV1;
    return true;
}

const Vst3ParameterTimelineSnapshotV1* Vst3SceneAutomationSchedulerV1::timeline_snapshot(
    const std::string_view timeline_id) const noexcept {
    const auto index = find_timeline(timeline_id);
    if (index == timelines_.size()) return nullptr;
    return &timelines_[index].snapshot;
}

bool Vst3SceneAutomationSchedulerV1::bind_scene(
    const std::string_view scene_id,
    const std::uint64_t lane_token,
    const std::string_view timeline_id) {
    if (!valid_id(scene_id) || !valid_id(timeline_id) || find_lane(lane_token) == lane_count_ ||
        find_timeline(timeline_id) == timelines_.size()) {
        return false;
    }
    try {
        for (auto& slot : bindings_) {
            if (slot.occupied && slot.binding.scene_id == scene_id &&
                slot.binding.lane_token == lane_token) {
                slot.binding.timeline_id.assign(timeline_id.data(), timeline_id.size());
                return true;
            }
        }
        for (auto& slot : bindings_) {
            if (!slot.occupied) {
                slot.binding.scene_id.assign(scene_id.data(), scene_id.size());
                slot.binding.lane_token = lane_token;
                slot.binding.timeline_id.assign(timeline_id.data(), timeline_id.size());
                slot.occupied = true;
                ++binding_count_;
                return true;
            }
        }
    } catch (const std::bad_alloc&) {
        return false;
    }
    return false;
}

Vst3SceneAutomationResultV1 Vst3SceneAutomationSchedulerV1::activate_scene(
    const std::string_view scene_id) {
    if (!valid_id(scene_id)) return Vst3SceneAutomationResultV1::invalid_argument;
    std::size_t matching = 0U;
    for (const auto& slot : bindings_) {
        if (!slot.occupied || slot.binding.scene_id != scene_id) continue;
        ++matching;
        const auto lane_index = find_lane(slot.binding.lane_token);
        const auto timeline_index = find_timeline(slot.binding.timeline_id);
        if (lane_index == lane_count_) return Vst3SceneAutomationResultV1::missing_lane;
        if (timeline_index == timelines_.size()) return Vst3SceneAutomationResultV1::missing_timeline;
        const auto lane_state = lanes_[lane_index]->state();
        if (lane_state != Vst3WorkerLaneStateV1::Prepared &&
            lane_state != Vst3WorkerLaneStateV1::Ready) {
            return Vst3SceneAutomationResultV1::lane_not_ready;
        }
    }
    if (matching == 0U) return Vst3SceneAutomationResultV1::not_bound;
    for (const auto& slot : bindings_) {
        if (!slot.occupied || slot.binding.scene_id != scene_id) continue;
        const auto lane_index = find_lane(slot.binding.lane_token);
        const auto timeline_index = find_timeline(slot.binding.timeline_id);
        if (!lanes_[lane_index]->set_parameter_timeline(timelines_[timeline_index].snapshot)) {
            return Vst3SceneAutomationResultV1::lane_not_ready;
        }
    }
    try {
        active_scene_.assign(scene_id.data(), scene_id.size());
    } catch (const std::bad_alloc&) {
        active_scene_.clear();
        return Vst3SceneAutomationResultV1::worker_failed;
    }
    return Vst3SceneAutomationResultV1::ok;
}

Vst3SceneAutomationResultV1 Vst3SceneAutomationSchedulerV1::process_lane_block(
    const std::string_view scene_id,
    const std::uint64_t lane_token,
    const std::uint64_t request_id,
    const std::uint64_t block_start,
    const std::uint32_t frames,
    const std::span<const float> input,
    const std::span<float> output) {
    if (!valid_id(scene_id) || active_scene_ != scene_id) {
        return Vst3SceneAutomationResultV1::scene_not_active;
    }
    const auto lane_index = find_lane(lane_token);
    if (lane_index == lane_count_) return Vst3SceneAutomationResultV1::missing_lane;
    bool bound = false;
    for (const auto& slot : bindings_) {
        if (slot.occupied && slot.binding.scene_id == scene_id &&
            slot.binding.lane_token == lane_token) {
            bound = true;
            break;
        }
    }
    if (!bound) return Vst3SceneAutomationResultV1::not_bound;
    if (busy_[lane_index].test_and_set(std::memory_order_acquire)) {
        return Vst3SceneAutomationResultV1::busy;
    }
    const auto result = lanes_[lane_index]->process_block(
        request_id, block_start, frames, input, output);
    busy_[lane_index].clear(std::memory_order_release);
    if (result == Vst3WorkerExchangeResultV1::ok) return Vst3SceneAutomationResultV1::ok;
    if (result == Vst3WorkerExchangeResultV1::not_connected ||
        result == Vst3WorkerExchangeResultV1::not_running) {
        return Vst3SceneAutomationResultV1::lane_not_ready;
    }
    return Vst3SceneAutomationResultV1::worker_failed;
}

}  // namespace hibiki
