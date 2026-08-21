#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#if defined(_WIN32)

#include "hibiki/volume_state.hpp"

#include <endpointvolume.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace hibiki {

struct WindowsVolumeNotificationSnapshotV1 {
    std::uint64_t sequence{0};
    std::uint64_t generation{0};
    double requested_db{-144.0};
    bool mute{false};
    std::uint32_t channel_count{0};
    std::array<float, 8> channel_scalars{};
    GUID event_context{};
};

// Implements only the COM callback. OnNotify performs atomic copies and never
// waits, allocates, calls COM, or invokes user code.
class WindowsVolumeCallback final : public IAudioEndpointVolumeCallback {
public:
    WindowsVolumeCallback() noexcept;

    WindowsVolumeCallback(const WindowsVolumeCallback&) = delete;
    WindowsVolumeCallback& operator=(const WindowsVolumeCallback&) = delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE OnNotify(
        AUDIO_VOLUME_NOTIFICATION_DATA* notification) override;

    [[nodiscard]] bool read(WindowsVolumeNotificationSnapshotV1& snapshot) const noexcept;

private:
    ~WindowsVolumeCallback() = default;

    std::atomic<ULONG> references_{1};
    std::atomic<std::uint64_t> sequence_{0};
    std::atomic<float> master_db_{-144.0F};
    std::atomic<std::uint32_t> mute_{0};
    std::atomic<std::uint32_t> channel_count_{0};
    std::array<std::atomic<float>, 8> channel_scalars_{};
    std::array<std::atomic<std::uint8_t>, sizeof(GUID)> context_bytes_{};
};

class WindowsVolumeBroker final {
public:
    WindowsVolumeBroker();
    ~WindowsVolumeBroker();

    WindowsVolumeBroker(const WindowsVolumeBroker&) = delete;
    WindowsVolumeBroker& operator=(const WindowsVolumeBroker&) = delete;

    [[nodiscard]] HRESULT bind(IMMDevice* device) noexcept;
    // Keeps the current COM callback registration when the default endpoint
    // identity is unchanged. A changed identity falls back to bind().
    [[nodiscard]] HRESULT bind_if_changed(IMMDevice* device) noexcept;
    void unbind() noexcept;
    [[nodiscard]] bool is_bound() const noexcept { return endpoint_ != nullptr; }
    [[nodiscard]] HRESULT write(const OutputGroupVolumeStateV1& state,
                                const GUID& event_context) noexcept;
    [[nodiscard]] HRESULT read_state(OutputGroupVolumeStateV1& state) noexcept;
    [[nodiscard]] bool poll(WindowsVolumeNotificationSnapshotV1& snapshot) noexcept;

private:
    IAudioEndpointVolume* endpoint_{nullptr};
    WindowsVolumeCallback* callback_{nullptr};
    std::wstring endpoint_id_{};
    std::uint64_t last_callback_sequence_{0};
    std::uint64_t generation_{0};
};

}  // namespace hibiki

#endif  // defined(_WIN32)
