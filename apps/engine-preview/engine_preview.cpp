// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/audio_engine.hpp"
#include "hibiki/control_status.hpp"
#include "hibiki/control_service.hpp"
#include "hibiki/engine_control.hpp"
#include "hibiki/wav_ir.hpp"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

std::atomic_bool g_stop{false};

struct IrPrepareState final {
    hibiki::AudioEngineModel* engine{nullptr};
    hibiki::IrPhaseResolutionV1 resolution{};
    std::string path{};
    bool prepared{false};
};

bool prepare_ir_file(const hibiki::IrPrepareCommandV1& request, void* const context) noexcept {
    auto* state = static_cast<IrPrepareState*>(context);
    if (state == nullptr || state->engine == nullptr || request.path_bytes == 0U ||
        request.path_bytes > request.path.size() ||
        request.mode > 3U || request.strength_q16_16 < 0 || request.strength_q16_16 > 65536 ||
        (request.mode == 3U && request.strength_q16_16 != 0)) {
        return false;
    }
    try {
        const std::string path(request.path.data(), request.path_bytes);
        std::ifstream file(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
        if (!file) return false;
        const auto end = file.tellg();
        if (end <= 0 || end > static_cast<std::streamoff>(hibiki::kMaxIrWavBytesV1)) return false;
        const auto size = static_cast<std::size_t>(end);
        std::vector<std::uint8_t> bytes(size);
        file.seekg(0, std::ios::beg);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
            return false;
        }
        const auto decoded = hibiki::decode_ir_wav_v1(bytes);
        if (!decoded.valid ||
            (request.expected_sample_rate != 0U &&
             decoded.data.sample_rate != request.expected_sample_rate) ||
            (request.expected_channels != 0U && decoded.data.channels != request.expected_channels)) {
            return false;
        }
        const auto policy = hibiki::IrPhasePolicyV1{
            1U, static_cast<hibiki::IrPhaseMode>(request.mode),
            static_cast<double>(request.strength_q16_16) / 65536.0};
        const auto resolution = hibiki::resolve_ir_phase_policy(policy);
        if (!resolution.valid || resolution.mode == hibiki::IrPhaseMode::Bypass) return false;
        if (!state->engine->prepare_ir("main", decoded.data, resolution) ||
            !state->engine->commit_ir()) {
            state->engine->rollback_ir();
            return false;
        }
        state->resolution = resolution;
        state->path = path;
        state->prepared = true;
        return true;
    } catch (...) {
        return false;
    }
}

void set_route(hibiki::ControlRouteHealthEntryV1& route,
               const std::string_view id,
               const std::string_view name,
               const std::string_view detail,
               const hibiki::ControlRouteHealthStateV1 state,
               const std::uint16_t flags = 0U) noexcept {
    route = {};
    route.id_bytes = static_cast<std::uint8_t>(id.size());
    route.name_bytes = static_cast<std::uint16_t>(name.size());
    route.detail_bytes = static_cast<std::uint16_t>(detail.size());
    route.state = state;
    route.flags = flags;
    std::copy(id.begin(), id.end(), route.id.begin());
    std::copy(name.begin(), name.end(), route.name.begin());
    std::copy(detail.begin(), detail.end(), route.detail.begin());
}

hibiki::ControlStatusSnapshotV1 make_initial_status(
    const hibiki::OutputGroupVolumeStateV1 volume) noexcept {
    hibiki::ControlStatusSnapshotV1 snapshot{};
    snapshot.sequence = 1U;
    snapshot.volume = volume;
    snapshot.route_count = 4U;
    set_route(snapshot.routes[0U], "engine-control", "引擎控制面",
              "named pipe 已啟動；目前為本機 user-space preview。",
              hibiki::ControlRouteHealthStateV1::Ready);
    set_route(snapshot.routes[1U], "main-output", "主輸出",
              "尚未連接實體 WASAPI／WaveRT sink。",
              hibiki::ControlRouteHealthStateV1::Unavailable, 1U);
    set_route(snapshot.routes[2U], "windows-volume", "Windows 音量",
              "尚未綁定虛擬端點 volume node；只展示安全狀態。",
              hibiki::ControlRouteHealthStateV1::Unavailable, 1U);
    set_route(snapshot.routes[3U], "vendor-asio", "廠商 ASIO",
              "Strict Direct／廠商 ASIO 不會被透明攔截。",
              hibiki::ControlRouteHealthStateV1::Bypassed);
    return snapshot;
}

bool same_volume(const hibiki::OutputGroupVolumeStateV1& left,
                 const hibiki::OutputGroupVolumeStateV1& right) noexcept {
    return left.schema_version == right.schema_version &&
           left.requested_db == right.requested_db &&
           left.safety_ceiling_db == right.safety_ceiling_db &&
           left.effective_db == right.effective_db && left.mute == right.mute &&
           left.generation == right.generation && left.origin == right.origin &&
           left.actuator == right.actuator;
}

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
    IrPrepareState ir_state{&engine};
    control_worker.set_ir_prepare_handler(prepare_ir_file, &ir_state);
    hibiki::ControlStatusSnapshotStoreV1 status_store;
    auto status = make_initial_status(engine.volume());
    if (!status_store.publish(status)) return 4;
    hibiki::ControlPlaneHostV1 host;
    if (!host.start_with_queue(hibiki::IpcNamedPipeConfigV1{kPipeName, 64U * 1024U, 1000U},
                               nullptr, &status_store, nullptr)) {
        return 3;
    }

    while (!g_stop.load(std::memory_order_acquire)) {
        (void)control_worker.drain(host.command_queue());
        const auto volume = engine.volume();
        if (!same_volume(volume, status.volume)) {
            status.volume = volume;
            ++status.sequence;
            (void)status_store.publish(status);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    host.stop();
    return 0;
}
