#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/scene_graph.hpp"
#include "hibiki/asio_transport_consumer.hpp"
#include "hibiki/driver_stream_bridge.hpp"
#include "hibiki/iso226.hpp"
#include "hibiki/peq_dsp.hpp"
#include "hibiki/wav_ir.hpp"
#include "hibiki/output_fanout.hpp"
#include "hibiki/volume_state.hpp"
#include "hibiki/true_peak_limiter.hpp"
#include "hibiki/windows_wasapi_handoff.hpp"
#include "hibiki/windows_wasapi_fanout.hpp"
#include "hibiki/program_loudness.hpp"
#include "hibiki/vst3_lane_bridge.hpp"
#include "hibiki/control_payloads.hpp"

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <memory>
#include <span>
#include <string_view>

namespace hibiki {

enum class EngineTransactionState : std::uint8_t {
    Ready,
    Prepared,
    Degraded,
};

class AudioEngineModel final {
public:
    struct LoudnessCurveSnapshotV1 {
        bool attached{false};
        std::array<char, kMaxOutputGroupBytes> output_group{};
        std::uint8_t output_group_bytes{0U};
        double current_phon{80.0};
        std::array<EqVisualSnapshotPointV1, kEqVisualSnapshotCapacityV1> points{};
        std::size_t point_count{0U};
    };

    AudioEngineModel();
    ~AudioEngineModel();
    AudioEngineModel(const AudioEngineModel&) = delete;
    AudioEngineModel& operator=(const AudioEngineModel&) = delete;

    [[nodiscard]] bool prepare_graph(const GraphConfigV1& graph, std::uint64_t revision) noexcept;
    [[nodiscard]] bool commit_graph() noexcept;
    void rollback_graph() noexcept;
    // IR is an independent graph attachment transaction.  Prepare performs
    // bounded file/data validation and coefficient preparation on the control
    // worker; commit is the only point at which the RT-visible attachment is
    // replaced.  The audio callback never reads a path or allocates.
    [[nodiscard]] bool prepare_ir(std::string_view output_group,
                                  const IrWavDataV1& data,
                                  const IrPhaseResolutionV1& phase) noexcept;
    [[nodiscard]] bool prepare_ir_clear() noexcept;
    [[nodiscard]] bool commit_ir() noexcept;
    void rollback_ir() noexcept;
    // Equal-loudness is an independent fixed-capacity output attachment.
    // Prepare evaluates caller-supplied ISO formula points on the control
    // worker and compiles them into bounded PEQ coefficients; commit is the
    // only RT-visible swap. The callback never allocates, waits, or reads a
    // path. This is user-space tone shaping, not ISO conformance evidence.

    [[nodiscard]] bool prepare_loudness_peq(
        std::string_view output_group,
        std::span<const Iso226FormulaPointV1> points,
        double current_phon,
        const EqualLoudnessPolicyV1& policy) noexcept;

    [[nodiscard]] bool prepare_loudness_peq_clear() noexcept;

    [[nodiscard]] bool commit_loudness_peq() noexcept;

    void rollback_loudness_peq() noexcept;

    // Recompute loudness PEQ with a new phon value using stored formula
    // points. Control-plane only; uses prepare/commit transaction. Fails
    // closed outside the 20..90 phon domain, for foreign groups, while a
    // transaction is pending, under Strict Direct, or when debounced.
    [[nodiscard]] bool update_loudness_phon(
        std::string_view output_group, double new_phon) noexcept;

    // Opt-in switch for volume-driven phon recompute on an already committed
    // equal-loudness attachment. Control-plane only; no-op for foreign or
    // detached groups.
    void set_loudness_live_update(std::string_view output_group, bool enabled) noexcept;

    // True when no equal-loudness transaction is pending and any committed
    // bounded RT crossfade has fully retired.

    [[nodiscard]] bool loudness_peq_transaction_idle() const noexcept;

    [[nodiscard]] bool loudness_peq_transition_complete() const noexcept;

    [[nodiscard]] bool has_active_loudness_peq(

        std::string_view output_group = "main") const noexcept;

    // Control-plane projection of the confirmed compensation curve. It is a
    // bounded visual snapshot, not an audio measurement or ISO conformance.
    [[nodiscard]] LoudnessCurveSnapshotV1 loudness_curve_snapshot() const noexcept;

    void reset_loudness_peq_state() noexcept;

    // Program-aware level is an independent fixed-capacity output
    // attachment. Prepare validates the caller policy on the control worker;
    // commit is the only RT-visible swap. The callback never allocates,
    // waits, or reads a path. This is a bounded loudness proxy, not BS.1770
    // conformance.
    [[nodiscard]] bool prepare_program_aware(
        std::string_view output_group,
        const ProgramAwareLevelPolicyV1& policy) noexcept;
    [[nodiscard]] bool prepare_program_aware_clear() noexcept;
    [[nodiscard]] bool commit_program_aware() noexcept;
    void rollback_program_aware() noexcept;
    [[nodiscard]] bool program_aware_transaction_idle() const noexcept;
    [[nodiscard]] bool has_active_program_aware(
        std::string_view output_group = "main") const noexcept;
    // Control-plane projection of the committed, enabled, non-Strict-Direct
    // program-aware attachment. Invalid when detached, foreign, bypassed,
    // disabled, silence-gated, or before the first rendered block. This is
    // bounded visual telemetry, not content analysis or physical audio.
    struct ProgramAwareTelemetrySnapshotV1 {
        bool valid{false};
        bool enabled{false};
        bool silence_gated{true};
        double measured_dbfs{-144.0};
        double applied_gain_db{0.0};
        std::uint64_t sequence{0U};
    };
    [[nodiscard]] ProgramAwareTelemetrySnapshotV1
    program_aware_telemetry_snapshot(std::string_view output_group) const noexcept;
    [[nodiscard]] ProgramAwareTelemetrySnapshotV1
    program_aware_visual_snapshot() const noexcept;
    void reset_program_aware_state() noexcept;

    // VST3 lane bridge is an independent fixed-capacity output attachment.
    // Prepare validates the caller ring on the control worker; commit is the
    // only RT-visible swap. The callback never allocates, waits, or reads a
    // pipe. Worker failure leaves the RT path as passthrough.
    [[nodiscard]] bool prepare_vst3_lane(
        std::string_view output_group,
        std::uint32_t channels,
        std::span<float> ring_storage) noexcept;
    [[nodiscard]] bool prepare_vst3_lane_clear(
        std::string_view output_group) noexcept;
    [[nodiscard]] bool commit_vst3_lane() noexcept;
    void rollback_vst3_lane() noexcept;
    [[nodiscard]] bool vst3_lane_transaction_idle() const noexcept;
    [[nodiscard]] bool has_active_vst3_lane(
        std::string_view output_group = "main") const noexcept;
    void reset_vst3_lane_state() noexcept;
    // True when no IR prepare/clear transaction is pending. SceneApply uses
    // this to decide whether an unchanged calibration reference can keep the
    // current attachment untouched instead of running a clear transaction.
    [[nodiscard]] bool ir_transaction_idle() const noexcept;
    [[nodiscard]] bool has_active_ir(std::string_view output_group = "main") const noexcept;
    [[nodiscard]] IrConvolverStatusV1 ir_status(
        std::string_view output_group = "main") const noexcept;
    [[nodiscard]] VolumeNotificationResult apply_windows_volume(
        const VolumeNotificationV1& notification) noexcept;
    [[nodiscard]] VolumeNotificationResult apply_windows_volume(
        std::string_view output_group,
        const VolumeNotificationV1& notification) noexcept;
    [[nodiscard]] OutputGroupVolumeStateV1 volume_state(
        std::string_view output_group) const noexcept;
    [[nodiscard]] bool process(std::span<const RtLaneInputV1> inputs,
                               float* output_interleaved,
                               std::size_t frames) noexcept;
    [[nodiscard]] bool process_output_group(std::string_view output_group,
                                             std::span<const RtLaneInputV1> inputs,
                                             float* output_interleaved,
                                             std::size_t frames) noexcept;
    // Bounded double-precision model entry points. These use the same
    // committed immutable graph as the float32 path and apply Group Master,
    // but intentionally exclude the final TruePeakLimiter, the plugin-latency
    // bank, and all float-only attachments in this v1 boundary. Callers
    // owning double samples must ensure peak safety and resolve plugin
    // latency upstream before entering this entry point.
    [[nodiscard]] bool process_f64(std::span<const RtLaneInputF64V1> inputs,
                                   double* output_interleaved,
                                   std::size_t frames) noexcept;
    [[nodiscard]] bool process_output_group_f64(
        std::string_view output_group,
        std::span<const RtLaneInputF64V1> inputs,
        double* output_interleaved,
        std::size_t frames) noexcept;
    [[nodiscard]] bool prepare_output_fanout(const OutputFanoutPlanV1& plan,
                                              double source_step = 1.0) noexcept;
    [[nodiscard]] bool observe_output_fanout_clock(std::size_t sink_index,
                                                   double source_frames,
                                                   double sink_frames,
                                                   double elapsed_seconds) noexcept;
    [[nodiscard]] bool process_output_group_fanout(
        std::string_view output_group,
        std::span<const RtLaneInputV1> inputs,
        float* graph_output_interleaved,
        std::size_t frames,
        std::span<float* const> outputs,
        std::span<const std::size_t> output_capacities,
        std::span<std::size_t> output_frames) noexcept;
    [[nodiscard]] OutputFanoutRuntimeSnapshotV1 output_fanout_snapshot() const noexcept;
    void set_sample_rate(std::uint32_t sample_rate) noexcept;
    [[nodiscard]] bool bind_asio_transport(std::wstring_view mapping_name,
                                            std::uint32_t channels,
                                            std::uint32_t sample_rate,
                                            std::uint32_t frames_per_buffer) noexcept;
    void unbind_asio_transport() noexcept;
    [[nodiscard]] bool asio_transport_bound() const noexcept;
    [[nodiscard]] bool process_asio_transport(
        std::size_t lane_index,
        float* transport_interleaved,
        std::uint32_t transport_capacity_frames,
        std::span<RtLaneInputV1> lane_inputs,
        float* output_interleaved,
        std::size_t output_capacity_frames,
        AsioTransportBlockV1& block) noexcept;
    [[nodiscard]] bool process_asio_transport_to_wasapi(
        std::size_t lane_index,
        float* transport_interleaved,
        std::uint32_t transport_capacity_frames,
        std::span<RtLaneInputV1> lane_inputs,
        float* output_interleaved,
        std::size_t output_capacity_frames,
        AsioTransportBlockV1& block) noexcept;
    [[nodiscard]] bool process_driver_stream_packet(
        std::size_t lane_index,
        std::string_view expected_endpoint_guid,
        std::span<const std::uint8_t> packet,
        std::span<float> packet_sample_storage,
        std::span<RtLaneInputV1> lane_inputs,
        float* output_interleaved) noexcept;
    [[nodiscard]] bool process_driver_stream_packet_to_wasapi(
        std::size_t lane_index,
        std::string_view expected_endpoint_guid,
        std::span<const std::uint8_t> packet,
        std::span<float> packet_sample_storage,
        std::span<RtLaneInputV1> lane_inputs,
        float* output_interleaved) noexcept;
    // Processes one caller-owned lane block through the normal graph, Group
    // Master and limiter path, then writes a complete outbound render packet.
    // This is a user-space transport boundary only; delivery to a WaveRT ring
    // remains the caller's responsibility.
    [[nodiscard]] bool encode_driver_stream_packet_from_lane(
        std::size_t lane_index,
        std::string_view endpoint_guid,
        std::uint64_t sequence,
        std::uint64_t generation,
        std::uint32_t flags,
        const float* input_interleaved,
        std::uint32_t input_channels,
        std::size_t frames,
        std::span<RtLaneInputV1> lane_inputs,
        float* processed_output_interleaved,
        std::span<std::uint8_t> packet,
        std::size_t& written_bytes) noexcept;
    // Windows/WASAPI sink boundary. The graph remains the only producer of
    // audio blocks; handoff owns the two bounded sink workers and never
    // restarts this engine during a device switch.
    [[nodiscard]] bool start_wasapi_output(const WasapiOutputConfigV1& config,
                                           std::uint32_t block_frames = 128U) noexcept;
    [[nodiscard]] bool begin_wasapi_output_handoff(
        const WasapiOutputConfigV1& candidate,
        std::uint32_t block_frames = 128U,
        std::uint32_t fade_ms = 30U) noexcept;
    [[nodiscard]] bool prepare_wasapi_output_handoff() noexcept;
    [[nodiscard]] bool commit_wasapi_output_handoff() noexcept;
    void rollback_wasapi_output_handoff() noexcept;
    void stop_wasapi_output() noexcept;
    [[nodiscard]] WasapiSinkHandoffSnapshotV1 wasapi_output_snapshot() const noexcept;
    [[nodiscard]] bool process_output_group_to_wasapi(
        std::string_view output_group,
        std::span<const RtLaneInputV1> inputs,
        float* graph_output_interleaved,
        std::size_t frames) noexcept;
    [[nodiscard]] bool prepare_wasapi_fanout(
        std::span<const WasapiFanoutSinkConfigV1> configs,
        std::uint32_t block_frames = 128U) noexcept;
    [[nodiscard]] bool process_output_group_to_wasapi_fanout(
        std::string_view output_group,
        std::span<const RtLaneInputV1> inputs,
        float* graph_output_interleaved,
        std::size_t frames) noexcept;
    [[nodiscard]] WasapiFanoutSnapshotV1 wasapi_fanout_snapshot() const noexcept;
    void stop_wasapi_fanout() noexcept;
    [[nodiscard]] bool process_lane_block(std::size_t lane_index,
                                          const float* input_interleaved,
                                          std::uint32_t input_channels,
                                          std::size_t frames,
                                          std::span<RtLaneInputV1> lane_inputs,
                                          float* output_interleaved) noexcept;
    [[nodiscard]] bool process_lane_block_to_wasapi(std::size_t lane_index,
                                                    const float* input_interleaved,
                                                    std::uint32_t input_channels,
                                                    std::size_t frames,
                                                    std::span<RtLaneInputV1> lane_inputs,
                                                    float* output_interleaved) noexcept;
    // Control-plane accessor for the pre-VST3 tap snapshot. The RT callback
    // publishes after program-aware level but before apply_vst3_lanes; the
    // control thread reads it to feed the sandbox worker. Never blocks.
    [[nodiscard]] bool read_vst3_tap(
        std::string_view output_group,
        float* destination,
        std::size_t max_frames,
        std::uint32_t& channels_out,
        std::size_t& frames_out,
        std::uint64_t& sequence_out) const noexcept;
    // Control-plane push into an already-committed VST3 lane ring. The caller
    // must have called prepare_vst3_lane + commit_vst3_lane first.
    [[nodiscard]] bool push_vst3_lane(
        std::string_view output_group,
        const float* interleaved,
        std::size_t frames) noexcept;
    [[nodiscard]] EngineTransactionState transaction_state() const noexcept { return state_; }
    [[nodiscard]] const RtGraphSnapshotV1& active_graph() const noexcept { return active_graph_; }
    // Control-plane snapshot.  The RT process path reads the two atomics
    // below instead of touching this mutable control-plane object.
    [[nodiscard]] OutputGroupVolumeStateV1 volume() const noexcept {
        return volume_bank_ != nullptr ? volume_bank_->state("main")
                                       : OutputGroupVolumeStateV1{};
    }
    [[nodiscard]] OutputGroupVolumeStateV1 volume(
        std::string_view output_group) const noexcept {
        return volume_bank_ != nullptr ? volume_bank_->state(output_group)
                                       : OutputGroupVolumeStateV1{};
    }

private:
    struct IrGraphAttachmentV1 {
        bool attached{false};
        std::uint8_t output_group_bytes{0U};
        std::array<char, kMaxOutputGroupBytes> output_group{};
        IrPhaseResolutionV1 phase{};
        IrConvolverV1 convolver{};
    };

    RtGraphSnapshotV1 active_graph_{};
    RtGraphSnapshotV1 pending_graph_{};
    mutable LaneLatencyBankV1 active_latency_bank_{};
    LaneLatencyBankV1 pending_latency_bank_{};
    std::unique_ptr<OutputGroupVolumeBankV1> volume_bank_{};
    std::atomic<std::uint32_t> sample_rate_{48000U};
    mutable IrGraphAttachmentV1 active_ir_{};
    IrGraphAttachmentV1 pending_ir_{};
    bool has_active_ir_{false};
    bool has_pending_ir_{false};
    struct LoudnessGraphAttachmentV1 {
        bool attached{false};
        std::uint8_t output_group_bytes{0U};
        std::array<char, kMaxOutputGroupBytes> output_group{};
        PeqProcessorV1 peq{};
        // Stored for live phon recompute. Not read by RT path.
        std::array<Iso226FormulaPointV1, 64U> formula_points{};
        std::size_t formula_point_count{0U};
        double current_phon{80.0};
        EqualLoudnessPolicyV1 policy{};
        bool live_update_enabled{false};
        // Per-group live-update debounce state (control plane only; never
        // touched by the audio thread). A recompute runs when at least
        // 250 ms elapsed since this group's last applied update OR the phon
        // request moved by 3.0 or more from this group's baseline.
        double last_loudness_phon_{80.0};
        std::chrono::steady_clock::time_point last_phon_update_time_{};
    };

    LoudnessGraphAttachmentV1 active_loudness_peq_{};

    LoudnessGraphAttachmentV1 pending_loudness_peq_{};

    LoudnessGraphAttachmentV1 previous_loudness_peq_{};

    struct LoudnessPeqCrossfadeState {
        bool active{false};
        std::size_t total_frames{0U};
        std::size_t processed_frames{0U};

        bool begin(const std::size_t frames) noexcept {
            if (frames == 0U) return false;
            active = true;
            total_frames = frames;
            processed_frames = 0U;
            return true;
        }

        void reset() noexcept { *this = {}; }
    };

    LoudnessPeqCrossfadeState loudness_crossfade_{};

    bool has_active_loudness_peq_{false};
    bool has_pending_loudness_peq_{false};

    struct ProgramAwareAttachmentV1 {
        bool attached{false};
        std::uint8_t output_group_bytes{0U};
        std::array<char, kMaxOutputGroupBytes> output_group{};
        mutable std::unique_ptr<ProgramAwareLevelBankV1> bank{};
    };

    ProgramAwareAttachmentV1 active_program_aware_{};
    ProgramAwareAttachmentV1 pending_program_aware_{};
    bool has_active_program_aware_{false};
    bool has_pending_program_aware_{false};
    Vst3LaneRingBridgeV1 active_vst3_lanes_{};
    Vst3LaneRingBridgeV1 pending_vst3_lanes_{};
    mutable Vst3TapBufferV1 vst3_tap_{};
    bool has_active_vst3_lanes_{false};
    bool has_pending_vst3_lanes_{false};
    std::string_view pending_vst3_lane_clear_target_{};
    EngineTransactionState state_{EngineTransactionState::Ready};
    bool has_active_graph_{false};
    bool has_pending_graph_{false};
    AsioTransportConsumerV1 asio_transport_{};
    mutable OutputFanoutRuntimeV1 output_fanout_{};
    WindowsWasapiSinkHandoffV1 wasapi_handoff_{};
    WindowsWasapiFanoutV1 wasapi_fanout_{};

    [[nodiscard]] bool apply_group_master(std::string_view output_group,
                                          float* output_interleaved,
                               std::size_t frames) noexcept;
    [[nodiscard]] bool apply_group_master_f64(
        std::string_view output_group,
        double* output_interleaved,
        std::size_t frames) noexcept;
    [[nodiscard]] bool apply_ir(std::string_view output_group,
                                float* output_interleaved,
                                             std::size_t frames) noexcept;
    [[nodiscard]] bool apply_loudness_peq(std::string_view output_group,
                                          float* output_interleaved,
                                          std::size_t frames) noexcept;
    [[nodiscard]] bool apply_program_aware(std::string_view output_group,
                                            float* output_interleaved,
                                            std::size_t frames) noexcept;
    [[nodiscard]] bool apply_vst3_lanes(std::string_view output_group,
                                         float* output_interleaved,
                                         std::size_t frames) noexcept;
};

}  // namespace hibiki
