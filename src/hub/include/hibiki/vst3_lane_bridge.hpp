#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace hibiki {

constexpr std::size_t kMaxVst3LaneGroupsV1 = 32U;
constexpr std::size_t kMaxOutputGroupBytesV1 = 64U;
constexpr std::size_t kMaxVst3RingFramesV1 = 4096U;

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

}  // namespace hibiki

