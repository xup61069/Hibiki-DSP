#include "hibiki/engine_control.hpp"

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
        if (scene_kind_from_id(scene_id, kind)) {
            auto candidate = make_easy_scene(kind, std::string(output_group));
            candidate_scene = std::move(candidate.scene);
            candidate_graph = std::move(candidate.graph);
        } else {
            if (scene_catalog_ == nullptr) return EngineControlResultV1::Invalid;
            const auto* const definition = scene_catalog_->find(scene_id);
            if (definition == nullptr || definition->scene.output_group != output_group) {
                return EngineControlResultV1::Invalid;
            }
            candidate_scene = definition->scene;
            candidate_graph = definition->graph;
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
        // Swapping vectors/strings is noexcept, so a failed commit can restore
        // the prior active Scene without a second allocation.
        std::swap(active_scene_, candidate_scene);
        if (!engine_.commit_graph()) {
            std::swap(active_scene_, candidate_scene);
            engine_.rollback_graph();
            return EngineControlResultV1::Failed;
        }
        revision_ = next_revision;
        has_active_scene_ = true;
        return EngineControlResultV1::Applied;
    } catch (...) {
        engine_.rollback_graph();
        return EngineControlResultV1::Failed;
    }
}

EngineControlResultV1 EngineControlWorkerV1::consume(
    const ControlCommandV1& command) noexcept {
    switch (command.type) {
        case IpcMessageType::Hello:
            return EngineControlResultV1::Ignored;
        case IpcMessageType::VolumeNotification:
            return (command.has_volume_target
                        ? engine_.apply_windows_volume(
                              std::string_view(command.volume_target.output_group.data(),
                                               command.volume_target.output_group_bytes),
                              command.volume)
                        : engine_.apply_windows_volume(command.volume)) ==
                       VolumeNotificationResult::Accepted
                       ? EngineControlResultV1::Applied
                       : EngineControlResultV1::Invalid;
        case IpcMessageType::SceneApply:
            return apply_scene(command.scene);
        case IpcMessageType::GraphCommit:
            return engine_.commit_graph() ? EngineControlResultV1::Applied
                                          : EngineControlResultV1::Failed;
        case IpcMessageType::GraphRollback:
            engine_.rollback_graph();
            return EngineControlResultV1::Applied;
        case IpcMessageType::GraphPrepare:
        case IpcMessageType::Ack:
        case IpcMessageType::Error:
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
