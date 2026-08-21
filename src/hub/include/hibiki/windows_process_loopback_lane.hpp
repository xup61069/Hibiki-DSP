#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#if defined(_WIN32)

#include "hibiki/audio_engine.hpp"
#include "hibiki/windows_process_loopback.hpp"

#include <cstdint>
#include <span>

namespace hibiki {

struct WindowsProcessLoopbackBlockV1 {
    std::uint32_t frames{0U};
    std::uint32_t channels{0U};
    std::uint32_t sample_rate{0U};
};

// Worker-to-engine adapter. The source performs the COM/WASAPI read; this
// function only validates the immutable source snapshot and feeds one
// caller-owned block through the existing lane graph. It never stores a PID,
// allocates, waits or performs physical routing by itself.
[[nodiscard]] bool process_windows_process_loopback_lane_v1(
    AudioEngineModel& engine,
    WindowsProcessLoopbackSourceV1& source,
    std::size_t lane_index,
    float* input_interleaved,
    std::uint32_t input_capacity_frames,
    std::span<RtLaneInputV1> lane_inputs,
    float* output_interleaved,
    std::uint32_t output_capacity_frames,
    WindowsProcessLoopbackBlockV1& block,
    bool to_wasapi = false) noexcept;

}  // namespace hibiki

#endif  // defined(_WIN32)
