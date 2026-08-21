// SPDX-License-Identifier: GPL-3.0-only

#if !defined(_WIN32)
#error "This probe is Windows-only"
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>

#include "hibiki/audio_engine.hpp"
#include "hibiki/windows_volume_broker.hpp"
#include "hibiki/windows_volume_link.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

namespace {

bool close_db(const double left, const double right) noexcept {
    return std::isfinite(left) && std::isfinite(right) && std::abs(left - right) <= 0.5;
}

}  // namespace

int wmain() {
    HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com_initialized = SUCCEEDED(com_result) || com_result == RPC_E_CHANGED_MODE;
    if (!com_initialized) {
        std::printf("volume_live=unavailable reason=com-init hr=0x%08lx\n",
                    static_cast<unsigned long>(com_result));
        return 0;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    hibiki::WindowsVolumeBroker broker;
    hibiki::OutputGroupVolumeStateV1 original{};
    bool changed = false;
    bool passed = false;
    double attenuated_db = -144.0;
    double restored_db = -144.0;
    auto restore = [&]() noexcept -> bool {
        if (!changed) return true;
        auto state = original;
        state.generation = std::max(original.generation + 1U, original.generation);
        const auto write_result = broker.write(
            state, hibiki::WindowsVolumeEventContextsV1::ui());
        if (FAILED(write_result)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        hibiki::OutputGroupVolumeStateV1 readback{};
        if (FAILED(broker.read_state(readback))) return false;
        restored_db = readback.requested_db;
        changed = false;
        return close_db(readback.requested_db, original.requested_db) &&
               readback.mute == original.mute;
    };

    do {
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    IID_PPV_ARGS(&enumerator))) ||
            enumerator == nullptr ||
            FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) ||
            device == nullptr || FAILED(broker.bind(device)) ||
            FAILED(broker.read_state(original))) {
            std::printf("volume_live=unavailable reason=endpoint-or-broker\n");
            break;
        }

        auto target = original;
        target.requested_db = std::max(-144.0, original.requested_db - 3.0);
        target.safety_ceiling_db = 0.0;
        target.generation = original.generation + 1U;
        if (FAILED(broker.write(target, hibiki::WindowsVolumeEventContextsV1::ui()))) {
            std::printf("volume_live=fail reason=attenuation-write\n");
            break;
        }
        changed = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        hibiki::OutputGroupVolumeStateV1 attenuated{};
        if (FAILED(broker.read_state(attenuated))) {
            std::printf("volume_live=fail reason=attenuation-read\n");
            break;
        }
        attenuated_db = attenuated.requested_db;
        if (!close_db(attenuated.requested_db, target.requested_db) ||
            attenuated.requested_db > original.requested_db + 0.5 ||
            attenuated.mute != original.mute) {
            std::printf("volume_live=fail reason=attenuation-readback\n");
            break;
        }
        passed = restore();
        if (!passed) {
            std::printf("volume_live=fail reason=restore\n");
            break;
        }
        std::printf("volume_live=pass original_db=%.2f attenuated_db=%.2f restored_db=%.2f\n",
                    original.requested_db, attenuated_db, restored_db);
    } while (false);

    if (!restore()) passed = false;
    broker.unbind();
    if (device != nullptr) device->Release();
    if (enumerator != nullptr) enumerator->Release();
    if (com_initialized && com_result != RPC_E_CHANGED_MODE) CoUninitialize();
    return passed ? 0 : 1;
}
