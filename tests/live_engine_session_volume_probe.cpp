// SPDX-License-Identifier: GPL-3.0-only

#if !defined(_WIN32)
#error "The live Windows Engine session-volume probe is Windows-only"
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

#include "hibiki/control_payloads.hpp"
#include "hibiki/ipc.hpp"
#include "hibiki/session_catalog.hpp"
#include "hibiki/volume_state.hpp"

namespace {

constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\HibikiDSP_v1_control";
constexpr std::wstring_view kSessionName = L"Hibiki live engine session volume probe";

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
            !transfer(true, const_cast<std::uint8_t*>(encoded.data()), encoded.size())) {
            return std::nullopt;
        }
        if (!transfer(false, length.data(), length.size())) return std::nullopt;
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

struct SessionReadingV1 {
    std::uint64_t handle{0U};
    std::uint64_t catalog_sequence{0U};
    double requested_db{-144.0};
    bool mute{false};
    std::uint8_t route_state{
        static_cast<std::uint8_t>(hibiki::SessionCatalogRouteStateV1::Unavailable)};
    std::uint8_t lane_bytes{0U};
    std::uint8_t output_bytes{0U};
    std::array<char, hibiki::kSessionRouteCommandLaneMaxBytesV1> lane{};
    std::array<char, hibiki::kSessionRouteCommandOutputMaxBytesV1> output{};
};

bool read_catalog(PipeClient& pipe,
                  const std::uint64_t request_id,
                  SessionReadingV1& reading) noexcept {
    const auto response = pipe.transact(hibiki::IpcMessageType::SessionCatalogRequest,
                                        request_id, {});
    if (!response.has_value() ||
        response->header.type != hibiki::IpcMessageType::SessionCatalogSnapshot) {
        return false;
    }
    hibiki::SessionCatalogSnapshotV1 catalog{};
    if (!hibiki::decode_session_catalog_snapshot_v1(response->payload, catalog) ||
        catalog.sequence == 0U) {
        return false;
    }
    for (std::size_t index = 0U; index < catalog.entry_count; ++index) {
        const auto& entry = catalog.entries[index];
        const std::string_view name(entry.name.data(), entry.name_bytes);
        if (name == "Hibiki live engine session volume probe" && entry.active != 0U &&
            (entry.flags & 1U) != 0U) {
            reading.handle = entry.handle;
            reading.catalog_sequence = catalog.sequence;
            reading.requested_db = hibiki::q16_16_to_db(entry.requested_db_q16_16);
            reading.mute = entry.mute != 0U;
            reading.route_state = static_cast<std::uint8_t>(entry.route_state);
            reading.lane_bytes = static_cast<std::uint8_t>(
                std::min<std::size_t>(entry.lane_bytes, reading.lane.size()));
            reading.output_bytes = static_cast<std::uint8_t>(
                std::min<std::size_t>(entry.output_bytes, reading.output.size()));
            std::copy_n(entry.lane.data(), reading.lane_bytes, reading.lane.data());
            std::copy_n(entry.output.data(), reading.output_bytes, reading.output.data());
            return true;
        }
    }
    return false;
}

bool wait_for_catalog(PipeClient& pipe,
                      std::uint64_t& request_id,
                      SessionReadingV1& reading) noexcept {
    for (std::uint32_t attempt = 0U; attempt < 80U; ++attempt) {
        if (read_catalog(pipe, request_id++, reading)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

bool send_volume_command(PipeClient& pipe,
                         const std::uint64_t request_id,
                         const SessionReadingV1& current,
                         const double requested_db,
                         const bool mute) noexcept {
    hibiki::SessionVolumeCommandV1 command{};
    command.handle = current.handle;
    command.catalog_sequence = current.catalog_sequence;
    command.requested_db_q16_16 = hibiki::db_to_q16_16(requested_db);
    command.mute = mute ? 1U : 0U;
    const auto encoded = hibiki::encode_session_volume_command_v1(command);
    if (encoded[0U] == 0U) return false;
    const auto response = pipe.transact(hibiki::IpcMessageType::SessionVolumeCommand,
                                        request_id, encoded);
    return response.has_value() && response->header.type == hibiki::IpcMessageType::Ack;
}

bool wait_for_value(PipeClient& pipe,
                    std::uint64_t& request_id,
                    const double requested_db,
                    const bool mute,
                    SessionReadingV1& reading) noexcept {
    for (std::uint32_t attempt = 0U; attempt < 80U; ++attempt) {
        SessionReadingV1 candidate{};
        if (read_catalog(pipe, request_id++, candidate) &&
            close_db(candidate.requested_db, requested_db) && candidate.mute == mute) {
            reading = candidate;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

bool send_route_command(PipeClient& pipe,
                        const std::uint64_t request_id,
                        const SessionReadingV1& current,
                        const std::string_view lane,
                        const std::string_view output_group) noexcept {
    hibiki::SessionRouteCommandV1 command{};
    command.handle = current.handle;
    command.catalog_sequence = current.catalog_sequence;
    if (lane.empty() || lane.size() > command.lane.size() || output_group.empty() ||
        output_group.size() > command.output_group.size()) {
        return false;
    }
    command.lane_bytes = static_cast<std::uint8_t>(lane.size());
    command.output_group_bytes = static_cast<std::uint8_t>(output_group.size());
    std::copy(lane.begin(), lane.end(), command.lane.begin());
    std::copy(output_group.begin(), output_group.end(), command.output_group.begin());
    const auto encoded = hibiki::encode_session_route_command_v1(command);
    if (encoded[0U] == 0U) return false;
    const auto response = pipe.transact(hibiki::IpcMessageType::SessionRouteCommand,
                                        request_id, encoded);
    return response.has_value() && response->header.type == hibiki::IpcMessageType::Ack;
}

bool send_route_rule_command(PipeClient& pipe,
                              const std::uint64_t request_id,
                              const SessionReadingV1& current,
                              const hibiki::SessionRouteRuleOperationV1 operation,
                              const std::string_view rule_id,
                              const std::string_view display_name,
                              const std::string_view lane,
                              const std::string_view output_group) noexcept {
    hibiki::SessionRouteRuleCommandV1 command{};
    command.schema_version = 1U;
    command.priority = 100;
    command.makeup_gain_q16_16 = 0;
    command.operation = operation;
    command.enabled = 1U;
    command.gain_owner = hibiki::SessionRouteRuleGainOwnerV1::WindowsSession;
    command.catalog_sequence = current.catalog_sequence;
    if (rule_id.empty() || rule_id.size() > command.rule_id.size()) return false;
    command.rule_id_bytes = static_cast<std::uint16_t>(rule_id.size());
    std::copy(rule_id.begin(), rule_id.end(), command.rule_id.begin());
    if (operation == hibiki::SessionRouteRuleOperationV1::Upsert) {
        if (display_name.empty() || display_name.size() > command.display_name.size() ||
            lane.empty() || lane.size() > command.lane.size() || output_group.empty() ||
            output_group.size() > command.output_group.size()) {
            return false;
        }
        command.display_name_bytes = static_cast<std::uint16_t>(display_name.size());
        command.lane_bytes = static_cast<std::uint16_t>(lane.size());
        command.output_group_bytes = static_cast<std::uint16_t>(output_group.size());
        std::copy(display_name.begin(), display_name.end(), command.display_name.begin());
        std::copy(lane.begin(), lane.end(), command.lane.begin());
        std::copy(output_group.begin(), output_group.end(), command.output_group.begin());
    }
    const auto encoded = hibiki::encode_session_route_rule_command_v1(command);
    if (encoded[0U] == 0U) return false;
    const auto response = pipe.transact(hibiki::IpcMessageType::SessionRouteRuleCommand,
                                        request_id, encoded);
    return response.has_value() && response->header.type == hibiki::IpcMessageType::Ack;
}

bool wait_for_route_state(PipeClient& pipe,
                          std::uint64_t& request_id,
                          const hibiki::SessionCatalogRouteStateV1 expected_state,
                          const std::string_view lane,
                          const std::string_view output_group,
                          const std::uint64_t minimum_sequence,
                          SessionReadingV1& reading) noexcept {
    for (std::uint32_t attempt = 0U; attempt < 80U; ++attempt) {
        SessionReadingV1 candidate{};
        if (read_catalog(pipe, request_id++, candidate) &&
            candidate.catalog_sequence > minimum_sequence &&
            candidate.route_state == static_cast<std::uint8_t>(expected_state) &&
            std::string_view(candidate.lane.data(), candidate.lane_bytes) == lane &&
            std::string_view(candidate.output.data(), candidate.output_bytes) == output_group) {
            reading = candidate;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

bool wait_for_route(PipeClient& pipe,
                    std::uint64_t& request_id,
                    const std::string_view lane,
                    const std::string_view output_group,
                    SessionReadingV1& reading) noexcept {
    return wait_for_route_state(pipe, request_id, hibiki::SessionCatalogRouteStateV1::Ready,
                                lane, output_group, 0U, reading);
}

}  // namespace

int wmain() {
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com_initialized = SUCCEEDED(com_result) || com_result == RPC_E_CHANGED_MODE;
    if (!com_initialized) {
        std::printf("session_volume_engine_live=unavailable reason=com-init\n");
        return 0;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    PipeClient pipe;
    std::uint64_t request_id = 100U;
    bool changed = false;
    bool passed = false;
    double original_db = -144.0;
    double attenuated_db = -144.0;
    double restored_db = -144.0;
    bool original_mute = false;
    SessionReadingV1 latest{};

    auto restore = [&]() noexcept -> bool {
        if (!changed) return true;
        SessionReadingV1 current{};
        if (!wait_for_catalog(pipe, request_id, current) ||
            !send_volume_command(pipe, request_id++, current, original_db, original_mute) ||
            !wait_for_value(pipe, request_id, original_db, original_mute, current)) {
            return false;
        }
        restored_db = current.requested_db;
        changed = false;
        return close_db(restored_db, original_db) && current.mute == original_mute;
    };

    do {
        HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                          IID_PPV_ARGS(&enumerator));
        if (SUCCEEDED(result)) {
            result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        }
        if (FAILED(result) || device == nullptr) {
            std::printf("session_volume_engine_live=unavailable reason=default-endpoint\n");
            break;
        }
        result = start_silent_session(device, &client, &render);
        if (FAILED(result)) {
            std::printf("session_volume_engine_live=unavailable reason=silent-session\n");
            break;
        }
        if (!pipe.connect()) {
            std::printf("session_volume_engine_live=unavailable reason=engine-pipe\n");
            break;
        }
        SessionReadingV1 current{};
        if (!wait_for_catalog(pipe, request_id, current)) {
            std::printf("session_volume_engine_live=unavailable reason=session-catalog\n");
            break;
        }
        original_db = current.requested_db;
        original_mute = current.mute;
        const double target_db = std::max(-144.0, original_db - 3.0);
        if (!send_volume_command(pipe, request_id++, current, target_db, original_mute)) {
            std::printf("session_volume_engine_live=fail reason=queue-write\n");
            break;
        }
        if (!wait_for_value(pipe, request_id, target_db, original_mute, latest)) {
            std::printf("session_volume_engine_live=fail reason=queue-readback\n");
            break;
        }
        changed = true;
        attenuated_db = latest.requested_db;
        if (!close_db(attenuated_db, target_db) || attenuated_db > original_db + 0.5 ||
            latest.mute != original_mute) {
            std::printf("session_volume_engine_live=fail reason=attenuation-readback\n");
            break;
        }
        const bool restored = restore();
        if (!restored) {
            std::printf("session_volume_engine_live=fail reason=restore\n");
            break;
        }
        constexpr std::string_view kProbeLane = "live-engine-session-lane";
        constexpr std::string_view kProbeOutput = "main";
        SessionReadingV1 route_current{};
        if (!wait_for_catalog(pipe, request_id, route_current) ||
            !send_route_command(pipe, request_id++, route_current, kProbeLane, kProbeOutput)) {
            std::printf("session_volume_engine_live=fail reason=route-queue-write\n");
            break;
        }
        SessionReadingV1 routed{};
        if (!wait_for_route(pipe, request_id, kProbeLane, kProbeOutput, routed)) {
            std::printf("session_volume_engine_live=fail reason=route-readback\n");
            break;
        }
        constexpr std::string_view kProbeRule = "live-engine-route-rule";
        constexpr std::string_view kProbeMatch = "Hibiki live engine session volume probe";
        if (!send_route_rule_command(pipe, request_id++, routed,
                                     hibiki::SessionRouteRuleOperationV1::Upsert, kProbeRule,
                                     kProbeMatch, kProbeLane, kProbeOutput)) {
            std::printf("session_volume_engine_live=fail reason=route-rule-upsert\n");
            break;
        }
        SessionReadingV1 rule_ready{};
        if (!wait_for_route_state(pipe, request_id, hibiki::SessionCatalogRouteStateV1::Ready,
                                  kProbeLane, kProbeOutput, routed.catalog_sequence,
                                  rule_ready)) {
            std::printf("session_volume_engine_live=fail reason=route-rule-readback\n");
            break;
        }
        if (!send_route_rule_command(pipe, request_id++, rule_ready,
                                     hibiki::SessionRouteRuleOperationV1::Remove, kProbeRule, {},
                                     {}, {})) {
            std::printf("session_volume_engine_live=fail reason=route-rule-remove\n");
            break;
        }
        SessionReadingV1 rule_removed{};
        if (!wait_for_route_state(pipe, request_id, hibiki::SessionCatalogRouteStateV1::Pending,
                                  {}, {}, rule_ready.catalog_sequence, rule_removed)) {
            std::printf("session_volume_engine_live=fail reason=route-rule-clear-readback\n");
            break;
        }
        passed = true;
        std::printf("session_volume_engine_live=pass original_db=%.2f attenuated_db=%.2f restored_db=%.2f route=ready rule=ready-to-pending transport=ipc-control-queue-com-worker\n",
                    original_db, attenuated_db, restored_db);
    } while (false);

    if (!restore()) passed = false;
    if (client != nullptr) (void)client->Stop();
    if (render != nullptr) render->Release();
    if (client != nullptr) client->Release();
    if (device != nullptr) device->Release();
    if (enumerator != nullptr) enumerator->Release();
    if (com_initialized && com_result != RPC_E_CHANGED_MODE) CoUninitialize();
    return passed ? 0 : 1;
}
