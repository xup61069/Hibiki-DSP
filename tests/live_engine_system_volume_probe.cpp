// SPDX-License-Identifier: GPL-3.0-only

#if !defined(_WIN32)
#error "The live Windows Engine system-volume probe is Windows-only"
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <optional>
#include <span>
#include <thread>
#include <vector>

#include "hibiki/control_payloads.hpp"
#include "hibiki/ipc.hpp"
#include "hibiki/windows_volume_broker.hpp"

namespace {

constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\HibikiDSP_v1_control";

bool close_db(const double left, const double right) noexcept {
    return std::isfinite(left) && std::isfinite(right) && std::abs(left - right) <= 0.5;
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

void write_u32_le(std::uint8_t* const bytes, const std::uint32_t value) noexcept {
    bytes[0] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes[2] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    bytes[3] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

std::uint32_t read_u32_le(const std::uint8_t* const bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

class PipeClient final {
public:
    PipeClient() noexcept = default;
    ~PipeClient() { close(); }

    PipeClient(const PipeClient&) = delete;
    PipeClient& operator=(const PipeClient&) = delete;

    bool connect() noexcept {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            if (WaitNamedPipeW(kPipeName, 250U) != FALSE) {
                handle_ = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE, 0U, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (handle_ != INVALID_HANDLE_VALUE) {
                    DWORD mode = PIPE_READMODE_BYTE;
                    if (SetNamedPipeHandleState(handle_, &mode, nullptr, nullptr) != FALSE) {
                        return true;
                    }
                    close();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        return false;
    }

    [[nodiscard]] std::optional<hibiki::IpcFrameV1> transact(
        const hibiki::IpcMessageType type,
        const std::uint64_t request_id,
        const std::span<const std::uint8_t> payload) noexcept {
        if (handle_ == INVALID_HANDLE_VALUE) return std::nullopt;
        hibiki::IpcFrameV1 request{};
        request.header.type = type;
        request.header.request_id = request_id;
        request.payload.assign(payload.begin(), payload.end());
        const auto encoded = hibiki::encode_ipc_frame(request);
        if (encoded.empty() || encoded.size() > hibiki::kIpcMaxPayloadBytes) return std::nullopt;

        std::array<std::uint8_t, 4U> length{};
        write_u32_le(length.data(), static_cast<std::uint32_t>(encoded.size()));
        if (!transfer(true, length.data(), length.size()) ||
            !transfer(true, const_cast<std::uint8_t*>(encoded.data()), encoded.size()) ||
            !transfer(false, length.data(), length.size())) {
            return std::nullopt;
        }
        const auto response_bytes = read_u32_le(length.data());
        if (response_bytes < 20U || response_bytes > hibiki::kIpcMaxPayloadBytes) {
            return std::nullopt;
        }
        std::vector<std::uint8_t> response(response_bytes);
        if (!transfer(false, response.data(), response.size())) return std::nullopt;
        hibiki::IpcDecodeError error{hibiki::IpcDecodeError::None};
        auto decoded = hibiki::decode_ipc_frame(response, error);
        if (!decoded.has_value() || decoded->header.request_id != request_id) return std::nullopt;
        return decoded;
    }

private:
    bool transfer(const bool writing, void* const data, const std::size_t bytes) noexcept {
        auto* cursor = static_cast<std::uint8_t*>(data);
        std::size_t remaining = bytes;
        while (remaining > 0U) {
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, 1U << 20U));
            DWORD transferred = 0U;
            const BOOL ok = writing
                                ? WriteFile(handle_, cursor, chunk, &transferred, nullptr)
                                : ReadFile(handle_, cursor, chunk, &transferred, nullptr);
            if (ok == FALSE || transferred == 0U) return false;
            cursor += transferred;
            remaining -= transferred;
        }
        return true;
    }

    void close() noexcept {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

    HANDLE handle_{INVALID_HANDLE_VALUE};
};

bool send_volume(PipeClient& pipe,
                 const std::uint64_t request_id,
                 const double requested_db,
                 const bool mute,
                 const std::uint64_t generation) noexcept {
    const hibiki::VolumeNotificationV1 notification{requested_db, mute, generation};
    const auto payload = hibiki::encode_volume_notification_payload_v1(notification);
    const auto response = pipe.transact(hibiki::IpcMessageType::VolumeNotification,
                                        request_id, payload);
    return response.has_value() && response->header.type == hibiki::IpcMessageType::Ack;
}

bool wait_for_endpoint(hibiki::WindowsVolumeBroker& broker,
                       const double requested_db,
                       const bool mute,
                       hibiki::OutputGroupVolumeStateV1& state) noexcept {
    for (std::uint32_t attempt = 0U; attempt < 80U; ++attempt) {
        if (SUCCEEDED(broker.read_state(state)) && close_db(state.requested_db, requested_db) &&
            state.mute == mute) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return false;
}

}  // namespace

int wmain(const int argc, wchar_t* const* argv) {
    if (argc == 2 && argv != nullptr && argv[1] != nullptr &&
        std::wcscmp(argv[1], L"--self-test") == 0) {
        const bool passed = write_target_self_test();
        std::printf("system_volume_engine_live_selftest=%s cases=4\n",
                    passed ? "pass" : "fail");
        return passed ? 0 : 2;
    }
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com_initialized = SUCCEEDED(com_result) || com_result == RPC_E_CHANGED_MODE;
    if (!com_initialized) {
        std::printf("system_volume_engine_live=unavailable reason=com-init\n");
        return 0;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioEndpointVolume* endpoint = nullptr;
    hibiki::WindowsVolumeBroker broker;
    PipeClient pipe;
    hibiki::OutputGroupVolumeStateV1 original{};
    bool changed = false;
    bool passed = false;
    bool unavailable = false;
    double attenuated_db = -144.0;
    double restored_db = -144.0;
    float range_min_db = 0.0F;
    float range_max_db = 0.0F;
    float range_increment_db = 0.0F;
    std::uint64_t request_id = 200U;
    std::uint64_t next_generation = 1U;

    auto restore = [&]() noexcept -> bool {
        if (!changed) return true;
        const auto generation = std::max(next_generation++, original.generation + 1U);
        if (!send_volume(pipe, request_id++, original.requested_db, original.mute, generation)) {
            return false;
        }
        hibiki::OutputGroupVolumeStateV1 restored{};
        if (!wait_for_endpoint(broker, original.requested_db, original.mute, restored)) {
            return false;
        }
        restored_db = restored.requested_db;
        changed = false;
        return close_db(restored_db, original.requested_db) && restored.mute == original.mute;
    };

    do {
        HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                          IID_PPV_ARGS(&enumerator));
        if (SUCCEEDED(result)) {
            result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        }
        if (SUCCEEDED(result) && device != nullptr) {
            result = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                      reinterpret_cast<void**>(&endpoint));
        }
        if (SUCCEEDED(result) && endpoint != nullptr) {
            result = endpoint->GetVolumeRange(&range_min_db, &range_max_db,
                                              &range_increment_db);
        }
        if (FAILED(result) || device == nullptr || endpoint == nullptr ||
            FAILED(broker.bind(device)) ||
            FAILED(broker.read_state(original))) {
            std::printf("system_volume_engine_live=unavailable reason=endpoint-or-broker\n");
            unavailable = true;
            break;
        }
        next_generation = std::max<std::uint64_t>(original.generation + 1U, 1U);
        if (!pipe.connect()) {
            std::printf("system_volume_engine_live=unavailable reason=engine-pipe\n");
            unavailable = true;
            break;
        }
        auto target = original;
        WriteTestMode mode{WriteTestMode::Attenuate};
        if (!choose_safe_write_target(original, range_min_db, range_max_db,
                                      range_increment_db, target, mode)) {
            std::printf(
                "system_volume_engine_live=unavailable reason=no-safe-attenuation "
                "original_db=%.2f original_mute=%s range_db=%.2f..%.2f "
                "increment_db=%.2f\n",
                original.requested_db, original.mute ? "true" : "false",
                static_cast<double>(range_min_db),
                static_cast<double>(range_max_db),
                static_cast<double>(range_increment_db));
            unavailable = true;
            break;
        }
        if (!send_volume(pipe, request_id++, target.requested_db, target.mute,
                         next_generation++)) {
            std::printf("system_volume_engine_live=fail reason=queue-write\n");
            break;
        }
        changed = true;
        hibiki::OutputGroupVolumeStateV1 attenuated{};
        if (!wait_for_endpoint(broker, target.requested_db, target.mute, attenuated)) {
            std::printf(
                "system_volume_engine_live=fail reason=endpoint-readback mode=%s "
                "target_db=%.2f target_mute=%s range_db=%.2f..%.2f increment_db=%.2f\n",
                mode == WriteTestMode::Attenuate ? "attenuate" : "mute",
                target.requested_db, target.mute ? "true" : "false",
                static_cast<double>(range_min_db),
                static_cast<double>(range_max_db),
                static_cast<double>(range_increment_db));
            break;
        }
        attenuated_db = attenuated.requested_db;
        if (attenuated_db > original.requested_db + 0.5 || attenuated.mute != target.mute) {
            std::printf("system_volume_engine_live=fail reason=unsafe-readback\n");
            break;
        }
        passed = restore();
        if (!passed) {
            std::printf("system_volume_engine_live=fail reason=restore\n");
            break;
        }
        std::printf(
            "system_volume_engine_live=pass mode=%s original_db=%.2f test_db=%.2f "
            "restored_db=%.2f range_db=%.2f..%.2f increment_db=%.2f "
            "transport=ipc-engine-broker\n",
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
