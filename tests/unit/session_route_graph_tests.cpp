// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/audio_session_registry.hpp"
#include "hibiki/scene_graph.hpp"
#include "hibiki/session_route.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

#define CHECK(expr) do { if (!(expr)) { std::fputs("FAILED: " #expr "\n", stderr); return 1; } } while (false)

namespace {

using hibiki::AudioSessionDescriptorV1;
using hibiki::AudioSessionIdentityV1;
using hibiki::AudioSessionRegistry;
using hibiki::build_session_route_graph;
using hibiki::GraphConfigV1;
using hibiki::SessionGainOwner;
using hibiki::SessionRouteGraphPolicyV1;

AudioSessionDescriptorV1 make_session(const std::string& endpoint,
                                      const std::string& instance,
                                      const std::string& lane,
                                      const std::string& group)
{
    AudioSessionDescriptorV1 descriptor;
    descriptor.identity.endpoint_id = endpoint;
    descriptor.identity.session_instance_id = instance;
    descriptor.active = true;
    descriptor.gain_owner = SessionGainOwner::WindowsSession;
    descriptor.lane_id = lane;
    descriptor.output_group = group;
    descriptor.makeup_gain_db = 0.0;
    return descriptor;
}

}  // namespace

int main()
{
    // ---- policy contract ------------------------------------------------------
    {
        AudioSessionRegistry registry;
        GraphConfigV1 graph;
        CHECK(!build_session_route_graph(registry, SessionRouteGraphPolicyV1{2U, 2U, false}, graph));
        CHECK(!build_session_route_graph(registry, SessionRouteGraphPolicyV1{1U, 4U, false}, graph));
        CHECK(!build_session_route_graph(registry, SessionRouteGraphPolicyV1{1U, 0U, false}, graph));
    }

    // ---- empty registry fails closed ------------------------------------------
    {
        AudioSessionRegistry registry;
        GraphConfigV1 graph;
        CHECK(!build_session_route_graph(registry, SessionRouteGraphPolicyV1{1U, 2U, false}, graph));
        CHECK(graph.lanes.empty());
        CHECK(graph.output_channels == 2U);
    }

    // ---- happy path: one active bound session ---------------------------------
    {
        AudioSessionRegistry registry;
        auto session = make_session("endpoint.main", "inst-1", "lane.game", "game");
        CHECK(registry.upsert(session));
        GraphConfigV1 graph;
        CHECK(build_session_route_graph(registry, SessionRouteGraphPolicyV1{1U, 2U, false}, graph));
        CHECK(graph.lanes.size() == 1U);
        CHECK(graph.lanes[0].id == "lane.game");
        CHECK(graph.lanes[0].output_group == "game");
        CHECK(graph.lanes[0].channel_count == 2U);
        CHECK(graph.lanes[0].enabled);
        CHECK(graph.lanes[0].makeup_gain_db == 0.0);
        CHECK(hibiki::validate_graph(graph));
    }

    // ---- inactive sessions are ignored -----------------------------------------
    {
        AudioSessionRegistry registry;
        auto inactive = make_session("endpoint.main", "inst-1", "lane.game", "game");
        inactive.active = false;
        CHECK(registry.upsert(inactive));
        GraphConfigV1 graph;
        CHECK(!build_session_route_graph(registry, SessionRouteGraphPolicyV1{1U, 2U, false}, graph));
    }

    // ---- sessions without a full binding are ignored ----------------------------
    {
        AudioSessionRegistry registry;
        auto unbound = make_session("endpoint.main", "inst-1", "", "game");
        CHECK(registry.upsert(unbound));
        auto groupless = make_session("endpoint.main", "inst-2", "lane.music", "");
        CHECK(registry.upsert(groupless));
        GraphConfigV1 graph;
        CHECK(!build_session_route_graph(registry, SessionRouteGraphPolicyV1{1U, 2U, false}, graph));
    }

    // ---- duplicate lane ids fail closed ------------------------------------------
    {
        AudioSessionRegistry registry;
        CHECK(registry.upsert(make_session("endpoint.main", "inst-1", "lane.dup", "game")));
        CHECK(registry.upsert(make_session("endpoint.main", "inst-2", "lane.dup", "music")));
        GraphConfigV1 graph;
        CHECK(!build_session_route_graph(registry, SessionRouteGraphPolicyV1{1U, 2U, false}, graph));
    }

    // ---- gain ownership semantics -------------------------------------------------
    {
        AudioSessionRegistry registry;
        auto internal = make_session("endpoint.main", "inst-1", "lane.internal", "game");
        internal.gain_owner = SessionGainOwner::HibikiInternal;
        internal.makeup_gain_db = -3.5;
        CHECK(registry.upsert(internal));
        auto windows = make_session("endpoint.main", "inst-2", "lane.windows", "music");
        windows.gain_owner = SessionGainOwner::WindowsSession;
        windows.makeup_gain_db = -3.5;
        CHECK(registry.upsert(windows));
        GraphConfigV1 graph;
        CHECK(build_session_route_graph(registry, SessionRouteGraphPolicyV1{1U, 2U, false}, graph));
        CHECK(graph.lanes.size() == 2U);
        CHECK(graph.lanes[0].makeup_gain_db == -3.5);
        CHECK(graph.lanes[1].makeup_gain_db == 0.0);
    }

    // ---- non-finite internal gains fail closed --------------------------------------
    {
        AudioSessionRegistry registry;
        auto broken = make_session("endpoint.main", "inst-1", "lane.broken", "game");
        broken.gain_owner = SessionGainOwner::HibikiInternal;
        CHECK(registry.upsert(broken));
        for (const double bad_gain : {std::nan(""), std::numeric_limits<double>::infinity()}) {
            registry.mutable_sessions()[0].makeup_gain_db = bad_gain;
            GraphConfigV1 graph;
            CHECK(!build_session_route_graph(registry, SessionRouteGraphPolicyV1{1U, 2U, false}, graph));
        }
    }

    // ---- capacity is fail-closed at the RT lane limit ------------------------------
    {
        AudioSessionRegistry registry;
        for (std::size_t index = 0; index < hibiki::kMaxRtLanes; ++index) {
            const auto suffix = std::to_string(index);
            CHECK(registry.upsert(make_session(
                "endpoint.main", "inst-" + suffix, "lane." + suffix, "group")));
        }
        GraphConfigV1 graph;
        CHECK(build_session_route_graph(registry, SessionRouteGraphPolicyV1{1U, 2U, false}, graph));
        CHECK(graph.lanes.size() == hibiki::kMaxRtLanes);

        CHECK(registry.upsert(make_session(
            "endpoint.main", "inst-overflow", "lane.overflow", "group")));
        CHECK(!build_session_route_graph(registry, SessionRouteGraphPolicyV1{1U, 2U, false}, graph));
    }

    // ---- supported output layouts pass through strict_direct ------------------------
    for (const std::uint32_t channels : {2U, 6U, 8U}) {
        AudioSessionRegistry registry;
        CHECK(registry.upsert(make_session("endpoint.main", "inst-1", "lane.main", "main")));
        GraphConfigV1 graph;
        CHECK(build_session_route_graph(registry, SessionRouteGraphPolicyV1{1U, channels, true}, graph));
        CHECK(graph.output_channels == channels);
        CHECK(graph.strict_direct);
        CHECK(hibiki::validate_graph(graph));
    }

    return 0;
}
