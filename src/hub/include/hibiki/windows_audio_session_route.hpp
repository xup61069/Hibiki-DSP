#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#if defined(_WIN32)

#include "hibiki/session_route.hpp"
#include "hibiki/session_route_rules.hpp"
#include "hibiki/process_loopback_plan.hpp"
#include "hibiki/windows_audio_session_watcher.hpp"

#include <cstddef>
#include <cstdint>

namespace hibiki {

enum class WindowsAudioSessionRouteRefreshResultV1 : std::uint8_t {
    Applied,
    NoRoutes,
    NoChange,
    Degraded,
    Unbound,
};

struct WindowsAudioSessionRouteSnapshotV1 {
    std::uint64_t generation{0U};
    std::size_t session_count{0U};
    std::size_t active_count{0U};
    std::size_t routed_count{0U};
    bool has_graph{false};
    bool degraded{false};
};

// Control-plane bridge from Windows IAudioSessionManager2 enumeration to the
// existing immutable GraphConfig transaction. It owns no RT state and never
// treats PID/session IDs as persistent profile identity.
class WindowsAudioSessionRouteCoordinatorV1 final {
public:
    WindowsAudioSessionRouteCoordinatorV1() noexcept = default;
    ~WindowsAudioSessionRouteCoordinatorV1() = default;

    WindowsAudioSessionRouteCoordinatorV1(const WindowsAudioSessionRouteCoordinatorV1&) = delete;
    WindowsAudioSessionRouteCoordinatorV1& operator=(
        const WindowsAudioSessionRouteCoordinatorV1&) = delete;

    [[nodiscard]] HRESULT bind(IMMDevice* device);
    void unbind() noexcept;
    [[nodiscard]] bool set_rules(const SessionRouteRuleStoreV1& rules) noexcept;
    [[nodiscard]] WindowsAudioSessionRouteRefreshResultV1 refresh() noexcept;
    [[nodiscard]] WindowsAudioSessionRouteRefreshResultV1 poll_and_refresh() noexcept;
    [[nodiscard]] bool copy_graph(GraphConfigV1& graph) const noexcept;
    [[nodiscard]] ProcessLoopbackPlanResultV1 copy_process_loopback_plan(
        ProcessLoopbackPlanV1& plan) const noexcept;
    [[nodiscard]] WindowsAudioSessionRouteSnapshotV1 snapshot() const noexcept;

private:
    WindowsAudioSessionWatcher watcher_{};
    SessionRouteRuleStoreV1 rules_{};
    AudioSessionRegistry registry_{};
    GraphConfigV1 graph_{};
    std::uint64_t generation_{0U};
    bool bound_{false};
    bool has_graph_{false};
    bool degraded_{false};
};

}  // namespace hibiki

#endif  // defined(_WIN32)
