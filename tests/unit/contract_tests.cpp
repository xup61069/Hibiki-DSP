#include "hibiki/contracts.hpp"
#include "hibiki/control_payloads.hpp"
#include "hibiki/control_service.hpp"
#include "hibiki/device_switch.hpp"
#include "hibiki/device_recovery.hpp"
#include "hibiki/ipc.hpp"
#include "hibiki/ipc_pipe.hpp"
#include "hibiki/asio_bridge.hpp"
#include "hibiki/calibration.hpp"
#include "hibiki/plugin_host.hpp"
#include "hibiki/vst3_sandbox.hpp"
#include "hibiki/vst3_worker_protocol.hpp"
#include "hibiki/vst3_worker_pipe.hpp"
#include "hibiki/tab_bridge.hpp"
#include "hibiki/asio_transport_v1.h"
#include "hibiki/output_sink.hpp"
#include "hibiki/output_crossfade.hpp"
#include "hibiki/output_handoff.hpp"
#include "hibiki/exporters.hpp"
#include "hibiki/engine_control.hpp"
#include "hibiki/audio_engine.hpp"
#include "hibiki/audio_session_registry.hpp"

extern "C" {
#include "hibiki/driver_control_v1.h"
#include "hibiki/driver_validation_v1.h"
#include "hibiki/wavert_endpoint_state_v1.h"
}
#include "hibiki/iso226.hpp"
#include "hibiki/ir_phase.hpp"
#include "hibiki/scene_graph.hpp"
#include "hibiki/scene_presets.hpp"
#include "hibiki/scene_safety.hpp"
#include "hibiki/session_route.hpp"
#include "hibiki/volume_state.hpp"
#include "hibiki/program_loudness.hpp"
#include "hibiki/peq_dsp.hpp"
#include "hibiki/ir_convolver.hpp"
#include "hibiki/virtual_mic.hpp"
#include "hibiki/true_peak_limiter.hpp"
#if defined(_WIN32)
#include <windows.h>
#include "hibiki/windows_volume_broker.hpp"
#include "hibiki/windows_device_watcher.hpp"
#include "hibiki/windows_audio_session_watcher.hpp"
#include "hibiki/windows_wasapi_output.hpp"
#endif

#include <cmath>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
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

int main() {
    using namespace hibiki;

    SceneProfileV1 scene;
    scene.id = "game";
    scene.name = "Game";
    scene.output_group = "main";
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

    PeqProcessorV1 peq;
    const std::array<PeqFilterV1, 1> peq_filters{{PeqFilterV1{1000.0, 6.0, 1.0}}};
    CHECK(peq.prepare(peq_filters, 48000U, 2U));
    float peq_block[8] = {0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    CHECK(peq.process_interleaved(peq_block, 4U));
    CHECK(std::isfinite(peq_block[2]) && peq_block[2] > 1.0F);
    CHECK(!peq.prepare(std::span<const PeqFilterV1>(peq_filters), 48000U, 9U));

    IrConvolverV1 ir;
    const std::array<float, 2> ir_kernel{{1.0F, 0.5F}};
    const auto ir_phase = resolve_ir_phase_policy(
        IrPhasePolicyV1{1, IrPhaseMode::MixedPhase, 0.5});
    CHECK(ir.prepare(ir_kernel, 2U, 1U, 2U, 48000U, ir_phase));
    float ir_block[4] = {1.0F, 1.0F, 0.0F, 0.0F};
    CHECK(ir.process_interleaved(ir_block, 2U, 2U));
    CHECK(std::abs(ir_block[0] - 1.0F) < 1e-6F &&
          std::abs(ir_block[1] - 1.0F) < 1e-6F &&
          std::abs(ir_block[2] - 0.5F) < 1e-6F &&
          std::abs(ir_block[3] - 0.5F) < 1e-6F);
    CHECK(!ir.prepare(ir_kernel, 2U, 1U, 2U, 48000U,
                      IrPhaseResolutionV1{1, IrPhaseMode::Bypass, 0.0, 0.0, false, false}));

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
    const auto limiter_gain = limiter.limit_in_place(limiter_samples, 2U, 2U, -1.0);
    CHECK(limiter_gain < 1.0F && std::abs(limiter_samples[0]) <= 0.891251F + 1e-5F &&
          std::abs(limiter_samples[1]) <= 0.891251F + 1e-5F);
    limiter.reset();
    float finite_guard[] = {std::numeric_limits<float>::quiet_NaN(), 0.25F};
    CHECK(limiter.limit_in_place(finite_guard, 1U, 2U, -1.0) == 1.0F &&
          finite_guard[0] == 0.0F);

    std::vector<IsoContourPoint> current{{100.0, 60.0}, {1000.0, 40.0}};
    std::vector<IsoContourPoint> reference{{100.0, 50.0}, {1000.0, 40.0}};
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

    EqualLoudnessPolicyV1 calibrated;
    calibrated.mode = EqualLoudnessMode::Calibrated;
    CHECK(!validate_policy(calibrated));
    calibrated.anchor_id = "speaker-anchor";
    CHECK(!validate_policy(calibrated));
    calibrated.standard = "iso-226-2023-calibrated";
    CHECK(validate_policy(calibrated));
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
    CHECK(hibiki_wavert_endpoint_state_init_v1(
        &wavert_state, "8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10", 2, 48000,
        HIBIKI_ACTUATOR_STRICT_DIRECT));
    CHECK(hibiki_wavert_endpoint_state_apply_volume_v1(
        &wavert_state, 0, 0, 0, 2, "direct"));
    CHECK(wavert_state.effective_db_q16_16 == 0);
    CHECK(!hibiki_wavert_endpoint_state_init_v1(
        &wavert_state, "bad", 4, 48000, HIBIKI_ACTUATOR_INTERNAL_DSP));

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
    ControlCommandV1 decoded_command{};
    IpcFrameV1 volume_command_frame;
    volume_command_frame.header.type = IpcMessageType::VolumeNotification;
    volume_command_frame.header.request_id = 99U;
    volume_command_frame.payload.assign(volume_payload.begin(), volume_payload.end());
    CHECK(decode_control_command_v1(volume_command_frame, decoded_command) &&
          decoded_command.type == IpcMessageType::VolumeNotification &&
          decoded_command.request_id == 99U && decoded_command.volume.mute);
    CHECK(make_ack_frame_v1(volume_command_frame).header.request_id == 99U &&
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
    bool command_accepted = false;
    ControlPlaneHandlerContextV1 service_context{accept_control_command, &command_accepted};
    IpcFrameV1 service_response;
    CHECK(handle_control_frame_v1(scene_frame, service_response, &service_context) &&
          command_accepted && service_response.header.type == IpcMessageType::Ack &&
          service_response.header.request_id == scene_frame.header.request_id);
    AudioEngineModel control_engine;
    EngineControlWorkerV1 control_worker(control_engine);
    ControlCommandQueueV1 control_queue;
    ControlCommandV1 scene_command{};
    scene_command.type = IpcMessageType::SceneApply;
    scene_command.scene = decoded_scene;
    CHECK(control_queue.try_push(scene_command));
    CHECK(control_worker.drain(control_queue) == 1U && control_worker.has_active_scene() &&
          control_worker.active_scene().output_group == "main" && control_worker.revision() == 1U);
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
    CHECK(control_worker.drain(control_queue) == 1U && control_worker.revision() == 1U);
    auto malformed = encoded;
    malformed[0] = 0;
    CHECK(!decode_ipc_frame(malformed, decode_error).has_value());
    CHECK(decode_error == IpcDecodeError::InvalidMagic);
    IpcNamedPipeServerV1 ipc_server;
    CHECK(!ipc_server.start(IpcNamedPipeConfigV1{L"", 1024U, 100U}, nullptr, nullptr));
#if defined(_WIN32)
    constexpr wchar_t kControlPipe[] = L"\\\\.\\pipe\\HibikiDSP_contract_control";
    CHECK(ipc_server.start(IpcNamedPipeConfigV1{kControlPipe, 1024U, 1000U},
                           acknowledge_ipc_request, nullptr));
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
    CloseHandle(ipc_client);
    ipc_server.stop();
    CHECK(!ipc_server.running());
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
    CHECK(session_registry.upsert(AudioSessionDescriptorV1{
        1, AudioSessionIdentityV1{"hibiki-main", "chrome-instance-a", 5678},
        "Chrome tab A renamed", "chrome.exe", true,
        SessionGainOwner::WindowsSession, {}, {}}));
    CHECK(session_registry.find(chrome_tab_a)->lane_id == "vlog-noise");
    CHECK(session_registry.find(chrome_tab_a)->identity.process_id == 5678);
    CHECK(session_registry.remove(chrome_tab_b));
    CHECK(session_registry.find(chrome_tab_b) == nullptr);

    PluginHostModel plugin;
    CHECK(!plugin.start(PluginDescriptorV1{"untrusted", 2, 2, 64, false}));
    CHECK(plugin.state() == PluginHostState::Quarantined);
    CHECK(plugin.start(PluginDescriptorV1{"builtin-test", 2, 2, 64, true}));
    CHECK(plugin.heartbeat(1000));
    CHECK(!plugin.poll_watchdog(1200));
    CHECK(plugin.poll_watchdog(1300));
    CHECK(plugin.state() == PluginHostState::Quarantined);
    CHECK(plugin.start(PluginDescriptorV1{"builtin-test", 2, 2, 64, true}));
    const float plugin_input[] = {0.1F, -0.2F};
    float plugin_output[2]{};
    CHECK(plugin.process_passthrough(plugin_input, plugin_output, 2));
    CHECK(plugin_output[0] == plugin_input[0] && plugin_output[1] == plugin_input[1]);
    plugin.report_crash();
    CHECK(!plugin.process_passthrough(plugin_input, plugin_output, 2));

    Vst3SandboxProcess sandbox;
    CHECK(!sandbox.launch(Vst3SandboxLaunchV1{L"", L"", 250}));
    CHECK(sandbox.state() == Vst3SandboxState::Quarantined);
    sandbox.stop();
    CHECK(sandbox.state() == Vst3SandboxState::Stopped);

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
    Vst3WorkerPipeV1 worker_pipe;
    CHECK(!worker_pipe.create_server(Vst3WorkerPipeConfigV1{L"", 1024U, 100U}));
    CHECK(!worker_pipe.connect_client(L"", 100U));

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
    TabBridgeServer tab_server;
    CHECK(!tab_server.start(TabBridgeServerConfigV1{17842U, 256U * 1024U}, nullptr, nullptr));
    CHECK(!tab_server.running());

    const auto transport_bytes = hibiki_asio_transport_region_size_v1();
    std::vector<std::uint64_t> transport_words(
        (transport_bytes + sizeof(std::uint64_t) - 1U) / sizeof(std::uint64_t));
    auto* transport = reinterpret_cast<hibiki_asio_transport_region_v1*>(transport_words.data());
    CHECK(hibiki_asio_transport_init_v1(transport,
                                        transport_words.size() * sizeof(std::uint64_t),
                                        2U, 48000U, 4U) == 1);
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

    AudioEngineModel engine;
    GraphConfigV1 engine_graph;
    engine_graph.lanes.push_back(LaneConfigV1{"game", "main", 2, 0.0, true});
    CHECK(engine.prepare_graph(engine_graph, 11));
    CHECK(engine.transaction_state() == EngineTransactionState::Prepared);
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
    std::vector<RtLaneInputV1> tab_lane_inputs(1);
    float tab_lane_input[8]{};
    float tab_lane_output[8]{};
    TabCaptureBlockV1 tab_lane_block{};
    ProgramAwareLevelControllerV1 tab_program_level;
    CHECK(tab_program_level.configure(
        ProgramAwareLevelPolicyV1{1, true, -23.0, 6.0, 12.0, 3000.0, 60.0, -70.0}, 48000U));
    PeqProcessorV1 tab_peq;
    CHECK(tab_peq.prepare(peq_filters, 48000U, 2U));
    IrConvolverV1 tab_ir;
    CHECK(tab_ir.prepare(ir_kernel, 2U, 1U, 2U, 48000U, ir_phase));
    TabLaneEffectsV1 tab_effects{&tab_peq, &tab_ir, &tab_program_level};
    CHECK(process_tab_capture_lane_v1(engine, 0, *tab_queue, tab_lane_input, 2U,
                                      tab_lane_inputs, tab_lane_output, 2U, tab_lane_block,
                                      &tab_effects));
    CHECK(tab_lane_block.frames == 2U && tab_lane_block.channels == 2U &&
          std::isfinite(tab_lane_output[0]) && std::isfinite(tab_lane_output[1]) &&
          tab_lane_output[0] > 0.125F && tab_lane_output[0] < 0.2F &&
          tab_lane_output[1] < -0.125F && tab_lane_output[1] > -0.2F);
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
#if defined(_WIN32)
    constexpr wchar_t kContractMapping[] = L"Local\\HibikiDSP_v1_contract_asio";
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
    auto* watcher = new WindowsDeviceWatcher();
    CHECK(watcher->OnDefaultDeviceChanged(eRender, eConsole, L"hibiki-endpoint") == S_OK);
    WindowsDeviceChangeSnapshotV1 device_change;
    CHECK(watcher->poll(device_change));
    CHECK(device_change.kind == WindowsDeviceChangeKind::DefaultChanged &&
          device_change.flow == eRender && device_change.endpoint_id[0] == L'h');
    CHECK(watcher->Release() == 0U);
    auto* session_watcher = new WindowsAudioSessionWatcher();
    std::uint64_t session_sequence = 0;
    CHECK(!session_watcher->poll(session_sequence));
    CHECK(session_watcher->OnSessionCreated(nullptr) == S_OK);
    CHECK(session_watcher->poll(session_sequence) && session_sequence == 1U);
    GUID session_context{};
    CHECK(session_watcher->write_session_volume("missing", -12.0, false, session_context) ==
          E_UNEXPECTED);
    double session_db = 0.0;
    bool session_mute = false;
    CHECK(session_watcher->read_session_volume("missing", session_db, session_mute) ==
          E_UNEXPECTED);
    CHECK(session_watcher->Release() == 0U);
    WindowsWasapiOutputV1 wasapi_output;
    CHECK(!wasapi_output.bind(WasapiOutputConfigV1{L"", 3U, 48000U, 20U}));
    CHECK(!wasapi_output.start());
    auto wasapi_worker = std::make_unique<WindowsWasapiSinkWorkerV1>();
    CHECK(!wasapi_worker->start(WasapiOutputConfigV1{L"", 3U, 48000U, 20U}, 128U));
    CHECK(!wasapi_worker->submit(nullptr, 128U, 2U));
#endif

    return 0;
}

#undef CHECK
