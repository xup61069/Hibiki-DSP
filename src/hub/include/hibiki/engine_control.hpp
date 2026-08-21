#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/audio_engine.hpp"
#include "hibiki/control_service.hpp"
#include "hibiki/scene_presets.hpp"
#include "hibiki/scene_catalog.hpp"

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

using ScenePreflightFnV1 = bool (*)(const SceneProfileV1& scene,
                                    void* context) noexcept;
using DeviceSwitchHandlerFnV1 = bool (*)(const DeviceSwitchPayloadV1& request,
                                         void* context) noexcept;
using SessionVolumeHandlerFnV1 = bool (*)(const SessionVolumeCommandV1& request,
                                          void* context) noexcept;

// Control-worker adapter for the fixed queue. It is intentionally not called
// by the pipe callback or the RT process function. SceneApply resolves only
// the four source-controlled Easy presets; Expert graph edits still use the
// explicit graph transaction API.
class EngineControlWorkerV1 final {
public:
    explicit EngineControlWorkerV1(AudioEngineModel& engine) noexcept : engine_(engine) {}

    // Optional control-plane gate for VST3 state, calibration and other
    // external Scene references. It runs before graph Prepare and never from
    // the pipe callback or RT thread.
    void set_scene_preflight(ScenePreflightFnV1 preflight, void* context) noexcept {
        scene_preflight_ = preflight;
        scene_preflight_context_ = context;
    }

    // Non-owning catalog configured by the control worker. Built-in Easy IDs
    // keep their existing behavior; any other ID is resolved from this catalog
    // and must match the payload's output group exactly.
    void set_scene_catalog(const SceneCatalogV1* catalog) noexcept {
        scene_catalog_ = catalog;
    }

    // The callback is control-plane only. It must resolve the request through
    // a PhysicalDeviceCatalog/RecoveryCoordinator and schedule sink handoff;
    // it must not call COM or touch the RT graph from the pipe callback.
    void set_device_switch_handler(DeviceSwitchHandlerFnV1 handler,
                                   void* context) noexcept {
        device_switch_handler_ = handler;
        device_switch_context_ = context;
    }

    // The callback runs on the control worker after the pipe has validated the
    // fixed payload. A Windows adapter may resolve the ephemeral handle and
    // call COM on its owning worker; RT graph code is never entered here.
    void set_session_volume_handler(SessionVolumeHandlerFnV1 handler,
                                    void* context) noexcept {
        session_volume_handler_ = handler;
        session_volume_context_ = context;
    }

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
    ScenePreflightFnV1 scene_preflight_{nullptr};
    void* scene_preflight_context_{nullptr};
    const SceneCatalogV1* scene_catalog_{nullptr};
    DeviceSwitchHandlerFnV1 device_switch_handler_{nullptr};
    void* device_switch_context_{nullptr};
    SessionVolumeHandlerFnV1 session_volume_handler_{nullptr};
    void* session_volume_context_{nullptr};
};

}  // namespace hibiki
