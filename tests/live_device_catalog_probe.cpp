// SPDX-License-Identifier: GPL-3.0-only

#if !defined(_WIN32)
#error "The live device catalog probe is Windows-only"
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>

#include <cstdio>
#include <optional>
#include <vector>

#include "hibiki/control_payloads.hpp"
#include "hibiki/control_status.hpp"
#include "hibiki/windows_device_catalog.hpp"

namespace {

bool transfer_exact(HANDLE handle, void* data, const std::size_t bytes, const bool write) {
    auto* raw = static_cast<std::uint8_t*>(data);
    std::size_t offset = 0U;
    while (offset < bytes) {
        DWORD transferred = 0U;
        const auto remaining = bytes - offset;
        const BOOL ok = write
                            ? WriteFile(handle, raw + offset, static_cast<DWORD>(remaining),
                                        &transferred, nullptr)
                            : ReadFile(handle, raw + offset, static_cast<DWORD>(remaining),
                                       &transferred, nullptr);
        if (ok == FALSE || transferred == 0U) return false;
        offset += transferred;
    }
    return true;
}

}  // namespace

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

    constexpr wchar_t kProbePipe[] = L"\\\\.\\pipe\\HibikiDSP_live_catalog_probe";
    hibiki::WindowsControlRuntimeV1 runtime;
    const bool runtime_started = runtime.start(
        enumerator, hibiki::IpcNamedPipeConfigV1{kProbePipe, 20000U, 1000U});
    result = runtime_started ? runtime.refresh_now() : E_FAIL;
    if (FAILED(result)) {
        std::fprintf(stderr, "Device catalog refresh failed: 0x%08lx\n",
                     static_cast<unsigned long>(result));
        enumerator->Release();
        if (SUCCEEDED(init)) CoUninitialize();
        return 3;
    }
    HANDLE client = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 30 && client == INVALID_HANDLE_VALUE; ++attempt) {
        client = CreateFileW(kProbePipe, GENERIC_READ | GENERIC_WRITE, 0U, nullptr,
                             OPEN_EXISTING, 0U, nullptr);
        if (client == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_PIPE_BUSY) (void)WaitNamedPipeW(kProbePipe, 100U);
            Sleep(10U);
        }
    }
    if (client == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "Control probe pipe unavailable\n");
        runtime.stop();
        enumerator->Release();
        if (SUCCEEDED(init)) CoUninitialize();
        return 4;
    }
    const auto round_trip = [&client](const hibiki::IpcFrameV1& request,
                                     std::vector<std::uint8_t>& response_bytes) {
        const auto request_bytes = hibiki::encode_ipc_frame(request);
        auto request_size = static_cast<std::uint32_t>(request_bytes.size());
        std::uint32_t response_size = 0U;
        const bool request_ok = !request_bytes.empty() &&
                                transfer_exact(client, &request_size, sizeof(request_size), true) &&
                                transfer_exact(client,
                                               const_cast<std::uint8_t*>(request_bytes.data()),
                                               request_bytes.size(), true) &&
                                transfer_exact(client, &response_size, sizeof(response_size), false) &&
                                response_size <= 20000U;
        response_bytes.assign(response_size, 0U);
        return request_ok &&
               transfer_exact(client, response_bytes.data(), response_bytes.size(), false);
    };
    hibiki::IpcFrameV1 request;
    request.header.type = hibiki::IpcMessageType::DeviceCatalogRequest;
    request.header.request_id = 777U;
    std::vector<std::uint8_t> response_bytes;
    const bool response_read = round_trip(request, response_bytes);
    CloseHandle(client);
    client = INVALID_HANDLE_VALUE;
    hibiki::IpcDecodeError response_error{hibiki::IpcDecodeError::None};
    const auto response = response_read
                              ? hibiki::decode_ipc_frame(response_bytes, response_error)
                              : std::optional<hibiki::IpcFrameV1>{};
    hibiki::DeviceCatalogSnapshotV1 decoded;
    const bool wire_ok = response.has_value() &&
                         response->header.type == hibiki::IpcMessageType::DeviceCatalogSnapshot &&
                         response->header.request_id == 777U &&
                         hibiki::decode_device_catalog_snapshot_v1(
                             std::span<const std::uint8_t>(response->payload.data(),
                                                            response->payload.size()),
                             decoded);
    hibiki::OutputGroupVolumeStateV1 volume_state{};
    const bool volume_ok = SUCCEEDED(runtime.read_volume(volume_state));
    for (int attempt = 0; attempt < 30 && client == INVALID_HANDLE_VALUE; ++attempt) {
        client = CreateFileW(kProbePipe, GENERIC_READ | GENERIC_WRITE, 0U, nullptr,
                             OPEN_EXISTING, 0U, nullptr);
        if (client == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_PIPE_BUSY) (void)WaitNamedPipeW(kProbePipe, 100U);
            Sleep(10U);
        }
    }
    hibiki::IpcFrameV1 status_request;
    status_request.header.type = hibiki::IpcMessageType::ControlStatusRequest;
    status_request.header.request_id = 778U;
    std::vector<std::uint8_t> status_bytes;
    const bool status_read = client != INVALID_HANDLE_VALUE &&
                             round_trip(status_request, status_bytes);
    hibiki::IpcDecodeError status_error{hibiki::IpcDecodeError::None};
    const auto status_response = status_read
                                     ? hibiki::decode_ipc_frame(status_bytes, status_error)
                                     : std::optional<hibiki::IpcFrameV1>{};
    hibiki::ControlStatusSnapshotV1 decoded_status;
    const bool status_ok = status_response.has_value() &&
                           status_response->header.type ==
                               hibiki::IpcMessageType::ControlStatusSnapshot &&
                           status_response->header.request_id == 778U &&
                           hibiki::decode_control_status_snapshot_v1(
                               std::span<const std::uint8_t>(status_response->payload.data(),
                                                              status_response->payload.size()),
                               decoded_status);
    std::printf("catalog_refresh=pass entries=%zu sequence=%llu payload_bytes=%zu wire=%s runtime=%s request=%s\n",
                runtime.catalog().size(), static_cast<unsigned long long>(runtime.catalog_sequence()),
                response.has_value() ? response->payload.size() : 0U,
                wire_ok ? "pass" : "fail", runtime.running() ? "pass" : "fail",
                wire_ok ? "pass" : "fail");
    std::printf("volume=%s status=%s routes=%u status_sequence=%llu\n", volume_ok ? "pass" : "unavailable",
                status_ok ? "pass" : "unavailable", decoded_status.route_count,
                static_cast<unsigned long long>(decoded_status.sequence));
    if (client != INVALID_HANDLE_VALUE) CloseHandle(client);
    runtime.stop();
    enumerator->Release();
    if (SUCCEEDED(init)) CoUninitialize();
    return wire_ok && status_ok && decoded.entry_count == runtime.catalog().size() ? 0 : 5;
}
