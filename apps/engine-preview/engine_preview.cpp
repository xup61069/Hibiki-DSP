// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/audio_engine.hpp"
#include "hibiki/control_service.hpp"
#include "hibiki/engine_control.hpp"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace {

std::atomic_bool g_stop{false};

BOOL WINAPI on_console_control(const DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT) {
        g_stop.store(true, std::memory_order_release);
        return TRUE;
    }
    return FALSE;
}

}  // namespace

int wmain() {
    constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\HibikiDSP_v1_control";
    if (SetConsoleCtrlHandler(on_console_control, TRUE) == FALSE) return 2;

    hibiki::AudioEngineModel engine;
    hibiki::EngineControlWorkerV1 control_worker{engine};
    hibiki::ControlPlaneHostV1 host;
    if (!host.start_with_queue(hibiki::IpcNamedPipeConfigV1{kPipeName, 64U * 1024U, 1000U})) {
        return 3;
    }

    while (!g_stop.load(std::memory_order_acquire)) {
        (void)control_worker.drain(host.command_queue());
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    host.stop();
    return 0;
}
