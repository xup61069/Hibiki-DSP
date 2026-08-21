// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/windows_process_loopback_lane.hpp"

#if defined(_WIN32)

namespace hibiki {

bool process_windows_process_loopback_lane_v1(
    AudioEngineModel& engine,
    WindowsProcessLoopbackSourceV1& source,
    const std::size_t lane_index,
    float* const input_interleaved,
    const std::uint32_t input_capacity_frames,
    const std::span<RtLaneInputV1> lane_inputs,
    float* const output_interleaved,
    const std::uint32_t output_capacity_frames,
    WindowsProcessLoopbackBlockV1& block,
    const bool to_wasapi) noexcept {
    block = {};
    if (input_interleaved == nullptr || output_interleaved == nullptr ||
        input_capacity_frames == 0U || output_capacity_frames == 0U) {
        return false;
    }
    const auto before = source.snapshot();
    if (before.state != WindowsProcessLoopbackStateV1::Running || before.channels == 0U ||
        before.sample_rate == 0U) {
        return false;
    }
    std::uint32_t frames = 0U;
    if (!source.read(input_interleaved, input_capacity_frames, frames) || frames == 0U ||
        frames > output_capacity_frames) {
        return false;
    }
    const auto after = source.snapshot();
    if (after.state != WindowsProcessLoopbackStateV1::Running ||
        after.channels != before.channels || after.sample_rate != before.sample_rate) {
        return false;
    }
    const bool processed = to_wasapi
                               ? engine.process_lane_block_to_wasapi(
                                     lane_index, input_interleaved, after.channels, frames,
                                     lane_inputs, output_interleaved)
                               : engine.process_lane_block(
                                     lane_index, input_interleaved, after.channels, frames,
                                     lane_inputs, output_interleaved);
    if (!processed) return false;
    block = WindowsProcessLoopbackBlockV1{frames, after.channels, after.sample_rate};
    return true;
}

}  // namespace hibiki

#endif  // defined(_WIN32)
