// SPDX-License-Identifier: GPL-3.0-only

#ifndef HIBIKI_VIRTUAL_MIC_HPP
#define HIBIKI_VIRTUAL_MIC_HPP

#include "hibiki/audio_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>

namespace hibiki {

struct VirtualMicDspPolicyV1 {
  bool echo_cancellation_enabled{false};
  bool noise_gate_enabled{false};
  std::uint32_t filter_length{32};
  float adaptation_rate{0.15F};
  float noise_gate_threshold_dbfs{-50.0F};
  float noise_gate_floor{0.08F};
  float attack_ms{5.0F};
  float release_ms{80.0F};
};

// Bounded worker/RT-safe reference canceller. It is intentionally a small
// normalized-LMS baseline, not a claim of acoustic/ITU AEC conformance.
class VirtualMicDspV1 final {
public:
  static constexpr std::uint32_t kMaxChannels = 2U;
  static constexpr std::uint32_t kMaxTaps = 128U;

  [[nodiscard]] bool prepare(const VirtualMicDspPolicyV1& policy,
                             std::uint32_t channels,
                             std::uint32_t sample_rate) noexcept;
  void reset() noexcept;
  [[nodiscard]] bool process(const float* capture,
                             const float* reference,
                             float* output,
                             std::size_t frames) noexcept;
  [[nodiscard]] bool prepared() const noexcept { return prepared_; }

private:
  VirtualMicDspPolicyV1 policy_{};
  std::array<std::array<float, kMaxTaps>, kMaxChannels> coefficients_{};
  std::array<std::array<float, kMaxTaps>, kMaxChannels> history_{};
  std::array<float, kMaxChannels> envelope_{};
  std::array<float, kMaxChannels> gate_gain_{};
  std::uint32_t channels_{0};
  std::uint32_t filter_length_{0};
  float threshold_linear_{0.0F};
  float attack_alpha_{0.0F};
  float release_alpha_{0.0F};
  bool prepared_{false};
};

struct VirtualMicConfigV1 {
  std::uint32_t channels{1};
  std::uint32_t sample_rate{48000};
  bool echo_reference_enabled{true};
  VirtualMicDspPolicyV1 dsp_policy{};
};

struct VirtualMicSnapshotV1 {
  bool prepared{false};
  bool privacy_muted{true};
  bool echo_reference_enabled{false};
  std::uint32_t channels{0};
  std::uint32_t sample_rate{0};
};

// User-space privacy/reference contract for the future virtual capture
// endpoint. Optional bounded DSP is a normalized-LMS/gate baseline, not AEC/NS
// conformance; privacy mute remains fail-closed.
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
  [[nodiscard]] bool process_capture_with_reference(const float* input,
                                                    const float* reference,
                                                    float* output,
                                                    std::size_t frames) const noexcept;
  [[nodiscard]] bool process_echo_reference(const float* render,
                                            float* reference,
                                            std::size_t frames) const noexcept;
  [[nodiscard]] const VirtualMicSnapshotV1& snapshot() const noexcept { return snapshot_; }

private:
  VirtualMicSnapshotV1 snapshot_{};
  bool privacy_muted_{true};
  mutable VirtualMicDspV1 dsp_{};
};

// Applies the privacy gate before entering the shared immutable graph. All
// audio storage remains caller-owned; the helper performs no allocation or
// blocking and fails closed on capacity/channel mismatches.
[[nodiscard]] bool process_virtual_mic_lane_v1(
    AudioEngineModel& engine,
    const VirtualMicRouteModel& route,
    std::size_t lane_index,
    const float* input_interleaved,
    std::uint32_t input_capacity_frames,
    float* capture_interleaved,
    std::uint32_t capture_capacity_frames,
    std::span<RtLaneInputV1> lane_inputs,
    float* output_interleaved,
    std::uint32_t output_capacity_frames,
    std::uint32_t frames,
    const float* echo_reference_interleaved = nullptr,
    std::uint32_t echo_reference_capacity_frames = 0U) noexcept;

}  // namespace hibiki

#endif
