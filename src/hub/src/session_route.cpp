#include "hibiki/session_route.hpp"

#include <cmath>

namespace hibiki {

bool build_session_route_graph(const AudioSessionRegistry& registry,
                               const SessionRouteGraphPolicyV1& policy,
                               GraphConfigV1& graph) {
    graph = {};
    graph.schema_version = 1;
    graph.output_channels = policy.output_channels;
    graph.strict_direct = policy.strict_direct;
    if (policy.schema_version != 1 ||
        (policy.output_channels != 2U && policy.output_channels != 6U &&
         policy.output_channels != 8U)) {
        return false;
    }

    for (const auto& session : registry.sessions()) {
        if (!session.active || session.lane_id.empty() || session.output_group.empty()) continue;
        if (graph.lanes.size() >= kMaxRtLanes) return false;
        for (const auto& existing : graph.lanes) {
            if (existing.id == session.lane_id) return false;
        }
        const auto makeup_gain = session.gain_owner == SessionGainOwner::HibikiInternal
                                     ? session.makeup_gain_db
                                     : 0.0;
        if (!std::isfinite(makeup_gain)) return false;
        graph.lanes.push_back(
            LaneConfigV1{session.lane_id, session.output_group, 2U, makeup_gain, true});
    }
    return !graph.lanes.empty() && validate_graph(graph);
}

}  // namespace hibiki
