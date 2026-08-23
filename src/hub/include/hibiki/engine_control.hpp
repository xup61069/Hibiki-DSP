#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/audio_engine.hpp"
#include "hibiki/control_payloads.hpp"
#include "hibiki/control_service.hpp"
#include "hibiki/scene_presets.hpp"
#include "hibiki/scene_catalog.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
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
using SessionRouteHandlerFnV1 = bool (*)(const SessionRouteCommandV1& request,
                                         void* context) noexcept;
using SessionRouteRuleHandlerFnV1 = bool (*)(const SessionRouteRuleCommandV1& request,
                                             void* context) noexcept;
using IrPrepareHandlerFnV1 = bool (*)(const IrPrepareCommandV1& request,
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

    // Non-owning read-only catalog for SceneApply. Built-in Easy IDs keep
    // their existing behavior; any other ID is resolved from this catalog and
    // must match the payload's output group exactly. Hosts that instead accept
    // SceneCatalog IPC own the writable catalog with
    // ensure_owned_scene_catalog(); these source modes are exclusive.
    void set_scene_catalog(const SceneCatalogV1* catalog) noexcept {
        owned_scene_catalog_.reset();
        mutable_scene_catalog_ = nullptr;
        external_scene_catalog_ = catalog;
    }

    // Ownership variant for hosts that do not keep their own catalog object.
    // The worker owns it for its lifetime and exposes the same non-owning
    // resolution semantics to SceneApply.
    void ensure_owned_scene_catalog() noexcept {
        if (!owned_scene_catalog_) {
            try {
                owned_scene_catalog_ = std::make_unique<SceneCatalogV1>();
            } catch (...) {
                owned_scene_catalog_.reset();
            }
        }

        external_scene_catalog_ = nullptr;
        mutable_scene_catalog_ = owned_scene_catalog_.get();
    }

    [[nodiscard]] SceneCatalogV1* mutable_scene_catalog() noexcept {
        ensure_owned_scene_catalog();
        return owned_scene_catalog_.get();
    }

    [[nodiscard]] const SceneCatalogV1* active_scene_catalog() const noexcept {
        return mutable_scene_catalog_ != nullptr ? mutable_scene_catalog_
                                                 : external_scene_catalog_;
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

    void set_session_route_handler(SessionRouteHandlerFnV1 handler,
                                   void* context) noexcept {
        session_route_handler_ = handler;
        session_route_context_ = context;
    }

    void set_session_route_rule_handler(SessionRouteRuleHandlerFnV1 handler,
                                        void* context) noexcept {
        session_route_rule_handler_ = handler;
        session_route_rule_context_ = context;
    }

    void set_ir_prepare_handler(IrPrepareHandlerFnV1 handler, void* context) noexcept {
        ir_prepare_handler_ = handler;
        ir_prepare_context_ = context;
    }

    [[nodiscard]] EngineControlResultV1 consume(const ControlCommandV1& command) noexcept;
    [[nodiscard]] std::size_t drain(ControlCommandQueueV1& queue,
                                     std::size_t max_commands = ControlCommandQueueV1::kCapacity) noexcept;

    [[nodiscard]] const SceneProfileV1& active_scene() const noexcept { return active_scene_; }
    [[nodiscard]] bool has_active_scene() const noexcept { return has_active_scene_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    [[nodiscard]] EngineControlResultV1 apply_scene(const SceneApplyPayloadV1& payload) noexcept;
    [[nodiscard]] EngineControlResultV1 apply_scene_catalog(
        const SceneCatalogCommandV1& payload) noexcept;

    AudioEngineModel& engine_;
    SceneProfileV1 active_scene_{};
    std::uint64_t revision_{0U};
    bool has_active_scene_{false};
    ScenePreflightFnV1 scene_preflight_{nullptr};
    void* scene_preflight_context_{nullptr};
    const SceneCatalogV1* external_scene_catalog_{nullptr};
    SceneCatalogV1* mutable_scene_catalog_{nullptr};
    std::unique_ptr<SceneCatalogV1> owned_scene_catalog_{};
    DeviceSwitchHandlerFnV1 device_switch_handler_{nullptr};
    void* device_switch_context_{nullptr};
    SessionVolumeHandlerFnV1 session_volume_handler_{nullptr};
    void* session_volume_context_{nullptr};
    SessionRouteHandlerFnV1 session_route_handler_{nullptr};
    void* session_route_context_{nullptr};
    SessionRouteRuleHandlerFnV1 session_route_rule_handler_{nullptr};
    void* session_route_rule_context_{nullptr};
    IrPrepareHandlerFnV1 ir_prepare_handler_{nullptr};
    void* ir_prepare_context_{nullptr};
};

}  // namespace hibiki
