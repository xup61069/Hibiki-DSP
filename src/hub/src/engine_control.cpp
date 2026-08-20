#include "hibiki/engine_control.hpp"

#include <string_view>

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
    if (output_group.empty() || !scene_kind_from_id(scene_id, kind)) {
        return EngineControlResultV1::Invalid;
    }

    const auto candidate = make_easy_scene(kind, std::string(output_group));
    const auto next_revision = revision_ + 1U;
    if (!engine_.prepare_graph(candidate.graph, next_revision)) {
        engine_.rollback_graph();
        return EngineControlResultV1::Failed;
    }
    if (!engine_.commit_graph()) {
        engine_.rollback_graph();
        return EngineControlResultV1::Failed;
    }
    active_scene_ = candidate.scene;
    revision_ = next_revision;
    has_active_scene_ = true;
    return EngineControlResultV1::Applied;
}

EngineControlResultV1 EngineControlWorkerV1::consume(
    const ControlCommandV1& command) noexcept {
    switch (command.type) {
        case IpcMessageType::Hello:
            return EngineControlResultV1::Ignored;
        case IpcMessageType::VolumeNotification:
            return engine_.apply_windows_volume(command.volume) == VolumeNotificationResult::Accepted
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
