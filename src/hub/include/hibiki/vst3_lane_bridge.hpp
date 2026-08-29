#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace hibiki {

constexpr std::size_t kMaxVst3LaneGroupsV1 = 32U;
constexpr std::size_t kMaxOutputGroupBytesV1 = 64U;
constexpr std::size_t kMaxVst3RingFramesV1 = 4096U;
// Must match the WASAPI sink block-frame ceiling (clamp(..., 16, 4096) in
// engine_preview.cpp). A smaller value silently drops every tap publish when
// the endpoint reports a larger buffer size, so the VST3 lane never receives
// audio. 4096 is the shared ceiling for both the tap snapshot and the ring.
constexpr std::size_t kMaxVst3TapFramesV1 = 4096U;
constexpr std::size_t kMaxVst3TapChannelsV1 = 8U;

// Fixed-capacity lock-free bridge that carries VST3 sandbox worker output
// into the RT render chain. Control thread pushes validated worker blocks;
// RT callback pops without blocking. Caller owns all storage.
class Vst3LaneRingBridgeV1 final {
public:
    // Prepare a lane slot for the given output group. Must be called from
    // the control plane before commit; not safe from the audio callback.
    [[nodiscard]] bool prepare_lane(std::string_view output_group,
                                    std::uint32_t channels,
                                    std::span<float> ring_storage) noexcept;

    // Remove a lane slot by output group. Control-plane only.
    bool clear_lane(std::string_view output_group) noexcept;
    void clear_all() noexcept;

    // Push a processed block from the control/IPC thread after a successful
    // PluginHostModel::process_worker_block() call. Rejects NaN/Inf.
    [[nodiscard]] bool push(std::string_view output_group,
                            const float* interleaved,
                            std::size_t frames) noexcept;

    // Pop from the RT callback. Returns false when no data is available
    // (caller should passthrough); never blocks or allocates.
    [[nodiscard]] bool pop(std::string_view output_group,
                           float* interleaved,
                           std::size_t frames) noexcept;

    [[nodiscard]] bool has_lane(std::string_view output_group) const noexcept;
    [[nodiscard]] std::uint32_t channel_count(
        std::string_view output_group) const noexcept;
    void reset() noexcept;

private:
    struct LaneSlot {
        bool used{false};
        std::uint8_t group_bytes{0U};
        std::array<char, kMaxOutputGroupBytesV1> group{};
        std::uint32_t channels{0U};
        std::span<float> storage{};
        std::size_t capacity_frames{0U};
        std::size_t read_frame{0U};
        std::size_t write_frame{0U};
        std::size_t available_frames{0U};
    };

    [[nodiscard]] int find_slot(
        std::string_view output_group) const noexcept;

    std::array<LaneSlot, kMaxVst3LaneGroupsV1> lanes_{};
};

// Lock-free fixed-slot snapshot of the pre-VST3 audio block. The RT callback
// publishes the latest complete slot; the control thread protects the slot
// it is reading so the writer never overwrites an in-flight snapshot. Torn
// reads are fail-closed (the caller keeps the previous safe state). This is
// bounded processing telemetry, not content analysis or physical audio
// evidence.
class Vst3TapBufferV1 final {
public:
    Vst3TapBufferV1();

    [[nodiscard]] bool publish(const std::string_view output_group,
                               const float* interleaved,
                               std::size_t frames,
                               std::uint32_t channels) noexcept;

    [[nodiscard]] bool read(std::string_view output_group,
                            float* destination,
                            std::size_t max_frames,
                            std::uint32_t& channels_out,
                            std::size_t& frames_out,
                            std::uint64_t& sequence_out) const noexcept;

    void reset() noexcept;
    [[nodiscard]] bool is_valid_for_diagnostics() const noexcept {
        return valid_.load(std::memory_order_acquire);
    }
    // Control-plane diagnostic counters. publish_attempts_ counts every
    // entry into publish(); publish_nonfinite_rejects_ counts rejections
    // caused by non-finite samples after the shape checks passed; the
    // remaining rejects are shape/group errors counted implicitly by
    // attempts minus (successes + nonfinite rejects). All are relaxed
    // atomics: diagnostics only, never used for control flow.
    [[nodiscard]] std::uint64_t publish_attempt_count() const noexcept {
        return publish_attempts_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t publish_success_count() const noexcept {
        return publish_successes_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t
    publish_nonfinite_reject_count() const noexcept {
        return publish_nonfinite_rejects_.load(std::memory_order_relaxed);
    }

    // Test-only hook: forces the internal sequence into an odd (mid-write)
    // state so contract tests can exercise the torn-read rejection path
    // without spawning a racing writer thread. Not used by production code.
    void force_sequence_odd_for_tests() noexcept;

private:
    static constexpr std::size_t kCapacitySamples =
        kMaxVst3TapFramesV1 * kMaxVst3TapChannelsV1;
    static constexpr std::size_t kSnapshotSlotCount = 3U;
    static constexpr std::uint32_t kNoReaderSlot = 0xFFFFFFFFU;

    struct SnapshotSlot final {
        std::array<std::atomic<std::uint32_t>, kCapacitySamples> buffer{};
        std::array<std::atomic<std::uint32_t>, kMaxOutputGroupBytesV1> group{};
        std::atomic<std::uint32_t> group_bytes{0U};
        std::atomic<std::uint32_t> channels{0U};
        std::atomic<std::uint64_t> frames{0U};
        std::atomic<std::uint64_t> sequence{0U};
    };

    // Every field read by the control thread is atomic. publication_ packs a
    // monotonically increasing generation, a slot index, and a low writing
    // bit. The writer marks the next slot as in-progress before changing it,
    // then publishes the stable token only after all release stores complete.
    // reader_slot_ is a bounded hazard slot: a writer may only reuse a slot
    // that is neither published nor protected by the reader. This prevents a
    // reader from sampling a slot while a later publication overwrites it.
    // Allocate the fixed slots when the bridge object is constructed, outside
    // the RT callback. publish()/read() never allocate or free storage.
    std::unique_ptr<SnapshotSlot[]> slots_;
    mutable std::atomic<std::uint64_t> publication_{0U};
    mutable std::atomic<std::uint32_t> reader_slot_{kNoReaderSlot};
    mutable std::atomic<bool> valid_{false};
    mutable std::atomic<std::uint64_t> publish_attempts_{0U};
    mutable std::atomic<std::uint64_t> publish_successes_{0U};
    mutable std::atomic<std::uint64_t> publish_nonfinite_rejects_{0U};
};

}  // namespace hibiki
