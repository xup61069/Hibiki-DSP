#include "hibiki/windows_audio_session_watcher.hpp"

#if defined(_WIN32)

#include <windows.h>

#include <cwchar>
#include <string>
#include <utility>

namespace hibiki {
namespace {

std::string utf8_from_wide(const wchar_t* value) {
    if (value == nullptr || *value == L'\0') {
        return {};
    }
    const int source_length = static_cast<int>(wcslen(value));
    const int output_length = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value, source_length, nullptr, 0, nullptr, nullptr);
    if (output_length <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(output_length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, source_length,
                            result.data(), output_length, nullptr, nullptr) != output_length) {
        return {};
    }
    return result;
}

std::string take_wide_string(LPWSTR value) {
    std::string result = utf8_from_wide(value);
    if (value != nullptr) {
        CoTaskMemFree(value);
    }
    return result;
}

}  // namespace

WindowsAudioSessionWatcher::~WindowsAudioSessionWatcher() {
    unbind();
}

HRESULT STDMETHODCALLTYPE WindowsAudioSessionWatcher::QueryInterface(REFIID iid,
                                                                       void** object) {
    if (object == nullptr) {
        return E_POINTER;
    }
    *object = nullptr;
    if (iid == IID_IUnknown || iid == __uuidof(IAudioSessionNotification)) {
        *object = static_cast<IAudioSessionNotification*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE WindowsAudioSessionWatcher::AddRef() {
    return references_.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE WindowsAudioSessionWatcher::Release() {
    const auto remaining = references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (remaining == 0) {
        delete this;
    }
    return remaining;
}

HRESULT STDMETHODCALLTYPE WindowsAudioSessionWatcher::OnSessionCreated(
    IAudioSessionControl* const) {
    sequence_.fetch_add(1, std::memory_order_release);
    return S_OK;
}

HRESULT WindowsAudioSessionWatcher::bind(IMMDevice* const device) {
    if (device == nullptr) {
        return E_INVALIDARG;
    }
    unbind();

    LPWSTR endpoint = nullptr;
    HRESULT result = device->GetId(&endpoint);
    if (FAILED(result)) {
        return result;
    }
    endpoint_id_ = take_wide_string(endpoint);
    if (endpoint_id_.empty()) {
        return E_FAIL;
    }

    result = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(&manager_));
    if (FAILED(result)) {
        endpoint_id_.clear();
        return result;
    }
    result = manager_->RegisterSessionNotification(this);
    if (FAILED(result)) {
        manager_->Release();
        manager_ = nullptr;
        endpoint_id_.clear();
        return result;
    }
    registered_ = true;
    last_sequence_ = 0;
    return S_OK;
}

void WindowsAudioSessionWatcher::unbind() noexcept {
    if (manager_ != nullptr) {
        if (registered_) {
            manager_->UnregisterSessionNotification(this);
        }
        manager_->Release();
        manager_ = nullptr;
    }
    registered_ = false;
    endpoint_id_.clear();
    last_sequence_ = 0;
}

HRESULT WindowsAudioSessionWatcher::enumerate(AudioSessionRegistry& registry) {
    if (manager_ == nullptr || endpoint_id_.empty()) {
        return E_UNEXPECTED;
    }
    registry.mark_endpoint_sessions_inactive(endpoint_id_);

    IAudioSessionEnumerator* enumerator = nullptr;
    HRESULT result = manager_->GetSessionEnumerator(&enumerator);
    if (FAILED(result)) {
        return result;
    }

    int session_count = 0;
    result = enumerator->GetCount(&session_count);
    if (FAILED(result)) {
        enumerator->Release();
        return result;
    }

    HRESULT first_error = S_OK;
    for (int index = 0; index < session_count; ++index) {
        IAudioSessionControl* control = nullptr;
        IAudioSessionControl2* control2 = nullptr;
        AudioSessionState state = AudioSessionStateExpired;
        DWORD process_id = 0;
        LPWSTR instance_id = nullptr;
        LPWSTR session_id = nullptr;
        LPWSTR display_name = nullptr;

        result = enumerator->GetSession(index, &control);
        if (SUCCEEDED(result)) {
            result = control->QueryInterface(__uuidof(IAudioSessionControl2),
                                             reinterpret_cast<void**>(&control2));
        }
        if (SUCCEEDED(result)) {
            result = control->GetState(&state);
        }
        if (SUCCEEDED(result)) {
            result = control2->GetSessionInstanceIdentifier(&instance_id);
        }
        if (SUCCEEDED(result)) {
            result = control2->GetSessionIdentifier(&session_id);
        }
        if (SUCCEEDED(result)) {
            result = control2->GetProcessId(&process_id);
        }
        if (control != nullptr) {
            // Display name is optional; failure must not hide a usable session.
            control->GetDisplayName(&display_name);
        }

        if (SUCCEEDED(result)) {
            const auto instance = take_wide_string(instance_id);
            instance_id = nullptr;
            auto app_id = take_wide_string(session_id);
            session_id = nullptr;
            if (app_id.empty()) {
                app_id = instance;
            }
            AudioSessionDescriptorV1 descriptor;
            descriptor.identity = AudioSessionIdentityV1{endpoint_id_, instance, process_id};
            descriptor.display_name = take_wide_string(display_name);
            display_name = nullptr;
            descriptor.app_id = std::move(app_id);
            descriptor.active = state == AudioSessionStateActive;
            if (instance.empty() || !registry.upsert(std::move(descriptor))) {
                first_error = E_FAIL;
            }
        } else if (SUCCEEDED(first_error)) {
            first_error = result;
        }

        if (instance_id != nullptr) {
            CoTaskMemFree(instance_id);
        }
        if (session_id != nullptr) {
            CoTaskMemFree(session_id);
        }
        if (display_name != nullptr) {
            CoTaskMemFree(display_name);
        }
        if (control2 != nullptr) {
            control2->Release();
        }
        if (control != nullptr) {
            control->Release();
        }
    }
    enumerator->Release();
    return first_error;
}

bool WindowsAudioSessionWatcher::poll(std::uint64_t& sequence) noexcept {
    const auto current = sequence_.load(std::memory_order_acquire);
    if (current == 0 || current == last_sequence_) {
        return false;
    }
    last_sequence_ = current;
    sequence = current;
    return true;
}

}  // namespace hibiki

#endif  // defined(_WIN32)
