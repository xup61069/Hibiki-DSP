#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstdint>
#include <cstddef>
#include <span>
#include <string>

#include "hibiki/vst3_worker_pipe.hpp"
#include "hibiki/vst3_worker_protocol.hpp"
#include "hibiki/vst3_crash_report.hpp"

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

// Stable, redacted reason codes for the control-plane incident summary. The
// enum deliberately carries no Win32 error, path, PID, handle, exception or
// plugin payload data.
enum class Vst3SandboxDiagnosticReasonV1 : std::uint8_t {
    None,
    InvalidLaunch,
    ProcessSetupFailed,
    WorkerExited,
    WatchdogTimeout,
    WorkerExchangeFailed,
    UnsupportedPlatform,
};

struct Vst3SandboxDiagnosticV1 {
    std::uint32_t schema_version{1U};
    Vst3SandboxState state{Vst3SandboxState::Stopped};
    Vst3SandboxDiagnosticReasonV1 reason{Vst3SandboxDiagnosticReasonV1::None};
    bool worker_pipe_server_ready{false};
    bool worker_connected{false};
};

// Bounded control-plane exchange result.  These calls are intentionally not
// callable from the RT graph: they may wait for the worker-pipe timeout and
// copy packet payloads owned by the caller.
enum class Vst3WorkerExchangeResultV1 : std::uint8_t {
    ok,
    not_running,
    not_connected,
    invalid_argument,
    allocation_failed,
    send_failed,
    receive_failed,
    worker_error,
    invalid_response,
    non_finite_output,
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
    [[nodiscard]] Vst3WorkerExchangeResultV1 handshake_worker(
        std::uint64_t request_id = 1U);
    [[nodiscard]] Vst3WorkerExchangeResultV1 process_worker_block(
        std::uint64_t request_id,
        std::uint32_t channels,
        std::uint32_t frames,
        std::span<const float> input,
        std::span<float> output,
        std::span<const Vst3WorkerParameterPointV1> parameters = {});
    [[nodiscard]] bool worker_pipe_ready() const noexcept { return worker_pipe_.server_ready(); }
    [[nodiscard]] bool worker_connected() const noexcept { return worker_pipe_.connected(); }
    [[nodiscard]] Vst3SandboxState state() const noexcept { return state_; }
    // Returns only stable enums and pipe-state booleans. No process/path,
    // exception, endpoint or opaque plugin data is retained or exposed.
    [[nodiscard]] Vst3SandboxDiagnosticV1 diagnostic() const noexcept {
        return Vst3SandboxDiagnosticV1{1U, state_, diagnostic_reason_,
                                       worker_pipe_.server_ready(), worker_pipe_.connected()};
    }
    // Bounded de-identified crash report ring fed by sandbox lifecycle
    // events (worker exit, watchdog timeout, exchange failure, forced
    // termination failure). The store never holds raw paths, PIDs,
    // handles, or command lines.
    [[nodiscard]] const Vst3CrashReportStoreV1& crash_report_store() const noexcept {
        return crash_reports_;
    }

    // Test-only injection point: forces the next TerminateJobObject call
    // inside stop()/quarantine() to be treated as failed so the control
    // plane can observe a de-identified job_object_failure entry without
    // needing a real kernel containment breakdown. Never set this in
    // production code paths; the flag is cleared after one use.
    void force_job_terminate_failure_for_test() noexcept {
        job_terminate_failure_for_test_ = true;
        crash_entry_slot_taken_ = false;
    }

private:
    void quarantine(Vst3SandboxDiagnosticReasonV1 reason) noexcept;
    void close_handles() noexcept;
    void record_crash_entry(Vst3CrashReportReasonV1 reason,
                            std::uint32_t exit_code,
                            std::uint64_t now_ms) noexcept;

    Vst3SandboxState state_{Vst3SandboxState::Stopped};
    Vst3SandboxDiagnosticReasonV1 diagnostic_reason_{
        Vst3SandboxDiagnosticReasonV1::None};
    std::uint32_t watchdog_timeout_ms_{250};
    std::uint64_t last_heartbeat_ms_{0};
    void* process_handle_{nullptr};
    void* job_handle_{nullptr};
    Vst3WorkerPipeV1 worker_pipe_{};
    Vst3CrashReportStoreV1 crash_reports_{};
    Vst3Sha256DigestV1 module_digest_{};
    std::uint64_t launched_at_ms_{0};
    bool job_terminate_failure_for_test_{false};
    // Issue 1335: single-event/single-entry semantics. record_crash_entry
    // claims this slot when it appends; later attempts to record another
    // reason for the same lifecycle event are dropped. quarantine() runs
    // before its caller's own recording, so a job_object_failure entry
    // wins the slot. stop() is an independent terminal event and resets
    // the slot first.
    bool crash_entry_slot_taken_{false};
};

}  // namespace hibiki
