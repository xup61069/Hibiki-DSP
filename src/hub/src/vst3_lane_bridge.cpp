#include "hibiki/vst3_lane_bridge.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

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
    if (frames > kMaxVst3RingFramesV1 || slot.channels == 0U ||
        frames > std::numeric_limits<std::size_t>::max() / slot.channels ||
        slot.available_frames > slot.capacity_frames ||
        frames > slot.capacity_frames - slot.available_frames) {
        return false;
    }
    const std::size_t sample_count = frames * slot.channels;
    for (std::size_t i = 0U; i < sample_count; ++i) {
        if (!std::isfinite(interleaved[i])) { return false; }
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
    if (frames > kMaxVst3RingFramesV1) {
        return false;
    }
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

// --- Vst3TapBufferV1 implementation ---

bool Vst3TapBufferV1::publish(
    const std::string_view output_group,
    const float* interleaved,
    const std::size_t frames,
    const std::uint32_t channels) noexcept {
    publish_attempts_.fetch_add(1U, std::memory_order_relaxed);
    if (output_group.empty() ||
        output_group.size() > kMaxOutputGroupBytesV1 ||
        interleaved == nullptr || frames == 0U ||
        frames > kMaxVst3TapFramesV1 ||
        channels == 0U || channels > kMaxVst3TapChannelsV1) {
        return false;
    }

    // Reject NaN/Inf — the worker must never receive garbage.
    const std::size_t sample_count = frames * static_cast<std::size_t>(channels);
    for (std::size_t i = 0U; i < sample_count; ++i) {
        if (!std::isfinite(interleaved[i])) {
            publish_nonfinite_rejects_.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }
    }

    // Acquire sequence: even = stable, odd = mid-write. We increment to
    // odd, write data, then increment to even (seqlock-style).
    const std::uint64_t seq =
        write_seq_.fetch_add(1U, std::memory_order_relaxed) + 1U;

    std::memcpy(buffer_.data(), interleaved,
                sample_count * sizeof(float));
    std::memcpy(group_.data(), output_group.data(), output_group.size());
    group_bytes_ = static_cast<std::uint8_t>(output_group.size());
    channels_ = channels;
    frames_ = frames;
    sequence_ = seq;

    // Release: publish with release ordering so readers see complete data.
    publish_successes_.fetch_add(1U, std::memory_order_relaxed);
    valid_.store(true, std::memory_order_release);
    write_seq_.fetch_add(1U, std::memory_order_release);
    return true;
}

bool Vst3TapBufferV1::read(
    const std::string_view output_group,
    float* destination,
    const std::size_t max_frames,
    std::uint32_t& channels_out,
    std::size_t& frames_out,
    std::uint64_t& sequence_out) const noexcept {
    if (destination == nullptr || max_frames == 0U) { return false; }

    // Acquire fence: see a consistent snapshot or reject.
    const bool is_valid = valid_.load(std::memory_order_acquire);
    if (!is_valid) { return false; }

    // Pre-read sequence: paired with the post-read load below so a writer
    // that completes a full publish while we copy is detected (the classic
    // seqlock retry check; this reader is fail-closed instead of retrying).
    const std::uint64_t pre_seq =
        write_seq_.load(std::memory_order_acquire);

    // Read all scalar fields first.
    const auto group_bytes = group_bytes_;
    const auto channels = channels_;
    const auto frames = frames_;
    const auto seq = sequence_;

    // Verify group matches.
    if (group_bytes != output_group.size() ||
        std::memcmp(group_.data(), output_group.data(), group_bytes) != 0) {
        return false;
    }

    // Check capacity and shape.
    if (channels == 0U || channels > kMaxVst3TapChannelsV1 ||
        frames == 0U || frames > kMaxVst3TapFramesV1) {
        return false;
    }

    const auto channel_count = static_cast<std::size_t>(channels);
    if (max_frames > std::numeric_limits<std::size_t>::max() / channel_count) {
        return false;
    }
    const std::size_t sample_count = static_cast<std::size_t>(frames) * channel_count;
    if (sample_count > max_frames * channel_count) { return false; }

    std::memcpy(destination, buffer_.data(), sample_count * sizeof(float));

    // Post-read validation: require the sequence to be unchanged and even.
    // An odd sequence means we raced an in-flight publish; a changed even
    // value means the writer completed a whole publish round during our
    // memcpy. Either way the samples may be torn: fail-closed rather than
    // returning partial audio.
    const std::uint64_t post_seq =
        write_seq_.load(std::memory_order_acquire);
    if (pre_seq != post_seq || (post_seq & 1U) != 0U) { return false; }

    channels_out = channels;
    frames_out = frames;
    sequence_out = seq;
    return true;
}

void Vst3TapBufferV1::reset() noexcept {
    valid_.store(false, std::memory_order_release);
}

void Vst3TapBufferV1::force_sequence_odd_for_tests() noexcept {
    // Simulate a reader that sampled pre_seq even and then observed an
    // in-flight publish (odd) at the post-read check.
    write_seq_.store(write_seq_.load(std::memory_order_acquire) + 1U,
                     std::memory_order_release);
}

}  // namespace hibiki
