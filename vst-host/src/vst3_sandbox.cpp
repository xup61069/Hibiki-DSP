// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_sandbox.hpp"

#include <chrono>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <vector>

namespace hibiki {
namespace {

HANDLE as_handle(void* value) noexcept { return reinterpret_cast<HANDLE>(value); }
void* as_pointer(HANDLE value) noexcept { return reinterpret_cast<void*>(value); }

std::wstring quote_argument(const std::wstring& value) {
    std::wstring quoted;
    quoted.reserve(value.size() + 2U);
    quoted.push_back(L'"');
    for (const auto character : value) {
        if (character == L'"') quoted.push_back(L'\\');
        quoted.push_back(character);
    }
    quoted.push_back(L'"');
    return quoted;
}

}  // namespace

bool validate_vst3_sandbox_launch_v1(const Vst3SandboxLaunchV1& launch_config) noexcept {
    if (launch_config.worker_executable.empty() || launch_config.plugin_path.empty() ||
        launch_config.watchdog_timeout_ms == 0U || launch_config.watchdog_timeout_ms > 5000U ||
        (!launch_config.worker_pipe_name.empty() && launch_config.worker_pipe_timeout_ms == 0U)) {
        return false;
    }
    if (launch_config.vst3_class_id.empty()) return true;
    if (!std::isfinite(launch_config.vst3_sample_rate) ||
        launch_config.vst3_sample_rate < 8000.0 || launch_config.vst3_sample_rate > 384000.0) {
        return false;
    }
    return launch_config.vst3_channels == 2U || launch_config.vst3_channels == 6U ||
           launch_config.vst3_channels == 8U;
}

Vst3SandboxProcess::~Vst3SandboxProcess() { stop(); }

bool Vst3SandboxProcess::launch(const Vst3SandboxLaunchV1& launch_config) {
    stop();
    if (!validate_vst3_sandbox_launch_v1(launch_config)) {
        quarantine(Vst3SandboxDiagnosticReasonV1::InvalidLaunch);
        return false;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) {
        quarantine(Vst3SandboxDiagnosticReasonV1::ProcessSetupFailed);
        return false;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) == FALSE) {
        CloseHandle(job);
        quarantine(Vst3SandboxDiagnosticReasonV1::ProcessSetupFailed);
        return false;
    }

    auto command_line = quote_argument(launch_config.worker_executable) + L" --plugin " +
                         quote_argument(launch_config.plugin_path);
    if (!launch_config.vst3_class_id.empty()) {
        command_line += L" --vst3-module " + quote_argument(launch_config.plugin_path);
        command_line += L" --vst3-class " + quote_argument(launch_config.vst3_class_id);
        command_line += L" --vst3-rate " + std::to_wstring(launch_config.vst3_sample_rate);
        command_line += L" --vst3-channels " + std::to_wstring(launch_config.vst3_channels);
    }
    if (!launch_config.worker_pipe_name.empty()) {
        if (!worker_pipe_.create_server(Vst3WorkerPipeConfigV1{
                launch_config.worker_pipe_name, 1024U * 1024U, launch_config.worker_pipe_timeout_ms})) {
            CloseHandle(job);
            quarantine(Vst3SandboxDiagnosticReasonV1::ProcessSetupFailed);
            return false;
        }
        command_line += L" --hibiki-pipe " + quote_argument(launch_config.worker_pipe_name);
    }
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process_info{};
    const BOOL created = CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process_info);
    if (created == FALSE || AssignProcessToJobObject(job, process_info.hProcess) == FALSE) {
        if (created != FALSE) {
            TerminateProcess(process_info.hProcess, 1U);
            CloseHandle(process_info.hThread);
            CloseHandle(process_info.hProcess);
        }
        CloseHandle(job);
        worker_pipe_.close();
        quarantine(Vst3SandboxDiagnosticReasonV1::ProcessSetupFailed);
        return false;
    }
    CloseHandle(process_info.hThread);
    process_handle_ = as_pointer(process_info.hProcess);
    job_handle_ = as_pointer(job);
    // Compute the de-identified module digest once at launch; only the
    // 32-byte SHA-256 of the plugin path bytes is retained, never the path.
    {
        const auto* narrow = reinterpret_cast<const std::uint8_t*>(
            launch_config.plugin_path.data());
        const auto narrow_bytes = launch_config.plugin_path.size() * sizeof(wchar_t);
        module_digest_ = vst3_sha256_v1({narrow, narrow_bytes});
    }
    watchdog_timeout_ms_ = launch_config.watchdog_timeout_ms;
    last_heartbeat_ms_ = launch_config.start_time_ms == 0U ? 1U : launch_config.start_time_ms;
    launched_at_ms_ = last_heartbeat_ms_;
    state_ = Vst3SandboxState::Running;
    diagnostic_reason_ = Vst3SandboxDiagnosticReasonV1::None;
    crash_reports_.clear();
    return true;
}

void Vst3SandboxProcess::close_handles() noexcept {
    if (process_handle_ != nullptr) {
        CloseHandle(as_handle(process_handle_));
        process_handle_ = nullptr;
    }
    if (job_handle_ != nullptr) {
        CloseHandle(as_handle(job_handle_));
        job_handle_ = nullptr;
    }
}

void Vst3SandboxProcess::stop() noexcept {
    if (job_handle_ != nullptr) {
        TerminateJobObject(as_handle(job_handle_), 0U);
        if (process_handle_ != nullptr) WaitForSingleObject(as_handle(process_handle_), 1000U);
    }
    close_handles();
    worker_pipe_.close();
    state_ = Vst3SandboxState::Stopped;
    diagnostic_reason_ = Vst3SandboxDiagnosticReasonV1::None;
    last_heartbeat_ms_ = 0;
    launched_at_ms_ = 0;
}

bool Vst3SandboxProcess::mark_heartbeat(const std::uint64_t now_ms) noexcept {
    if (state_ != Vst3SandboxState::Running ||
        (last_heartbeat_ms_ != 0U && now_ms < last_heartbeat_ms_)) {
        return false;
    }
    last_heartbeat_ms_ = now_ms;
    return true;
}

void Vst3SandboxProcess::quarantine(const Vst3SandboxDiagnosticReasonV1 reason) noexcept {
    state_ = Vst3SandboxState::Quarantined;
    diagnostic_reason_ = reason;
    if (job_handle_ != nullptr) TerminateJobObject(as_handle(job_handle_), 1U);
}

void Vst3SandboxProcess::record_crash_entry(
    const Vst3CrashReportReasonV1 reason,
    const std::uint32_t exit_code,
    const std::uint64_t now_ms) noexcept {
    Vst3CrashReportEntryV1 entry{};
    // Wall-clock UTC capture instant (SPEC-0008 requires a non-zero UTC
    // epoch); uptime below keeps using the injected monotonic clock so the
    // two clocks never mix.
    entry.captured_utc = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
    entry.reason = reason;
    entry.exit_code = exit_code;
    // Uptime derived from the monotonic heartbeat clock: delta between now
    // and launch. Zero if the clock went backwards or was not initialized.
    entry.uptime_ms = now_ms > launched_at_ms_ ? now_ms - launched_at_ms_ : 0U;
    entry.module_sha256 = module_digest_;
    static_cast<void>(crash_reports_.append(entry));
}

bool Vst3SandboxProcess::poll_watchdog(const std::uint64_t now_ms) noexcept {
    if (state_ != Vst3SandboxState::Running || process_handle_ == nullptr) return false;
    if (WaitForSingleObject(as_handle(process_handle_), 0U) == WAIT_OBJECT_0) {
        std::uint32_t worker_exit_code = 0U;
        DWORD raw_exit_code = 0U;
        if (GetExitCodeProcess(as_handle(process_handle_), &raw_exit_code) != FALSE &&
            raw_exit_code != 0U && raw_exit_code != STILL_ACTIVE) {
            worker_exit_code = static_cast<std::uint32_t>(raw_exit_code);
        }
        quarantine(Vst3SandboxDiagnosticReasonV1::WorkerExited);
        if (worker_exit_code != 0U) {
            record_crash_entry(Vst3CrashReportReasonV1::worker_exit_nonzero,
                               worker_exit_code, now_ms);
        }
        return true;
    }
    if (last_heartbeat_ms_ == 0U || now_ms < last_heartbeat_ms_ ||
        now_ms - last_heartbeat_ms_ <= watchdog_timeout_ms_) {
        return false;
    }
    quarantine(Vst3SandboxDiagnosticReasonV1::WatchdogTimeout);
    record_crash_entry(Vst3CrashReportReasonV1::worker_timeout, 0U, now_ms);
    return true;
}

bool Vst3SandboxProcess::wait_for_worker(const std::uint32_t timeout_ms) noexcept {
    if (state_ != Vst3SandboxState::Running || !worker_pipe_.server_ready()) return false;
    return worker_pipe_.wait_for_client(timeout_ms);
}

bool Vst3SandboxProcess::send_worker_frame(const std::span<const std::uint8_t> frame) noexcept {
    if (state_ != Vst3SandboxState::Running || !worker_pipe_.connected() ||
        !worker_pipe_.send(frame)) {
        if (state_ == Vst3SandboxState::Running) {
            quarantine(Vst3SandboxDiagnosticReasonV1::WorkerExchangeFailed);
            record_crash_entry(Vst3CrashReportReasonV1::pipe_failure, 0U,
                               last_heartbeat_ms_);
        }
        return false;
    }
    return true;
}

bool Vst3SandboxProcess::receive_worker_frame(const std::span<std::uint8_t> destination,
                                              std::size_t& bytes_read) noexcept {
    if (state_ != Vst3SandboxState::Running || !worker_pipe_.connected()) {
        bytes_read = 0U;
        if (state_ == Vst3SandboxState::Running) {
            quarantine(Vst3SandboxDiagnosticReasonV1::WorkerExchangeFailed);
        }
        return false;
    }
    if (!worker_pipe_.receive(destination, bytes_read)) {
        quarantine(Vst3SandboxDiagnosticReasonV1::WorkerExchangeFailed);
        record_crash_entry(Vst3CrashReportReasonV1::pipe_failure, 0U,
                           last_heartbeat_ms_);
        return false;
    }
    return true;
}

}  // namespace hibiki

#else

namespace hibiki {

Vst3SandboxProcess::~Vst3SandboxProcess() = default;

bool Vst3SandboxProcess::launch(const Vst3SandboxLaunchV1&) {
    stop();
    quarantine(Vst3SandboxDiagnosticReasonV1::UnsupportedPlatform);
    return false;
}

bool validate_vst3_sandbox_launch_v1(const Vst3SandboxLaunchV1& launch_config) noexcept {
    if (launch_config.worker_executable.empty() || launch_config.plugin_path.empty() ||
        launch_config.watchdog_timeout_ms == 0U || launch_config.watchdog_timeout_ms > 5000U ||
        (!launch_config.worker_pipe_name.empty() && launch_config.worker_pipe_timeout_ms == 0U)) {
        return false;
    }
    if (launch_config.vst3_class_id.empty()) return true;
    if (!std::isfinite(launch_config.vst3_sample_rate) ||
        launch_config.vst3_sample_rate < 8000.0 || launch_config.vst3_sample_rate > 384000.0) {
        return false;
    }
    return launch_config.vst3_channels == 2U || launch_config.vst3_channels == 6U ||
           launch_config.vst3_channels == 8U;
}

void Vst3SandboxProcess::stop() noexcept {
    state_ = Vst3SandboxState::Stopped;
    diagnostic_reason_ = Vst3SandboxDiagnosticReasonV1::None;
    last_heartbeat_ms_ = 0;
}

bool Vst3SandboxProcess::mark_heartbeat(const std::uint64_t) noexcept { return false; }

bool Vst3SandboxProcess::poll_watchdog(const std::uint64_t) noexcept { return false; }

bool Vst3SandboxProcess::wait_for_worker(std::uint32_t) noexcept { return false; }
bool Vst3SandboxProcess::send_worker_frame(std::span<const std::uint8_t>) noexcept { return false; }
bool Vst3SandboxProcess::receive_worker_frame(std::span<std::uint8_t>, std::size_t& bytes_read) noexcept {
    bytes_read = 0U;
    return false;
}

void Vst3SandboxProcess::quarantine(const Vst3SandboxDiagnosticReasonV1 reason) noexcept {
    state_ = Vst3SandboxState::Quarantined;
    diagnostic_reason_ = reason;
}
void Vst3SandboxProcess::close_handles() noexcept {}

}  // namespace hibiki

#endif

namespace hibiki {
namespace {

constexpr std::size_t kWorkerMaxPacketBytesV1 =
    kVst3WorkerHeaderBytesV1 + kVst3WorkerMaxPayloadBytesV1;

bool valid_worker_layout(const std::uint32_t channels,
                         const std::uint32_t frames,
                         const std::span<const float> input,
                         const std::span<float> output) noexcept {
    if ((channels != 2U && channels != 6U && channels != 8U) || frames == 0U ||
        frames > kVst3WorkerMaxFramesV1) {
        return false;
    }
    const auto sample_count = static_cast<std::size_t>(channels) * frames;
    return input.size() == sample_count && output.size() == sample_count;
}

bool make_worker_control_frame(const Vst3WorkerMessageTypeV1 type,
                               const std::uint64_t request_id,
                               std::array<std::uint8_t, kVst3WorkerHeaderBytesV1>& packet) noexcept {
    Vst3WorkerFrameV1 frame{};
    frame.type = type;
    frame.request_id = request_id;
    std::size_t bytes_written = 0U;
    return encode_vst3_worker_frame_v1(frame, packet, bytes_written) &&
           bytes_written == packet.size();
}

}  // namespace

Vst3WorkerExchangeResultV1 Vst3SandboxProcess::handshake_worker(
    const std::uint64_t request_id) {
    if (state_ != Vst3SandboxState::Running) return Vst3WorkerExchangeResultV1::not_running;
    if (!worker_pipe_.connected()) {
        quarantine(Vst3SandboxDiagnosticReasonV1::WorkerExchangeFailed);
        return Vst3WorkerExchangeResultV1::not_connected;
    }
    if (request_id == 0U) return Vst3WorkerExchangeResultV1::invalid_argument;

    std::array<std::uint8_t, kVst3WorkerHeaderBytesV1> request{};
    if (!make_worker_control_frame(Vst3WorkerMessageTypeV1::Hello, request_id, request) ||
        !send_worker_frame(request)) {
        return Vst3WorkerExchangeResultV1::send_failed;
    }
    std::array<std::uint8_t, kVst3WorkerHeaderBytesV1> response{};
    std::size_t bytes_read = 0U;
    if (!receive_worker_frame(response, bytes_read)) return Vst3WorkerExchangeResultV1::receive_failed;
    Vst3WorkerFrameV1 frame{};
    Vst3WorkerProtocolErrorV1 error{Vst3WorkerProtocolErrorV1::None};
    if (!decode_vst3_worker_frame_v1(
            std::span<const std::uint8_t>(response.data(), bytes_read), frame, error) ||
        frame.request_id != request_id || frame.payload_bytes != 0U) {
        quarantine(Vst3SandboxDiagnosticReasonV1::WorkerExchangeFailed);
        record_crash_entry(Vst3CrashReportReasonV1::protocol_error, 0U,
                           last_heartbeat_ms_);
        return Vst3WorkerExchangeResultV1::invalid_response;
    }
    if (frame.type == Vst3WorkerMessageTypeV1::Error) {
        quarantine(Vst3SandboxDiagnosticReasonV1::WorkerExchangeFailed);
        record_crash_entry(Vst3CrashReportReasonV1::protocol_error, 0U,
                           last_heartbeat_ms_);
        return Vst3WorkerExchangeResultV1::worker_error;
    }
    if (frame.type != Vst3WorkerMessageTypeV1::HelloAck) {
        quarantine(Vst3SandboxDiagnosticReasonV1::WorkerExchangeFailed);
        return Vst3WorkerExchangeResultV1::invalid_response;
    }
    return Vst3WorkerExchangeResultV1::ok;
}

Vst3WorkerExchangeResultV1 Vst3SandboxProcess::process_worker_block(
    const std::uint64_t request_id,
    const std::uint32_t channels,
    const std::uint32_t frames,
    const std::span<const float> input,
    const std::span<float> output,
    const std::span<const Vst3WorkerParameterPointV1> parameters) {
    if (state_ != Vst3SandboxState::Running) return Vst3WorkerExchangeResultV1::not_running;
    if (!worker_pipe_.connected()) {
        quarantine(Vst3SandboxDiagnosticReasonV1::WorkerExchangeFailed);
        return Vst3WorkerExchangeResultV1::not_connected;
    }
    if (request_id == 0U || !valid_worker_layout(channels, frames, input, output) ||
        parameters.size() > kVst3WorkerMaxParameterPointsV1) {
        return Vst3WorkerExchangeResultV1::invalid_argument;
    }
    std::fill(output.begin(), output.end(), 0.0F);
    for (const auto sample : input) {
        if (!std::isfinite(sample)) return Vst3WorkerExchangeResultV1::invalid_argument;
    }

    const auto sample_count = static_cast<std::size_t>(channels) * frames;
    try {
        const auto parameter_bytes = kVst3WorkerParameterPrefixBytesV1 +
            parameters.size() * kVst3WorkerParameterPointBytesV1;
        const auto payload_bytes = parameter_bytes + sample_count * sizeof(float);
        if (payload_bytes > kVst3WorkerMaxPayloadBytesV1 || payload_bytes > UINT32_MAX) {
            return Vst3WorkerExchangeResultV1::invalid_argument;
        }
        const auto wire_payload_bytes = parameters.empty() ? sample_count * sizeof(float)
                                                            : payload_bytes;
        std::vector<std::uint8_t> request(kVst3WorkerHeaderBytesV1 + wire_payload_bytes, 0U);
        Vst3WorkerFrameV1 request_frame{};
        request_frame.type = parameters.empty()
                                 ? Vst3WorkerMessageTypeV1::ProcessBlock
                                 : Vst3WorkerMessageTypeV1::ProcessBlockWithParameters;
        request_frame.request_id = request_id;
        request_frame.channels = channels;
        request_frame.frames = frames;
        request_frame.payload_bytes = static_cast<std::uint32_t>(
            wire_payload_bytes);
        std::size_t bytes_written = 0U;
        if (parameters.empty()) {
            if (!encode_vst3_worker_frame_v1(
                    request_frame,
                    std::span<std::uint8_t>(request.data(), kVst3WorkerHeaderBytesV1),
                    bytes_written)) {
                return Vst3WorkerExchangeResultV1::invalid_argument;
            }
            std::memcpy(request.data() + kVst3WorkerHeaderBytesV1, input.data(),
                        sample_count * sizeof(float));
        } else {
            if (!encode_vst3_worker_parameter_frame_v1(
                    request_frame, parameters, input, request, bytes_written)) {
                return Vst3WorkerExchangeResultV1::invalid_argument;
            }
        }
        if (!send_worker_frame(request)) return Vst3WorkerExchangeResultV1::send_failed;

        std::vector<std::uint8_t> response(kWorkerMaxPacketBytesV1, 0U);
        std::size_t bytes_read = 0U;
        if (!receive_worker_frame(response, bytes_read)) {
            return Vst3WorkerExchangeResultV1::receive_failed;
        }
        Vst3WorkerFrameV1 response_frame{};
        Vst3WorkerProtocolErrorV1 protocol_error{Vst3WorkerProtocolErrorV1::None};
        const auto response_packet = std::span<const std::uint8_t>(response.data(), bytes_read);
        if (!decode_vst3_worker_frame_v1(response_packet, response_frame, protocol_error) ||
            response_frame.request_id != request_id) {
            quarantine(Vst3SandboxDiagnosticReasonV1::WorkerExchangeFailed);
            record_crash_entry(Vst3CrashReportReasonV1::protocol_error, 0U,
                               last_heartbeat_ms_);
            return Vst3WorkerExchangeResultV1::invalid_response;
        }
        if (response_frame.type == Vst3WorkerMessageTypeV1::Error) {
            quarantine(Vst3SandboxDiagnosticReasonV1::WorkerExchangeFailed);
            record_crash_entry(Vst3CrashReportReasonV1::protocol_error, 0U,
                               last_heartbeat_ms_);
            return Vst3WorkerExchangeResultV1::worker_error;
        }
        std::span<const float> response_samples;
        if (response_frame.type != Vst3WorkerMessageTypeV1::ProcessBlockResponse ||
            response_frame.channels != channels || response_frame.frames != frames ||
            !validate_vst3_worker_audio_frame_v1(response_packet, response_frame,
                                                 response_samples, protocol_error) ||
            response_samples.size() != sample_count) {
            quarantine(Vst3SandboxDiagnosticReasonV1::WorkerExchangeFailed);
            return Vst3WorkerExchangeResultV1::invalid_response;
        }
        std::memcpy(output.data(), response_samples.data(), sample_count * sizeof(float));
        for (const auto sample : output) {
            if (!std::isfinite(sample)) {
                quarantine(Vst3SandboxDiagnosticReasonV1::WorkerExchangeFailed);
                return Vst3WorkerExchangeResultV1::non_finite_output;
            }
        }
        return Vst3WorkerExchangeResultV1::ok;
    } catch (const std::bad_alloc&) {
        quarantine(Vst3SandboxDiagnosticReasonV1::WorkerExchangeFailed);
        return Vst3WorkerExchangeResultV1::allocation_failed;
    }
}

}  // namespace hibiki
