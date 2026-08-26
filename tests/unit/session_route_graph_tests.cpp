// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/session_route.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {
using namespace hibiki;

AudioSessionDescriptorV1 make_session(const std::string& endpoint,
                                      const std::string& instance,
                                      const std::string& lane) {
    AudioSessionDescriptorV1 descriptor;
    descriptor.identity.endpoint_id = endpoint;
    descriptor.identity.session_instance_id = instance;
    descriptor.active = true;
    descriptor.lane_id = lane;
    descriptor.output_group = "main";
    return descriptor;
}

bool add_session(AudioSessionRegistry& registry,
                 const std::string& instance,
                 const std::string& lane) {
    return registry.upsert(make_session("endpoint", instance, lane));
}
}  // namespace

int main() {
    // ---- policy validation ---------------------------------------------------
    {
        const AudioSessionRegistry empty_registry;
        GraphConfigV1 graph{};
        SessionRouteGraphPolicyV1 bad_schema{};
        bad_schema.schema_version = 2U;
        CHECK(!build_session_route_graph(empty_registry, bad_schema, graph));

        for (const auto channels : {0U, 1U, 3U, 4U, 5U, 7U, 9U}) {
            SessionRouteGraphPolicyV1 bad_channels{};
            bad_channels.output_channels = channels;
            CHECK(!build_session_route_graph(empty_registry, bad_channels, graph));
        }
    }

    // ---- skip rules keep compilation successful ------------------------------
    {
        AudioSessionRegistry registry;
        auto inactive = make_session("endpoint", "inactive", "lane-a");
        inactive.active = false;
        CHECK(registry.upsert(inactive));
        // Upsert permits an empty lane_id/output_group pair; such a session is
        // stored active but unbound until a successful bind() call.
        auto unbound = make_session("endpoint", "unbound", "");
        unbound.output_group = "";
        CHECK(registry.upsert(unbound));
        auto no_group = make_session("endpoint", "no-group", "lane-b");
        no_group.output_group = "";
        CHECK(registry.upsert(no_group));

        GraphConfigV1 graph{};
        const SessionRouteGraphPolicyV1 policy{};
        const bool compiled = build_session_route_graph(registry, policy, graph);
        CHECK(!compiled);
        CHECK(graph.lanes.empty());
    }

    // ---- happy path with gain ownership --------------------------------------
    {
        AudioSessionRegistry registry;
        CHECK(add_session(registry, "win-owned", "lane-win") &&
              registry.bind({"endpoint", "win-owned"}, "lane-win", "main"));
        CHECK(add_session(registry, "hibiki-owned", "lane-hibiki") &&
              registry.bind({"endpoint", "hibiki-owned"}, "lane-hibiki", "main") &&
              registry.set_gain_owner({"endpoint", "hibiki-owned"},
                                      SessionGainOwner::HibikiInternal) &&
              registry.set_makeup_gain_db({"endpoint", "hibiki-owned"}, -3.5));

        GraphConfigV1 graph{};
        const SessionRouteGraphPolicyV1 policy{};
        CHECK(build_session_route_graph(registry, policy, graph));
        CHECK(graph.output_channels == 2U);
        CHECK(graph.lanes.size() == 2U);
        bool saw_win_zero_gain = false;
        bool saw_hibiki_negative_gain = false;
        for (const auto& lane : graph.lanes) {
            if (lane.id == "lane-win" && lane.makeup_gain_db == 0.0) {
                saw_win_zero_gain = true;
            }
            if (lane.id == "lane-hibiki" &&
                std::abs(lane.makeup_gain_db + 3.5) < 1e-12) {
                saw_hibiki_negative_gain = true;
            }
        }
        CHECK(saw_win_zero_gain && saw_hibiki_negative_gain);
        CHECK(validate_graph(graph));
    }

    // ---- duplicate lane ids fail closed ---------------------------------------
    {
        AudioSessionRegistry registry;
        CHECK(add_session(registry, "first", "shared-lane") &&
              registry.bind({"endpoint", "first"}, "shared-lane", "main"));
        auto second = make_session("endpoint", "second", "shared-lane");
        CHECK(registry.upsert(second));
        CHECK(registry.bind({"endpoint", "second"}, "shared-lane", "main"));
        GraphConfigV1 graph{};
        const SessionRouteGraphPolicyV1 policy{};
        CHECK(!build_session_route_graph(registry, policy, graph));
    }

    // ---- non-finite makeup gain fails closed ----------------------------------
    {
        AudioSessionRegistry registry;
        CHECK(add_session(registry, "bad-gain", "lane-bad") &&
              registry.bind({"endpoint", "bad-gain"}, "lane-bad", "main") &&
              registry.set_gain_owner({"endpoint", "bad-gain"},
                                      SessionGainOwner::HibikiInternal));
        const auto qnan_d = std::numeric_limits<double>::quiet_NaN();
        CHECK(registry.set_makeup_gain_db({"endpoint", "bad-gain"}, 0.0));
        // The public setter keeps stored values finite; inject NaN through the
        // descriptor to exercise the compiler's own isfinite gate.
        auto* descriptor =
            registry.find({"endpoint", "bad-gain"});
        if (descriptor != nullptr && descriptor->gain_owner ==
                                         SessionGainOwner::HibikiInternal) {
            descriptor->makeup_gain_db = qnan_d;
        }
        GraphConfigV1 graph{};
        const SessionRouteGraphPolicyV1 policy{};
        CHECK(!build_session_route_graph(registry, policy, graph));
    }

    // ---- capacity limit fails closed ------------------------------------------
    {
        AudioSessionRegistry registry;
        for (std::size_t index = 0U; index <= kMaxRtLanes; ++index) {
            const auto instance = "inst-" + std::to_string(index);
            const auto lane = "l" + std::to_string(index);
            CHECK(add_session(registry, instance, lane));
            CHECK(registry.bind({"endpoint", instance}, lane, "main"));
        }
        GraphConfigV1 graph{};
        const SessionRouteGraphPolicyV1 policy{};
        CHECK(!build_session_route_graph(registry, policy, graph));
        // Removing exactly one session drops the count back to kMaxRtLanes,
        // including the previously rejected overflow session.
        CHECK(registry.remove({"endpoint", "inst-0"}));
        CHECK(build_session_route_graph(registry, policy, graph));
        CHECK(graph.lanes.size() == kMaxRtLanes);
    }

    std::fputs("session_route_graph_tests passed\n", stdout);
    return 0;
}
