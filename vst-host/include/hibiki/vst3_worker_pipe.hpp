// SPDX-License-Identifier: GPL-3.0-only

#ifndef HIBIKI_VST3_WORKER_PIPE_HPP
#define HIBIKI_VST3_WORKER_PIPE_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace hibiki {

struct Vst3WorkerPipeConfigV1 {
  std::wstring pipe_name;
  std::uint32_t max_frame_bytes{1024U * 1024U};
  std::uint32_t io_timeout_ms{1000U};
};

// Control-plane named-pipe server. It is deliberately not callable from an
// audio callback; all I/O is bounded and uses caller-owned buffers.
class Vst3WorkerPipeV1 final {
public:
  Vst3WorkerPipeV1() noexcept = default;
  ~Vst3WorkerPipeV1();

  Vst3WorkerPipeV1(const Vst3WorkerPipeV1&) = delete;
  Vst3WorkerPipeV1& operator=(const Vst3WorkerPipeV1&) = delete;

  [[nodiscard]] bool create_server(const Vst3WorkerPipeConfigV1& config) noexcept;
  [[nodiscard]] bool connect_client(std::wstring_view pipe_name,
                                    std::uint32_t io_timeout_ms) noexcept;
  [[nodiscard]] bool wait_for_client(std::uint32_t timeout_ms) noexcept;
  [[nodiscard]] bool send(std::span<const std::uint8_t> frame) noexcept;
  [[nodiscard]] bool receive(std::span<std::uint8_t> destination,
                             std::size_t& bytes_read) noexcept;
  void close() noexcept;

  [[nodiscard]] bool server_ready() const noexcept { return handle_ != nullptr; }
  [[nodiscard]] bool connected() const noexcept { return connected_; }

private:
#if defined(_WIN32)
  [[nodiscard]] bool transfer(bool write,
                              void* data,
                              std::size_t bytes) noexcept;
#endif

  void* handle_{nullptr};
  std::uint32_t max_frame_bytes_{0U};
  std::uint32_t io_timeout_ms_{0U};
  bool connected_{false};
};

}  // namespace hibiki

#endif
