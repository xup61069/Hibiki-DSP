#include "hibiki/vst3_lane_bridge.hpp"
#include "hibiki/control_payloads.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace hibiki {

namespace {

[[nodiscard]] bool valid_output_group(const std::string_view output_group) noexcept {
    return !output_group.empty() &&
           output_group.size() <= kMaxOutputGroupBytesV1 &&
           output_group.find('\0') == std::string_view::npos &&
           is_printable_utf8_v1(output_group);
}

}  // namespace

Vst3TapBufferV1::Vst3TapBufferV1()
    : slots_(std::make_unique<SnapshotSlot[]>(kSnapshotSlotCount)) {}

int Vst3LaneRingBridgeV1::find_slot(
    const std::string_view output_group) const noexcept {
    if (!valid_output_group(output_group)) return -1;
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
    if (!valid_output_group(output_group) || channels == 0U ||
        channels > 8U || ring_storage.empty()) {
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
    if (!valid_output_group(output_group) ||
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

    constexpr std::uint64_t kWritingBit = 1U;
    constexpr std::uint64_t kSlotMask = 0x6U;
    constexpr std::uint32_t kSlotShift = 1U;
    constexpr std::uint32_t kGenerationShift = 3U;

    // A three-slot publication keeps the active snapshot immutable while the
    // control reader holds its hazard slot. The sequence token is
    // generation|slot|phase: an odd token is an in-progress publication and
    // an even token is a complete snapshot. Seq_cst is intentional for the
    // short publication/hazard handshake; it closes the window where a
    // reader announces its slot just as a writer is selecting a reuse slot.
    const std::uint64_t current_token =
        publication_.load(std::memory_order_seq_cst);
    if ((current_token & kWritingBit) != 0U) { return false; }

    const auto current_slot =
        current_token == 0U
            ? kNoReaderSlot
            : static_cast<std::uint32_t>((current_token & kSlotMask) >>
                                          kSlotShift);
    const auto reader_slot = reader_slot_.load(std::memory_order_seq_cst);
    std::uint32_t target_slot = kNoReaderSlot;
    for (std::uint32_t candidate = 0U;
         candidate < static_cast<std::uint32_t>(kSnapshotSlotCount);
         ++candidate) {
        if (candidate != current_slot && candidate != reader_slot) {
            target_slot = candidate;
            break;
        }
    }
    if (target_slot == kNoReaderSlot) { return false; }

    const auto current_generation = current_token >> kGenerationShift;
    const auto max_generation =
        std::numeric_limits<std::uint64_t>::max() >> kGenerationShift;
    const auto next_generation =
        current_generation >= max_generation ? 1U : current_generation + 1U;
    const auto in_progress_token =
        (next_generation << kGenerationShift) |
        (static_cast<std::uint64_t>(target_slot) << kSlotShift) | kWritingBit;
    const auto stable_token = in_progress_token & ~kWritingBit;

    // Mark the target slot in-flight before touching its payload. A reader
    // that already selected that slot will observe this token at its second
    // validation load and reject the copy.
    publication_.store(in_progress_token, std::memory_order_seq_cst);

    for (std::size_t index = 0U; index < sample_count; ++index) {
        slots_[target_slot].buffer[index].store(
            std::bit_cast<std::uint32_t>(interleaved[index]),
            std::memory_order_release);
    }
    for (std::size_t index = 0U; index < output_group.size(); ++index) {
        slots_[target_slot].group[index].store(
            static_cast<std::uint32_t>(
                static_cast<unsigned char>(output_group[index])),
            std::memory_order_release);
    }
    slots_[target_slot].group_bytes.store(
        static_cast<std::uint32_t>(output_group.size()),
        std::memory_order_release);
    slots_[target_slot].channels.store(channels, std::memory_order_release);
    slots_[target_slot].frames.store(static_cast<std::uint64_t>(frames),
                                     std::memory_order_release);
    slots_[target_slot].sequence.store(next_generation,
                                        std::memory_order_release);

    // Publish only after every field in the selected slot is complete.
    publish_successes_.fetch_add(1U, std::memory_order_relaxed);
    publication_.store(stable_token, std::memory_order_seq_cst);
    valid_.store(true, std::memory_order_release);
    return true;
}

bool Vst3TapBufferV1::read(
    const std::string_view output_group,
    float* destination,
    const std::size_t max_frames,
    std::uint32_t& channels_out,
    std::size_t& frames_out,
    std::uint64_t& sequence_out) const noexcept {
    if (!valid_output_group(output_group) || destination == nullptr ||
        max_frames == 0U) {
        return false;
    }

    const bool is_valid = valid_.load(std::memory_order_acquire);
    if (!is_valid) { return false; }

    constexpr std::uint64_t kWritingBit = 1U;
    constexpr std::uint64_t kSlotMask = 0x6U;
    constexpr std::uint32_t kSlotShift = 1U;
    const auto pre_token = publication_.load(std::memory_order_seq_cst);
    if (pre_token == 0U || (pre_token & kWritingBit) != 0U) {
        return false;
    }
    const auto slot = static_cast<std::uint32_t>(
        (pre_token & kSlotMask) >> kSlotShift);
    if (slot >= kSnapshotSlotCount) { return false; }

    // Announce the slot before the second token check. The writer either sees
    // this hazard and chooses another slot, or its in-progress token makes the
    // second check fail before any payload is read.
    reader_slot_.store(slot, std::memory_order_seq_cst);
    struct ReaderSlotGuard final {
        std::atomic<std::uint32_t>& slot;
        ~ReaderSlotGuard() {
            slot.store(Vst3TapBufferV1::kNoReaderSlot,
                       std::memory_order_seq_cst);
        }
    } reader_guard{reader_slot_};
    if (publication_.load(std::memory_order_seq_cst) != pre_token) {
        return false;
    }

    // Read all scalar fields first.
    const auto group_bytes =
        slots_[slot].group_bytes.load(std::memory_order_acquire);
    const auto channels = slots_[slot].channels.load(std::memory_order_acquire);
    const auto frames = slots_[slot].frames.load(std::memory_order_acquire);
    const auto seq = slots_[slot].sequence.load(std::memory_order_acquire);

    // Verify group matches.
    if (group_bytes != output_group.size() || group_bytes > kMaxOutputGroupBytesV1) {
        return false;
    }
    for (std::size_t index = 0U; index < group_bytes; ++index) {
        if (slots_[slot].group[index].load(std::memory_order_acquire) !=
            static_cast<std::uint32_t>(
                static_cast<unsigned char>(output_group[index]))) {
            return false;
        }
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

    for (std::size_t index = 0U; index < sample_count; ++index) {
        destination[index] = std::bit_cast<float>(
            slots_[slot].buffer[index].load(std::memory_order_acquire));
    }

    // Post-read validation: the protected slot and complete publication token
    // must still be the same. Any in-flight or later generation is rejected.
    const std::uint64_t post_token =
        publication_.load(std::memory_order_seq_cst);
    if (pre_token != post_token || (post_token & kWritingBit) != 0U) {
        return false;
    }

    channels_out = channels;
    frames_out = frames;
    sequence_out = seq;
    return true;
}

void Vst3TapBufferV1::reset() noexcept {
    publication_.store(0U, std::memory_order_seq_cst);
    // Do not clear an in-flight reader hazard here. A reader that observed
    // the previous stable token may still be copying its slot; preserving the
    // hazard keeps the first post-reset publication from reusing that slot.
    // The reader guard clears it when the copy finishes. If no reader is
    // active, it is already kNoReaderSlot.
    valid_.store(false, std::memory_order_release);
}

void Vst3TapBufferV1::force_sequence_odd_for_tests() noexcept {
    // Simulate a reader that observes an in-flight publication. reset() is
    // the only supported way to clear this test-only state.
    const auto token = publication_.load(std::memory_order_seq_cst);
    if (token != 0U) {
        publication_.store(token | 1U, std::memory_order_seq_cst);
    }
}

}  // namespace hibiki
