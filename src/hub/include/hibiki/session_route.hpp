#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/audio_session_registry.hpp"
#include "hibiki/scene_graph.hpp"

#include <cstdint>

namespace hibiki {

struct SessionRouteGraphPolicyV1 {
    std::uint32_t schema_version{1};
    std::uint32_t output_channels{2};
    bool strict_direct{false};
};

// Control-plane graph compiler for per-session software routing. Unbound or
// inactive sessions are ignored; duplicate lane IDs and invalid output
// layouts fail closed. The resulting graph is still committed through the
// normal AudioEngine transaction and never mutated by the RT thread.
[[nodiscard]] bool build_session_route_graph(const AudioSessionRegistry& registry,
                                             const SessionRouteGraphPolicyV1& policy,
                                             GraphConfigV1& graph);

}  // namespace hibiki
