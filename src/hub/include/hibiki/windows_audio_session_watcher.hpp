#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#if defined(_WIN32)

#include "hibiki/audio_session_registry.hpp"
#include "hibiki/session_route_rules.hpp"

#include <audiopolicy.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hibiki {

// IAudioSessionNotification callback boundary. The callback does not query
// COM objects or allocate; it only retains a bounded control reference and
// signals the worker to enumerate sessions.
class WindowsAudioSessionWatcher final : public IAudioSessionNotification {
public:
    WindowsAudioSessionWatcher() noexcept;
    ~WindowsAudioSessionWatcher();

    WindowsAudioSessionWatcher(const WindowsAudioSessionWatcher&) = delete;
    WindowsAudioSessionWatcher& operator=(const WindowsAudioSessionWatcher&) = delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl* new_session) override;

    [[nodiscard]] HRESULT bind(IMMDevice* device);
    void unbind() noexcept;
    [[nodiscard]] HRESULT enumerate(AudioSessionRegistry& registry);
    // Non-owning control-plane rule store. The session callback never reads
    // this pointer; the worker applies rules during enumerate().
    void set_route_rules(const SessionRouteRuleStoreV1* rules) noexcept {
        route_rules_ = rules;
    }
    [[nodiscard]] HRESULT write_session_volume(std::string_view session_instance_id,
                                                double requested_db,
                                                bool mute,
                                                const GUID& event_context);
    [[nodiscard]] HRESULT read_session_volume(std::string_view session_instance_id,
                                               double& requested_db,
                                               bool& mute);
    [[nodiscard]] bool poll(std::uint64_t& sequence) noexcept;
    [[nodiscard]] const std::string& endpoint_id() const noexcept { return endpoint_id_; }

private:
    static constexpr std::size_t kPendingSessionCapacity = 64U;
    static constexpr std::size_t kSessionControlCacheCapacity = 256U;
    static constexpr std::uint32_t kCallbacksBlocked = 1U << 31U;
    static constexpr std::uint32_t kCallbacksCountMask = kCallbacksBlocked - 1U;

    struct PendingSessionSlot final {
        std::atomic<std::size_t> sequence{0U};
        IAudioSessionControl* control{nullptr};
    };

    [[nodiscard]] bool enqueue_pending_session(IAudioSessionControl* control) noexcept;
    [[nodiscard]] bool dequeue_pending_session(IAudioSessionControl*& control) noexcept;
    [[nodiscard]] bool try_enter_callback() noexcept;
    void leave_callback() noexcept;
    void block_callbacks() noexcept;
    void reset_pending_sessions() noexcept;
    void release_pending_sessions() noexcept;
    void release_cached_session_controls() noexcept;
    [[nodiscard]] bool cache_session_control(std::string_view session_instance_id,
                                              IAudioSessionControl* control) noexcept;
    [[nodiscard]] IAudioSessionControl* find_cached_session_control(
        std::string_view session_instance_id) const noexcept;
    [[nodiscard]] HRESULT upsert_session_control(AudioSessionRegistry& registry,
                                                 IAudioSessionControl* control);

    std::atomic<ULONG> references_{1};
    // Teardown sets the high bit to close callback admission, then waits for
    // the low-bit in-flight count before draining or resetting the queue. A
    // callback that races with closure can only win the CAS before closure or
    // observe the closed state; it cannot enter after the wait sees zero.
    std::atomic<std::uint32_t> callbacks_state_{0U};
    std::atomic<bool> destroying_{false};
    std::atomic<std::uint64_t> sequence_{0};
    std::uint64_t last_sequence_{0};
    IAudioSessionManager2* manager_{nullptr};
    bool registered_{false};
    std::string endpoint_id_;
    const SessionRouteRuleStoreV1* route_rules_{nullptr};
    std::array<PendingSessionSlot, kPendingSessionCapacity> pending_sessions_{};
    std::atomic<std::size_t> pending_enqueue_{0U};
    std::atomic<std::size_t> pending_dequeue_{0U};

    struct CachedSessionControl final {
        std::string session_instance_id;
        IAudioSessionControl* control{nullptr};
    };
    // Worker-owned bounded FIFO cache. Entries are evicted oldest-first when
    // Windows reports more historical sessions than the registry capacity.
    std::vector<CachedSessionControl> cached_session_controls_;
};

}  // namespace hibiki

#endif  // defined(_WIN32)
