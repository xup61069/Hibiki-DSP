// SPDX-License-Identifier: GPL-3.0-only

#if !defined(_WIN32)
#error "The live device catalog probe is Windows-only"
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>

#include <array>
#include <cstdio>

#include "hibiki/control_payloads.hpp"
#include "hibiki/device_catalog.hpp"
#include "hibiki/windows_device_catalog.hpp"

int main() {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
        std::fprintf(stderr, "CoInitializeEx failed: 0x%08lx\n",
                     static_cast<unsigned long>(init));
        return 2;
    }
    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      IID_PPV_ARGS(&enumerator));
    if (FAILED(result) || enumerator == nullptr) {
        std::fprintf(stderr, "MMDeviceEnumerator unavailable: 0x%08lx\n",
                     static_cast<unsigned long>(result));
        if (SUCCEEDED(init)) CoUninitialize();
        return 2;
    }

    hibiki::WindowsPhysicalDeviceCatalogCoordinator coordinator;
    hibiki::PhysicalDeviceCatalogV1 catalog;
    std::uint64_t sequence = 0U;
    std::array<std::uint8_t, hibiki::kDeviceCatalogSnapshotPayloadBytesV1> payload{};
    std::size_t payload_bytes = 0U;
    result = coordinator.bind(enumerator);
    if (SUCCEEDED(result)) {
        result = coordinator.refresh_now(catalog, sequence, payload, payload_bytes);
    }
    if (FAILED(result)) {
        std::fprintf(stderr, "Device catalog refresh failed: 0x%08lx\n",
                     static_cast<unsigned long>(result));
        enumerator->Release();
        if (SUCCEEDED(init)) CoUninitialize();
        return 3;
    }
    hibiki::DeviceCatalogSnapshotV1 decoded;
    const bool wire_ok = hibiki::decode_device_catalog_snapshot_v1(
        std::span<const std::uint8_t>(payload.data(), payload_bytes), decoded);
    std::printf("catalog_refresh=pass entries=%zu sequence=%llu payload_bytes=%zu wire=%s\n",
                catalog.size(), static_cast<unsigned long long>(sequence), payload_bytes,
                wire_ok ? "pass" : "fail");
    coordinator.unbind();
    enumerator->Release();
    if (SUCCEEDED(init)) CoUninitialize();
    return wire_ok && decoded.entry_count == catalog.size() ? 0 : 4;
}
