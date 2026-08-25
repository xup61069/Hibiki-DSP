#include "hibiki/vst3_lane_bridge.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hibiki {

int Vst3LaneRingBridgeV1::find_slot(
    const std::string_view output_group) const noexcept {
    if (output_group.empty() ||
        output_group.size() > kMaxOutputGroupBytesV1) {
        return -1;
    }
    for (std::size_t i = 0U; i < lanes_.size(); ++i) {
        const auto& slot = lanes_[i];
        if (slot.used &&
            slot.group_bytes == output_group.size() &&
            std::memcmp(slot.group.data(), output_group.data(),
                        output_group.size()) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool Vst3LaneRingBridgeV1::prepare_lane(
    const std::string_view output_group,
    const std::uint32_t channels,
    const std::span<float> ring_storage) noexcept {
    if (output_group.empty() ||
        output_group.size() > kMaxOutputGroupBytesV1 ||
        output_group.find('\0') != std::string_view::npos ||
        channels == 0U || channels > 8U || ring_storage.empty()) {
        return false;
    }

    // Reject duplicate registration.
    if (find_slot(output_group) >= 0) { return false; }

    // Find an unused slot.
    int free_index = -1;
    for (std::size_t i = 0U; i < lanes_.size(); ++i) {
        if (!lanes_[i].used) {
            free_index = static_cast<int>(i);
            break;
        }
    }
    if (free_index < 0) { return false; }

    auto& slot = lanes_[static_cast<std::size_t>(free_index)];
    slot.used = true;
    slot.group_bytes = static_cast<std::uint8_t>(output_group.size());
    std::memcpy(slot.group.data(), output_group.data(), output_group.size());
    slot.channels = channels;
    slot.storage = ring_storage;
    slot.capacity_frames = ring_storage.size() / channels;
    slot.read_frame = 0U;
    slot.write_frame = 0U;
    slot.available_frames = 0U;
    return true;
}

bool Vst3LaneRingBridgeV1::clear_lane(
    const std::string_view output_group) noexcept {
    const int index = find_slot(output_group);
    if (index < 0) { return false; }
    auto& slot = lanes_[static_cast<std::size_t>(index)];
    slot.used = false;
    slot.group_bytes = 0U;
    slot.channels = 0U;
    slot.capacity_frames = 0U;
    slot.read_frame = 0U;
    slot.write_frame = 0U;
    slot.available_frames = 0U;
    return true;
}

void Vst3LaneRingBridgeV1::clear_all() noexcept {
    for (auto& slot : lanes_) {
        slot.used = false;
        slot.group_bytes = 0U;
        slot.channels = 0U;
        slot.capacity_frames = 0U;
        slot.read_frame = 0U;
        slot.write_frame = 0U;
        slot.available_frames = 0U;
    }
}

bool Vst3LaneRingBridgeV1::push(
    const std::string_view output_group,
    const float* interleaved,
    const std::size_t frames) noexcept {
    const int index = find_slot(output_group);
    if (index < 0 || interleaved == nullptr || frames == 0U) {
        return false;
    }
    auto& slot = lanes_[static_cast<std::size_t>(index)];
    const std::size_t sample_count = frames * slot.channels;
    for (std::size_t i = 0U; i < sample_count; ++i) {
        if (!std::isfinite(interleaved[i])) { return false; }
    }
    if (frames + slot.available_frames > slot.capacity_frames) {
        return false;  // Ring full.
    }
    for (std::size_t f = 0U; f < frames; ++f) {
        const float* src = interleaved + f * slot.channels;
        float* dst = slot.storage.data() +
                     slot.write_frame * slot.channels;
        std::memcpy(dst, src, slot.channels * sizeof(float));
        slot.write_frame =
            (slot.write_frame + 1U) % slot.capacity_frames;
    }
    slot.available_frames += frames;
    return true;
}

bool Vst3LaneRingBridgeV1::pop(
    const std::string_view output_group,
    float* interleaved,
    const std::size_t frames) noexcept {
    const int index = find_slot(output_group);
    if (index < 0 || interleaved == nullptr || frames == 0U) {
        return false;
    }
    auto& slot = lanes_[static_cast<std::size_t>(index)];
    if (slot.available_frames < frames) {
        return false;  // Not enough data yet.
    }
    for (std::size_t f = 0U; f < frames; ++f) {
        const float* src = slot.storage.data() +
                           slot.read_frame * slot.channels;
        float* dst = interleaved + f * slot.channels;
        std::memcpy(dst, src, slot.channels * sizeof(float));
        slot.read_frame =
            (slot.read_frame + 1U) % slot.capacity_frames;
    }
    slot.available_frames -= frames;
    return true;
}

bool Vst3LaneRingBridgeV1::has_lane(
    const std::string_view output_group) const noexcept {
    return find_slot(output_group) >= 0;
}

std::uint32_t Vst3LaneRingBridgeV1::channel_count(
    const std::string_view output_group) const noexcept {
    const int index = find_slot(output_group);
    if (index < 0) { return 0U; }
    return lanes_[static_cast<std::size_t>(index)].channels;
}

void Vst3LaneRingBridgeV1::reset() noexcept {
    clear_all();
}

}  // namespace hibiki

