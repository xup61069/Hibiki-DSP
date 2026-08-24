// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/session_route_rules.hpp"

#include "hibiki/control_payloads.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <new>

namespace hibiki {
namespace {

bool equal_ascii_folded(const std::string_view left,
                        const std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        const auto lhs = static_cast<unsigned char>(left[index]);
        const auto rhs = static_cast<unsigned char>(right[index]);
        if (std::tolower(lhs) != std::tolower(rhs)) return false;
    }
    return true;
}

bool contains_ascii_folded(const std::string_view value,
                           const std::string_view needle) noexcept {
    if (needle.empty() || needle.size() > value.size()) return false;
    for (std::size_t start = 0U; start + needle.size() <= value.size(); ++start) {
        if (equal_ascii_folded(value.substr(start, needle.size()), needle)) return true;
    }
    return false;
}

bool valid_rule_id_format(const std::string_view value) noexcept {
    if (value.empty() || value.size() > kSessionRouteRuleMaxIdBytesV1) return false;
    for (std::size_t index = 0U; index < value.size(); ++index) {
        const auto character = static_cast<unsigned char>(value[index]);
        const auto lowercase = character >= 'a' && character <= 'z';
        const auto digit = character >= '0' && character <= '9';
        const auto separator = character == '.' || character == '_' || character == '-';
        if ((!lowercase && !digit && !separator) ||
            (index == 0U && !lowercase && !digit)) {
            return false;
        }
    }
    return true;
}

bool contains_non_ascii_space(std::string_view value) noexcept {
    for (const auto character : value) {
        switch (character) {
        case ' ':
        case '\t':
        case '\n':
        case '\v':
        case '\f':
        case '\r':
            continue;
        default:
            return true;
        }
    }
    return false;
}

}  // namespace

bool SessionRouteRuleStoreV1::valid(const SessionRouteRuleV1& rule) noexcept {
    const bool has_match =
        (!rule.app_id.empty() && contains_non_ascii_space(rule.app_id)) ||
        (!rule.display_name_contains.empty() &&
         contains_non_ascii_space(rule.display_name_contains));
    return rule.schema_version == 1U && !rule.rule_id.empty() &&
           rule.rule_id.size() <= kSessionRouteRuleMaxIdBytesV1 &&
           valid_rule_id_format(rule.rule_id) &&
           is_printable_utf8_v1(rule.rule_id) &&
           rule.priority >= -1'000'000 && rule.priority <= 1'000'000 && has_match &&
           rule.app_id.size() <= kSessionRouteRuleMaxMatchBytesV1 &&
           (rule.app_id.empty() || is_printable_utf8_v1(rule.app_id)) &&
           rule.display_name_contains.size() <= kSessionRouteRuleMaxMatchBytesV1 &&
           (rule.display_name_contains.empty() ||
            is_printable_utf8_v1(rule.display_name_contains)) &&
           !rule.lane_id.empty() && rule.lane_id.size() <= kSessionRouteRuleMaxRouteBytesV1 &&
           contains_non_ascii_space(rule.lane_id) && is_printable_utf8_v1(rule.lane_id) &&
           !rule.output_group.empty() &&
           rule.output_group.size() <= kSessionRouteRuleMaxRouteBytesV1 &&
           contains_non_ascii_space(rule.output_group) &&
           is_printable_utf8_v1(rule.output_group) &&
           std::isfinite(rule.makeup_gain_db) && rule.makeup_gain_db >= -144.0 &&
           rule.makeup_gain_db <= 12.0;
}

bool SessionRouteRuleStoreV1::matches(
    const SessionRouteRuleV1& rule,
    const AudioSessionDescriptorV1& descriptor) noexcept {
    if (!rule.enabled) return false;
    if (!rule.app_id.empty() && !equal_ascii_folded(rule.app_id, descriptor.app_id)) {
        return false;
    }
    if (!rule.display_name_contains.empty() &&
        !contains_ascii_folded(descriptor.display_name, rule.display_name_contains)) {
        return false;
    }
    return true;
}

std::size_t SessionRouteRuleStoreV1::find(const std::string_view rule_id) const noexcept {
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        if (slots_[index].occupied && slots_[index].rule.rule_id == rule_id) return index;
    }
    return slots_.size();
}

SessionRouteRuleResultV1 SessionRouteRuleStoreV1::upsert(const SessionRouteRuleV1& rule) {
    if (!valid(rule)) return SessionRouteRuleResultV1::invalid_argument;
    auto index = find(rule.rule_id);
    if (index == slots_.size()) {
        for (std::size_t candidate = 0U; candidate < slots_.size(); ++candidate) {
            if (!slots_[candidate].occupied) {
                index = candidate;
                break;
            }
        }
        if (index == slots_.size()) return SessionRouteRuleResultV1::capacity_exhausted;
    }
    try {
        Slot candidate{};
        candidate.occupied = true;
        candidate.rule = rule;
        const bool was_occupied = slots_[index].occupied;
        slots_[index] = std::move(candidate);
        if (!was_occupied) ++count_;
        return SessionRouteRuleResultV1::applied;
    } catch (const std::bad_alloc&) {
        return SessionRouteRuleResultV1::capacity_exhausted;
    }
}

bool SessionRouteRuleStoreV1::remove(const std::string_view rule_id) noexcept {
    const auto index = find(rule_id);
    if (index == slots_.size()) return false;
    slots_[index] = {};
    --count_;
    return true;
}

void SessionRouteRuleStoreV1::clear() noexcept {
    for (auto& slot : slots_) slot = {};
    count_ = 0U;
}

SessionRouteRuleResultV1 SessionRouteRuleStoreV1::apply(
    AudioSessionDescriptorV1& descriptor) const {
    const Slot* selected = nullptr;
    for (const auto& slot : slots_) {
        if (!slot.occupied || !matches(slot.rule, descriptor)) continue;
        if (selected == nullptr || slot.rule.priority > selected->rule.priority) {
            selected = &slot;
            continue;
        }
        if (slot.rule.priority == selected->rule.priority) {
            return SessionRouteRuleResultV1::ambiguous;
        }
    }
    if (selected == nullptr) return SessionRouteRuleResultV1::no_match;

    try {
        auto candidate = descriptor;
        candidate.lane_id = selected->rule.lane_id;
        candidate.output_group = selected->rule.output_group;
        candidate.gain_owner = selected->rule.gain_owner;
        candidate.makeup_gain_db = selected->rule.makeup_gain_db;
        descriptor = std::move(candidate);
        return SessionRouteRuleResultV1::applied;
    } catch (const std::bad_alloc&) {
        return SessionRouteRuleResultV1::capacity_exhausted;
    }
}

}  // namespace hibiki
