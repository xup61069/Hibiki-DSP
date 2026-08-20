// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_sandbox.hpp"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
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

Vst3SandboxProcess::~Vst3SandboxProcess() { stop(); }

bool Vst3SandboxProcess::launch(const Vst3SandboxLaunchV1& launch_config) {
    stop();
    if (launch_config.worker_executable.empty() || launch_config.plugin_path.empty() ||
        launch_config.watchdog_timeout_ms == 0U || launch_config.watchdog_timeout_ms > 5000U ||
        (!launch_config.worker_pipe_name.empty() && launch_config.worker_pipe_timeout_ms == 0U)) {
        state_ = Vst3SandboxState::Quarantined;
        return false;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) {
        state_ = Vst3SandboxState::Quarantined;
        return false;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) == FALSE) {
        CloseHandle(job);
        state_ = Vst3SandboxState::Quarantined;
        return false;
    }

    auto command_line = quote_argument(launch_config.worker_executable) + L" --plugin " +
                         quote_argument(launch_config.plugin_path);
    if (!launch_config.worker_pipe_name.empty()) {
        if (!worker_pipe_.create_server(Vst3WorkerPipeConfigV1{
                launch_config.worker_pipe_name, 1024U * 1024U, launch_config.worker_pipe_timeout_ms})) {
            CloseHandle(job);
            state_ = Vst3SandboxState::Quarantined;
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
        state_ = Vst3SandboxState::Quarantined;
        return false;
    }
    CloseHandle(process_info.hThread);
    process_handle_ = as_pointer(process_info.hProcess);
    job_handle_ = as_pointer(job);
    watchdog_timeout_ms_ = launch_config.watchdog_timeout_ms;
    last_heartbeat_ms_ = launch_config.start_time_ms == 0U ? 1U : launch_config.start_time_ms;
    state_ = Vst3SandboxState::Running;
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
    last_heartbeat_ms_ = 0;
}

bool Vst3SandboxProcess::mark_heartbeat(const std::uint64_t now_ms) noexcept {
    if (state_ != Vst3SandboxState::Running ||
        (last_heartbeat_ms_ != 0U && now_ms < last_heartbeat_ms_)) {
        return false;
    }
    last_heartbeat_ms_ = now_ms;
    return true;
}

void Vst3SandboxProcess::quarantine() noexcept {
    state_ = Vst3SandboxState::Quarantined;
    if (job_handle_ != nullptr) TerminateJobObject(as_handle(job_handle_), 1U);
}

bool Vst3SandboxProcess::poll_watchdog(const std::uint64_t now_ms) noexcept {
    if (state_ != Vst3SandboxState::Running || process_handle_ == nullptr) return false;
    if (WaitForSingleObject(as_handle(process_handle_), 0U) == WAIT_OBJECT_0) {
        quarantine();
        return true;
    }
    if (last_heartbeat_ms_ == 0U || now_ms < last_heartbeat_ms_ ||
        now_ms - last_heartbeat_ms_ <= watchdog_timeout_ms_) {
        return false;
    }
    quarantine();
    return true;
}

bool Vst3SandboxProcess::wait_for_worker(const std::uint32_t timeout_ms) noexcept {
    if (state_ != Vst3SandboxState::Running || !worker_pipe_.server_ready()) return false;
    return worker_pipe_.wait_for_client(timeout_ms);
}

bool Vst3SandboxProcess::send_worker_frame(const std::span<const std::uint8_t> frame) noexcept {
    if (state_ != Vst3SandboxState::Running || !worker_pipe_.connected() ||
        !worker_pipe_.send(frame)) {
        return false;
    }
    return true;
}

bool Vst3SandboxProcess::receive_worker_frame(const std::span<std::uint8_t> destination,
                                              std::size_t& bytes_read) noexcept {
    if (state_ != Vst3SandboxState::Running || !worker_pipe_.connected()) {
        bytes_read = 0U;
        return false;
    }
    return worker_pipe_.receive(destination, bytes_read);
}

}  // namespace hibiki

#else

namespace hibiki {

Vst3SandboxProcess::~Vst3SandboxProcess() = default;

bool Vst3SandboxProcess::launch(const Vst3SandboxLaunchV1&) {
    state_ = Vst3SandboxState::Quarantined;
    return false;
}

void Vst3SandboxProcess::stop() noexcept {
    state_ = Vst3SandboxState::Stopped;
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

void Vst3SandboxProcess::quarantine() noexcept { state_ = Vst3SandboxState::Quarantined; }
void Vst3SandboxProcess::close_handles() noexcept {}

}  // namespace hibiki

#endif
