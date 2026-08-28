#pragma once

// SPDX-License-Identifier: Apache-2.0

#include "hibiki/volume_state.hpp"
#include "hibiki/ipc.hpp"
#include "hibiki/contracts.hpp"
#include "hibiki/ir_phase.hpp"
#include "hibiki/equal_loudness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>
#include <span>
#include <string_view>

namespace hibiki {

// Shared strict UTF-8 predicate for bounded control-plane records. It accepts
// printable Unicode only and rejects control characters, overlong sequences,
// surrogates and truncated code points.
[[nodiscard]] bool is_printable_utf8_v1(std::string_view value) noexcept;

constexpr std::size_t kVolumeNotificationPayloadBytesV1 = 16U;
constexpr std::size_t kGroupedVolumeNotificationPayloadBytesV1 = 48U;
constexpr std::size_t kSceneApplyPayloadBytesV1 = 64U;
constexpr std::size_t kDeviceSwitchEndpointMaxBytesV1 = 260U;
constexpr std::size_t kDeviceSwitchPayloadBytesV1 = 288U;
constexpr std::size_t kDeviceCatalogSnapshotHeaderBytesV1 = 16U;
constexpr std::size_t kDeviceCatalogSnapshotEntryBytesV1 = 416U;
constexpr std::size_t kDeviceCatalogSnapshotCapacityV1 = 32U;
constexpr std::size_t kDeviceCatalogSnapshotPayloadBytesV1 =
    kDeviceCatalogSnapshotHeaderBytesV1 +
    (kDeviceCatalogSnapshotEntryBytesV1 * kDeviceCatalogSnapshotCapacityV1);
constexpr std::size_t kSessionVolumeCommandPayloadBytesV1 = 24U;
constexpr double kSessionVolumeMinDbV1 = -144.0;
constexpr double kSessionVolumeMaxDbV1 = 0.0;
constexpr std::int32_t kSessionVolumeMinDbQ16_16V1 = -144 * 65536;
constexpr std::int32_t kSessionVolumeMaxDbQ16_16V1 = 0;

[[nodiscard]] constexpr bool is_valid_session_volume_db_q16_16_v1(
    const std::int32_t requested_db_q16_16) noexcept {
    return requested_db_q16_16 >= kSessionVolumeMinDbQ16_16V1 &&
           requested_db_q16_16 <= kSessionVolumeMaxDbQ16_16V1;
}

constexpr std::size_t kSessionRouteCommandPayloadBytesV1 = 128U;
constexpr std::size_t kSessionRouteCommandLaneMaxBytesV1 = 48U;
constexpr std::size_t kSessionRouteCommandOutputMaxBytesV1 = 48U;
constexpr std::size_t kSessionRouteRuleIdMaxBytesV1 = 64U;
constexpr std::size_t kSessionRouteRuleMatchMaxBytesV1 = 128U;
constexpr std::size_t kSessionRouteRuleRouteMaxBytesV1 = 64U;
constexpr std::size_t kSessionRouteRuleCommandPayloadBytesV1 = 480U;
constexpr std::size_t kIrPreparePathMaxBytesV1 = 260U;
constexpr std::size_t kIrPrepareCommandPayloadBytesV1 = 288U;
constexpr std::size_t kSceneCatalogIdMaxBytesV1 = 31U;
constexpr std::size_t kSceneCatalogNameMaxBytesV1 = 120U;
constexpr std::size_t kSceneCatalogOutputGroupMaxBytesV1 = 64U;
constexpr std::size_t kSceneCatalogLaneCountV1 = 4U;
constexpr std::size_t kSceneCatalogTimelineCapacityV1 = 16U;
constexpr std::size_t kSceneCatalogTimelineIdBytesV1 = 64U;
constexpr std::size_t kSceneCatalogCommandPayloadBytesV1 = 3260U;

constexpr std::size_t kCalibrationPeqOutputGroupMaxBytesV1 = 64U;
constexpr std::size_t kCalibrationPeqMaxFiltersV1 = 16U;
constexpr std::size_t kCalibrationPeqCommandPayloadBytesV1 =
    16U + 64U + (kCalibrationPeqMaxFiltersV1 * 24U); // header + group + filters

enum class SessionRouteRuleOperationV1 : std::uint8_t {
    Upsert = 1U,
    Remove = 2U,
    Clear = 3U,
};

struct SceneCatalogWireLaneV1 {
    std::array<char, kSceneCatalogIdMaxBytesV1> id{};
    std::array<char, kSceneCatalogOutputGroupMaxBytesV1> output_group{};
    std::uint32_t channel_count{2U};
    float makeup_gain_db{0.0F};
    std::uint8_t enabled{1U};
    std::uint8_t matrix_enabled{0U};
    std::array<std::int8_t, 8> channel_map{0, 1, 2, 3, 4, 5, 6, 7};
    std::array<std::array<float, 8>, 8> channel_matrix{};
    std::uint32_t reported_latency_samples{0U};
    std::uint16_t id_bytes{0U};
    std::uint16_t output_group_bytes{0U};
    std::uint16_t reserved{0U};
};

struct SceneCatalogCommandV1 {
    std::uint32_t schema_version{1U};
    SessionRouteRuleOperationV1 operation{SessionRouteRuleOperationV1::Upsert};
    LatencyMode latency_mode{LatencyMode::Game};
    IrPhaseMode ir_phase_mode{IrPhaseMode::MinimumPhase};
    EqualLoudnessMode loudness_mode{EqualLoudnessMode::Relative};
    double limiter_dbtp{-1.0};
    double auto_attenuate_gain{-1.0};
    double reference_phon{80.0};
    double strength{1.0};
    double max_boost_db{6.0};
    double measured_f3_hz{0.0};
    double ir_phase_strength{0.0};
    std::uint64_t reserved_a{0U};
    std::uint8_t auto_attenuate{1U};
    std::uint8_t strict_direct{0U};
    std::uint8_t graph_output_channels{2U};
    std::uint8_t lane_count{1U};
    std::uint8_t timeline_count{0U};
    std::uint8_t id_bytes{0U};
    std::uint8_t name_bytes{0U};
    std::uint8_t output_group_bytes{0U};
    std::uint8_t ir_reference_bytes{0U};
    std::uint8_t anchor_id_bytes{0U};
    std::uint8_t standard_id{0U};
    std::uint8_t calibrated_flag{0U};
    std::uint8_t loudness_live_update{0U};
    std::uint8_t reserved_b{0U};
    std::array<char, kSceneCatalogIdMaxBytesV1> id{};
    std::array<char, kSceneCatalogNameMaxBytesV1> name{};
    std::array<char, kSceneCatalogOutputGroupMaxBytesV1> output_group{};
    std::array<char, 64> ir_reference{};
    std::array<char, 64> anchor_id{};
    std::array<std::array<char, kSceneCatalogTimelineIdBytesV1>, kSceneCatalogTimelineCapacityV1> timeline_ids{};
    std::array<SceneCatalogWireLaneV1, kSceneCatalogLaneCountV1> lanes{};
};

struct SessionVolumeCommandV1 {
    std::uint64_t handle{0U};
    std::int32_t requested_db_q16_16{0};
    std::uint8_t mute{0U};
    std::uint64_t catalog_sequence{0U};
};

[[nodiscard]] std::array<std::uint8_t, kSessionVolumeCommandPayloadBytesV1>
encode_session_volume_command_v1(const SessionVolumeCommandV1& command) noexcept;
[[nodiscard]] bool decode_session_volume_command_v1(
    std::span<const std::uint8_t> payload,
    SessionVolumeCommandV1& command) noexcept;

struct SessionRouteCommandV1 {
    std::uint64_t handle{0U};
    std::uint64_t catalog_sequence{0U};
    std::uint8_t lane_bytes{0U};
    std::uint8_t output_group_bytes{0U};
    std::array<char, kSessionRouteCommandLaneMaxBytesV1> lane{};
    std::array<char, kSessionRouteCommandOutputMaxBytesV1> output_group{};
};

// A bounded path request keeps the queue fixed-size while the control worker
// performs file I/O and WAV decoding off the pipe and RT threads. The path is
// UTF-8 and must be resolved on the same machine as the engine.
struct IrPrepareCommandV1 {
    std::uint32_t schema_version{1U};
    std::uint8_t mode{0U};
    std::int32_t strength_q16_16{0};
    std::uint32_t expected_sample_rate{0U};
    std::uint32_t expected_channels{0U};
    std::uint16_t path_bytes{0U};
    std::array<char, kIrPreparePathMaxBytesV1> path{};
};

[[nodiscard]] std::array<std::uint8_t, kIrPrepareCommandPayloadBytesV1>
encode_ir_prepare_command_v1(const IrPrepareCommandV1& command) noexcept;
[[nodiscard]] bool decode_ir_prepare_command_v1(
    std::span<const std::uint8_t> payload,
    IrPrepareCommandV1& command) noexcept;

[[nodiscard]] bool encode_scene_catalog_command_v1(
    const SceneCatalogCommandV1& command,
    std::vector<std::uint8_t>& payload) noexcept;
[[nodiscard]] bool decode_scene_catalog_command_v1(
    std::span<const std::uint8_t> payload,
    SceneCatalogCommandV1& command) noexcept;

struct CalibrationPeqPrepareCommandV1 {
    std::uint32_t schema_version{1U};
    std::uint8_t filter_count{0U};
    std::uint16_t output_group_bytes{0U};
    std::uint8_t clear_existing{0U};
    std::array<char, kCalibrationPeqOutputGroupMaxBytesV1> output_group{};
    struct WireFilter {
        double frequency_hz{0.0};
        double gain_db{0.0};
        double q{0.0};
    };
    std::array<WireFilter, kCalibrationPeqMaxFiltersV1> filters{};
};

[[nodiscard]] bool encode_calibration_peq_prepare_command_v1(
    const CalibrationPeqPrepareCommandV1& command,
    std::vector<std::uint8_t>& payload) noexcept;
[[nodiscard]] bool decode_calibration_peq_prepare_command_v1(
    std::span<const std::uint8_t> payload,
    CalibrationPeqPrepareCommandV1& command) noexcept;

[[nodiscard]] std::array<std::uint8_t, kSessionRouteCommandPayloadBytesV1>
encode_session_route_command_v1(const SessionRouteCommandV1& command) noexcept;
[[nodiscard]] bool decode_session_route_command_v1(
    std::span<const std::uint8_t> payload,
    SessionRouteCommandV1& command) noexcept;

enum class SessionRouteRuleGainOwnerV1 : std::uint8_t {
    WindowsSession = 0U,
    HibikiInternal = 1U,
};

// Fixed control-plane rule command. The wire caps are intentionally narrower
// than the in-process rule store so a malformed/oversized profile cannot turn
// a pipe request into an unbounded allocation.
struct SessionRouteRuleCommandV1 {
    std::uint32_t schema_version{1U};
    std::int32_t priority{0};
    std::int32_t makeup_gain_q16_16{0};
    SessionRouteRuleOperationV1 operation{SessionRouteRuleOperationV1::Upsert};
    std::uint8_t enabled{1U};
    SessionRouteRuleGainOwnerV1 gain_owner{SessionRouteRuleGainOwnerV1::WindowsSession};
    std::uint64_t catalog_sequence{0U};
    std::uint16_t rule_id_bytes{0U};
    std::uint16_t app_id_bytes{0U};
    std::uint16_t display_name_bytes{0U};
    std::uint16_t lane_bytes{0U};
    std::uint16_t output_group_bytes{0U};
    std::array<char, kSessionRouteRuleIdMaxBytesV1> rule_id{};
    std::array<char, kSessionRouteRuleMatchMaxBytesV1> app_id{};
    std::array<char, kSessionRouteRuleMatchMaxBytesV1> display_name{};
    std::array<char, kSessionRouteRuleRouteMaxBytesV1> lane{};
    std::array<char, kSessionRouteRuleRouteMaxBytesV1> output_group{};
};

[[nodiscard]] std::array<std::uint8_t, kSessionRouteRuleCommandPayloadBytesV1>
encode_session_route_rule_command_v1(
    const SessionRouteRuleCommandV1& command) noexcept;
[[nodiscard]] bool decode_session_route_rule_command_v1(
    std::span<const std::uint8_t> payload,
    SessionRouteRuleCommandV1& command) noexcept;

// Fixed little-endian control payload shared with apps/control-model. The
// legacy 16-byte form stores dB Q16.16 at offset 0, mute at offset 4, reserved
// bytes 5..7 and generation at offset 8. The 48-byte grouped form appends a
// one-byte UTF-8 output-group length at offset 16 and 31 bytes of zero-padded
// label at offset 17. No native C++ struct layout crosses IPC.
[[nodiscard]] std::array<std::uint8_t, kVolumeNotificationPayloadBytesV1>
encode_volume_notification_payload_v1(const VolumeNotificationV1& notification) noexcept;

[[nodiscard]] bool decode_volume_notification_payload_v1(
    std::span<const std::uint8_t> payload,
    VolumeNotificationV1& notification) noexcept;

struct GroupedVolumeNotificationPayloadV1 {
    std::uint8_t output_group_bytes{0U};
    std::array<char, 31> output_group{};
};

[[nodiscard]] std::array<std::uint8_t, kGroupedVolumeNotificationPayloadBytesV1>
encode_grouped_volume_notification_payload_v1(
    std::string_view output_group,
    const VolumeNotificationV1& notification) noexcept;
[[nodiscard]] bool decode_grouped_volume_notification_payload_v1(
    std::span<const std::uint8_t> payload,
    VolumeNotificationV1& notification,
    GroupedVolumeNotificationPayloadV1& target) noexcept;

struct SceneApplyPayloadV1 {
    std::uint8_t scene_id_bytes{0U};
    std::array<char, 31> scene_id{};
    std::uint8_t output_group_bytes{0U};
    std::array<char, 31> output_group{};
};

[[nodiscard]] bool encode_scene_apply_payload_v1(
    std::string_view scene_id,
    std::string_view output_group,
    std::array<std::uint8_t, kSceneApplyPayloadBytesV1>& payload) noexcept;
[[nodiscard]] bool decode_scene_apply_payload_v1(
    std::span<const std::uint8_t> payload,
    SceneApplyPayloadV1& command) noexcept;

struct DeviceSwitchPayloadV1 {
    std::uint16_t endpoint_id_bytes{0U};
    std::array<char, kDeviceSwitchEndpointMaxBytesV1> endpoint_id{};
    std::uint32_t channels{0U};
    std::uint32_t sample_rate{0U};
    std::uint32_t buffer_frames{0U};
    std::uint64_t catalog_sequence{0U};
};

[[nodiscard]] std::array<std::uint8_t, kDeviceSwitchPayloadBytesV1>
encode_device_switch_payload_v1(std::string_view endpoint_id,
                                 std::uint32_t channels,
                                 std::uint32_t sample_rate,
                                 std::uint32_t buffer_frames,
                                 std::uint64_t catalog_sequence) noexcept;
[[nodiscard]] bool decode_device_switch_payload_v1(
    std::span<const std::uint8_t> payload,
    DeviceSwitchPayloadV1& command) noexcept;

// Snapshot wire records are integer/byte-only so this Apache control contract
// does not include the GPL catalog implementation. The engine worker converts
// its descriptors before publishing a DeviceCatalogSnapshot frame.
struct DeviceCatalogSnapshotEntryV1 {
    std::uint16_t endpoint_id_bytes{0U};
    std::array<char, kDeviceSwitchEndpointMaxBytesV1> endpoint_id{};
    std::uint16_t display_name_bytes{0U};
    std::array<char, 128U> display_name{};
    std::uint8_t flow{0U};
    std::uint8_t availability{3U};
    std::uint16_t flags{0U};
    std::uint32_t channels{0U};
    std::uint32_t sample_rate{0U};
    std::uint32_t buffer_frames{0U};
    std::uint64_t last_sequence{0U};
};

struct DeviceCatalogSnapshotV1 {
    std::uint16_t entry_count{0U};
    std::uint64_t catalog_sequence{0U};
    std::array<DeviceCatalogSnapshotEntryV1, kDeviceCatalogSnapshotCapacityV1> entries{};
};

[[nodiscard]] bool encode_device_catalog_snapshot_v1(
    std::span<const DeviceCatalogSnapshotEntryV1> entries,
    std::uint64_t catalog_sequence,
    std::array<std::uint8_t, kDeviceCatalogSnapshotPayloadBytesV1>& payload,
    std::size_t& payload_bytes) noexcept;
[[nodiscard]] bool decode_device_catalog_snapshot_v1(
    std::span<const std::uint8_t> payload,
    DeviceCatalogSnapshotV1& snapshot) noexcept;

// The live EQ surface consumes a bounded confirmed compensation curve. The
// fixed Apache control contract carries frequency/gain pairs only; policy and
// equal-loudness formula inputs stay engine-local. Source 1 is equal loudness. Source 2
// is the bounded projection of the committed program-aware slow level proxy;
// it is control-plane visual feedback, not content analysis or a BS.1770/equal-loudness
// conformance claim.
constexpr std::size_t kEqVisualSnapshotHeaderBytesV1 = 10U;
constexpr std::size_t kEqVisualSnapshotPointBytesV1 = 16U;
constexpr std::size_t kEqVisualSnapshotCapacityV1 = 32U;
constexpr std::size_t kEqVisualSnapshotPayloadBytesV1 =
    kEqVisualSnapshotHeaderBytesV1 +
    (kEqVisualSnapshotPointBytesV1 * kEqVisualSnapshotCapacityV1);

struct EqVisualSnapshotPointV1 {
    double frequency_hz{0.0};
    double gain_db{0.0};
};

struct EqVisualSnapshotV1 {
    std::uint64_t sequence{0U};
    std::uint8_t source{0U};
    std::array<EqVisualSnapshotPointV1, kEqVisualSnapshotCapacityV1> points{};
};

[[nodiscard]] bool encode_eq_visual_snapshot_v1(
    const EqVisualSnapshotV1& snapshot,
    std::array<std::uint8_t, kEqVisualSnapshotPayloadBytesV1>& payload,
    std::size_t& payload_bytes) noexcept;

[[nodiscard]] bool decode_eq_visual_snapshot_v1(
    std::span<const std::uint8_t> payload,
    EqVisualSnapshotV1& snapshot) noexcept;

// Bounded control-plane cache for the latest confirmed EQ visual frame.
// Publication is serialized, replies copy a whole validated frame, and no RT
// code touches this object. A missing snapshot intentionally makes an
// EqVisualSnapshotRequest return Error instead of an empty curve.
class EqVisualSnapshotStoreV1 final {
public:
    EqVisualSnapshotStoreV1() noexcept = default;

    [[nodiscard]] bool publish(const EqVisualSnapshotV1& snapshot) noexcept;
    [[nodiscard]] bool reply(IpcFrameV1& response) const noexcept;
    [[nodiscard]] bool has_snapshot() const noexcept;
    [[nodiscard]] std::uint64_t sequence() const noexcept;

private:
    mutable std::mutex mutex_{};
    std::array<std::uint8_t, kEqVisualSnapshotPayloadBytesV1> payload_{};
    std::size_t payload_bytes_{0U};
    std::uint64_t sequence_{0U};
};

[[nodiscard]] bool eq_visual_snapshot_reply_v1(IpcFrameV1& response,
                                               void* context) noexcept;

struct ControlCommandV1 {
    IpcMessageType type{IpcMessageType::Error};
    std::uint64_t request_id{0U};
    // Grouped volume commands intentionally carry both the dB/mute value and
    // the output-group selector, so these two fields cannot alias. The
    // remaining fixed payloads are also kept as named fields: queue copies
    // must not depend on an anonymous union's active-member lifetime.
    VolumeNotificationV1 volume{};
    GroupedVolumeNotificationPayloadV1 volume_target{};
    SceneApplyPayloadV1 scene{};
    DeviceSwitchPayloadV1 device_switch{};
    SessionVolumeCommandV1 session_volume{};
    SessionRouteCommandV1 session_route{};
    SessionRouteRuleCommandV1 session_route_rule{};
    IrPrepareCommandV1 ir_prepare{};
    SceneCatalogCommandV1 scene_catalog{};
    CalibrationPeqPrepareCommandV1 calibration_peq{};
    bool has_volume_target{false};
};

[[nodiscard]] bool decode_control_command_v1(const IpcFrameV1& frame,
                                             ControlCommandV1& command) noexcept;
[[nodiscard]] IpcFrameV1 make_ack_frame_v1(const IpcFrameV1& request) noexcept;
[[nodiscard]] IpcFrameV1 make_error_frame_v1(const IpcFrameV1& request) noexcept;

}  // namespace hibiki
