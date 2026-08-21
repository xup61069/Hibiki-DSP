#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/audio_session_registry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace hibiki {

enum class ProcessLoopbackPlanResultV1 : std::uint8_t {
    Applied,
    NoRoutes,
    InvalidProcessIdentity,
    AmbiguousProcess,
    CapacityExhausted,
};

struct ProcessLoopbackPlanEntryV1 {
    std::uint32_t process_id{0U};
    std::uint32_t session_count{0U};
    bool include_process_tree{true};
    std::string lane_id;
    std::string output_group;
};

struct ProcessLoopbackPlanV1 {
    static constexpr std::size_t kMaxEntries = 64U;
    std::uint32_t schema_version{1U};
    std::array<ProcessLoopbackPlanEntryV1, kMaxEntries> entries{};
    std::size_t size{0U};
};

// Compiles active session routes into bounded process-level capture requests.
// Process IDs are deliberately transient: this plan is for the current worker
// generation only and must never be persisted as profile identity.
[[nodiscard]] ProcessLoopbackPlanResultV1 build_process_loopback_plan(
    const AudioSessionRegistry& registry,
    ProcessLoopbackPlanV1& plan) noexcept;

}  // namespace hibiki
