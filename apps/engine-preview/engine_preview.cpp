// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/audio_engine.hpp"
#include "hibiki/control_status.hpp"
#include "hibiki/control_service.hpp"
#include "hibiki/engine_control.hpp"
#include "hibiki/windows_device_catalog.hpp"
#include "hibiki/windows_volume_broker.hpp"
#include "hibiki/windows_volume_link.hpp"
#include "hibiki/wav_ir.hpp"

#include <Windows.h>
#include <mmdeviceapi.h>

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

struct SystemVolumeState final {
    hibiki::WindowsVolumeBroker broker{};
    hibiki::WindowsVolumeLinkV1 link{};
    bool enabled{false};
    bool bound{false};
    bool have_last_engine_state{false};
    hibiki::OutputGroupVolumeStateV1 last_engine_state{};
};

bool has_command_line_flag(const int argc,
                           wchar_t* const* argv,
                           const std::wstring_view flag) noexcept {
    if (argv == nullptr || flag.empty()) return false;
    for (int index = 1; index < argc; ++index) {
        if (argv[index] != nullptr && std::wstring_view(argv[index]) == flag) return true;
    }
    return false;
}

HRESULT bind_default_volume(IMMDeviceEnumerator* const enumerator,
                            hibiki::WindowsVolumeBroker& broker) noexcept {
    if (enumerator == nullptr) return E_INVALIDARG;
    IMMDevice* device = nullptr;
    const auto result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(result) || device == nullptr) return FAILED(result) ? result : E_FAIL;
    const auto bind_result = broker.bind(device);
    device->Release();
    return bind_result;
}

HRESULT rebind_default_volume_if_changed(IMMDeviceEnumerator* const enumerator,
                                         hibiki::WindowsVolumeBroker& broker,
                                         const bool currently_bound) noexcept {
    if (enumerator == nullptr) return E_INVALIDARG;
    IMMDevice* device = nullptr;
    const auto result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(result) || device == nullptr) return FAILED(result) ? result : E_FAIL;
    const auto bind_result = currently_bound ? broker.bind_if_changed(device)
                                             : broker.bind(device);
    device->Release();
    return bind_result;
}

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
    const hibiki::OutputGroupVolumeStateV1 volume,
    const std::string_view physical_catalog_detail,
    const bool system_volume_enabled,
    const std::string_view system_volume_detail) noexcept {
    hibiki::ControlStatusSnapshotV1 snapshot{};
    snapshot.sequence = 1U;
    snapshot.volume = volume;
    snapshot.route_count = 4U;
    set_route(snapshot.routes[0U], "engine-control", "引擎控制面",
              "named pipe 已啟動；目前為本機 user-space preview。",
              hibiki::ControlRouteHealthStateV1::Ready);
    set_route(snapshot.routes[1U], "main-output", "主輸出",
              physical_catalog_detail,
              hibiki::ControlRouteHealthStateV1::Unavailable, 1U);
    set_route(snapshot.routes[2U], "windows-volume", "Windows 音量",
              system_volume_detail,
              system_volume_enabled ? hibiki::ControlRouteHealthStateV1::Ready
                                     : hibiki::ControlRouteHealthStateV1::Unavailable,
              system_volume_enabled ? 0U : 1U);
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

bool same_route(const hibiki::ControlRouteHealthEntryV1& left,
                const hibiki::ControlRouteHealthEntryV1& right) noexcept {
    return left.id_bytes == right.id_bytes && left.name_bytes == right.name_bytes &&
           left.detail_bytes == right.detail_bytes && left.state == right.state &&
           left.flags == right.flags && left.id == right.id && left.name == right.name &&
           left.detail == right.detail;
}

BOOL WINAPI on_console_control(const DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT) {
        g_stop.store(true, std::memory_order_release);
        return TRUE;
    }
    return FALSE;
}

}  // namespace

int wmain(const int argc, wchar_t* const* argv) {
    constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\HibikiDSP_v1_control";
    if (SetConsoleCtrlHandler(on_console_control, TRUE) == FALSE) return 2;
    const bool system_volume_requested =
        has_command_line_flag(argc, argv, L"--enable-system-volume");

    hibiki::AudioEngineModel engine;
    hibiki::EngineControlWorkerV1 control_worker{engine};
    IrPrepareState ir_state{&engine};
    control_worker.set_ir_prepare_handler(prepare_ir_file, &ir_state);
    SystemVolumeState system_volume;
    system_volume.enabled = system_volume_requested;
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com_initialized = SUCCEEDED(com_result) || com_result == RPC_E_CHANGED_MODE;
    IMMDeviceEnumerator* device_enumerator = nullptr;
    hibiki::WindowsPhysicalDeviceCatalogServiceV1 physical_catalog;
    bool physical_catalog_ready = false;
    HRESULT catalog_result = com_result;
    if (com_initialized) {
        catalog_result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                          IID_PPV_ARGS(&device_enumerator));
        if (SUCCEEDED(catalog_result) && device_enumerator != nullptr) {
            catalog_result = physical_catalog.bind(device_enumerator);
            if (SUCCEEDED(catalog_result)) {
                catalog_result = physical_catalog.refresh_now();
                physical_catalog_ready = SUCCEEDED(catalog_result);
            }
        }
    }
    const std::string catalog_detail = physical_catalog_ready
        ? "physical catalog ready; Preview sink disabled."
        : "physical catalog unavailable; safe Preview retained.";
    std::string system_volume_detail = system_volume_requested
        ? "system volume link requested; binding Windows endpoint..."
        : "system volume link disabled; Preview will not write Windows volume.";
    if (system_volume_requested && device_enumerator != nullptr) {
        const auto volume_result = bind_default_volume(device_enumerator, system_volume.broker);
        system_volume.bound = SUCCEEDED(volume_result);
        if (system_volume.bound) {
            hibiki::OutputGroupVolumeStateV1 initial_volume{};
            if (SUCCEEDED(system_volume.broker.read_state(initial_volume))) {
                const hibiki::VolumeNotificationV1 notification{
                    initial_volume.requested_db, initial_volume.mute, initial_volume.generation};
                if (engine.apply_windows_volume("main", notification) ==
                    hibiki::VolumeNotificationResult::Accepted) {
                    system_volume.last_engine_state = engine.volume();
                    system_volume.have_last_engine_state = true;
                    system_volume_detail =
                        "system endpoint volume linked; write-through explicitly enabled.";
                } else {
                    system_volume.broker.unbind();
                    system_volume.bound = false;
                    system_volume_detail =
                        "system endpoint read failed; write-through disabled.";
                }
            } else {
                system_volume.broker.unbind();
                system_volume.bound = false;
                system_volume_detail = "system endpoint read failed; write-through disabled.";
            }
        } else {
            system_volume_detail = "system endpoint unavailable; write-through disabled.";
        }
    }
    const bool system_volume_active = system_volume_requested && system_volume.bound;
    hibiki::ControlStatusSnapshotStoreV1 status_store;
    auto status = make_initial_status(engine.volume(), catalog_detail, system_volume_active,
                                      system_volume_detail);
    if (!status_store.publish(status)) return 4;
    hibiki::ControlPlaneHostV1 host;
    if (!host.start_with_queue(hibiki::IpcNamedPipeConfigV1{kPipeName, 64U * 1024U, 1000U},
                               physical_catalog_ready ? physical_catalog.snapshot_store() : nullptr,
                               &status_store, nullptr)) {
        physical_catalog.unbind();
        system_volume.broker.unbind();
        if (device_enumerator != nullptr) device_enumerator->Release();
        if (com_initialized && com_result != RPC_E_CHANGED_MODE) CoUninitialize();
        return 3;
    }

    auto next_catalog_poll = std::chrono::steady_clock::now() + std::chrono::milliseconds{250};
    auto next_volume_poll = std::chrono::steady_clock::now() + std::chrono::milliseconds{50};
    auto next_volume_write = std::chrono::steady_clock::now();
    while (!g_stop.load(std::memory_order_acquire)) {
        const auto now = std::chrono::steady_clock::now();
        if (system_volume_requested && device_enumerator != nullptr && now >= next_volume_poll) {
            const auto bind_result = rebind_default_volume_if_changed(
                device_enumerator, system_volume.broker, system_volume.bound);
            if (bind_result == S_OK) {
                system_volume.bound = true;
                hibiki::OutputGroupVolumeStateV1 rebound{};
                if (SUCCEEDED(system_volume.broker.read_state(rebound))) {
                    const hibiki::VolumeNotificationV1 notification{
                        rebound.requested_db, rebound.mute, rebound.generation};
                    if (engine.apply_windows_volume("main", notification) ==
                        hibiki::VolumeNotificationResult::Accepted) {
                        system_volume.last_engine_state = engine.volume();
                        system_volume.have_last_engine_state = true;
                    }
                }
            } else if (FAILED(bind_result)) {
                system_volume.broker.unbind();
                system_volume.bound = false;
            }
            next_volume_poll = now + std::chrono::milliseconds{250};
        }
        if (system_volume_requested && system_volume.bound) {
            hibiki::WindowsVolumeNotificationSnapshotV1 notification{};
            if (system_volume.broker.poll(notification)) {
                (void)system_volume.link.apply(engine, "main", notification);
                system_volume.last_engine_state = engine.volume();
                system_volume.have_last_engine_state = true;
            }
        }
        (void)control_worker.drain(host.command_queue());
        if (system_volume_requested && system_volume.bound &&
            system_volume.have_last_engine_state && now >= next_volume_write) {
            const auto current_engine_state = engine.volume();
            if (!same_volume(current_engine_state, system_volume.last_engine_state)) {
                const auto write_result = system_volume.broker.write(
                    current_engine_state, hibiki::WindowsVolumeEventContextsV1::ui());
                if (SUCCEEDED(write_result)) {
                    system_volume.last_engine_state = current_engine_state;
                    next_volume_write = now;
                } else {
                    next_volume_write = now + std::chrono::milliseconds{100};
                    if (write_result == AUDCLNT_E_DEVICE_INVALIDATED) {
                        system_volume.broker.unbind();
                        system_volume.bound = false;
                    }
                }
            }
        }
        if (physical_catalog_ready && std::chrono::steady_clock::now() >= next_catalog_poll) {
            (void)physical_catalog.poll_and_refresh();
            next_catalog_poll = std::chrono::steady_clock::now() +
                                std::chrono::milliseconds{250};
        }
        const auto volume = engine.volume();
        bool status_changed = false;
        if (!same_volume(volume, status.volume)) {
            status.volume = volume;
            status_changed = true;
        }
        const bool volume_route_ready = system_volume_requested && system_volume.bound;
        const auto volume_detail = volume_route_ready
            ? std::string_view("system endpoint volume linked; write-through explicitly enabled.")
            : (system_volume_requested
                   ? std::string_view("system endpoint unavailable; write-through disabled.")
                   : std::string_view("system volume link disabled; Preview will not write Windows volume."));
        const auto previous_volume_route = status.routes[2U];
        set_route(status.routes[2U], "windows-volume", "Windows 音量", volume_detail,
                  volume_route_ready ? hibiki::ControlRouteHealthStateV1::Ready
                                      : hibiki::ControlRouteHealthStateV1::Unavailable,
                  volume_route_ready ? 0U : 1U);
        if (!same_route(previous_volume_route, status.routes[2U])) status_changed = true;
        if (status_changed) {
            ++status.sequence;
            (void)status_store.publish(status);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    host.stop();
    physical_catalog.unbind();
    system_volume.broker.unbind();
    if (device_enumerator != nullptr) device_enumerator->Release();
    if (com_initialized && com_result != RPC_E_CHANGED_MODE) CoUninitialize();
    return 0;
}
