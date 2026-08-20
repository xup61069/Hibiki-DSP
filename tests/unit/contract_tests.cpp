#include "hibiki/contracts.hpp"
#include "hibiki/device_switch.hpp"
#include "hibiki/device_recovery.hpp"
#include "hibiki/ipc.hpp"
#include "hibiki/asio_bridge.hpp"
#include "hibiki/calibration.hpp"
#include "hibiki/plugin_host.hpp"
#include "hibiki/vst3_sandbox.hpp"
#include "hibiki/tab_bridge.hpp"
#include "hibiki/asio_transport_v1.h"
#include "hibiki/output_sink.hpp"
#include "hibiki/output_crossfade.hpp"
#include "hibiki/exporters.hpp"
#include "hibiki/audio_engine.hpp"
#include "hibiki/audio_session_registry.hpp"

extern "C" {
#include "hibiki/driver_control_v1.h"
#include "hibiki/driver_validation_v1.h"
#include "hibiki/wavert_endpoint_state_v1.h"
}
#include "hibiki/iso226.hpp"
#include "hibiki/scene_graph.hpp"
#include "hibiki/scene_presets.hpp"
#include "hibiki/scene_safety.hpp"
#include "hibiki/volume_state.hpp"
#if defined(_WIN32)
#include <windows.h>
#include "hibiki/windows_volume_broker.hpp"
#include "hibiki/windows_device_watcher.hpp"
#include "hibiki/windows_audio_session_watcher.hpp"
#endif

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            std::fprintf(stderr, "contract check failed: %s (%s:%d)\n", #condition,       \
                         __FILE__, __LINE__);                                               \
            return 1;                                                                       \
        }                                                                                   \
    } while (false)

int main() {
    using namespace hibiki;

    SceneProfileV1 scene;
    scene.id = "game";
    scene.name = "Game";
    scene.output_group = "main";
    CHECK(validate_scene(scene));

    OutputGroupVolumeStateV1 state;
    state.requested_db = 3.0;
    state.safety_ceiling_db = -6.0;
    state = reconcile(state);
    CHECK(std::abs(state.effective_db + 6.0) < 1e-12);

    state.actuator = ActuatorMode::StrictDirect;
    state = reconcile(state);
    CHECK(std::abs(state.effective_db) < 1e-12);

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

    OutputCrossfade crossfade;
    CHECK(crossfade.begin(2, 48000, 30));
    std::vector<float> old_sink(48000U * 30U / 1000U * 2U, 1.0F);
    std::vector<float> new_sink(old_sink.size(), 0.0F);
    std::vector<float> mixed(old_sink.size(), 0.0F);
    CHECK(crossfade.process(old_sink.data(), new_sink.data(), mixed.data(), old_sink.size() / 2U));
    CHECK(crossfade.snapshot().processed_frames == crossfade.snapshot().total_frames);
    CHECK(!crossfade.snapshot().active);
    CHECK(mixed.front() > 0.0F && mixed.back() < 0.01F);

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
    const auto studio_scene = make_easy_scene(EasySceneKind::Studio, "main");
    CHECK(validate_scene(studio_scene.scene));
    CHECK(studio_scene.graph.strict_direct);
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
    auto malformed = encoded;
    malformed[0] = 0;
    CHECK(!decode_ipc_frame(malformed, decode_error).has_value());
    CHECK(decode_error == IpcDecodeError::InvalidMagic);

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
    CHECK(session_registry.find(chrome_tab_a)->lane_id == "vlog-noise");
    CHECK(session_registry.find(chrome_tab_a)->output_group == "headphones");
    CHECK(session_registry.find(chrome_tab_a)->gain_owner == SessionGainOwner::HibikiInternal);
    CHECK(session_registry.find(chrome_tab_b)->lane_id == "music");
    CHECK(session_registry.find(chrome_tab_b)->output_group == "speakers");
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
    CHECK(engine.apply_windows_volume(VolumeNotificationV1{-6.0206, false, 1}) ==
          VolumeNotificationResult::Accepted);
    const float engine_input[] = {1.0F, -1.0F};
    const RtLaneInputV1 engine_input_view{engine_input, 2};
    float engine_output[2]{};
    CHECK(engine.process(std::span<const RtLaneInputV1>(&engine_input_view, 1), engine_output, 1));
    CHECK(std::abs(engine_output[0] - 0.5F) < 1e-5F);
    CHECK(std::abs(engine_output[1] + 0.5F) < 1e-5F);
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
          std::abs(contract_asio_output[0] - 0.5F) < 1e-5F &&
          std::abs(contract_asio_output[1] + 0.5F) < 1e-5F);
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
#endif

    return 0;
}

#undef CHECK
