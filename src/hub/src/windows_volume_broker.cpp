#include "hibiki/windows_volume_broker.hpp"

#if defined(_WIN32)

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hibiki {

WindowsVolumeCallback::WindowsVolumeCallback() noexcept {
    for (auto& scalar : channel_scalars_) {
        scalar.store(0.0F, std::memory_order_relaxed);
    }
    for (auto& byte : context_bytes_) {
        byte.store(0U, std::memory_order_relaxed);
    }
}

HRESULT STDMETHODCALLTYPE WindowsVolumeCallback::QueryInterface(REFIID iid,
                                                                 void** object) {
    if (object == nullptr) {
        return E_POINTER;
    }
    *object = nullptr;
    if (iid == IID_IUnknown || iid == __uuidof(IAudioEndpointVolumeCallback)) {
        *object = static_cast<IAudioEndpointVolumeCallback*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE WindowsVolumeCallback::AddRef() {
    return references_.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE WindowsVolumeCallback::Release() {
    const auto remaining = references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (remaining == 0) {
        delete this;
    }
    return remaining;
}

HRESULT STDMETHODCALLTYPE WindowsVolumeCallback::OnNotify(
    AUDIO_VOLUME_NOTIFICATION_DATA* const notification) {
    if (notification == nullptr) {
        return E_INVALIDARG;
    }
    sequence_.fetch_add(1, std::memory_order_acq_rel); // odd = writer in progress
    master_db_.store(notification->fMasterVolume, std::memory_order_relaxed);
    mute_.store(notification->bMuted != FALSE ? 1U : 0U, std::memory_order_relaxed);
    const auto count = std::min<std::uint32_t>(notification->nChannels, 8U);
    channel_count_.store(count, std::memory_order_relaxed);
    for (std::uint32_t index = 0; index < count; ++index) {
        channel_scalars_[index].store(notification->afChannelVolumes[index],
                                      std::memory_order_relaxed);
    }
    const auto* context = reinterpret_cast<const std::uint8_t*>(&notification->guidEventContext);
    for (std::size_t index = 0; index < sizeof(GUID); ++index) {
        context_bytes_[index].store(context[index], std::memory_order_relaxed);
    }
    sequence_.fetch_add(1, std::memory_order_release); // even = stable snapshot
    return S_OK;
}

bool WindowsVolumeCallback::read(WindowsVolumeNotificationSnapshotV1& snapshot) const noexcept {
    for (std::uint32_t attempt = 0; attempt < 4; ++attempt) {
        const auto first = sequence_.load(std::memory_order_acquire);
        if ((first & 1U) != 0U) {
            continue;
        }
        WindowsVolumeNotificationSnapshotV1 copy;
        copy.sequence = first;
        copy.requested_db = static_cast<double>(master_db_.load(std::memory_order_relaxed));
        copy.mute = mute_.load(std::memory_order_relaxed) != 0U;
        copy.channel_count = channel_count_.load(std::memory_order_relaxed);
        for (std::uint32_t index = 0; index < copy.channel_count && index < 8U; ++index) {
            copy.channel_scalars[index] = channel_scalars_[index].load(std::memory_order_relaxed);
        }
        auto* context = reinterpret_cast<std::uint8_t*>(&copy.event_context);
        for (std::size_t index = 0; index < sizeof(GUID); ++index) {
            context[index] = context_bytes_[index].load(std::memory_order_relaxed);
        }
        const auto second = sequence_.load(std::memory_order_acquire);
        if (first == second && (second & 1U) == 0U) {
            snapshot = copy;
            return true;
        }
    }
    return false;
}

WindowsVolumeBroker::WindowsVolumeBroker() : callback_(new WindowsVolumeCallback()) {}

WindowsVolumeBroker::~WindowsVolumeBroker() {
    unbind();
    if (callback_ != nullptr) {
        callback_->Release();
        callback_ = nullptr;
    }
}

HRESULT WindowsVolumeBroker::bind(IMMDevice* const device) noexcept {
    if (device == nullptr || callback_ == nullptr) {
        return E_INVALIDARG;
    }
    unbind();
    HRESULT result = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                     reinterpret_cast<void**>(&endpoint_));
    if (FAILED(result)) {
        endpoint_ = nullptr;
        return result;
    }
    result = endpoint_->RegisterControlChangeNotify(callback_);
    if (FAILED(result)) {
        endpoint_->Release();
        endpoint_ = nullptr;
    }
    last_callback_sequence_ = 0;
    return result;
}

void WindowsVolumeBroker::unbind() noexcept {
    if (endpoint_ != nullptr) {
        endpoint_->UnregisterControlChangeNotify(callback_);
        endpoint_->Release();
        endpoint_ = nullptr;
    }
    last_callback_sequence_ = 0;
}

HRESULT WindowsVolumeBroker::write(const OutputGroupVolumeStateV1& input,
                                   const GUID& event_context) noexcept {
    if (endpoint_ == nullptr) {
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
    const auto state = reconcile(input);
    const auto level = static_cast<float>(std::clamp(state.effective_db, -144.0, 12.0));
    HRESULT result = endpoint_->SetMasterVolumeLevel(level, &event_context);
    if (FAILED(result)) {
        return result;
    }
    return endpoint_->SetMute(state.mute ? TRUE : FALSE, &event_context);
}

HRESULT WindowsVolumeBroker::read_state(OutputGroupVolumeStateV1& state) noexcept {
    if (endpoint_ == nullptr) {
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
    float level = -144.0F;
    BOOL muted = FALSE;
    HRESULT result = endpoint_->GetMasterVolumeLevel(&level);
    if (FAILED(result)) {
        return result;
    }
    result = endpoint_->GetMute(&muted);
    if (FAILED(result)) {
        return result;
    }
    state.requested_db = static_cast<double>(level);
    state.mute = muted != FALSE;
    state.generation = ++generation_;
    state.origin = VolumeOrigin::Windows;
    state = reconcile(state);
    return S_OK;
}

bool WindowsVolumeBroker::poll(WindowsVolumeNotificationSnapshotV1& snapshot) noexcept {
    if (endpoint_ == nullptr || callback_ == nullptr) {
        return false;
    }
    WindowsVolumeNotificationSnapshotV1 next;
    if (!callback_->read(next) || next.sequence == 0 || next.sequence == last_callback_sequence_) {
        return false;
    }
    last_callback_sequence_ = next.sequence;
    next.generation = ++generation_;
    snapshot = next;
    return true;
}

}  // namespace hibiki

#endif  // defined(_WIN32)
