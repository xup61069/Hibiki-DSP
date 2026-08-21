// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_plugin_state.hpp"

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

}  // namespace hibiki
