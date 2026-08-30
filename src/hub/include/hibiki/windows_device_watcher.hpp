#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#if defined(_WIN32)

#include <mmdeviceapi.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace hibiki {

enum class WindowsDeviceChangeKind : std::uint8_t {
    Added,
    Removed,
    StateChanged,
    PropertyChanged,
    DefaultChanged,
};

struct WindowsDeviceChangeSnapshotV1 {
    std::uint64_t sequence{0};
    WindowsDeviceChangeKind kind{WindowsDeviceChangeKind::PropertyChanged};
    EDataFlow flow{eAll};
    ERole role{eConsole};
    DWORD state{0};
    std::array<wchar_t, 260> endpoint_id{};
};

// IMMNotificationClient callback that uses an atomic sequence claim before
// copying bounded data into atomic fields. A contended callback drops its
// event rather than expose a partial tuple. Rebinding and COM object lifetime
// changes happen on a worker thread after poll(), never inside these callbacks.
class WindowsDeviceWatcher final : public IMMNotificationClient {
public:
    WindowsDeviceWatcher() noexcept = default;

    WindowsDeviceWatcher(const WindowsDeviceWatcher&) = delete;
    WindowsDeviceWatcher& operator=(const WindowsDeviceWatcher&) = delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR id, DWORD state) override;
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR id) override;
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR id) override;
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow,
                                                      ERole role,
                                                      LPCWSTR id) override;
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR id,
                                                     const PROPERTYKEY key) override;

    [[nodiscard]] HRESULT register_with(IMMDeviceEnumerator* enumerator) noexcept;
    void unregister() noexcept;
    [[nodiscard]] bool poll(WindowsDeviceChangeSnapshotV1& snapshot) noexcept;

private:
    ~WindowsDeviceWatcher();
    void publish(WindowsDeviceChangeKind kind,
                 EDataFlow flow,
                 ERole role,
                 LPCWSTR id,
                 DWORD state) noexcept;

    std::atomic<ULONG> references_{1};
    std::atomic<std::uint64_t> sequence_{0};
    std::atomic<std::uint8_t> kind_{static_cast<std::uint8_t>(WindowsDeviceChangeKind::PropertyChanged)};
    std::atomic<std::int32_t> flow_{static_cast<std::int32_t>(eAll)};
    std::atomic<std::int32_t> role_{static_cast<std::int32_t>(eConsole)};
    std::atomic<DWORD> state_{0};
    std::array<std::atomic<wchar_t>, 260> endpoint_id_{};
    IMMDeviceEnumerator* enumerator_{nullptr};
    std::uint64_t last_sequence_{0};
};

}  // namespace hibiki

#endif  // defined(_WIN32)
