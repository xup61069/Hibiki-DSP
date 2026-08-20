// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/virtual_mic.hpp"

#include <algorithm>

namespace hibiki {

bool VirtualMicRouteModel::prepare(const VirtualMicConfigV1& config) noexcept {
  if ((config.channels != 1U && config.channels != 2U) ||
      (config.sample_rate != 44100U && config.sample_rate != 48000U &&
       config.sample_rate != 96000U && config.sample_rate != 192000U)) {
    return false;
  }
  snapshot_ = VirtualMicSnapshotV1{true, true, config.echo_reference_enabled,
                                   config.channels, config.sample_rate};
  privacy_muted_ = true;
  return true;
}

void VirtualMicRouteModel::reset() noexcept {
  snapshot_ = {};
  privacy_muted_ = true;
}

bool process_virtual_mic_lane_v1(
    AudioEngineModel& engine,
    const VirtualMicRouteModel& route,
    const std::size_t lane_index,
    const float* const input_interleaved,
    const std::uint32_t input_capacity_frames,
    float* const capture_interleaved,
    const std::uint32_t capture_capacity_frames,
    const std::span<RtLaneInputV1> lane_inputs,
    float* const output_interleaved,
    const std::uint32_t output_capacity_frames,
    const std::uint32_t frames) noexcept {
  const auto& snapshot = route.snapshot();
  if (!snapshot.prepared || input_interleaved == nullptr || capture_interleaved == nullptr ||
      output_interleaved == nullptr || frames == 0U || frames > input_capacity_frames ||
      frames > capture_capacity_frames || frames > output_capacity_frames) {
    return false;
  }
  if (!route.process_capture(input_interleaved, capture_interleaved, frames)) return false;
  return engine.process_lane_block(lane_index, capture_interleaved, snapshot.channels, frames,
                                   lane_inputs, output_interleaved);
}

bool VirtualMicRouteModel::process_capture(const float* const input,
                                           float* const output,
                                           const std::size_t frames) const noexcept {
  if (!snapshot_.prepared || input == nullptr || output == nullptr || frames == 0U) return false;
  const auto samples = frames * static_cast<std::size_t>(snapshot_.channels);
  if (privacy_muted_) {
    std::fill_n(output, samples, 0.0F);
  } else {
    std::copy_n(input, samples, output);
  }
  return true;
}

bool VirtualMicRouteModel::process_echo_reference(const float* const render,
                                                  float* const reference,
                                                  const std::size_t frames) const noexcept {
  if (!snapshot_.prepared || !snapshot_.echo_reference_enabled || render == nullptr ||
      reference == nullptr || frames == 0U) {
    return false;
  }
  std::copy_n(render, frames * static_cast<std::size_t>(snapshot_.channels), reference);
  return true;
}

}  // namespace hibiki
