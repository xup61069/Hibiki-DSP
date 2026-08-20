#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstdint>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <vector>
#include <string_view>

namespace hibiki {

struct LaneConfigV1 {
    std::string id;
    std::string output_group;
    std::uint32_t channel_count{2};
    double makeup_gain_db{0.0};
    bool enabled{true};
    // Each input channel maps to one output channel; -1 means intentionally
    // muted. The default is identity for all supported layouts.
    std::array<std::int8_t, 8> channel_map{0, 1, 2, 3, 4, 5, 6, 7};
};

struct GraphConfigV1 {
    std::uint32_t schema_version{1};
    std::vector<LaneConfigV1> lanes;
    std::uint32_t output_channels{2};
    bool strict_direct{false};
};

constexpr std::size_t kMaxRtLanes = 32;
constexpr std::size_t kMaxOutputGroupBytes = 64;

struct RtLaneSnapshotV1 {
    std::uint32_t input_channels{2};
    std::array<std::int8_t, 8> channel_map{0, 1, 2, 3, 4, 5, 6, 7};
    std::uint8_t output_group_bytes{0U};
    std::array<char, kMaxOutputGroupBytes> output_group{};
    float makeup_gain_linear{1.0F};
    bool enabled{true};
};

struct RtGraphSnapshotV1 {
    std::uint32_t schema_version{1};
    std::uint32_t output_channels{2};
    std::uint32_t lane_count{0};
    std::uint64_t revision{0};
    std::array<RtLaneSnapshotV1, kMaxRtLanes> lanes{};
};

struct RtLaneInputV1 {
    const float* interleaved{nullptr};
    std::uint32_t channel_count{0};
};

// Control-plane validation is allocation-free and safe to call before a
// snapshot is handed to the RT thread.
[[nodiscard]] bool validate_graph(const GraphConfigV1& graph) noexcept;
[[nodiscard]] bool compile_rt_snapshot(const GraphConfigV1& graph,
                                       std::uint64_t revision,
                                       RtGraphSnapshotV1& snapshot) noexcept;
[[nodiscard]] bool process_graph(const RtGraphSnapshotV1& snapshot,
                                 std::span<const RtLaneInputV1> inputs,
                                 float* output_interleaved,
                                 std::size_t frames) noexcept;
[[nodiscard]] bool process_graph_for_output_group(
    const RtGraphSnapshotV1& snapshot,
    std::string_view output_group,
    std::span<const RtLaneInputV1> inputs,
    float* output_interleaved,
    std::size_t frames) noexcept;

}  // namespace hibiki
