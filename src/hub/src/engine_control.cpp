#include "hibiki/engine_control.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace hibiki {
namespace {

bool scene_kind_from_id(const std::string_view id, EasySceneKind& kind) noexcept {
    if (id == "game") {
        kind = EasySceneKind::Game;
        return true;
    }
    if (id == "movie") {
        kind = EasySceneKind::Movie;
        return true;
    }
    if (id == "voice") {
        kind = EasySceneKind::Voice;
        return true;
    }
    if (id == "studio") {
        kind = EasySceneKind::Studio;
        return true;
    }
    return false;
}

}  // namespace

EngineControlResultV1 EngineControlWorkerV1::apply_scene_catalog(
    const SceneCatalogCommandV1& payload) noexcept {
    if (payload.operation == SessionRouteRuleOperationV1::Clear) {
        if (mutable_scene_catalog() != nullptr) mutable_scene_catalog()->clear();
        return EngineControlResultV1::Applied;
    }

    const std::string_view scene_id(payload.id.data(), payload.id_bytes);
    if (payload.operation == SessionRouteRuleOperationV1::Remove) {
        auto* const catalog = mutable_scene_catalog();
        if (catalog == nullptr ||
            catalog->remove(scene_id) != SceneCatalogResultV1::Applied) {
            return EngineControlResultV1::Invalid;
        }
        return EngineControlResultV1::Applied;
    }

    SceneDefinitionV1 definition{};
    definition.scene.schema_version = 1U;
    definition.scene.id.assign(scene_id);
    definition.scene.name.assign(payload.name.data(), payload.name_bytes);
    definition.scene.output_group.assign(payload.output_group.data(),
                                         payload.output_group_bytes);
    if (payload.ir_reference_bytes > 0U) {
        definition.scene.ir_reference.assign(payload.ir_reference.data(),
                                             payload.ir_reference_bytes);
    }
    definition.scene.latency_mode = payload.latency_mode;
    definition.scene.ir_phase.schema_version = 1U;
    definition.scene.ir_phase.mode = payload.ir_phase_mode;
    definition.scene.ir_phase.strength = payload.ir_phase_strength;
    definition.scene.auto_attenuate = payload.auto_attenuate != 0U;
    definition.scene.limiter_dbtp = payload.limiter_dbtp;

    definition.graph.schema_version = 1U;
    definition.graph.output_channels = payload.graph_output_channels;
    definition.graph.strict_direct = payload.strict_direct != 0U;
    for (std::size_t lane_index = 0U; lane_index < payload.lane_count; ++lane_index) {
        const auto& wire_lane = payload.lanes[lane_index];
        LaneConfigV1 lane{};
        lane.id.assign(wire_lane.id.data(), wire_lane.id_bytes);
        lane.output_group.assign(wire_lane.output_group.data(),
                                 wire_lane.output_group_bytes);
        lane.channel_count = wire_lane.channel_count;
        lane.makeup_gain_db = static_cast<double>(wire_lane.makeup_gain_db);
        lane.enabled = wire_lane.enabled != 0U;
        lane.reported_latency_samples = wire_lane.reported_latency_samples;
        lane.channel_map = wire_lane.channel_map;
        lane.matrix_enabled = wire_lane.matrix_enabled != 0U;
        lane.channel_matrix = wire_lane.channel_matrix;
        definition.graph.lanes.push_back(std::move(lane));
    }

    definition.loudness.schema_version = 1U;
    switch (payload.standard_id) {
        case 1U:
            definition.loudness.standard = "iso-226-2023-derived";
            break;
        case 2U:
            definition.loudness.standard = "iso-226-2023-calibrated";
            break;
        default:
            definition.loudness.standard = "invalid";
            break;
    }
    definition.loudness.mode = payload.loudness_mode;
    definition.loudness.reference_phon = payload.reference_phon;
    definition.loudness.strength = payload.strength;
    definition.loudness.max_boost_db = payload.max_boost_db;
    definition.loudness.measured_f3_hz = payload.measured_f3_hz;
    if (payload.anchor_id_bytes > 0U) {
        definition.loudness.anchor_id.assign(payload.anchor_id.data(),
                                             payload.anchor_id_bytes);
    }
    definition.loudness.calibrated = payload.calibrated_flag != 0U;
    definition.loudness.live_update_enabled = payload.loudness_live_update != 0U;

    auto* const catalog = mutable_scene_catalog();
    if (catalog == nullptr ||
        !validate_scene_definition_v1(definition) ||
        catalog->upsert(definition) != SceneCatalogResultV1::Applied) {
        return EngineControlResultV1::Invalid;
    }
    return EngineControlResultV1::Applied;
}

EngineControlResultV1 EngineControlWorkerV1::apply_scene(
    const SceneApplyPayloadV1& payload) noexcept {
    const std::string_view scene_id(payload.scene_id.data(), payload.scene_id_bytes);
    const std::string_view output_group(payload.output_group.data(), payload.output_group_bytes);
    EasySceneKind kind{};
    if (output_group.empty()) {
        return EngineControlResultV1::Invalid;
    }

    try {
        SceneProfileV1 candidate_scene{};
        GraphConfigV1 candidate_graph{};
        EqualLoudnessPolicyV1 candidate_loudness{};
        ProgramAwareLevelPolicyV1 candidate_program_aware{};
        if (scene_kind_from_id(scene_id, kind)) {
            auto candidate = make_easy_scene(kind, std::string(output_group));
            candidate_scene = std::move(candidate.scene);
            candidate_graph = std::move(candidate.graph);
            candidate_loudness = candidate.loudness;
            candidate_program_aware = candidate.program_aware;
        } else {
            if (active_scene_catalog() == nullptr) return EngineControlResultV1::Invalid;
            const auto* const definition = active_scene_catalog()->find(scene_id);
            if (definition == nullptr || definition->scene.output_group != output_group) {
                return EngineControlResultV1::Invalid;
            }
            candidate_scene = definition->scene;
            candidate_graph = definition->graph;
            candidate_loudness = definition->loudness;
            // Custom catalog scenes do not yet carry a program-aware policy;
            // the attachment stays cleared for them.
        }
        if (scene_preflight_ != nullptr &&
            !scene_preflight_(candidate_scene, scene_preflight_context_)) {
            return EngineControlResultV1::Failed;
        }
        const auto next_revision = revision_ + 1U;
        if (!engine_.prepare_graph(candidate_graph, next_revision)) {
            engine_.rollback_graph();
            return EngineControlResultV1::Failed;
        }
        // SceneProfileV1 may carry an opaque calibration label. When the
        // incoming scene references the exact same non-empty ir_reference and
        // targets the same output group, an already committed attachment can
        // survive the switch. Any other case detaches the previous IR inside
        // this same control transaction so an unrelated Movie calibration
        // cannot silently survive a later Game/Studio scene switch; the user
        // must explicitly prepare a new IR afterwards.
        const bool keep_referenced_ir =
            !candidate_scene.ir_reference.empty() &&
            candidate_scene.ir_reference == active_scene_.ir_reference &&
            has_active_scene_ &&
            engine_.ir_transaction_idle();
        if (!keep_referenced_ir) {
            if (!engine_.prepare_ir_clear()) {
                engine_.rollback_graph();
                engine_.rollback_ir();
                engine_.rollback_loudness_peq();
                engine_.rollback_program_aware();
                return EngineControlResultV1::Failed;
            }
        }
        // A scene whose equal-loudness policy is meaningful (Relative mode,
        // positive strength) and explicitly opts in to live phon recompute
        // mounts its bounded formula attachment inside this same
        // transaction; the single-point 1 kHz proxy is the same caller-owned
        // formula shape used by tests and the live recompute path. Scenes
        // without the opt-in keep the legacy clear-only behavior.
        const bool mount_loudness_peq =
            candidate_loudness.mode == EqualLoudnessMode::Relative &&
            candidate_loudness.strength > 0.0 &&
            candidate_loudness.live_update_enabled &&
            validate_policy(candidate_loudness);
        if (mount_loudness_peq) {
            const Iso226FormulaPointV1 live_points{
                1000.0, 0.30, 2.4, 0.0};
            if (!engine_.prepare_loudness_peq(
                    output_group,
                    std::span<const Iso226FormulaPointV1>(&live_points, 1U),
                    candidate_loudness.reference_phon,
                    candidate_loudness)) {
                engine_.rollback_graph();
                if (!keep_referenced_ir) engine_.rollback_ir();
                engine_.rollback_loudness_peq();
                engine_.rollback_program_aware();
                return EngineControlResultV1::Failed;
            }
        }
        // A freshly mounted scene attachment replaces the previous one in
        // this transaction; without a mount, keep the legacy clear-only path.
        if (!mount_loudness_peq && !engine_.prepare_loudness_peq_clear()) {
            engine_.rollback_graph();
            if (!keep_referenced_ir) engine_.rollback_ir();
            engine_.rollback_loudness_peq();
            engine_.rollback_program_aware();
            return EngineControlResultV1::Failed;
        }
        if (!engine_.prepare_program_aware_clear()) {
            engine_.rollback_graph();
            engine_.rollback_ir();
            engine_.rollback_loudness_peq();
            engine_.rollback_program_aware();
            return EngineControlResultV1::Failed;
        }
        if (candidate_program_aware.enabled &&
            !engine_.prepare_program_aware(output_group, candidate_program_aware)) {
            engine_.rollback_graph();
            engine_.rollback_ir();
            engine_.rollback_loudness_peq();
            engine_.rollback_program_aware();
            return EngineControlResultV1::Failed;
        }
        // Capture the opt-in request before the swap below replaces
        // candidate_loudness with the previous scene's policy.
        const bool enable_live_after_commit =
            mount_loudness_peq && candidate_loudness.live_update_enabled;
        std::swap(active_scene_, candidate_scene);
        std::swap(active_loudness_, candidate_loudness);
        if (!engine_.commit_graph()) {
            std::swap(active_scene_, candidate_scene);
            std::swap(active_loudness_, candidate_loudness);
            engine_.rollback_graph();
            engine_.rollback_ir();
            engine_.rollback_loudness_peq();
            engine_.rollback_program_aware();
            return EngineControlResultV1::Failed;
        }
        if (!keep_referenced_ir && !engine_.commit_ir()) {
            std::swap(active_scene_, candidate_scene);
            std::swap(active_loudness_, candidate_loudness);
            engine_.rollback_graph();
            engine_.rollback_ir();
            engine_.rollback_loudness_peq();
            engine_.rollback_program_aware();
            return EngineControlResultV1::Failed;
        }
        if (!engine_.commit_loudness_peq()) {
            std::swap(active_scene_, candidate_scene);
            std::swap(active_loudness_, candidate_loudness);
            engine_.rollback_graph();
            engine_.rollback_ir();
            engine_.rollback_loudness_peq();
            engine_.rollback_program_aware();
            return EngineControlResultV1::Failed;
        }
        // Program-aware is the final commit in the scene transaction; a
        // failure here still rolls back every earlier swap above.
        if (!engine_.commit_program_aware()) {
            std::swap(active_scene_, candidate_scene);
            std::swap(active_loudness_, candidate_loudness);
            engine_.rollback_graph();
            engine_.rollback_ir();
            engine_.rollback_loudness_peq();
            engine_.rollback_program_aware();
            return EngineControlResultV1::Failed;
        }
        // The live-update switch belongs to the committed attachment; opt in
        // only after every commit succeeded. Failure is non-fatal: the
        // attachment stays mounted but live recompute remains disabled.
        if (enable_live_after_commit) {
            engine_.set_loudness_live_update(output_group, true);
        }
        revision_ = next_revision;
        has_active_scene_ = true;
        return EngineControlResultV1::Applied;
    } catch (...) {
        engine_.rollback_graph();
        engine_.rollback_ir();
        engine_.rollback_loudness_peq();
        engine_.rollback_program_aware();
        return EngineControlResultV1::Failed;
    }
}

EngineControlResultV1 EngineControlWorkerV1::consume(
    const ControlCommandV1& command) noexcept {
    switch (command.type) {
        case IpcMessageType::Hello:
        case IpcMessageType::DeviceCatalogRequest:
        case IpcMessageType::ControlStatusRequest:
        case IpcMessageType::SessionCatalogRequest:
            return EngineControlResultV1::Ignored;
        case IpcMessageType::SessionVolumeCommand:
            if (session_volume_handler_ == nullptr) return EngineControlResultV1::Failed;
            return session_volume_handler_(command.session_volume, session_volume_context_)
                       ? EngineControlResultV1::Applied
                       : EngineControlResultV1::Failed;
        case IpcMessageType::SessionRouteCommand:
            if (session_route_handler_ == nullptr) return EngineControlResultV1::Failed;
            return session_route_handler_(command.session_route, session_route_context_)
                       ? EngineControlResultV1::Applied
                       : EngineControlResultV1::Failed;
        case IpcMessageType::SessionRouteRuleCommand:
            if (session_route_rule_handler_ == nullptr) return EngineControlResultV1::Failed;
            return session_route_rule_handler_(command.session_route_rule,
                                               session_route_rule_context_)
                       ? EngineControlResultV1::Applied
                       : EngineControlResultV1::Failed;
        case IpcMessageType::IrPrepareCommand:
            if (ir_prepare_handler_ == nullptr) return EngineControlResultV1::Failed;
            return ir_prepare_handler_(command.ir_prepare, ir_prepare_context_)
                       ? EngineControlResultV1::Applied
                       : EngineControlResultV1::Failed;
        case IpcMessageType::VolumeNotification:
            {
                const auto result = command.has_volume_target
                        ? engine_.apply_windows_volume(
                              std::string_view(command.volume_target.output_group.data(),
                                               command.volume_target.output_group_bytes),
                              command.volume)
                        : engine_.apply_windows_volume(command.volume);
                if (result == VolumeNotificationResult::Accepted) {
                    // Bounded proxy: 70 phon corresponds to the 0 dBFS
                    // reference; each dB of requested Windows volume moves the
                    // estimate one phon, clamped to the safe domain. The
                    // engine-side live-update switch stays opt-in and debounced.
                    const double estimated_phon =
                        std::clamp(70.0 + command.volume.requested_db, 20.0, 90.0);
                    const std::string_view target_group(
                        command.has_volume_target
                            ? std::string_view(command.volume_target.output_group.data(),
                                               command.volume_target.output_group_bytes)
                            : std::string_view{"main"});
                    if (engine_.update_loudness_phon(target_group, estimated_phon)) {
                        const auto curve = engine_.loudness_curve_snapshot();
                        EqVisualSnapshotV1 visual{};
                        visual.sequence = next_eq_visual_sequence_;
                        visual.source = 1U;
                        for (std::size_t index = 0U; index < curve.point_count &&
                             index < visual.points.size(); ++index) {
                            visual.points[index] = curve.points[index];
                        }
                        std::array<std::uint8_t,
                                   kEqVisualSnapshotPayloadBytesV1> encoded_snapshot{};
                        std::size_t encoded_bytes = 0U;
                        EqVisualSnapshotV1 decoded_snapshot{};
                        if (eq_visual_publisher_ != nullptr &&
                            encode_eq_visual_snapshot_v1(visual, encoded_snapshot,
                                                         encoded_bytes) &&
                            decode_eq_visual_snapshot_v1(
                                std::span<const std::uint8_t>(encoded_snapshot.data(),
                                                              encoded_bytes),
                                decoded_snapshot)) {
                            ++next_eq_visual_sequence_;
                            eq_visual_publisher_(decoded_snapshot,
                                                 eq_visual_publisher_context_);
                        }
                    }
                }
                return result == VolumeNotificationResult::Accepted
                           ? EngineControlResultV1::Applied
                           : EngineControlResultV1::Invalid;
            }
        case IpcMessageType::SceneApply:
            return apply_scene(command.scene);
        case IpcMessageType::SceneCatalogCommand:
            return apply_scene_catalog(command.scene_catalog);
        case IpcMessageType::DeviceSwitch:
            if (device_switch_handler_ == nullptr) return EngineControlResultV1::Failed;
            return device_switch_handler_(command.device_switch, device_switch_context_)
                       ? EngineControlResultV1::Applied
                       : EngineControlResultV1::Failed;
        case IpcMessageType::GraphCommit:
            return engine_.commit_graph() ? EngineControlResultV1::Applied
                                          : EngineControlResultV1::Failed;
        case IpcMessageType::GraphRollback:
            engine_.rollback_graph();
            return EngineControlResultV1::Applied;
        case IpcMessageType::GraphPrepare:
        case IpcMessageType::Ack:
        case IpcMessageType::Error:
        case IpcMessageType::ControlStatusSnapshot:
        case IpcMessageType::SessionCatalogSnapshot:
            return EngineControlResultV1::Invalid;
    }
    return EngineControlResultV1::Invalid;
}

std::size_t EngineControlWorkerV1::drain(ControlCommandQueueV1& queue,
                                         const std::size_t max_commands) noexcept {
    std::size_t processed = 0U;
    ControlCommandV1 command{};
    while (processed < max_commands && queue.try_pop(command)) {
        (void)consume(command);
        ++processed;
    }
    return processed;
}

}  // namespace hibiki
