// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/scene_catalog.hpp"

#include <algorithm>
#include <cmath>

namespace hibiki {
namespace {

constexpr std::size_t kMaxSceneIdBytesV1 = 31U;
constexpr std::size_t kMaxSceneNameBytesV1 = 120U;

bool valid_text(const std::string_view value, const std::size_t maximum) noexcept {
    return !value.empty() && value.size() <= maximum &&
           value.find('\0') == std::string_view::npos;
}

bool valid_scene_id(const std::string_view value) noexcept {
    if (!valid_text(value, kMaxSceneIdBytesV1)) return false;
    for (std::size_t index = 0U; index < value.size(); ++index) {
        const auto ch = static_cast<unsigned char>(value[index]);
        const bool lower = ch >= static_cast<unsigned char>('a') &&
                           ch <= static_cast<unsigned char>('z');
        const bool digit = ch >= static_cast<unsigned char>('0') &&
                           ch <= static_cast<unsigned char>('9');
        const bool separator = ch == static_cast<unsigned char>('.') ||
                               ch == static_cast<unsigned char>('_') ||
                               ch == static_cast<unsigned char>('-');
        if ((!lower && !digit && !separator) || (index == 0U && !lower && !digit)) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool validate_scene_definition_v1(const SceneDefinitionV1& definition) noexcept {
    if (definition.schema_version != 1U || !validate_scene(definition.scene) ||
        !validate_graph(definition.graph) || !validate_policy(definition.loudness) ||
        !valid_scene_id(definition.scene.id) ||
        !valid_text(definition.scene.name, kMaxSceneNameBytesV1) ||
        definition.scene.output_group.size() > kMaxOutputGroupBytes ||
        (definition.scene.latency_mode == LatencyMode::StrictDirect) !=
            definition.graph.strict_direct) {
        return false;
    }
    return true;
}

std::size_t SceneCatalogV1::find_index(const std::string_view scene_id) const noexcept {
    if (!valid_text(scene_id, kMaxSceneIdBytesV1)) return kMaxCustomScenesV1;
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        const auto& slot = slots_[index];
        if (slot.definition != nullptr && slot.definition->scene.id == scene_id) return index;
    }
    return kMaxCustomScenesV1;
}

SceneCatalogResultV1 SceneCatalogV1::upsert(
    const SceneDefinitionV1& definition) noexcept {
    if (!validate_scene_definition_v1(definition)) return SceneCatalogResultV1::Invalid;

    try {
        auto replacement = std::make_unique<SceneDefinitionV1>(definition);
        const auto existing = find_index(definition.scene.id);
        if (existing != kMaxCustomScenesV1) {
            slots_[existing].definition.swap(replacement);
            return SceneCatalogResultV1::Applied;
        }
        if (size_ >= kMaxCustomScenesV1) return SceneCatalogResultV1::CapacityExhausted;
        for (auto& slot : slots_) {
            if (slot.definition != nullptr) continue;
            slot.definition = std::move(replacement);
            ++size_;
            return SceneCatalogResultV1::Applied;
        }
    } catch (...) {
        return SceneCatalogResultV1::Invalid;
    }
    return SceneCatalogResultV1::CapacityExhausted;
}

SceneCatalogResultV1 SceneCatalogV1::remove(const std::string_view scene_id) noexcept {
    const auto index = find_index(scene_id);
    if (index == kMaxCustomScenesV1) return SceneCatalogResultV1::NotFound;
    slots_[index].definition.reset();
    --size_;
    return SceneCatalogResultV1::Applied;
}

void SceneCatalogV1::clear() noexcept {
    for (auto& slot : slots_) slot.definition.reset();
    size_ = 0U;
}

const SceneDefinitionV1* SceneCatalogV1::find(
    const std::string_view scene_id) const noexcept {
    const auto index = find_index(scene_id);
    return index == kMaxCustomScenesV1 ? nullptr : slots_[index].definition.get();
}

}  // namespace hibiki
