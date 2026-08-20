#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/audio_engine.hpp"
#include "hibiki/control_service.hpp"
#include "hibiki/scene_presets.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace hibiki {

enum class EngineControlResultV1 : std::uint8_t {
    Applied,
    Ignored,
    Invalid,
    Failed,
};

// Control-worker adapter for the fixed queue. It is intentionally not called
// by the pipe callback or the RT process function. SceneApply resolves only
// the four source-controlled Easy presets; Expert graph edits still use the
// explicit graph transaction API.
class EngineControlWorkerV1 final {
public:
    explicit EngineControlWorkerV1(AudioEngineModel& engine) noexcept : engine_(engine) {}

    [[nodiscard]] EngineControlResultV1 consume(const ControlCommandV1& command) noexcept;
    [[nodiscard]] std::size_t drain(ControlCommandQueueV1& queue,
                                     std::size_t max_commands = ControlCommandQueueV1::kCapacity) noexcept;

    [[nodiscard]] const SceneProfileV1& active_scene() const noexcept { return active_scene_; }
    [[nodiscard]] bool has_active_scene() const noexcept { return has_active_scene_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    [[nodiscard]] EngineControlResultV1 apply_scene(const SceneApplyPayloadV1& payload) noexcept;

    AudioEngineModel& engine_;
    SceneProfileV1 active_scene_{};
    std::uint64_t revision_{0U};
    bool has_active_scene_{false};
};

}  // namespace hibiki
