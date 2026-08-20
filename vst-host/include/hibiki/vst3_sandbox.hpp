#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstdint>
#include <string>

namespace hibiki {

struct Vst3SandboxLaunchV1 {
    std::wstring worker_executable;
    std::wstring plugin_path;
    std::uint32_t watchdog_timeout_ms{250};
};

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
    [[nodiscard]] Vst3SandboxState state() const noexcept { return state_; }

private:
    void quarantine() noexcept;
    void close_handles() noexcept;

    Vst3SandboxState state_{Vst3SandboxState::Stopped};
    std::uint32_t watchdog_timeout_ms_{250};
    std::uint64_t last_heartbeat_ms_{0};
    void* process_handle_{nullptr};
    void* job_handle_{nullptr};
};

}  // namespace hibiki
