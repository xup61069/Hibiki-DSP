#include "hibiki/contracts.hpp"
#include "hibiki/device_switch.hpp"
#include "hibiki/ipc.hpp"
#include "hibiki/asio_bridge.hpp"
#include "hibiki/calibration.hpp"
#include "hibiki/plugin_host.hpp"
#include "hibiki/output_sink.hpp"
#include "hibiki/exporters.hpp"

extern "C" {
#include "hibiki/driver_control_v1.h"
#include "hibiki/driver_validation_v1.h"
}
#include "hibiki/iso226.hpp"
#include "hibiki/scene_graph.hpp"
#include "hibiki/scene_presets.hpp"
#include "hibiki/volume_state.hpp"

#include <cmath>
#include <cstdio>
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

    PluginHostModel plugin;
    CHECK(!plugin.start(PluginDescriptorV1{"untrusted", 2, 2, 64, false}));
    CHECK(plugin.state() == PluginHostState::Quarantined);
    CHECK(plugin.start(PluginDescriptorV1{"builtin-test", 2, 2, 64, true}));
    const float plugin_input[] = {0.1F, -0.2F};
    float plugin_output[2]{};
    CHECK(plugin.process_passthrough(plugin_input, plugin_output, 2));
    CHECK(plugin_output[0] == plugin_input[0] && plugin_output[1] == plugin_input[1]);
    plugin.report_crash();
    CHECK(!plugin.process_passthrough(plugin_input, plugin_output, 2));

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

    return 0;
}

#undef CHECK
