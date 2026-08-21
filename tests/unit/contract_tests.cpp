#include "hibiki/contracts.hpp"
#include "hibiki/control_payloads.hpp"
#include "hibiki/control_service.hpp"
#include "hibiki/device_switch.hpp"
#include "hibiki/device_recovery.hpp"
#include "hibiki/device_catalog.hpp"
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
#include "hibiki/driver_stream_bridge.hpp"

extern "C" {
#include "hibiki/driver_control_v1.h"
#include "hibiki/driver_stream_transport_v1.h"
#include "hibiki/driver_validation_v1.h"
#include "hibiki/wavert_endpoint_state_v1.h"
#include "hibiki/wavert_stream_v1.h"
#include "hibiki/endpoint_topology_v1.h"
}
#include "hibiki/iso226.hpp"
#include "hibiki/ir_phase.hpp"
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
#include <windows.h>
#include "hibiki/windows_volume_broker.hpp"
#include "hibiki/windows_device_watcher.hpp"
#include "hibiki/windows_audio_session_watcher.hpp"
#include "hibiki/windows_wasapi_output.hpp"
#include "hibiki/windows_wasapi_handoff.hpp"
#include "hibiki/windows_wasapi_fanout.hpp"
#endif

#include <cmath>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
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

bool allow_scene_preflight(const hibiki::SceneProfileV1&, void* context) noexcept {
    return context != nullptr && *static_cast<const bool*>(context);
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
    scene.automation_timeline_ids.push_back("game-vst3-default");
    CHECK(validate_scene(scene));
    scene.automation_timeline_ids.push_back(std::string(65, 'x'));
    CHECK(!validate_scene(scene));
    scene.automation_timeline_ids.pop_back();

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
    const std::array<Iso226FormulaPointV1, 2> formula_points{{
        {100.0, 0.25, 50.0, 0.0}, {1000.0, 0.30, 2.4, 0.0}}};
    const auto formula_result = build_formula_compensation(formula_points, 60.0, policy);
    CHECK(formula_result.points.size() == 2U &&
          std::abs(formula_result.points[1].gain_db) < 1e-10 &&
          std::isfinite(formula_result.points[0].gain_db));
    const std::array<Iso226FormulaPointV1, 1> no_anchor{{{100.0, 0.25, 50.0, 0.0}}};
    CHECK(build_formula_compensation(no_anchor, 60.0, policy).points.empty());

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
    auto unsorted_response = calibration_response;
    std::swap(unsorted_response[0], unsorted_response[1]);
    CHECK(!validate_calibration_response_v1(unsorted_response, calibration_policy));
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
    CHECK(!sandbox.launch(Vst3SandboxLaunchV1{L"", L"", 250}));
    CHECK(!validate_vst3_sandbox_launch_v1(Vst3SandboxLaunchV1{L"worker.exe", L"plugin.vst3", 250,
                                                               1U, L"pipe", 1000U, L"uid", 48000.0,
                                                               4U}));
    CHECK(validate_vst3_sandbox_launch_v1(Vst3SandboxLaunchV1{L"worker.exe", L"plugin.vst3", 250,
                                                              1U, L"pipe", 1000U, L"uid", 48000.0,
                                                              2U}));
    CHECK(sandbox.state() == Vst3SandboxState::Quarantined);
    sandbox.stop();
    CHECK(sandbox.state() == Vst3SandboxState::Stopped);
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
