// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/virtual_mic.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hibiki {

namespace {

[[nodiscard]] bool checked_interleaved_sample_count(
    const std::size_t frames,
    const std::uint32_t channels,
    std::size_t& samples) noexcept {
  if (channels == 0U) return false;
  const auto channel_count = static_cast<std::size_t>(channels);
  if (frames > std::numeric_limits<std::size_t>::max() / channel_count) {
    return false;
  }
  samples = frames * channel_count;
  return true;
}

}  // namespace

bool VirtualMicDspV1::prepare(const VirtualMicDspPolicyV1& policy,
                              const std::uint32_t channels,
                              const std::uint32_t sample_rate) noexcept {
  if (channels == 0U || channels > kMaxChannels || sample_rate == 0U ||
      policy.filter_length == 0U || policy.filter_length > kMaxTaps ||
      !std::isfinite(policy.adaptation_rate) || policy.adaptation_rate < 0.0F ||
      policy.adaptation_rate > 1.0F || !std::isfinite(policy.noise_gate_threshold_dbfs) ||
      policy.noise_gate_threshold_dbfs > 0.0F || policy.noise_gate_threshold_dbfs < -144.0F ||
      !std::isfinite(policy.noise_gate_floor) || policy.noise_gate_floor < 0.0F ||
      policy.noise_gate_floor > 1.0F || !std::isfinite(policy.attack_ms) ||
      policy.attack_ms <= 0.0F || !std::isfinite(policy.release_ms) ||
      policy.release_ms <= 0.0F) {
    return false;
  }
  policy_ = policy;
  channels_ = channels;
  filter_length_ = policy.filter_length;
  threshold_linear_ = std::pow(10.0F, policy.noise_gate_threshold_dbfs / 20.0F);
  // Upper-only hysteresis: the gate closes at the configured threshold
  // (identical to prior behavior) but requires a 2 dB higher envelope to
  // reopen. This prevents chatter when the signal hovers near threshold.
  threshold_open_linear_ = threshold_linear_ * std::pow(10.0F, 2.0F / 20.0F);
  attack_alpha_ = std::exp(-1.0F / (policy.attack_ms * 0.001F * static_cast<float>(sample_rate)));
  release_alpha_ = std::exp(-1.0F / (policy.release_ms * 0.001F * static_cast<float>(sample_rate)));
  prepared_ = true;
  reset();
  return true;
}

void VirtualMicDspV1::reset() noexcept {
  coefficients_.fill({});
  history_.fill({});
  envelope_.fill(0.0F);
  gate_gain_.fill(1.0F);
  gate_open_.fill(false);
}

bool VirtualMicDspV1::process(const float* const capture,
                              const float* const reference,
                              float* const output,
                              const std::size_t frames) noexcept {
  if (!prepared_ || capture == nullptr || output == nullptr || frames == 0U) return false;
  std::size_t samples = 0U;
  if (!checked_interleaved_sample_count(frames, channels_, samples)) return false;
  for (std::size_t index = 0U; index < samples; ++index) {
    if (!std::isfinite(capture[index]) ||
        (reference != nullptr && !std::isfinite(reference[index]))) {
      std::fill_n(output, samples, 0.0F);
      return false;
    }
  }
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    for (std::uint32_t channel = 0U; channel < channels_; ++channel) {
      const std::size_t index = frame * channels_ + channel;
      float value = capture[index];
      if (policy_.echo_cancellation_enabled && reference != nullptr) {
        for (std::uint32_t tap = filter_length_ - 1U; tap > 0U; --tap) {
          history_[channel][tap] = history_[channel][tap - 1U];
        }
        history_[channel][0] = reference[index];
        float estimate = 0.0F;
        float energy = 1.0e-6F;
        for (std::uint32_t tap = 0U; tap < filter_length_; ++tap) {
          estimate += coefficients_[channel][tap] * history_[channel][tap];
          energy += history_[channel][tap] * history_[channel][tap];
        }
        const float error = value - estimate;
        const float update = policy_.adaptation_rate * error / energy;
        for (std::uint32_t tap = 0U; tap < filter_length_; ++tap) {
          coefficients_[channel][tap] += update * history_[channel][tap];
        }
        value = error;
      }
      if (policy_.noise_gate_enabled) {
        const float magnitude = std::abs(value);
        const float alpha = magnitude > envelope_[channel] ? attack_alpha_ : release_alpha_;
        envelope_[channel] = alpha * envelope_[channel] + (1.0F - alpha) * magnitude;
        // Upper-only hysteresis prevents chatter: close at the configured
        // threshold, reopen only 2 dB higher; hold state between.
        if (!gate_open_[channel] && envelope_[channel] >= threshold_open_linear_) {
          gate_open_[channel] = true;
        } else if (gate_open_[channel] && envelope_[channel] < threshold_linear_) {
          gate_open_[channel] = false;
        }
        const float target = gate_open_[channel] ? 1.0F : policy_.noise_gate_floor;
        // Attack (attack_ms) controls how fast the gate OPENS as the signal
        // rises above threshold; release (release_ms) controls how fast it
        // CLOSES after the signal falls below threshold.
        const float gate_opening = target > gate_gain_[channel];
        const float gain_alpha = gate_opening ? attack_alpha_ : release_alpha_;
        gate_gain_[channel] = gain_alpha * gate_gain_[channel] + (1.0F - gain_alpha) * target;
        value *= gate_gain_[channel];
      }
      output[index] = std::isfinite(value) ? value : 0.0F;
    }
  }
  return true;
}

bool VirtualMicRouteModel::prepare(const VirtualMicConfigV1& config) noexcept {
  if ((config.channels != 1U && config.channels != 2U) ||
      (config.sample_rate != 44100U && config.sample_rate != 48000U &&
       config.sample_rate != 96000U && config.sample_rate != 192000U)) {
    return false;
  }
  if (!dsp_.prepare(config.dsp_policy, config.channels, config.sample_rate)) return false;
  snapshot_ = VirtualMicSnapshotV1{true, true, config.echo_reference_enabled,
                                   config.channels, config.sample_rate};
  privacy_muted_ = true;
  return true;
}

void VirtualMicRouteModel::reset() noexcept {
  snapshot_ = {};
  privacy_muted_ = true;
  dsp_.reset();
}

static bool process_virtual_mic_lane_impl(
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
    const std::uint32_t frames,
    const float* const echo_reference_interleaved,
    const std::uint32_t echo_reference_capacity_frames,
    const bool to_wasapi) noexcept {
  const auto& snapshot = route.snapshot();
  if (!snapshot.prepared || input_interleaved == nullptr || capture_interleaved == nullptr ||
      output_interleaved == nullptr || frames == 0U || frames > input_capacity_frames ||
      frames > capture_capacity_frames || frames > output_capacity_frames ||
      (echo_reference_interleaved != nullptr && frames > echo_reference_capacity_frames)) {
    return false;
  }
  if (!route.process_capture_with_reference(input_interleaved, echo_reference_interleaved,
                                             capture_interleaved, frames)) return false;
  return to_wasapi
             ? engine.process_lane_block_to_wasapi(lane_index, capture_interleaved,
                                                   snapshot.channels, frames, lane_inputs,
                                                   output_interleaved)
             : engine.process_lane_block(lane_index, capture_interleaved, snapshot.channels,
                                         frames, lane_inputs, output_interleaved);
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
    const std::uint32_t frames,
    const float* const echo_reference_interleaved,
    const std::uint32_t echo_reference_capacity_frames) noexcept {
  return process_virtual_mic_lane_impl(
      engine, route, lane_index, input_interleaved, input_capacity_frames, capture_interleaved,
      capture_capacity_frames, lane_inputs, output_interleaved, output_capacity_frames, frames,
      echo_reference_interleaved, echo_reference_capacity_frames, false);
}

bool process_virtual_mic_lane_to_wasapi_v1(
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
    const std::uint32_t frames,
    const float* const echo_reference_interleaved,
    const std::uint32_t echo_reference_capacity_frames) noexcept {
  return process_virtual_mic_lane_impl(
      engine, route, lane_index, input_interleaved, input_capacity_frames, capture_interleaved,
      capture_capacity_frames, lane_inputs, output_interleaved, output_capacity_frames, frames,
      echo_reference_interleaved, echo_reference_capacity_frames, true);
}

bool VirtualMicRouteModel::process_capture(const float* const input,
                                           float* const output,
                                           const std::size_t frames) const noexcept {
  if (!snapshot_.prepared || input == nullptr || output == nullptr || frames == 0U) return false;
  std::size_t samples = 0U;
  if (!checked_interleaved_sample_count(frames, snapshot_.channels, samples)) return false;
  if (privacy_muted_) {
    std::fill_n(output, samples, 0.0F);
  } else {
    if (!dsp_.process(input, nullptr, output, frames)) return false;
  }
  return true;
}

bool VirtualMicRouteModel::process_capture_with_reference(
    const float* const input,
    const float* const reference,
    float* const output,
    const std::size_t frames) const noexcept {
  if (!snapshot_.prepared || input == nullptr || output == nullptr || frames == 0U) return false;
  std::size_t samples = 0U;
  if (!checked_interleaved_sample_count(frames, snapshot_.channels, samples)) return false;
  if (privacy_muted_) {
    std::fill_n(output, samples, 0.0F);
    return true;
  }
  if (reference != nullptr && !snapshot_.echo_reference_enabled) return false;
  return dsp_.process(input, reference, output, frames);
}

bool VirtualMicRouteModel::process_echo_reference(const float* const render,
                                                  float* const reference,
                                                  const std::size_t frames) const noexcept {
  if (!snapshot_.prepared || !snapshot_.echo_reference_enabled || render == nullptr ||
      reference == nullptr || frames == 0U) {
    return false;
  }
  std::size_t samples = 0U;
  if (!checked_interleaved_sample_count(frames, snapshot_.channels, samples)) return false;
  std::copy_n(render, samples, reference);
  return true;
}

}  // namespace hibiki
