// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/process_loopback_plan.hpp"

#include <new>

namespace hibiki {

ProcessLoopbackPlanResultV1 build_process_loopback_plan(
    const AudioSessionRegistry& registry,
    ProcessLoopbackPlanV1& plan) noexcept {
    plan = {};
    for (const auto& session : registry.sessions()) {
        if (!session.active || session.lane_id.empty() || session.output_group.empty()) continue;
        if (session.identity.process_id == 0U) {
            plan = {};
            return ProcessLoopbackPlanResultV1::InvalidProcessIdentity;
        }
        for (std::size_t index = 0U; index < plan.size; ++index) {
            const auto& entry = plan.entries[index];
            if (entry.lane_id == session.lane_id &&
                entry.process_id != session.identity.process_id) {
                plan = {};
                return ProcessLoopbackPlanResultV1::DuplicateLane;
            }
        }
        std::size_t existing_index = plan.size;
        for (std::size_t index = 0U; index < plan.size; ++index) {
            if (plan.entries[index].process_id == session.identity.process_id) {
                existing_index = index;
                break;
            }
        }
        try {
            if (existing_index < plan.size) {
                auto& entry = plan.entries[existing_index];
                if (entry.lane_id != session.lane_id || entry.output_group != session.output_group) {
                    plan = {};
                    return ProcessLoopbackPlanResultV1::AmbiguousProcess;
                }
                ++entry.session_count;
                continue;
            }
            if (plan.size >= ProcessLoopbackPlanV1::kMaxEntries) {
                plan = {};
                return ProcessLoopbackPlanResultV1::CapacityExhausted;
            }
            auto& entry = plan.entries[plan.size++];
            entry.process_id = session.identity.process_id;
            entry.session_count = 1U;
            entry.include_process_tree = true;
            entry.lane_id = session.lane_id;
            entry.output_group = session.output_group;
        } catch (const std::bad_alloc&) {
            plan = {};
            return ProcessLoopbackPlanResultV1::CapacityExhausted;
        }
    }
    return plan.size == 0U ? ProcessLoopbackPlanResultV1::NoRoutes
                           : ProcessLoopbackPlanResultV1::Applied;
}

}  // namespace hibiki
