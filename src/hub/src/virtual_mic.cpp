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
