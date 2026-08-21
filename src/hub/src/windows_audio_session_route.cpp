// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/windows_audio_session_route.hpp"

#if defined(_WIN32)

#include <cmath>
#include <limits>
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

WindowsAudioSessionRouteRefreshResultV1
WindowsAudioSessionRouteCoordinatorV1::set_rules_and_refresh(
    const SessionRouteRuleStoreV1& rules) noexcept {
    if (!bound_) return WindowsAudioSessionRouteRefreshResultV1::Unbound;
    try {
        SessionRouteRuleStoreV1 candidate_rules = rules;
        AudioSessionRegistry candidate_registry;
        watcher_.set_route_rules(&candidate_rules);
        if (FAILED(watcher_.enumerate(candidate_registry))) {
            watcher_.set_route_rules(&rules_);
            degraded_ = true;
            return WindowsAudioSessionRouteRefreshResultV1::Degraded;
        }
        std::size_t routed_count = 0U;
        for (const auto& session : candidate_registry.sessions()) {
            if (session.active && !session.lane_id.empty() && !session.output_group.empty()) {
                ++routed_count;
            }
        }
        GraphConfigV1 candidate_graph{};
        const bool graph_ready = routed_count > 0U &&
                                 build_session_route_graph(
                                     candidate_registry, SessionRouteGraphPolicyV1{}, candidate_graph);
        if (routed_count > 0U && !graph_ready) {
            watcher_.set_route_rules(&rules_);
            degraded_ = true;
            return WindowsAudioSessionRouteRefreshResultV1::Degraded;
        }
        rules_ = std::move(candidate_rules);
        registry_ = std::move(candidate_registry);
        graph_ = std::move(candidate_graph);
        watcher_.set_route_rules(&rules_);
        has_graph_ = graph_ready;
        degraded_ = false;
        ++generation_;
        return has_graph_ ? WindowsAudioSessionRouteRefreshResultV1::Applied
                          : WindowsAudioSessionRouteRefreshResultV1::NoRoutes;
    } catch (const std::bad_alloc&) {
        watcher_.set_route_rules(&rules_);
        degraded_ = true;
        return WindowsAudioSessionRouteRefreshResultV1::Degraded;
    } catch (...) {
        watcher_.set_route_rules(&rules_);
        degraded_ = true;
        return WindowsAudioSessionRouteRefreshResultV1::Degraded;
    }
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

HRESULT WindowsAudioSessionRouteCoordinatorV1::write_session_volume_handle(
    const std::uint64_t handle,
    const double requested_db,
    const bool mute,
    const GUID& event_context) noexcept {
    if (!bound_) return E_UNEXPECTED;
    const auto handle_generation = handle >> 32U;
    const auto handle_index = static_cast<std::uint32_t>(handle & 0xffffffffULL);
    if (handle == 0U || handle_generation == 0U || handle_generation != generation_ ||
        handle_index == 0U || handle_index > registry_.sessions().size()) {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    const auto& identity = registry_.sessions()[handle_index - 1U].identity.session_instance_id;
    return write_session_volume(identity, requested_db, mute, event_context);
}

HRESULT WindowsAudioSessionRouteCoordinatorV1::read_session_volume_handle(
    const std::uint64_t handle,
    double& requested_db,
    bool& mute) noexcept {
    if (!bound_) return E_UNEXPECTED;
    const auto handle_generation = handle >> 32U;
    const auto handle_index = static_cast<std::uint32_t>(handle & 0xffffffffULL);
    if (handle == 0U || handle_generation == 0U || handle_generation != generation_ ||
        handle_index == 0U || handle_index > registry_.sessions().size()) {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    const auto& identity = registry_.sessions()[handle_index - 1U].identity.session_instance_id;
    return read_session_volume(identity, requested_db, mute);
}

HRESULT WindowsAudioSessionRouteCoordinatorV1::bind_session_route_handle(
    const std::uint64_t handle,
    const std::string_view lane_id,
    const std::string_view output_group) noexcept {
    if (!bound_) return E_UNEXPECTED;
    if (lane_id.empty() || lane_id.size() > kSessionRouteCommandLaneMaxBytesV1 ||
        output_group.empty() || output_group.size() > kSessionRouteCommandOutputMaxBytesV1 ||
        !is_printable_utf8_v1(lane_id) || !is_printable_utf8_v1(output_group)) {
        return E_INVALIDARG;
    }
    const auto handle_generation = handle >> 32U;
    const auto handle_index = static_cast<std::uint32_t>(handle & 0xffffffffULL);
    if (handle == 0U || handle_generation == 0U || handle_generation != generation_ ||
        handle_index == 0U || handle_index > registry_.sessions().size()) {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    try {
        AudioSessionRegistry candidate_registry = registry_;
        const auto identity = candidate_registry.sessions()[handle_index - 1U].identity;
        if (!candidate_registry.bind(identity, std::string(lane_id), std::string(output_group))) {
            return E_INVALIDARG;
        }
        std::size_t routed_count = 0U;
        for (const auto& session : candidate_registry.sessions()) {
            if (session.active && !session.lane_id.empty() && !session.output_group.empty()) {
                ++routed_count;
            }
        }
        GraphConfigV1 candidate_graph{};
        const bool graph_ready = routed_count > 0U &&
                                 build_session_route_graph(candidate_registry,
                                                           SessionRouteGraphPolicyV1{},
                                                           candidate_graph);
        if (routed_count > 0U && !graph_ready) return E_FAIL;
        registry_ = std::move(candidate_registry);
        graph_ = std::move(candidate_graph);
        has_graph_ = graph_ready;
        degraded_ = false;
        ++generation_;
        return S_OK;
    } catch (const std::bad_alloc&) {
        degraded_ = true;
        return E_OUTOFMEMORY;
    } catch (...) {
        degraded_ = true;
        return E_FAIL;
    }
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

bool WindowsAudioSessionRouteCoordinatorV1::make_session_catalog_snapshot(
    const std::uint64_t sequence,
    SessionCatalogSnapshotV1& snapshot) noexcept {
    snapshot = {};
    if (!bound_ || degraded_ || sequence == 0U ||
        generation_ > static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)()) ||
        registry_.sessions().size() > kSessionCatalogSnapshotCapacityV1) {
        return false;
    }
    snapshot.sequence = sequence;
    snapshot.generation = generation_;
    snapshot.entry_count = static_cast<std::uint16_t>(registry_.sessions().size());
    for (std::size_t index = 0U; index < registry_.sessions().size(); ++index) {
        const auto& source = registry_.sessions()[index];
        auto& target = snapshot.entries[index];
        target.handle = (generation_ << 32U) | static_cast<std::uint64_t>(index + 1U);
        target.active = source.active ? 1U : 0U;
        target.route_state = !source.active
                                 ? SessionCatalogRouteStateV1::Unavailable
                                 : (source.lane_id.empty() || source.output_group.empty()
                                        ? SessionCatalogRouteStateV1::Pending
                                        : SessionCatalogRouteStateV1::Ready);
        if (source.active) {
            double requested_db = 0.0;
            bool mute = false;
            if (SUCCEEDED(watcher_.read_session_volume(source.identity.session_instance_id,
                                                       requested_db, mute))) {
                target.flags = 1U;
                target.requested_db_q16_16 = db_to_q16_16(requested_db);
                target.mute = mute ? 1U : 0U;
            }
        }
        const auto copy_text = [](const std::string& source_text,
                                  auto& target_text,
                                  std::uint16_t& target_bytes) noexcept {
            if (source_text.size() > target_text.size() ||
                !is_printable_utf8_v1(source_text)) {
                // Metadata is optional. Keep the session handle and route
                // state usable while omitting a malformed/oversized label;
                // never copy raw Windows IDs as a fallback label.
                target_bytes = 0U;
                target_text.fill('\0');
                return true;
            }
            target_bytes = static_cast<std::uint16_t>(source_text.size());
            std::copy(source_text.begin(), source_text.end(), target_text.begin());
            return true;
        };
        if (!copy_text(source.display_name, target.name, target.name_bytes) ||
            !copy_text(source.app_id, target.app, target.app_bytes) ||
            !copy_text(source.lane_id, target.lane, target.lane_bytes) ||
            !copy_text(source.output_group, target.output, target.output_bytes)) {
            snapshot = {};
            return false;
        }
    }
    return true;
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
