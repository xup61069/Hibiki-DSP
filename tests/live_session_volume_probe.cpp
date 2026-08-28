// SPDX-License-Identifier: GPL-3.0-only

#if !defined(_WIN32)
#error "The live Windows session-volume probe is Windows-only"
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <thread>

#include "hibiki/session_catalog.hpp"
#include "hibiki/windows_audio_session_route.hpp"

namespace {

constexpr std::wstring_view kSessionName = L"Hibiki live session volume probe";

bool close_db(const double left, const double right) noexcept {
    return std::isfinite(left) && std::isfinite(right) && std::abs(left - right) <= 0.5;
}

// Start a silent shared-mode stream so Windows creates a real session for this
// probe process. No sample data is written; every submitted block is marked
// AUDCLNT_BUFFERFLAGS_SILENT.
HRESULT start_silent_session(IMMDevice* const device,
                             IAudioClient** client_out,
                             IAudioRenderClient** render_out) noexcept {
    if (device == nullptr || client_out == nullptr || render_out == nullptr) return E_INVALIDARG;
    *client_out = nullptr;
    *render_out = nullptr;

    IAudioClient* client = nullptr;
    WAVEFORMATEX* format = nullptr;
    IAudioRenderClient* render = nullptr;
    HRESULT result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                      reinterpret_cast<void**>(&client));
    if (SUCCEEDED(result)) result = client->GetMixFormat(&format);
    if (SUCCEEDED(result) && format == nullptr) result = E_FAIL;
    if (SUCCEEDED(result)) {
        result = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                     AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                         AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                     200000, 0, format, nullptr);
    }
    if (SUCCEEDED(result)) {
        result = client->GetService(__uuidof(IAudioRenderClient),
                                    reinterpret_cast<void**>(&render));
    }
    if (format != nullptr) CoTaskMemFree(format);
    if (FAILED(result)) {
        if (render != nullptr) render->Release();
        if (client != nullptr) client->Release();
        return result;
    }

    IAudioSessionControl* session_control = nullptr;
    result = client->GetService(__uuidof(IAudioSessionControl),
                                reinterpret_cast<void**>(&session_control));
    if (SUCCEEDED(result)) {
        GUID event_context{};
        (void)CoCreateGuid(&event_context);
        result = session_control->SetDisplayName(kSessionName.data(), &event_context);
    }
    if (session_control != nullptr) session_control->Release();
    if (FAILED(result)) {
        render->Release();
        client->Release();
        return result;
    }

    result = client->Start();
    if (SUCCEEDED(result)) {
        // A few silent blocks are enough for the audio service to publish the
        // session while keeping the probe inaudible.
        for (std::uint32_t attempt = 0U; attempt < 20U; ++attempt) {
            UINT32 buffer_frames = 0U;
            UINT32 padding = 0U;
            result = client->GetBufferSize(&buffer_frames);
            if (SUCCEEDED(result)) result = client->GetCurrentPadding(&padding);
            if (SUCCEEDED(result) && buffer_frames > padding) {
                BYTE* data = nullptr;
                const UINT32 available = buffer_frames - padding;
                result = render->GetBuffer(available, &data);
                if (SUCCEEDED(result)) {
                    result = render->ReleaseBuffer(available, AUDCLNT_BUFFERFLAGS_SILENT);
                }
            }
            if (FAILED(result)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    if (FAILED(result)) {
        (void)client->Stop();
        render->Release();
        client->Release();
        return result;
    }
    *client_out = client;
    *render_out = render;
    return S_OK;
}

}  // namespace

int wmain() {
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com_initialized = SUCCEEDED(com_result) || com_result == RPC_E_CHANGED_MODE;
    if (!com_initialized) {
        std::printf("session_volume_live=unavailable reason=com-init\n");
        return 0;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    hibiki::WindowsAudioSessionRouteCoordinatorV1 coordinator;
    // Arm cleanup before issuing the write. A successful write can lose its
    // later readback, so cleanup must assume the temporary session changed
    // until restoration is confirmed.
    bool restore_required = false;
    bool passed = false;
    bool unavailable = false;
    double original_db = -144.0;
    double attenuated_db = -144.0;
    double restored_db = -144.0;
    bool original_mute = false;
    std::uint64_t target_handle = 0U;

    auto restore = [&]() noexcept -> bool {
        if (!restore_required) return true;
        GUID event_context{};
        (void)CoCreateGuid(&event_context);
        const HRESULT write_result = coordinator.write_session_volume_handle(
            target_handle, original_db, original_mute, event_context);
        if (FAILED(write_result)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        double read_db = -144.0;
        bool read_mute = false;
        if (FAILED(coordinator.read_session_volume_handle(target_handle, read_db, read_mute))) {
            return false;
        }
        restored_db = read_db;
        if (!close_db(read_db, original_db) || read_mute != original_mute) {
            return false;
        }
        restore_required = false;
        return true;
    };

    do {
        HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                          IID_PPV_ARGS(&enumerator));
        if (SUCCEEDED(result)) {
            result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        }
        if (FAILED(result) || device == nullptr) {
            std::printf("session_volume_live=unavailable reason=default-endpoint\n");
            unavailable = true;
            break;
        }

        result = start_silent_session(device, &client, &render);
        if (FAILED(result)) {
            std::printf("session_volume_live=unavailable reason=silent-session\n");
            unavailable = true;
            break;
        }
        result = coordinator.bind(device);
        if (FAILED(result)) {
            std::printf("session_volume_live=unavailable reason=route-bind\n");
            unavailable = true;
            break;
        }
        const auto refresh_result = coordinator.refresh();
        if (refresh_result == hibiki::WindowsAudioSessionRouteRefreshResultV1::Degraded ||
            refresh_result == hibiki::WindowsAudioSessionRouteRefreshResultV1::Unbound) {
            std::printf("session_volume_live=unavailable reason=route-refresh\n");
            unavailable = true;
            break;
        }

        hibiki::SessionCatalogSnapshotV1 catalog{};
        if (!coordinator.make_session_catalog_snapshot(1U, catalog)) {
            std::printf("session_volume_live=unavailable reason=catalog\n");
            unavailable = true;
            break;
        }
        for (std::size_t index = 0U; index < catalog.entry_count; ++index) {
            const auto& entry = catalog.entries[index];
            const std::string_view name(entry.name.data(), entry.name_bytes);
            // The display name is ASCII and intentionally not an endpoint or
            // process identity. It only selects the session created above.
            if (name == "Hibiki live session volume probe" && (entry.flags & 1U) != 0U) {
                target_handle = entry.handle;
                break;
            }
        }
        if (target_handle == 0U) {
            std::printf("session_volume_live=unavailable reason=session-not-published\n");
            unavailable = true;
            break;
        }

        if (FAILED(coordinator.read_session_volume_handle(target_handle, original_db, original_mute))) {
            std::printf("session_volume_live=unavailable reason=initial-read\n");
            unavailable = true;
            break;
        }
        const double target_db = std::max(-144.0, original_db - 3.0);
        GUID event_context{};
        (void)CoCreateGuid(&event_context);
        restore_required = true;
        if (FAILED(coordinator.write_session_volume_handle(
                target_handle, target_db, original_mute, event_context))) {
            std::printf("session_volume_live=fail reason=attenuation-write\n");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        bool attenuated_mute = false;
        if (FAILED(coordinator.read_session_volume_handle(
                target_handle, attenuated_db, attenuated_mute))) {
            std::printf("session_volume_live=fail reason=attenuation-read\n");
            break;
        }
        if (!close_db(attenuated_db, target_db) || attenuated_db > original_db + 0.5 ||
            attenuated_mute != original_mute) {
            std::printf("session_volume_live=fail reason=attenuation-readback\n");
            break;
        }
        passed = restore();
        if (!passed) {
            std::printf("session_volume_live=fail reason=restore\n");
            break;
        }
        std::printf("session_volume_live=pass original_db=%.2f attenuated_db=%.2f restored_db=%.2f\n",
                    original_db, attenuated_db, restored_db);
    } while (false);

    if (!restore()) passed = false;
    coordinator.unbind();
    if (client != nullptr) (void)client->Stop();
    if (render != nullptr) render->Release();
    if (client != nullptr) client->Release();
    if (device != nullptr) device->Release();
    if (enumerator != nullptr) enumerator->Release();
    if (com_initialized && com_result != RPC_E_CHANGED_MODE) CoUninitialize();
    return passed || unavailable ? 0 : 1;
}
