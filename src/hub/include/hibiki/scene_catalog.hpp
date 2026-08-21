#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/contracts.hpp"
#include "hibiki/iso226.hpp"
#include "hibiki/scene_graph.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>

namespace hibiki {

// A persisted/custom Scene is a validated control-plane bundle. The RT graph
// receives only its compiled immutable GraphConfig snapshot; strings and
// vectors never cross into the audio callback.
struct SceneDefinitionV1 {
    std::uint32_t schema_version{1U};
    SceneProfileV1 scene{};
    GraphConfigV1 graph{};
    EqualLoudnessPolicyV1 loudness{};
};

constexpr std::size_t kMaxCustomScenesV1 = 32U;

enum class SceneCatalogResultV1 : std::uint8_t {
    Applied,
    Invalid,
    CapacityExhausted,
    NotFound,
};

[[nodiscard]] bool validate_scene_definition_v1(
    const SceneDefinitionV1& definition) noexcept;

// Fixed-capacity catalog used by the control worker. Upsert builds a complete
// replacement before swapping it into the slot, so an allocation/copy failure
// cannot leave a partially updated Scene visible to a resolver.
class SceneCatalogV1 final {
public:
    SceneCatalogV1() noexcept = default;
    SceneCatalogV1(const SceneCatalogV1&) = delete;
    SceneCatalogV1& operator=(const SceneCatalogV1&) = delete;

    [[nodiscard]] SceneCatalogResultV1 upsert(
        const SceneDefinitionV1& definition) noexcept;
    [[nodiscard]] SceneCatalogResultV1 remove(std::string_view scene_id) noexcept;
    void clear() noexcept;
    [[nodiscard]] const SceneDefinitionV1* find(
        std::string_view scene_id) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

private:
    struct Slot {
        std::unique_ptr<SceneDefinitionV1> definition;
    };

    [[nodiscard]] std::size_t find_index(std::string_view scene_id) const noexcept;

    std::array<Slot, kMaxCustomScenesV1> slots_{};
    std::size_t size_{0U};
};

}  // namespace hibiki
