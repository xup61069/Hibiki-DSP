// SPDX-License-Identifier: GPL-3.0-only

#ifndef HIBIKI_WINDOWS_WASAPI_OUTPUT_HPP
#define HIBIKI_WINDOWS_WASAPI_OUTPUT_HPP

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "hibiki/output_sink.hpp"

namespace hibiki {

struct WasapiOutputConfigV1 {
  std::wstring endpoint_id;
  std::uint32_t channels{2};
  std::uint32_t sample_rate{48000};
  std::uint32_t buffer_duration_ms{20};
};

struct WasapiClockSampleV1 {
  std::uint64_t device_position{0U};
  std::uint64_t qpc_position{0U};
};

enum class WasapiSampleEncodingV1 : std::uint8_t {
  Float32,
  Pcm16,
  Pcm24,
  Pcm32,
};

// WASAPI shared-mode endpoint owned by one dedicated sink worker (not the
// Hibiki graph RT thread). That worker must initialize COM and perform bind,
// start, render and unbind on the same apartment; the control plane schedules
// commands to it. Only Float32 mix formats are accepted.
class WindowsWasapiOutputV1 final {
public:
  WindowsWasapiOutputV1() noexcept = default;
  ~WindowsWasapiOutputV1();

  WindowsWasapiOutputV1(const WindowsWasapiOutputV1&) = delete;
  WindowsWasapiOutputV1& operator=(const WindowsWasapiOutputV1&) = delete;

  [[nodiscard]] bool bind(const WasapiOutputConfigV1& config) noexcept;
  [[nodiscard]] bool start() noexcept;
  void stop() noexcept;
  void unbind() noexcept;
  [[nodiscard]] bool render(const float* interleaved, std::uint32_t frames) noexcept;
  [[nodiscard]] bool wait_for_buffer(std::uint32_t timeout_ms) noexcept;
  [[nodiscard]] bool read_clock(WasapiClockSampleV1& sample) const noexcept;

  [[nodiscard]] bool bound() const noexcept { return client_ != nullptr && render_client_ != nullptr; }
  [[nodiscard]] bool started() const noexcept { return started_; }
  [[nodiscard]] std::uint32_t channels() const noexcept { return channels_; }
  [[nodiscard]] std::uint32_t sample_rate() const noexcept { return sample_rate_; }
  [[nodiscard]] std::uint32_t buffer_frames() const noexcept { return buffer_frames_; }
  [[nodiscard]] WasapiSampleEncodingV1 encoding() const noexcept { return encoding_; }

private:
  void release_resources() noexcept;

#if defined(_WIN32)
  void* client_{nullptr};
  void* render_client_{nullptr};
  void* clock_{nullptr};
  void* event_{nullptr};
#else
  void* client_{nullptr};
  void* render_client_{nullptr};
  void* clock_{nullptr};
#endif
  std::uint32_t channels_{0};
  std::uint32_t sample_rate_{0};
  std::uint32_t buffer_frames_{0};
  WasapiSampleEncodingV1 encoding_{WasapiSampleEncodingV1::Float32};
  std::uint32_t bytes_per_sample_{0};
  bool started_{false};
};

struct WasapiSinkWorkerSnapshotV1 {
  bool running{false};
  bool endpoint_ready{false};
  bool degraded{false};
  std::uint32_t channels{0};
  std::uint32_t sample_rate{0};
  std::uint32_t block_frames{0};
  std::uint32_t dropped_blocks{0};
  std::uint64_t submitted_blocks{0};
  std::uint64_t rendered_blocks{0};
  double source_step{1.0};
  double drift_ppm{0.0};
};

// Owns one WASAPI endpoint on a dedicated COM apartment. Producers only call
// submit(), which is a bounded SPSC copy and never touches COM, allocates or
// waits. The worker renders silence on an empty queue and exposes overrun/
// endpoint state for the control plane.
class WindowsWasapiSinkWorkerV1 final {
public:
  WindowsWasapiSinkWorkerV1() noexcept = default;
  ~WindowsWasapiSinkWorkerV1();

  WindowsWasapiSinkWorkerV1(const WindowsWasapiSinkWorkerV1&) = delete;
  WindowsWasapiSinkWorkerV1& operator=(const WindowsWasapiSinkWorkerV1&) = delete;

  [[nodiscard]] bool start(const WasapiOutputConfigV1& config,
                           std::uint32_t block_frames = 128U) noexcept;
  void stop() noexcept;
  void observe_clock(double source_frames,
                     double sink_frames,
                     double elapsed_seconds) noexcept;
  [[nodiscard]] bool submit(const float* interleaved,
                            std::uint32_t frames,
                            std::uint32_t channels) noexcept;
  [[nodiscard]] bool submit_scaled(const float* interleaved,
                                   std::uint32_t frames,
                                   std::uint32_t channels,
                                   float gain) noexcept;
  [[nodiscard]] WasapiSinkWorkerSnapshotV1 snapshot() const noexcept;

private:
  static constexpr std::uint32_t kSlotCount = 8U;
  static constexpr std::uint32_t kMaxChannels = 8U;
  static constexpr std::uint32_t kMaxFrames = 4096U;
  struct Slot {
    std::atomic<std::uint32_t> ready_sequence{0U};
    std::uint32_t frames{0U};
    std::uint32_t channels{0U};
    std::array<float, kMaxChannels * kMaxFrames> samples{};
  };

  void run(WasapiOutputConfigV1 config, std::uint32_t block_frames) noexcept;
  [[nodiscard]] bool pop(float* interleaved,
                         std::uint32_t output_capacity_frames,
                         std::uint32_t& frames,
                         std::uint32_t& channels) noexcept;

  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> running_{false};
  std::atomic<bool> endpoint_ready_{false};
  std::atomic<bool> degraded_{false};
  std::atomic<std::uint32_t> producer_sequence_{0U};
  std::atomic<std::uint32_t> consumer_sequence_{0U};
  std::atomic<std::uint32_t> dropped_blocks_{0U};
  std::atomic<std::uint64_t> submitted_blocks_{0U};
  std::atomic<std::uint64_t> rendered_blocks_{0U};
  std::atomic<std::uint64_t> clock_request_sequence_{0U};
  std::atomic<double> clock_source_frames_{0.0};
  std::atomic<double> clock_sink_frames_{0.0};
  std::atomic<double> clock_elapsed_seconds_{0.0};
  std::atomic<double> source_step_{1.0};
  std::atomic<double> drift_ppm_{0.0};
  std::uint32_t channels_{0U};
  std::uint32_t sample_rate_{0U};
  std::uint32_t block_frames_{0U};
  // The bounded ring is prepared on the control side. Keeping the large
  // sample storage off the object stack is required because two workers are
  // alive during a handoff; submit/pop never allocate or resize it.
  std::unique_ptr<std::array<Slot, kSlotCount>> slots_{};
  OutputSinkModel sink_model_{};
  std::thread worker_;
};

}  // namespace hibiki

#endif
