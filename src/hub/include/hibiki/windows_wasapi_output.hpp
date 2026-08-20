// SPDX-License-Identifier: GPL-3.0-only

#ifndef HIBIKI_WINDOWS_WASAPI_OUTPUT_HPP
#define HIBIKI_WINDOWS_WASAPI_OUTPUT_HPP

#include <cstdint>
#include <string>

namespace hibiki {

struct WasapiOutputConfigV1 {
  std::wstring endpoint_id;
  std::uint32_t channels{2};
  std::uint32_t sample_rate{48000};
  std::uint32_t buffer_duration_ms{20};
};

// Control-plane WASAPI shared-mode endpoint with an allocation-free render
// call for a dedicated sink worker (not the Hibiki graph RT thread). The
// caller must initialize COM on the binding thread. Only Float32 mix formats
// are accepted so the sink worker never performs format conversion.
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

  [[nodiscard]] bool bound() const noexcept { return client_ != nullptr && render_client_ != nullptr; }
  [[nodiscard]] bool started() const noexcept { return started_; }
  [[nodiscard]] std::uint32_t channels() const noexcept { return channels_; }
  [[nodiscard]] std::uint32_t sample_rate() const noexcept { return sample_rate_; }
  [[nodiscard]] std::uint32_t buffer_frames() const noexcept { return buffer_frames_; }

private:
  void release_resources() noexcept;

#if defined(_WIN32)
  void* client_{nullptr};
  void* render_client_{nullptr};
  void* event_{nullptr};
#else
  void* client_{nullptr};
  void* render_client_{nullptr};
#endif
  std::uint32_t channels_{0};
  std::uint32_t sample_rate_{0};
  std::uint32_t buffer_frames_{0};
  bool started_{false};
};

}  // namespace hibiki

#endif
