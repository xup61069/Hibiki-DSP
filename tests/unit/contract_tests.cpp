#include "hibiki/contracts.hpp"
#include "hibiki/control_payloads.hpp"
#include "hibiki/control_service.hpp"
#include "hibiki/control_status.hpp"
#include "hibiki/device_switch.hpp"
#include "hibiki/device_recovery.hpp"
#include "hibiki/device_catalog.hpp"
#include "hibiki/device_catalog_snapshot.hpp"
#include "hibiki/ipc.hpp"
#include "hibiki/ipc_pipe.hpp"
#include "hibiki/asio_bridge.hpp"
#include "hibiki/calibration.hpp"
#include "hibiki/calibration_compiler.hpp"
#include "hibiki/plugin_host.hpp"
#include "hibiki/vst3_sandbox.hpp"
#include "hibiki/vst3_worker_protocol.hpp"
#include "hibiki/vst3_worker_pipe.hpp"
#include "hibiki/latency_compensation.hpp"
#include "hibiki/latency_graph_commit.hpp"
#include "hibiki/vst3_parameter_timeline.hpp"
#include "hibiki/vst3_timeline_editor.hpp"
#include "hibiki/vst3_timeline_persistence.hpp"
#include "hibiki/vst3_timeline_file_store.hpp"
#include "hibiki/vst3_supervisor_surface.hpp"
#include "hibiki/vst3_worker_lane.hpp"
#include "hibiki/vst3_scene_automation.hpp"
#include "hibiki/vst3_scene_state.hpp"
#include "hibiki/tab_bridge.hpp"
#include "hibiki/asio_transport_v1.h"
#include "hibiki/output_sink.hpp"
#include "hibiki/output_crossfade.hpp"
#include "hibiki/output_handoff.hpp"
#include "hibiki/output_fanout.hpp"
#include "hibiki/exporters.hpp"
#include "hibiki/engine_control.hpp"
#include "hibiki/audio_engine.hpp"
#include "hibiki/audio_session_registry.hpp"
#include "hibiki/driver_control_bridge.hpp"
#include "hibiki/driver_stream_bridge.hpp"
#include "hibiki/session_catalog.hpp"
#include "hibiki/session_command_queue.hpp"

extern "C" {
#include "hibiki/driver_control_v1.h"
#include "hibiki/driver_control_transport_v1.h"
#include "hibiki/driver_stream_transport_v1.h"
#include "hibiki/driver_validation_v1.h"
#include "hibiki/wavert_endpoint_state_v1.h"
#include "hibiki/wavert_stream_v1.h"
#include "hibiki/endpoint_topology_v1.h"
}
#include "hibiki/iso226.hpp"
#include "hibiki/ir_phase.hpp"
#include "hibiki/wav_ir.hpp"
#include "hibiki/scene_graph.hpp"
#include "hibiki/scene_catalog.hpp"
#include "hibiki/vst3_bus_layout.hpp"
#include "hibiki/scene_presets.hpp"
#include "hibiki/scene_safety.hpp"
#include "hibiki/session_route.hpp"
#include "hibiki/session_route_rules.hpp"
#include "hibiki/volume_state.hpp"
#include "hibiki/program_loudness.hpp"
#include "hibiki/peq_dsp.hpp"
#include "hibiki/ir_convolver.hpp"
#include "hibiki/noise_suppressor.hpp"
#include "hibiki/virtual_mic.hpp"
#include "hibiki/true_peak_limiter.hpp"
#if defined(_WIN32)
#include <chrono>
#include <process.h>
#include <windows.h>
#include "hibiki/windows_volume_broker.hpp"
#include "hibiki/windows_volume_link.hpp"
#include "hibiki/windows_device_watcher.hpp"
#include "hibiki/windows_device_catalog.hpp"
#include "hibiki/windows_audio_session_watcher.hpp"
#include "hibiki/windows_audio_session_route.hpp"
#include "hibiki/windows_process_loopback.hpp"
#include "hibiki/windows_process_loopback_lane.hpp"
#include "hibiki/process_loopback_plan.hpp"
#include "hibiki/windows_wasapi_output.hpp"
#include "hibiki/windows_wasapi_handoff.hpp"
#include "hibiki/windows_wasapi_fanout.hpp"
#endif

#include <cmath>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            std::fprintf(stderr, "contract check failed: %s (%s:%d)\n", #condition,       \
                         __FILE__, __LINE__);                                               \
            return 1;                                                                       \
        }                                                                                   \
    } while (false)

#if defined(_WIN32)
bool acknowledge_ipc_request(const hibiki::IpcFrameV1& request,
                             hibiki::IpcFrameV1& response,
                             void*) noexcept {
    response.header.type = hibiki::IpcMessageType::Ack;
    response.header.request_id = request.header.request_id;
    response.payload.clear();
    return true;
}
#endif

bool accept_control_command(const hibiki::ControlCommandV1& command, void* context) noexcept {
    if (context == nullptr) return false;
    auto* accepted = static_cast<bool*>(context);
    *accepted = command.type == hibiki::IpcMessageType::SceneApply;
    return *accepted;
}

bool accept_catalog_request(const hibiki::ControlCommandV1& command, void* context) noexcept {
    if (context == nullptr) return false;
    auto* accepted = static_cast<bool*>(context);
    *accepted = command.type == hibiki::IpcMessageType::DeviceCatalogRequest;
    return *accepted;
}

bool accept_status_request(const hibiki::ControlCommandV1& command, void* context) noexcept {
    if (context == nullptr) return false;
    auto* accepted = static_cast<bool*>(context);
    *accepted = command.type == hibiki::IpcMessageType::ControlStatusRequest;
    return *accepted;
}

bool accept_session_catalog_request(const hibiki::ControlCommandV1& command,
                                    void* context) noexcept {
    if (context == nullptr) return false;
    auto* accepted = static_cast<bool*>(context);
    *accepted = command.type == hibiki::IpcMessageType::SessionCatalogRequest;
    return *accepted;
}

struct SnapshotReplyContext {
    const std::uint8_t* payload{nullptr};
    std::size_t payload_bytes{0U};
};

bool provide_catalog_snapshot(hibiki::IpcFrameV1& response, void* context) noexcept {
    if (context == nullptr) return false;
    const auto* source = static_cast<const SnapshotReplyContext*>(context);
    if (source->payload == nullptr || source->payload_bytes == 0U ||
        source->payload_bytes > hibiki::kDeviceCatalogSnapshotPayloadBytesV1) {
        return false;
    }
    try {
        response.header.type = hibiki::IpcMessageType::DeviceCatalogSnapshot;
        response.payload.assign(source->payload, source->payload + source->payload_bytes);
        return true;
    } catch (...) {
        response = {};
        return false;
    }
}

bool allow_scene_preflight(const hibiki::SceneProfileV1&, void* context) noexcept {
    return context != nullptr && *static_cast<const bool*>(context);
}

bool accept_device_switch(const hibiki::DeviceSwitchPayloadV1& request,
                          void* context) noexcept {
    if (context == nullptr || request.endpoint_id_bytes == 0U) return false;
    auto* accepted = static_cast<bool*>(context);
    *accepted = request.channels == 2U && request.sample_rate == 48000U &&
                request.buffer_frames == 128U;
    return *accepted;
}

bool accept_session_volume(const hibiki::SessionVolumeCommandV1& request,
                           void* context) noexcept {
    if (context == nullptr) return false;
    auto* accepted = static_cast<bool*>(context);
    *accepted = request.handle == ((2ULL << 32U) | 1ULL) &&
                request.catalog_sequence == 12U && request.mute == 1U &&
                request.requested_db_q16_16 == -622592;
    return *accepted;
}

bool accept_session_route(const hibiki::SessionRouteCommandV1& request,
                          void* context) noexcept {
    if (context == nullptr) return false;
    auto* accepted = static_cast<bool*>(context);
    *accepted = request.handle == ((2ULL << 32U) | 1ULL) &&
                request.catalog_sequence == 12U &&
                std::string_view(request.lane.data(), request.lane_bytes) == "game" &&
                std::string_view(request.output_group.data(), request.output_group_bytes) ==
                    "surround";
    return *accepted;
}

bool accept_session_route_rule(const hibiki::SessionRouteRuleCommandV1& request,
                               void* context) noexcept {
    if (context == nullptr) return false;
    auto* accepted = static_cast<bool*>(context);
    *accepted = request.operation == hibiki::SessionRouteRuleOperationV1::Upsert &&
                request.catalog_sequence == 12U && request.rule_id_bytes == 10U;
    return *accepted;
}

hibiki::Vst3PluginStateResultV1 migrate_test_plugin_state(
    const std::uint32_t source_version,
    const std::span<const std::uint8_t> source,
    const std::uint32_t target_version,
    const std::span<std::uint8_t> destination,
    std::size_t& bytes_written,
    void*) noexcept {
    bytes_written = 0U;
    if (source_version != 1U || target_version != 2U || destination.size() < source.size() + 1U) {
        return hibiki::Vst3PluginStateResultV1::migration_failed;
    }
    std::copy(source.begin(), source.end(), destination.begin());
    destination[source.size()] = 0x42U;
    bytes_written = source.size() + 1U;
    return hibiki::Vst3PluginStateResultV1::ok;
}

hibiki::Vst3PluginStateResultV1 migrate_oversized_plugin_state(
    const std::uint32_t,
    const std::span<const std::uint8_t>,
    const std::uint32_t,
    const std::span<std::uint8_t> destination,
    std::size_t& bytes_written,
    void*) noexcept {
    // Deliberately report a result larger than the caller-owned bounded span;
    // the store must reject it without exposing a partial state.
    bytes_written = destination.size() + 1U;
    return hibiki::Vst3PluginStateResultV1::ok;
}

int main() {
    using namespace hibiki;

    SceneProfileV1 scene;
    scene.id = "game";
    scene.name = "Game";
    scene.output_group = "main";
    CHECK(validate_scene(scene));
    scene.output_group = std::string(64, 'g');
    CHECK(validate_scene(scene));
    scene.output_group = std::string(65, 'g');
    CHECK(!validate_scene(scene));
    scene.output_group = "main";
    scene.lanes.push_back("game-lane");
    CHECK(validate_scene(scene));
    scene.lanes.clear();
    CHECK(validate_scene(scene));
    scene.lanes.push_back("");
    CHECK(!validate_scene(scene));
    scene.lanes[0] = std::string(65, 'x');
    CHECK(!validate_scene(scene));
    scene.lanes[0] = "game-lane";
    scene.lanes.push_back(std::string("lane\0hidden", 11U));
    CHECK(!validate_scene(scene));
    scene.lanes.clear();
    for (std::size_t i = 0U; i < 32U; ++i) {
        scene.lanes.push_back("lane-" + std::to_string(i));
    }
    CHECK(validate_scene(scene));
    scene.lanes.push_back("one-too-many");
    CHECK(!validate_scene(scene));
    scene.lanes.pop_back();
    scene.automation_timeline_ids.push_back("game-vst3-default");
    CHECK(validate_scene(scene));
    scene.automation_timeline_ids.push_back(std::string(65, 'x'));
    CHECK(!validate_scene(scene));
    scene.automation_timeline_ids.pop_back();
    scene.ir_reference = "calibration-a";
    CHECK(validate_scene(scene) && scene.ir_reference.size() == 13U);
    scene.ir_reference = "short";
    CHECK(!validate_scene(scene));
    scene.ir_reference = std::string(65, 'x');
    CHECK(!validate_scene(scene));
    scene.ir_reference.clear();
    scene.ir_reference.push_back('a');
    scene.ir_reference.push_back('\0');
    scene.ir_reference.append("calibration");
    CHECK(!validate_scene(scene));
    scene.ir_reference.clear();
    CHECK(validate_scene(scene));

    const auto game_ir = resolve_ir_phase_policy(
        IrPhasePolicyV1{1, IrPhaseMode::MinimumPhase, 1.0});
    CHECK(game_ir.valid && !game_ir.uses_fir && game_ir.added_delay_ms == 0.0);
    const auto balanced_ir = resolve_ir_phase_policy(
        IrPhasePolicyV1{1, IrPhaseMode::MixedPhase, 0.5});
    CHECK(balanced_ir.valid && balanced_ir.uses_fir &&
          std::abs(balanced_ir.added_delay_ms - 40.0) < 1e-12);
    const auto movie_ir = resolve_ir_phase_policy(
        IrPhasePolicyV1{1, IrPhaseMode::LinearPhase, 1.0});
    CHECK(movie_ir.valid && movie_ir.uses_fir &&
          std::abs(movie_ir.added_delay_ms - 160.0) < 1e-12);
    CHECK(!validate_ir_phase_policy(IrPhasePolicyV1{1, IrPhaseMode::Bypass, 0.1}));
    CHECK(!validate_ir_phase_policy(IrPhasePolicyV1{1, IrPhaseMode::MixedPhase, 1.1}));

    ProgramAwareLevelControllerV1 program_loudness;
    CHECK(validate_program_aware_policy(ProgramAwareLevelPolicyV1{}));
    CHECK(!validate_program_aware_policy(
        ProgramAwareLevelPolicyV1{1, true, -23.0, 6.0, 12.0, 3000.0, 0.0, -70.0}));
    CHECK(program_loudness.configure(ProgramAwareLevelPolicyV1{1, true, -23.0, 6.0, 12.0,
                                                                3000.0, 6.0, -70.0},
                                     48000U));
    float loud_program[4800];
    std::fill(std::begin(loud_program), std::end(loud_program), 0.5F);
    CHECK(program_loudness.process_interleaved(loud_program, 4800U, 1U));
    CHECK(program_loudness.status().valid && program_loudness.status().applied_gain_db < 0.0);
    CHECK(std::isfinite(loud_program[0]));
    program_loudness.reset();
    float silent_program[128]{};
    CHECK(program_loudness.process_interleaved(silent_program, 128U, 1U));
    CHECK(program_loudness.status().silence_gated);

    ProgramAwareLevelControllerV1 k_weighted_program;
    CHECK(k_weighted_program.configure(
        ProgramAwareLevelPolicyV1{1, true, -23.0, 6.0, 12.0, 3000.0, 6.0, -70.0,
                                  ProgramAwareMeterModeV1::KWeightedProxy, -1},
        48000U));
    std::array<float, 4800> k_weighted_tone{};
    for (std::size_t frame = 0U; frame < k_weighted_tone.size(); ++frame) {
        k_weighted_tone[frame] = static_cast<float>(
            0.25 * std::sin(2.0 * 3.14159265358979323846 * 1000.0 *
                            static_cast<double>(frame) / 48000.0));
    }
    CHECK(k_weighted_program.process_interleaved(k_weighted_tone.data(),
                                                 k_weighted_tone.size(), 1U));
    CHECK(k_weighted_program.status().meter_mode == ProgramAwareMeterModeV1::KWeightedProxy);
    CHECK(k_weighted_program.status().valid &&
          std::isfinite(k_weighted_program.status().measured_dbfs));
    CHECK(validate_program_aware_policy(ProgramAwareLevelPolicyV1{
        1, false, -23.0, 6.0, 12.0, 3000.0, 6.0, -70.0,
        ProgramAwareMeterModeV1::KWeightedProxy, 3}));
    CHECK(!validate_program_aware_policy(ProgramAwareLevelPolicyV1{
        1, false, -23.0, 6.0, 12.0, 3000.0, 6.0, -70.0,
        ProgramAwareMeterModeV1::KWeightedProxy, 8}));

    PeqProcessorV1 peq;
    const std::array<PeqFilterV1, 1> peq_filters{{PeqFilterV1{1000.0, 6.0, 1.0}}};
    CHECK(peq.prepare(peq_filters, 48000U, 2U));
    float peq_block[8] = {0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    CHECK(peq.process_interleaved(peq_block, 4U));
    CHECK(std::isfinite(peq_block[2]) && peq_block[2] > 1.0F);
    CHECK(!peq.prepare(std::span<const PeqFilterV1>(peq_filters), 48000U, 9U));

    auto ir = std::make_unique<IrConvolverV1>();
    const std::array<float, 2> ir_kernel{{1.0F, 0.5F}};
    const auto ir_phase = resolve_ir_phase_policy(
        IrPhasePolicyV1{1, IrPhaseMode::MixedPhase, 0.5});
    CHECK(ir->prepare(ir_kernel, 2U, 1U, 2U, 48000U, ir_phase));
    float ir_block[4] = {1.0F, 1.0F, 0.0F, 0.0F};
    CHECK(ir->process_interleaved(ir_block, 2U, 2U));
    CHECK(std::abs(ir_block[0] - 1.0F) < 1e-6F &&
          std::abs(ir_block[1] - 1.0F) < 1e-6F &&
          std::abs(ir_block[2] - 0.5F) < 1e-6F &&
          std::abs(ir_block[3] - 0.5F) < 1e-6F);
    CHECK(!ir->prepare(ir_kernel, 2U, 1U, 2U, 48000U,
                      IrPhaseResolutionV1{1, IrPhaseMode::Bypass, 0.0, 0.0, false, false}));

    BasicNoiseSuppressorV1 noise_suppressor;
    CHECK(validate_noise_suppressor_policy(BasicNoiseSuppressorPolicyV1{}));
    CHECK(noise_suppressor.configure(
        BasicNoiseSuppressorPolicyV1{1, true, -40.0, -30.0, 1.0, 10.0, 0.0}, 48000U, 1U));
    std::array<float, 480> noise_block{};
    noise_block.fill(0.01F);
    CHECK(noise_suppressor.process_interleaved(noise_block.data(), noise_block.size()));
    CHECK(std::abs(noise_block.back()) < std::abs(noise_block.front()));
    noise_suppressor.reset();
    std::array<float, 16> voice_block{};
    voice_block.fill(0.5F);
    CHECK(noise_suppressor.process_interleaved(voice_block.data(), voice_block.size()));
    CHECK(voice_block.back() > 0.4F);
    // Regression: gate opening must use attack_ms (fast), not release_ms (slow).
    // Without reset, close the gate first with sub-threshold signal.
    BasicNoiseSuppressorV1 gate_transition;
    CHECK(gate_transition.configure(
        BasicNoiseSuppressorPolicyV1{1, true, -40.0, -30.0, 1.0, 10.0, 80.0}, 48000U, 1U));
    std::array<float, 480> silence_block{};
    silence_block.fill(0.001F);
    CHECK(gate_transition.process_interleaved(silence_block.data(), silence_block.size()));
    // After processing silence, gain should be near floor (gate closed).
    CHECK(silence_block.back() < 0.0005F);
    // Now feed a loud above-threshold signal without resetting.
    std::array<float, 48> open_block{};
    open_block.fill(0.5F);
    CHECK(gate_transition.process_interleaved(open_block.data(), open_block.size()));
    // With attack_ms=1.0 at 48kHz (~21 samples per time constant), after 48 frames
    // the gate is well open. The reversed mapping (opening with the 10 ms
    // release coefficient) only reached ~0.062 here.
    CHECK(open_block.back() > 0.1F);
    // Compare two short close tails after a common recovery point so envelope
    // history cannot mask the rate change: the second (attack_ms) tail must
    // collapse much further than the first (release_ms) tail.
    std::array<float, 96> recovery_block{};
    recovery_block.fill(0.5F);
    CHECK(gate_transition.process_interleaved(recovery_block.data(),
                                              recovery_block.size()));
    CHECK(recovery_block.back() > 1.0e-4F);
    std::array<float, 480> quiet_tail{};
    quiet_tail.fill(0.001F);
    CHECK(gate_transition.process_interleaved(quiet_tail.data(), quiet_tail.size()));
    std::array<float, 24> reopen_block{};
    reopen_block.fill(0.5F);
    CHECK(gate_transition.process_interleaved(reopen_block.data(),
                                              reopen_block.size()));
    CHECK(reopen_block.back() > 1.0e-4F);
    std::array<float, 960> fast_tail{};
    fast_tail.fill(0.001F);
    CHECK(gate_transition.process_interleaved(fast_tail.data(), fast_tail.size()));
    // Closing with attack_ms collapses to well under 5 percent of the
    // release_ms tail level within the same window; compare magnitudes
    // because the high-pass makes both tail samples negative.
    CHECK(std::abs(fast_tail.back()) < std::abs(quiet_tail.back()) * 0.05F);

    // Regression: hysteresis prevents gate chatter. A signal that oscillates
    // between just below and just above the configured threshold must not
    // cause rapid open/close cycling. With a -40 dBFS threshold, the close
    // boundary is at 0.01 linear; reopen requires ~0.01259 (2 dB above).
    BasicNoiseSuppressorV1 chatter_gate;
    CHECK(chatter_gate.configure(
        BasicNoiseSuppressorPolicyV1{1, true, -40.0, -30.0, 1.0, 10.0, 80.0},
        48000U, 1U));
    // Close the gate first with well-below-threshold silence.
    std::array<float, 480> chatter_silence{};
    chatter_silence.fill(0.001F);
    CHECK(chatter_gate.process_interleaved(chatter_silence.data(),
                                           chatter_silence.size()));
    // Feed alternating blocks near the threshold: below (closes), then in the
    // hysteresis band between threshold and threshold + 2 dB (holds closed).
    // The old hard-threshold code would have reopened on every crossing.
    constexpr std::size_t kChatterBlocks = 8U;
    float chatter_gain_min = 1.0F;
    float chatter_gain_max = 0.0F;
    for (std::size_t block = 0U; block < kChatterBlocks; ++block) {
        const auto near_threshold =
            (block % 2U == 0U) ? 0.009F : 0.011F;
        std::array<float, 48> chatter_block{};
        chatter_block.fill(static_cast<float>(near_threshold));
        CHECK(chatter_gate.process_interleaved(chatter_block.data(),
                                               chatter_block.size()));
        // Track the gain trajectory to verify it stays bounded.
        for (const auto sample : chatter_block) {
            const auto approx_gain = std::abs(sample) / near_threshold;
            chatter_gain_min = (std::min)(chatter_gain_min, approx_gain);
            chatter_gain_max = (std::max)(chatter_gain_max, approx_gain);
        }
    }
    // With hysteresis the gain stays bounded within floor..unity without
    // rapid cycling. Without hysteresis, the gain would repeatedly swing
    // between floor (~0.0316) and unity across these blocks.
    CHECK(chatter_gain_max < 0.5F);

    OutputGroupVolumeStateV1 state;
    state.requested_db = 3.0;
    state.safety_ceiling_db = -6.0;
    state = reconcile(state);
    CHECK(std::abs(state.effective_db + 6.0) < 1e-12);

    state.actuator = ActuatorMode::StrictDirect;
    state = reconcile(state);
    CHECK(std::abs(state.effective_db) < 1e-12);

    VolumeRampProcessorV1 ramp;
    ramp.reset(-60.0, false);
    ramp.observe_target(db_to_q16_16(0.0), false, 8000U);
    float previous_gain = 0.0F;
    for (std::size_t frame = 0U; frame < 64U; ++frame) {
        const auto gain = ramp.next_gain();
        CHECK(gain >= previous_gain);
        previous_gain = gain;
    }
    CHECK(std::abs(previous_gain - 1.0F) < 1e-6F && ramp.remaining_frames() == 0U);
    ramp.observe_target(db_to_q16_16(-6.0206), true, 8000U);
    for (std::size_t frame = 0U; frame < 40U; ++frame) (void)ramp.next_gain();
    CHECK(ramp.next_gain() == 0.0F);
    ramp.observe_target(db_to_q16_16(-6.0206), false, 8000U);
    for (std::size_t frame = 0U; frame < 120U; ++frame) (void)ramp.next_gain();
    CHECK(std::abs(ramp.next_gain() - 0.5F) < 1e-5F);

    TruePeakLimiterV1 limiter;
    float limiter_samples[] = {1.2F, -1.1F, 0.8F, -0.8F};
    const auto limiter_gain = limiter.limit_in_place(limiter_samples, 2U, 2U, -1.0, 48000U);
    CHECK(limiter_gain < 1.0F && std::abs(limiter_samples[0]) <= 0.891251F + 1e-5F &&
          std::abs(limiter_samples[1]) <= 0.891251F + 1e-5F);
    limiter.reset();
    float finite_guard[] = {std::numeric_limits<float>::quiet_NaN(), 0.25F};
    CHECK(limiter.limit_in_place(finite_guard, 1U, 2U, -1.0, 48000U) == 1.0F &&
          finite_guard[0] == 0.0F);
    limiter.reset();
    float limiter_loud[] = {2.0F, -2.0F};
    const auto loud_gain = limiter.limit_in_place(limiter_loud, 1U, 2U, -1.0, 48000U);
    CHECK(loud_gain < 1.0F);
    float limiter_quiet[] = {0.001F, -0.001F};
    const auto recovery_gain =
        limiter.limit_in_place(limiter_quiet, 1U, 2U, -1.0, 48000U);
    CHECK(recovery_gain > loud_gain && recovery_gain < 1.0F &&
          recovery_gain <= loud_gain * 2.0F + 1.0e-6F);
    const auto settled_gain = limiter.limit_in_place(limiter_quiet, 1U, 2U, -1.0, 48000U);
    CHECK(settled_gain > recovery_gain && settled_gain < 1.0F);

    // Regression: recovery rate must be independent of render block size.
    // Two limiters engage on a loud block, then recover over the same total
    // frame span using different block sizes; gains must match throughout.
    TruePeakLimiterV1 limiter_small_blocks;
    TruePeakLimiterV1 limiter_large_blocks;
    float engage_small[] = {2.0F, -2.0F};
    float engage_large[] = {2.0F, -2.0F};
    const auto engage_gain_small =
        limiter_small_blocks.limit_in_place(engage_small, 1U, 2U, -1.0, 48000U);
    const auto engage_gain_large =
        limiter_large_blocks.limit_in_place(engage_large, 1U, 2U, -1.0, 48000U);
    CHECK(std::abs(engage_gain_small - engage_gain_large) < 1e-6F);

    // Recover over the same 960-frame span using 20 x 48-frame blocks versus
    // one 960-frame block; the applied gain must match because release is
    // per-ms, not per-block.  Both limiters start from a fresh loud block, so
    // their initial attenuation is identical.
    static float quiet_small[96];   // 48 frames x 2 channels per call
    static float quiet_large[1920]; // 960 frames x 2 channels in one call
    for (auto& f : quiet_small) f = 0.001F;
    for (auto& f : quiet_large) f = 0.001F;
    for (std::size_t b = 0; b < 20; ++b) {
        (void)limiter_small_blocks.limit_in_place(quiet_small, 48U, 2U,
                                                  -1.0, 48000U);
    }
    const auto small_final =
        limiter_small_blocks.applied_gain_for_test();

    (void)limiter_large_blocks.limit_in_place(quiet_large, 960U, 2U,
                                              -1.0, 48000U);
    const auto large_final =
        limiter_large_blocks.applied_gain_for_test();

    // Compare in dB domain; pow accumulation differs slightly between paths.
    const auto small_db = 20.0 * std::log10(static_cast<double>(small_final));
    const auto large_db = 20.0 * std::log10(static_cast<double>(large_final));
    CHECK(std::abs(small_db - large_db) < 0.01);

    // Regression: recovery must be equivalent across sample rates over the
    // same elapsed time.  480 frames at 48 kHz and 960 frames at 96 kHz are
    // both 10 ms; after one quiet block each limiter should have recovered
    // by approximately the same number of dB.
    TruePeakLimiterV1 limiter_48k;
    TruePeakLimiterV1 limiter_96k;
    float engage_48k[] = {2.0F, -2.0F};
    float engage_96k[] = {2.0F, -2.0F};
    const auto engage_48k_gain =
        limiter_48k.limit_in_place(engage_48k, 1U, 2U, -1.0, 48000U);
    const auto engage_96k_gain =
        limiter_96k.limit_in_place(engage_96k, 1U, 2U, -1.0, 96000U);
    CHECK(std::abs(engage_48k_gain - engage_96k_gain) < 1e-6F);

    static float quiet_48k[960];   // 480 frames x 2 channels
    static float quiet_96k[1920];  // 960 frames x 2 channels
    for (auto& f : quiet_48k) f = 0.001F;
    for (auto& f : quiet_96k) f = 0.001F;
    (void)limiter_48k.limit_in_place(quiet_48k, 480U, 2U, -1.0, 48000U);
    (void)limiter_96k.limit_in_place(quiet_96k, 960U, 2U, -1.0, 96000U);
    const auto gain_48k = limiter_48k.applied_gain_for_test();
    const auto gain_96k = limiter_96k.applied_gain_for_test();

    // Both should have recovered by ~6 dB/ms * 10 ms = ~60 dB from their
    // engaged level.  Compare the amount recovered in dB domain.
    const auto recovered_48k_db =
        20.0 * std::log10(static_cast<double>(gain_48k)) -
        20.0 * std::log10(static_cast<double>(engage_48k_gain));
    const auto recovered_96k_db =
        20.0 * std::log10(static_cast<double>(gain_96k)) -
        20.0 * std::log10(static_cast<double>(engage_96k_gain));
    CHECK(std::abs(recovered_48k_db - recovered_96k_db) < 0.01);

    const std::vector<IsoContourPoint> current{{100.0, 60.0}, {1000.0, 40.0}};
    const std::vector<IsoContourPoint> reference{{100.0, 50.0}, {1000.0, 40.0}};
    EqualLoudnessPolicyV1 policy;
    policy.max_boost_db = 6.0;
    const auto result = build_compensation(current, reference, policy);
    CHECK(result.points.size() == 2);
    CHECK(std::abs(result.points[1].gain_db) < 1e-12);
    CHECK(std::abs(result.points[0].gain_db - 6.0) < 1e-12);
    CHECK(result.limited);
    double one_k_spl = 0.0;
    CHECK(iso226_spl_from_phon(Iso226FormulaPointV1{1000.0, 0.30, 2.4, 0.0},
                               Iso226FormulaReferenceV1{0.30, 2.4}, 80.0, one_k_spl));
    CHECK(std::abs(one_k_spl - 80.0) < 1e-10);
    CHECK(!iso226_spl_from_phon(Iso226FormulaPointV1{1000.0, 0.30, 2.4, 0.0},
                                Iso226FormulaReferenceV1{0.30, 2.4}, 10.0, one_k_spl));
    CHECK(iso226_spl_from_phon(Iso226FormulaPointV1{1000.0, 0.25, 2.4, 0.0},
                               Iso226FormulaReferenceV1{0.25, 2.4}, 60.0, one_k_spl));
    CHECK(std::abs(one_k_spl - 60.0) < 1e-10);
    CHECK(iso226_spl_from_phon(Iso226FormulaPointV1{4000.0, 0.25, 50.0, 0.0},
                               Iso226FormulaReferenceV1{0.30, 2.4}, 90.0, one_k_spl));
    CHECK(!iso226_spl_from_phon(Iso226FormulaPointV1{4000.0, 0.25, 50.0, 0.0},
                                Iso226FormulaReferenceV1{0.30, 2.4}, 90.5, one_k_spl));
    CHECK(iso226_spl_from_phon(Iso226FormulaPointV1{5000.0, 0.25, 50.0, 0.0},
                               Iso226FormulaReferenceV1{0.30, 2.4}, 20.0, one_k_spl));
    CHECK(!iso226_spl_from_phon(Iso226FormulaPointV1{5000.0, 0.25, 50.0, 0.0},
                                Iso226FormulaReferenceV1{0.30, 2.4}, 19.5, one_k_spl));
    CHECK(iso226_spl_from_phon(Iso226FormulaPointV1{12500.0, 0.25, 50.0, 0.0},
                               Iso226FormulaReferenceV1{0.30, 2.4}, 80.0, one_k_spl));
    CHECK(!iso226_spl_from_phon(Iso226FormulaPointV1{12500.0, 0.25, 50.0, 0.0},
                                Iso226FormulaReferenceV1{0.30, 2.4}, 80.5, one_k_spl));
    CHECK(!iso226_spl_from_phon(Iso226FormulaPointV1{1000.0, 0.30, 2.4, 0.0},
                                Iso226FormulaReferenceV1{0.30, 2.4}, 0.0, one_k_spl));
    const std::array<Iso226FormulaPointV1, 2> formula_points{{
        {100.0, 0.25, 50.0, 0.0}, {1000.0, 0.30, 2.4, 0.0}}};
    const auto formula_result = build_formula_compensation(formula_points, 60.0, policy);
    CHECK(formula_result.points.size() == 2U &&
          std::abs(formula_result.points[1].gain_db) < 1e-10 &&
          std::isfinite(formula_result.points[0].gain_db));
    const std::array<Iso226FormulaPointV1, 1> no_anchor{{{100.0, 0.25, 50.0, 0.0}}};
    CHECK(build_formula_compensation(no_anchor, 60.0, policy).points.empty());
    EqualLoudnessPolicyV1 full_range_policy{};
    full_range_policy.measured_f3_hz = 20000.0;
    CHECK(build_formula_compensation(formula_points, 60.0, full_range_policy)
              .points.size() == 2U);
    full_range_policy.measured_f3_hz = 20000.1;
    CHECK(build_formula_compensation(formula_points, 60.0, full_range_policy).points.empty());
    full_range_policy.measured_f3_hz = 0.0;
    const std::array<Iso226FormulaPointV1, 3> valid_high_band_points{{
        {4000.0, 0.25, 50.0, 0.0},
        {5000.0, 0.25, 50.0, 0.0},
        {1000.0, 0.30, 2.4, 0.0}}};
    CHECK(build_formula_compensation(valid_high_band_points, 80.0, full_range_policy)
              .points.size() == 3U);
    const std::array<Iso226FormulaPointV1, 2> invalid_high_band_points{{
        {1000.0, 0.30, 2.4, 0.0}, {12500.0, 0.25, 50.0, 0.0}}};
    CHECK(build_formula_compensation(invalid_high_band_points, 85.0,
                                     full_range_policy).points.empty());

    EqualLoudnessPolicyV1 calibrated;
    calibrated.mode = EqualLoudnessMode::Calibrated;
    CHECK(!validate_policy(calibrated));

    const std::array<CalibrationResponsePointV1, 4> calibration_response{{
        {100.0, -10.0, 0.0}, {250.0, -2.0, 0.0}, {1000.0, 0.0, 0.0},
        {10000.0, 6.0, 0.0}}};
    CalibrationCompilePolicyV1 calibration_policy;
    calibration_policy.max_filters = 2U;
    calibration_policy.max_boost_db = 4.0;
    calibration_policy.max_cut_db = 3.0;
    const auto calibration_result = compile_bounded_peq_correction_v1(
        calibration_response, calibration_policy);
    CHECK(calibration_result.filters.size() == 2U && calibration_result.limited &&
          calibration_result.filters[0].frequency_hz == 100.0 &&
          std::abs(calibration_result.filters[0].gain_db - 4.0) < 1e-12 &&
          calibration_result.maximum_requested_correction_db == 10.0);
    CHECK(validate_calibration_response_v1(calibration_response, calibration_policy));
    {
        std::vector<CalibrationResponsePointV1> out_of_range(
            calibration_response.begin(), calibration_response.end());
        out_of_range[0].measured_db = -144.5;
        CHECK(!validate_calibration_response_v1(out_of_range, calibration_policy));
        out_of_range[0] = calibration_response[0];
        out_of_range[0].target_db = 12.5;
        CHECK(!validate_calibration_response_v1(out_of_range, calibration_policy));
        out_of_range[0] = calibration_response[0];
        out_of_range[0].measured_db = -144.0;
        out_of_range[0].target_db = 12.0;
        CHECK(validate_calibration_response_v1(out_of_range, calibration_policy));
    }
    auto unsorted_response = calibration_response;
    std::swap(unsorted_response[0], unsorted_response[1]);
    CHECK(!validate_calibration_response_v1(unsorted_response, calibration_policy));

    CalibrationCompilePolicyV1 calibration_cap_policy;
    calibration_cap_policy.max_filters = 16U;
    calibration_cap_policy.min_frequency_hz = 20.0;
    calibration_cap_policy.max_frequency_hz = 20000.0;
    calibration_cap_policy.min_q = 0.3;
    calibration_cap_policy.max_q = 12.0;
    calibration_cap_policy.min_spacing_octaves = 1.0 / 12.0;
    calibration_cap_policy.ignore_error_db = 0.25;
    std::vector<CalibrationResponsePointV1> calibration_candidates;
    for (std::uint32_t candidate_index = 0U; candidate_index < 17U; ++candidate_index) {
        const double frequency = 100.0 * std::pow(2.0, static_cast<double>(candidate_index) / 3.0);
        calibration_candidates.push_back(CalibrationResponsePointV1{frequency, -6.0, 0.0});
    }
    const auto calibration_cap_result = compile_bounded_peq_correction_v1(
        calibration_candidates, calibration_cap_policy);
    CHECK(calibration_cap_result.filters.size() == 16U && calibration_cap_result.limited);
    CHECK(calibration_cap_result.diagnostic.find("clipped or unrepresented residuals") !=
          std::string::npos);
    double cap_previous_frequency = 20.0;
    double cap_minimum_spacing = std::numeric_limits<double>::infinity();
    for (const auto& filter : calibration_cap_result.filters) {
        CHECK(filter.frequency_hz >= 20.0 && filter.frequency_hz <= 20000.0);
        CHECK(filter.q >= calibration_cap_policy.min_q &&
              filter.q <= calibration_cap_policy.max_q);
        CHECK(filter.frequency_hz > cap_previous_frequency);
        cap_minimum_spacing = (std::min)(
            cap_minimum_spacing,
            std::abs(std::log2(filter.frequency_hz / cap_previous_frequency)));
        cap_previous_frequency = filter.frequency_hz;
    }
    CHECK(cap_minimum_spacing >= calibration_cap_policy.min_spacing_octaves);
    CalibrationCompilePolicyV1 over_cap_policy = calibration_cap_policy;
    over_cap_policy.max_filters = 17U;
    CHECK(!validate_calibration_compile_policy_v1(over_cap_policy));
    CalibrationCompilePolicyV1 above_default_q_policy = calibration_cap_policy;
    above_default_q_policy.max_q = 12.5;
    CHECK(validate_calibration_compile_policy_v1(above_default_q_policy));
    CalibrationCompilePolicyV1 invalid_q_policy = calibration_cap_policy;
    invalid_q_policy.max_q = 100.1;
    CHECK(!validate_calibration_compile_policy_v1(invalid_q_policy));
    CalibrationCompilePolicyV1 invalid_spacing_policy = calibration_cap_policy;
    invalid_spacing_policy.min_spacing_octaves = 0.008;
    CHECK(!validate_calibration_compile_policy_v1(invalid_spacing_policy));
    calibrated.anchor_id = "speaker-anchor";
    CHECK(!validate_policy(calibrated));
    calibrated.standard = "iso-226-2023-calibrated";
    CHECK(validate_policy(calibrated));
    calibrated.anchor_id = std::string(64, 'a');
    CHECK(validate_policy(calibrated));
    calibrated.anchor_id = std::string(65, 'a');
    CHECK(!validate_policy(calibrated));
    calibrated.anchor_id = "speaker-anchor";
    calibrated.reference_phon = 95.0;
    CHECK(!validate_policy(calibrated));

    OutputGroupVolumeStateV1 linked;
    linked.generation = 4;
    CHECK(apply_windows_notification(linked, VolumeNotificationV1{-12.0, false, 5}) ==
          VolumeNotificationResult::Accepted);
    CHECK(linked.origin == VolumeOrigin::Windows);
    CHECK(apply_windows_notification(linked, VolumeNotificationV1{-6.0, false, 3}) ==
          VolumeNotificationResult::StaleGeneration);
    const auto q16 = db_to_q16_16(-6.0206);
    CHECK(std::abs(q16_16_to_db(q16) + 6.0206) < 0.00002);

    auto volume_bank = std::make_unique<OutputGroupVolumeBankV1>();
    CHECK(volume_bank->has_group("main") && volume_bank->register_group("movie") &&
          volume_bank->group_count() == 2U && !volume_bank->register_group(""));
    CHECK(volume_bank->apply_windows_notification(
              "movie", VolumeNotificationV1{-12.0, false, 1U}) ==
          VolumeNotificationResult::Accepted);
    CHECK(std::abs(volume_bank->state("movie").requested_db + 12.0) < 1e-12 &&
          volume_bank->apply_windows_notification("missing", VolumeNotificationV1{-3.0, false, 1U}) ==
              VolumeNotificationResult::Invalid);
    std::array<float, 128> movie_volume_samples{};
    for (std::size_t index = 0U; index < movie_volume_samples.size(); index += 2U) {
        movie_volume_samples[index] = 1.0F;
        movie_volume_samples[index + 1U] = -1.0F;
    }
    CHECK(volume_bank->apply_to_interleaved("movie", movie_volume_samples.data(), 64U, 2U, 8000U));
    CHECK(std::abs(movie_volume_samples[126] - 0.25118864F) < 1e-5F &&
          std::abs(movie_volume_samples[127] + 0.25118864F) < 1e-5F);

    hibiki_wavert_endpoint_state_v1 wavert_state{};
    CHECK(hibiki_wavert_endpoint_state_init_v1(
        &wavert_state, "8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10", 8, 48000,
        HIBIKI_ACTUATOR_INTERNAL_DSP));
    CHECK(hibiki_wavert_endpoint_state_apply_volume_v1(
        &wavert_state, -6 * 65536, -12 * 65536, 0, 2,
        "volume-windows"));
    CHECK(wavert_state.effective_db_q16_16 == -12 * 65536);
    CHECK(!hibiki_wavert_endpoint_state_apply_volume_v1(
        &wavert_state, 0, 0, 0, 1, "stale"));
    const auto stable_requested_db = wavert_state.requested_db_q16_16;
    const auto stable_effective_db = wavert_state.effective_db_q16_16;
    const auto stable_generation = wavert_state.generation;
    const auto stable_context = std::string(wavert_state.last_event_context_guid);
    std::array<char, HIBIKI_ENDPOINT_GUID_CAPACITY + 1U> invalid_context{};
    std::fill(invalid_context.begin(), invalid_context.end() - 1, 'x');
    CHECK(!hibiki_wavert_endpoint_state_apply_volume_v1(
              &wavert_state, 0, 0, 1, stable_generation + 1U, invalid_context.data()) &&
          wavert_state.requested_db_q16_16 == stable_requested_db &&
          wavert_state.effective_db_q16_16 == stable_effective_db &&
          wavert_state.generation == stable_generation &&
          std::string(wavert_state.last_event_context_guid) == stable_context);
    CHECK(hibiki_wavert_endpoint_state_init_v1(
        &wavert_state, "8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10", 2, 48000,
        HIBIKI_ACTUATOR_STRICT_DIRECT));
    CHECK(hibiki_wavert_endpoint_state_apply_volume_v1(
        &wavert_state, 0, 0, 0, 2, "direct"));
    CHECK(wavert_state.effective_db_q16_16 == 0);
    CHECK(!hibiki_wavert_endpoint_state_init_v1(
        &wavert_state, "bad", 4, 48000, HIBIKI_ACTUATOR_INTERNAL_DSP));
    std::array<std::uint8_t, 32> wavert_storage{};
    std::array<std::uint8_t, 24> wavert_input{};
    std::array<std::uint8_t, 32> wavert_output{};
    for (std::size_t index = 0U; index < wavert_input.size(); ++index) {
        wavert_input[index] = static_cast<std::uint8_t>(index + 1U);
    }
    hibiki_wavert_stream_v1 wavert_stream{};
    CHECK(hibiki_wavert_stream_init_v1(&wavert_stream, wavert_storage.data(),
                                       wavert_storage.size(), 2U, 48000U, 2U, 2U) ==
          HIBIKI_WAVERT_STREAM_OK_V1);
    CHECK(hibiki_wavert_stream_push_v1(&wavert_stream, wavert_input.data(), 3U) ==
          HIBIKI_WAVERT_STREAM_OK_V1);
    CHECK(hibiki_wavert_stream_push_v1(&wavert_stream, wavert_input.data(), 2U) ==
          HIBIKI_WAVERT_STREAM_REJECTED_V1 && wavert_stream.dropped_frames == 2U);
    CHECK(hibiki_wavert_stream_pop_v1(&wavert_stream, wavert_output.data(), 2U) ==
          HIBIKI_WAVERT_STREAM_OK_V1 && wavert_output[0] == wavert_input[0] &&
          wavert_output[15] == wavert_input[15]);
    CHECK(hibiki_wavert_stream_push_v1(&wavert_stream, wavert_input.data(), 2U) ==
          HIBIKI_WAVERT_STREAM_OK_V1);
    wavert_output.fill(0xA5U);
    CHECK(hibiki_wavert_stream_pop_or_silence_v1(&wavert_stream, wavert_output.data(), 4U) ==
          HIBIKI_WAVERT_STREAM_UNDERRUN_V1 && wavert_stream.underrun_frames == 1U);
    CHECK(wavert_output[31] == 0U);
    hibiki_wavert_stream_reset_v1(&wavert_stream);
    CHECK(wavert_stream.available_frames == 0U && wavert_stream.dropped_frames == 0U &&
          wavert_stream.underrun_frames == 0U);
    CHECK(hibiki_wavert_stream_init_v1(&wavert_stream, wavert_storage.data(), 8U, 2U, 48000U,
                                       2U, 2U) == HIBIKI_WAVERT_STREAM_REJECTED_V1);
    const float driver_samples[] = {0.25F, -0.25F, 0.5F, -0.5F};
    std::array<std::uint8_t, 128> driver_packet{};
    std::size_t driver_packet_bytes = 0U;
    CHECK(hibiki_driver_stream_packet_encode_v1(
              driver_packet.data(), driver_packet.size(), HIBIKI_DRIVER_STREAM_RENDER_V1,
              42U, "8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10", 2U, 48000U, 2U,
              HIBIKI_DRIVER_STREAM_FLAG_DISCONTINUITY_V1, 9U, driver_samples,
              &driver_packet_bytes) == 1 && driver_packet_bytes == 96U);
    CHECK(hibiki_driver_stream_packet_validate_v1(driver_packet.data(), driver_packet_bytes) == 1);
    std::array<std::uint8_t, 128> zero_freshness_packet{};
    zero_freshness_packet.fill(0xA5U);
    std::size_t zero_freshness_bytes = 123U;
    CHECK(hibiki_driver_stream_packet_encode_v1(
              zero_freshness_packet.data(), zero_freshness_packet.size(),
              HIBIKI_DRIVER_STREAM_RENDER_V1, 0U,
              "8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10", 2U, 48000U, 2U,
              HIBIKI_DRIVER_STREAM_FLAG_DISCONTINUITY_V1, 9U, driver_samples,
              &zero_freshness_bytes) == 0 &&
          zero_freshness_bytes == 0U);
    CHECK(std::all_of(zero_freshness_packet.begin(), zero_freshness_packet.end(),
                      [](const std::uint8_t value) { return value == 0xA5U; }));
    zero_freshness_packet.fill(0xA5U);
    zero_freshness_bytes = 123U;
    CHECK(hibiki_driver_stream_packet_encode_v1(
              zero_freshness_packet.data(), zero_freshness_packet.size(),
              HIBIKI_DRIVER_STREAM_RENDER_V1, 42U,
              "8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10", 2U, 48000U, 2U,
              HIBIKI_DRIVER_STREAM_FLAG_DISCONTINUITY_V1, 0U, driver_samples,
              &zero_freshness_bytes) == 0 &&
          zero_freshness_bytes == 0U);
    CHECK(std::all_of(zero_freshness_packet.begin(), zero_freshness_packet.end(),
                      [](const std::uint8_t value) { return value == 0xA5U; }));
    auto zero_sequence_header_packet = driver_packet;
    const std::uint64_t zero_freshness = 0U;
    std::memcpy(zero_sequence_header_packet.data() +
                    offsetof(hibiki_driver_stream_packet_header_v1, sequence),
                &zero_freshness, sizeof(zero_freshness));
    CHECK(hibiki_driver_stream_packet_validate_v1(zero_sequence_header_packet.data(),
                                                  driver_packet_bytes) == 0);
    auto zero_generation_header_packet = driver_packet;
    std::memcpy(zero_generation_header_packet.data() +
                    offsetof(hibiki_driver_stream_packet_header_v1, generation),
                &zero_freshness, sizeof(zero_freshness));
    CHECK(hibiki_driver_stream_packet_validate_v1(zero_generation_header_packet.data(),
                                                  driver_packet_bytes) == 0);
    std::array<std::uint8_t, 128> max_freshness_packet{};
    std::size_t max_freshness_bytes = 0U;
    const auto max_freshness = (std::numeric_limits<std::uint64_t>::max)();
    CHECK(hibiki_driver_stream_packet_encode_v1(
              max_freshness_packet.data(), max_freshness_packet.size(),
              HIBIKI_DRIVER_STREAM_RENDER_V1, max_freshness,
              "8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10", 2U, 48000U, 2U,
              HIBIKI_DRIVER_STREAM_FLAG_DISCONTINUITY_V1, max_freshness, driver_samples,
              &max_freshness_bytes) == 1 && max_freshness_bytes == driver_packet_bytes &&
          hibiki_driver_stream_packet_validate_v1(max_freshness_packet.data(),
                                                  max_freshness_bytes) == 1);
    std::array<float, 4> max_freshness_samples{};
    DriverStreamLaneBlockV1 max_freshness_block{};
    CHECK(decode_driver_stream_packet_v1(
              std::span<const std::uint8_t>(max_freshness_packet.data(), max_freshness_bytes),
              max_freshness_samples, max_freshness_block) &&
          max_freshness_block.sequence == max_freshness &&
          max_freshness_block.generation == max_freshness);
    std::array<float, 4> decoded_driver_samples{};
    DriverStreamLaneBlockV1 decoded_driver_block{};
    const auto driver_packet_view = std::span<const std::uint8_t>(driver_packet.data(),
                                                                   driver_packet_bytes);
    CHECK(decode_driver_stream_packet_v1(driver_packet_view, decoded_driver_samples,
                                         decoded_driver_block) &&
          decoded_driver_block.interleaved == decoded_driver_samples.data() &&
          decoded_driver_block.channels == 2U && decoded_driver_block.sample_rate == 48000U &&
          decoded_driver_block.frames == 2U &&
          decoded_driver_block.sequence == 42U && decoded_driver_block.generation == 9U &&
          decoded_driver_block.flags == HIBIKI_DRIVER_STREAM_FLAG_DISCONTINUITY_V1 &&
          decoded_driver_samples[3] == driver_samples[3]);
    const float driver_nan = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(driver_packet.data() + HIBIKI_DRIVER_STREAM_HEADER_BYTES_V1 + sizeof(float),
                &driver_nan, sizeof(driver_nan));
    decoded_driver_samples.fill(1.0F);
    CHECK(!decode_driver_stream_packet_v1(driver_packet_view, decoded_driver_samples,
                                          decoded_driver_block) &&
          decoded_driver_samples[0] == 0.0F && decoded_driver_block.interleaved == nullptr);

    PersistentLinearResampler persistent_src;
    CHECK(persistent_src.prepare(1, 1.0));
    const float first_block[]{0.0F, 1.0F, 2.0F, 3.0F};
    const float second_block[]{4.0F, 5.0F, 6.0F, 7.0F};
    float resampled[8]{};
    std::size_t output_frames = 0;
    CHECK(persistent_src.process(first_block, 4, resampled, 8, output_frames));
    CHECK(output_frames == 3 && resampled[0] == 0.0F && resampled[2] == 2.0F);
    CHECK(persistent_src.process(second_block, 4, resampled, 8, output_frames));
    CHECK(output_frames == 4 && resampled[0] == 3.0F && resampled[3] == 6.0F);
    OutputSinkModel sink_model;
    CHECK(sink_model.prepare(1, 1.0));
    sink_model.observe_clock(48000.0, 48012.0, 1.0);
    CHECK(sink_model.snapshot().ratio > 1.0 && sink_model.snapshot().source_step < 1.0);
    CHECK(sink_model.process(first_block, 4, resampled, 8, output_frames));
    CHECK(output_frames > 0);
    OutputSinkModel fast_sink;
    CHECK(fast_sink.prepare(1U, 4.0));
    CHECK(fast_sink.process(first_block, 4U, resampled, 8U, output_frames));
    CHECK(fast_sink.process(first_block, 4U, resampled, 8U, output_frames));

    VirtualMicRouteModel virtual_mic;
    CHECK(virtual_mic.prepare(VirtualMicConfigV1{1U, 48000U, true}));
    const float mic_input[2] = {0.25F, -0.5F};
    float mic_output[2]{};
    float mic_reference[2]{};
    CHECK(virtual_mic.process_capture(mic_input, mic_output, 2U));
    CHECK(mic_output[0] == 0.0F && mic_output[1] == 0.0F);
    virtual_mic.set_privacy_mute(false);
    CHECK(virtual_mic.process_capture(mic_input, mic_output, 2U));
    CHECK(mic_output[0] == mic_input[0] && mic_output[1] == mic_input[1]);
    CHECK(virtual_mic.process_echo_reference(mic_input, mic_reference, 2U));
    CHECK(mic_reference[1] == mic_input[1]);
    CHECK(!virtual_mic.prepare(VirtualMicConfigV1{3U, 48000U, true}));

    OutputCrossfade crossfade;
    CHECK(crossfade.begin(2, 48000, 30));
    std::vector<float> old_sink(48000U * 30U / 1000U * 2U, 1.0F);
    std::vector<float> new_sink(old_sink.size(), 0.0F);
    std::vector<float> mixed(old_sink.size(), 0.0F);
    CHECK(crossfade.process(old_sink.data(), new_sink.data(), mixed.data(), old_sink.size() / 2U));
    CHECK(crossfade.snapshot().processed_frames == crossfade.snapshot().total_frames);
    CHECK(!crossfade.snapshot().active);
    CHECK(mixed.front() > 0.0F && mixed.back() < 0.01F);

    OutputHandoffCoordinatorV1 handoff;
    CHECK(handoff.begin(DeviceTargetV1{"endpoint-handoff", 2U, 48000U, 128U}, 30U));
    CHECK(handoff.prepare() && handoff.state() == OutputHandoffStateV1::Fading);
    CHECK(!handoff.commit());
    CHECK(handoff.process(old_sink.data(), new_sink.data(), mixed.data(), old_sink.size() / 2U));
    CHECK(handoff.crossfade().active == false && handoff.commit() &&
          handoff.state() == OutputHandoffStateV1::Committed &&
          handoff.active_target().endpoint_id == "endpoint-handoff");
    CHECK(handoff.begin(DeviceTargetV1{"endpoint-rollback", 2U, 48000U, 128U}));
    CHECK(handoff.prepare());
    handoff.rollback();
    CHECK(handoff.state() == OutputHandoffStateV1::RolledBack &&
          handoff.active_target().endpoint_id == "endpoint-handoff");

    const std::array<OutputFanoutSinkConfigV1, 3> fanout_configs{{
        {"headphones", 2U, true}, {"speakers", 2U, true}, {"preview", 2U, false}}};
    OutputFanoutPlanV1 fanout_plan{};
    CHECK(prepare_output_fanout_plan_v1(fanout_configs, 2U, 7U, fanout_plan) &&
          validate_output_fanout_plan_v1(fanout_plan));
    const float fanout_input[] = {0.25F, -0.25F, 0.5F, -0.5F};
    std::array<float, 4> fanout_a{};
    std::array<float, 4> fanout_b{};
    std::array<float, 4> fanout_disabled{};
    std::array<float*, 3> fanout_outputs{{fanout_a.data(), fanout_b.data(),
                                          fanout_disabled.data()}};
    const std::array<std::size_t, 3> fanout_capacities{{2U, 2U, 2U}};
    const std::array<float, 4> expected_fanout{{0.25F, -0.25F, 0.5F, -0.5F}};
    const std::array<float, 4> expected_silence{};
    CHECK(fanout_interleaved_v1(fanout_plan, fanout_input, 2U, fanout_outputs,
                                fanout_capacities) &&
          fanout_a == expected_fanout && fanout_b == fanout_a &&
          fanout_disabled == expected_silence);
    const std::array<std::size_t, 3> short_capacities{{1U, 2U, 2U}};
    CHECK(!fanout_interleaved_v1(fanout_plan, fanout_input, 2U, fanout_outputs,
                                 short_capacities));
    const std::array<OutputFanoutSinkConfigV1, 2> duplicate_sinks{{
        {"same", 2U, true}, {"same", 2U, true}}};
    CHECK(!prepare_output_fanout_plan_v1(duplicate_sinks, 2U, 8U, fanout_plan));
    const float fanout_nan_input[] = {0.25F, std::numeric_limits<float>::quiet_NaN(),
                                      0.5F, -0.5F};
    CHECK(!fanout_interleaved_v1(fanout_plan, fanout_nan_input, 2U, fanout_outputs,
                                 fanout_capacities));
    const std::array<OutputFanoutSinkConfigV1, 1> disabled_sinks{{
        {"disabled", 2U, false}}};
    CHECK(!prepare_output_fanout_plan_v1(disabled_sinks, 2U, 9U, fanout_plan));
    OutputFanoutRuntimeV1 fanout_runtime;
    CHECK(fanout_runtime.prepare(fanout_plan, 1.0));
    std::array<float, 16> runtime_a{};
    std::array<float, 16> runtime_b{};
    std::array<float, 16> runtime_disabled{};
    std::array<float*, 3> runtime_outputs{{runtime_a.data(), runtime_b.data(),
                                            runtime_disabled.data()}};
    const std::array<std::size_t, 3> runtime_capacities{{16U, 16U, 16U}};
    std::array<std::size_t, 3> runtime_frames{};
    CHECK(fanout_runtime.process(fanout_input, 2U, runtime_outputs, runtime_capacities,
                                 runtime_frames) &&
          runtime_frames[0] == 1U && runtime_frames[1] == 1U && runtime_frames[2] == 0U &&
          runtime_a[0] == fanout_input[0] && runtime_b[1] == fanout_input[1]);
    CHECK(fanout_runtime.observe_clock(0U, 48000.0, 48012.0, 1.0));
    CHECK(fanout_runtime.snapshot().sinks[0].drift_ppm > 0.0);
    const std::array<std::size_t, 3> runtime_short_capacities{{16U, 0U, 16U}};
    CHECK(!fanout_runtime.process(fanout_input, 2U, runtime_outputs,
                                  runtime_short_capacities, runtime_frames));

    DeviceRecoveryCoordinator recovery;
    CHECK(recovery.observe(DeviceRecoveryEventV1{
        1, DeviceRecoveryEventKind::EndpointInvalidated, true}));
    CHECK(recovery.state() == DeviceRecoveryState::RebindPending);
    CHECK(!recovery.observe(DeviceRecoveryEventV1{
        1, DeviceRecoveryEventKind::EndpointAdded, false}));
    CHECK(recovery.begin_rebind(DeviceTargetV1{"hibiki-main", 2, 48000, 128}));
    CHECK(recovery.prepare());
    CHECK(recovery.commit());
    CHECK(recovery.state() == DeviceRecoveryState::Stable);
    OutputGroupVolumeStateV1 restart_volume;
    restart_volume.requested_db = 0.0;
    restart_volume.safety_ceiling_db = 0.0;
    const auto safe_restart = recovery.safe_restart_state(restart_volume, -48.0);
    CHECK(safe_restart.mute);
    CHECK(std::abs(safe_restart.requested_db + 48.0) < 1e-12);
    CHECK(safe_restart.effective_db <= -48.0);

    GraphConfigV1 graph;
    graph.output_channels = 8;
    graph.lanes.push_back(LaneConfigV1{"game", "main", 8, 0.0, true});
    CHECK(validate_graph(graph));
    graph.strict_direct = true;
    graph.lanes[0].makeup_gain_db = 1.0;
    CHECK(!validate_graph(graph));

    const auto game_scene = make_easy_scene(EasySceneKind::Game, "main");
    CHECK(validate_scene(game_scene.scene));
    CHECK(validate_graph(game_scene.graph));
    CHECK(game_scene.scene.latency_mode == LatencyMode::Game);
    CHECK(game_scene.scene.ir_phase.mode == IrPhaseMode::MinimumPhase &&
          resolve_ir_phase_policy(game_scene.scene.ir_phase).added_delay_ms == 0.0);
    const auto studio_scene = make_easy_scene(EasySceneKind::Studio, "main");
    CHECK(validate_scene(studio_scene.scene));
    CHECK(studio_scene.graph.strict_direct);
    CHECK(studio_scene.scene.ir_phase.mode == IrPhaseMode::Bypass);
    CHECK(validate_graph(studio_scene.graph));
    CHECK(studio_scene.loudness.max_boost_db == 0.0);

    OutputGroupVolumeStateV1 safety_volume{};
    safety_volume.requested_db = -12.0;
    safety_volume.safety_ceiling_db = -3.0;
    SceneSafetyController safety_controller;
    CHECK(safety_controller.begin(make_easy_scene(EasySceneKind::Movie, "movie").scene,
                                   safety_volume));
    CHECK(safety_controller.active() && safety_controller.baseline_db() == -12.0);
    const auto no_action = safety_controller.observe_peak(-1.6, 50, safety_volume);
    CHECK(no_action.kind == SceneSafetyActionKind::None);
    const auto attenuation = safety_controller.observe_peak(-0.2, 100, safety_volume);
    CHECK(attenuation.kind == SceneSafetyActionKind::Attenuate);
    CHECK(std::abs(attenuation.requested_db + 12.8) < 1e-12);
    safety_volume.requested_db = attenuation.requested_db;
    const auto restore = safety_controller.end(safety_volume);
    CHECK(restore.kind == SceneSafetyActionKind::Restore && restore.requested_db == -12.0);

    CHECK(safety_controller.begin(make_easy_scene(EasySceneKind::Movie, "movie").scene,
                                  safety_volume));
    safety_volume.requested_db = -6.0;
    const auto manual_end = safety_controller.end(safety_volume);
    CHECK(manual_end.kind == SceneSafetyActionKind::None &&
          safety_controller.user_override_detected());

    AcousticAnchorV1 anchor;
    anchor.test_signal_dbfs = -20.0;
    anchor.measured_1k_spl_db = 65.0;
    anchor.uncertainty_db = 1.5;
    CHECK(validate_acoustic_anchor(anchor));
    anchor.measured_1k_spl_db = -0.0001;
   CHECK(!validate_acoustic_anchor(anchor));
    anchor.measured_1k_spl_db = 0.0;
   CHECK(validate_acoustic_anchor(anchor));
    anchor.measured_1k_spl_db = 140.0001;
   CHECK(!validate_acoustic_anchor(anchor));
   anchor.measured_1k_spl_db = 65.0;
   anchor.endpoint_gain_db = 12.5;
   CHECK(!validate_acoustic_anchor(anchor));
    anchor.endpoint_gain_db = 12.0;
    CHECK(validate_acoustic_anchor(anchor));
    anchor.endpoint_gain_db = -144.5;
    CHECK(!validate_acoustic_anchor(anchor));
    anchor.endpoint_gain_db = -144.0;
    CHECK(validate_acoustic_anchor(anchor));
    anchor.endpoint_gain_db = 0.0;
    anchor.measured_f3_hz = -0.0001;
    CHECK(!validate_acoustic_anchor(anchor));
    anchor.measured_f3_hz = 0.0;
    CHECK(validate_acoustic_anchor(anchor));
    anchor.measured_f3_hz = 20000.0;
    CHECK(validate_acoustic_anchor(anchor));
    anchor.measured_f3_hz = 20000.1;
    CHECK(!validate_acoustic_anchor(anchor));
    anchor.measured_f3_hz = 0.0;
    anchor.uncertainty_db = 36.0;
    CHECK(validate_acoustic_anchor(anchor));
    anchor.uncertainty_db = 36.0001;
    CHECK(!validate_acoustic_anchor(anchor));
    anchor.uncertainty_db = 1.5;
    const auto phon = estimate_phon(anchor, -14.0, -3.0);
    CHECK(phon.calibrated && std::abs(phon.phon - 68.0) < 1e-12);
    anchor.device_class = AcousticDeviceClass::HeadphoneEstimated;
    CHECK(!estimate_phon(anchor, -20.0, 0.0).calibrated);

    GraphConfigV1 render_graph;
    render_graph.output_channels = 8;
    render_graph.lanes.push_back(LaneConfigV1{"stereo", "main", 2, 6.0205999, true});
    render_graph.lanes[0].channel_map = {2, 3, -1, -1, -1, -1, -1, -1};
    RtGraphSnapshotV1 snapshot;
    CHECK(compile_rt_snapshot(render_graph, 7, snapshot));
    CHECK(snapshot.revision == 7 && snapshot.output_channels == 8);
    const float lane_audio[] = {0.5F, -0.25F, 1.0F, 0.25F};
    const RtLaneInputV1 lane_input{lane_audio, 2};
    float rendered[16]{};
    CHECK(process_graph(snapshot, std::span<const RtLaneInputV1>(&lane_input, 1), rendered, 2));
    CHECK(std::abs(rendered[2] - 1.0F) < 1e-5F);
    CHECK(std::abs(rendered[3] + 0.5F) < 1e-5F);
    CHECK(std::abs(rendered[10] - 2.0F) < 1e-5F);
    CHECK(std::abs(rendered[11] - 0.5F) < 1e-5F);

    GraphConfigV1 matrix_graph;
    matrix_graph.output_channels = 2U;
    matrix_graph.lanes.push_back(LaneConfigV1{"matrix", "main", 2U, 0.0, true});
    matrix_graph.lanes[0].matrix_enabled = true;
    matrix_graph.lanes[0].channel_matrix[0] = {0.5F, 0.25F, 0.0F, 0.0F,
                                               0.0F, 0.0F, 0.0F, 0.0F};
    matrix_graph.lanes[0].channel_matrix[1] = {0.25F, 0.5F, 0.0F, 0.0F,
                                               0.0F, 0.0F, 0.0F, 0.0F};
    CHECK(validate_graph(matrix_graph));
    RtGraphSnapshotV1 matrix_snapshot;
    CHECK(compile_rt_snapshot(matrix_graph, 9U, matrix_snapshot));
    const float matrix_input[] = {1.0F, -1.0F};
    const RtLaneInputV1 matrix_view{matrix_input, 2U};
    float matrix_output[2]{};
    CHECK(process_graph(matrix_snapshot, std::span<const RtLaneInputV1>(&matrix_view, 1),
                        matrix_output, 1U));
    CHECK(std::abs(matrix_output[0] - 0.25F) < 1e-6F &&
          std::abs(matrix_output[1] + 0.25F) < 1e-6F);
    matrix_graph.strict_direct = true;
    CHECK(!validate_graph(matrix_graph));

    GraphConfigV1 multi_group_graph;
    multi_group_graph.output_channels = 2;
    for (std::uint32_t lane_index = 0U; lane_index < 4U; ++lane_index) {
        LaneConfigV1 lane{"lane" + std::to_string(lane_index),
                          "group" + std::to_string(lane_index), 2, 0.0, true};
        multi_group_graph.lanes.push_back(std::move(lane));
    }
    RtGraphSnapshotV1 multi_group_snapshot;
    CHECK(compile_rt_snapshot(multi_group_graph, 8U, multi_group_snapshot));
    const float group_inputs[] = {1.0F, -1.0F, 2.0F, -2.0F, 3.0F, -3.0F, 4.0F, -4.0F};
    std::array<RtLaneInputV1, 4> group_views{};
    for (std::size_t lane_index = 0U; lane_index < group_views.size(); ++lane_index) {
        group_views[lane_index] = RtLaneInputV1{group_inputs + lane_index * 2U, 2U};
    }
    float group_rendered[2]{};
    CHECK(process_graph_for_output_group(multi_group_snapshot, "group2", group_views,
                                         group_rendered, 1U));
    CHECK(group_rendered[0] == 3.0F && group_rendered[1] == -3.0F);
    CHECK(process_graph_for_output_group(multi_group_snapshot, "group0", group_views,
                                         group_rendered, 1U));
    CHECK(group_rendered[0] == 1.0F && group_rendered[1] == -1.0F);
    CHECK(!process_graph_for_output_group(multi_group_snapshot, "missing", group_views,
                                          group_rendered, 1U));
    multi_group_graph.lanes[0].output_group.assign(kMaxOutputGroupBytes + 1U, 'x');
    CHECK(!validate_graph(multi_group_graph));

    DeviceSwitchTransaction transaction;
    CHECK(transaction.begin(DeviceTargetV1{"endpoint-a", 2, 48000, 128}));
    CHECK(transaction.prepare_complete());
    CHECK(transaction.commit());
    CHECK(transaction.state() == DeviceSwitchState::Synced);
    CHECK(transaction.begin(DeviceTargetV1{"endpoint-b", 6, 48000, 128}));
    CHECK(transaction.prepare_complete());
    transaction.rollback();
    CHECK(transaction.state() == DeviceSwitchState::Synced);
    CHECK(transaction.active_target().endpoint_id == "endpoint-a");

    PhysicalDeviceCatalogV1 devices;
    PhysicalDeviceDescriptorV1 speakers;
    speakers.endpoint_id = "endpoint-a";
    speakers.display_name = "Living room speakers";
    speakers.flow = PhysicalDeviceFlowV1::Render;
    speakers.availability = PhysicalDeviceAvailabilityV1::Active;
    speakers.channels = 8U;
    speakers.sample_rate = 48000U;
    speakers.buffer_frames = 128U;
    speakers.is_default = true;
    speakers.last_sequence = 10U;
    CHECK(devices.upsert(speakers) == PhysicalDeviceCatalogResultV1::Accepted &&
          devices.size() == 1U && devices.default_device(PhysicalDeviceFlowV1::Render) != nullptr &&
          devices.selectable("endpoint-a", PhysicalDeviceFlowV1::Render));
    auto headphones = speakers;
    headphones.endpoint_id = "endpoint-b";
    headphones.display_name = "Headphones";
    headphones.channels = 2U;
    headphones.is_default = true;
    headphones.last_sequence = 11U;
    CHECK(devices.upsert(headphones) == PhysicalDeviceCatalogResultV1::Accepted &&
          devices.default_device(PhysicalDeviceFlowV1::Render) != nullptr &&
          devices.default_device(PhysicalDeviceFlowV1::Render)->endpoint_id == "endpoint-b" &&
          !devices.find("endpoint-a")->is_default);
    CHECK(devices.set_availability("endpoint-b", PhysicalDeviceAvailabilityV1::Unplugged, 12U) ==
              PhysicalDeviceCatalogResultV1::Accepted &&
          !devices.selectable("endpoint-b", PhysicalDeviceFlowV1::Render) &&
          devices.default_device(PhysicalDeviceFlowV1::Render) == nullptr);
    CHECK(devices.set_availability("endpoint-b", PhysicalDeviceAvailabilityV1::Active, 9U) ==
              PhysicalDeviceCatalogResultV1::InvalidState &&
          devices.find("endpoint-b")->availability == PhysicalDeviceAvailabilityV1::Unplugged &&
          devices.find("endpoint-b")->last_sequence == 12U);
    CHECK(devices.set_availability("endpoint-b", PhysicalDeviceAvailabilityV1::Active, 13U) ==
              PhysicalDeviceCatalogResultV1::Accepted);
    CHECK(devices.mark_default("endpoint-b", PhysicalDeviceFlowV1::Render, 13U) ==
              PhysicalDeviceCatalogResultV1::Accepted);
    auto invalid_device = speakers;
    invalid_device.endpoint_id.clear();
    CHECK(devices.upsert(invalid_device) == PhysicalDeviceCatalogResultV1::InvalidDescriptor &&
          devices.size() == 2U);
    auto unavailable_default = speakers;
    unavailable_default.endpoint_id = "endpoint-c";
    unavailable_default.availability = PhysicalDeviceAvailabilityV1::Unplugged;
    CHECK(devices.upsert(unavailable_default) == PhysicalDeviceCatalogResultV1::InvalidDescriptor &&
          devices.size() == 2U);
    CHECK(devices.remove("endpoint-a") == PhysicalDeviceCatalogResultV1::Accepted &&
          devices.remove("missing") == PhysicalDeviceCatalogResultV1::NotFound &&
          devices.size() == 1U);
    PhysicalDeviceCatalogV1 full_devices;
    for (std::uint32_t index = 0U;
         index < static_cast<std::uint32_t>(kPhysicalDeviceCatalogCapacityV1); ++index) {
        PhysicalDeviceDescriptorV1 entry;
        entry.endpoint_id = "full-" + std::to_string(index);
        entry.display_name = "Fixture " + std::to_string(index);
        entry.flow = index % 2U == 0U ? PhysicalDeviceFlowV1::Render
                                      : PhysicalDeviceFlowV1::Capture;
        entry.availability = PhysicalDeviceAvailabilityV1::Active;
        entry.channels = entry.flow == PhysicalDeviceFlowV1::Render ? 2U : 1U;
        entry.last_sequence = index + 1U;
        CHECK(full_devices.upsert(entry) == PhysicalDeviceCatalogResultV1::Accepted);
    }
    PhysicalDeviceDescriptorV1 overflow = speakers;
    overflow.endpoint_id = "overflow";
    overflow.is_default = false;
    CHECK(full_devices.size() == kPhysicalDeviceCatalogCapacityV1 &&
          full_devices.upsert(overflow) == PhysicalDeviceCatalogResultV1::CapacityExceeded &&
          full_devices.size() == kPhysicalDeviceCatalogCapacityV1);

    DeviceRecoveryCoordinator catalog_recovery;
    PhysicalDeviceCatalogV1 recovery_devices;
    auto recovery_target = speakers;
    recovery_target.endpoint_id = "recovery-render";
    recovery_target.display_name = "Recovery render";
    recovery_target.is_default = false;
    recovery_target.last_sequence = 20U;
    CHECK(recovery_devices.upsert(recovery_target) == PhysicalDeviceCatalogResultV1::Accepted);
    CHECK(catalog_recovery.observe(DeviceRecoveryEventV1{
              1U, DeviceRecoveryEventKind::EndpointInvalidated, true}) &&
          catalog_recovery.begin_rebind(recovery_devices, "recovery-render") &&
          catalog_recovery.prepare() && catalog_recovery.commit() &&
          catalog_recovery.transaction().active_target().endpoint_id == "recovery-render");
    CHECK(recovery_devices.set_availability("recovery-render",
                                            PhysicalDeviceAvailabilityV1::Unplugged, 21U) ==
              PhysicalDeviceCatalogResultV1::Accepted &&
          !catalog_recovery.begin_rebind(recovery_devices, "recovery-render") &&
          catalog_recovery.transaction().active_target().endpoint_id == "recovery-render");

    IpcFrameV1 frame;
    frame.header.type = IpcMessageType::VolumeNotification;
    frame.header.request_id = 42;
    frame.payload = {0x01U, 0x02U, 0x03U};
    const auto encoded = encode_ipc_frame(frame);
    CHECK(encoded.size() == 23);
    IpcDecodeError decode_error = IpcDecodeError::None;
    const auto decoded = decode_ipc_frame(encoded, decode_error);
    CHECK(decoded.has_value() && decode_error == IpcDecodeError::None);
    CHECK(decoded->header.request_id == 42 && decoded->payload == frame.payload);
    const VolumeNotificationV1 payload_volume{-6.0205999, true, 7U};
    const auto volume_payload = encode_volume_notification_payload_v1(payload_volume);
    VolumeNotificationV1 decoded_volume{};
    CHECK(decode_volume_notification_payload_v1(volume_payload, decoded_volume));
    CHECK(std::abs(decoded_volume.requested_db + 6.020599365234375) < 1e-5 &&
          decoded_volume.mute && decoded_volume.generation == 7U);
    auto invalid_volume_payload = volume_payload;
    invalid_volume_payload[4] = 2U;
    CHECK(!decode_volume_notification_payload_v1(invalid_volume_payload, decoded_volume));
    const auto grouped_volume_payload = encode_grouped_volume_notification_payload_v1(
        "movie", payload_volume);
    GroupedVolumeNotificationPayloadV1 decoded_volume_target{};
    CHECK(decode_grouped_volume_notification_payload_v1(
              grouped_volume_payload, decoded_volume, decoded_volume_target) &&
          decoded_volume_target.output_group_bytes == 5U &&
          std::string_view(decoded_volume_target.output_group.data(), 5U) == "movie");
    ControlCommandV1 decoded_command{};
    IpcFrameV1 volume_command_frame;
    volume_command_frame.header.type = IpcMessageType::VolumeNotification;
    volume_command_frame.header.request_id = 99U;
    volume_command_frame.payload.assign(volume_payload.begin(), volume_payload.end());
    CHECK(decode_control_command_v1(volume_command_frame, decoded_command) &&
          decoded_command.type == IpcMessageType::VolumeNotification &&
          decoded_command.request_id == 99U && decoded_command.volume.mute);
    volume_command_frame.header.request_id = 100U;
    volume_command_frame.payload.assign(grouped_volume_payload.begin(), grouped_volume_payload.end());
    CHECK(decode_control_command_v1(volume_command_frame, decoded_command) &&
          decoded_command.has_volume_target &&
          std::string_view(decoded_command.volume_target.output_group.data(),
                           decoded_command.volume_target.output_group_bytes) == "movie");
    CHECK(make_ack_frame_v1(volume_command_frame).header.request_id == 100U &&
          make_error_frame_v1(volume_command_frame).header.type == IpcMessageType::Error);
    std::array<std::uint8_t, kSceneApplyPayloadBytesV1> scene_payload{};
    CHECK(encode_scene_apply_payload_v1("game", "main", scene_payload));
    CHECK(!encode_scene_apply_payload_v1(std::string_view("\xFF", 1), "main", scene_payload));
    CHECK(encode_scene_apply_payload_v1("game", "main", scene_payload));
    SceneApplyPayloadV1 decoded_scene{};
    CHECK(decode_scene_apply_payload_v1(scene_payload, decoded_scene) &&
          decoded_scene.scene_id_bytes == 4U && decoded_scene.output_group_bytes == 4U &&
          decoded_scene.scene_id[0] == 'g' && decoded_scene.output_group[0] == 'm');
    IpcFrameV1 scene_frame;
    scene_frame.header.type = IpcMessageType::SceneApply;
    scene_frame.payload.assign(scene_payload.begin(), scene_payload.end());
    CHECK(decode_control_command_v1(scene_frame, decoded_command) &&
          decoded_command.type == IpcMessageType::SceneApply);
    const auto device_payload = encode_device_switch_payload_v1(
        "recovery-render", 2U, 48000U, 128U, 21U);
    DeviceSwitchPayloadV1 decoded_device{};
    CHECK(decode_device_switch_payload_v1(device_payload, decoded_device) &&
          decoded_device.endpoint_id_bytes == 15U && decoded_device.channels == 2U &&
          decoded_device.sample_rate == 48000U && decoded_device.buffer_frames == 128U &&
          decoded_device.catalog_sequence == 21U);
    IpcFrameV1 device_frame;
    device_frame.header.type = IpcMessageType::DeviceSwitch;
    device_frame.payload.assign(device_payload.begin(), device_payload.end());
    CHECK(decode_control_command_v1(device_frame, decoded_command) &&
          decoded_command.type == IpcMessageType::DeviceSwitch &&
          decoded_command.device_switch.endpoint_id_bytes == 15U);
    auto malformed_device_payload = device_payload;
    malformed_device_payload[262U] = 1U;
    CHECK(!decode_device_switch_payload_v1(malformed_device_payload, decoded_device));
    DeviceCatalogSnapshotEntryV1 snapshot_entry{};
    const std::string_view snapshot_endpoint = "recovery-render";
    const std::string_view snapshot_name = "Recovery render";
    snapshot_entry.endpoint_id_bytes = static_cast<std::uint16_t>(snapshot_endpoint.size());
    snapshot_entry.display_name_bytes = static_cast<std::uint16_t>(snapshot_name.size());
    std::copy(snapshot_endpoint.begin(), snapshot_endpoint.end(), snapshot_entry.endpoint_id.begin());
    std::copy(snapshot_name.begin(), snapshot_name.end(), snapshot_entry.display_name.begin());
    snapshot_entry.flow = 0U;
    snapshot_entry.availability = 0U;
    snapshot_entry.flags = 1U;
    snapshot_entry.channels = 2U;
    snapshot_entry.sample_rate = 48000U;
    snapshot_entry.buffer_frames = 128U;
    snapshot_entry.last_sequence = 21U;
    std::array<std::uint8_t, kDeviceCatalogSnapshotPayloadBytesV1> snapshot_payload{};
    std::size_t snapshot_bytes = 0U;
    CHECK(is_valid_message_type(IpcMessageType::DeviceCatalogSnapshot) &&
          encode_device_catalog_snapshot_v1(std::span<const DeviceCatalogSnapshotEntryV1>(
                                                 &snapshot_entry, 1U),
                                             21U, snapshot_payload, snapshot_bytes) &&
          snapshot_bytes == kDeviceCatalogSnapshotHeaderBytesV1 +
                                kDeviceCatalogSnapshotEntryBytesV1);
    DeviceCatalogSnapshotV1 decoded_snapshot{};
    CHECK(decode_device_catalog_snapshot_v1(
              std::span<const std::uint8_t>(snapshot_payload.data(), snapshot_bytes),
              decoded_snapshot) &&
          decoded_snapshot.entry_count == 1U && decoded_snapshot.catalog_sequence == 21U &&
          decoded_snapshot.entries[0].endpoint_id_bytes == snapshot_endpoint.size() &&
          decoded_snapshot.entries[0].flags == 1U);
    auto malformed_snapshot = snapshot_payload;
    malformed_snapshot[2U] = 1U;
    CHECK(!decode_device_catalog_snapshot_v1(
        std::span<const std::uint8_t>(malformed_snapshot.data(), snapshot_bytes),
        decoded_snapshot));
    IpcFrameV1 snapshot_frame;
    snapshot_frame.header.type = IpcMessageType::DeviceCatalogSnapshot;
    snapshot_frame.payload.assign(snapshot_payload.begin(), snapshot_payload.begin() +
                                                        static_cast<std::ptrdiff_t>(snapshot_bytes));
    CHECK(!decode_control_command_v1(snapshot_frame, decoded_command));
    DeviceCatalogSnapshotPublisherV1 snapshot_publisher;
    std::array<std::uint8_t, kDeviceCatalogSnapshotPayloadBytesV1> published_payload{};
    std::size_t published_bytes = 0U;
    CHECK(snapshot_publisher.publish(full_devices, 32U, published_payload, published_bytes) &&
          published_bytes == kDeviceCatalogSnapshotPayloadBytesV1);
    CHECK(decode_device_catalog_snapshot_v1(
              std::span<const std::uint8_t>(published_payload.data(), published_bytes),
              decoded_snapshot) &&
          decoded_snapshot.entry_count == kPhysicalDeviceCatalogCapacityV1 &&
          decoded_snapshot.catalog_sequence == 32U &&
          decoded_snapshot.entries[0].endpoint_id_bytes == 6U);
    DeviceCatalogSnapshotStoreV1 snapshot_store;
    IpcFrameV1 stored_snapshot_response;
    CHECK(!snapshot_store.has_snapshot() && snapshot_store.sequence() == 0U &&
          !snapshot_store.reply(stored_snapshot_response));
    CHECK(snapshot_store.publish(
              std::span<const std::uint8_t>(published_payload.data(), published_bytes), 32U) &&
          snapshot_store.has_snapshot() && snapshot_store.sequence() == 32U &&
          snapshot_store.reply(stored_snapshot_response) &&
          stored_snapshot_response.header.type == IpcMessageType::DeviceCatalogSnapshot &&
          stored_snapshot_response.header.payload_bytes == published_bytes &&
          stored_snapshot_response.payload.size() == published_bytes);
    IpcFrameV1 callback_snapshot_response;
    CHECK(device_catalog_snapshot_reply_v1(callback_snapshot_response, &snapshot_store) &&
          callback_snapshot_response.payload == stored_snapshot_response.payload);
    auto invalid_store_payload = published_payload;
    invalid_store_payload[0U] ^= 0xFFU;
    CHECK(!snapshot_store.publish(
        std::span<const std::uint8_t>(invalid_store_payload.data(), published_bytes), 33U) &&
          snapshot_store.sequence() == 32U);
    CHECK(!snapshot_store.publish(
              std::span<const std::uint8_t>(published_payload.data(), published_bytes), 32U) &&
          snapshot_store.sequence() == 32U);
    ControlStatusSnapshotV1 status_snapshot{};
    status_snapshot.sequence = 3U;
    status_snapshot.volume.requested_db = -6.0;
    status_snapshot.volume.safety_ceiling_db = -12.0;
    status_snapshot.volume.effective_db = -12.0;
    status_snapshot.volume.generation = 4U;
    status_snapshot.volume.origin = VolumeOrigin::Safety;
    status_snapshot.volume.actuator = ActuatorMode::InternalDsp;
    status_snapshot.route_count = 2U;
    const auto fill_status_route = [](ControlRouteHealthEntryV1& route,
                                      const std::string_view id,
                                      const std::string_view name,
                                      const std::string_view detail,
                                      const ControlRouteHealthStateV1 state,
                                      const bool requires_action) {
        route.id_bytes = static_cast<std::uint8_t>(id.size());
        route.name_bytes = static_cast<std::uint16_t>(name.size());
        route.detail_bytes = static_cast<std::uint16_t>(detail.size());
        route.state = state;
        route.flags = requires_action ? 1U : 0U;
        std::copy(id.begin(), id.end(), route.id.begin());
        std::copy(name.begin(), name.end(), route.name.begin());
        std::copy(detail.begin(), detail.end(), route.detail.begin());
    };
    fill_status_route(status_snapshot.routes[0], "windows-session", "Windows Session",
                      "等待 active session", ControlRouteHealthStateV1::Pending, false);
    fill_status_route(status_snapshot.routes[1], "browser-tab", "Chrome tab",
                      "需要使用者點擊擴充功能", ControlRouteHealthStateV1::Pending, true);
    std::array<std::uint8_t, kControlStatusSnapshotPayloadBytesV1> status_payload{};
    std::size_t status_bytes = 0U;
    CHECK(is_valid_message_type(IpcMessageType::ControlStatusRequest) &&
          is_valid_message_type(IpcMessageType::ControlStatusSnapshot) &&
          encode_control_status_snapshot_v1(status_snapshot, status_payload, status_bytes) &&
          status_bytes == kControlStatusSnapshotHeaderBytesV1 +
                              (2U * kControlStatusSnapshotEntryBytesV1));
    ControlStatusSnapshotV1 decoded_status{};
    CHECK(decode_control_status_snapshot_v1(
              std::span<const std::uint8_t>(status_payload.data(), status_bytes), decoded_status) &&
          decoded_status.sequence == 3U && decoded_status.route_count == 2U &&
          decoded_status.volume.effective_db < decoded_status.volume.requested_db &&
          decoded_status.routes[1].flags == 1U);
    auto malformed_status = status_payload;
    malformed_status[2U] = 1U;
    CHECK(!decode_control_status_snapshot_v1(
        std::span<const std::uint8_t>(malformed_status.data(), status_bytes), decoded_status));
    ControlStatusSnapshotStoreV1 status_store;
    IpcFrameV1 status_response;
    CHECK(!status_store.has_snapshot() && !status_store.reply(status_response) &&
          status_store.publish(status_snapshot) && status_store.sequence() == 3U &&
          status_store.reply(status_response) &&
          status_response.header.type == IpcMessageType::ControlStatusSnapshot &&
          status_response.payload.size() == status_bytes);
    CHECK(!status_store.publish(status_snapshot) && status_store.sequence() == 3U &&
          control_status_snapshot_reply_v1(status_response, &status_store));
    // Keep the 8 KiB snapshot off the test harness stack; this main function
    // intentionally retains many fixed wire fixtures until its final checks.
    auto session_catalog_snapshot_storage = std::unique_ptr<SessionCatalogSnapshotV1>(
        new (std::nothrow) SessionCatalogSnapshotV1());
    auto& session_catalog_snapshot = *session_catalog_snapshot_storage;
    session_catalog_snapshot.sequence = 12U;
    session_catalog_snapshot.generation = 2U;
    session_catalog_snapshot.entry_count = 2U;
    session_catalog_snapshot.entries[0].handle = (2ULL << 32U) | 1ULL;
    session_catalog_snapshot.entries[0].active = 1U;
    session_catalog_snapshot.entries[0].route_state = SessionCatalogRouteStateV1::Ready;
    session_catalog_snapshot.entries[0].flags = 1U;
    session_catalog_snapshot.entries[0].requested_db_q16_16 = -622592; // -9.5 dB
    session_catalog_snapshot.entries[0].name_bytes = 5U;
    session_catalog_snapshot.entries[0].app_bytes = 8U;
    session_catalog_snapshot.entries[0].lane_bytes = 4U;
    session_catalog_snapshot.entries[0].output_bytes = 4U;
    std::memcpy(session_catalog_snapshot.entries[0].name.data(), "DJMAX", 5U);
    std::memcpy(session_catalog_snapshot.entries[0].app.data(), "game.exe", 8U);
    std::memcpy(session_catalog_snapshot.entries[0].lane.data(), "game", 4U);
    std::memcpy(session_catalog_snapshot.entries[0].output.data(), "main", 4U);
    session_catalog_snapshot.entries[1].handle = (2ULL << 32U) | 2ULL;
    session_catalog_snapshot.entries[1].route_state = SessionCatalogRouteStateV1::Unavailable;
    session_catalog_snapshot.entries[1].mute = 1U;
    session_catalog_snapshot.entries[1].name_bytes = 10U;
    session_catalog_snapshot.entries[1].app_bytes = 10U;
    session_catalog_snapshot.entries[1].lane_bytes = 11U;
    session_catalog_snapshot.entries[1].output_bytes = 4U;
    std::memcpy(session_catalog_snapshot.entries[1].name.data(), "Chrome tab", 10U);
    std::memcpy(session_catalog_snapshot.entries[1].app.data(), "chrome.exe", 10U);
    std::memcpy(session_catalog_snapshot.entries[1].lane.data(), "browser-tab", 11U);
    std::memcpy(session_catalog_snapshot.entries[1].output.data(), "main", 4U);
    std::array<std::uint8_t, kSessionCatalogSnapshotPayloadBytesV1> session_payload{};
    std::size_t session_bytes = 0U;
    CHECK(is_valid_message_type(IpcMessageType::SessionCatalogRequest) &&
          is_valid_message_type(IpcMessageType::SessionCatalogSnapshot) &&
          encode_session_catalog_snapshot_v1(session_catalog_snapshot, session_payload, session_bytes) &&
          session_bytes == kSessionCatalogSnapshotHeaderBytesV1 +
                               (2U * kSessionCatalogSnapshotEntryBytesV1));
    SessionCatalogSnapshotV1 decoded_session{};
    CHECK(decode_session_catalog_snapshot_v1(
              std::span<const std::uint8_t>(session_payload.data(), session_bytes),
              decoded_session) &&
          decoded_session.sequence == 12U && decoded_session.generation == 2U &&
          decoded_session.entry_count == 2U && decoded_session.entries[0].handle ==
              session_catalog_snapshot.entries[0].handle &&
          decoded_session.entries[1].route_state == SessionCatalogRouteStateV1::Unavailable);
    auto malformed_session = session_payload;
    malformed_session[2U] = 1U;
    CHECK(!decode_session_catalog_snapshot_v1(
        std::span<const std::uint8_t>(malformed_session.data(), session_bytes), decoded_session));
    SessionCatalogSnapshotStoreV1 session_store;
    IpcFrameV1 session_response;
    CHECK(!session_store.has_snapshot() && !session_store.reply(session_response) &&
          session_store.publish(session_catalog_snapshot) && session_store.sequence() == 12U &&
          session_store.reply(session_response) &&
          session_response.header.type == IpcMessageType::SessionCatalogSnapshot &&
          session_response.payload.size() == session_bytes);
    CHECK(!session_store.publish(session_catalog_snapshot) && session_store.sequence() == 12U &&
          session_catalog_snapshot_reply_v1(session_response, &session_store));
    SessionVolumeCommandV1 session_volume_command{(2ULL << 32U) | 1ULL,
                                                  -622592, 1U, 12U};
    const auto session_volume_payload =
        encode_session_volume_command_v1(session_volume_command);
    SessionVolumeCommandV1 decoded_session_volume{};
    CHECK(decode_session_volume_command_v1(session_volume_payload, decoded_session_volume) &&
          decoded_session_volume.handle == session_volume_command.handle &&
          decoded_session_volume.requested_db_q16_16 == -622592 &&
          decoded_session_volume.mute == 1U && decoded_session_volume.catalog_sequence == 12U);
    auto malformed_session_volume = session_volume_payload;
    malformed_session_volume[13U] = 1U;
    CHECK(!decode_session_volume_command_v1(malformed_session_volume, decoded_session_volume));
    SessionRouteCommandV1 session_route_command{};
    session_route_command.handle = session_volume_command.handle;
    session_route_command.catalog_sequence = 12U;
    session_route_command.lane_bytes = 4U;
    session_route_command.output_group_bytes = 8U;
    std::copy_n("game", 4U, session_route_command.lane.data());
    std::copy_n("surround", 8U, session_route_command.output_group.data());
    const auto session_route_payload = encode_session_route_command_v1(session_route_command);
    SessionRouteCommandV1 decoded_session_route{};
    CHECK(decode_session_route_command_v1(session_route_payload, decoded_session_route) &&
          decoded_session_route.handle == session_route_command.handle &&
          decoded_session_route.catalog_sequence == 12U &&
          std::string_view(decoded_session_route.lane.data(), decoded_session_route.lane_bytes) ==
              "game" &&
          std::string_view(decoded_session_route.output_group.data(),
                           decoded_session_route.output_group_bytes) == "surround");
    auto malformed_session_route = session_route_payload;
    malformed_session_route[18U] = 1U;
    CHECK(!decode_session_route_command_v1(malformed_session_route, decoded_session_route));
    SessionRouteRuleCommandV1 session_route_rule_command{};
    session_route_rule_command.priority = 20;
    session_route_rule_command.makeup_gain_q16_16 = db_to_q16_16(3.5);
    session_route_rule_command.catalog_sequence = 12U;
    session_route_rule_command.rule_id_bytes = 10U;
    session_route_rule_command.app_id_bytes = 8U;
    session_route_rule_command.display_name_bytes = 5U;
    session_route_rule_command.lane_bytes = 4U;
    session_route_rule_command.output_group_bytes = 8U;
    std::memcpy(session_route_rule_command.rule_id.data(), "quiet-game", 10U);
    std::memcpy(session_route_rule_command.app_id.data(), "game.exe", 8U);
    std::memcpy(session_route_rule_command.display_name.data(), "DJMAX", 5U);
    std::memcpy(session_route_rule_command.lane.data(), "game", 4U);
    std::memcpy(session_route_rule_command.output_group.data(), "surround", 8U);
    const auto session_route_rule_payload =
        encode_session_route_rule_command_v1(session_route_rule_command);
    SessionRouteRuleCommandV1 decoded_session_route_rule{};
    CHECK(decode_session_route_rule_command_v1(session_route_rule_payload,
                                               decoded_session_route_rule) &&
          decoded_session_route_rule.priority == 20 &&
          std::abs(q16_16_to_db(decoded_session_route_rule.makeup_gain_q16_16) - 3.5) < 0.00002 &&
          std::string_view(decoded_session_route_rule.rule_id.data(),
                           decoded_session_route_rule.rule_id_bytes) == "quiet-game" &&
          std::string_view(decoded_session_route_rule.output_group.data(),
                           decoded_session_route_rule.output_group_bytes) == "surround");
    const auto decoded_upsert_session_route_rule = decoded_session_route_rule;
    auto malformed_session_route_rule = session_route_rule_payload;
    malformed_session_route_rule[29U] = 1U;
    CHECK(!decode_session_route_rule_command_v1(malformed_session_route_rule,
                                                decoded_session_route_rule));
    session_route_rule_command.operation = SessionRouteRuleOperationV1::Remove;
    session_route_rule_command.app_id_bytes = 0U;
    session_route_rule_command.display_name_bytes = 0U;
    session_route_rule_command.lane_bytes = 0U;
    session_route_rule_command.output_group_bytes = 0U;
    const auto remove_route_rule_payload =
        encode_session_route_rule_command_v1(session_route_rule_command);
    CHECK(decode_session_route_rule_command_v1(remove_route_rule_payload,
                                               decoded_session_route_rule) &&
          decoded_session_route_rule.operation == SessionRouteRuleOperationV1::Remove);
    session_route_rule_command.operation = SessionRouteRuleOperationV1::Clear;
    session_route_rule_command.rule_id_bytes = 0U;
    const auto clear_route_rule_payload =
        encode_session_route_rule_command_v1(session_route_rule_command);
    CHECK(decode_session_route_rule_command_v1(clear_route_rule_payload,
                                               decoded_session_route_rule) &&
          decoded_session_route_rule.operation == SessionRouteRuleOperationV1::Clear &&
          is_valid_message_type(IpcMessageType::SessionRouteRuleCommand));
    IrPrepareCommandV1 ir_prepare_command{};
    ir_prepare_command.mode = 2U;
    ir_prepare_command.strength_q16_16 = 32768;
    ir_prepare_command.expected_sample_rate = 48000U;
    ir_prepare_command.expected_channels = 2U;
    constexpr std::string_view ir_prepare_path = "C:/Hibiki/measurements/movie.wav";
    ir_prepare_command.path_bytes = static_cast<std::uint16_t>(ir_prepare_path.size());
    std::copy(ir_prepare_path.begin(), ir_prepare_path.end(), ir_prepare_command.path.begin());
    const auto ir_prepare_payload = encode_ir_prepare_command_v1(ir_prepare_command);
    IrPrepareCommandV1 decoded_ir_prepare{};
    CHECK(decode_ir_prepare_command_v1(ir_prepare_payload, decoded_ir_prepare) &&
          decoded_ir_prepare.mode == 2U && decoded_ir_prepare.strength_q16_16 == 32768 &&
          decoded_ir_prepare.expected_sample_rate == 48000U &&
          decoded_ir_prepare.expected_channels == 2U &&
          std::string_view(decoded_ir_prepare.path.data(), decoded_ir_prepare.path_bytes) ==
              ir_prepare_path && is_valid_message_type(IpcMessageType::IrPrepareCommand));
    auto malformed_ir_prepare = ir_prepare_payload;
    malformed_ir_prepare[22U] = 1U;
    CHECK(!decode_ir_prepare_command_v1(malformed_ir_prepare, decoded_ir_prepare));
    ir_prepare_command.mode = 3U;
    ir_prepare_command.strength_q16_16 = 1;
    CHECK(encode_ir_prepare_command_v1(ir_prepare_command)[0U] == 0U);
    IpcFrameV1 session_volume_frame;
    session_volume_frame.header.type = IpcMessageType::SessionVolumeCommand;
    session_volume_frame.header.request_id = 780U;
    session_volume_frame.payload.assign(session_volume_payload.begin(),
                                        session_volume_payload.end());
    CHECK(decode_control_command_v1(session_volume_frame, decoded_command) &&
          decoded_command.type == IpcMessageType::SessionVolumeCommand &&
          decoded_command.session_volume.handle == session_volume_command.handle);
    IpcFrameV1 session_route_frame;
    session_route_frame.header.type = IpcMessageType::SessionRouteCommand;
    session_route_frame.header.request_id = 781U;
    session_route_frame.payload.assign(session_route_payload.begin(), session_route_payload.end());
    CHECK(decode_control_command_v1(session_route_frame, decoded_command) &&
          decoded_command.type == IpcMessageType::SessionRouteCommand &&
          decoded_command.session_route.output_group_bytes == 8U);
    IpcFrameV1 session_route_rule_frame;
    session_route_rule_frame.header.type = IpcMessageType::SessionRouteRuleCommand;
    session_route_rule_frame.header.request_id = 782U;
    session_route_rule_frame.payload.assign(session_route_rule_payload.begin(),
                                            session_route_rule_payload.end());
    CHECK(decode_control_command_v1(session_route_rule_frame, decoded_command) &&
          decoded_command.type == IpcMessageType::SessionRouteRuleCommand &&
          decoded_command.session_route_rule.rule_id_bytes == 10U);
    IpcFrameV1 ir_prepare_frame;
    ir_prepare_frame.header.type = IpcMessageType::IrPrepareCommand;
    ir_prepare_frame.header.request_id = 783U;
    ir_prepare_frame.payload.assign(ir_prepare_payload.begin(), ir_prepare_payload.end());
    CHECK(decode_control_command_v1(ir_prepare_frame, decoded_command) &&
          decoded_command.type == IpcMessageType::IrPrepareCommand &&
          decoded_command.ir_prepare.path_bytes == ir_prepare_path.size());
    ControlCommandQueueV1 command_queue;
    ControlCommandV1 queued_command{};
    queued_command.type = IpcMessageType::SceneApply;
    queued_command.request_id = 123U;
    CHECK(command_queue.try_push(queued_command));
    CHECK(enqueue_control_command_v1(queued_command, &command_queue));
    CHECK(command_queue.try_pop(decoded_command) && decoded_command.request_id == 123U);
    CHECK(command_queue.try_pop(decoded_command) && decoded_command.request_id == 123U);
    for (std::size_t index = 0; index < ControlCommandQueueV1::kCapacity; ++index) {
        CHECK(command_queue.try_push(queued_command));
    }
    CHECK(!command_queue.try_push(queued_command) && command_queue.dropped() == 1U);
    SessionCommandQueueV1 session_command_queue;
    CHECK(session_command_queue.try_push_volume(session_volume_command) &&
          session_command_queue.try_push_route(session_route_command));
    SessionCommandWorkItemV1 session_work_item{};
    CHECK(session_command_queue.try_pop(session_work_item) &&
          session_work_item.kind == SessionCommandKindV1::Volume &&
          session_work_item.volume.handle == session_volume_command.handle);
    CHECK(session_command_queue.try_pop(session_work_item) &&
          session_work_item.kind == SessionCommandKindV1::Route &&
          session_work_item.route.output_group_bytes ==
              session_route_command.output_group_bytes);
    CHECK(session_command_queue.try_push_route_rule(session_route_rule_command));
    CHECK(session_command_queue.try_pop(session_work_item) &&
          session_work_item.kind == SessionCommandKindV1::RouteRule &&
          session_work_item.route_rule.rule_id_bytes ==
              session_route_rule_command.rule_id_bytes);
    SessionCommandWorkItemV1 filled_session_item{};
    filled_session_item.kind = SessionCommandKindV1::Volume;
    for (std::size_t index = 0U; index < SessionCommandQueueV1::kCapacity; ++index) {
        CHECK(session_command_queue.try_push(filled_session_item));
    }
    CHECK(!session_command_queue.try_push(filled_session_item) &&
          session_command_queue.dropped() == 1U);
    session_command_queue.reset();
    CHECK(session_command_queue.dropped() == 0U && !session_command_queue.try_pop(session_work_item));
    bool command_accepted = false;
    ControlPlaneHandlerContextV1 service_context{accept_control_command, &command_accepted};
    IpcFrameV1 service_response;
    CHECK(handle_control_frame_v1(scene_frame, service_response, &service_context) &&
          command_accepted && service_response.header.type == IpcMessageType::Ack &&
          service_response.header.request_id == scene_frame.header.request_id);
    bool catalog_request_accepted = false;
    SnapshotReplyContext snapshot_reply_context{snapshot_payload.data(), snapshot_bytes};
    ControlPlaneHandlerContextV1 catalog_service_context{
        accept_catalog_request, &catalog_request_accepted, provide_catalog_snapshot,
        &snapshot_reply_context};
    IpcFrameV1 catalog_request;
    catalog_request.header.type = IpcMessageType::DeviceCatalogRequest;
    catalog_request.header.request_id = 777U;
    IpcFrameV1 catalog_response;
    CHECK(handle_control_frame_v1(catalog_request, catalog_response,
                                  &catalog_service_context) &&
          catalog_request_accepted &&
          catalog_response.header.type == IpcMessageType::DeviceCatalogSnapshot &&
          catalog_response.header.request_id == 777U &&
          catalog_response.payload.size() == snapshot_bytes);
    ControlPlaneHandlerContextV1 no_snapshot_service_context{
        accept_catalog_request, &catalog_request_accepted, nullptr, nullptr};
    CHECK(handle_control_frame_v1(catalog_request, catalog_response,
                                  &no_snapshot_service_context) &&
          catalog_response.header.type == IpcMessageType::Error &&
          catalog_response.header.request_id == 777U);
    bool status_request_accepted = false;
    ControlPlaneHandlerContextV1 status_service_context{
        accept_status_request, &status_request_accepted, nullptr, nullptr,
        control_status_snapshot_reply_v1, &status_store};
    IpcFrameV1 status_request;
    status_request.header.type = IpcMessageType::ControlStatusRequest;
    status_request.header.request_id = 778U;
    CHECK(handle_control_frame_v1(status_request, status_response,
                                  &status_service_context) && status_request_accepted &&
          status_response.header.type == IpcMessageType::ControlStatusSnapshot &&
          status_response.header.request_id == 778U);
    bool session_request_accepted = false;
    ControlPlaneHandlerContextV1 session_service_context{
        accept_session_catalog_request, &session_request_accepted, nullptr, nullptr,
        nullptr, nullptr, session_catalog_snapshot_reply_v1, &session_store};
    IpcFrameV1 session_request;
    session_request.header.type = IpcMessageType::SessionCatalogRequest;
    session_request.header.request_id = 779U;
    CHECK(handle_control_frame_v1(session_request, session_response,
                                  &session_service_context) && session_request_accepted &&
          session_response.header.type == IpcMessageType::SessionCatalogSnapshot &&
          session_response.header.request_id == 779U &&
          session_response.payload.size() == session_bytes);

    auto scene_catalog = std::make_unique<SceneCatalogV1>();
    auto custom_defaults = make_easy_scene(EasySceneKind::Movie, "custom-output");
    custom_defaults.scene.id = "quiet-game";
    custom_defaults.scene.name = "Quiet Game";
    SceneDefinitionV1 custom_definition;
    custom_definition.scene = std::move(custom_defaults.scene);
    custom_definition.graph = std::move(custom_defaults.graph);
    custom_definition.loudness = std::move(custom_defaults.loudness);
    CHECK(validate_scene_definition_v1(custom_definition) &&
          scene_catalog->upsert(custom_definition) == SceneCatalogResultV1::Applied &&
          scene_catalog->size() == 1U && scene_catalog->find("quiet-game") != nullptr);
    auto invalid_definition = custom_definition;
    invalid_definition.graph.strict_direct = !invalid_definition.graph.strict_direct;
    CHECK(!validate_scene_definition_v1(invalid_definition) &&
          scene_catalog->upsert(invalid_definition) == SceneCatalogResultV1::Invalid);
    auto invalid_scene_id_definition = custom_definition;
    invalid_scene_id_definition.scene.id = "Bad Scene";
    CHECK(!validate_scene_definition_v1(invalid_scene_id_definition));
    auto capacity_catalog = std::make_unique<SceneCatalogV1>();
    for (std::size_t scene_index = 0U; scene_index < kMaxCustomScenesV1; ++scene_index) {
        auto capacity_definition = custom_definition;
        capacity_definition.scene.id = "scene-" + std::to_string(scene_index);
        CHECK(capacity_catalog->upsert(capacity_definition) == SceneCatalogResultV1::Applied);
    }
    auto over_capacity_definition = custom_definition;
    over_capacity_definition.scene.id = "scene-over-capacity";
    CHECK(capacity_catalog->upsert(over_capacity_definition) ==
          SceneCatalogResultV1::CapacityExhausted && capacity_catalog->size() == kMaxCustomScenesV1);
    CHECK(capacity_catalog->remove("scene-0") == SceneCatalogResultV1::Applied &&
          capacity_catalog->size() == kMaxCustomScenesV1 - 1U &&
          capacity_catalog->upsert(over_capacity_definition) == SceneCatalogResultV1::Applied &&
          capacity_catalog->size() == kMaxCustomScenesV1);
    capacity_catalog->clear();
    CHECK(capacity_catalog->size() == 0U && capacity_catalog->find("scene-1") == nullptr);
    auto custom_scene_engine = std::make_unique<AudioEngineModel>();
    EngineControlWorkerV1 custom_scene_worker(*custom_scene_engine);
    custom_scene_worker.set_scene_catalog(scene_catalog.get());
    ControlCommandV1 custom_scene_command{};
    custom_scene_command.type = IpcMessageType::SceneApply;
    CHECK(encode_scene_apply_payload_v1("quiet-game", "custom-output", scene_payload));
    CHECK(decode_scene_apply_payload_v1(scene_payload, custom_scene_command.scene));
    CHECK(custom_scene_worker.consume(custom_scene_command) == EngineControlResultV1::Applied &&
          custom_scene_worker.active_scene().id == "quiet-game" &&
          custom_scene_worker.active_scene().output_group == "custom-output" &&
          custom_scene_worker.revision() == 1U);
    ControlCommandV1 grouped_volume_command{};
    grouped_volume_command.type = IpcMessageType::VolumeNotification;
    grouped_volume_command.volume = VolumeNotificationV1{-9.0, false, 1U};
    grouped_volume_command.has_volume_target = true;
    grouped_volume_command.volume_target.output_group_bytes = 13U;
    std::copy_n("custom-output", 13U, grouped_volume_command.volume_target.output_group.data());
    CHECK(custom_scene_worker.consume(grouped_volume_command) == EngineControlResultV1::Applied &&
          std::abs(custom_scene_engine->volume("custom-output").requested_db + 9.0) < 1e-12);
    CHECK(encode_scene_apply_payload_v1("quiet-game", "main", scene_payload));
    CHECK(decode_scene_apply_payload_v1(scene_payload, custom_scene_command.scene));
    CHECK(custom_scene_worker.consume(custom_scene_command) == EngineControlResultV1::Invalid);

    // The UI sends the bounded wire form; the owning engine worker must turn
    // that command into a validated complete SceneDefinition before applying it.
    SceneCatalogCommandV1 catalog_command{};
    catalog_command.operation = SessionRouteRuleOperationV1::Upsert;
    catalog_command.id_bytes = 10;
    std::copy_n("quiet-game", 10, catalog_command.id.data());
    catalog_command.name_bytes = 10;
    std::copy_n("Quiet Game", 10, catalog_command.name.data());
    catalog_command.output_group_bytes = 13;
    std::copy_n("custom-output", 13, catalog_command.output_group.data());
    catalog_command.standard_id = 1U;
    catalog_command.lanes[0].id_bytes = 15;
    std::copy_n("quiet-game-lane", 15, catalog_command.lanes[0].id.data());
    catalog_command.lanes[0].output_group_bytes = 13;
    std::copy_n("custom-output", 13, catalog_command.lanes[0].output_group.data());
    std::vector<std::uint8_t> catalog_payload;
    CHECK(encode_scene_catalog_command_v1(catalog_command, catalog_payload));
    IpcFrameV1 catalog_frame;
    catalog_frame.header.type = IpcMessageType::SceneCatalogCommand;
    catalog_frame.header.request_id = 555U;
    catalog_frame.payload.assign(catalog_payload.begin(), catalog_payload.end());
    ControlCommandV1 decoded_catalog_command{};
    CHECK(decode_control_command_v1(catalog_frame, decoded_catalog_command) &&
          decoded_catalog_command.type == IpcMessageType::SceneCatalogCommand &&
          decoded_catalog_command.request_id == 555U);
    auto owned_scene_catalog_engine = std::make_unique<AudioEngineModel>();
    EngineControlWorkerV1 scene_catalog_worker(*owned_scene_catalog_engine);
    scene_catalog_worker.ensure_owned_scene_catalog();
    CHECK(scene_catalog_worker.consume(decoded_catalog_command) == EngineControlResultV1::Applied);
    const auto* const stored_definition =
        scene_catalog_worker.mutable_scene_catalog()->find("quiet-game");
    CHECK(stored_definition != nullptr &&
          stored_definition->scene.name == "Quiet Game" &&
          stored_definition->scene.output_group == "custom-output" &&
          stored_definition->graph.lanes.size() == 1U &&
          stored_definition->graph.lanes[0].id == "quiet-game-lane");

    ControlCommandV1 catalog_apply_command{};
    catalog_apply_command.type = IpcMessageType::SceneApply;
    std::array<std::uint8_t, kSceneApplyPayloadBytesV1> catalog_scene_payload{};
    CHECK(encode_scene_apply_payload_v1("quiet-game", "custom-output",
                                        catalog_scene_payload));
    CHECK(decode_scene_apply_payload_v1(catalog_scene_payload,
                                        catalog_apply_command.scene));
    CHECK(scene_catalog_worker.consume(catalog_apply_command) == EngineControlResultV1::Applied &&
          scene_catalog_worker.active_scene().id == "quiet-game" &&
          scene_catalog_worker.active_scene().output_group == "custom-output");

    SceneCatalogCommandV1 remove_catalog_command{};
    remove_catalog_command.operation = SessionRouteRuleOperationV1::Remove;
    remove_catalog_command.lane_count = 0U;
    remove_catalog_command.id_bytes = 10;
    std::copy_n("quiet-game", 10, remove_catalog_command.id.data());
    CHECK(encode_scene_catalog_command_v1(remove_catalog_command, catalog_payload));
    IpcFrameV1 remove_catalog_frame;
    remove_catalog_frame.header.type = IpcMessageType::SceneCatalogCommand;
    remove_catalog_frame.payload.assign(catalog_payload.begin(), catalog_payload.end());
    ControlCommandV1 decoded_remove_catalog_command{};
    CHECK(decode_control_command_v1(remove_catalog_frame,
                                    decoded_remove_catalog_command));
    CHECK(scene_catalog_worker.consume(decoded_remove_catalog_command) ==
          EngineControlResultV1::Applied &&
          scene_catalog_worker.mutable_scene_catalog()->find("quiet-game") == nullptr);

    std::vector<std::uint8_t> corrupted_catalog_payload = catalog_payload;
    corrupted_catalog_payload[15U] = kSceneCatalogLaneCountV1 + 1U;
    IpcFrameV1 corrupted_catalog_frame;
    corrupted_catalog_frame.header.type = IpcMessageType::SceneCatalogCommand;
    corrupted_catalog_frame.payload = corrupted_catalog_payload;
    ControlCommandV1 corrupted_catalog_command{};
    CHECK(!decode_control_command_v1(corrupted_catalog_frame,
                                     corrupted_catalog_command));

    AudioEngineModel control_engine;
    EngineControlWorkerV1 control_worker(control_engine);
    ControlCommandQueueV1 control_queue;
    ControlCommandV1 scene_command{};
    scene_command.type = IpcMessageType::SceneApply;
    scene_command.scene = decoded_scene;
    CHECK(control_queue.try_push(scene_command));
    CHECK(control_worker.drain(control_queue) == 1U && control_worker.has_active_scene() &&
          control_worker.active_scene().output_group == "main" && control_worker.revision() == 1U);
    bool scene_gate_open = false;
    control_worker.set_scene_preflight(allow_scene_preflight, &scene_gate_open);
    CHECK(control_worker.consume(scene_command) == EngineControlResultV1::Failed &&
          control_worker.revision() == 1U && control_worker.active_scene().output_group == "main");
    scene_gate_open = true;
    CHECK(control_worker.consume(scene_command) == EngineControlResultV1::Applied &&
          control_worker.revision() == 2U);
    bool device_switch_accepted = false;
    control_worker.set_device_switch_handler(accept_device_switch, &device_switch_accepted);
    CHECK(decode_control_command_v1(device_frame, decoded_command) &&
          control_worker.consume(decoded_command) == EngineControlResultV1::Applied &&
          device_switch_accepted);
    decoded_command.device_switch.channels = 8U;
    CHECK(control_worker.consume(decoded_command) == EngineControlResultV1::Failed);
    bool session_volume_accepted = false;
    control_worker.set_session_volume_handler(accept_session_volume, &session_volume_accepted);
    ControlCommandV1 session_volume_control{};
    session_volume_control.type = IpcMessageType::SessionVolumeCommand;
    session_volume_control.session_volume = session_volume_command;
    CHECK(control_worker.consume(session_volume_control) == EngineControlResultV1::Applied &&
          session_volume_accepted);
    session_volume_control.session_volume.catalog_sequence = 13U;
    CHECK(control_worker.consume(session_volume_control) == EngineControlResultV1::Failed);
    bool session_route_accepted = false;
    control_worker.set_session_route_handler(accept_session_route, &session_route_accepted);
    ControlCommandV1 session_route_control{};
    session_route_control.type = IpcMessageType::SessionRouteCommand;
    session_route_control.session_route = session_route_command;
    CHECK(control_worker.consume(session_route_control) == EngineControlResultV1::Applied &&
          session_route_accepted);
    session_route_control.session_route.catalog_sequence = 13U;
    CHECK(control_worker.consume(session_route_control) == EngineControlResultV1::Failed);
    bool session_route_rule_accepted = false;
    control_worker.set_session_route_rule_handler(accept_session_route_rule,
                                                   &session_route_rule_accepted);
    ControlCommandV1 session_route_rule_control{};
    session_route_rule_control.type = IpcMessageType::SessionRouteRuleCommand;
    session_route_rule_control.session_route_rule = decoded_upsert_session_route_rule;
    CHECK(control_worker.consume(session_route_rule_control) == EngineControlResultV1::Applied &&
          session_route_rule_accepted);
    session_route_rule_control.session_route_rule.catalog_sequence = 13U;
    CHECK(control_worker.consume(session_route_rule_control) == EngineControlResultV1::Failed);
    control_engine.set_sample_rate(8000U);
    ControlCommandV1 control_volume{};
    control_volume.type = IpcMessageType::VolumeNotification;
    control_volume.volume = VolumeNotificationV1{0.0, false, 1U};
    CHECK(control_queue.try_push(control_volume));
    CHECK(control_worker.drain(control_queue) == 1U);
    std::array<float, 256> control_input{};
    std::array<float, 256> control_output{};
    for (std::size_t index = 0U; index < control_input.size(); index += 2U) {
        control_input[index] = 1.0F;
        control_input[index + 1U] = -1.0F;
    }
    const RtLaneInputV1 control_view{control_input.data(), 2};
    CHECK(control_engine.process(std::span<const RtLaneInputV1>(&control_view, 1),
                                 control_output.data(), 128U));
    CHECK(std::abs(control_output[254] - 0.8912509F) < 1e-5F &&
          std::abs(control_output[255] + 0.8912509F) < 1e-5F);
    control_volume.volume = VolumeNotificationV1{-6.0206, false, 2U};
    CHECK(control_queue.try_push(control_volume));
    CHECK(control_worker.drain(control_queue) == 1U);
    CHECK(control_engine.process(std::span<const RtLaneInputV1>(&control_view, 1),
                                 control_output.data(), 128U));
    CHECK(control_engine.process(std::span<const RtLaneInputV1>(&control_view, 1),
                                 control_output.data(), 128U));
    CHECK(std::abs(control_output[254] - 0.5F) < 1e-5F);
    CHECK(control_engine.process_output_group("main",
                                             std::span<const RtLaneInputV1>(&control_view, 1),
                                             control_output.data(), 128U));
    CHECK(std::abs(control_output[254] - 0.5F) < 1e-5F);
    CHECK(!control_engine.process_output_group("missing",
                                              std::span<const RtLaneInputV1>(&control_view, 1),
                                              control_output.data(), 128U));
    scene_command.scene.scene_id_bytes = 7U;
    std::copy_n("unknown", 7U, scene_command.scene.scene_id.data());
    CHECK(control_queue.try_push(scene_command));
    CHECK(control_worker.drain(control_queue) == 1U && control_worker.revision() == 2U);
    auto malformed = encoded;
    malformed[0] = 0;
    CHECK(!decode_ipc_frame(malformed, decode_error).has_value());
    CHECK(decode_error == IpcDecodeError::InvalidMagic);
    IpcNamedPipeServerV1 ipc_server;
    CHECK(!ipc_server.start(IpcNamedPipeConfigV1{L"", 1024U, 100U}, nullptr, nullptr));
    ControlPlaneHostV1 control_host;
    CHECK(!control_host.start(IpcNamedPipeConfigV1{L"", 1024U, 100U}, nullptr, nullptr) &&
          !control_host.running() &&
          !control_host.start_with_queue(IpcNamedPipeConfigV1{L"", 1024U, 100U}));
#if defined(_WIN32)
    const std::wstring control_pipe =
        L"\\\\.\\pipe\\HibikiDSP_contract_control_" + std::to_wstring(_getpid());
    const wchar_t* const kControlPipe = control_pipe.c_str();
    IpcNamedPipeConfigV1 owned_config{};
    owned_config.pipe_name = control_pipe;
    owned_config.max_frame_bytes = 1024U;
    owned_config.io_timeout_ms = 1000U;
    owned_config.require_first_pipe_instance = true;
    CHECK(ipc_server.start(owned_config,
                           acknowledge_ipc_request, nullptr));
    // Issue #628 regression: a canonical single-owner pipe must fail closed
    // when another server already owns the first instance of the same name.
    IpcNamedPipeServerV1 duplicate_owned_server;
    CHECK(!duplicate_owned_server.start(owned_config, acknowledge_ipc_request, nullptr) &&
          !duplicate_owned_server.running());
    // Issue #637 regression: stop() immediately after a successful owned
    // start() must cancel the worker's pending connect instead of waiting for
    // the full idle timeout.
    {
        const std::wstring prompt_pipe =
            L"\\\\.\\pipe\\HibikiDSP_contract_prompt_" + std::to_wstring(_getpid());
        IpcNamedPipeServerV1 prompt_stop_server;
        IpcNamedPipeConfigV1 prompt_config{};
        prompt_config.pipe_name = prompt_pipe;
        prompt_config.max_frame_bytes = 1024U;
        prompt_config.io_timeout_ms = 1000U;
        prompt_config.require_first_pipe_instance = true;
        const auto prompt_start = std::chrono::steady_clock::now();
        CHECK(prompt_stop_server.start(prompt_config, acknowledge_ipc_request, nullptr));
        prompt_stop_server.stop();
    const auto prompt_elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - prompt_start)
            .count();
    CHECK(prompt_elapsed_ms < prompt_config.io_timeout_ms &&
          !prompt_stop_server.running());

    // Issue #655 regression: stop() during the between-connection gap must
    // also cancel promptly. A single cancellation can miss the short window
    // after a client disconnects and before the next ConnectNamedPipe() is
    // armed, so repeat until the worker observes stop.
    {
        const std::wstring between_pipe =
            L"\\\\.\\pipe\\HibikiDSP_contract_between_stop_" + std::to_wstring(_getpid());
        IpcNamedPipeServerV1 between_stop_server;
        IpcNamedPipeConfigV1 between_config{};
        between_config.pipe_name = between_pipe;
        between_config.max_frame_bytes = 1024U;
        between_config.io_timeout_ms = 1000U;
        CHECK(between_stop_server.start(between_config, acknowledge_ipc_request, nullptr));

        HANDLE between_client = INVALID_HANDLE_VALUE;
        for (int attempt = 0;
             attempt < 300 && between_client == INVALID_HANDLE_VALUE; ++attempt) {
            between_client = CreateFileW(between_pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0U,
                                         nullptr, OPEN_EXISTING, 0U, nullptr);
            if (between_client == INVALID_HANDLE_VALUE &&
                GetLastError() == ERROR_PIPE_BUSY) {
                (void)WaitNamedPipeW(between_pipe.c_str(), 100U);
            }
        }
        CHECK(between_client != INVALID_HANDLE_VALUE);
        DisconnectNamedPipe(between_client);
        CloseHandle(between_client);

        const auto between_start = std::chrono::steady_clock::now();
        between_stop_server.stop();
        const auto between_elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - between_start)
                .count();
        CHECK(between_elapsed_ms < between_config.io_timeout_ms &&
              !between_stop_server.running());
    }
    }
    HANDLE ipc_client = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 30 && ipc_client == INVALID_HANDLE_VALUE; ++attempt) {
        ipc_client = CreateFileW(kControlPipe, GENERIC_READ | GENERIC_WRITE, 0U, nullptr,
                                 OPEN_EXISTING, 0U, nullptr);
        if (ipc_client == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_PIPE_BUSY) (void)WaitNamedPipeW(kControlPipe, 100U);
            Sleep(10U);
        }
    }
    CHECK(ipc_client != INVALID_HANDLE_VALUE);
    const auto request_bytes = encode_ipc_frame(frame);
    const std::uint32_t request_size = static_cast<std::uint32_t>(request_bytes.size());
    DWORD transferred = 0U;
    CHECK(WriteFile(ipc_client, &request_size, sizeof(request_size), &transferred, nullptr) != FALSE &&
          transferred == sizeof(request_size));
    CHECK(WriteFile(ipc_client, request_bytes.data(), request_size, &transferred, nullptr) != FALSE &&
          transferred == request_size);
    std::uint32_t response_size = 0U;
    CHECK(ReadFile(ipc_client, &response_size, sizeof(response_size), &transferred, nullptr) != FALSE &&
          transferred == sizeof(response_size) && response_size <= 1024U);
    std::vector<std::uint8_t> response_bytes(response_size);
    CHECK(ReadFile(ipc_client, response_bytes.data(), response_size, &transferred, nullptr) != FALSE &&
          transferred == response_size);
    IpcDecodeError pipe_decode_error{IpcDecodeError::None};
    const auto pipe_response = decode_ipc_frame(response_bytes, pipe_decode_error);
    CHECK(pipe_response.has_value() && pipe_decode_error == IpcDecodeError::None &&
          pipe_response->header.type == IpcMessageType::Ack &&
          pipe_response->header.request_id == frame.header.request_id);
    // Issue #347 regression: client must survive an idle gap longer than io_timeout_ms.
    Sleep(1500U);
    DWORD idle_transferred = 0U;
    CHECK(WriteFile(ipc_client, &request_size, sizeof(request_size), &idle_transferred, nullptr) != FALSE &&
          idle_transferred == sizeof(request_size));
    CHECK(WriteFile(ipc_client, request_bytes.data(), request_size, &idle_transferred, nullptr) != FALSE &&
          idle_transferred == request_size);
    std::uint32_t idle_resp_size = 0U;
    CHECK(ReadFile(ipc_client, &idle_resp_size, sizeof(idle_resp_size), &idle_transferred, nullptr) != FALSE &&
          idle_transferred == sizeof(idle_resp_size) && idle_resp_size <= 1024U);
    std::vector<std::uint8_t> idle_resp_bytes(idle_resp_size);
    CHECK(ReadFile(ipc_client, idle_resp_bytes.data(), idle_resp_size, &idle_transferred, nullptr) != FALSE &&
          idle_transferred == idle_resp_size);
    const auto idle_decoded = decode_ipc_frame(idle_resp_bytes, pipe_decode_error);
    CHECK(idle_decoded.has_value() && pipe_decode_error == IpcDecodeError::None &&
          idle_decoded->header.type == IpcMessageType::Ack &&
          idle_decoded->header.request_id == frame.header.request_id);
    CloseHandle(ipc_client);
    ipc_server.stop();
    CHECK(!ipc_server.running());

    const std::wstring host_control_pipe =
        L"\\\\.\\pipe\\HibikiDSP_contract_host_" + std::to_wstring(_getpid());
    const wchar_t* const kHostControlPipe = host_control_pipe.c_str();
    ControlPlaneHostV1 host;
    CHECK(host.start_with_queue(IpcNamedPipeConfigV1{kHostControlPipe, 1024U, 1000U}));
    HANDLE host_client = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 30 && host_client == INVALID_HANDLE_VALUE; ++attempt) {
        host_client = CreateFileW(kHostControlPipe, GENERIC_READ | GENERIC_WRITE, 0U, nullptr,
                                  OPEN_EXISTING, 0U, nullptr);
        if (host_client == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_PIPE_BUSY) (void)WaitNamedPipeW(kHostControlPipe, 100U);
            Sleep(10U);
        }
    }
    CHECK(host_client != INVALID_HANDLE_VALUE);
    auto host_frame = scene_frame;
    host_frame.header.request_id = 314U;
    const auto host_request_bytes = encode_ipc_frame(host_frame);
    const auto host_request_size = static_cast<std::uint32_t>(host_request_bytes.size());
    transferred = 0U;
    CHECK(WriteFile(host_client, &host_request_size, sizeof(host_request_size), &transferred,
                    nullptr) != FALSE && transferred == sizeof(host_request_size) &&
          WriteFile(host_client, host_request_bytes.data(), host_request_size, &transferred,
                    nullptr) != FALSE && transferred == host_request_size);
    response_size = 0U;
    CHECK(ReadFile(host_client, &response_size, sizeof(response_size), &transferred, nullptr) !=
              FALSE &&
          transferred == sizeof(response_size) && response_size <= 1024U);
    std::vector<std::uint8_t> host_response_bytes(response_size);
    CHECK(ReadFile(host_client, host_response_bytes.data(), response_size, &transferred, nullptr) !=
              FALSE &&
          transferred == response_size);
    const auto host_response = decode_ipc_frame(host_response_bytes, pipe_decode_error);
    CHECK(host_response.has_value() && host_response->header.type == IpcMessageType::Ack &&
          host_response->header.request_id == 314U);
    CloseHandle(host_client);
    host.stop();
    CHECK(!host.running() && host.command_queue().try_pop(decoded_command) &&
          decoded_command.type == IpcMessageType::SceneApply);
#endif

    AsioBridgeModel asio;
    CHECK(asio.prepare(AsioStreamConfigV1{48000, 2, 128}));
    OutputGroupVolumeStateV1 asio_volume;
    asio_volume.effective_db = -6.0205999;
    asio.apply_group_volume(asio_volume);
    const float asio_input[] = {1.0F, -1.0F, 0.5F, -0.5F};
    float asio_output[4]{};
    CHECK(asio.process_interleaved(asio_input, asio_output, 2));
    CHECK(std::abs(asio_output[0] - 0.5F) < 1e-5F);
    CHECK(std::abs(asio_output[1] + 0.5F) < 1e-5F);
    asio_volume.mute = true;
    asio.apply_group_volume(asio_volume);
    CHECK(asio.process_interleaved(asio_input, asio_output, 2));
    CHECK(asio_output[0] == 0.0F && asio_output[1] == 0.0F);

    AudioSessionRegistry session_registry;
    const AudioSessionIdentityV1 chrome_tab_a{"hibiki-main", "chrome-instance-a", 1234};
    const AudioSessionIdentityV1 chrome_tab_b{"hibiki-main", "chrome-instance-b", 1234};
    CHECK(session_registry.upsert(AudioSessionDescriptorV1{
        1, chrome_tab_a, "Chrome tab A", "chrome.exe", true,
        SessionGainOwner::WindowsSession, {}, {}}));
    CHECK(session_registry.upsert(AudioSessionDescriptorV1{
        1, chrome_tab_b, "Chrome tab B", "chrome.exe", true,
        SessionGainOwner::WindowsSession, {}, {}}));
    auto route_rules = std::make_unique<SessionRouteRuleStoreV1>();
    SessionRouteRuleV1 chrome_rule;
    chrome_rule.rule_id = "chrome-vlog";
    chrome_rule.priority = 100;
    chrome_rule.app_id = "Chrome.EXE";
    chrome_rule.lane_id = "vlog-noise";
    chrome_rule.output_group = "headphones";
    chrome_rule.gain_owner = SessionGainOwner::HibikiInternal;
    chrome_rule.makeup_gain_db = 3.0;
    CHECK(route_rules->upsert(chrome_rule) == SessionRouteRuleResultV1::applied);
    auto rule_target = *session_registry.find(chrome_tab_b);
    rule_target.identity.process_id = 9876U;
    CHECK(route_rules->apply(rule_target) == SessionRouteRuleResultV1::applied &&
          rule_target.lane_id == "vlog-noise" &&
          rule_target.output_group == "headphones" &&
          rule_target.gain_owner == SessionGainOwner::HibikiInternal &&
          std::abs(rule_target.makeup_gain_db - 3.0) < 1e-12);
    SessionRouteRuleV1 display_rule;
    display_rule.rule_id = "chrome-display-fallback";
    display_rule.priority = 10;
    display_rule.display_name_contains = "TAB B";
    display_rule.lane_id = "fallback";
    display_rule.output_group = "main";
    CHECK(route_rules->upsert(display_rule) == SessionRouteRuleResultV1::applied);
    CHECK(route_rules->apply(rule_target) == SessionRouteRuleResultV1::applied &&
          rule_target.lane_id == "vlog-noise");
    SessionRouteRuleV1 tie_rule = chrome_rule;
    tie_rule.rule_id = "chrome-tie";
    tie_rule.output_group = "surround";
    CHECK(route_rules->upsert(tie_rule) == SessionRouteRuleResultV1::applied);
    CHECK(route_rules->apply(rule_target) == SessionRouteRuleResultV1::ambiguous &&
          rule_target.output_group == "headphones");
    SessionRouteRuleV1 invalid_rule;
    invalid_rule.rule_id = "missing-match";
    invalid_rule.lane_id = "lane";
    invalid_rule.output_group = "main";
    CHECK(route_rules->upsert(invalid_rule) == SessionRouteRuleResultV1::invalid_argument);
    CHECK(route_rules->remove("chrome-tie") && route_rules->size() == 2U);
    for (std::size_t rule_index = 0U; rule_index < 62U; ++rule_index) {
        SessionRouteRuleV1 capacity_rule;
        capacity_rule.rule_id = "capacity-" + std::to_string(rule_index);
        capacity_rule.priority = static_cast<std::int32_t>(rule_index);
        capacity_rule.app_id = "app-" + std::to_string(rule_index);
        capacity_rule.lane_id = "lane-" + std::to_string(rule_index);
        capacity_rule.output_group = "main";
        CHECK(route_rules->upsert(capacity_rule) == SessionRouteRuleResultV1::applied);
    }
    SessionRouteRuleV1 over_capacity_rule;
    over_capacity_rule.rule_id = "capacity-overflow";
    over_capacity_rule.app_id = "overflow.exe";
    over_capacity_rule.lane_id = "overflow";
    over_capacity_rule.output_group = "main";
    CHECK(route_rules->size() == kMaxSessionRouteRulesV1 &&
          route_rules->upsert(over_capacity_rule) == SessionRouteRuleResultV1::capacity_exhausted);
    // Store caps must agree with the fixed wire command and UI catalog:
    // matchers accept 128 bytes and route labels accept exactly 64 bytes.
    chrome_rule.display_name_contains.clear();
    chrome_rule.app_id.assign(kSessionRouteRuleMaxMatchBytesV1, 'a');
    CHECK(route_rules->upsert(chrome_rule) == SessionRouteRuleResultV1::applied);
    chrome_rule.app_id.assign(kSessionRouteRuleMaxMatchBytesV1 + 1U, 'a');
    CHECK(route_rules->upsert(chrome_rule) == SessionRouteRuleResultV1::invalid_argument);
    chrome_rule.app_id = "Chrome.EXE";
    chrome_rule.lane_id.assign(kSessionRouteRuleMaxRouteBytesV1, 'l');
    CHECK(route_rules->upsert(chrome_rule) == SessionRouteRuleResultV1::applied);
    chrome_rule.lane_id.assign(kSessionRouteRuleMaxRouteBytesV1 + 1U, 'l');
    CHECK(route_rules->upsert(chrome_rule) == SessionRouteRuleResultV1::invalid_argument);
    chrome_rule.lane_id = "vlog-noise";
    chrome_rule.output_group.assign(kSessionRouteRuleMaxRouteBytesV1, 'g');
    CHECK(route_rules->upsert(chrome_rule) == SessionRouteRuleResultV1::applied);
    chrome_rule.output_group.assign(kSessionRouteRuleMaxRouteBytesV1 + 1U, 'g');
    CHECK(route_rules->upsert(chrome_rule) == SessionRouteRuleResultV1::invalid_argument);
    chrome_rule.output_group = "headphones";
    CHECK(session_registry.bind(chrome_tab_a, "vlog-noise", "headphones"));
    CHECK(session_registry.bind(chrome_tab_b, "music", "speakers"));
    CHECK(session_registry.set_gain_owner(chrome_tab_a, SessionGainOwner::HibikiInternal));
    CHECK(session_registry.set_makeup_gain_db(chrome_tab_a, 6.0205999));
    CHECK(!session_registry.set_makeup_gain_db(chrome_tab_a, 13.0));
    CHECK(session_registry.find(chrome_tab_a)->lane_id == "vlog-noise");
    CHECK(session_registry.find(chrome_tab_a)->output_group == "headphones");
    CHECK(session_registry.find(chrome_tab_a)->gain_owner == SessionGainOwner::HibikiInternal);
    CHECK(session_registry.find(chrome_tab_b)->lane_id == "music");
    CHECK(session_registry.find(chrome_tab_b)->output_group == "speakers");
    GraphConfigV1 session_graph;
    CHECK(build_session_route_graph(session_registry, SessionRouteGraphPolicyV1{1, 2U, false},
                                    session_graph));
    CHECK(session_graph.lanes.size() == 2U &&
          std::abs(session_graph.lanes[0].makeup_gain_db - 6.0205999) < 1e-6 &&
          session_graph.lanes[0].output_group == "headphones");
    RtGraphSnapshotV1 session_snapshot;
    CHECK(compile_rt_snapshot(session_graph, 4U, session_snapshot));
    const float session_samples[] = {1.0F, -1.0F, 0.25F, -0.25F};
    const std::array<RtLaneInputV1, 2> session_inputs{
        RtLaneInputV1{session_samples, 2U}, RtLaneInputV1{session_samples + 2U, 2U}};
    float session_output[2]{};
    CHECK(process_graph_for_output_group(session_snapshot, "headphones", session_inputs,
                                         session_output, 1U));
    CHECK(std::abs(session_output[0] - 2.0F) < 1e-5F &&
          std::abs(session_output[1] + 2.0F) < 1e-5F);
    CHECK(process_graph_for_output_group(session_snapshot, "speakers", session_inputs,
                                         session_output, 1U));
    CHECK(std::abs(session_output[0] - 0.25F) < 1e-5F &&
          std::abs(session_output[1] + 0.25F) < 1e-5F);
    CHECK(!build_session_route_graph(session_registry,
                                     SessionRouteGraphPolicyV1{1, 2U, true}, session_graph));
    ProcessLoopbackPlanV1 process_plan;
    CHECK(build_process_loopback_plan(session_registry, process_plan) ==
              ProcessLoopbackPlanResultV1::AmbiguousProcess && process_plan.size == 0U);
    AudioSessionRegistry distinct_process_registry;
    CHECK(distinct_process_registry.upsert(AudioSessionDescriptorV1{
        1, AudioSessionIdentityV1{"hibiki-main", "distinct-a", 100U}, "A", "app-a", true,
        SessionGainOwner::WindowsSession, "lane-a", "main", 0.0}));
    CHECK(distinct_process_registry.upsert(AudioSessionDescriptorV1{
        1, AudioSessionIdentityV1{"hibiki-main", "distinct-b", 101U}, "B", "app-b", true,
        SessionGainOwner::WindowsSession, "lane-b", "main", 0.0}));
    CHECK(build_process_loopback_plan(distinct_process_registry, process_plan) ==
              ProcessLoopbackPlanResultV1::Applied && process_plan.size == 2U &&
          process_plan.entries[0].session_count == 1U);
    AudioSessionRegistry duplicate_process_registry;
    CHECK(duplicate_process_registry.upsert(AudioSessionDescriptorV1{
        1, AudioSessionIdentityV1{"hibiki-main", "same-a", 42U}, "A", "app-a", true,
        SessionGainOwner::WindowsSession, "lane-a", "main", 0.0}));
    CHECK(duplicate_process_registry.upsert(AudioSessionDescriptorV1{
        1, AudioSessionIdentityV1{"hibiki-main", "same-b", 42U}, "B", "app-b", true,
        SessionGainOwner::WindowsSession, "lane-a", "main", 0.0}));
    CHECK(build_process_loopback_plan(duplicate_process_registry, process_plan) ==
              ProcessLoopbackPlanResultV1::Applied && process_plan.size == 1U &&
          process_plan.entries[0].session_count == 2U);
    CHECK(duplicate_process_registry.set_makeup_gain_db(
        AudioSessionIdentityV1{"hibiki-main", "same-b", 42U}, 0.0));
    CHECK(duplicate_process_registry.bind(
        AudioSessionIdentityV1{"hibiki-main", "same-b", 42U}, "lane-b", "main"));
    CHECK(build_process_loopback_plan(duplicate_process_registry, process_plan) ==
          ProcessLoopbackPlanResultV1::AmbiguousProcess && process_plan.size == 0U);
    AudioSessionRegistry duplicate_lane_registry;
    CHECK(duplicate_lane_registry.upsert(AudioSessionDescriptorV1{
        1, AudioSessionIdentityV1{"hibiki-main", "lane-a", 10U}, "A", "app-a", true,
        SessionGainOwner::WindowsSession, "same-lane", "main", 0.0}));
    CHECK(duplicate_lane_registry.upsert(AudioSessionDescriptorV1{
        1, AudioSessionIdentityV1{"hibiki-main", "lane-b", 11U}, "B", "app-b", true,
        SessionGainOwner::WindowsSession, "same-lane", "main", 0.0}));
    CHECK(build_process_loopback_plan(duplicate_lane_registry, process_plan) ==
              ProcessLoopbackPlanResultV1::DuplicateLane && process_plan.size == 0U);
    AudioSessionRegistry invalid_process_registry;
    CHECK(invalid_process_registry.upsert(AudioSessionDescriptorV1{
        1, AudioSessionIdentityV1{"hibiki-main", "zero", 0U}, "zero", "zero", true,
        SessionGainOwner::WindowsSession, "lane", "main", 0.0}));
    CHECK(build_process_loopback_plan(invalid_process_registry, process_plan) ==
              ProcessLoopbackPlanResultV1::InvalidProcessIdentity && process_plan.size == 0U);
    CHECK(session_registry.upsert(AudioSessionDescriptorV1{
        1, AudioSessionIdentityV1{"hibiki-main", "chrome-instance-a", 5678},
        "Chrome tab A renamed", "chrome.exe", true,
        SessionGainOwner::WindowsSession, {}, {}}));
    CHECK(session_registry.find(chrome_tab_a)->lane_id == "vlog-noise");
    CHECK(session_registry.find(chrome_tab_a)->identity.process_id == 5678);
    CHECK(session_registry.remove(chrome_tab_b));
    CHECK(session_registry.find(chrome_tab_b) == nullptr);

    // Issue #987: output_group must be aligned to 64-byte canonical bound.
    AudioSessionRegistry og_bounds_registry;
    const std::string og_64(64U, 'a');
    const std::string og_65(65U, 'a');
    const AudioSessionIdentityV1 og_identity{"hibiki-main", "og-bounds", 1U};
    CHECK(og_bounds_registry.upsert(AudioSessionDescriptorV1{
        1, og_identity, "OG", "app.exe", true,
        SessionGainOwner::WindowsSession, {}, og_64, 0.0}));
    CHECK(!og_bounds_registry.upsert(AudioSessionDescriptorV1{
        1, og_identity, "OG", "app.exe", true,
        SessionGainOwner::WindowsSession, {}, og_65, 0.0}));
    CHECK(og_bounds_registry.bind(og_identity, "lane-x", og_64));
    CHECK(!og_bounds_registry.bind(og_identity, "lane-x", og_65));

    Vst3BusLayoutV1 sidechain_layout{};
    sidechain_layout.input_bus_count = 2U;
    sidechain_layout.output_bus_count = 1U;
    sidechain_layout.inputs[0] = Vst3AudioBusV1{Vst3BusRoleV1::Main, 1U, 0U, 2U};
    sidechain_layout.inputs[1] = Vst3AudioBusV1{Vst3BusRoleV1::Sidechain, 1U, 0U, 2U};
    sidechain_layout.outputs[0] = Vst3AudioBusV1{Vst3BusRoleV1::Main, 1U, 0U, 2U};
    CHECK(validate_vst3_bus_layout_v1(sidechain_layout) == Vst3BusLayoutResultV1::Valid &&
          vst3_bus_layout_matches_main_v1(sidechain_layout, 2U, 2U) &&
          vst3_bus_layout_has_sidechain_v1(sidechain_layout));
    auto invalid_bus_layout = sidechain_layout;
    invalid_bus_layout.inputs[0].role = Vst3BusRoleV1::Auxiliary;
    invalid_bus_layout.inputs[1].role = Vst3BusRoleV1::Main;
    CHECK(validate_vst3_bus_layout_v1(invalid_bus_layout) ==
              Vst3BusLayoutResultV1::MainNotFirst);
    invalid_bus_layout = sidechain_layout;
    invalid_bus_layout.outputs[1].channels = 2U;
    CHECK(validate_vst3_bus_layout_v1(invalid_bus_layout) ==
              Vst3BusLayoutResultV1::InvalidInactiveBus);
    invalid_bus_layout = sidechain_layout;
    invalid_bus_layout.outputs[0].role = Vst3BusRoleV1::Sidechain;
    CHECK(validate_vst3_bus_layout_v1(invalid_bus_layout) ==
              Vst3BusLayoutResultV1::SidechainOutput);

    PluginHostModel plugin;
    CHECK(!plugin.start(PluginDescriptorV1{"untrusted", 2, 2, 64, false, 250, true, 41U}));
    CHECK(plugin.state() == PluginHostState::Quarantined);
    CHECK(!plugin.start(PluginDescriptorV1{"missing-token", 2, 2, 64, true, 250, true, 0U}));
    CHECK(plugin.state() == PluginHostState::Quarantined);
    CHECK(plugin.start(PluginDescriptorV1{"builtin-test", 2, 2, 64, true, 250, true, 42U}));
    PluginDescriptorV1 sidechain_descriptor{"sidechain-test", 2, 2, 64, true, 250, true, 43U};
    sidechain_descriptor.bus_layout = sidechain_layout;
    CHECK(plugin.start(sidechain_descriptor));
    auto broken_sidechain_descriptor = sidechain_descriptor;
    broken_sidechain_descriptor.bus_layout.outputs[0].channels = 6U;
    CHECK(!plugin.start(broken_sidechain_descriptor) &&
          plugin.state() == PluginHostState::Quarantined);
    CHECK(plugin.start(PluginDescriptorV1{"builtin-test", 2, 2, 64, true, 250, true, 42U}));
    CHECK(plugin.latency_lane_input().lane_token == 42U &&
          plugin.latency_lane_input().active &&
          plugin.latency_lane_input().reported_latency_samples == 64U);
    CHECK(plugin.heartbeat(1000));
    CHECK(!plugin.poll_watchdog(1200));
    CHECK(plugin.poll_watchdog(1300));
    CHECK(plugin.state() == PluginHostState::Quarantined);
    CHECK(plugin.start(PluginDescriptorV1{"builtin-test", 2, 2, 64, true, 250, true, 42U}));
    const float plugin_input[] = {0.1F, -0.2F};
    float plugin_output[2]{};
    CHECK(plugin.process_passthrough(plugin_input, plugin_output, 2));
    CHECK(plugin_output[0] == plugin_input[0] && plugin_output[1] == plugin_input[1]);
    Vst3PluginStateIdentityV1 plugin_identity{};
    plugin_identity.plugin_id = "builtin-test";
    plugin_identity.class_id = "0123456789abcdef0123456789abcdef";
    plugin_identity.module_sha256[0] = 1U;
    const std::array<std::uint8_t, 3> plugin_state_bytes{{1U, 2U, 3U}};
    CHECK(plugin.capture_plugin_state("movie-state", plugin_identity, 1U, plugin_state_bytes) ==
              Vst3PluginStateResultV1::ok &&
          plugin.plugin_state_count() == 1U);
    std::array<std::uint8_t, 2> small_state_output{};
    std::size_t state_bytes_written = 0U;
    CHECK(plugin.restore_plugin_state("movie-state", plugin_identity, 1U, small_state_output,
                                      state_bytes_written) ==
          Vst3PluginStateResultV1::output_too_small);
    std::array<std::uint8_t, 3> restored_state{};
    CHECK(plugin.restore_plugin_state("movie-state", plugin_identity, 1U, restored_state,
                                      state_bytes_written) == Vst3PluginStateResultV1::ok &&
          state_bytes_written == 3U && restored_state == plugin_state_bytes);
    auto wrong_identity = plugin_identity;
    wrong_identity.module_sha256[0] = 2U;
    CHECK(plugin.restore_plugin_state("movie-state", wrong_identity, 1U, restored_state,
                                      state_bytes_written) ==
          Vst3PluginStateResultV1::identity_mismatch);
    CHECK(plugin.restore_plugin_state("movie-state", plugin_identity, 2U, restored_state,
                                      state_bytes_written) == Vst3PluginStateResultV1::version_mismatch);
    CHECK(plugin.restore_plugin_state_with_migration(
              "movie-state", plugin_identity, 2U, restored_state, state_bytes_written, nullptr) ==
          Vst3PluginStateResultV1::migration_unavailable);
    std::array<std::uint8_t, 4> migrated_state{};
    CHECK(plugin.restore_plugin_state_with_migration(
              "movie-state", plugin_identity, 2U, migrated_state, state_bytes_written,
              migrate_test_plugin_state) == Vst3PluginStateResultV1::ok &&
          state_bytes_written == 4U && migrated_state[0] == 1U && migrated_state[3] == 0x42U);
    CHECK(plugin.register_plugin_state_migration(plugin_identity, 1U, 2U,
                                                 migrate_test_plugin_state) ==
              Vst3PluginStateResultV1::ok &&
          plugin.plugin_state_migration_count() == 1U);
    migrated_state.fill(0U);
    CHECK(plugin.restore_plugin_state_via_registry("movie-state", plugin_identity, 2U,
                                                   migrated_state, state_bytes_written) ==
              Vst3PluginStateResultV1::ok &&
          state_bytes_written == 4U && migrated_state[3] == 0x42U);
    CHECK(plugin.register_plugin_state_migration(plugin_identity, 9U, 2U,
                                                 migrate_test_plugin_state) ==
              Vst3PluginStateResultV1::invalid_argument);
    CHECK(plugin.restore_plugin_state_with_migration(
              "movie-state", plugin_identity, 2U, restored_state, state_bytes_written,
              migrate_oversized_plugin_state) ==
              Vst3PluginStateResultV1::migration_output_too_large &&
          state_bytes_written == 0U);
    std::vector<std::uint8_t> max_plugin_state(kVst3PluginStateMaxBytesV1, 0x5AU);
    std::vector<std::uint8_t> oversized_plugin_state(kVst3PluginStateMaxBytesV1 + 1U, 0x5AU);
    CHECK(plugin.capture_plugin_state("max-state", plugin_identity, 1U, max_plugin_state) ==
              Vst3PluginStateResultV1::ok &&
          plugin.capture_plugin_state("oversized-state", plugin_identity, 1U,
                                      oversized_plugin_state) ==
              Vst3PluginStateResultV1::invalid_argument);

    Vst3PluginStateStoreV1 scene_state_store;
    Vst3PluginStateMigrationRegistryV1 scene_state_migrations;
    CHECK(scene_state_store.capture("scene-movie-state", plugin_identity, 1U,
                                    plugin_state_bytes) == Vst3PluginStateResultV1::ok);
    CHECK(scene_state_migrations.register_rule(plugin_identity, 1U, 2U,
                                               migrate_test_plugin_state) ==
              Vst3PluginStateResultV1::ok);
    Vst3SceneStateCoordinatorV1 scene_state;
    CHECK(scene_state.prepare(scene_state_store, scene_state_migrations));
    CHECK(scene_state.bind("movie", "scene-movie-state", plugin_identity, 2U) ==
              Vst3SceneStateResultV1::ok);
    CHECK(scene_state.validate_scene("movie") == Vst3SceneStateResultV1::ok);
    migrated_state.fill(0U);
    CHECK(scene_state.restore("movie", "scene-movie-state", migrated_state,
                             state_bytes_written) == Vst3SceneStateResultV1::ok &&
          state_bytes_written == 4U && migrated_state[3] == 0x42U);
    CHECK(scene_state.bind("main.scene", "scene-movie-state", plugin_identity, 2U) ==
              Vst3SceneStateResultV1::ok);
    const auto coordinator_scene = make_easy_scene(EasySceneKind::Game, "main").scene;
    CHECK(preflight_scene_vst3_state_v1(coordinator_scene, &scene_state));
    const auto unrelated_scene = make_easy_scene(EasySceneKind::Movie, "other").scene;
    CHECK(!preflight_scene_vst3_state_v1(unrelated_scene, &scene_state));
    CHECK(scene_state.validate_scene("missing") == Vst3SceneStateResultV1::missing_binding);
    CHECK(scene_state.remove("main.scene", "scene-movie-state"));
    CHECK(scene_state.remove("movie", "scene-movie-state") && scene_state.binding_count() == 0U);
    plugin.report_crash();
    CHECK(!plugin.process_passthrough(plugin_input, plugin_output, 2));

    Vst3SandboxProcess sandbox;
    const auto initial_sandbox_diagnostic = sandbox.diagnostic();
    CHECK(initial_sandbox_diagnostic.schema_version == 1U &&
          initial_sandbox_diagnostic.state == Vst3SandboxState::Stopped &&
          initial_sandbox_diagnostic.reason == Vst3SandboxDiagnosticReasonV1::None &&
          !initial_sandbox_diagnostic.worker_pipe_server_ready &&
          !initial_sandbox_diagnostic.worker_connected);
    CHECK(!sandbox.launch(Vst3SandboxLaunchV1{L"", L"", 250}));
    const auto invalid_launch_diagnostic = sandbox.diagnostic();
    CHECK(invalid_launch_diagnostic.state == Vst3SandboxState::Quarantined &&
          invalid_launch_diagnostic.reason == Vst3SandboxDiagnosticReasonV1::InvalidLaunch &&
          !invalid_launch_diagnostic.worker_pipe_server_ready &&
          !invalid_launch_diagnostic.worker_connected);
    CHECK(!validate_vst3_sandbox_launch_v1(Vst3SandboxLaunchV1{L"worker.exe", L"plugin.vst3", 250,
                                                               1U, L"pipe", 1000U, L"uid", 48000.0,
                                                               4U}));
    CHECK(validate_vst3_sandbox_launch_v1(Vst3SandboxLaunchV1{L"worker.exe", L"plugin.vst3", 250,
                                                              1U, L"pipe", 1000U, L"uid", 48000.0,
                                                              2U}));
    CHECK(sandbox.state() == Vst3SandboxState::Quarantined);
    sandbox.stop();
    CHECK(sandbox.state() == Vst3SandboxState::Stopped &&
          sandbox.diagnostic().reason == Vst3SandboxDiagnosticReasonV1::None);
#if defined(_WIN32)
    Vst3SandboxProcess setup_failure_sandbox;
    CHECK(!setup_failure_sandbox.launch(Vst3SandboxLaunchV1{
              L"hibiki-missing-worker-405.exe", L"missing-plugin-405.vst3", 250U}));
    CHECK(setup_failure_sandbox.diagnostic().state == Vst3SandboxState::Quarantined &&
          setup_failure_sandbox.diagnostic().reason ==
              Vst3SandboxDiagnosticReasonV1::ProcessSetupFailed);
#else
    Vst3SandboxProcess unsupported_sandbox;
    CHECK(!unsupported_sandbox.launch(Vst3SandboxLaunchV1{
              L"worker.exe", L"plugin.vst3", 250U}));
    CHECK(unsupported_sandbox.diagnostic().state == Vst3SandboxState::Quarantined &&
          unsupported_sandbox.diagnostic().reason ==
              Vst3SandboxDiagnosticReasonV1::UnsupportedPlatform);
#endif
    CHECK(sandbox.handshake_worker() == Vst3WorkerExchangeResultV1::not_running);
    const std::array<float, 4> worker_exchange_input{0.1F, -0.1F, 0.2F, -0.2F};
    std::array<float, 4> worker_exchange_output{1.0F, 1.0F, 1.0F, 1.0F};
    CHECK(sandbox.process_worker_block(1U, 2U, 2U, worker_exchange_input,
                                       worker_exchange_output) ==
          Vst3WorkerExchangeResultV1::not_running);
    CHECK(plugin.start(PluginDescriptorV1{"builtin-test", 2, 2, 64, true, 250, true, 42U}) &&
          plugin.prepare_worker_session(sandbox, 48000.0, 128U) &&
          plugin.worker_lane_state() == Vst3WorkerLaneStateV1::Prepared &&
          plugin.worker_latency_lane_input().lane_token == 42U &&
          !plugin.worker_latency_lane_input().active);
    CHECK(plugin.handshake_worker() == Vst3WorkerExchangeResultV1::not_running &&
          plugin.state() == PluginHostState::Quarantined &&
          plugin.worker_lane_state() == Vst3WorkerLaneStateV1::Detached);

    std::array<std::uint8_t, kVst3WorkerHeaderBytesV1 + 4U * sizeof(float)> worker_packet{};
    const Vst3WorkerFrameV1 worker_frame{Vst3WorkerMessageTypeV1::ProcessBlock, 17U, 2U, 2U,
                                         4U * sizeof(float), 0U};
    std::size_t worker_header_bytes = 0U;
    CHECK(encode_vst3_worker_frame_v1(worker_frame,
                                      std::span<std::uint8_t>(worker_packet).subspan(
                                          0U, kVst3WorkerHeaderBytesV1),
                                      worker_header_bytes));
    const float worker_samples[4] = {0.25F, -0.25F, 0.5F, -0.5F};
    std::memcpy(worker_packet.data() + kVst3WorkerHeaderBytesV1, worker_samples,
                sizeof(worker_samples));
    Vst3WorkerFrameV1 decoded_worker{};
    Vst3WorkerProtocolErrorV1 worker_error{Vst3WorkerProtocolErrorV1::None};
    std::span<const float> decoded_samples;
    CHECK(validate_vst3_worker_audio_frame_v1(worker_packet, decoded_worker, decoded_samples,
                                               worker_error));
    CHECK(decoded_worker.request_id == 17U && decoded_samples.size() == 4U &&
          std::abs(decoded_samples[2] - 0.5F) < 1e-6F);
    worker_packet[0] = 0U;
    CHECK(!decode_vst3_worker_frame_v1(worker_packet, decoded_worker, worker_error) &&
          worker_error == Vst3WorkerProtocolErrorV1::InvalidMagic);
    worker_packet[0] = 'H';
    float worker_nan = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(worker_packet.data() + kVst3WorkerHeaderBytesV1, &worker_nan, sizeof(worker_nan));
    CHECK(!validate_vst3_worker_audio_frame_v1(worker_packet, decoded_worker, decoded_samples,
                                                worker_error) &&
          worker_error == Vst3WorkerProtocolErrorV1::NonFiniteSample);
    const Vst3WorkerParameterPointV1 worker_parameters[] = {
        {7U, 0, 0.25}, {7U, 1, 0.75}, {9U, 0, 0.5}};
    const auto parameter_payload_bytes = kVst3WorkerParameterPrefixBytesV1 +
        3U * kVst3WorkerParameterPointBytesV1 + sizeof(worker_samples);
    std::vector<std::uint8_t> parameter_packet(kVst3WorkerHeaderBytesV1 +
                                                parameter_payload_bytes);
    const Vst3WorkerFrameV1 parameter_frame{
        Vst3WorkerMessageTypeV1::ProcessBlockWithParameters, 18U, 2U, 2U,
        static_cast<std::uint32_t>(parameter_payload_bytes), 0U};
    std::size_t parameter_bytes_written = 0U;
    CHECK(encode_vst3_worker_parameter_frame_v1(
        parameter_frame, worker_parameters, std::span<const float>(worker_samples),
        std::span<std::uint8_t>(parameter_packet), parameter_bytes_written) &&
          parameter_bytes_written == parameter_packet.size());
    std::array<Vst3WorkerParameterPointV1, kVst3WorkerMaxParameterPointsV1>
        decoded_parameters{};
    std::size_t decoded_parameter_count = 0U;
    std::span<const float> decoded_parameter_samples;
    CHECK(validate_vst3_worker_parameter_frame_v1(
        parameter_packet, decoded_worker, std::span<Vst3WorkerParameterPointV1>(decoded_parameters),
        decoded_parameter_count, decoded_parameter_samples, worker_error) &&
          decoded_parameter_count == 3U && decoded_parameters[1].parameter_id == 7U &&
          decoded_parameters[1].sample_offset == 1 &&
          std::abs(decoded_parameters[1].normalized_value - 0.75) < 1e-12 &&
          decoded_parameter_samples.size() == 4U);
    parameter_packet[kVst3WorkerHeaderBytesV1 + 4U] = 1U;
    CHECK(!validate_vst3_worker_parameter_frame_v1(
        parameter_packet, decoded_worker, std::span<Vst3WorkerParameterPointV1>(decoded_parameters),
        decoded_parameter_count, decoded_parameter_samples, worker_error) &&
          worker_error == Vst3WorkerProtocolErrorV1::InvalidFormat);

    // Issue 284: bounded offline fixtures for the v1 worker frame codec. Each case
    // is deterministic, in-memory, and exercises one fail-closed boundary without
    // launching a process or touching the filesystem.
    {
        std::array<std::uint8_t, kVst3WorkerHeaderBytesV1 + 2U * sizeof(float)>
            selftest_packet{};
        const Vst3WorkerFrameV1 selftest_frame{
            Vst3WorkerMessageTypeV1::ProcessBlockResponse, 284U, 2U, 1U,
            2U * sizeof(float), 0U};
        std::size_t selftest_header_bytes = 0U;

        // 1) Valid little-endian round-trip: encode → decode → validate finite payload.
        CHECK(encode_vst3_worker_frame_v1(
            selftest_frame,
            std::span<std::uint8_t>(selftest_packet).first(kVst3WorkerHeaderBytesV1),
            selftest_header_bytes));
        const float selftest_samples[2] = {0.5F, -0.5F};
        std::memcpy(selftest_packet.data() + kVst3WorkerHeaderBytesV1, selftest_samples,
                    sizeof(selftest_samples));
        Vst3WorkerFrameV1 selftest_decoded{};
        Vst3WorkerProtocolErrorV1 selftest_error{Vst3WorkerProtocolErrorV1::None};
        std::span<const float> selftest_payload;
        CHECK(validate_vst3_worker_audio_frame_v1(selftest_packet, selftest_decoded,
                                                   selftest_payload, selftest_error));
        CHECK(selftest_decoded.request_id == 284U && selftest_payload.size() == 2U);
        // Explicit little-endian byte order on the wire.
        CHECK(selftest_packet[0] == 0x48U && selftest_packet[1] == 0x49U &&
              selftest_packet[2] == 0x56U && selftest_packet[3] == 0x53U);

        // 2) Truncated header: below the fixed 36-byte minimum must fail closed.
        Vst3WorkerFrameV1 truncated_frame{};
        Vst3WorkerProtocolErrorV1 truncated_error{Vst3WorkerProtocolErrorV1::None};
        CHECK(!decode_vst3_worker_frame_v1(
            std::span<const std::uint8_t>(selftest_packet).first(kVst3WorkerHeaderBytesV1 - 1U),
            truncated_frame, truncated_error));
        CHECK(truncated_error == Vst3WorkerProtocolErrorV1::Truncated);

        // 3) Non-finite sample: NaN in an otherwise valid audio frame is rejected.
        float selftest_nan = std::numeric_limits<float>::quiet_NaN();
        std::memcpy(selftest_packet.data() + kVst3WorkerHeaderBytesV1, &selftest_nan,
                    sizeof(selftest_nan));
        Vst3WorkerProtocolErrorV1 nan_error{Vst3WorkerProtocolErrorV1::None};
        CHECK(!validate_vst3_worker_audio_frame_v1(selftest_packet, selftest_decoded,
                                                    selftest_payload, nan_error));
        CHECK(nan_error == Vst3WorkerProtocolErrorV1::NonFiniteSample);
        std::memcpy(selftest_packet.data() + kVst3WorkerHeaderBytesV1, selftest_samples,
                    sizeof(selftest_samples));

        // 4) Wrong-endian rejection: big-endian byte order of the same magic fails closed.
        std::array<std::uint8_t, kVst3WorkerHeaderBytesV1 + 2U * sizeof(float)>
            wrong_endian_packet{};
        std::memcpy(wrong_endian_packet.data(), selftest_packet.data(), wrong_endian_packet.size());
        std::swap(wrong_endian_packet[0], wrong_endian_packet[3]);
        std::swap(wrong_endian_packet[1], wrong_endian_packet[2]);
        Vst3WorkerFrameV1 endian_frame{};
        Vst3WorkerProtocolErrorV1 endian_error{Vst3WorkerProtocolErrorV1::None};
        CHECK(!decode_vst3_worker_frame_v1(wrong_endian_packet, endian_frame, endian_error));
        CHECK(endian_error == Vst3WorkerProtocolErrorV1::InvalidMagic);
    }
    // Issue 284 regression guard: the pre-existing parameter-frame boundary must
    // still fail closed on a reserved-byte violation after the new fixtures.
    CHECK(!validate_vst3_worker_parameter_frame_v1(
        parameter_packet, decoded_worker, std::span<Vst3WorkerParameterPointV1>(decoded_parameters),
        decoded_parameter_count, decoded_parameter_samples, worker_error) &&
          worker_error == Vst3WorkerProtocolErrorV1::InvalidFormat);
    Vst3ParameterTimelineV1 parameter_timeline;
    CHECK(parameter_timeline.append(Vst3ParameterTimelineEventV1{7U, 48003U, 0.75}) &&
          parameter_timeline.append(Vst3ParameterTimelineEventV1{3U, 48000U, 0.25}) &&
          parameter_timeline.append(Vst3ParameterTimelineEventV1{7U, 48000U, 0.5}) &&
          validate_vst3_parameter_timeline_v1(parameter_timeline.snapshot()) &&
          parameter_timeline.snapshot().events[0].parameter_id == 3U &&
          parameter_timeline.snapshot().events[1].parameter_id == 7U);
    std::array<Vst3WorkerParameterPointV1, kVst3WorkerMaxParameterPointsV1> timeline_points{};
    std::size_t timeline_point_count = 0U;
    CHECK(parameter_timeline.collect_block(48000U, 8U, timeline_points, timeline_point_count) &&
          timeline_point_count == 3U && timeline_points[0].sample_offset == 0 &&
          timeline_points[0].parameter_id == 3U && timeline_points[1].parameter_id == 7U &&
          timeline_points[2].sample_offset == 3);
    CHECK(parameter_timeline.erase(1U) && parameter_timeline.snapshot().event_count == 2U &&
          parameter_timeline.collect_block(48000U, 2U, timeline_points, timeline_point_count) &&
          timeline_point_count == 1U);
    CHECK(!parameter_timeline.append(Vst3ParameterTimelineEventV1{99U, 0U, 1.1}) &&
          !parameter_timeline.collect_block(0U, 0U, timeline_points, timeline_point_count));
    CHECK(!validate_vst3_worker_lane_config_v1(Vst3WorkerLaneConfigV1{0U, 2U, 48000.0, 0U, 128U}));
    CHECK(validate_vst3_worker_lane_config_v1(Vst3WorkerLaneConfigV1{99U, 2U, 48000.0, 64U, 128U}));
    Vst3WorkerLaneSessionV1 worker_lane;
    CHECK(worker_lane.prepare(sandbox, Vst3WorkerLaneConfigV1{99U, 2U, 48000.0, 64U, 128U}) &&
          worker_lane.state() == Vst3WorkerLaneStateV1::Prepared &&
          worker_lane.append_parameter_event(Vst3ParameterTimelineEventV1{7U, 4U, 0.5}) &&
          worker_lane.parameter_timeline().event_count == 1U &&
          worker_lane.latency_lane_input().lane_token == 99U &&
          !worker_lane.latency_lane_input().active &&
          worker_lane.latency_lane_input().reported_latency_samples == 0U);
    CHECK(worker_lane.handshake() == Vst3WorkerExchangeResultV1::not_running &&
          worker_lane.state() == Vst3WorkerLaneStateV1::Degraded);
    CHECK(!worker_lane.append_parameter_event(Vst3ParameterTimelineEventV1{8U, 8U, 0.25}));
    worker_lane.detach();
    CHECK(worker_lane.state() == Vst3WorkerLaneStateV1::Detached);
    CHECK(worker_lane.prepare(sandbox, Vst3WorkerLaneConfigV1{99U, 2U, 48000.0, 64U, 128U}));
    auto automation_scheduler = std::make_unique<Vst3SceneAutomationSchedulerV1>();
    const std::array<Vst3WorkerLaneSessionV1*, 1> automation_lanes{{&worker_lane}};
    Vst3ParameterTimelineSnapshotV1 automation_timeline{};
    automation_timeline.event_count = 1U;
    automation_timeline.events[0] = Vst3ParameterTimelineEventV1{7U, 4U, 0.5};
    CHECK(automation_scheduler->prepare(automation_lanes) &&
          automation_scheduler->upsert_timeline("djmax-default", automation_timeline) &&
          automation_scheduler->bind_scene("DJMAX", 99U, "djmax-default") &&
          automation_scheduler->activate_scene("DJMAX") == Vst3SceneAutomationResultV1::ok &&
          automation_scheduler->active_scene() == "DJMAX" &&
          automation_scheduler->timeline_count() == 1U &&
          automation_scheduler->binding_count() == 1U &&
          worker_lane.parameter_timeline().event_count == 1U);
    const std::array<float, 4> automation_input{0.1F, -0.1F, 0.2F, -0.2F};
    std::array<float, 4> automation_output{};
    CHECK(automation_scheduler->process_lane_block(
              "DJMAX", 99U, 2U, 0U, 2U, automation_input, automation_output) ==
          Vst3SceneAutomationResultV1::lane_not_ready);
    CHECK(automation_scheduler->activate_scene("missing") ==
          Vst3SceneAutomationResultV1::not_bound &&
          !automation_scheduler->remove_timeline("djmax-default"));
    automation_scheduler->clear();
    CHECK(automation_scheduler->active_scene().empty() && automation_scheduler->lane_count() == 0U);
    Vst3ParameterTimelineV1 too_many_parameters;
    bool timeline_parameter_capacity = true;
    for (std::uint32_t parameter_id = 0U; parameter_id < 17U; ++parameter_id) {
        timeline_parameter_capacity =
            too_many_parameters.append(Vst3ParameterTimelineEventV1{parameter_id, parameter_id,
                                                                     0.5}) &&
            timeline_parameter_capacity;
    }
    CHECK(!timeline_parameter_capacity && too_many_parameters.snapshot().event_count == 16U);
    Vst3TimelineEditorV1 timeline_editor;
    CHECK(timeline_editor.published().event_count == 0U &&
          !timeline_editor.has_edit_session() && timeline_editor.draft() == nullptr);
    CHECK(!timeline_editor.upsert(Vst3ParameterTimelineEventV1{1U, 0U, 0.5}) &&
          !timeline_editor.remove_at(0U) &&
          !timeline_editor.set_value_at(0U, 0.5));
    CHECK(!timeline_editor.commit() && !timeline_editor.discard());
    Vst3ParameterTimelineSnapshotV1 corrupt_editor_base{};
    corrupt_editor_base.schema_version = 2U;
    CHECK(!timeline_editor.reset(corrupt_editor_base) &&
          timeline_editor.published().event_count == 0U);
    CHECK(timeline_editor.begin_edit() &&
          timeline_editor.upsert(Vst3ParameterTimelineEventV1{7U, 48003U, 0.75}) &&
          timeline_editor.upsert(Vst3ParameterTimelineEventV1{3U, 48000U, 0.25}) &&
          timeline_editor.draft() != nullptr &&
          timeline_editor.draft()->event_count == 2U &&
          timeline_editor.published().event_count == 0U);
    CHECK(!timeline_editor.begin_edit());
    CHECK(timeline_editor.upsert(Vst3ParameterTimelineEventV1{7U, 48000U, 0.5}) &&
          timeline_editor.draft()->event_count == 3U &&
          timeline_editor.draft()->events[1].parameter_id == 7U);
    CHECK(timeline_editor.upsert(Vst3ParameterTimelineEventV1{7U, 48000U, 0.625}) &&
          timeline_editor.draft()->event_count == 3U &&
          timeline_editor.draft()->events[1].normalized_value == 0.625);
    CHECK(!timeline_editor.upsert(Vst3ParameterTimelineEventV1{9U, 10U, 1.1}) &&
          !timeline_editor.upsert(Vst3ParameterTimelineEventV1{9U, 10U, -0.25}) &&
          !timeline_editor.upsert(Vst3ParameterTimelineEventV1{9U, 10U, std::nan("")}) &&
          timeline_editor.draft()->event_count == 3U);
    CHECK(timeline_editor.set_value_at(1U, 0.125) &&
          timeline_editor.draft()->events[1].normalized_value == 0.125 &&
          !timeline_editor.set_value_at(3U, 0.5) &&
          !timeline_editor.set_value_at(0U, -1.0) &&
          !timeline_editor.set_value_at(0U, std::nan("")) &&
          timeline_editor.draft()->events[0].normalized_value == 0.25);
    CHECK(!timeline_editor.remove_at(3U));
    CHECK(timeline_editor.commit() && !timeline_editor.has_edit_session() &&
          timeline_editor.draft() == nullptr &&
          timeline_editor.published().event_count == 3U &&
          validate_vst3_parameter_timeline_v1(timeline_editor.published()) &&
          timeline_editor.published().events[0].parameter_id == 3U &&
          timeline_editor.published().events[0].sample_position == 48000U &&
          timeline_editor.published().events[1].normalized_value == 0.125 &&
          timeline_editor.published().events[2].sample_position == 48003U);
    CHECK(timeline_editor.begin_edit() &&
          timeline_editor.remove_at(0U) &&
          timeline_editor.draft()->event_count == 2U &&
          timeline_editor.draft()->events[0].sample_position == 48000U &&
          timeline_editor.discard() &&
          !timeline_editor.has_edit_session() &&
          timeline_editor.published().event_count == 3U);
    Vst3TimelineEditorV1 adopted_editor;
    CHECK(adopted_editor.reset(timeline_editor.published()) &&
          adopted_editor.begin_edit() &&
          adopted_editor.set_value_at(2U, 0.5) &&
          adopted_editor.commit() &&
          adopted_editor.published().events[2].normalized_value == 0.5 &&
          timeline_editor.published().events[2].normalized_value == 0.75);
    CHECK(adopted_editor.begin_edit());
    Vst3ParameterTimelineSnapshotV1 empty_editor_base{};
    CHECK(!adopted_editor.reset(empty_editor_base) && adopted_editor.has_edit_session());
    CHECK(adopted_editor.discard() &&
          adopted_editor.reset(empty_editor_base) &&
          adopted_editor.published().event_count == 0U && !adopted_editor.has_edit_session());
    Vst3TimelineEditorV1 capacity_editor;
    CHECK(capacity_editor.begin_edit());
    bool editor_capacity_ok = true;
    for (std::uint32_t editor_index = 0U; editor_index <= kVst3TimelineMaxEventsV1;
         ++editor_index) {
        editor_capacity_ok =
            capacity_editor.upsert(Vst3ParameterTimelineEventV1{
                editor_index % 8U, static_cast<std::uint64_t>(editor_index) * 4U, 0.5}) &&
            editor_capacity_ok;
    }
    CHECK(!editor_capacity_ok &&
          capacity_editor.draft()->event_count == kVst3TimelineMaxEventsV1);
    Vst3TimelineEditorV1 parameter_cap_editor;
    CHECK(parameter_cap_editor.begin_edit());
    bool editor_parameters_ok = true;
    for (std::uint32_t parameter_id = 0U; parameter_id < 17U; ++parameter_id) {
        editor_parameters_ok =
            parameter_cap_editor.upsert(
                Vst3ParameterTimelineEventV1{parameter_id, parameter_id * 2U, 0.25}) &&
            editor_parameters_ok;
    }
    CHECK(!editor_parameters_ok && parameter_cap_editor.draft()->event_count == 16U);
    Vst3TimelineEditorV1 history_editor;
    CHECK(!history_editor.can_undo() && !history_editor.can_redo());
    CHECK(!history_editor.undo() && !history_editor.redo());
    Vst3ParameterTimelineSnapshotV1 hist_base{};
    hist_base.event_count = 1U;
    hist_base.events[0] = Vst3ParameterTimelineEventV1{1U, 10U, 0.1};
    CHECK(history_editor.reset(hist_base));
    CHECK(history_editor.begin_edit() &&
          history_editor.upsert(Vst3ParameterTimelineEventV1{2U, 20U, 0.2}) &&
          history_editor.commit());
    CHECK(history_editor.can_undo() && !history_editor.can_redo() &&
          history_editor.undo_depth() == 1U &&
          history_editor.published().event_count == 2U);
    CHECK(history_editor.undo() &&
          history_editor.published().event_count == 1U &&
          history_editor.can_redo() && history_editor.redo_depth() == 1U);
    CHECK(history_editor.redo() &&
          history_editor.published().event_count == 2U &&
          !history_editor.can_redo() && history_editor.redo_depth() == 0U);
    CHECK(history_editor.begin_edit() &&
          history_editor.upsert(Vst3ParameterTimelineEventV1{3U, 30U, 0.3}) &&
          history_editor.commit() &&
          history_editor.published().event_count == 3U &&
          history_editor.undo_depth() == 2U);
    CHECK(history_editor.undo() &&
          history_editor.published().event_count == 2U);
    CHECK(history_editor.begin_edit() &&
          history_editor.upsert(Vst3ParameterTimelineEventV1{4U, 40U, 0.4}) &&
          history_editor.commit() && !history_editor.can_redo() &&
          history_editor.published().event_count == 3U &&
          history_editor.published().events[2].parameter_id == 4U &&
          history_editor.published().events[2].normalized_value == 0.4);
    CHECK(history_editor.begin_edit() &&
          history_editor.upsert(Vst3ParameterTimelineEventV1{5U, 50U, 0.5}));
    CHECK(!history_editor.undo() && !history_editor.redo() &&
          history_editor.has_edit_session() &&
          history_editor.draft()->event_count == 4U);
    CHECK(history_editor.discard() && history_editor.can_undo() &&
          !history_editor.has_edit_session());
    Vst3ParameterTimelineSnapshotV1 fresh_hist_base{};
    CHECK(history_editor.reset(fresh_hist_base) &&
          !history_editor.can_undo() && !history_editor.can_redo() &&
          history_editor.undo_depth() == 0U && history_editor.redo_depth() == 0U);
    Vst3TimelineEditorV1 capacity_history;
    CHECK(capacity_history.reset(Vst3ParameterTimelineSnapshotV1{}));
    bool history_capacity_ok = true;
    for (std::uint32_t step = 1U; step <= 12U; ++step) {
        history_capacity_ok =
            capacity_history.begin_edit() &&
            capacity_history.upsert(Vst3ParameterTimelineEventV1{
                7U, static_cast<std::uint64_t>(step) * 100U, 0.5}) &&
            capacity_history.commit() && history_capacity_ok;
    }
    CHECK(history_capacity_ok &&
          capacity_history.undo_depth() == kVst3TimelineEditorMaxHistoryV1 &&
          capacity_history.can_undo() && !capacity_history.can_redo());
    for (std::size_t drained = 0U; drained < kVst3TimelineEditorMaxHistoryV1;
         ++drained) {
        CHECK(capacity_history.undo());
    }
    CHECK(capacity_history.can_redo() &&
          capacity_history.redo_depth() == kVst3TimelineEditorMaxHistoryV1 &&
          capacity_history.published().event_count == 4U &&
          capacity_history.published().events[3].sample_position == 400U);
    CHECK(automation_scheduler->prepare(automation_lanes) &&
          automation_scheduler->upsert_timeline("djmax-default", automation_timeline) &&
          automation_scheduler->bind_scene("DJMAX", 99U, "djmax-default") &&
          automation_scheduler->timeline_snapshot("djmax-default") != nullptr &&
          automation_scheduler->timeline_snapshot("djmax-default")->event_count == 1U &&
          automation_scheduler->timeline_snapshot("missing") == nullptr);
    CHECK(!automation_scheduler->begin_timeline_edit("") &&
          !automation_scheduler->begin_timeline_edit("nope"));
    CHECK(automation_scheduler->begin_timeline_edit("djmax-default") &&
          !automation_scheduler->begin_timeline_edit("djmax-default"));
    auto* scheduler_editor = automation_scheduler->editing_timeline();
    CHECK(scheduler_editor != nullptr && scheduler_editor->has_edit_session() &&
          scheduler_editor->draft() != nullptr &&
          scheduler_editor->draft()->event_count == 1U);
    CHECK(scheduler_editor->upsert(Vst3ParameterTimelineEventV1{7U, 6U, 0.875}) &&
          scheduler_editor->draft()->event_count == 2U);
    CHECK(!automation_scheduler->remove_timeline("djmax-default"));
    CHECK(automation_scheduler->cancel_timeline_edit() &&
          automation_scheduler->timeline_snapshot("djmax-default")->event_count == 1U);
    CHECK(automation_scheduler->begin_timeline_edit("djmax-default"));
    scheduler_editor = automation_scheduler->editing_timeline();
    CHECK(scheduler_editor->upsert(Vst3ParameterTimelineEventV1{7U, 6U, 0.875}) &&
          scheduler_editor->draft()->event_count == 2U);
    CHECK(automation_scheduler->commit_timeline_edit());
    CHECK(!automation_scheduler->commit_timeline_edit());
    const auto* committed_timeline = automation_scheduler->timeline_snapshot("djmax-default");
    CHECK(committed_timeline != nullptr && committed_timeline->event_count == 2U &&
          committed_timeline->events[1].sample_position == 6U &&
          committed_timeline->events[1].normalized_value == 0.875);
    CHECK(automation_scheduler->activate_scene("DJMAX") ==
              Vst3SceneAutomationResultV1::ok &&
          worker_lane.parameter_timeline().event_count == 2U);
    CHECK(automation_scheduler->begin_timeline_edit("djmax-default"));
    scheduler_editor = automation_scheduler->editing_timeline();
    CHECK(scheduler_editor->remove_at(0U) &&
          scheduler_editor->draft()->event_count == 1U);
    CHECK(automation_scheduler->cancel_timeline_edit());
    CHECK(!automation_scheduler->cancel_timeline_edit() &&
          automation_scheduler->editing_timeline() == nullptr &&
          automation_scheduler->timeline_snapshot("djmax-default")->event_count == 2U);
    CHECK(automation_scheduler->begin_timeline_edit("djmax-default"));
    automation_scheduler->clear();
    CHECK(automation_scheduler->editing_timeline() == nullptr &&
          automation_scheduler->timeline_count() == 0U &&
          automation_scheduler->active_scene().empty());
    Vst3ParameterTimelineSnapshotV1 persist_source{};
    persist_source.event_count = 2U;
    persist_source.events[0] = Vst3ParameterTimelineEventV1{3U, 48000U, 0.25};
    persist_source.events[1] = Vst3ParameterTimelineEventV1{7U, 48003U, 0.75};
    std::string persisted_document;
    CHECK(serialize_vst3_parameter_timeline_v1(persist_source, persisted_document) &&
          persisted_document.size() > 0U &&
          persisted_document.size() <= kVst3TimelineMaxSerializedBytesV1);
    Vst3ParameterTimelineSnapshotV1 persist_roundtrip{};
    CHECK(parse_vst3_parameter_timeline_v1(persisted_document, persist_roundtrip) ==
              Vst3TimelineParseErrorV1::none &&
          persist_roundtrip.event_count == 2U &&
          persist_roundtrip.events[0].parameter_id == 3U &&
          persist_roundtrip.events[0].sample_position == 48000U &&
          std::abs(persist_roundtrip.events[0].normalized_value - 0.25) < 1e-15 &&
          persist_roundtrip.events[1].parameter_id == 7U &&
          persist_roundtrip.events[1].normalized_value == 0.75);
    std::string persisted_again;
    CHECK(serialize_vst3_parameter_timeline_v1(persist_roundtrip, persisted_again) &&
          persisted_again == persisted_document);
    Vst3ParameterTimelineSnapshotV1 persist_spaced{};
    CHECK(parse_vst3_parameter_timeline_v1(
              "\n\t " + persisted_document + " \r\n", persist_spaced) ==
              Vst3TimelineParseErrorV1::none && persist_spaced.event_count == 2U);
    Vst3ParameterTimelineSnapshotV1 persist_empty{};
    std::string empty_document;
    CHECK(serialize_vst3_parameter_timeline_v1(persist_empty, empty_document));
    Vst3ParameterTimelineSnapshotV1 empty_roundtrip{};
    CHECK(parse_vst3_parameter_timeline_v1(empty_document, empty_roundtrip) ==
              Vst3TimelineParseErrorV1::none && empty_roundtrip.event_count == 0U);
    Vst3ParameterTimelineSnapshotV1 unsorted_source{};
    unsorted_source.event_count = 2U;
    unsorted_source.events[0] = Vst3ParameterTimelineEventV1{7U, 48003U, 0.75};
    unsorted_source.events[1] = Vst3ParameterTimelineEventV1{3U, 48000U, 0.25};
    std::string rejected_document;
    CHECK(!serialize_vst3_parameter_timeline_v1(unsorted_source, rejected_document) &&
          rejected_document.empty());
    auto parse_expectation = [](std::string_view text,
                                Vst3TimelineParseErrorV1 expected) {
        Vst3ParameterTimelineSnapshotV1 sink{};
        sink.event_count = 1U;
        sink.events[0] = Vst3ParameterTimelineEventV1{9U, 9U, 0.9};
        const auto result = parse_vst3_parameter_timeline_v1(text, sink);
        const bool untouched =
            sink.event_count == 1U && sink.events[0].parameter_id == 9U &&
            sink.events[0].normalized_value == 0.9;
        return result == expected && untouched;
    };
    CHECK(parse_expectation("", Vst3TimelineParseErrorV1::unexpected_token));
    CHECK(parse_expectation(std::string(kVst3TimelineMaxSerializedBytesV1 + 1U, ' '),
                            Vst3TimelineParseErrorV1::too_large));
    CHECK(parse_expectation(persisted_document.substr(0U, 30U),
                            Vst3TimelineParseErrorV1::truncated));
    {
        std::string wrong_version = persisted_document;
        wrong_version.replace(wrong_version.find("\"schema_version\": 1"), 19,
                              "\"schema_version\": 2");
        CHECK(parse_expectation(wrong_version,
                                Vst3TimelineParseErrorV1::unsupported_version));
    }
    {
        std::string unknown_key = persisted_document;
        unknown_key.replace(unknown_key.find("\"event_count\""), 13,
                            "\"event_counts\"");
        CHECK(parse_expectation(unknown_key, Vst3TimelineParseErrorV1::unknown_key));
    }
    {
        std::string duplicate_key = persisted_document;
        const auto count_anchor = duplicate_key.find("\"event_count\": ") + 15U;
        duplicate_key.insert(duplicate_key.find(',', count_anchor),
                             ", \"event_count\": 2");
        CHECK(parse_expectation(duplicate_key,
                                Vst3TimelineParseErrorV1::duplicate_key));
    }
    {
        std::string missing_key = persisted_document;
        const auto start = missing_key.find("\"event_count\": ");
        const auto end = missing_key.find('\n', start);
        missing_key.erase(start, end - start + 1U);
        CHECK(parse_expectation(missing_key, Vst3TimelineParseErrorV1::missing_key));
    }
    {
        std::string bad_number = persisted_document;
        bad_number.replace(bad_number.find("\"normalized_value\": 0.25"), 23,
                           "\"normalized_value\": .25");
        CHECK(parse_expectation(bad_number, Vst3TimelineParseErrorV1::invalid_number));
    }
    {
        std::string nan_literal = persisted_document;
        nan_literal.replace(nan_literal.find("\"normalized_value\": 0.25"), 23,
                            "\"normalized_value\": NaN");
        CHECK(parse_expectation(nan_literal, Vst3TimelineParseErrorV1::invalid_number));
    }
    {
        std::string out_of_range = persisted_document;
        out_of_range.replace(out_of_range.find("\"normalized_value\": 0.25"), 23,
                             "\"normalized_value\": 1.25");
        CHECK(parse_expectation(out_of_range,
                                Vst3TimelineParseErrorV1::value_out_of_range));
    }
    {
        std::string id_overflow = persisted_document;
        id_overflow.replace(id_overflow.find("\"parameter_id\": 3,"), 18,
                            "\"parameter_id\": 4294967296,");
        CHECK(parse_expectation(id_overflow,
                                Vst3TimelineParseErrorV1::value_out_of_range));
    }
    {
        std::string position_overflow = persisted_document;
        position_overflow.replace(position_overflow.find("\"sample_position\": 48000"),
                                  24, "\"sample_position\": 18446744073709551616");
        CHECK(parse_expectation(position_overflow,
                                Vst3TimelineParseErrorV1::value_out_of_range));
    }
    {
        std::string short_array = persisted_document;
        const auto first_event = short_array.find("{\"parameter_id\": 3,");
        CHECK(first_event != std::string::npos);
        const auto comma_after = short_array.find(",\n", first_event);
        CHECK(comma_after != std::string::npos);
        short_array.erase(first_event, comma_after - first_event + 2U);
        CHECK(parse_expectation(short_array,
                                Vst3TimelineParseErrorV1::unexpected_token));
    }
    {
        const std::string padded_slot =
            "{\"parameter_id\": 0, \"sample_position\": 0, \"normalized_value\": 0}";
        std::string hidden_slot = persisted_document;
        const auto pad_position = hidden_slot.find(padded_slot);
        CHECK(pad_position != std::string::npos);
        hidden_slot.replace(pad_position, padded_slot.size(),
                            "{\"parameter_id\": 1, \"sample_position\": 0, "
                            "\"normalized_value\": 0}");
        CHECK(parse_expectation(hidden_slot,
                                Vst3TimelineParseErrorV1::event_count_mismatch));
    }
    CHECK(parse_expectation(persisted_document + "x",
                            Vst3TimelineParseErrorV1::truncated));
#if defined(_WIN32)
    {
        wchar_t store_temp_root[MAX_PATH];
        const DWORD store_temp_length = GetTempPathW(MAX_PATH, store_temp_root);
        CHECK(store_temp_length > 0U && store_temp_length < MAX_PATH);
        const std::filesystem::path store_root =
            std::filesystem::path(std::wstring(store_temp_root)) /
            (L"hibiki_timeline_store_" + std::to_wstring(GetTickCount()));
        Vst3TimelineFileStoreV1 file_store;
        Vst3ParameterTimelineSnapshotV1 store_sink{};
        store_sink.event_count = 1U;
        store_sink.events[0] = Vst3ParameterTimelineEventV1{9U, 9U, 0.9};
        CHECK(file_store.save("alpha", persist_source) ==
              Vst3TimelineStoreStatusV1::invalid_argument);
        CHECK(file_store.load("alpha", store_sink) ==
              Vst3TimelineStoreStatusV1::invalid_argument);
        CHECK(file_store.remove("alpha") ==
              Vst3TimelineStoreStatusV1::invalid_argument);
        CHECK(file_store.open(store_root.wstring()) && file_store.is_open());
        CHECK(file_store.save("", persist_source) ==
              Vst3TimelineStoreStatusV1::id_rejected);
        const std::string rejected_store_ids[] = {
            "..", "../up", "sub/dir", "back\\slash", ".hidden",
            "-leading", "_underscore", std::string(65U, 'a'),
            "\xe6\x99\x82\xe9\x96\x93", "con", "COM1",
        };
        bool store_ids_rejected = true;
        for (const auto& rejected : rejected_store_ids) {
            store_ids_rejected =
                store_ids_rejected &&
                file_store.save(rejected, persist_source) ==
                    Vst3TimelineStoreStatusV1::id_rejected;
        }
        CHECK(store_ids_rejected);
        std::array<std::string, kVst3TimelineStoreMaxEntriesV1> store_listing{};
        std::size_t store_listing_count = 0U;
        CHECK(file_store.list_ids(store_listing, store_listing_count) ==
                  Vst3TimelineStoreStatusV1::ok &&
              store_listing_count == 0U);
        CHECK(file_store.save("alpha", persist_source) ==
                  Vst3TimelineStoreStatusV1::ok &&
              file_store.save("beta-01", persist_empty) ==
                  Vst3TimelineStoreStatusV1::ok);
        Vst3ParameterTimelineSnapshotV1 loaded_alpha{};
        Vst3ParameterTimelineSnapshotV1 loaded_beta{};
        CHECK(file_store.load("alpha", loaded_alpha) ==
                  Vst3TimelineStoreStatusV1::ok &&
              loaded_alpha.event_count == 2U &&
              loaded_alpha.events[1].parameter_id == 7U);
        CHECK(file_store.load("beta-01", loaded_beta) ==
                  Vst3TimelineStoreStatusV1::ok &&
              loaded_beta.event_count == 0U);
        CHECK(file_store.list_ids(store_listing, store_listing_count) ==
                  Vst3TimelineStoreStatusV1::ok &&
              store_listing_count == 2U &&
              store_listing[0] == "alpha" && store_listing[1] == "beta-01");
        CHECK(file_store.load("missing", loaded_alpha) ==
              Vst3TimelineStoreStatusV1::not_found);
        CHECK(file_store.remove("gamma") == Vst3TimelineStoreStatusV1::not_found);
        {
            std::ofstream corrupt_stream(store_root / L"alpha.json",
                                         std::ios::binary | std::ios::trunc);
            corrupt_stream << "{ not a timeline";
            corrupt_stream.close();
        }
        CHECK(file_store.load("alpha", store_sink) ==
                  Vst3TimelineStoreStatusV1::parse_error &&
              store_sink.event_count == 1U &&
              store_sink.events[0].parameter_id == 9U);
        CHECK(file_store.save("alpha", persist_source) ==
              Vst3TimelineStoreStatusV1::ok);
        bool store_capacity_filled = true;
        for (std::uint32_t filler = 0U; filler < 14U; ++filler) {
            const std::string filler_id =
                "s" + std::string(filler < 10U ? 1U : 0U, '0') +
                std::to_string(filler);
            store_capacity_filled =
                store_capacity_filled &&
                file_store.save(filler_id, persist_empty) ==
                    Vst3TimelineStoreStatusV1::ok;
        }
        CHECK(store_capacity_filled);
        CHECK(file_store.save("overflow", persist_empty) ==
              Vst3TimelineStoreStatusV1::capacity_exhausted);
        CHECK(file_store.save("alpha", persist_source) ==
              Vst3TimelineStoreStatusV1::ok);
        CHECK(file_store.remove("beta-01") == Vst3TimelineStoreStatusV1::ok &&
              file_store.remove("beta-01") == Vst3TimelineStoreStatusV1::not_found);
        CHECK(file_store.list_ids(store_listing, store_listing_count) ==
                  Vst3TimelineStoreStatusV1::ok &&
              store_listing_count == kVst3TimelineStoreMaxEntriesV1 - 1U &&
              store_listing[0] == "alpha");
        file_store.close();
        CHECK(!file_store.is_open() &&
              file_store.load("alpha", loaded_alpha) ==
                  Vst3TimelineStoreStatusV1::invalid_argument);
        Vst3TimelineFileStoreV1 reopened_store;
        CHECK(reopened_store.open(store_root.wstring()) &&
              reopened_store.load("alpha", loaded_alpha) ==
                  Vst3TimelineStoreStatusV1::ok &&
              loaded_alpha.events[0].normalized_value == 0.25);
        std::error_code store_cleanup_error;
        std::filesystem::remove_all(store_root, store_cleanup_error);
    }
#endif
#if defined(_WIN32)
    {
        wchar_t sync_temp_root[MAX_PATH];
        const DWORD sync_temp_length = GetTempPathW(MAX_PATH, sync_temp_root);
        CHECK(sync_temp_length > 0U && sync_temp_length < MAX_PATH);
        const std::filesystem::path sync_root =
            std::filesystem::path(std::wstring(sync_temp_root)) /
            (L"hibiki_timeline_sync_" + std::to_wstring(GetTickCount()));
        Vst3TimelineFileStoreV1 sync_store;
        Vst3TimelineFileStoreV1 closed_sync_store;
        auto sync_scheduler = std::make_unique<Vst3SceneAutomationSchedulerV1>();
        Vst3TimelineStoreSyncResultV1 sync_result{};
        CHECK(sync_timeline_store_to_scheduler_v1(
                  closed_sync_store, *sync_scheduler, sync_result) ==
                  Vst3TimelineStoreStatusV1::invalid_argument &&
              sync_result.loaded == 0U && sync_result.skipped == 0U);
        CHECK(sync_store.open(sync_root.wstring()));
        CHECK(sync_timeline_store_to_scheduler_v1(
                  sync_store, *sync_scheduler, sync_result) ==
                  Vst3TimelineStoreStatusV1::ok &&
              sync_result.loaded == 0U && sync_result.skipped == 0U &&
              sync_scheduler->timeline_count() == 0U);
        CHECK(sync_store.save("alpha", persist_source) ==
                  Vst3TimelineStoreStatusV1::ok &&
              sync_store.save("beta-01", persist_empty) ==
                  Vst3TimelineStoreStatusV1::ok);
        CHECK(sync_timeline_store_to_scheduler_v1(
                  sync_store, *sync_scheduler, sync_result) ==
                  Vst3TimelineStoreStatusV1::ok &&
              sync_result.loaded == 2U && sync_result.skipped == 0U &&
              sync_scheduler->timeline_count() == 2U &&
              sync_scheduler->timeline_snapshot("alpha") != nullptr &&
              sync_scheduler->timeline_snapshot("alpha")->event_count == 2U);
        {
            std::ofstream corrupt_stream(sync_root / L"alpha.json",
                                         std::ios::binary | std::ios::trunc);
            corrupt_stream << "{ broken";
            corrupt_stream.close();
        }
        auto partial_scheduler = std::make_unique<Vst3SceneAutomationSchedulerV1>();
        CHECK(sync_timeline_store_to_scheduler_v1(
                  sync_store, *partial_scheduler, sync_result) ==
                  Vst3TimelineStoreStatusV1::ok &&
              sync_result.loaded == 1U && sync_result.skipped == 1U &&
              partial_scheduler->timeline_count() == 1U &&
              partial_scheduler->timeline_snapshot("alpha") == nullptr);
        auto full_scheduler = std::make_unique<Vst3SceneAutomationSchedulerV1>();
        bool sync_capacity_filled = true;
        for (std::uint32_t filler = 0U; filler < kVst3TimelineStoreMaxEntriesV1;
             ++filler) {
            Vst3ParameterTimelineSnapshotV1 filler_snapshot{};
            filler_snapshot.event_count = 1U;
            filler_snapshot.events[0] = Vst3ParameterTimelineEventV1{
                filler + 1U, static_cast<std::uint64_t>(filler) * 8U, 0.5};
            sync_capacity_filled =
                sync_capacity_filled &&
                full_scheduler->upsert_timeline(
                    "f" + std::to_string(filler), filler_snapshot);
        }
        CHECK(sync_capacity_filled && full_scheduler->timeline_count() == 16U);
        CHECK(sync_timeline_store_to_scheduler_v1(
                  sync_store, *full_scheduler, sync_result) ==
                  Vst3TimelineStoreStatusV1::ok &&
              sync_result.loaded == 0U && sync_result.skipped == 2U &&
              full_scheduler->timeline_count() == 16U);
        std::error_code sync_cleanup_error;
        std::filesystem::remove_all(sync_root, sync_cleanup_error);
    }
#endif
#if defined(_WIN32)
    {
        wchar_t export_temp_root[MAX_PATH];
        const DWORD export_temp_length = GetTempPathW(MAX_PATH, export_temp_root);
        CHECK(export_temp_length > 0U && export_temp_length < MAX_PATH);
        const std::filesystem::path export_root =
            std::filesystem::path(std::wstring(export_temp_root)) /
            (L"hibiki_timeline_export_" + std::to_wstring(GetTickCount()));
        Vst3TimelineFileStoreV1 export_store;
        auto export_scheduler = std::make_unique<Vst3SceneAutomationSchedulerV1>();
        Vst3SchedulerStoreExportResultV1 export_result{};
        std::array<std::string, kVst3TimelineStoreMaxEntriesV1> stale_listing{};
        std::size_t stale_listing_count = 99U;
        CHECK(sync_scheduler_to_timeline_store_v1(
                  *export_scheduler, export_store, stale_listing,
                  stale_listing_count, export_result) ==
                  Vst3TimelineStoreStatusV1::invalid_argument &&
              export_result.saved == 0U && export_result.skipped == 0U &&
              stale_listing_count == 0U);
        CHECK(export_store.open(export_root.wstring()) && export_store.is_open());
        CHECK(export_store.save("ghost", persist_empty) ==
              Vst3TimelineStoreStatusV1::ok);
        {
            std::ofstream corrupt_stream(export_root / L"alpha.json",
                                         std::ios::binary | std::ios::trunc);
            corrupt_stream << "{ broken";
            corrupt_stream.close();
        }
        CHECK(sync_scheduler_to_timeline_store_v1(
                  *export_scheduler, export_store, stale_listing,
                  stale_listing_count, export_result) ==
                  Vst3TimelineStoreStatusV1::ok &&
              export_result.saved == 0U && export_result.skipped == 0U &&
              stale_listing_count == 2U && stale_listing[0] == "alpha" &&
              stale_listing[1] == "ghost");
        CHECK(export_scheduler->upsert_timeline("alpha", persist_source) &&
              export_scheduler->upsert_timeline("beta-9", persist_empty));
        CHECK(sync_scheduler_to_timeline_store_v1(
                  *export_scheduler, export_store, stale_listing,
                  stale_listing_count, export_result) ==
                  Vst3TimelineStoreStatusV1::ok &&
              export_result.saved == 2U && export_result.skipped == 0U &&
              stale_listing_count == 1U && stale_listing[0] == "ghost");
        Vst3ParameterTimelineSnapshotV1 exported_reload{};
        CHECK(export_store.load("alpha", exported_reload) ==
                  Vst3TimelineStoreStatusV1::ok &&
              exported_reload.event_count == 2U &&
              exported_reload.events[1].parameter_id == 7U);
        CHECK(export_store.save("stale-one", persist_empty) ==
              Vst3TimelineStoreStatusV1::ok);
        std::array<std::string, 0> zero_stale{};
        stale_listing_count = 0U;
        CHECK(sync_scheduler_to_timeline_store_v1(
                  *export_scheduler, export_store, zero_stale,
                  stale_listing_count, export_result) ==
                  Vst3TimelineStoreStatusV1::capacity_exhausted &&
              stale_listing_count == 2U);
        std::error_code export_cleanup_error;
        std::filesystem::remove_all(export_root, export_cleanup_error);
    }
#endif
#if defined(_WIN32)
    {
        wchar_t temp_root[MAX_PATH];
        const DWORD temp_length = GetTempPathW(MAX_PATH, temp_root);
        CHECK(temp_length > 0U && temp_length < MAX_PATH);
        const std::filesystem::path file_path =
            std::filesystem::path(std::wstring(temp_root)) /
            L"hibiki_timeline_contract_v1.json";
        const std::wstring file_text = file_path.wstring();
        CHECK(save_vst3_parameter_timeline_file_v1(persist_source, file_text) ==
              Vst3TimelineFileErrorV1::none);
        Vst3ParameterTimelineSnapshotV1 file_loaded{};
        CHECK(load_vst3_parameter_timeline_file_v1(file_text, file_loaded) ==
                  Vst3TimelineFileErrorV1::none &&
              file_loaded.event_count == 2U &&
              file_loaded.events[1].sample_position == 48003U);
        CHECK(save_vst3_parameter_timeline_file_v1(persist_roundtrip, file_text) ==
                  Vst3TimelineFileErrorV1::none &&
              load_vst3_parameter_timeline_file_v1(file_text, file_loaded) ==
                  Vst3TimelineFileErrorV1::none &&
              file_loaded.events[0].normalized_value == 0.25);
        CHECK(save_vst3_parameter_timeline_file_v1(unsorted_source, file_text) ==
              Vst3TimelineFileErrorV1::serialize_error);
        {
            std::ofstream corrupt_stream(file_path, std::ios::binary | std::ios::trunc);
            corrupt_stream << "{ broken";
            corrupt_stream.close();
        }
        CHECK(load_vst3_parameter_timeline_file_v1(file_text, file_loaded) ==
              Vst3TimelineFileErrorV1::parse_error);
        CHECK(load_vst3_parameter_timeline_file_v1(file_path.wstring() + L".missing",
                                                   file_loaded) ==
              Vst3TimelineFileErrorV1::io_error);
        std::error_code cleanup_error;
        std::filesystem::remove(file_path, cleanup_error);
    }
#endif
#if defined(_WIN32)
    {
        wchar_t surface_temp_root[MAX_PATH];
        const DWORD surface_temp_length = GetTempPathW(MAX_PATH, surface_temp_root);
        CHECK(surface_temp_length > 0U && surface_temp_length < MAX_PATH);
        const std::filesystem::path surface_root =
            std::filesystem::path(std::wstring(surface_temp_root)) /
            (L"hibiki_supervisor_surface_" + std::to_wstring(GetTickCount()));

        Vst3TimelineSupervisorSurfaceV1 detached_surface;
        std::array<std::string, kVst3TimelineStoreMaxEntriesV1> surface_ids{};
        std::size_t surface_id_count = 0U;
        CHECK(!detached_surface.is_attached() && !detached_surface.has_selection());
        CHECK(detached_surface.refresh_ids(surface_ids, surface_id_count) ==
              Vst3TimelineStoreStatusV1::invalid_argument);
        CHECK(!detached_surface.select("alpha") && !detached_surface.begin_edit() &&
              !detached_surface.upsert(Vst3ParameterTimelineEventV1{1U, 0U, 0.5}) &&
              !detached_surface.commit() && !detached_surface.undo() &&
              !detached_surface.redo() &&
              detached_surface.save_selected() ==
                  Vst3TimelineStoreStatusV1::invalid_argument);
        CHECK(detached_surface.last_store_status() ==
              Vst3TimelineStoreStatusV1::ok);

        Vst3TimelineFileStoreV1 surface_store;
        CHECK(surface_store.open(surface_root.wstring()) && surface_store.is_open());
        CHECK(surface_store.save("alpha", persist_source) ==
                  Vst3TimelineStoreStatusV1::ok &&
              surface_store.save("beta", persist_empty) ==
                  Vst3TimelineStoreStatusV1::ok);

        Vst3TimelineSupervisorSurfaceV1 surface;
        CHECK(surface.attach(surface_store));
        CHECK(!surface.attach(surface_store));
        CHECK(surface.refresh_ids(surface_ids, surface_id_count) ==
                  Vst3TimelineStoreStatusV1::ok &&
              surface_id_count == 2U && surface_ids[0] == "alpha" &&
              surface_ids[1] == "beta");
        CHECK(!surface.has_selection() && !surface.is_dirty());

        const bool unselected_refused =
            !surface.begin_edit() &&
            !surface.discard() && !surface.commit() &&
            !surface.upsert(Vst3ParameterTimelineEventV1{1U, 0U, 0.5}) &&
            !surface.remove_at(0U) && !surface.set_value_at(0U, 0.5) &&
            !surface.undo() && !surface.redo();
        CHECK(unselected_refused);
        CHECK(surface.save_selected() ==
              Vst3TimelineStoreStatusV1::invalid_argument);

        const std::string long_but_valid_id(64U, 'a');
        const std::string over_long_id(65U, 'a');
        CHECK(surface_store.save(long_but_valid_id, persist_empty) ==
              Vst3TimelineStoreStatusV1::ok);
        CHECK(!surface.select("") && !surface.select("bad/id") &&
              !surface.select(over_long_id) &&
              surface.last_store_status() == Vst3TimelineStoreStatusV1::id_rejected);
        CHECK(!surface.select("missing") &&
              surface.last_store_status() == Vst3TimelineStoreStatusV1::not_found);
        CHECK(!surface.has_selection());

        CHECK(surface.select("alpha") && surface.selected_id() == "alpha");
        CHECK(surface.editor().published().event_count == 2U &&
              surface.editor().published().events[1].parameter_id == 7U);
        CHECK(!surface.is_dirty() && !surface.editor().has_edit_session());

        CHECK(surface.begin_edit() && surface.editor().has_edit_session());
        CHECK(!surface.select("beta"));
        CHECK(surface.upsert(Vst3ParameterTimelineEventV1{9U, 48010U, 0.5}) &&
              surface.editor().draft() != nullptr &&
              surface.editor().draft()->event_count == 3U);
        CHECK(!surface.is_dirty() &&
              surface.editor().published().event_count == 2U);
        CHECK(surface.commit() && !surface.editor().has_edit_session());
        CHECK(surface.is_dirty() &&
              surface.editor().published().event_count == 3U);

        CHECK(surface.begin_edit() && surface.remove_at(0U) && surface.commit());
        CHECK(surface.editor().published().event_count == 2U &&
              surface.editor().published().events[0].parameter_id == 7U);
        CHECK(surface.is_dirty());
        CHECK(surface.undo() &&
              surface.editor().published().event_count == 3U &&
              surface.is_dirty());
        CHECK(surface.save_selected() == Vst3TimelineStoreStatusV1::ok &&
              !surface.is_dirty());
        {
            Vst3ParameterTimelineSnapshotV1 persisted_after_save{};
            CHECK(surface_store.load("alpha", persisted_after_save) ==
                      Vst3TimelineStoreStatusV1::ok &&
                  persisted_after_save.event_count == 3U &&
                  persisted_after_save.events[2].parameter_id == 9U);
        }

        CHECK(surface.undo() &&
              surface.editor().published().events[0].parameter_id == 3U &&
              surface.is_dirty());
        CHECK(surface.redo() &&
              surface.editor().published().event_count == 3U &&
              !surface.is_dirty());

        // Surface-level history introspection: the accessors delegate to the
        // composed editor and are valid even while detached/unselected.
        CHECK(surface.can_undo() && surface.can_redo() &&
              surface.undo_depth() == 1U && surface.redo_depth() == 1U);
        surface.clear_history();
        CHECK(!surface.can_undo() && !surface.can_redo() &&
              surface.undo_depth() == 0U && surface.redo_depth() == 0U);
        CHECK(surface.editor().published().event_count == 3U &&
              !surface.is_dirty());
        // Detached surface: introspection is still valid on an empty editor.
        Vst3TimelineSupervisorSurfaceV1 empty_history_surface;
        CHECK(!empty_history_surface.can_undo() &&
              !empty_history_surface.can_redo() &&
              empty_history_surface.undo_depth() == 0U &&
              empty_history_surface.redo_depth() == 0U);
        empty_history_surface.clear_history();
        CHECK(empty_history_surface.undo_depth() == 0U);

        CHECK(surface.begin_edit() && !surface.detach());
        CHECK(surface.discard() && surface.detach());
        CHECK(!surface.is_attached() && !surface.has_selection() &&
              !surface.is_dirty());
        CHECK(!surface.detach());
        CHECK(!detached_surface.remove_selected() &&
              detached_surface.last_store_status() ==
                  Vst3TimelineStoreStatusV1::ok);

        Vst3TimelineSupervisorSurfaceV1 remove_surface;
        CHECK(remove_surface.attach(surface_store));
        for (auto& stale_id : surface_ids) {
            stale_id.clear();
        }
        CHECK(remove_surface.refresh_ids(surface_ids, surface_id_count) ==
                  Vst3TimelineStoreStatusV1::ok && surface_id_count == 3U &&
              surface_ids[0] == long_but_valid_id &&
              surface_ids[1] == "alpha" && surface_ids[2] == "beta");
        const bool remove_unselected_refused =
            !remove_surface.remove_selected() &&
            remove_surface.last_store_status() ==
                Vst3TimelineStoreStatusV1::ok;
        CHECK(remove_unselected_refused);
        CHECK(remove_surface.select("beta") && remove_surface.has_selection());
        CHECK(remove_surface.begin_edit() && !remove_surface.remove_selected() &&
              remove_surface.has_selection() &&
              remove_surface.editor().has_edit_session());
        CHECK(remove_surface.upsert(Vst3ParameterTimelineEventV1{5U, 48000U, 0.25}));
        CHECK(remove_surface.commit() && remove_surface.is_dirty() &&
              remove_surface.remove_selected());
        CHECK(!remove_surface.is_dirty() && !remove_surface.has_selection() &&
              !remove_surface.can_undo() && !remove_surface.can_redo() &&
              remove_surface.selected_id().empty());
        CHECK(remove_surface.last_store_status() == Vst3TimelineStoreStatusV1::ok);
        CHECK(remove_surface.refresh_ids(surface_ids, surface_id_count) ==
                  Vst3TimelineStoreStatusV1::ok && surface_id_count == 2U &&
              surface_ids[0] == long_but_valid_id &&
              surface_ids[1] == "alpha");
        {
            Vst3ParameterTimelineSnapshotV1 removed_probe{};
            CHECK(surface_store.load("beta", removed_probe) ==
                      Vst3TimelineStoreStatusV1::not_found);
        }

        std::error_code surface_cleanup_error;
        std::filesystem::remove_all(surface_root, surface_cleanup_error);
    }
#endif
    Vst3WorkerLaneSessionV1 introspect_second_lane;
    CHECK(introspect_second_lane.prepare(sandbox,
          Vst3WorkerLaneConfigV1{77U, 2U, 48000.0, 64U, 128U}));
    const std::array<Vst3WorkerLaneSessionV1*, 2> introspect_lanes{
        {&worker_lane, &introspect_second_lane}};
    CHECK(automation_scheduler->timeline_count() == 0U);
    std::array<std::string, kVst3SceneAutomationMaxEntriesV1> introspect_ids{};
    std::size_t introspect_id_count = 0U;
    CHECK(automation_scheduler->timeline_ids(introspect_ids, introspect_id_count) &&
          introspect_id_count == 0U);
    CHECK(automation_scheduler->prepare(introspect_lanes) &&
          automation_scheduler->upsert_timeline("zeta", automation_timeline) &&
          automation_scheduler->upsert_timeline("alpha-01", automation_timeline) &&
          automation_scheduler->upsert_timeline("mid", automation_timeline));
    CHECK(automation_scheduler->timeline_ids(introspect_ids, introspect_id_count) &&
          introspect_id_count == 3U &&
          introspect_ids[0] == "alpha-01" && introspect_ids[1] == "mid" &&
          introspect_ids[2] == "zeta");
    std::array<std::string, 2> undersized_ids{};
    std::size_t undersized_count = 99U;
    CHECK(!automation_scheduler->timeline_ids(undersized_ids, undersized_count) &&
          undersized_count == 0U);
    CHECK(automation_scheduler->bind_scene("SCENE-A", 99U, "alpha-01") &&
          automation_scheduler->bind_scene("SCENE-B", 77U, "mid"));
    std::array<Vst3SceneAutomationBindingViewV1, kVst3SceneAutomationMaxEntriesV1>
        introspect_views{};
    std::size_t introspect_view_count = 0U;
    CHECK(automation_scheduler->binding_views(introspect_views, introspect_view_count) &&
          introspect_view_count == 2U &&
          introspect_views[0].scene_id == "SCENE-A" &&
          introspect_views[0].lane_token == 99U &&
          introspect_views[0].timeline_id == "alpha-01" &&
          introspect_views[1].scene_id == "SCENE-B" &&
          introspect_views[1].lane_token == 77U &&
          introspect_views[1].timeline_id == "mid");
    CHECK(automation_scheduler->remove_timeline("zeta"));
    CHECK(automation_scheduler->binding_views(introspect_views, introspect_view_count) &&
          introspect_view_count == 2U &&
          introspect_views[1].timeline_id == "mid" &&
          automation_scheduler->timeline_ids(introspect_ids, introspect_id_count) &&
          introspect_id_count == 2U && introspect_ids[1] == "mid");
    std::array<Vst3SceneAutomationBindingViewV1, 1> single_view{};
    std::size_t single_view_count = 0U;
    CHECK(automation_scheduler->binding_views(single_view, single_view_count) ==
              false && single_view_count == 0U);
    automation_scheduler->clear();
    CHECK(automation_scheduler->timeline_ids(introspect_ids, introspect_id_count) &&
          introspect_id_count == 0U);
    Vst3WorkerPipeV1 worker_pipe;
    CHECK(!worker_pipe.create_server(Vst3WorkerPipeConfigV1{L"", 1024U, 100U}));
    CHECK(!worker_pipe.connect_client(L"", 100U));

    const std::array<LatencyLaneInputV1, 3> latency_lanes{{
        {true, 64U}, {true, 256U}, {false, 0U}}};
    LatencyAlignmentPlanV1 latency_plan{};
    CHECK(build_latency_alignment_plan_v1(latency_lanes, latency_plan) &&
          latency_plan.maximum_latency_samples == 256U &&
          latency_plan.delay_samples[0] == 192U && latency_plan.delay_samples[1] == 0U &&
          latency_plan.delay_samples[2] == 0U &&
          validate_latency_alignment_plan_v1(latency_plan));
    auto delay_line = std::make_unique<FixedDelayLineV1>();
    CHECK(delay_line->prepare(2U, 2U));
    const float delay_input[] = {1.0F, -1.0F, 0.5F, -0.5F, 0.25F, -0.25F};
    float delay_output[6]{};
    CHECK(delay_line->process(delay_input, delay_output, 3U) &&
          delay_output[0] == 0.0F && delay_output[1] == 0.0F &&
          delay_output[2] == 0.0F && delay_output[3] == 0.0F &&
          std::abs(delay_output[4] - 1.0F) < 1e-6F &&
          std::abs(delay_output[5] + 1.0F) < 1e-6F);
    const float delay_nan[] = {std::numeric_limits<float>::quiet_NaN(), 0.0F};
    CHECK(!delay_line->process(delay_nan, delay_output, 1U) && delay_output[0] == 0.0F &&
          delay_output[1] == 0.0F);

    const std::array<LatencyGraphLaneInputV1, 3> graph_latency_lanes{{
        {101U, true, 2U, 64U}, {202U, true, 6U, 256U}, {303U, false, 2U, 128U}}};
    LatencyGraphCommitV1 graph_latency_commit{};
    CHECK(prepare_latency_graph_commit_v1(graph_latency_lanes, 10U, 11U,
                                          graph_latency_commit) &&
          graph_latency_commit.maximum_latency_samples == 256U &&
          graph_latency_commit.lanes[0].compensation_delay_samples == 192U &&
          graph_latency_commit.lanes[1].compensation_delay_samples == 0U &&
          graph_latency_commit.lanes[2].reported_latency_samples == 0U &&
          graph_latency_commit.lanes[2].compensation_delay_samples == 256U &&
          validate_latency_graph_commit_v1(graph_latency_commit));
    auto graph_latency_committer = std::make_unique<LatencyGraphCommitterV1>();
    CHECK(graph_latency_committer->prepare(graph_latency_lanes, 1U) &&
          graph_latency_committer->state() == LatencyGraphTransactionStateV1::Prepared &&
          graph_latency_committer->commit() &&
          graph_latency_committer->active_graph_revision() == 1U);
    CHECK(graph_latency_committer->prepare(graph_latency_lanes, 3U) &&
          graph_latency_committer->pending().base_graph_revision == 1U);
    graph_latency_committer->rollback();
    CHECK(graph_latency_committer->active_graph_revision() == 1U &&
          graph_latency_committer->state() == LatencyGraphTransactionStateV1::Ready);
    CHECK(!graph_latency_committer->prepare(graph_latency_lanes, 1U) &&
          graph_latency_committer->state() == LatencyGraphTransactionStateV1::Degraded);
    const std::array<LatencyGraphLaneInputV1, 2> duplicate_latency_lanes{{
        {7U, true, 2U, 1U}, {7U, true, 2U, 2U}}};
    CHECK(!prepare_latency_graph_commit_v1(duplicate_latency_lanes, 0U, 1U,
                                            graph_latency_commit));

    GraphConfigV1 delayed_graph;
    delayed_graph.lanes.push_back(LaneConfigV1{"fast-plugin", "main", 2, 0.0, true});
    delayed_graph.lanes.push_back(LaneConfigV1{"slow-plugin", "main", 2, 0.0, true});
    delayed_graph.lanes[0].reported_latency_samples = 64U;
    delayed_graph.lanes[1].reported_latency_samples = 256U;
    RtGraphSnapshotV1 delayed_snapshot{};
    CHECK(compile_rt_snapshot(delayed_graph, 12U, delayed_snapshot) &&
          delayed_snapshot.lanes[0].compensation_delay_samples == 192U &&
          delayed_snapshot.lanes[1].compensation_delay_samples == 0U);
    const std::array<LaneLatencyConfigV1, 2> delayed_configs{{
        {2U, delayed_snapshot.lanes[0].compensation_delay_samples, true},
        {2U, delayed_snapshot.lanes[1].compensation_delay_samples, true}}};
    LaneLatencyBankV1 delayed_bank;
    CHECK(delayed_bank.prepare(delayed_configs));
    std::array<float, 2U * 256U> fast_impulse{};
    fast_impulse[0] = 1.0F;
    fast_impulse[1] = -1.0F;
    std::array<float, 2U * 256U> slow_silence{};
    const std::array<RtLaneInputV1, 2> delayed_inputs{{
        {fast_impulse.data(), 2U}, {slow_silence.data(), 2U}}};
    std::array<float, 2U * 256U> delayed_output{};
    CHECK(process_graph(delayed_snapshot, delayed_inputs, delayed_output.data(), 256U,
                        &delayed_bank));
    CHECK(delayed_output[2U * 191U] == 0.0F && delayed_output[2U * 192U] == 1.0F &&
          delayed_output[2U * 192U + 1U] == -1.0F);

    std::vector<std::uint8_t> tab_packet(16U + 2U * 2U * sizeof(float), 0U);
    tab_packet[0] = 'H'; tab_packet[1] = 'I'; tab_packet[2] = 'B'; tab_packet[3] = 'T';
    tab_packet[4] = 1U;
    tab_packet[6] = 2U;
    tab_packet[8] = 2U;
    tab_packet[12] = 0x80U; tab_packet[13] = 0xBBU; tab_packet[14] = 0U; tab_packet[15] = 0U;
    const float tab_samples[4] = {0.25F, -0.25F, 0.5F, -0.5F};
    std::memcpy(tab_packet.data() + 16U, tab_samples, sizeof(tab_samples));
    TabCapturePacketViewV1 tab_view{};
    TabPacketError tab_error{TabPacketError::None};
    CHECK(decode_tab_capture_packet_v1(tab_packet, tab_view, tab_error));
    CHECK(tab_view.channels == 2U && tab_view.frames == 2U && tab_view.sample_rate == 48000U);
    CHECK(std::abs(tab_view.sample(2U) - 0.5F) < 1e-6F);
    auto tab_queue = std::make_unique<TabCaptureQueueV1>();
    enqueue_tab_capture_packet_v1(tab_view, tab_queue.get());
    float tab_output[8]{};
    TabCaptureBlockV1 tab_block{};
    CHECK(tab_queue->pop(tab_output, 2U, tab_block));
    CHECK(tab_block.frames == 2U && tab_block.channels == 2U &&
          std::abs(tab_output[2] - 0.5F) < 1e-6F);
    auto full_tab_queue = std::make_unique<TabCaptureQueueV1>();
    CHECK(full_tab_queue->push(tab_view));
    CHECK(full_tab_queue->push(tab_view));
    CHECK(full_tab_queue->push(tab_view));
    CHECK(full_tab_queue->push(tab_view));
    CHECK(!full_tab_queue->push(tab_view) && full_tab_queue->dropped_blocks() == 1U);
    CHECK(tab_queue->push(tab_view));
    tab_packet.pop_back();
    CHECK(!decode_tab_capture_packet_v1(tab_packet, tab_view, tab_error) &&
          tab_error == TabPacketError::LengthMismatch);

    // Rebuild a valid 2ch x 2fr @48k packet for bounded negative-path coverage.
    auto valid_packet = std::vector<std::uint8_t>(16U + 2U * 2U * sizeof(float), 0U);
    valid_packet[0] = 'H'; valid_packet[1] = 'I'; valid_packet[2] = 'B'; valid_packet[3] = 'T';
    valid_packet[4] = 1U;
    valid_packet[6] = 2U;
    valid_packet[8] = 2U;
    valid_packet[12] = 0x80U; valid_packet[13] = 0xBBU; valid_packet[14] = 0U; valid_packet[15] = 0U;
    const float valid_samples[4] = {0.25F, -0.25F, 0.5F, -0.5F};
    std::memcpy(valid_packet.data() + 16U, valid_samples, sizeof(valid_samples));

    const auto truncated_header = std::vector<std::uint8_t>(
        valid_packet.begin(), valid_packet.begin() + static_cast<std::ptrdiff_t>(15U));
    CHECK(!decode_tab_capture_packet_v1(truncated_header, tab_view, tab_error) &&
          tab_error == TabPacketError::Truncated);

    auto truncated_payload = valid_packet;
    truncated_payload.pop_back();
    CHECK(!decode_tab_capture_packet_v1(truncated_payload, tab_view, tab_error) &&
          tab_error == TabPacketError::LengthMismatch);

    auto nan_packet = valid_packet;
    const float nan_sample = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(nan_packet.data() + 16U, &nan_sample, sizeof(nan_sample));
    CHECK(!decode_tab_capture_packet_v1(nan_packet, tab_view, tab_error) &&
          tab_error == TabPacketError::NonFiniteSample);

    auto bad_channels = valid_packet;
    bad_channels[6U] = 3U;
    CHECK(!decode_tab_capture_packet_v1(bad_channels, tab_view, tab_error) &&
          tab_error == TabPacketError::InvalidChannels);

    auto bad_rate = valid_packet;
    bad_rate[12U] = (22222U >> 0U) & 0xFFU;
    bad_rate[13U] = (22222U >> 8U) & 0xFFU;
    bad_rate[14U] = (22222U >> 16U) & 0xFFU;
    bad_rate[15U] = (22222U >> 24U) & 0xFFU;
    CHECK(!decode_tab_capture_packet_v1(bad_rate, tab_view, tab_error) &&
          tab_error == TabPacketError::InvalidSampleRate);

    TabBridgeServer tab_server;
    CHECK(!tab_server.start(TabBridgeServerConfigV1{17842U, 256U * 1024U}, nullptr, nullptr));
    CHECK(!tab_server.running());

    const auto transport_bytes = hibiki_asio_transport_region_size_v1();
    std::vector<std::uint64_t> transport_words(
        (transport_bytes + sizeof(std::uint64_t) - 1U) / sizeof(std::uint64_t));
    auto* transport = reinterpret_cast<hibiki_asio_transport_region_v1*>(transport_words.data());
    CHECK(hibiki_asio_transport_init_v1(transport,
                                        transport_words.size() * sizeof(std::uint64_t),
                                        2U, 48000U, 4U) == 1 &&
          transport->reserved == 0U);
    const float left[4] = {1.0F, 2.0F, 3.0F, 4.0F};
    const float right[4] = {-1.0F, -2.0F, -3.0F, -4.0F};
    const float* planar[2] = {left, right};
    CHECK(hibiki_asio_transport_push_planar_v1(
              transport, transport_words.size() * sizeof(std::uint64_t), planar, 2U, 4U) == 1);
    float transport_output[8]{};
    uint32_t transport_frames = 0U;
    uint32_t transport_channels = 0U;
    uint32_t transport_rate = 0U;
    CHECK(hibiki_asio_transport_pop_interleaved_v1(
              transport, transport_words.size() * sizeof(std::uint64_t), transport_output, 4U,
              &transport_frames, &transport_channels, &transport_rate) == 1);
    CHECK(transport_frames == 4U && transport_channels == 2U && transport_rate == 48000U);
    CHECK(transport_output[0] == 1.0F && transport_output[1] == -1.0F &&
          transport_output[6] == 4.0F && transport_output[7] == -4.0F);
    const auto producer_before_reserved = transport->producer_sequence;
    const auto consumer_before_reserved = transport->consumer_sequence;
    const auto dropped_before_reserved = transport->dropped_blocks;
    transport->reserved = 1U;
    CHECK(hibiki_asio_transport_push_planar_v1(
              transport, transport_words.size() * sizeof(std::uint64_t), planar, 2U, 4U) == 0 &&
          transport->producer_sequence == producer_before_reserved &&
          transport->consumer_sequence == consumer_before_reserved &&
          transport->dropped_blocks == dropped_before_reserved);
    transport_frames = 17U;
    transport_channels = 17U;
    transport_rate = 17U;
    CHECK(hibiki_asio_transport_pop_interleaved_v1(
              transport, transport_words.size() * sizeof(std::uint64_t), transport_output, 4U,
              &transport_frames, &transport_channels, &transport_rate) == 0 &&
          transport_frames == 17U && transport_channels == 17U && transport_rate == 17U &&
          transport->producer_sequence == producer_before_reserved &&
          transport->consumer_sequence == consumer_before_reserved);
    transport->reserved = 0U;

    hibiki_driver_endpoint_state_v1 driver_state{};
    driver_state.header.abi_version = HIBIKI_DRIVER_CONTROL_ABI_V1;
    driver_state.header.message_type = HIBIKI_DRIVER_ENDPOINT_STATE;
    driver_state.header.size_bytes = sizeof(driver_state);
    driver_state.endpoint_guid[0] = 'x';
    driver_state.endpoint_guid[1] = '\0';
    driver_state.event_context_guid[0] = 'u';
    driver_state.event_context_guid[1] = '\0';
    driver_state.channel_count = 8;
    driver_state.sample_rate = 48000;
    driver_state.frames_per_buffer = 128;
    CHECK(driver_state.header.abi_version == 1U && driver_state.channel_count == 8U);
    CHECK(hibiki_driver_validate_endpoint_state_v1(&driver_state, sizeof(driver_state)) == 1);
    driver_state.sample_rate = 12345;
    CHECK(hibiki_driver_validate_endpoint_state_v1(&driver_state, sizeof(driver_state)) == 0);

    driver_state.sample_rate = 48000U;
    driver_state.requested_db_q16_16 = -18 * 65536;
    driver_state.safety_ceiling_db_q16_16 = 0;
    driver_state.effective_db_q16_16 = -18 * 65536;
    driver_state.mute = 0U;
    driver_state.generation = 7U;
    driver_state.actuator = HIBIKI_ACTUATOR_DEVICE_HARDWARE;
    std::memcpy(driver_state.endpoint_guid, "hibiki-main", 12U);
    std::memcpy(driver_state.event_context_guid, "u", 2U);
    std::array<std::uint8_t, HIBIKI_DRIVER_CONTROL_ENDPOINT_STATE_PACKET_BYTES_V1>
        driver_control_packet{};
    std::size_t driver_control_packet_bytes = 0U;
    CHECK(hibiki_driver_endpoint_state_packet_encode_v1(
              driver_control_packet.data(), driver_control_packet.size(),
              HIBIKI_DRIVER_VOLUME_NOTIFICATION, 123U, &driver_state,
              &driver_control_packet_bytes) == 1 &&
          driver_control_packet_bytes == HIBIKI_DRIVER_CONTROL_ENDPOINT_STATE_PACKET_BYTES_V1);
    CHECK(hibiki_driver_endpoint_state_packet_validate_v1(
              driver_control_packet.data(), driver_control_packet_bytes) == 1);
    const std::uint64_t zero_request_id = 0U;
    const auto max_request_id = (std::numeric_limits<std::uint64_t>::max)();
    std::array<std::uint8_t, HIBIKI_DRIVER_CONTROL_ENDPOINT_STATE_PACKET_BYTES_V1>
        zero_request_packet{};
    zero_request_packet.fill(0xA5U);
    std::size_t zero_request_bytes = 123U;
    CHECK(hibiki_driver_endpoint_state_packet_encode_v1(
              zero_request_packet.data(), zero_request_packet.size(),
              HIBIKI_DRIVER_VOLUME_NOTIFICATION, zero_request_id, &driver_state,
              &zero_request_bytes) == 0 &&
          zero_request_bytes == 0U);
    CHECK(std::all_of(zero_request_packet.begin(), zero_request_packet.end(),
                      [](const std::uint8_t value) { return value == 0xA5U; }));
    std::array<std::uint8_t, HIBIKI_DRIVER_CONTROL_ENDPOINT_STATE_PACKET_BYTES_V1>
        driver_control_packet_copy = driver_control_packet;
    std::memcpy(driver_control_packet_copy.data() + 8U, &zero_request_id,
                sizeof(zero_request_id));
    CHECK(hibiki_driver_endpoint_state_packet_validate_v1(
              driver_control_packet_copy.data(), driver_control_packet_copy.size()) == 0);
    hibiki_driver_endpoint_state_v1 max_driver_state{};
    std::uint16_t max_driver_message_type = 0U;
    std::uint64_t max_driver_request_id = 0U;
    CHECK(hibiki_driver_endpoint_state_packet_encode_v1(
              driver_control_packet.data(), driver_control_packet.size(),
              HIBIKI_DRIVER_VOLUME_NOTIFICATION, max_request_id, &driver_state,
              &driver_control_packet_bytes) == 1 &&
          hibiki_driver_endpoint_state_packet_validate_v1(
              driver_control_packet.data(), driver_control_packet_bytes) == 1 &&
          hibiki_driver_endpoint_state_packet_decode_v1(
              driver_control_packet.data(), driver_control_packet_bytes, &max_driver_state,
              &max_driver_message_type, &max_driver_request_id) == 1 &&
          max_driver_request_id == max_request_id);
    CHECK(hibiki_driver_endpoint_state_packet_encode_v1(
              driver_control_packet.data(), driver_control_packet.size(),
              HIBIKI_DRIVER_VOLUME_NOTIFICATION, 123U, &driver_state,
              &driver_control_packet_bytes) == 1);
    driver_control_packet_copy = driver_control_packet;
    driver_control_packet_copy[6U] = 0U;
    CHECK(hibiki_driver_endpoint_state_packet_validate_v1(
              driver_control_packet_copy.data(), driver_control_packet_copy.size()) == 0);
    hibiki_driver_endpoint_state_v1 decoded_driver_state{};
    std::uint16_t decoded_driver_message_type = 0U;
    std::uint64_t decoded_driver_request_id = 0U;
    CHECK(hibiki_driver_endpoint_state_packet_decode_v1(
              driver_control_packet.data(), driver_control_packet.size(), &decoded_driver_state,
              &decoded_driver_message_type, &decoded_driver_request_id) == 1 &&
          decoded_driver_message_type == HIBIKI_DRIVER_VOLUME_NOTIFICATION &&
          decoded_driver_request_id == 123U && decoded_driver_state.channel_count == 8U &&
          decoded_driver_state.requested_db_q16_16 == -18 * 65536 &&
          hibiki_driver_validate_endpoint_state_v1(
              &decoded_driver_state, sizeof(decoded_driver_state)) == 1);

    std::array<std::uint8_t, HIBIKI_DRIVER_CONTROL_HEADER_BYTES_V1> driver_header_packet{};
    std::size_t driver_header_packet_bytes = 0U;
    CHECK(hibiki_driver_control_header_packet_encode_v1(
              driver_header_packet.data(), driver_header_packet.size(), HIBIKI_DRIVER_HELLO,
              456U, &driver_header_packet_bytes) == 1 &&
          driver_header_packet_bytes == HIBIKI_DRIVER_CONTROL_HEADER_BYTES_V1 &&
          hibiki_driver_control_header_packet_validate_v1(
              driver_header_packet.data(), driver_header_packet.size()) == 1);
    std::uint16_t decoded_driver_header_type = 0U;
    std::uint64_t decoded_driver_header_request = 0U;
    CHECK(hibiki_driver_control_header_packet_decode_v1(
              driver_header_packet.data(), driver_header_packet.size(),
              &decoded_driver_header_type, &decoded_driver_header_request) == 1 &&
          decoded_driver_header_type == HIBIKI_DRIVER_HELLO &&
          decoded_driver_header_request == 456U);
    std::array<std::uint8_t, HIBIKI_DRIVER_CONTROL_HEADER_BYTES_V1> zero_header_packet{};
    zero_header_packet.fill(0xA5U);
    std::size_t zero_header_bytes = 123U;
    CHECK(hibiki_driver_control_header_packet_encode_v1(
              zero_header_packet.data(), zero_header_packet.size(), HIBIKI_DRIVER_HELLO,
              zero_request_id, &zero_header_bytes) == 0 && zero_header_bytes == 0U);
    CHECK(std::all_of(zero_header_packet.begin(), zero_header_packet.end(),
                      [](const std::uint8_t value) { return value == 0xA5U; }));
    auto zero_header_copy = driver_header_packet;
    std::memcpy(zero_header_copy.data() + 8U, &zero_request_id, sizeof(zero_request_id));
    CHECK(hibiki_driver_control_header_packet_validate_v1(
              zero_header_copy.data(), zero_header_copy.size()) == 0);
    CHECK(hibiki_driver_control_header_packet_encode_v1(
              driver_header_packet.data(), driver_header_packet.size(), HIBIKI_DRIVER_HELLO,
              max_request_id, &driver_header_packet_bytes) == 1 &&
          hibiki_driver_control_header_packet_validate_v1(
              driver_header_packet.data(), driver_header_packet_bytes) == 1 &&
          hibiki_driver_control_header_packet_decode_v1(
              driver_header_packet.data(), driver_header_packet_bytes,
              &decoded_driver_header_type, &decoded_driver_header_request) == 1 &&
          decoded_driver_header_request == max_request_id);
    for (std::uint16_t message_type = HIBIKI_DRIVER_HELLO;
         message_type <= HIBIKI_DRIVER_ERROR; ++message_type) {
        CHECK(hibiki_driver_control_header_packet_encode_v1(
                  driver_header_packet.data(), driver_header_packet.size(), message_type,
                  500U + message_type, &driver_header_packet_bytes) == 1 &&
              hibiki_driver_control_header_packet_validate_v1(
                  driver_header_packet.data(), driver_header_packet_bytes) == 1 &&
              hibiki_driver_control_header_packet_decode_v1(
                  driver_header_packet.data(), driver_header_packet_bytes,
                  &decoded_driver_header_type, &decoded_driver_header_request) == 1 &&
              decoded_driver_header_type == message_type &&
              decoded_driver_header_request == 500U + message_type);
    }
    driver_header_packet[0U] = 0U;
    CHECK(hibiki_driver_control_header_packet_validate_v1(
              driver_header_packet.data(), driver_header_packet.size()) == 0);

    DriverEndpointStateV1 driver_bridge_state{};
    std::uint16_t bridge_message_type = 0U;
    std::uint64_t bridge_request_id = 0U;
    CHECK(decode_driver_endpoint_state_packet_v1(
              std::span<const std::uint8_t>(driver_control_packet.data(),
                                             driver_control_packet.size()),
              driver_bridge_state, bridge_message_type, bridge_request_id) &&
          bridge_message_type == HIBIKI_DRIVER_VOLUME_NOTIFICATION &&
          bridge_request_id == 123U && driver_bridge_state.channel_count == 8U);
    DriverVolumeLinkV1 driver_volume_link;
    CHECK(driver_volume_link.add_ignored_event_context("u"));
    AudioEngineModel driver_volume_engine;
    CHECK(driver_volume_link.apply(driver_volume_engine, "main", driver_bridge_state) ==
          DriverVolumeSyncResultV1::IgnoredSelf);
    driver_bridge_state.event_context_guid.fill('\0');
    std::memcpy(driver_bridge_state.event_context_guid.data(), "external", 9U);
    driver_bridge_state.generation = 8U;
    CHECK(driver_volume_link.apply(driver_volume_engine, "main", driver_bridge_state) ==
          DriverVolumeSyncResultV1::Applied);
    CHECK(std::abs(driver_volume_engine.volume().requested_db - (-18.0)) < 0.001);
    driver_bridge_state.generation = 7U;
    CHECK(driver_volume_link.apply(driver_volume_engine, "main", driver_bridge_state) ==
          DriverVolumeSyncResultV1::StaleGeneration);

    CHECK(hibiki_endpoint_topology_count_v1() == HIBIKI_ENDPOINT_TOPOLOGY_COUNT_V1);
    hibiki_endpoint_topology_v1 topology{};
    CHECK(hibiki_endpoint_topology_get_v1(0U, &topology) == 1);
    CHECK(topology.endpoint_kind == HIBIKI_ENDPOINT_MAIN_RENDER_V1 &&
          topology.direction == HIBIKI_ENDPOINT_DIRECTION_RENDER_V1 &&
          topology.channel_count == 2U &&
          topology.channel_mask == HIBIKI_CHANNEL_MASK_STEREO_V1);
    CHECK(hibiki_endpoint_topology_get_v1(2U, &topology) == 1);
    CHECK(topology.endpoint_kind == HIBIKI_ENDPOINT_SURROUND_RENDER_V1 &&
          topology.channel_count == 8U && topology.channel_mask == HIBIKI_CHANNEL_MASK_71_V1);
    CHECK(hibiki_endpoint_topology_get_v1(3U, &topology) == 1);
    CHECK(topology.direction == HIBIKI_ENDPOINT_DIRECTION_CAPTURE_V1 &&
          topology.endpoint_kind == HIBIKI_ENDPOINT_VIRTUAL_MIC_CAPTURE_V1);
    topology.channel_mask = HIBIKI_CHANNEL_MASK_STEREO_V1;
    topology.channel_count = 8U;
    CHECK(hibiki_endpoint_topology_validate_v1(&topology) == 0);
    CHECK(hibiki_endpoint_topology_get_v1(HIBIKI_ENDPOINT_TOPOLOGY_COUNT_V1, &topology) == 0);

    float ring_storage[8]{};
    InterleavedRingBuffer ring(std::span<float>(ring_storage), 2);
    CHECK(ring.valid() && ring.capacity_frames() == 4);
    const float ring_input[] = {1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
    CHECK(ring.push(ring_input, 3));
    float ring_output[4]{};
    CHECK(ring.pop(ring_output, 2));
    CHECK(ring_output[0] == 1.0F && ring_output[1] == 2.0F && ring_output[2] == 3.0F &&
          ring_output[3] == 4.0F);
    ClockDriftEstimator drift;
    drift.observe(48000.0, 48012.0, 1.0);
    CHECK(drift.ratio() > 1.0 && drift.drift_ppm() <= 500.0);
    const float resample_input[] = {0.0F, 1.0F, 2.0F, 3.0F};
    float resample_output[6]{};
    CHECK(linear_resample_interleaved(resample_input, 4, resample_output, 3, 1, 1.5));
    CHECK(std::abs(resample_output[1] - 1.5F) < 1e-5F);

    VirtualMicDspPolicyV1 mic_dsp_policy{};
    mic_dsp_policy.echo_cancellation_enabled = true;
    mic_dsp_policy.filter_length = 8U;
    mic_dsp_policy.adaptation_rate = 0.5F;
    VirtualMicDspV1 mic_dsp;
    CHECK(mic_dsp.prepare(mic_dsp_policy, 1U, 48000U));
    std::array<float, 64> aec_capture{};
    std::array<float, 64> aec_reference{};
    std::array<float, 64> aec_clean{};
    for (std::size_t aec_frame = 0U; aec_frame < aec_capture.size(); ++aec_frame) {
        aec_reference[aec_frame] = 0.5F;
        aec_capture[aec_frame] = 0.25F;
    }
    CHECK(mic_dsp.process(aec_capture.data(), aec_reference.data(), aec_clean.data(),
                          aec_capture.size()));
    CHECK(std::abs(aec_clean.back()) < 0.01F);
    aec_capture[3] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!mic_dsp.process(aec_capture.data(), aec_reference.data(), aec_clean.data(),
                           aec_capture.size()));
    CHECK(aec_clean[0] == 0.0F && aec_clean.back() == 0.0F);

    // Regression: the virtual mic gate must close with release_ms (slow) and
    // open with attack_ms (fast). A fresh instance starts with the gate open,
    // so sub-threshold input exercises the closing direction first.
    VirtualMicDspPolicyV1 vm_gate_policy{};
    vm_gate_policy.noise_gate_enabled = true;
    vm_gate_policy.noise_gate_threshold_dbfs = -50.0F;
    vm_gate_policy.noise_gate_floor = 0.08F;
    vm_gate_policy.attack_ms = 1.0F;
    vm_gate_policy.release_ms = 10.0F;
    VirtualMicDspV1 vm_gate;
    CHECK(vm_gate.prepare(vm_gate_policy, 1U, 48000U));
    std::array<float, 960> vm_close{};
    vm_close.fill(0.0005F);
    CHECK(vm_gate.process(vm_close.data(), nullptr, vm_close.data(), vm_close.size()));
    // With release_ms=10 ms (a 480-sample time constant), 960 frames leave the
    // closed gain near 0.205, so the output tail is near 1.02e-4. The reversed
    // mapping would close with attack_ms instead and settle at the 0.08 floor
    // (output near 4.0e-5).
    CHECK(std::abs(vm_close.back()) > 9.0e-5F);
    CHECK(std::abs(vm_close.back()) < 1.5e-4F);

    // Feed an above-threshold signal without resetting. With attack_ms=1 ms at
    // 48 kHz (~48 samples per time constant), the gate is well open after 48
    // frames. The reversed mapping (opening with release_ms) only reaches about
    // 0.07 here.
    std::array<float, 48> vm_open{};
    vm_open.fill(0.25F);
    CHECK(vm_gate.process(vm_open.data(), nullptr, vm_open.data(), vm_open.size()));
    CHECK(vm_open.back() > 0.1F);

    // Regression: hysteresis prevents gate chatter. With a -50 dBFS threshold
    // the close boundary is ~0.00316 linear; reopen requires ~0.00398 (+2 dB).
    // A signal oscillating inside that band must hold the gate closed instead
    // of cycling open/closed every block.
    VirtualMicDspPolicyV1 vm_chatter_policy{};
    vm_chatter_policy.noise_gate_enabled = true;
    vm_chatter_policy.noise_gate_threshold_dbfs = -50.0F;
    vm_chatter_policy.noise_gate_floor = 0.08F;
    vm_chatter_policy.attack_ms = 1.0F;
    vm_chatter_policy.release_ms = 10.0F;
    VirtualMicDspV1 vm_chatter;
    CHECK(vm_chatter.prepare(vm_chatter_policy, 1U, 48000U));
    std::array<float, 960> vm_chatter_silence{};
    vm_chatter_silence.fill(0.0005F);
    CHECK(vm_chatter.process(vm_chatter_silence.data(), nullptr,
                             vm_chatter_silence.data(), vm_chatter_silence.size()));
    float vm_chatter_gain_max = 0.0F;
    for (std::size_t block = 0U; block < 8U; ++block) {
        const auto near_threshold = (block % 2U == 0U) ? 0.0028F : 0.0035F;
        std::array<float, 96> vm_chatter_block{};
        vm_chatter_block.fill(static_cast<float>(near_threshold));
        CHECK(vm_chatter.process(vm_chatter_block.data(), nullptr,
                                 vm_chatter_block.data(), vm_chatter_block.size()));
        for (const auto sample : vm_chatter_block) {
            vm_chatter_gain_max = (std::max)(vm_chatter_gain_max, std::abs(sample) / near_threshold);
        }
    }
    CHECK(vm_chatter_gain_max < 0.5F);

    const std::vector<PeqFilterV1> filters{{1000.0, 3.0, 1.0}, {100.0, -2.0, 0.7}};
    const auto apo = export_equalizer_apo(filters);
    const auto camilla = export_camilladsp_yaml(filters);
    const auto rew = export_rew_filter_list(filters);
    const auto profile = export_hibiki_profile(filters);
    CHECK(apo.find("Filter 1: ON PK Fc") != std::string::npos);
    CHECK(camilla.find("type: Peaking") != std::string::npos);
    CHECK(rew.find("Freq (Hz)") != std::string::npos);
    CHECK(profile.find("schema_version") != std::string::npos);
    const float ir_samples[] = {1.0F, 0.0F, -0.25F, 0.125F};
    const auto wav = export_wav_f32_ir(ir_samples, 48000, 2);
    CHECK(wav.size() == 60 && wav[0] == 'R' && wav[1] == 'I' && wav[2] == 'F' &&
          wav[3] == 'F' && wav[8] == 'W' && wav[9] == 'A' && wav[10] == 'V' && wav[11] == 'E');
    const auto decoded_ir = decode_ir_wav_v1(wav);
    CHECK(decoded_ir.valid && decoded_ir.data.sample_rate == 48000U &&
          decoded_ir.data.channels == 2U && decoded_ir.data.frames() == 2U &&
          std::abs(decoded_ir.data.interleaved_samples[2] + 0.25F) < 1e-6F);
    const auto ir_phase_resolution = resolve_ir_phase_policy(
        IrPhasePolicyV1{1U, IrPhaseMode::LinearPhase, 0.5});
    IrConvolverV1 decoded_convolver;
    CHECK(prepare_ir_convolver_from_wav_v1(decoded_convolver, decoded_ir.data,
                                           ir_phase_resolution) &&
          decoded_convolver.status().valid && decoded_convolver.status().taps == 2U &&
          decoded_convolver.status().uses_fir &&
          std::abs(decoded_convolver.status().declared_delay_ms - 80.0) < 1e-9);
    IrWavDataV1 mono_ir = decoded_ir.data;
    mono_ir.channels = 1U;
    mono_ir.interleaved_samples = {1.0F, 0.0F};
    IrConvolverV1 broadcast_convolver;
    CHECK(prepare_ir_convolver_from_wav_v1(broadcast_convolver, mono_ir,
                                           ir_phase_resolution, 2U) &&
          broadcast_convolver.status().channels == 2U &&
          broadcast_convolver.status().kernel_channels == 1U);
    std::array<float, 4> decoded_block{1.0F, 1.0F, 0.0F, 0.0F};
    CHECK(decoded_convolver.process_interleaved(decoded_block.data(), 2U, 2U));
    auto pcm16_wav = wav;
    pcm16_wav.resize(52U);
    const auto write_test_u16 = [&pcm16_wav](const std::size_t offset, const std::uint16_t value) {
        pcm16_wav[offset] = static_cast<std::uint8_t>(value & 0xffU);
        pcm16_wav[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    };
    const auto write_test_u32 = [&pcm16_wav](const std::size_t offset, const std::uint32_t value) {
        for (std::size_t shift = 0U; shift < 32U; shift += 8U)
            pcm16_wav[offset + (shift / 8U)] = static_cast<std::uint8_t>(value >> shift);
    };
    write_test_u32(4U, 44U);
    write_test_u16(20U, 1U); // signed PCM
    write_test_u32(28U, 192000U);
    write_test_u16(32U, 4U);
    write_test_u16(34U, 16U);
    write_test_u32(40U, 8U);
    write_test_u16(44U, 32767U);
    write_test_u16(46U, static_cast<std::uint16_t>(-32768));
    write_test_u16(48U, 0U);
    write_test_u16(50U, 16384U);
    const auto decoded_pcm16 = decode_ir_wav_v1(pcm16_wav);
    CHECK(decoded_pcm16.valid && decoded_pcm16.data.frames() == 2U &&
          std::abs(decoded_pcm16.data.interleaved_samples[0] - (32767.0F / 32768.0F)) < 1e-5F &&
          std::abs(decoded_pcm16.data.interleaved_samples[1] + 1.0F) < 1e-6F);
    auto nonfinite_wav = wav;
    nonfinite_wav[44U] = 0x00U;
    nonfinite_wav[45U] = 0x00U;
    nonfinite_wav[46U] = 0xc0U;
    nonfinite_wav[47U] = 0x7fU;
    CHECK(!decode_ir_wav_v1(nonfinite_wav).valid);
    auto malformed_container = wav;
    malformed_container[4U] = 0xffU;
    malformed_container[5U] = 0xffU;
    malformed_container[6U] = 0xffU;
    malformed_container[7U] = 0x7fU;
    CHECK(!decode_ir_wav_v1(malformed_container).valid);
    auto malformed_wav = wav;
    malformed_wav[34U] = 16U;
    CHECK(!decode_ir_wav_v1(malformed_wav).valid);

    const std::vector<float> phase_source = {
        0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    const auto minimum_resolution = resolve_ir_phase_policy(
        IrPhasePolicyV1{1U, IrPhaseMode::MinimumPhase, 1.0});
    const auto minimum_kernel = build_ir_phase_kernel_v1(
        phase_source, 8U, 2U, 48000U, minimum_resolution);
    CHECK(minimum_kernel.valid && minimum_kernel.channel_major.size() == phase_source.size() &&
          std::abs(minimum_kernel.channel_major[0] - 1.0F) < 1.0e-4F &&
          std::abs(minimum_kernel.channel_major[3]) < 1.0e-4F &&
          std::abs(minimum_kernel.channel_major[8] - 1.0F) < 1.0e-4F &&
          std::abs(minimum_kernel.channel_major[9]) < 1.0e-4F);
    const auto linear_resolution = resolve_ir_phase_policy(
        IrPhasePolicyV1{1U, IrPhaseMode::LinearPhase, 1.0});
    const auto linear_kernel = build_ir_phase_kernel_v1(
        phase_source, 8U, 2U, 48000U, linear_resolution);
    CHECK(linear_kernel.valid && std::abs(linear_kernel.channel_major[4] - 1.0F) < 1.0e-4F &&
          std::abs(linear_kernel.channel_major[12] - 1.0F) < 1.0e-4F);
    const auto mixed_source_resolution = resolve_ir_phase_policy(
        IrPhasePolicyV1{1U, IrPhaseMode::MixedPhase, 0.0});
    const auto mixed_source = build_ir_phase_kernel_v1(
        phase_source, 8U, 2U, 48000U, mixed_source_resolution);
    CHECK(mixed_source.valid && mixed_source.channel_major == phase_source);
    const auto bypass_resolution = resolve_ir_phase_policy(
        IrPhasePolicyV1{1U, IrPhaseMode::Bypass, 0.0});
    const auto bypass_kernel = build_ir_phase_kernel_v1(
        phase_source, 8U, 2U, 48000U, bypass_resolution);
    CHECK(bypass_kernel.valid && bypass_kernel.channel_major == phase_source);
    IrConvolverV1 bypass_convolver;
    CHECK(!prepare_ir_convolver_from_wav_v1(bypass_convolver, decoded_ir.data,
                                            bypass_resolution));

    AudioEngineModel ir_graph_engine;
    GraphConfigV1 ir_graph;
    ir_graph.lanes.push_back(LaneConfigV1{"ir-lane", "main", 2, 0.0, true});
    CHECK(ir_graph_engine.prepare_graph(ir_graph, 1U) && ir_graph_engine.commit_graph());
    CHECK(ir_graph_engine.prepare_ir("main", decoded_ir.data, ir_phase_resolution));
    CHECK(!ir_graph_engine.has_active_ir("main"));
    CHECK(ir_graph_engine.commit_ir() && ir_graph_engine.has_active_ir("main"));
    const auto attached_ir_status = ir_graph_engine.ir_status("main");
    CHECK(attached_ir_status.valid && attached_ir_status.sample_rate == 48000U &&
          attached_ir_status.channels == 2U && attached_ir_status.uses_fir);
    CHECK(ir_graph_engine.apply_windows_volume(VolumeNotificationV1{0.0, false, 1U}) ==
          VolumeNotificationResult::Accepted);
    std::array<float, 1024> ir_graph_input{};
    ir_graph_input[0] = 1.0F;
    ir_graph_input[1] = -1.0F;
    std::array<float, 1024> ir_graph_output{};
    const RtLaneInputV1 ir_graph_input_view{ir_graph_input.data(), 2U};
    CHECK(ir_graph_engine.process(std::span<const RtLaneInputV1>(&ir_graph_input_view, 1U),
                                  ir_graph_output.data(), 512U));
    CHECK(std::all_of(ir_graph_output.begin(), ir_graph_output.end(),
                      [](const float value) { return std::isfinite(value); }) &&
          std::any_of(ir_graph_output.begin(), ir_graph_output.end(),
                      [](const float value) { return std::abs(value) > 1.0e-6F; }));
    EngineControlWorkerV1 ir_scene_worker(ir_graph_engine);
    ControlCommandV1 ir_scene_command{};
    ir_scene_command.type = IpcMessageType::SceneApply;
    std::array<std::uint8_t, kSceneApplyPayloadBytesV1> ir_scene_payload{};
    CHECK(encode_scene_apply_payload_v1("game", "main", ir_scene_payload) &&
          decode_scene_apply_payload_v1(ir_scene_payload, ir_scene_command.scene) &&
          ir_scene_worker.consume(ir_scene_command) == EngineControlResultV1::Applied &&
          !ir_graph_engine.has_active_ir("main"));
    CHECK(ir_graph_engine.prepare_graph(ir_graph, 2U));
    CHECK(ir_graph_engine.commit_graph());
    CHECK(ir_graph_engine.prepare_ir("main", decoded_ir.data, ir_phase_resolution));
    CHECK(ir_graph_engine.commit_ir() && ir_graph_engine.has_active_ir("main"));
    CHECK(!ir_graph_engine.prepare_ir(
        "main", decoded_ir.data,
        resolve_ir_phase_policy(IrPhasePolicyV1{1U, IrPhaseMode::Bypass, 0.0})));
    ir_graph_engine.rollback_ir();

    AudioEngineModel ir_keep_engine;
    GraphConfigV1 ir_keep_graph;
    ir_keep_graph.lanes.push_back(LaneConfigV1{"ir-keep-lane", "main", 2, 0.0, true});
    CHECK(ir_keep_engine.prepare_graph(ir_keep_graph, 1U) && ir_keep_engine.commit_graph());
    EngineControlWorkerV1 ir_keep_worker(ir_keep_engine);
    ControlCommandV1 ir_keep_command{};
    ir_keep_command.type = IpcMessageType::SceneApply;
    std::array<std::uint8_t, kSceneApplyPayloadBytesV1> ir_keep_payload{};
    CHECK(encode_scene_apply_payload_v1("movie", "main", ir_keep_payload));
    CHECK(decode_scene_apply_payload_v1(ir_keep_payload, ir_keep_command.scene));
    CHECK(ir_keep_worker.consume(ir_keep_command) == EngineControlResultV1::Applied &&
          !ir_keep_engine.has_active_ir("main") &&
          ir_keep_engine.ir_transaction_idle());
    auto referenced_movie = make_easy_scene(EasySceneKind::Movie, "main");
    referenced_movie.scene.ir_reference = "studio-calibration-a";
    CHECK(validate_scene(referenced_movie.scene));
    SceneCatalogV1 ir_keep_catalog;
    SceneDefinitionV1 referenced_definition;
    referenced_definition.scene = referenced_movie.scene;
    referenced_definition.scene.id = "movie-ref-a";
    referenced_definition.scene.name = "Referenced Movie";
    referenced_definition.graph = std::move(referenced_movie.graph);
    referenced_definition.loudness = std::move(referenced_movie.loudness);
    CHECK(ir_keep_catalog.upsert(referenced_definition) == SceneCatalogResultV1::Applied);
    ir_keep_worker.set_scene_catalog(&ir_keep_catalog);
    CHECK(encode_scene_apply_payload_v1("movie-ref-a", "main", ir_keep_payload));
    CHECK(decode_scene_apply_payload_v1(ir_keep_payload, ir_keep_command.scene));
    const auto referenced_apply = ir_keep_worker.consume(ir_keep_command);
    CHECK(referenced_apply == EngineControlResultV1::Applied);
    CHECK(!ir_keep_engine.has_active_ir("main"));
    CHECK(ir_keep_worker.active_scene().id == "movie-ref-a");
    CHECK(ir_keep_worker.active_scene().ir_reference == "studio-calibration-a");
    CHECK(ir_keep_engine.prepare_ir("main", decoded_ir.data, ir_phase_resolution));
    CHECK(ir_keep_engine.commit_ir() && ir_keep_engine.has_active_ir("main"));
    CHECK(encode_scene_apply_payload_v1("movie-ref-a", "main", ir_keep_payload));
    CHECK(decode_scene_apply_payload_v1(ir_keep_payload, ir_keep_command.scene));
    const auto referenced_reapply = ir_keep_worker.consume(ir_keep_command);
    CHECK(referenced_reapply == EngineControlResultV1::Applied);
    CHECK(ir_keep_engine.has_active_ir("main"));
    CHECK(ir_keep_engine.ir_transaction_idle());
    CHECK(ir_keep_worker.active_scene().ir_reference == "studio-calibration-a");
    CHECK(encode_scene_apply_payload_v1("game", "main", ir_keep_payload));
    CHECK(decode_scene_apply_payload_v1(ir_keep_payload, ir_keep_command.scene));
    const auto game_apply = ir_keep_worker.consume(ir_keep_command);
    CHECK(game_apply == EngineControlResultV1::Applied);
    CHECK(ir_keep_engine.ir_transaction_idle());
    CHECK(!ir_keep_engine.has_active_ir("main"));
    auto second_referenced = referenced_definition;
    second_referenced.scene.id = "quiet-movie-b";
    second_referenced.scene.ir_reference = "studio-calibration-b";
    CHECK(validate_scene(second_referenced.scene) &&
          ir_keep_catalog.upsert(second_referenced) == SceneCatalogResultV1::Applied);
    CHECK(ir_keep_engine.prepare_ir("main", decoded_ir.data, ir_phase_resolution));
    CHECK(ir_keep_engine.commit_ir() && ir_keep_engine.has_active_ir("main"));
    CHECK(encode_scene_apply_payload_v1("quiet-movie-b", "main", ir_keep_payload));
    CHECK(decode_scene_apply_payload_v1(ir_keep_payload, ir_keep_command.scene));
    CHECK(ir_keep_worker.consume(ir_keep_command) == EngineControlResultV1::Applied &&
          !ir_keep_engine.has_active_ir("main"));
    CHECK(encode_scene_apply_payload_v1("quiet-game", "custom-output", ir_keep_payload));
    CHECK(decode_scene_apply_payload_v1(ir_keep_payload, ir_keep_command.scene));
    CHECK(ir_keep_worker.consume(ir_keep_command) == EngineControlResultV1::Invalid);

    AudioEngineModel engine;
    GraphConfigV1 engine_graph;
    engine_graph.lanes.push_back(LaneConfigV1{"game", "main", 2, 0.0, true});
    CHECK(engine.prepare_graph(engine_graph, 11));
    CHECK(engine.transaction_state() == EngineTransactionState::Prepared);
    engine.rollback_graph();
    CHECK(engine.transaction_state() == EngineTransactionState::Degraded);
    CHECK(engine.prepare_graph(engine_graph, 11));
    CHECK(engine.commit_graph());
    CHECK(engine.transaction_state() == EngineTransactionState::Ready);
    engine.set_sample_rate(8000U);
    CHECK(engine.apply_windows_volume(VolumeNotificationV1{-6.0206, false, 1}) ==
          VolumeNotificationResult::Accepted);
    std::array<float, 256> engine_input{};
    std::array<float, 256> engine_output{};
    for (std::size_t index = 0U; index < engine_input.size(); index += 2U) {
        engine_input[index] = 1.0F;
        engine_input[index + 1U] = -1.0F;
    }
    const RtLaneInputV1 engine_input_view{engine_input.data(), 2};
    CHECK(engine.process(std::span<const RtLaneInputV1>(&engine_input_view, 1),
                         engine_output.data(), 128U));
    CHECK(std::abs(engine_output[254] - 0.5F) < 1e-5F);
    CHECK(std::abs(engine_output[255] + 0.5F) < 1e-5F);
    engine.set_sample_rate(48000U);
    CHECK(hibiki_driver_stream_packet_encode_v1(
              driver_packet.data(), driver_packet.size(), HIBIKI_DRIVER_STREAM_RENDER_V1,
              43U, "8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10", 2U, 48000U, 2U, 0U, 10U,
              driver_samples, &driver_packet_bytes) == 1);
    const auto engine_driver_packet =
        std::span<const std::uint8_t>(driver_packet.data(), driver_packet_bytes);
    std::array<float, 4> engine_driver_storage{};
    std::array<RtLaneInputV1, 1> engine_driver_inputs{};
    std::array<float, 4> engine_driver_output{};
    CHECK(engine.process_driver_stream_packet(
        0U, "8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10", engine_driver_packet,
        engine_driver_storage, engine_driver_inputs, engine_driver_output.data()));
    const float engine_driver_nan = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(driver_packet.data() + HIBIKI_DRIVER_STREAM_HEADER_BYTES_V1,
                &engine_driver_nan, sizeof(engine_driver_nan));
    const auto bad_engine_driver_packet =
        std::span<const std::uint8_t>(driver_packet.data(), driver_packet_bytes);
    CHECK(!engine.process_driver_stream_packet(
        0U, "6d5706a4-b661-4bf6-9c2d-9c31b8f7df21", bad_engine_driver_packet,
        engine_driver_storage, engine_driver_inputs, engine_driver_output.data()));

    AudioEngineModel limiter_reset_engine;
    GraphConfigV1 limiter_reset_graph;
    limiter_reset_graph.lanes.push_back(LaneConfigV1{"limiter-reset", "main", 2, 0.0, true});
    CHECK(limiter_reset_engine.prepare_graph(limiter_reset_graph, 20U) &&
          limiter_reset_engine.commit_graph());
    limiter_reset_engine.set_sample_rate(48000U);
    CHECK(limiter_reset_engine.apply_windows_volume(
              VolumeNotificationV1{0.0, false, 1U}) == VolumeNotificationResult::Accepted);
    // The 512-frame warm-up spans the 8 ms Group Master ramp at 48 kHz, so the
    // second block differs only by the committed limiter state.
    std::array<float, 1024> limiter_loud_input{};
    std::array<float, 1024> limiter_loud_output{};
    for (std::size_t index = 0U; index < limiter_loud_input.size(); index += 2U) {
        limiter_loud_input[index] = 2.0F;
        limiter_loud_input[index + 1U] = -2.0F;
    }
    const RtLaneInputV1 limiter_loud_view{limiter_loud_input.data(), 2};
    CHECK(limiter_reset_engine.process(
        std::span<const RtLaneInputV1>(&limiter_loud_view, 1),
        limiter_loud_output.data(), 512U));
    // A 2.0 peak exceeds the -1 dBTP ceiling, so the guard must attenuate.
    CHECK(std::abs(limiter_loud_output[0]) < 1.0F);
    CHECK(limiter_reset_engine.prepare_graph(limiter_reset_graph, 21U));
    CHECK(limiter_reset_engine.commit_graph());
    std::array<float, 64> limiter_quiet_input{};
    std::array<float, 64> limiter_quiet_output{};
    for (std::size_t index = 0U; index < limiter_quiet_input.size(); index += 2U) {
        limiter_quiet_input[index] = 0.001F;
        limiter_quiet_input[index + 1U] = -0.001F;
    }
    const RtLaneInputV1 limiter_quiet_view{limiter_quiet_input.data(), 2};
    CHECK(limiter_reset_engine.process(
        std::span<const RtLaneInputV1>(&limiter_quiet_view, 1),
        limiter_quiet_output.data(), 32U));
    // Graph commit starts the limiter at unity gain; attenuation carried over
    // from the loud graph must not scale the first quiet block.
    for (std::size_t index = 0U; index < limiter_quiet_output.size(); ++index) {
        CHECK(limiter_quiet_output[index] == limiter_quiet_input[index]);
    }
    engine.set_sample_rate(8000U);
    CHECK(engine.prepare_output_fanout(fanout_plan, 1.0));
    std::array<float, 16> engine_fanout_a{};
    std::array<float, 16> engine_fanout_b{};
    std::array<float, 16> engine_fanout_disabled{};
    std::array<float*, 3> engine_fanout_outputs{{engine_fanout_a.data(), engine_fanout_b.data(),
                                                  engine_fanout_disabled.data()}};
    const std::array<std::size_t, 3> engine_fanout_capacities{{16U, 16U, 16U}};
    std::array<std::size_t, 3> engine_fanout_frames{};
    CHECK(engine.process_output_group_fanout(
              "main", std::span<const RtLaneInputV1>(&engine_input_view, 1),
              engine_output.data(), 2U, engine_fanout_outputs, engine_fanout_capacities,
              engine_fanout_frames) &&
          engine_fanout_frames[0] == 1U && engine_fanout_frames[1] == 1U &&
          engine_fanout_frames[2] == 0U && engine_fanout_a[0] == engine_fanout_b[0]);
    CHECK(engine.observe_output_fanout_clock(0U, 48000.0, 48012.0, 1.0));
    CHECK(engine.output_fanout_snapshot().sinks[0].drift_ppm > 0.0);
    CHECK(engine.apply_windows_volume(VolumeNotificationV1{-6.0206, true, 2}) ==
          VolumeNotificationResult::Accepted);
    CHECK(engine.process(std::span<const RtLaneInputV1>(&engine_input_view, 1),
                         engine_output.data(), 128U));
    CHECK(engine_output[254] == 0.0F && engine_output[255] == 0.0F);
    CHECK(engine.apply_windows_volume(VolumeNotificationV1{-6.0206, false, 3}) ==
          VolumeNotificationResult::Accepted);
    CHECK(engine.process(std::span<const RtLaneInputV1>(&engine_input_view, 1),
                         engine_output.data(), 128U));
    CHECK(std::abs(engine_output[254] - 0.5F) < 1e-5F);
    CHECK(engine.apply_windows_volume(VolumeNotificationV1{-6.0206, false, 3}) ==
          VolumeNotificationResult::Accepted);

    auto group_engine = std::make_unique<AudioEngineModel>();
    GraphConfigV1 group_engine_graph;
    group_engine_graph.lanes.push_back(LaneConfigV1{"main-lane", "main", 2, 0.0, true});
    group_engine_graph.lanes.push_back(LaneConfigV1{"movie-lane", "movie", 2, 0.0, true});
    CHECK(group_engine->prepare_graph(group_engine_graph, 1U) && group_engine->commit_graph());
    group_engine->set_sample_rate(8000U);
    CHECK(group_engine->apply_windows_volume("main", VolumeNotificationV1{-6.0206, false, 1U}) ==
          VolumeNotificationResult::Accepted);
    CHECK(group_engine->apply_windows_volume("movie", VolumeNotificationV1{-12.0412, false, 1U}) ==
          VolumeNotificationResult::Accepted);
    std::array<float, 256> group_engine_main_input{};
    std::array<float, 256> group_engine_movie_input{};
    std::array<RtLaneInputV1, 2> group_engine_inputs{{
        RtLaneInputV1{group_engine_main_input.data(), 2U},
        RtLaneInputV1{group_engine_movie_input.data(), 2U}}};
    for (std::size_t index = 0U; index < group_engine_main_input.size(); index += 2U) {
        group_engine_main_input[index] = 1.0F;
        group_engine_main_input[index + 1U] = -1.0F;
        group_engine_movie_input[index] = 1.0F;
        group_engine_movie_input[index + 1U] = -1.0F;
    }
    std::array<float, 256> group_engine_output{};
    CHECK(group_engine->process_output_group("main", group_engine_inputs,
                                           group_engine_output.data(), 128U));
    CHECK(std::abs(group_engine_output[254] - 0.5F) < 1e-5F &&
          std::abs(group_engine_output[255] + 0.5F) < 1e-5F);
    CHECK(group_engine->process_output_group("movie", group_engine_inputs,
                                           group_engine_output.data(), 128U));
    CHECK(std::abs(group_engine_output[254] - 0.25F) < 1e-5F &&
          std::abs(group_engine_output[255] + 0.25F) < 1e-5F);

    // Per-group limiter isolation: a loud transient in movie must leave its
    // bounded recovery state attached to movie only, so the next quiet main
    // block is not ducked by protection engaged for a different sink.
    auto isolated_limiter_engine = std::make_unique<AudioEngineModel>();
    GraphConfigV1 isolated_limiter_graph;
    isolated_limiter_graph.lanes.push_back(
        LaneConfigV1{"isolated-main-lane", "main", 2, 0.0, true});
    isolated_limiter_graph.lanes.push_back(
        LaneConfigV1{"isolated-movie-lane", "movie", 2, 0.0, true});
    CHECK(isolated_limiter_engine->prepare_graph(isolated_limiter_graph, 1U) &&
          isolated_limiter_engine->commit_graph());
    isolated_limiter_engine->set_sample_rate(48000U);
    CHECK(isolated_limiter_engine->apply_windows_volume(
              "main", VolumeNotificationV1{0.0, false, 1U}) ==
          VolumeNotificationResult::Accepted);
    CHECK(isolated_limiter_engine->apply_windows_volume(
              "movie", VolumeNotificationV1{0.0, false, 1U}) ==
          VolumeNotificationResult::Accepted);
    std::array<float, 8> isolated_quiet_input{};
    for (std::size_t index = 0U; index < isolated_quiet_input.size(); index += 2U) {
        isolated_quiet_input[index] = 0.001F;
        isolated_quiet_input[index + 1U] = -0.001F;
    }
    std::array<float, 8> isolated_quiet_output{};
    const RtLaneInputV1 isolated_main_view{isolated_quiet_input.data(), 2U};
    const RtLaneInputV1 isolated_movie_view{isolated_quiet_input.data(), 2U};
    const std::array<RtLaneInputV1, 2> isolated_quiet_views{{
        isolated_main_view,
        isolated_movie_view,
    }};
    // The bank starts at -60 dB; consume the full 8 ms ramp so any remaining
    // attenuation can come only from the true-peak guard.
    for (int ramp_pass = 0; ramp_pass < 100; ++ramp_pass) {
        CHECK(isolated_limiter_engine->process_output_group(
            "main", isolated_quiet_views,
            isolated_quiet_output.data(), 4U));
        CHECK(isolated_limiter_engine->process_output_group(
            "movie", isolated_quiet_views,
            isolated_quiet_output.data(), 4U));
    }

    std::array<float, 8> isolated_loud_input{};
    std::array<float, 8> isolated_movie_after_loud{};
    for (std::size_t index = 0U; index < isolated_loud_input.size(); index += 2U) {
        isolated_loud_input[index] = 2.0F;
        isolated_loud_input[index + 1U] = -2.0F;
    }
    const RtLaneInputV1 isolated_loud_view{isolated_loud_input.data(), 2U};
    const std::array<RtLaneInputV1, 2> isolated_loud_views{{
        isolated_main_view,
        isolated_loud_view,
    }};
    CHECK(isolated_limiter_engine->process_output_group(
        "movie", isolated_loud_views,
        isolated_movie_after_loud.data(), 4U));
    CHECK(std::abs(isolated_movie_after_loud[0]) < 1.0F);

    std::array<float, 8> isolated_main_after_movie_loud{};
    CHECK(isolated_limiter_engine->process_output_group(
        "main", isolated_quiet_views,
        isolated_main_after_movie_loud.data(), 4U));
    for (std::size_t index = 0U; index < isolated_main_after_movie_loud.size();
         ++index) {
        CHECK(isolated_main_after_movie_loud[index] ==
              isolated_quiet_input[index]);
    }

    // Multi-group render clock: a delayed lane must advance once per render,
    // even while another output group is selected, so its fixed ring stays on
    // the shared audio timeline instead of returning stale alignment.
    auto clocked_group_engine = std::make_unique<AudioEngineModel>();
    GraphConfigV1 clocked_group_graph;
    clocked_group_graph.lanes.push_back(LaneConfigV1{"clock-delay", "main", 2, 0.0, true});
    clocked_group_graph.lanes.push_back(LaneConfigV1{"clock-reference", "main", 2, 0.0, true});
    clocked_group_graph.lanes.push_back(LaneConfigV1{"clock-movie", "movie", 2, 0.0, true});
    clocked_group_graph.lanes[0].reported_latency_samples = 0U;
    clocked_group_graph.lanes[1].reported_latency_samples = 2U;
    CHECK(clocked_group_engine->prepare_graph(clocked_group_graph, 2U) &&
          clocked_group_engine->commit_graph());
    CHECK(clocked_group_engine->apply_windows_volume(
              "main", VolumeNotificationV1{12.0, false, 1U}) ==
          VolumeNotificationResult::Accepted);
    CHECK(clocked_group_engine->apply_windows_volume(
              "movie", VolumeNotificationV1{0.0, false, 1U}) ==
          VolumeNotificationResult::Accepted);
    clocked_group_engine->set_sample_rate(8000U);
    std::array<float, 2U * 128U> clock_prime_input{};
    std::array<float, 2U * 128U> clock_prime_output{};
    const std::array<RtLaneInputV1, 3> clock_prime_views{{
        {clock_prime_input.data(), 2U},
        {clock_prime_input.data(), 2U},
        {clock_prime_input.data(), 2U}}};
    for (int prime_pass = 0; prime_pass < 2; ++prime_pass) {
        CHECK(clocked_group_engine->process_output_group(
            "movie", clock_prime_views, clock_prime_output.data(), 128U));
        // The silent reference lane keeps the shared limiter state quiet so
        // the delayed impulse is not rescaled between grouped render calls.
        CHECK(clocked_group_engine->process_output_group(
            "main", clock_prime_views, clock_prime_output.data(), 128U));
    }
    std::array<float, 2> clock_movie_input{0.3F, -0.3F};
    std::array<float, 2> clock_reference_input{0.0F, 0.0F};
    const std::array<std::array<float, 2>, 4> clock_delay_inputs{{
        {0.5F, -0.5F}, {0.1F, -0.1F}, {0.2F, -0.2F}, {0.15F, -0.15F}}};
    std::array<float, 2> clock_output{};
    const std::array<RtLaneInputV1, 3> clock_inputs_cb1{{
        {clock_delay_inputs[0].data(), 2U},
        {clock_reference_input.data(), 2U},
        {clock_movie_input.data(), 2U}}};
    CHECK(clocked_group_engine->process_output_group(
        "movie", clock_inputs_cb1, clock_output.data(), 1U));
    CHECK(std::abs(clock_output[0] - 0.3F) < 1e-6F &&
          std::abs(clock_output[1] + 0.3F) < 1e-6F);
    for (std::size_t clock_step = 1U; clock_step < 4U; ++clock_step) {
        const std::array<RtLaneInputV1, 3> clock_inputs{{
            {clock_delay_inputs[clock_step].data(), 2U},
            {clock_reference_input.data(), 2U},
            {clock_movie_input.data(), 2U}}};
        const float expected_clock_sample =
            clock_step == 2U ? clock_delay_inputs[0][0]
                             : (clock_step == 3U ? clock_delay_inputs[1][0] : 0.0F);
        CHECK(clocked_group_engine->process_output_group(
            "main", clock_inputs, clock_output.data(), 1U));
        // The two-sample ring needs two displacement reads: the interleaved
        // movie callback consumes the first, and the first main render consumes
        // the second. The cb1 impulse therefore re-emerges on the second main
        // render, exactly on the shared audio timeline.
        CHECK(std::abs(clock_output[0] - expected_clock_sample) < 1e-6F &&
              std::abs(clock_output[1] + expected_clock_sample) < 1e-6F);
    }

    std::vector<RtLaneInputV1> tab_lane_inputs(1);
    float tab_lane_input[8]{};
    float tab_lane_output[8]{};
    TabCaptureBlockV1 tab_lane_block{};
    ProgramAwareLevelControllerV1 tab_program_level;
    CHECK(tab_program_level.configure(
        ProgramAwareLevelPolicyV1{1, true, -23.0, 6.0, 12.0, 3000.0, 60.0, -70.0}, 48000U));
    PeqProcessorV1 tab_peq;
    CHECK(tab_peq.prepare(peq_filters, 48000U, 2U));
    auto tab_ir = std::make_unique<IrConvolverV1>();
    CHECK(tab_ir->prepare(ir_kernel, 2U, 1U, 2U, 48000U, ir_phase));
    BasicNoiseSuppressorV1 tab_noise;
    CHECK(tab_noise.configure(
        BasicNoiseSuppressorPolicyV1{1, true, -45.0, -24.0, 8.0, 120.0, 80.0}, 48000U, 2U));
    TabLaneEffectsV1 tab_effects{&tab_peq, tab_ir.get(), &tab_noise, &tab_program_level};
    CHECK(process_tab_capture_lane_v1(engine, 0, *tab_queue, tab_lane_input, 2U,
                                      tab_lane_inputs, tab_lane_output, 2U, tab_lane_block,
                                      &tab_effects));
    CHECK(tab_lane_block.frames == 2U && tab_lane_block.channels == 2U &&
          std::isfinite(tab_lane_output[0]) && std::isfinite(tab_lane_output[1]) &&
          tab_lane_output[0] > 0.125F && tab_lane_output[0] < 0.2F &&
          tab_lane_output[1] < -0.125F && tab_lane_output[1] > -0.2F);
    auto tab_wasapi_queue = std::make_unique<TabCaptureQueueV1>();
    const TabCapturePacketViewV1 tab_wasapi_view{
        2U, 2U, 48000U, reinterpret_cast<const std::uint8_t*>(tab_samples), 4U};
    CHECK(tab_wasapi_queue->push(tab_wasapi_view));
    TabCaptureBlockV1 tab_wasapi_block{};
    CHECK(!process_tab_capture_lane_to_wasapi_v1(
        engine, 0U, *tab_wasapi_queue, tab_lane_input, 2U, tab_lane_inputs, tab_lane_output, 2U,
        tab_wasapi_block, &tab_effects));
    CHECK(tab_wasapi_block.frames == 0U && tab_wasapi_block.channels == 0U);
    VirtualMicRouteModel lane_mic;
    CHECK(lane_mic.prepare(VirtualMicConfigV1{2U, 48000U, true}));
    float lane_mic_input[4] = {0.75F, -0.75F, 0.5F, -0.5F};
    float lane_mic_capture[4]{};
    float lane_mic_output[4]{};
    std::vector<RtLaneInputV1> mic_lane_inputs(1);
    CHECK(process_virtual_mic_lane_v1(engine, lane_mic, 0, lane_mic_input, 2U, lane_mic_capture,
                                      2U, mic_lane_inputs, lane_mic_output, 2U, 2U));
    CHECK(lane_mic_output[0] == 0.0F && lane_mic_output[1] == 0.0F);
    lane_mic.set_privacy_mute(false);
    CHECK(process_virtual_mic_lane_v1(engine, lane_mic, 0, lane_mic_input, 2U, lane_mic_capture,
                                      2U, mic_lane_inputs, lane_mic_output, 2U, 2U));
    CHECK(std::abs(lane_mic_output[0] - 0.375F) < 1e-5F &&
          std::abs(lane_mic_output[1] + 0.375F) < 1e-5F);
    lane_mic.set_privacy_mute(true);
    CHECK(!process_virtual_mic_lane_to_wasapi_v1(
        engine, lane_mic, 0U, lane_mic_input, 2U, lane_mic_capture, 2U, mic_lane_inputs,
        lane_mic_output, 2U, 2U));
#if defined(_WIN32)
    const std::wstring contract_mapping_name =
        L"Local\\HibikiDSP_v1_contract_asio_" + std::to_wstring(_getpid());
    const wchar_t* const kContractMapping = contract_mapping_name.c_str();
    CHECK(engine.bind_asio_transport(kContractMapping, 2U, 48000U, 4U));
    CHECK(engine.asio_transport_bound());
    const auto contract_bytes = hibiki_asio_transport_region_size_v1();
    HANDLE contract_mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE,
                                                kContractMapping);
    CHECK(contract_mapping != nullptr);
    auto* contract_region = static_cast<hibiki_asio_transport_region_v1*>(
        MapViewOfFile(contract_mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, contract_bytes));
    CHECK(contract_region != nullptr);
    const float contract_left[4] = {1.0F, 2.0F, 3.0F, 4.0F};
    const float contract_right[4] = {-1.0F, -2.0F, -3.0F, -4.0F};
    const float* contract_planar[2] = {contract_left, contract_right};
    CHECK(hibiki_asio_transport_push_planar_v1(contract_region, contract_bytes, contract_planar,
                                                2U, 4U) == 1);
    std::vector<RtLaneInputV1> asio_inputs(1);
    float contract_asio_transport[8]{};
    float contract_asio_output[8]{};
    AsioTransportBlockV1 asio_block{};
    CHECK(engine.process_asio_transport(0, contract_asio_transport, 4U, asio_inputs,
                                        contract_asio_output, 4U,
                                        asio_block));
    CHECK(asio_block.frames == 4U && asio_block.channels == 2U &&
          std::abs(contract_asio_output[0] - (0.5F * 0.8912509F / 2.0F)) < 1e-5F &&
          std::abs(contract_asio_output[1] + (0.5F * 0.8912509F / 2.0F)) < 1e-5F);
    UnmapViewOfFile(contract_region);
    CloseHandle(contract_mapping);
    engine.unbind_asio_transport();
    CHECK(!engine.asio_transport_bound());
#endif
    AsioTransportBlockV1 detached_block{};
    std::vector<RtLaneInputV1> detached_inputs(1);
    float detached_transport[8]{};
    float detached_output[8]{};
    CHECK(!engine.asio_transport_bound());
    CHECK(!engine.process_asio_transport(0, detached_transport, 4U, detached_inputs,
                                         detached_output, 4U, detached_block));
    GraphConfigV1 invalid_graph;
    CHECK(!engine.prepare_graph(invalid_graph, 12));
    CHECK(engine.transaction_state() == EngineTransactionState::Degraded);
    engine.rollback_graph();
    CHECK(engine.transaction_state() == EngineTransactionState::Ready);

#if defined(_WIN32)
    WindowsVolumeBroker volume_broker;
    CHECK(!volume_broker.is_bound());
    WindowsVolumeNotificationSnapshotV1 no_notification;
    CHECK(!volume_broker.poll(no_notification));
    struct NotificationWithEightChannels {
        GUID guidEventContext;
        BOOL bMuted;
        float fMasterVolume;
        UINT nChannels;
        float afChannelVolumes[8];
    } notification{};
    notification.guidEventContext.Data1 = 0x12345678U;
    notification.bMuted = FALSE;
    notification.fMasterVolume = -12.0F;
    notification.nChannels = 2U;
    notification.afChannelVolumes[0] = 0.5F;
    notification.afChannelVolumes[1] = 0.25F;
    auto* callback = new WindowsVolumeCallback();
    CHECK(callback->OnNotify(reinterpret_cast<AUDIO_VOLUME_NOTIFICATION_DATA*>(&notification)) ==
          S_OK);
    WindowsVolumeNotificationSnapshotV1 callback_snapshot;
    CHECK(callback->read(callback_snapshot));
    CHECK(std::abs(callback_snapshot.requested_db + 12.0) < 1e-6 &&
          callback_snapshot.channel_count == 2U &&
          callback_snapshot.channel_scalars[1] == 0.25F &&
          callback_snapshot.event_context.Data1 == 0x12345678U);
    CHECK(callback->Release() == 0U);
    AudioEngineModel linked_engine;
    WindowsVolumeLinkV1 volume_link;
    GUID hibiki_write_context{};
    hibiki_write_context.Data1 = 0xABCDEF01U;
    CHECK(volume_link.add_ignored_context(hibiki_write_context));
    WindowsVolumeNotificationSnapshotV1 external_snapshot{};
    external_snapshot.generation = 1U;
    external_snapshot.requested_db = -18.0;
    external_snapshot.event_context.Data1 = 0x01020304U;
    CHECK(volume_link.apply(linked_engine, "main", external_snapshot) ==
          WindowsVolumeSyncResultV1::Applied);
    CHECK(std::abs(linked_engine.volume().requested_db + 18.0) < 1e-9 &&
          linked_engine.volume().generation == 1U);
    external_snapshot.event_context = WindowsVolumeEventContextsV1::ui();
    external_snapshot.generation = 2U;
    CHECK(volume_link.apply(linked_engine, "main", external_snapshot) ==
          WindowsVolumeSyncResultV1::IgnoredSelf &&
          linked_engine.volume().generation == 1U);
    external_snapshot.event_context = hibiki_write_context;
    CHECK(volume_link.add_ignored_context(hibiki_write_context));
    CHECK(volume_link.apply(linked_engine, "main", external_snapshot) ==
          WindowsVolumeSyncResultV1::IgnoredSelf);
    volume_link.clear_ignored_contexts();
    external_snapshot.event_context = GUID{};
    external_snapshot.generation = 3U;
    external_snapshot.requested_db = -6.0;
    CHECK(volume_link.apply(linked_engine, "main", external_snapshot) ==
          WindowsVolumeSyncResultV1::Applied &&
          linked_engine.volume().generation == 3U);
    external_snapshot.event_context = GUID{};
    external_snapshot.generation = 1U;
    CHECK(volume_link.apply(linked_engine, "main", external_snapshot) ==
          WindowsVolumeSyncResultV1::StaleGeneration);
    external_snapshot.generation = 4U;
    external_snapshot.requested_db = 24.0;
    CHECK(volume_link.apply(linked_engine, "main", external_snapshot) ==
          WindowsVolumeSyncResultV1::Invalid);
    auto* watcher = new WindowsDeviceWatcher();
    CHECK(watcher->OnDefaultDeviceChanged(eRender, eConsole, L"hibiki-endpoint") == S_OK);
    WindowsDeviceChangeSnapshotV1 device_change;
    CHECK(watcher->poll(device_change));
    CHECK(device_change.kind == WindowsDeviceChangeKind::DefaultChanged &&
          device_change.flow == eRender && device_change.endpoint_id[0] == L'h');
    CHECK(watcher->Release() == 0U);
    WindowsPhysicalDeviceCatalogWorker catalog_worker;
    PhysicalDeviceCatalogV1 worker_catalog;
    std::uint64_t worker_sequence = 0U;
    std::array<std::uint8_t, kDeviceCatalogSnapshotPayloadBytesV1> worker_payload{};
    std::size_t worker_payload_bytes = 99U;
    CHECK(catalog_worker.bind(nullptr) == E_INVALIDARG &&
          catalog_worker.refresh(worker_catalog, worker_sequence) == E_UNEXPECTED &&
          catalog_worker.refresh_snapshot(worker_catalog, worker_sequence, worker_payload,
                                          worker_payload_bytes) == E_UNEXPECTED &&
          worker_payload_bytes == 0U && worker_catalog.size() == 0U);
    WindowsPhysicalDeviceCatalogCoordinator catalog_coordinator;
    CHECK(catalog_coordinator.bind(nullptr) == E_INVALIDARG &&
          catalog_coordinator.refresh_now(worker_catalog, worker_sequence, worker_payload,
                                          worker_payload_bytes) == E_UNEXPECTED &&
          catalog_coordinator.poll_and_refresh(worker_catalog, worker_sequence, worker_payload,
                                               worker_payload_bytes) == E_UNEXPECTED);
    WindowsPhysicalDeviceCatalogServiceV1 catalog_service;
    IpcFrameV1 service_catalog_response;
    CHECK(catalog_service.bind(nullptr) == E_INVALIDARG &&
          catalog_service.refresh_now() == E_UNEXPECTED &&
          catalog_service.poll_and_refresh() == E_UNEXPECTED &&
          !catalog_service.has_snapshot() &&
          !device_catalog_snapshot_reply_v1(service_catalog_response,
                                             catalog_service.snapshot_store()));
    WindowsControlRuntimeV1 control_runtime;
    double unbound_session_db = -4.0;
    bool unbound_session_mute = true;
    GUID session_context{};
    CHECK(!control_runtime.start(nullptr, IpcNamedPipeConfigV1{L"", 1024U, 100U}) &&
          !control_runtime.running() && control_runtime.refresh_now() == E_UNEXPECTED &&
          control_runtime.poll_and_refresh() == E_UNEXPECTED &&
          control_runtime.refresh_default_volume(nullptr) == E_INVALIDARG &&
          control_runtime.refresh_default_volume_if_changed(nullptr) == E_INVALIDARG &&
          control_runtime.write_session_volume("missing", -12.0, false, session_context) ==
              E_UNEXPECTED &&
          control_runtime.read_session_volume("missing", unbound_session_db,
                                             unbound_session_mute) == E_UNEXPECTED &&
          control_runtime.write_session_volume_handle(0x0000000200000001ULL, 1U, -12.0,
                                                      false, session_context) == E_UNEXPECTED &&
          control_runtime.read_session_volume_handle(0x0000000200000001ULL, 1U,
                                                     unbound_session_db,
                                                     unbound_session_mute) == E_UNEXPECTED &&
          control_runtime.bind_session_route_handle(0x0000000200000001ULL, 1U, "game",
                                                    "surround") == E_UNEXPECTED &&
          !control_runtime.enqueue_session_route_rule_command(session_route_rule_command));
    auto* session_watcher = new WindowsAudioSessionWatcher();
    std::uint64_t session_sequence = 0;
    CHECK(!session_watcher->poll(session_sequence));
    CHECK(session_watcher->OnSessionCreated(nullptr) == S_OK);
    CHECK(session_watcher->poll(session_sequence) && session_sequence == 1U);
    CHECK(session_watcher->write_session_volume("missing", -12.0, false, session_context) ==
          E_UNEXPECTED);
    double session_db = 0.0;
    bool session_mute = false;
    CHECK(session_watcher->read_session_volume("missing", session_db, session_mute) ==
          E_UNEXPECTED);
    CHECK(session_watcher->Release() == 0U);
    WindowsAudioSessionRouteCoordinatorV1 session_route_coordinator;
    GraphConfigV1 session_route_graph;
    ProcessLoopbackPlanV1 session_process_plan;
    CHECK(session_route_coordinator.bind(nullptr) == E_INVALIDARG &&
          session_route_coordinator.refresh() ==
              WindowsAudioSessionRouteRefreshResultV1::Unbound &&
          session_route_coordinator.poll_and_refresh() ==
              WindowsAudioSessionRouteRefreshResultV1::Unbound &&
          !session_route_coordinator.copy_graph(session_route_graph) &&
          session_route_coordinator.copy_process_loopback_plan(session_process_plan) ==
              ProcessLoopbackPlanResultV1::NoRoutes && session_process_plan.size == 0U &&
          !session_route_coordinator.snapshot().has_graph &&
          session_route_coordinator.write_session_volume("missing", -12.0, false,
                                                         session_context) == E_UNEXPECTED &&
          session_route_coordinator.read_session_volume("missing", unbound_session_db,
                                                        unbound_session_mute) == E_UNEXPECTED &&
          session_route_coordinator.write_session_volume_handle(
              0x0000000200000001ULL, -12.0, false, session_context) == E_UNEXPECTED &&
          session_route_coordinator.read_session_volume_handle(
              0x0000000200000001ULL, unbound_session_db, unbound_session_mute) == E_UNEXPECTED &&
          session_route_coordinator.bind_session_route_handle(
              0x0000000200000001ULL, "game", "surround") == E_UNEXPECTED);
    WindowsProcessLoopbackSourceV1 process_loopback;
    std::uint32_t loopback_frames = 99U;
    CHECK(process_loopback.start(WindowsProcessLoopbackConfigV1{}) == E_INVALIDARG &&
          process_loopback.snapshot().state == WindowsProcessLoopbackStateV1::Degraded &&
          !process_loopback.read(nullptr, 0U, loopback_frames) && loopback_frames == 0U);
    process_loopback.stop();
    CHECK(process_loopback.snapshot().state == WindowsProcessLoopbackStateV1::Degraded);
    WindowsProcessLoopbackBlockV1 loopback_block{};
    std::array<RtLaneInputV1, 1> loopback_inputs{};
    float loopback_input[2]{};
    float loopback_output[2]{};
    CHECK(!process_windows_process_loopback_lane_v1(
        engine, process_loopback, 0U, loopback_input, 1U, loopback_inputs, loopback_output, 1U,
        loopback_block));
    WindowsWasapiOutputV1 wasapi_output;
    CHECK(!wasapi_output.bind(WasapiOutputConfigV1{L"", 3U, 48000U, 20U}));
    CHECK(!wasapi_output.start());
    auto wasapi_worker = std::make_unique<WindowsWasapiSinkWorkerV1>();
    CHECK(!wasapi_worker->start(WasapiOutputConfigV1{L"", 3U, 48000U, 20U}, 128U));
    CHECK(!wasapi_worker->submit(nullptr, 128U, 2U));
    CHECK(!wasapi_worker->submit_scaled(nullptr, 128U, 2U, 0.5F));
    WindowsWasapiSinkHandoffV1 wasapi_handoff;
    CHECK(wasapi_handoff.state() == WasapiSinkHandoffStateV1::Unbound);
    CHECK(!wasapi_handoff.process(nullptr, 128U, 2U));
    CHECK(!wasapi_handoff.begin(WasapiOutputConfigV1{L"", 2U, 48000U, 20U}, 128U, 30U));
    CHECK(!wasapi_handoff.start_initial(WasapiOutputConfigV1{L"", 3U, 48000U, 20U}, 128U));
    CHECK(wasapi_handoff.state() == WasapiSinkHandoffStateV1::Degraded);
    wasapi_handoff.stop();
    wasapi_handoff.rollback();
    CHECK(wasapi_handoff.state() == WasapiSinkHandoffStateV1::Unbound);
    AudioEngineModel wasapi_engine;
    CHECK(!wasapi_engine.start_wasapi_output(WasapiOutputConfigV1{L"", 3U, 48000U, 20U}, 128U));
    CHECK(wasapi_engine.wasapi_output_snapshot().state == WasapiSinkHandoffStateV1::Degraded);
    CHECK(!wasapi_engine.begin_wasapi_output_handoff(
        WasapiOutputConfigV1{L"", 2U, 48000U, 20U}, 128U, 30U));
    wasapi_engine.stop_wasapi_output();
    CHECK(wasapi_engine.wasapi_output_snapshot().state == WasapiSinkHandoffStateV1::Unbound);
    std::array<RtLaneInputV1, 1U> wasapi_lane_inputs{};
    std::array<float, 256U> wasapi_transport_buffer{};
    std::array<float, 256U> wasapi_graph_buffer{};
    AsioTransportBlockV1 wasapi_asio_block{};
    CHECK(!wasapi_engine.process_asio_transport_to_wasapi(
        0U, wasapi_transport_buffer.data(), 128U, std::span<RtLaneInputV1>(wasapi_lane_inputs),
        wasapi_graph_buffer.data(), wasapi_graph_buffer.size(), wasapi_asio_block));
    CHECK(!wasapi_engine.process_driver_stream_packet_to_wasapi(
        0U, "endpoint", {}, {}, std::span<RtLaneInputV1>(wasapi_lane_inputs),
        wasapi_graph_buffer.data()));
    WindowsWasapiFanoutV1 wasapi_fanout;
    const std::array<WasapiFanoutSinkConfigV1, 2U> invalid_fanout{{
        {true, WasapiOutputConfigV1{L"endpoint-a", 2U, 48000U, 20U}},
        {true, WasapiOutputConfigV1{L"endpoint-b", 8U, 48000U, 20U}}}};
    CHECK(!wasapi_fanout.prepare(invalid_fanout, 128U));
    CHECK(wasapi_fanout.snapshot().degraded);
    wasapi_fanout.stop();
    AudioEngineModel fanout_engine;
    CHECK(!fanout_engine.prepare_wasapi_fanout(invalid_fanout, 128U));
    CHECK(fanout_engine.wasapi_fanout_snapshot().degraded);
    CHECK(!fanout_engine.process_output_group_to_wasapi_fanout(
        "main", {}, wasapi_graph_buffer.data(), 2U));
    fanout_engine.stop_wasapi_fanout();
#endif

    return 0;
}

#undef CHECK
