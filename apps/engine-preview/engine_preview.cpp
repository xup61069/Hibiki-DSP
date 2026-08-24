// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/audio_engine.hpp"
#include "hibiki/control_status.hpp"
#include "hibiki/control_service.hpp"
#include "hibiki/engine_control.hpp"
#include "hibiki/scene_presets.hpp"
#include "hibiki/session_catalog.hpp"
#include "hibiki/session_command_queue.hpp"
#include "hibiki/session_route_rules.hpp"
#include "hibiki/windows_audio_session_route.hpp"
#include "hibiki/windows_device_catalog.hpp"
#include "hibiki/windows_volume_broker.hpp"
#include "hibiki/windows_volume_link.hpp"
#include "hibiki/wav_ir.hpp"

#include <Windows.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
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

struct SessionRoutingState final {
    hibiki::WindowsAudioSessionRouteCoordinatorV1 coordinator{};
    hibiki::SessionCommandQueueV1 queue{};
    hibiki::SessionCatalogSnapshotStoreV1 catalog_store{};
    hibiki::SessionRouteRuleStoreV1 rules{};
    std::array<wchar_t, 260U> endpoint_id{};
    std::uint16_t endpoint_id_chars{0U};
    bool requested{false};
    bool bound{false};
};

struct WasapiOutputState final {
    hibiki::AudioEngineModel* engine{nullptr};
    hibiki::WasapiOutputConfigV1 config{};
    std::string endpoint_id{};
    std::uint32_t block_frames{128U};
    bool requested{false};
    bool active{false};
    bool test_tone_enabled{false};
};

constexpr std::uint32_t kTestToneInputChannels = 2U;
constexpr std::uint32_t kTestToneMaxFrames = 4096U;
constexpr std::uint32_t kTestToneMaxOutputChannels = 8U;

struct TestToneState final {
    std::array<float, kTestToneMaxFrames * kTestToneInputChannels> input{};
    std::array<float, kTestToneMaxFrames * kTestToneMaxOutputChannels> output{};
    std::array<hibiki::RtLaneInputV1, 1U> lanes{};
    double phase{0.0};
    std::uint32_t sample_rate{0U};
    std::uint32_t block_frames{0U};
};

bool supported_wasapi_layout(const std::uint32_t channels) noexcept {
    return channels == 2U || channels == 6U || channels == 8U;
}

bool supported_wasapi_rate(const std::uint32_t sample_rate) noexcept {
    return sample_rate == 44100U || sample_rate == 48000U ||
           sample_rate == 96000U || sample_rate == 192000U;
}

bool start_default_wasapi_output(
    WasapiOutputState& state,
    hibiki::AudioEngineModel& engine,
    const hibiki::PhysicalDeviceCatalogV1& catalog) noexcept {
    state.active = false;
    state.endpoint_id.clear();
    state.config = {};
    const auto* const descriptor = catalog.default_device(hibiki::PhysicalDeviceFlowV1::Render);
    if (descriptor == nullptr || descriptor->availability !=
                                     hibiki::PhysicalDeviceAvailabilityV1::Active ||
        descriptor->endpoint_id.empty() || !supported_wasapi_layout(descriptor->channels) ||
        !supported_wasapi_rate(descriptor->sample_rate)) {
        return false;
    }

    const auto block_frames = descriptor->buffer_frames == 0U
                                  ? 128U
                                  : std::clamp(descriptor->buffer_frames, 16U, 4096U);
    hibiki::WasapiOutputConfigV1 config{
        std::wstring(descriptor->endpoint_id.begin(), descriptor->endpoint_id.end()),
        descriptor->channels, descriptor->sample_rate, 20U};
    if (!engine.start_wasapi_output(config, block_frames)) return false;
    state.engine = &engine;
    state.config = std::move(config);
    state.endpoint_id = descriptor->endpoint_id;
    state.block_frames = block_frames;
    state.active = true;
    return true;
}

bool prepare_test_tone(TestToneState& tone,
                       WasapiOutputState& output,
                       hibiki::AudioEngineModel& engine) noexcept {
    if (!output.active || output.config.sample_rate == 0U || output.block_frames == 0U ||
        output.block_frames > kTestToneMaxFrames ||
        output.config.channels > kTestToneMaxOutputChannels) {
        return false;
    }

    auto scene = hibiki::make_easy_scene(hibiki::EasySceneKind::Studio, "main");
    scene.graph.output_channels = output.config.channels;
    engine.set_sample_rate(output.config.sample_rate);
    if (!engine.prepare_graph(scene.graph, 1U) || !engine.commit_graph()) {
        engine.rollback_graph();
        return false;
    }

    tone.lanes[0U] = hibiki::RtLaneInputV1{tone.input.data(), kTestToneInputChannels};
    tone.sample_rate = output.config.sample_rate;
    tone.block_frames = output.block_frames;
    output.test_tone_enabled = true;
    return true;
}

bool render_test_tone(TestToneState& tone,
                      hibiki::AudioEngineModel& engine) noexcept {
    constexpr double kFrequencyHz = 440.0;
    constexpr float kAmplitude = 0.1F;  // approximately -20 dBFS
    constexpr double kTwoPi = 6.28318530717958647692;
    if (tone.sample_rate == 0U || tone.block_frames == 0U ||
        tone.block_frames > kTestToneMaxFrames) {
        return false;
    }

    const auto phase_increment = kTwoPi * kFrequencyHz /
                                 static_cast<double>(tone.sample_rate);
    for (std::uint32_t frame = 0U; frame < tone.block_frames; ++frame) {
        const auto sample = kAmplitude * static_cast<float>(std::sin(tone.phase));
        if (!std::isfinite(sample)) return false;
        const auto offset = static_cast<std::size_t>(frame) * kTestToneInputChannels;
        tone.input[offset] = sample;
        tone.input[offset + 1U] = sample;
        tone.phase += phase_increment;
        if (tone.phase >= kTwoPi) tone.phase -= kTwoPi;
    }
    return engine.process_output_group_to_wasapi(
        "main", std::span<const hibiki::RtLaneInputV1>(tone.lanes), tone.output.data(),
        tone.block_frames);
}

bool has_rendered_blocks(const hibiki::WasapiSinkHandoffSnapshotV1& snapshot) noexcept {
    return snapshot.primary.rendered_blocks > 0U || snapshot.secondary.rendered_blocks > 0U;
}

hibiki::ControlRouteHealthStateV1 wasapi_route_state(
    const WasapiOutputState& state,
    const hibiki::WasapiSinkHandoffSnapshotV1& snapshot) noexcept {
    if (!state.requested) return hibiki::ControlRouteHealthStateV1::Unavailable;
    if (!state.active) return hibiki::ControlRouteHealthStateV1::Pending;
    if (snapshot.state == hibiki::WasapiSinkHandoffStateV1::Degraded ||
        snapshot.primary.degraded || snapshot.secondary.degraded) {
        return hibiki::ControlRouteHealthStateV1::Degraded;
    }
    if (snapshot.state == hibiki::WasapiSinkHandoffStateV1::Synced &&
        snapshot.primary.endpoint_ready) {
        return hibiki::ControlRouteHealthStateV1::Ready;
    }
    return hibiki::ControlRouteHealthStateV1::Pending;
}

std::string_view wasapi_route_detail(
    const WasapiOutputState& state,
    const hibiki::WasapiSinkHandoffSnapshotV1& snapshot) noexcept {
    if (!state.requested) {
        return "physical catalog ready; shared-mode WASAPI sink disabled by default.";
    }
    if (!state.active) {
        return "WASAPI sink requested; no supported active default render endpoint; output remains muted.";
    }
    if (snapshot.state == hibiki::WasapiSinkHandoffStateV1::Degraded ||
        snapshot.primary.degraded || snapshot.secondary.degraded) {
        return "WASAPI sink degraded; fail-closed until a supported endpoint is available.";
    }
    if (snapshot.state == hibiki::WasapiSinkHandoffStateV1::Synced &&
        snapshot.primary.endpoint_ready) {
        if (state.test_tone_enabled && has_rendered_blocks(snapshot)) {
            return "test tone rendering.";
        }
        return "shared-mode WASAPI sink ready; graph/ASIO delivery remains an explicit source boundary.";
    }
    return "WASAPI sink warming; no audio is reported ready until the worker confirms the endpoint.";
}

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

HRESULT acquire_default_render_device(IMMDeviceEnumerator* const enumerator,
                                      IMMDevice** const device,
                                      std::array<wchar_t, 260U>& endpoint_id,
                                      std::uint16_t& endpoint_id_chars) noexcept {
    endpoint_id.fill(L'\0');
    endpoint_id_chars = 0U;
    if (enumerator == nullptr || device == nullptr) return E_INVALIDARG;
    *device = nullptr;
    const auto result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device);
    if (FAILED(result) || *device == nullptr) return FAILED(result) ? result : E_FAIL;
    LPWSTR raw_id = nullptr;
    const auto id_result = (*device)->GetId(&raw_id);
    if (FAILED(id_result) || raw_id == nullptr) {
        if (raw_id != nullptr) CoTaskMemFree(raw_id);
        (*device)->Release();
        *device = nullptr;
        return FAILED(id_result) ? id_result : E_FAIL;
    }
    const auto length = std::wcslen(raw_id);
    if (length == 0U || length >= endpoint_id.size()) {
        CoTaskMemFree(raw_id);
        (*device)->Release();
        *device = nullptr;
        return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    }
    std::copy_n(raw_id, length, endpoint_id.data());
    endpoint_id[length] = L'\0';
    endpoint_id_chars = static_cast<std::uint16_t>(length);
    CoTaskMemFree(raw_id);
    return S_OK;
}

bool same_endpoint_id(const std::array<wchar_t, 260U>& left,
                      const std::uint16_t left_chars,
                      const std::array<wchar_t, 260U>& right,
                      const std::uint16_t right_chars) noexcept {
    return left_chars == right_chars &&
           std::equal(left.begin(), left.begin() + left_chars, right.begin());
}

HRESULT bind_default_session_route_if_changed(IMMDeviceEnumerator* const enumerator,
                                               SessionRoutingState& state) noexcept {
    IMMDevice* device = nullptr;
    std::array<wchar_t, 260U> endpoint_id{};
    std::uint16_t endpoint_id_chars = 0U;
    const auto acquire_result = acquire_default_render_device(
        enumerator, &device, endpoint_id, endpoint_id_chars);
    if (FAILED(acquire_result)) {
        state.coordinator.unbind();
        state.bound = false;
        state.endpoint_id.fill(L'\0');
        state.endpoint_id_chars = 0U;
        return acquire_result;
    }
    if (state.bound && same_endpoint_id(state.endpoint_id, state.endpoint_id_chars,
                                        endpoint_id, endpoint_id_chars)) {
        device->Release();
        return S_FALSE;
    }
    const auto bind_result = state.coordinator.bind(device);
    device->Release();
    if (FAILED(bind_result)) {
        state.bound = false;
        return bind_result;
    }
    state.endpoint_id = endpoint_id;
    state.endpoint_id_chars = endpoint_id_chars;
    state.bound = true;
    (void)state.coordinator.set_rules(state.rules);
    return S_OK;
}

bool publish_session_catalog(SessionRoutingState& state) noexcept {
    if (!state.requested || !state.bound) return false;
    const auto previous = state.catalog_store.sequence();
    if (previous == UINT64_MAX) return false;
    hibiki::SessionCatalogSnapshotV1 candidate{};
    if (!state.coordinator.make_session_catalog_snapshot(previous + 1U, candidate) ||
        !state.catalog_store.publish(candidate)) {
        return false;
    }
    return true;
}

bool enqueue_session_volume_command(const hibiki::SessionVolumeCommandV1& request,
                                    void* const context) noexcept {
    auto* state = static_cast<SessionRoutingState*>(context);
    return state != nullptr && state->requested && state->bound &&
           state->queue.try_push_volume(request);
}

bool enqueue_session_route_command(const hibiki::SessionRouteCommandV1& request,
                                   void* const context) noexcept {
    auto* state = static_cast<SessionRoutingState*>(context);
    return state != nullptr && state->requested && state->bound &&
           state->queue.try_push_route(request);
}

bool enqueue_session_route_rule_command(const hibiki::SessionRouteRuleCommandV1& request,
                                        void* const context) noexcept {
    auto* state = static_cast<SessionRoutingState*>(context);
    if (state == nullptr || !state->requested || !state->bound) return false;
    const auto encoded = hibiki::encode_session_route_rule_command_v1(request);
    if (encoded[0U] == 0U) return false;
    return state->queue.try_push_route_rule(request);
}

bool drain_session_commands(SessionRoutingState& state) noexcept {
    if (!state.requested || !state.bound) return false;
    std::uint64_t expected_sequence = state.catalog_store.sequence();
    bool catalog_dirty = false;
    bool any_processed = false;
    hibiki::SessionCommandWorkItemV1 item{};
    while (state.queue.try_pop(item)) {
        any_processed = true;
        if (item.kind == hibiki::SessionCommandKindV1::Volume) {
            if (item.volume.catalog_sequence != expected_sequence) continue;
            const auto result = state.coordinator.write_session_volume_handle(
                item.volume.handle, hibiki::q16_16_to_db(item.volume.requested_db_q16_16),
                item.volume.mute != 0U, hibiki::WindowsVolumeEventContextsV1::session());
            catalog_dirty = catalog_dirty || SUCCEEDED(result);
        } else if (item.kind == hibiki::SessionCommandKindV1::Route) {
            if (item.route.catalog_sequence != expected_sequence) continue;
            const auto result = state.coordinator.bind_session_route_handle(
                item.route.handle,
                std::string_view(item.route.lane.data(), item.route.lane_bytes),
                std::string_view(item.route.output_group.data(), item.route.output_group_bytes));
            if (SUCCEEDED(result)) {
                catalog_dirty = true;
                if (expected_sequence != UINT64_MAX) ++expected_sequence;
            }
        } else if (item.kind == hibiki::SessionCommandKindV1::RouteRule) {
            if (item.route_rule.catalog_sequence != expected_sequence) continue;
            try {
                auto candidate_rules = state.rules;
                const auto operation = item.route_rule.operation;
                hibiki::SessionRouteRuleResultV1 rule_result =
                    hibiki::SessionRouteRuleResultV1::invalid_argument;
                if (operation == hibiki::SessionRouteRuleOperationV1::Upsert) {
                    hibiki::SessionRouteRuleV1 rule{};
                    rule.schema_version = item.route_rule.schema_version;
                    rule.rule_id.assign(item.route_rule.rule_id.data(),
                                        item.route_rule.rule_id_bytes);
                    rule.priority = item.route_rule.priority;
                    rule.enabled = item.route_rule.enabled != 0U;
                    rule.app_id.assign(item.route_rule.app_id.data(), item.route_rule.app_id_bytes);
                    rule.display_name_contains.assign(item.route_rule.display_name.data(),
                                                      item.route_rule.display_name_bytes);
                    rule.lane_id.assign(item.route_rule.lane.data(), item.route_rule.lane_bytes);
                    rule.output_group.assign(item.route_rule.output_group.data(),
                                             item.route_rule.output_group_bytes);
                    rule.gain_owner = item.route_rule.gain_owner ==
                                              hibiki::SessionRouteRuleGainOwnerV1::HibikiInternal
                                          ? hibiki::SessionGainOwner::HibikiInternal
                                          : hibiki::SessionGainOwner::WindowsSession;
                    rule.makeup_gain_db = hibiki::q16_16_to_db(
                        item.route_rule.makeup_gain_q16_16);
                    rule_result = candidate_rules.upsert(rule);
                } else if (operation == hibiki::SessionRouteRuleOperationV1::Remove) {
                    rule_result = candidate_rules.remove(
                                      std::string_view(item.route_rule.rule_id.data(),
                                                       item.route_rule.rule_id_bytes))
                                      ? hibiki::SessionRouteRuleResultV1::applied
                                      : hibiki::SessionRouteRuleResultV1::no_match;
                } else if (operation == hibiki::SessionRouteRuleOperationV1::Clear) {
                    candidate_rules.clear();
                    rule_result = hibiki::SessionRouteRuleResultV1::applied;
                }
                if (rule_result == hibiki::SessionRouteRuleResultV1::applied) {
                    const auto refresh_result = state.coordinator.set_rules_and_refresh(candidate_rules);
                    const bool applied = refresh_result ==
                                             hibiki::WindowsAudioSessionRouteRefreshResultV1::Applied ||
                                         refresh_result ==
                                             hibiki::WindowsAudioSessionRouteRefreshResultV1::NoRoutes;
                    if (applied) {
                        state.rules = std::move(candidate_rules);
                        catalog_dirty = true;
                        if (expected_sequence != UINT64_MAX) ++expected_sequence;
                    }
                }
            } catch (...) {
                // Keep the previous rule store when a bounded control update fails.
            }
        }
    }
    if (catalog_dirty) (void)publish_session_catalog(state);
    return any_processed;
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
    const WasapiOutputState& wasapi_output,
    const hibiki::WasapiSinkHandoffSnapshotV1& wasapi_snapshot,
    const bool system_volume_enabled,
    const std::string_view system_volume_detail,
    const bool session_routing_enabled,
    const std::string_view session_routing_detail,
    const std::string_view process_loopback_detail) noexcept {
    hibiki::ControlStatusSnapshotV1 snapshot{};
    snapshot.sequence = 1U;
    snapshot.volume = volume;
    snapshot.route_count = 6U;
    set_route(snapshot.routes[0U], "engine-control", "引擎控制面",
              "named pipe 已啟動；目前為本機 user-space preview。",
              hibiki::ControlRouteHealthStateV1::Ready);
    set_route(snapshot.routes[1U], "main-output", "主輸出",
              wasapi_output.requested ? wasapi_route_detail(wasapi_output, wasapi_snapshot)
                                      : physical_catalog_detail,
              wasapi_output.requested ? wasapi_route_state(wasapi_output, wasapi_snapshot)
                                      : hibiki::ControlRouteHealthStateV1::Unavailable,
              wasapi_output.requested ? 0U : 1U);
    set_route(snapshot.routes[2U], "windows-volume", "Windows 音量",
              system_volume_detail,
              system_volume_enabled ? hibiki::ControlRouteHealthStateV1::Ready
                                     : hibiki::ControlRouteHealthStateV1::Unavailable,
              system_volume_enabled ? 0U : 1U);
    set_route(snapshot.routes[3U], "vendor-asio", "廠商 ASIO",
              "Strict Direct／廠商 ASIO 不會被透明攔截。",
              hibiki::ControlRouteHealthStateV1::Bypassed);
    set_route(snapshot.routes[4U], "windows-session", "Windows App／Session",
              session_routing_detail,
              session_routing_enabled ? hibiki::ControlRouteHealthStateV1::Ready
                                       : hibiki::ControlRouteHealthStateV1::Unavailable,
              session_routing_enabled ? 0U : 1U);
    set_route(snapshot.routes[5U], "process-loopback", "Process Loopback",
              process_loopback_detail,
              session_routing_enabled ? hibiki::ControlRouteHealthStateV1::Pending
                                       : hibiki::ControlRouteHealthStateV1::Unavailable,
              session_routing_enabled ? 0U : 1U);
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
    const bool session_routing_requested =
        has_command_line_flag(argc, argv, L"--enable-session-routing");
    const bool wasapi_output_requested =
        has_command_line_flag(argc, argv, L"--enable-wasapi-output");
    const bool test_tone_requested =
        has_command_line_flag(argc, argv, L"--enable-test-tone");

    hibiki::AudioEngineModel engine;
    hibiki::EngineControlWorkerV1 control_worker{engine};
    // Custom scene cards arrive over control IPC; the preview host owns the
    // fixed-capacity catalog for the lifetime of the worker.
    control_worker.ensure_owned_scene_catalog();
    IrPrepareState ir_state{&engine};
    control_worker.set_ir_prepare_handler(prepare_ir_file, &ir_state);
    SystemVolumeState system_volume;
    system_volume.enabled = system_volume_requested;
    SessionRoutingState session_routing;
    session_routing.requested = session_routing_requested;
    WasapiOutputState wasapi_output;
    wasapi_output.engine = &engine;
    wasapi_output.requested = wasapi_output_requested;
    TestToneState test_tone;
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
    const auto wasapi_started = wasapi_output_requested && physical_catalog_ready
                                    ? start_default_wasapi_output(wasapi_output, engine,
                                                                 physical_catalog.catalog())
                                    : false;
    if (wasapi_started && test_tone_requested) {
        (void)prepare_test_tone(test_tone, wasapi_output, engine);
    }
    const auto initial_wasapi_snapshot = engine.wasapi_output_snapshot();
    const std::string catalog_detail = physical_catalog_ready
        ? "physical catalog ready; Preview sink disabled unless WASAPI opt-in is requested."
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
    std::string session_routing_detail = session_routing_requested
        ? "session routing requested; binding Windows App sessions..."
        : "session routing disabled; Preview will not enumerate Apps.";
    std::string process_loopback_detail = session_routing_requested
        ? "session source is worker-owned; process delivery remains unverified."
        : "session routing disabled; process source is not bound.";
    if (session_routing_requested) {
        hibiki::SessionCatalogSnapshotV1 initial_session_catalog{};
        initial_session_catalog.sequence = 1U;
        if (!session_routing.catalog_store.publish(initial_session_catalog)) {
            session_routing_detail = "session catalog store unavailable; routing disabled.";
        }
        if (device_enumerator != nullptr) {
            const auto bind_result = bind_default_session_route_if_changed(
                device_enumerator, session_routing);
            if (SUCCEEDED(bind_result)) {
                const auto refresh_result = session_routing.coordinator.refresh();
                if (refresh_result != hibiki::WindowsAudioSessionRouteRefreshResultV1::Degraded &&
                    publish_session_catalog(session_routing)) {
                    session_routing_detail =
                        "session catalog linked; per-App controls enabled; delivery unverified.";
                } else {
                    session_routing.coordinator.unbind();
                    session_routing.bound = false;
                    session_routing_detail =
                        "session enumeration failed; per-App controls disabled.";
                }
            } else {
                session_routing_detail =
                    "default render session endpoint unavailable; routing disabled.";
            }
        } else {
            session_routing_detail = "Windows session endpoint unavailable; routing disabled.";
        }
    }
    const bool system_volume_active = system_volume_requested && system_volume.bound;
    const bool session_routing_active = session_routing_requested && session_routing.bound;
    if (!session_routing_active && session_routing_requested) {
        process_loopback_detail = "session route unavailable; process source remains unbound.";
    }
    control_worker.set_session_volume_handler(enqueue_session_volume_command, &session_routing);
    control_worker.set_session_route_handler(enqueue_session_route_command, &session_routing);
    control_worker.set_session_route_rule_handler(enqueue_session_route_rule_command,
                                                  &session_routing);
    hibiki::ControlStatusSnapshotStoreV1 status_store;
    auto status = make_initial_status(engine.volume(), catalog_detail, wasapi_output,
                                      initial_wasapi_snapshot, system_volume_active,
                                      system_volume_detail, session_routing_active,
                                      session_routing_detail, process_loopback_detail);
    if (!status_store.publish(status)) return 4;
    hibiki::ControlPlaneHostV1 host;
    hibiki::IpcNamedPipeConfigV1 pipe_config{kPipeName, 64U * 1024U, 1000U};
    pipe_config.require_first_pipe_instance = true;
    if (!host.start_with_queue(pipe_config,
                               physical_catalog_ready ? physical_catalog.snapshot_store() : nullptr,
                               &status_store,
                               session_routing_requested ? &session_routing.catalog_store
                                                         : nullptr)) {
        engine.stop_wasapi_output();
        physical_catalog.unbind();
        session_routing.coordinator.unbind();
        system_volume.broker.unbind();
        if (device_enumerator != nullptr) device_enumerator->Release();
        if (com_initialized && com_result != RPC_E_CHANGED_MODE) CoUninitialize();
        return 3;
    }

    auto next_catalog_poll = std::chrono::steady_clock::now() + std::chrono::milliseconds{250};
    auto next_session_poll = std::chrono::steady_clock::now() + std::chrono::milliseconds{250};
    auto next_volume_poll = std::chrono::steady_clock::now() + std::chrono::milliseconds{50};
    auto next_volume_write = std::chrono::steady_clock::now();
    auto next_test_tone_render = std::chrono::steady_clock::now();
    while (!g_stop.load(std::memory_order_acquire)) {
        const auto now = std::chrono::steady_clock::now();
        if (session_routing_requested && device_enumerator != nullptr &&
            now >= next_session_poll) {
            const auto bind_result = bind_default_session_route_if_changed(
                device_enumerator, session_routing);
            if (bind_result == S_OK) {
                const auto refresh_result = session_routing.coordinator.refresh();
                if (refresh_result != hibiki::WindowsAudioSessionRouteRefreshResultV1::Degraded) {
                    (void)publish_session_catalog(session_routing);
                } else {
                    session_routing.coordinator.unbind();
                    session_routing.bound = false;
                }
            } else if (bind_result == S_FALSE && session_routing.bound) {
                const auto refresh_result = session_routing.coordinator.poll_and_refresh();
                if (refresh_result != hibiki::WindowsAudioSessionRouteRefreshResultV1::NoChange &&
                    refresh_result != hibiki::WindowsAudioSessionRouteRefreshResultV1::Degraded &&
                    refresh_result != hibiki::WindowsAudioSessionRouteRefreshResultV1::Unbound) {
                    (void)publish_session_catalog(session_routing);
                }
                if (refresh_result == hibiki::WindowsAudioSessionRouteRefreshResultV1::Degraded) {
                    session_routing.bound = false;
                }
            } else if (FAILED(bind_result)) {
                session_routing.coordinator.unbind();
                session_routing.bound = false;
            }
            next_session_poll = now + std::chrono::milliseconds{250};
        }
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
        (void)drain_session_commands(session_routing);
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
        if (wasapi_output.test_tone_enabled && now >= next_test_tone_render) {
            (void)render_test_tone(test_tone, engine);
            next_test_tone_render = now + std::chrono::milliseconds{10};
        }
        const auto wasapi_snapshot = engine.wasapi_output_snapshot();
        const auto volume = engine.volume();
        bool status_changed = false;
        if (!same_volume(volume, status.volume)) {
            status.volume = volume;
            status_changed = true;
        }
        const auto previous_main_output_route = status.routes[1U];
        set_route(status.routes[1U], "main-output", "主輸出",
                  wasapi_output.requested
                      ? wasapi_route_detail(wasapi_output, wasapi_snapshot)
                      : (physical_catalog_ready
                             ? std::string_view(
                                   "physical catalog ready; shared-mode WASAPI sink disabled by default.")
                             : std::string_view("physical catalog unavailable; safe Preview retained.")),
                  wasapi_output.requested
                      ? wasapi_route_state(wasapi_output, wasapi_snapshot)
                      : hibiki::ControlRouteHealthStateV1::Unavailable,
                  wasapi_output.requested ? 0U : 1U);
        if (!same_route(previous_main_output_route, status.routes[1U])) status_changed = true;
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
        const auto session_snapshot = session_routing.coordinator.snapshot();
        const bool session_route_ready = session_routing_requested && session_routing.bound &&
                                         !session_snapshot.degraded;
        const auto session_route_state = session_snapshot.degraded
            ? hibiki::ControlRouteHealthStateV1::Degraded
            : session_route_ready && session_snapshot.has_graph
                ? hibiki::ControlRouteHealthStateV1::Ready
                : session_route_ready && session_snapshot.session_count > 0U
                    ? hibiki::ControlRouteHealthStateV1::Pending
                    : hibiki::ControlRouteHealthStateV1::Unavailable;
        const auto process_route_state = session_snapshot.degraded
            ? hibiki::ControlRouteHealthStateV1::Degraded
            : session_route_ready && session_snapshot.routed_count > 0U
                ? hibiki::ControlRouteHealthStateV1::Pending
                : hibiki::ControlRouteHealthStateV1::Unavailable;
        const auto session_detail = !session_routing_requested
            ? std::string_view("session routing disabled; Preview will not enumerate Apps.")
            : !session_routing.bound
                ? std::string_view("session endpoint unavailable; per-App controls disabled.")
                : session_snapshot.degraded
                    ? std::string_view("session enumeration degraded; controls fail closed.")
                    : std::string_view("session catalog linked; per-App controls enabled; delivery unverified.");
        const auto process_detail = !session_routing_requested
            ? std::string_view("session routing disabled; process source is not bound.")
            : std::string_view("process-tree source remains worker-owned; physical delivery unverified.");
        const auto previous_session_route = status.routes[4U];
        const auto previous_process_route = status.routes[5U];
        set_route(status.routes[4U], "windows-session", "Windows App／Session", session_detail,
                  session_route_state, session_routing_requested && session_routing.bound ? 0U : 1U);
        set_route(status.routes[5U], "process-loopback", "Process Loopback", process_detail,
                  process_route_state, session_routing_requested && session_routing.bound ? 0U : 1U);
        if (!same_route(previous_session_route, status.routes[4U]) ||
            !same_route(previous_process_route, status.routes[5U])) {
            status_changed = true;
        }
        if (status_changed) {
            ++status.sequence;
            (void)status_store.publish(status);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    host.stop();
    engine.stop_wasapi_output();
    physical_catalog.unbind();
    session_routing.coordinator.unbind();
    system_volume.broker.unbind();
    if (device_enumerator != nullptr) device_enumerator->Release();
    if (com_initialized && com_result != RPC_E_CHANGED_MODE) CoUninitialize();
    return 0;
}
