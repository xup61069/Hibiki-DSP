// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_scene_state.hpp"

#include <new>

namespace hibiki {
namespace {

bool same_identity(const Vst3PluginStateIdentityV1& left,
                   const Vst3PluginStateIdentityV1& right) noexcept {
    return left.plugin_id == right.plugin_id && left.class_id == right.class_id &&
           left.module_sha256 == right.module_sha256;
}

}  // namespace

bool Vst3SceneStateCoordinatorV1::valid_id(const std::string_view id) const noexcept {
    return !id.empty() && id.size() <= kVst3SceneStateMaxIdBytesV1 &&
           id.find('\0') == std::string_view::npos;
}

std::size_t Vst3SceneStateCoordinatorV1::find(const std::string_view scene_id,
                                              const std::string_view state_id) const noexcept {
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        if (slots_[index].occupied && slots_[index].binding.scene_id == scene_id &&
            slots_[index].binding.state_id == state_id) {
            return index;
        }
    }
    return slots_.size();
}

bool Vst3SceneStateCoordinatorV1::prepare(
    const Vst3PluginStateStoreV1& store,
    const Vst3PluginStateMigrationRegistryV1& migrations) noexcept {
    store_ = &store;
    migrations_ = &migrations;
    return true;
}

void Vst3SceneStateCoordinatorV1::clear() noexcept {
    for (auto& slot : slots_) slot = {};
    binding_count_ = 0U;
    store_ = nullptr;
    migrations_ = nullptr;
}

Vst3SceneStateResultV1 Vst3SceneStateCoordinatorV1::bind(
    const std::string_view scene_id,
    const std::string_view state_id,
    const Vst3PluginStateIdentityV1& identity,
    const std::uint32_t target_state_version) {
    if (store_ == nullptr || migrations_ == nullptr) return Vst3SceneStateResultV1::not_prepared;
    if (!valid_id(scene_id) || !valid_id(state_id) ||
        !validate_vst3_plugin_state_identity_v1(identity) || target_state_version == 0U) {
        return Vst3SceneStateResultV1::invalid_argument;
    }
    auto index = find(scene_id, state_id);
    try {
        if (index == slots_.size()) {
            for (std::size_t candidate = 0U; candidate < slots_.size(); ++candidate) {
                if (!slots_[candidate].occupied) {
                    index = candidate;
                    break;
                }
            }
            if (index == slots_.size()) return Vst3SceneStateResultV1::capacity_exhausted;
            slots_[index].occupied = true;
            ++binding_count_;
        }
        slots_[index].binding.scene_id.assign(scene_id.data(), scene_id.size());
        slots_[index].binding.state_id.assign(state_id.data(), state_id.size());
        slots_[index].binding.identity = identity;
        slots_[index].binding.target_state_version = target_state_version;
        return Vst3SceneStateResultV1::ok;
    } catch (const std::bad_alloc&) {
        return Vst3SceneStateResultV1::capacity_exhausted;
    }
}

bool Vst3SceneStateCoordinatorV1::remove(const std::string_view scene_id,
                                         const std::string_view state_id) noexcept {
    const auto index = find(scene_id, state_id);
    if (index == slots_.size()) return false;
    slots_[index] = {};
    --binding_count_;
    return true;
}

Vst3SceneStateResultV1 Vst3SceneStateCoordinatorV1::map_store_result(
    const Vst3PluginStateResultV1 result) const noexcept {
    switch (result) {
    case Vst3PluginStateResultV1::ok:
        return Vst3SceneStateResultV1::ok;
    case Vst3PluginStateResultV1::missing:
        return Vst3SceneStateResultV1::missing_state;
    case Vst3PluginStateResultV1::identity_mismatch:
        return Vst3SceneStateResultV1::identity_mismatch;
    case Vst3PluginStateResultV1::migration_unavailable:
        return Vst3SceneStateResultV1::migration_unavailable;
    case Vst3PluginStateResultV1::migration_failed:
        return Vst3SceneStateResultV1::migration_failed;
    case Vst3PluginStateResultV1::output_too_small:
        return Vst3SceneStateResultV1::output_too_small;
    case Vst3PluginStateResultV1::migration_output_too_large:
        return Vst3SceneStateResultV1::output_too_large;
    case Vst3PluginStateResultV1::invalid_argument:
    case Vst3PluginStateResultV1::capacity_exhausted:
    case Vst3PluginStateResultV1::version_mismatch:
        return Vst3SceneStateResultV1::invalid_argument;
    }
    return Vst3SceneStateResultV1::invalid_argument;
}

Vst3SceneStateResultV1 Vst3SceneStateCoordinatorV1::validate_scene(
    const std::string_view scene_id) const noexcept {
    if (store_ == nullptr || migrations_ == nullptr) return Vst3SceneStateResultV1::not_prepared;
    if (!valid_id(scene_id)) return Vst3SceneStateResultV1::invalid_argument;
    std::size_t matched = 0U;
    for (const auto& slot : slots_) {
        if (!slot.occupied || slot.binding.scene_id != scene_id) continue;
        ++matched;
        std::uint32_t source_version = 0U;
        std::size_t byte_count = 0U;
        const auto inspected = store_->inspect(slot.binding.state_id, slot.binding.identity,
                                               source_version, byte_count);
        if (inspected != Vst3PluginStateResultV1::ok) return map_store_result(inspected);
        if (byte_count > kVst3PluginStateMaxBytesV1) {
            return Vst3SceneStateResultV1::output_too_large;
        }
        if (source_version != slot.binding.target_state_version &&
            !migrations_->has_rule(slot.binding.identity, source_version,
                                   slot.binding.target_state_version)) {
            return Vst3SceneStateResultV1::migration_unavailable;
        }
    }
    return matched == 0U ? Vst3SceneStateResultV1::missing_binding
                         : Vst3SceneStateResultV1::ok;
}

Vst3SceneStateResultV1 Vst3SceneStateCoordinatorV1::restore(
    const std::string_view scene_id,
    const std::string_view state_id,
    const std::span<std::uint8_t> destination,
    std::size_t& bytes_written) const noexcept {
    bytes_written = 0U;
    if (store_ == nullptr || migrations_ == nullptr) return Vst3SceneStateResultV1::not_prepared;
    const auto index = find(scene_id, state_id);
    if (index == slots_.size()) return Vst3SceneStateResultV1::missing_binding;
    const auto& binding = slots_[index].binding;
    const auto result = migrations_->restore(*store_, binding.state_id, binding.identity,
                                              binding.target_state_version, destination,
                                              bytes_written);
    return map_store_result(result);
}

}  // namespace hibiki
