// SPDX-License-Identifier: GPL-3.0-only

#if !defined(_WIN32)
#error "The live Windows audio-session probe is Windows-only"
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>

#include <cstdint>
#include <cstdio>

#include "hibiki/audio_session_registry.hpp"
#include "hibiki/windows_audio_session_watcher.hpp"

int main() {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
        std::fprintf(stderr, "CoInitializeEx failed: 0x%08lx\n",
                     static_cast<unsigned long>(init));
        return 2;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      IID_PPV_ARGS(&enumerator));
    if (SUCCEEDED(result) && enumerator != nullptr) {
        result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    }
    if (FAILED(result) || device == nullptr) {
        std::printf("sessions=unavailable stage=default_endpoint\n");
        if (device != nullptr) device->Release();
        if (enumerator != nullptr) enumerator->Release();
        if (SUCCEEDED(init)) CoUninitialize();
        return 0;
    }

    hibiki::WindowsAudioSessionWatcher watcher;
    result = watcher.bind(device);
    hibiki::AudioSessionRegistry registry;
    if (SUCCEEDED(result)) result = watcher.enumerate(registry);

    std::uint32_t active = 0U;
    for (const auto& session : registry.sessions()) {
        if (session.active) ++active;
    }
    const bool passed = SUCCEEDED(result);
    std::printf("sessions=%s count=%zu active=%u route_identity=endpoint+session_instance\n",
                passed ? "pass" : "fail", registry.sessions().size(), active);

    watcher.unbind();
    device->Release();
    enumerator->Release();
    if (SUCCEEDED(init)) CoUninitialize();
    return passed ? 0 : 5;
}
