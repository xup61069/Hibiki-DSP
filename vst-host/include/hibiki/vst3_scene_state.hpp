#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "hibiki/vst3_plugin_state.hpp"
#include "hibiki/contracts.hpp"

namespace hibiki {

constexpr std::size_t kVst3SceneStateMaxBindingsV1 = 16U;
constexpr std::size_t kVst3SceneStateMaxIdBytesV1 = 64U;

struct Vst3SceneStateBindingV1 {
    std::string scene_id;
    std::string state_id;
    Vst3PluginStateIdentityV1 identity{};
    std::uint32_t target_state_version{0U};
};

enum class Vst3SceneStateResultV1 : std::uint8_t {
    ok,
    invalid_argument,
    not_prepared,
    capacity_exhausted,
    missing_binding,
    missing_state,
    identity_mismatch,
    migration_unavailable,
    migration_failed,
    output_too_small,
    output_too_large,
};

// Control-plane coordinator for opaque VST3 state references owned by a
// Scene. It keeps only IDs and identity metadata; state bytes remain in the
// private store and every restore writes to a caller-owned buffer.
class Vst3SceneStateCoordinatorV1 final {
public:
    Vst3SceneStateCoordinatorV1() noexcept = default;

    [[nodiscard]] bool prepare(const Vst3PluginStateStoreV1& store,
                               const Vst3PluginStateMigrationRegistryV1& migrations) noexcept;
    void clear() noexcept;

    [[nodiscard]] Vst3SceneStateResultV1 bind(
        std::string_view scene_id,
        std::string_view state_id,
        const Vst3PluginStateIdentityV1& identity,
        std::uint32_t target_state_version);
    [[nodiscard]] bool remove(std::string_view scene_id,
                              std::string_view state_id) noexcept;

    // Validate every state reference for a Scene before activation. Exact
    // versions are accepted; mismatches require one explicit registry rule.
    [[nodiscard]] Vst3SceneStateResultV1 validate_scene(
        std::string_view scene_id) const noexcept;

    [[nodiscard]] Vst3SceneStateResultV1 restore(
        std::string_view scene_id,
        std::string_view state_id,
        std::span<std::uint8_t> destination,
        std::size_t& bytes_written) const noexcept;

    [[nodiscard]] std::size_t binding_count() const noexcept { return binding_count_; }

private:
    struct Slot {
        bool occupied{false};
        Vst3SceneStateBindingV1 binding{};
    };

    [[nodiscard]] bool valid_id(std::string_view id) const noexcept;
    [[nodiscard]] std::size_t find(std::string_view scene_id,
                                   std::string_view state_id) const noexcept;
    [[nodiscard]] Vst3SceneStateResultV1 map_store_result(
        Vst3PluginStateResultV1 result) const noexcept;

    const Vst3PluginStateStoreV1* store_{nullptr};
    const Vst3PluginStateMigrationRegistryV1* migrations_{nullptr};
    std::array<Slot, kVst3SceneStateMaxBindingsV1> slots_{};
    std::size_t binding_count_{0U};
};

// Adapter for EngineControlWorkerV1::set_scene_preflight. A coordinator with
// no bindings is treated as a no-op for backward-compatible Easy Scenes;
// once bindings exist, the matching Scene must pass the full metadata/migration
// preflight before graph Prepare.
[[nodiscard]] bool preflight_scene_vst3_state_v1(const SceneProfileV1& scene,
                                                 void* context) noexcept;

}  // namespace hibiki
