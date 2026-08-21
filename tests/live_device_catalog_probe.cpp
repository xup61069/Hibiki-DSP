// SPDX-License-Identifier: GPL-3.0-only

#if !defined(_WIN32)
#error "The live device catalog probe is Windows-only"
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>

#include <cstdio>

#include "hibiki/control_payloads.hpp"
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

    hibiki::WindowsPhysicalDeviceCatalogServiceV1 service;
    result = service.bind(enumerator);
    if (SUCCEEDED(result)) {
        result = service.refresh_now();
    }
    if (FAILED(result)) {
        std::fprintf(stderr, "Device catalog refresh failed: 0x%08lx\n",
                     static_cast<unsigned long>(result));
        enumerator->Release();
        if (SUCCEEDED(init)) CoUninitialize();
        return 3;
    }
    hibiki::IpcFrameV1 response;
    const bool provider_ok = hibiki::device_catalog_snapshot_reply_v1(
        response, service.snapshot_store());
    hibiki::DeviceCatalogSnapshotV1 decoded;
    const bool wire_ok = provider_ok && hibiki::decode_device_catalog_snapshot_v1(
        std::span<const std::uint8_t>(response.payload.data(), response.payload.size()), decoded);
    std::printf("catalog_refresh=pass entries=%zu sequence=%llu payload_bytes=%zu wire=%s provider=%s\n",
                service.catalog().size(), static_cast<unsigned long long>(service.sequence()),
                response.payload.size(), wire_ok ? "pass" : "fail",
                provider_ok ? "pass" : "fail");
    service.unbind();
    enumerator->Release();
    if (SUCCEEDED(init)) CoUninitialize();
    return wire_ok && decoded.entry_count == service.catalog().size() ? 0 : 4;
}
