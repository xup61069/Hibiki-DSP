// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_plugin_state.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <new>

namespace hibiki {
namespace {

bool valid_hex_class_id(const std::string& class_id) noexcept {
    if (class_id.size() != 32U) return false;
    for (const auto character : class_id) {
        if (!std::isxdigit(static_cast<unsigned char>(character))) return false;
    }
    return true;
}

bool has_hash_byte(const std::array<std::uint8_t, 32U>& hash) noexcept {
    for (const auto byte : hash) {
        if (byte != 0U) return true;
    }
    return false;
}

bool same_identity(const Vst3PluginStateIdentityV1& left,
                   const Vst3PluginStateIdentityV1& right) noexcept {
    return left.plugin_id == right.plugin_id && left.class_id == right.class_id &&
           left.module_sha256 == right.module_sha256;
}

}  // namespace

bool validate_vst3_plugin_state_identity_v1(
    const Vst3PluginStateIdentityV1& identity) noexcept {
    return !identity.plugin_id.empty() && identity.plugin_id.size() <=
               kVst3PluginStateMaxPluginIdBytesV1 &&
           identity.plugin_id.find('\0') == std::string::npos &&
           valid_hex_class_id(identity.class_id) && has_hash_byte(identity.module_sha256);
}

bool Vst3PluginStateStoreV1::valid_state_id(const std::string_view state_id) const noexcept {
    return !state_id.empty() && state_id.size() <= kVst3PluginStateMaxIdBytesV1 &&
           state_id.find('\0') == std::string_view::npos;
}

std::size_t Vst3PluginStateStoreV1::find(const std::string_view state_id) const noexcept {
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        if (slots_[index].occupied && slots_[index].state_id == state_id) return index;
    }
    return slots_.size();
}

Vst3PluginStateResultV1 Vst3PluginStateStoreV1::capture(
    const std::string_view state_id,
    const Vst3PluginStateIdentityV1& identity,
    const std::uint32_t state_version,
    const std::span<const std::uint8_t> bytes) {
    if (!valid_state_id(state_id) || !validate_vst3_plugin_state_identity_v1(identity) ||
        state_version == 0U || bytes.size() > kVst3PluginStateMaxBytesV1) {
        return Vst3PluginStateResultV1::invalid_argument;
    }
    auto index = find(state_id);
    if (index == slots_.size()) {
        for (std::size_t candidate = 0U; candidate < slots_.size(); ++candidate) {
            if (!slots_[candidate].occupied) {
                index = candidate;
                break;
            }
        }
        if (index == slots_.size()) return Vst3PluginStateResultV1::capacity_exhausted;
    }
    try {
        Slot candidate{};
        candidate.occupied = true;
        candidate.state_id.assign(state_id.data(), state_id.size());
        candidate.identity = identity;
        candidate.state_version = state_version;
        candidate.bytes.assign(bytes.begin(), bytes.end());
        const bool was_occupied = slots_[index].occupied;
        slots_[index] = std::move(candidate);
        if (!was_occupied) ++count_;
        return Vst3PluginStateResultV1::ok;
    } catch (const std::bad_alloc&) {
        return Vst3PluginStateResultV1::capacity_exhausted;
    }
}

Vst3PluginStateResultV1 Vst3PluginStateStoreV1::restore(
    const std::string_view state_id,
    const Vst3PluginStateIdentityV1& expected_identity,
    const std::uint32_t expected_state_version,
    const std::span<std::uint8_t> destination,
    std::size_t& bytes_written) const noexcept {
    bytes_written = 0U;
    if (!valid_state_id(state_id) || !validate_vst3_plugin_state_identity_v1(expected_identity) ||
        expected_state_version == 0U) {
        return Vst3PluginStateResultV1::invalid_argument;
    }
    const auto index = find(state_id);
    if (index == slots_.size()) return Vst3PluginStateResultV1::missing;
    const auto& slot = slots_[index];
    if (slot.identity.plugin_id != expected_identity.plugin_id ||
        slot.identity.class_id != expected_identity.class_id ||
        slot.identity.module_sha256 != expected_identity.module_sha256) {
        return Vst3PluginStateResultV1::identity_mismatch;
    }
    if (slot.state_version != expected_state_version) return Vst3PluginStateResultV1::version_mismatch;
    if (destination.size() < slot.bytes.size()) return Vst3PluginStateResultV1::output_too_small;
    if (!slot.bytes.empty()) {
        std::memcpy(destination.data(), slot.bytes.data(), slot.bytes.size());
    }
    bytes_written = slot.bytes.size();
    return Vst3PluginStateResultV1::ok;
}

Vst3PluginStateResultV1 Vst3PluginStateStoreV1::restore_with_migration(
    const std::string_view state_id,
    const Vst3PluginStateIdentityV1& expected_identity,
    const std::uint32_t expected_state_version,
    const std::span<std::uint8_t> destination,
    std::size_t& bytes_written,
    const Vst3PluginStateMigrationFnV1 migration,
    void* const context) const noexcept {
    bytes_written = 0U;
    if (!valid_state_id(state_id) || !validate_vst3_plugin_state_identity_v1(expected_identity) ||
        expected_state_version == 0U) {
        return Vst3PluginStateResultV1::invalid_argument;
    }
    const auto index = find(state_id);
    if (index == slots_.size()) return Vst3PluginStateResultV1::missing;
    const auto& slot = slots_[index];
    if (slot.identity.plugin_id != expected_identity.plugin_id ||
        slot.identity.class_id != expected_identity.class_id ||
        slot.identity.module_sha256 != expected_identity.module_sha256) {
        return Vst3PluginStateResultV1::identity_mismatch;
    }
    if (slot.state_version == expected_state_version) {
        if (destination.size() < slot.bytes.size()) return Vst3PluginStateResultV1::output_too_small;
        if (!slot.bytes.empty()) std::memcpy(destination.data(), slot.bytes.data(), slot.bytes.size());
        bytes_written = slot.bytes.size();
        return Vst3PluginStateResultV1::ok;
    }
    if (migration == nullptr) return Vst3PluginStateResultV1::migration_unavailable;

    const auto bounded_size = std::min(destination.size(), kVst3PluginStateMaxBytesV1);
    const auto result = migration(slot.state_version,
                                  std::span<const std::uint8_t>(slot.bytes.data(), slot.bytes.size()),
                                  expected_state_version,
                                  destination.first(bounded_size), bytes_written, context);
    if (result != Vst3PluginStateResultV1::ok) return Vst3PluginStateResultV1::migration_failed;
    if (bytes_written > bounded_size || bytes_written > kVst3PluginStateMaxBytesV1) {
        bytes_written = 0U;
        return Vst3PluginStateResultV1::migration_output_too_large;
    }
    return Vst3PluginStateResultV1::ok;
}

Vst3PluginStateResultV1 Vst3PluginStateStoreV1::inspect(
    const std::string_view state_id,
    const Vst3PluginStateIdentityV1& expected_identity,
    std::uint32_t& state_version,
    std::size_t& byte_count) const noexcept {
    state_version = 0U;
    byte_count = 0U;
    if (!valid_state_id(state_id) || !validate_vst3_plugin_state_identity_v1(expected_identity)) {
        return Vst3PluginStateResultV1::invalid_argument;
    }
    const auto index = find(state_id);
    if (index == slots_.size()) return Vst3PluginStateResultV1::missing;
    const auto& slot = slots_[index];
    if (!same_identity(slot.identity, expected_identity)) {
        return Vst3PluginStateResultV1::identity_mismatch;
    }
    state_version = slot.state_version;
    byte_count = slot.bytes.size();
    return Vst3PluginStateResultV1::ok;
}

bool Vst3PluginStateStoreV1::remove(const std::string_view state_id) noexcept {
    const auto index = find(state_id);
    if (index == slots_.size()) return false;
    slots_[index] = {};
    --count_;
    return true;
}

void Vst3PluginStateStoreV1::clear() noexcept {
    for (auto& slot : slots_) slot = {};
    count_ = 0U;
}

std::size_t Vst3PluginStateMigrationRegistryV1::find(
    const Vst3PluginStateIdentityV1& identity,
    const std::uint32_t source_version,
    const std::uint32_t target_version) const noexcept {
    for (std::size_t index = 0U; index < rules_.size(); ++index) {
        const auto& rule = rules_[index];
        if (rule.occupied && same_identity(rule.identity, identity) &&
            rule.source_version == source_version && rule.target_version == target_version) {
            return index;
        }
    }
    return rules_.size();
}

Vst3PluginStateResultV1 Vst3PluginStateMigrationRegistryV1::register_rule(
    const Vst3PluginStateIdentityV1& identity,
    const std::uint32_t source_version,
    const std::uint32_t target_version,
    const Vst3PluginStateMigrationFnV1 migration,
    void* const context) {
    if (!validate_vst3_plugin_state_identity_v1(identity) || source_version == 0U ||
        target_version == 0U || source_version == target_version || migration == nullptr) {
        return Vst3PluginStateResultV1::invalid_argument;
    }
    for (const auto& rule : rules_) {
        if (rule.occupied && same_identity(rule.identity, identity) &&
            rule.target_version == target_version && rule.source_version != source_version) {
            return Vst3PluginStateResultV1::invalid_argument;
        }
    }
    auto index = find(identity, source_version, target_version);
    if (index == rules_.size()) {
        for (std::size_t candidate = 0U; candidate < rules_.size(); ++candidate) {
            if (!rules_[candidate].occupied) {
                index = candidate;
                break;
            }
        }
        if (index == rules_.size()) return Vst3PluginStateResultV1::capacity_exhausted;
    }
    rules_[index] = Rule{true, identity, source_version, target_version, migration, context};
    count_ = 0U;
    for (const auto& rule : rules_) {
        if (rule.occupied) ++count_;
    }
    return Vst3PluginStateResultV1::ok;
}

Vst3PluginStateResultV1 Vst3PluginStateMigrationRegistryV1::restore(
    const Vst3PluginStateStoreV1& store,
    const std::string_view state_id,
    const Vst3PluginStateIdentityV1& expected_identity,
    const std::uint32_t expected_state_version,
    const std::span<std::uint8_t> destination,
    std::size_t& bytes_written) const noexcept {
    const auto exact = store.restore(state_id, expected_identity, expected_state_version,
                                     destination, bytes_written);
    if (exact != Vst3PluginStateResultV1::version_mismatch) return exact;

    std::size_t matching_rule = rules_.size();
    for (std::size_t index = 0U; index < rules_.size(); ++index) {
        const auto& rule = rules_[index];
        if (rule.occupied && same_identity(rule.identity, expected_identity) &&
            rule.target_version == expected_state_version) {
            if (matching_rule != rules_.size()) return Vst3PluginStateResultV1::migration_unavailable;
            matching_rule = index;
        }
    }
    if (matching_rule == rules_.size()) return Vst3PluginStateResultV1::migration_unavailable;
    const auto& rule = rules_[matching_rule];
    return store.restore_with_migration(state_id, expected_identity, expected_state_version,
                                        destination, bytes_written, rule.migration, rule.context);
}

bool Vst3PluginStateMigrationRegistryV1::has_rule(
    const Vst3PluginStateIdentityV1& identity,
    const std::uint32_t source_version,
    const std::uint32_t target_version) const noexcept {
    return find(identity, source_version, target_version) != rules_.size();
}

bool Vst3PluginStateMigrationRegistryV1::remove_rule(
    const Vst3PluginStateIdentityV1& identity,
    const std::uint32_t source_version,
    const std::uint32_t target_version) noexcept {
    const auto index = find(identity, source_version, target_version);
    if (index == rules_.size()) return false;
    rules_[index] = {};
    count_ = 0U;
    for (const auto& rule : rules_) {
        if (rule.occupied) ++count_;
    }
    return true;
}

void Vst3PluginStateMigrationRegistryV1::clear() noexcept {
    for (auto& rule : rules_) rule = {};
    count_ = 0U;
}

}  // namespace hibiki
