#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstdint>
#include <cstddef>
#include <span>
#include <string>

#include "hibiki/vst3_worker_pipe.hpp"

namespace hibiki {

struct Vst3SandboxLaunchV1 {
    std::wstring worker_executable;
    std::wstring plugin_path;
  std::uint32_t watchdog_timeout_ms{250};
  std::uint64_t start_time_ms{1};
  std::wstring worker_pipe_name;
  std::uint32_t worker_pipe_timeout_ms{1000};
  // Optional fields select the SDK-backed worker executable.  Empty class_id
  // keeps the existing passthrough worker contract unchanged.
  std::wstring vst3_class_id;
  double vst3_sample_rate{48000.0};
  std::uint32_t vst3_channels{0};
};

[[nodiscard]] bool validate_vst3_sandbox_launch_v1(
    const Vst3SandboxLaunchV1& launch) noexcept;

enum class Vst3SandboxState : std::uint8_t {
    Stopped,
    Running,
    Quarantined,
};

// Control-plane process supervisor. Audio never calls this class. The future
// VST3 worker IPC must publish heartbeat only after it has completed its own
// SDK/plugin work; a dead or stalled worker is terminated as one job.
class Vst3SandboxProcess final {
public:
    Vst3SandboxProcess() noexcept = default;
    ~Vst3SandboxProcess();

    Vst3SandboxProcess(const Vst3SandboxProcess&) = delete;
    Vst3SandboxProcess& operator=(const Vst3SandboxProcess&) = delete;

    [[nodiscard]] bool launch(const Vst3SandboxLaunchV1& launch);
    void stop() noexcept;
    [[nodiscard]] bool mark_heartbeat(std::uint64_t now_ms) noexcept;
    [[nodiscard]] bool poll_watchdog(std::uint64_t now_ms) noexcept;
    [[nodiscard]] bool wait_for_worker(std::uint32_t timeout_ms) noexcept;
    [[nodiscard]] bool send_worker_frame(std::span<const std::uint8_t> frame) noexcept;
    [[nodiscard]] bool receive_worker_frame(std::span<std::uint8_t> destination,
                                             std::size_t& bytes_read) noexcept;
    [[nodiscard]] bool worker_pipe_ready() const noexcept { return worker_pipe_.server_ready(); }
    [[nodiscard]] bool worker_connected() const noexcept { return worker_pipe_.connected(); }
    [[nodiscard]] Vst3SandboxState state() const noexcept { return state_; }

private:
    void quarantine() noexcept;
    void close_handles() noexcept;

    Vst3SandboxState state_{Vst3SandboxState::Stopped};
    std::uint32_t watchdog_timeout_ms_{250};
    std::uint64_t last_heartbeat_ms_{0};
    void* process_handle_{nullptr};
    void* job_handle_{nullptr};
    Vst3WorkerPipeV1 worker_pipe_{};
};

}  // namespace hibiki
