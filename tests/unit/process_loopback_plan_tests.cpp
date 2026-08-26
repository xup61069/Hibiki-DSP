// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/process_loopback_plan.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::AudioSessionDescriptorV1;
using hibiki::AudioSessionRegistry;
using hibiki::ProcessLoopbackPlanResultV1;
using hibiki::ProcessLoopbackPlanV1;

AudioSessionDescriptorV1 make_session(std::uint32_t process_id,
                                      const std::string& lane_id,
                                      const std::string& output_group,
                                      bool active = true) {
    AudioSessionDescriptorV1 descriptor;
    descriptor.identity.endpoint_id = "endpoint";
    descriptor.identity.session_instance_id = "session-" + std::to_string(process_id)
        + "-" + lane_id + "-" + output_group;
    descriptor.identity.process_id = process_id;
    descriptor.active = active;
    descriptor.lane_id = lane_id;
    descriptor.output_group = output_group;
    return descriptor;
}

}  // namespace

int main() {
    // Applied: two sessions on the same process collapse into one entry.
    {
        AudioSessionRegistry registry;
        CHECK(registry.upsert(make_session(100U, "game", "main")));
        auto second = make_session(100U, "game", "main");
        second.identity.session_instance_id = "session-100-game-main-2";
        CHECK(registry.upsert(second));

        ProcessLoopbackPlanV1 plan;
        CHECK(build_process_loopback_plan(registry, plan)
              == ProcessLoopbackPlanResultV1::Applied);
        CHECK(plan.schema_version == 1U);
        CHECK(plan.size == 1U);
        CHECK(plan.entries[0].process_id == 100U);
        CHECK(plan.entries[0].session_count == 2U);
        CHECK(plan.entries[0].include_process_tree);
        CHECK(plan.entries[0].lane_id == "game");
        CHECK(plan.entries[0].output_group == "main");
    }

    // NoRoutes: empty registry and filtered-out sessions.
    {
        AudioSessionRegistry registry;
        ProcessLoopbackPlanV1 plan;
        CHECK(build_process_loopback_plan(registry, plan)
              == ProcessLoopbackPlanResultV1::NoRoutes);
        CHECK(plan.size == 0U);

        auto inactive = make_session(200U, "game", "main");
        inactive.active = false;
        CHECK(registry.upsert(inactive));
        auto missing_lane = make_session(201U, "", "main");
        missing_lane.identity.session_instance_id = "session-201--main";
        CHECK(registry.upsert(missing_lane));
        auto missing_group = make_session(202U, "game", "");
        missing_group.identity.session_instance_id = "session-202-game-";
        CHECK(registry.upsert(missing_group));
        CHECK(build_process_loopback_plan(registry, plan)
              == ProcessLoopbackPlanResultV1::NoRoutes);
        CHECK(plan.size == 0U);
    }

    // InvalidProcessIdentity resets any partially built plan.
    {
        AudioSessionRegistry registry;
        CHECK(registry.upsert(make_session(300U, "game", "main")));
        auto zero_pid = make_session(0U, "music", "main");
        zero_pid.identity.session_instance_id = "session-zero";
        CHECK(registry.upsert(zero_pid));

        ProcessLoopbackPlanV1 plan;
        CHECK(build_process_loopback_plan(registry, plan)
              == ProcessLoopbackPlanResultV1::InvalidProcessIdentity);
        CHECK(plan.size == 0U);
    }

    // DuplicateLane: same lane bound by two different processes fails closed.
    {
        AudioSessionRegistry registry;
        CHECK(registry.upsert(make_session(400U, "game", "main")));
        CHECK(registry.upsert(make_session(401U, "game", "main")));

        ProcessLoopbackPlanV1 plan;
        CHECK(build_process_loopback_plan(registry, plan)
              == ProcessLoopbackPlanResultV1::DuplicateLane);
        CHECK(plan.size == 0U);
    }

    // AmbiguousProcess: one process mapped to two lanes fails closed.
    {
        AudioSessionRegistry registry;
        CHECK(registry.upsert(make_session(500U, "game", "main")));
        CHECK(registry.upsert(make_session(500U, "music", "main")));

        ProcessLoopbackPlanV1 plan;
        CHECK(build_process_loopback_plan(registry, plan)
              == ProcessLoopbackPlanResultV1::AmbiguousProcess);
        CHECK(plan.size == 0U);
    }

    // CapacityExhausted: exactly kMaxEntries processes succeed.
    {
        AudioSessionRegistry registry;
        for (std::uint32_t pid = 600U; pid < 600U + 64U; ++pid) {
            CHECK(registry.upsert(make_session(pid, "lane-" + std::to_string(pid), "main")));
        }

        ProcessLoopbackPlanV1 plan;
        CHECK(build_process_loopback_plan(registry, plan)
              == ProcessLoopbackPlanResultV1::Applied);
        CHECK(plan.size == ProcessLoopbackPlanV1::kMaxEntries);
    }

    // CapacityExhausted: one more than capacity fails and resets the plan.
    {
        AudioSessionRegistry registry;
        for (std::uint32_t pid = 700U; pid < 700U + 65U; ++pid) {
            CHECK(registry.upsert(make_session(pid, "lane-" + std::to_string(pid), "main")));
        }

        ProcessLoopbackPlanV1 plan;
        CHECK(build_process_loopback_plan(registry, plan)
              == ProcessLoopbackPlanResultV1::CapacityExhausted);
        CHECK(plan.size == 0U);
    }

    // A failure after several valid entries discards all of them.
    {
        AudioSessionRegistry registry;
        CHECK(registry.upsert(make_session(800U, "alpha", "main")));
        CHECK(registry.upsert(make_session(801U, "beta", "main")));
        CHECK(registry.upsert(make_session(802U, "gamma", "main")));
        CHECK(registry.upsert(make_session(0U, "delta", "main")));

        ProcessLoopbackPlanV1 plan;
        CHECK(build_process_loopback_plan(registry, plan)
              == ProcessLoopbackPlanResultV1::InvalidProcessIdentity);
        CHECK(plan.size == 0U);
    }

    std::fputs("process_loopback_plan_tests passed\n", stdout);
    return 0;
}
