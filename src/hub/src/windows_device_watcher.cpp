#include "hibiki/windows_device_watcher.hpp"

#if defined(_WIN32)

#include <algorithm>

namespace hibiki {

WindowsDeviceWatcher::~WindowsDeviceWatcher() {
    unregister();
}

HRESULT STDMETHODCALLTYPE WindowsDeviceWatcher::QueryInterface(REFIID iid, void** object) {
    if (object == nullptr) {
        return E_POINTER;
    }
    *object = nullptr;
    if (iid == IID_IUnknown || iid == __uuidof(IMMNotificationClient)) {
        *object = static_cast<IMMNotificationClient*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE WindowsDeviceWatcher::AddRef() {
    return references_.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE WindowsDeviceWatcher::Release() {
    const auto remaining = references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (remaining == 0) {
        delete this;
    }
    return remaining;
}

void WindowsDeviceWatcher::publish(const WindowsDeviceChangeKind kind,
                                   const EDataFlow flow,
                                   const ERole role,
                                   LPCWSTR id,
                                   const DWORD state) noexcept {
    auto claimed_sequence = sequence_.load(std::memory_order_relaxed);
    if ((claimed_sequence & 1U) != 0U ||
        !sequence_.compare_exchange_strong(claimed_sequence, claimed_sequence + 1U,
                                           std::memory_order_acq_rel,
                                           std::memory_order_relaxed)) {
        return;
    }
    kind_.store(static_cast<std::uint8_t>(kind), std::memory_order_relaxed);
    flow_.store(static_cast<std::int32_t>(flow), std::memory_order_relaxed);
    role_.store(static_cast<std::int32_t>(role), std::memory_order_relaxed);
    state_.store(state, std::memory_order_relaxed);
    // Keep the bounded snapshot fail-closed: never read past the caller's
    // string terminator and always leave a terminating NUL in the fixed
    // endpoint-id slots.
    const auto last_index = endpoint_id_.size() - 1;
    for (std::size_t index = 0; index < endpoint_id_.size(); ++index) {
        const auto value =
            (id == nullptr || index == last_index) ? L'\0' : id[index];
        endpoint_id_[index].store(value, std::memory_order_relaxed);
        if (value == L'\0') {
            for (std::size_t rest = index + 1; rest < endpoint_id_.size(); ++rest) {
                endpoint_id_[rest].store(L'\0', std::memory_order_relaxed);
            }
            break;
        }
    }
    sequence_.store(claimed_sequence + 2U, std::memory_order_release);
}

HRESULT STDMETHODCALLTYPE WindowsDeviceWatcher::OnDeviceStateChanged(LPCWSTR id,
                                                                       DWORD state) {
    publish(WindowsDeviceChangeKind::StateChanged, eAll, eConsole, id, state);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WindowsDeviceWatcher::OnDeviceAdded(LPCWSTR id) {
    publish(WindowsDeviceChangeKind::Added, eAll, eConsole, id, DEVICE_STATE_ACTIVE);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WindowsDeviceWatcher::OnDeviceRemoved(LPCWSTR id) {
    publish(WindowsDeviceChangeKind::Removed, eAll, eConsole, id, DEVICE_STATE_NOTPRESENT);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WindowsDeviceWatcher::OnDefaultDeviceChanged(EDataFlow flow,
                                                                        ERole role,
                                                                        LPCWSTR id) {
    publish(WindowsDeviceChangeKind::DefaultChanged, flow, role, id, DEVICE_STATE_ACTIVE);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WindowsDeviceWatcher::OnPropertyValueChanged(LPCWSTR id,
                                                                         const PROPERTYKEY) {
    publish(WindowsDeviceChangeKind::PropertyChanged, eAll, eConsole, id, 0);
    return S_OK;
}

HRESULT WindowsDeviceWatcher::register_with(IMMDeviceEnumerator* const enumerator) noexcept {
    if (enumerator == nullptr) {
        return E_INVALIDARG;
    }
    unregister();
    const auto result = enumerator->RegisterEndpointNotificationCallback(this);
    if (SUCCEEDED(result)) {
        enumerator_ = enumerator;
        enumerator_->AddRef();
        last_sequence_ = 0;
    }
    return result;
}

void WindowsDeviceWatcher::unregister() noexcept {
    if (enumerator_ != nullptr) {
        enumerator_->UnregisterEndpointNotificationCallback(this);
        enumerator_->Release();
        enumerator_ = nullptr;
    }
    last_sequence_ = 0;
}

bool WindowsDeviceWatcher::poll(WindowsDeviceChangeSnapshotV1& snapshot) noexcept {
    for (std::uint32_t attempt = 0; attempt < 4; ++attempt) {
        const auto first = sequence_.load(std::memory_order_acquire);
        if ((first & 1U) != 0U || first == 0 || first == last_sequence_) {
            return false;
        }
        WindowsDeviceChangeSnapshotV1 copy;
        copy.sequence = first;
        copy.kind = static_cast<WindowsDeviceChangeKind>(kind_.load(std::memory_order_relaxed));
        copy.flow = static_cast<EDataFlow>(flow_.load(std::memory_order_relaxed));
        copy.role = static_cast<ERole>(role_.load(std::memory_order_relaxed));
        copy.state = state_.load(std::memory_order_relaxed);
        for (std::size_t index = 0; index < copy.endpoint_id.size(); ++index) {
            copy.endpoint_id[index] = endpoint_id_[index].load(std::memory_order_relaxed);
        }
        const auto second = sequence_.load(std::memory_order_acquire);
        if (first == second && (second & 1U) == 0U) {
            last_sequence_ = second;
            snapshot = copy;
            return true;
        }
    }
    return false;
}

}  // namespace hibiki

#endif  // defined(_WIN32)
