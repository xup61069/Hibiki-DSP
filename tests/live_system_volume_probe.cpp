// SPDX-License-Identifier: GPL-3.0-only

#if !defined(_WIN32)
#error "This probe is Windows-only"
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>

#include "hibiki/audio_engine.hpp"
#include "hibiki/windows_volume_broker.hpp"
#include "hibiki/windows_volume_link.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <thread>

namespace {

bool close_db(const double left, const double right) noexcept {
    return std::isfinite(left) && std::isfinite(right) && std::abs(left - right) <= 0.5;
}

const char* write_result_category(const HRESULT result) noexcept {
    if (result == E_INVALIDARG) return "invalid-argument";
    if (result == E_ACCESSDENIED) return "access-denied";
    if (result == AUDCLNT_E_DEVICE_INVALIDATED) return "device-invalidated";
    if (result == AUDCLNT_E_SERVICE_NOT_RUNNING) return "service-not-running";
    return "other";
}

enum class WriteTestMode {
    Attenuate,
    Mute,
};

bool choose_safe_write_target(const hibiki::OutputGroupVolumeStateV1& original,
                              const float range_min_db,
                              const float range_max_db,
                              const float range_increment_db,
                              hibiki::OutputGroupVolumeStateV1& target,
                              WriteTestMode& mode) noexcept {
    if (!std::isfinite(range_min_db) || !std::isfinite(range_max_db) ||
        !std::isfinite(range_increment_db) || range_min_db > range_max_db ||
        range_increment_db <= 0.0F ||
        original.requested_db < static_cast<double>(range_min_db) - 0.5 ||
        original.requested_db > static_cast<double>(range_max_db) + 0.5) {
        return false;
    }
    target = original;
    const double minimum = std::max(-144.0, static_cast<double>(range_min_db));
    const double attenuation = std::max(3.0, static_cast<double>(range_increment_db));
    if (original.requested_db > minimum + (static_cast<double>(range_increment_db) * 0.5)) {
        target.requested_db = std::max(minimum, original.requested_db - attenuation);
        mode = WriteTestMode::Attenuate;
        return target.requested_db < original.requested_db;
    }
    if (!original.mute) {
        target.mute = true;
        mode = WriteTestMode::Mute;
        return true;
    }
    return false;
}

bool write_target_self_test() noexcept {
    hibiki::OutputGroupVolumeStateV1 original{};
    hibiki::OutputGroupVolumeStateV1 target{};
    WriteTestMode mode{WriteTestMode::Mute};
    original.requested_db = -20.0;
    if (!choose_safe_write_target(original, -64.0F, 0.0F, 0.5F, target, mode) ||
        mode != WriteTestMode::Attenuate || !close_db(target.requested_db, -23.0) ||
        target.mute) {
        return false;
    }
    original.requested_db = -64.0;
    if (!choose_safe_write_target(original, -64.0F, 0.0F, 0.5F, target, mode) ||
        mode != WriteTestMode::Mute || !close_db(target.requested_db, -64.0) ||
        !target.mute) {
        return false;
    }
    original.mute = true;
    if (choose_safe_write_target(original, -64.0F, 0.0F, 0.5F, target, mode) ||
        choose_safe_write_target(original, 0.0F, -64.0F, 0.5F, target, mode)) {
        return false;
    }
    return true;
}

}  // namespace

int wmain(const int argc, wchar_t* const* argv) {
    if (argc == 2 && argv != nullptr && argv[1] != nullptr &&
        std::wcscmp(argv[1], L"--self-test") == 0) {
        const bool passed = write_target_self_test();
        std::printf("volume_live_selftest=%s cases=4\n", passed ? "pass" : "fail");
        return passed ? 0 : 2;
    }
    HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com_initialized = SUCCEEDED(com_result) || com_result == RPC_E_CHANGED_MODE;
    if (!com_initialized) {
        std::printf("volume_live=unavailable reason=com-init hr=0x%08lx\n",
                    static_cast<unsigned long>(com_result));
        return 0;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioEndpointVolume* endpoint = nullptr;
    hibiki::WindowsVolumeBroker broker;
    hibiki::OutputGroupVolumeStateV1 original{};
    bool changed = false;
    bool passed = false;
    bool unavailable = false;
    double attenuated_db = -144.0;
    double restored_db = -144.0;
    float range_min_db = 0.0F;
    float range_max_db = 0.0F;
    float range_increment_db = 0.0F;
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
            device == nullptr ||
            FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                    reinterpret_cast<void**>(&endpoint))) ||
            endpoint == nullptr ||
            FAILED(endpoint->GetVolumeRange(&range_min_db, &range_max_db,
                                            &range_increment_db)) ||
            FAILED(broker.bind(device)) ||
            FAILED(broker.read_state(original))) {
            std::printf("volume_live=unavailable reason=endpoint-or-broker\n");
            unavailable = true;
            break;
        }

        auto target = original;
        WriteTestMode mode{WriteTestMode::Attenuate};
        if (!choose_safe_write_target(original, range_min_db, range_max_db,
                                      range_increment_db, target, mode)) {
            std::printf(
                "volume_live=unavailable reason=no-safe-attenuation "
                "original_db=%.2f original_mute=%s range_db=%.2f..%.2f "
                "increment_db=%.2f\n",
                original.requested_db, original.mute ? "true" : "false",
                static_cast<double>(range_min_db),
                static_cast<double>(range_max_db),
                static_cast<double>(range_increment_db));
            unavailable = true;
            break;
        }
        target.safety_ceiling_db = 0.0;
        target.generation = original.generation + 1U;
        const auto write_result =
            broker.write(target, hibiki::WindowsVolumeEventContextsV1::ui());
        if (FAILED(write_result)) {
            std::printf(
                "volume_live=fail reason=endpoint-write category=%s "
                "mode=%s original_db=%.2f target_db=%.2f range_db=%.2f..%.2f "
                "increment_db=%.2f\n",
                write_result_category(write_result),
                mode == WriteTestMode::Attenuate ? "attenuate" : "mute",
                original.requested_db, target.requested_db,
                static_cast<double>(range_min_db),
                static_cast<double>(range_max_db),
                static_cast<double>(range_increment_db));
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
            attenuated.mute != target.mute) {
            std::printf("volume_live=fail reason=attenuation-readback\n");
            break;
        }
        passed = restore();
        if (!passed) {
            std::printf("volume_live=fail reason=restore\n");
            break;
        }
        std::printf(
            "volume_live=pass mode=%s original_db=%.2f test_db=%.2f restored_db=%.2f "
            "range_db=%.2f..%.2f increment_db=%.2f\n",
            mode == WriteTestMode::Attenuate ? "attenuate" : "mute",
            original.requested_db, attenuated_db, restored_db,
            static_cast<double>(range_min_db), static_cast<double>(range_max_db),
            static_cast<double>(range_increment_db));
    } while (false);

    if (!restore()) passed = false;
    broker.unbind();
    if (endpoint != nullptr) endpoint->Release();
    if (device != nullptr) device->Release();
    if (enumerator != nullptr) enumerator->Release();
    if (com_initialized && com_result != RPC_E_CHANGED_MODE) CoUninitialize();
    return passed || unavailable ? 0 : 1;
}
