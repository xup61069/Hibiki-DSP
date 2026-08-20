#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#if defined(_WIN32)

#include "hibiki/audio_session_registry.hpp"

#include <audiopolicy.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace hibiki {

// IAudioSessionNotification callback boundary. The callback does not query
// COM objects or allocate; it only signals the worker to enumerate sessions.
class WindowsAudioSessionWatcher final : public IAudioSessionNotification {
public:
    WindowsAudioSessionWatcher() noexcept = default;
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
    std::atomic<ULONG> references_{1};
    std::atomic<std::uint64_t> sequence_{0};
    std::uint64_t last_sequence_{0};
    IAudioSessionManager2* manager_{nullptr};
    bool registered_{false};
    std::string endpoint_id_;
};

}  // namespace hibiki

#endif  // defined(_WIN32)
