// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/windows_audio_session_route.hpp"

#if defined(_WIN32)

#include <cmath>
#include <new>
#include <utility>

namespace hibiki {

HRESULT WindowsAudioSessionRouteCoordinatorV1::bind(IMMDevice* const device) {
    if (device == nullptr) return E_INVALIDARG;
    unbind();
    const auto result = watcher_.bind(device);
    bound_ = SUCCEEDED(result);
    if (!bound_) degraded_ = true;
    return result;
}

void WindowsAudioSessionRouteCoordinatorV1::unbind() noexcept {
    watcher_.unbind();
    registry_ = {};
    graph_ = {};
    generation_ = 0U;
    bound_ = false;
    has_graph_ = false;
    degraded_ = false;
}

bool WindowsAudioSessionRouteCoordinatorV1::set_rules(
    const SessionRouteRuleStoreV1& rules) noexcept {
    try {
        rules_ = rules;
    } catch (const std::bad_alloc&) {
        degraded_ = true;
        return false;
    }
    watcher_.set_route_rules(&rules_);
    return true;
}

namespace {

constexpr std::size_t kMaxSessionControlIdentityBytesV1 = 260U;

bool valid_session_control_request(const std::string_view session_instance_id,
                                   const double requested_db) noexcept {
    return !session_instance_id.empty() &&
           session_instance_id.size() <= kMaxSessionControlIdentityBytesV1 &&
           std::isfinite(requested_db) && requested_db >= -144.0 && requested_db <= 12.0;
}

bool has_session_instance(const AudioSessionRegistry& registry,
                          const std::string_view session_instance_id) noexcept {
    for (const auto& session : registry.sessions()) {
        if (session.identity.session_instance_id == session_instance_id) return true;
    }
    return false;
}

}  // namespace

HRESULT WindowsAudioSessionRouteCoordinatorV1::write_session_volume(
    const std::string_view session_instance_id,
    const double requested_db,
    const bool mute,
    const GUID& event_context) noexcept {
    if (!bound_) return E_UNEXPECTED;
    if (!valid_session_control_request(session_instance_id, requested_db)) {
        return E_INVALIDARG;
    }
    if (!has_session_instance(registry_, session_instance_id)) {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    return watcher_.write_session_volume(session_instance_id, requested_db, mute, event_context);
}

HRESULT WindowsAudioSessionRouteCoordinatorV1::read_session_volume(
    const std::string_view session_instance_id,
    double& requested_db,
    bool& mute) noexcept {
    if (!bound_) return E_UNEXPECTED;
    if (session_instance_id.empty() ||
        session_instance_id.size() > kMaxSessionControlIdentityBytesV1) {
        return E_INVALIDARG;
    }
    if (!has_session_instance(registry_, session_instance_id)) {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    return watcher_.read_session_volume(session_instance_id, requested_db, mute);
}

WindowsAudioSessionRouteRefreshResultV1 WindowsAudioSessionRouteCoordinatorV1::refresh() noexcept {
    if (!bound_) return WindowsAudioSessionRouteRefreshResultV1::Unbound;

    AudioSessionRegistry candidate_registry;
    watcher_.set_route_rules(&rules_);
    if (FAILED(watcher_.enumerate(candidate_registry))) {
        degraded_ = true;
        return WindowsAudioSessionRouteRefreshResultV1::Degraded;
    }

    std::size_t active_count = 0U;
    std::size_t routed_count = 0U;
    for (const auto& session : candidate_registry.sessions()) {
        if (!session.active) continue;
        ++active_count;
        if (!session.lane_id.empty() && !session.output_group.empty()) ++routed_count;
    }

    GraphConfigV1 candidate_graph{};
    const bool graph_ready = routed_count > 0U &&
                             build_session_route_graph(
                                 candidate_registry, SessionRouteGraphPolicyV1{}, candidate_graph);
    if (routed_count > 0U && !graph_ready) {
        degraded_ = true;
        return WindowsAudioSessionRouteRefreshResultV1::Degraded;
    }

    registry_ = std::move(candidate_registry);
    graph_ = std::move(candidate_graph);
    has_graph_ = graph_ready;
    degraded_ = false;
    ++generation_;
    return has_graph_ ? WindowsAudioSessionRouteRefreshResultV1::Applied
                      : WindowsAudioSessionRouteRefreshResultV1::NoRoutes;
}

WindowsAudioSessionRouteRefreshResultV1
WindowsAudioSessionRouteCoordinatorV1::poll_and_refresh() noexcept {
    if (!bound_) return WindowsAudioSessionRouteRefreshResultV1::Unbound;
    std::uint64_t sequence = 0U;
    if (!watcher_.poll(sequence)) return WindowsAudioSessionRouteRefreshResultV1::NoChange;
    return refresh();
}

bool WindowsAudioSessionRouteCoordinatorV1::copy_graph(GraphConfigV1& graph) const noexcept {
    if (!has_graph_ || degraded_) return false;
    try {
        graph = graph_;
    } catch (const std::bad_alloc&) {
        return false;
    }
    return true;
}

ProcessLoopbackPlanResultV1 WindowsAudioSessionRouteCoordinatorV1::copy_process_loopback_plan(
    ProcessLoopbackPlanV1& plan) const noexcept {
    if (!bound_ || degraded_ || !has_graph_) {
        plan = {};
        return ProcessLoopbackPlanResultV1::NoRoutes;
    }
    return build_process_loopback_plan(registry_, plan);
}

WindowsAudioSessionRouteSnapshotV1
WindowsAudioSessionRouteCoordinatorV1::snapshot() const noexcept {
    std::size_t active_count = 0U;
    std::size_t routed_count = 0U;
    for (const auto& session : registry_.sessions()) {
        if (!session.active) continue;
        ++active_count;
        if (!session.lane_id.empty() && !session.output_group.empty()) ++routed_count;
    }
    return WindowsAudioSessionRouteSnapshotV1{
        generation_, registry_.sessions().size(), active_count, routed_count, has_graph_, degraded_};
}

}  // namespace hibiki

#endif  // defined(_WIN32)
