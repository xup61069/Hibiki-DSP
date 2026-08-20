#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/ipc.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>

namespace hibiki {

struct IpcNamedPipeConfigV1 {
    // Full Win32 path, e.g. \\ . \\ pipe \\ HibikiDSP_v1_control (without spaces).
    std::wstring pipe_name;
    std::uint32_t max_frame_bytes{static_cast<std::uint32_t>(kIpcMaxPayloadBytes + 20U)};
    std::uint32_t io_timeout_ms{1000U};
};

using IpcFrameHandlerV1 = bool (*)(const IpcFrameV1& request,
                                   IpcFrameV1& response,
                                   void* context) noexcept;

// Control-plane-only named pipe. Every operation runs on its own worker;
// callers must never invoke it from an audio callback. A client frame is
// length-prefixed, decoded, handed to the callback, then a bounded response
// is encoded and sent back. One client is served at a time and remote clients
// are rejected by the pipe mode.
class IpcNamedPipeServerV1 final {
public:
    IpcNamedPipeServerV1() noexcept = default;
    ~IpcNamedPipeServerV1();

    IpcNamedPipeServerV1(const IpcNamedPipeServerV1&) = delete;
    IpcNamedPipeServerV1& operator=(const IpcNamedPipeServerV1&) = delete;

    [[nodiscard]] bool start(const IpcNamedPipeConfigV1& config,
                             IpcFrameHandlerV1 handler,
                             void* context) noexcept;
    void stop() noexcept;

    [[nodiscard]] bool running() const noexcept { return running_.load(std::memory_order_acquire); }
    [[nodiscard]] bool client_connected() const noexcept {
        return client_connected_.load(std::memory_order_acquire);
    }

private:
    void run() noexcept;
    void cancel_current_io() noexcept;

    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> client_connected_{false};
    std::atomic<std::uintptr_t> pipe_handle_{0U};
    std::wstring pipe_name_;
    std::uint32_t max_frame_bytes_{0U};
    std::uint32_t io_timeout_ms_{0U};
    IpcFrameHandlerV1 handler_{nullptr};
    void* handler_context_{nullptr};
    std::thread worker_;
};

}  // namespace hibiki
