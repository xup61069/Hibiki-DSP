// SPDX-License-Identifier: GPL-3.0-only

#ifndef HIBIKI_VIRTUAL_MIC_HPP
#define HIBIKI_VIRTUAL_MIC_HPP

#include <cstddef>
#include <cstdint>

namespace hibiki {

struct VirtualMicConfigV1 {
  std::uint32_t channels{1};
  std::uint32_t sample_rate{48000};
  bool echo_reference_enabled{true};
};

struct VirtualMicSnapshotV1 {
  bool prepared{false};
  bool privacy_muted{true};
  bool echo_reference_enabled{false};
  std::uint32_t channels{0};
  std::uint32_t sample_rate{0};
};

// User-space privacy/reference contract for the future virtual capture
// endpoint. It performs no AEC/NS claim: capture and render-reference blocks
// are copied through caller-owned buffers, while privacy mute is fail-closed.
class VirtualMicRouteModel final {
public:
  [[nodiscard]] bool prepare(const VirtualMicConfigV1& config) noexcept;
  void reset() noexcept;
  void set_privacy_mute(bool muted) noexcept {
    privacy_muted_ = muted;
    snapshot_.privacy_muted = muted;
  }

  [[nodiscard]] bool process_capture(const float* input,
                                     float* output,
                                     std::size_t frames) const noexcept;
  [[nodiscard]] bool process_echo_reference(const float* render,
                                            float* reference,
                                            std::size_t frames) const noexcept;
  [[nodiscard]] const VirtualMicSnapshotV1& snapshot() const noexcept { return snapshot_; }

private:
  VirtualMicSnapshotV1 snapshot_{};
  bool privacy_muted_{true};
};

}  // namespace hibiki

#endif
