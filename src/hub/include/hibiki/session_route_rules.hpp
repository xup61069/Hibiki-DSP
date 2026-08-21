#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/audio_session_registry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace hibiki {

constexpr std::size_t kMaxSessionRouteRulesV1 = 64U;
constexpr std::size_t kSessionRouteRuleMaxIdBytesV1 = 64U;
constexpr std::size_t kSessionRouteRuleMaxMatchBytesV1 = 256U;
constexpr std::size_t kSessionRouteRuleMaxRouteBytesV1 = 256U;

// Rules match stable Windows app/session metadata. process_id is deliberately
// excluded: it is an advisory observation and cannot be used as a persistent
// profile identity across process restarts.
struct SessionRouteRuleV1 {
    std::uint32_t schema_version{1};
    std::string rule_id;
    std::int32_t priority{0};
    bool enabled{true};
    std::string app_id;                 // case-insensitive exact match
    std::string display_name_contains;  // case-insensitive substring match
    std::string lane_id;
    std::string output_group;
    SessionGainOwner gain_owner{SessionGainOwner::WindowsSession};
    double makeup_gain_db{0.0};
};

enum class SessionRouteRuleResultV1 : std::uint8_t {
    applied,
    no_match,
    invalid_argument,
    ambiguous,
    capacity_exhausted,
};

// Control-plane rule store. It is bounded and deterministic: the single
// highest-priority matching rule is applied; equal-priority matches fail
// closed instead of depending on insertion order. No rule is evaluated from
// the RT thread and no process ID is persisted as profile identity.
class SessionRouteRuleStoreV1 final {
public:
    SessionRouteRuleStoreV1() noexcept = default;

    [[nodiscard]] SessionRouteRuleResultV1 upsert(const SessionRouteRuleV1& rule);
    [[nodiscard]] bool remove(std::string_view rule_id) noexcept;
    void clear() noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return count_; }

    // Applies the selected rule atomically from the caller's perspective:
    // candidate data is prepared first, then copied into the descriptor. A
    // failed allocation leaves the original descriptor unchanged.
    [[nodiscard]] SessionRouteRuleResultV1 apply(
        AudioSessionDescriptorV1& descriptor) const;

private:
    struct Slot {
        bool occupied{false};
        SessionRouteRuleV1 rule{};
    };

    [[nodiscard]] static bool valid(const SessionRouteRuleV1& rule) noexcept;
    [[nodiscard]] static bool matches(const SessionRouteRuleV1& rule,
                                      const AudioSessionDescriptorV1& descriptor) noexcept;
    [[nodiscard]] std::size_t find(std::string_view rule_id) const noexcept;

    std::array<Slot, kMaxSessionRouteRulesV1> slots_{};
    std::size_t count_{0U};
};

}  // namespace hibiki
