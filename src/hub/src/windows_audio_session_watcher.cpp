#include "hibiki/windows_audio_session_watcher.hpp"

#if defined(_WIN32)

#include <windows.h>

#include <cwchar>
#include <cmath>
#include <algorithm>
#include <string>
#include <string_view>
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

float db_to_session_scalar(const double db) {
    if (!std::isfinite(db) || db < -144.0 || db > 0.0) {
        return -1.0F;
    }
    return db <= -144.0 ? 0.0F : static_cast<float>(std::pow(10.0, db / 20.0));
}

double session_scalar_to_db(const float scalar) {
    if (!std::isfinite(scalar) || scalar <= 0.0F) {
        return -144.0;
    }
    return std::clamp(20.0 * std::log10(static_cast<double>(scalar)), -144.0, 0.0);
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

namespace {

HRESULT acquire_session_volume(IAudioSessionManager2* const manager,
                                const std::string& endpoint_id,
                                const std::string_view session_instance_id,
                                ISimpleAudioVolume** volume) {
    if (manager == nullptr || volume == nullptr || session_instance_id.empty()) {
        return E_INVALIDARG;
    }
    *volume = nullptr;
    IAudioSessionEnumerator* enumerator = nullptr;
    HRESULT result = manager->GetSessionEnumerator(&enumerator);
    if (FAILED(result)) {
        return result;
    }
    int session_count = 0;
    result = enumerator->GetCount(&session_count);
    if (FAILED(result)) {
        enumerator->Release();
        return result;
    }
    result = E_INVALIDARG;
    for (int index = 0; index < session_count; ++index) {
        IAudioSessionControl* control = nullptr;
        IAudioSessionControl2* control2 = nullptr;
        LPWSTR instance_id = nullptr;
        if (SUCCEEDED(enumerator->GetSession(index, &control)) &&
            SUCCEEDED(control->QueryInterface(__uuidof(IAudioSessionControl2),
                                               reinterpret_cast<void**>(&control2))) &&
            SUCCEEDED(control2->GetSessionInstanceIdentifier(&instance_id))) {
            const auto instance = take_wide_string(instance_id);
            instance_id = nullptr;
            if (instance == session_instance_id &&
                SUCCEEDED(control->QueryInterface(__uuidof(ISimpleAudioVolume),
                                                   reinterpret_cast<void**>(volume)))) {
                result = S_OK;
            }
        }
        if (instance_id != nullptr) {
            CoTaskMemFree(instance_id);
        }
        if (control2 != nullptr) {
            control2->Release();
        }
        if (control != nullptr) {
            control->Release();
        }
        if (SUCCEEDED(result)) {
            break;
        }
    }
    enumerator->Release();
    (void)endpoint_id;
    return result;
}

}  // namespace

HRESULT WindowsAudioSessionWatcher::write_session_volume(
    const std::string_view session_instance_id,
    const double requested_db,
    const bool mute,
    const GUID& event_context) {
    if (manager_ == nullptr || endpoint_id_.empty()) {
        return E_UNEXPECTED;
    }
    const float scalar = db_to_session_scalar(requested_db);
    if (scalar < 0.0F) {
        return E_INVALIDARG;
    }
    ISimpleAudioVolume* volume = nullptr;
    HRESULT result = acquire_session_volume(manager_, endpoint_id_, session_instance_id, &volume);
    if (SUCCEEDED(result)) {
        result = volume->SetMasterVolume(scalar, &event_context);
    }
    if (SUCCEEDED(result)) {
        result = volume->SetMute(mute ? TRUE : FALSE, &event_context);
    }
    if (volume != nullptr) {
        volume->Release();
    }
    return result;
}

HRESULT WindowsAudioSessionWatcher::read_session_volume(
    const std::string_view session_instance_id,
    double& requested_db,
    bool& mute) {
    if (manager_ == nullptr || endpoint_id_.empty()) {
        return E_UNEXPECTED;
    }
    ISimpleAudioVolume* volume = nullptr;
    const HRESULT result = acquire_session_volume(manager_, endpoint_id_, session_instance_id, &volume);
    if (FAILED(result)) {
        return result;
    }
    float scalar = 0.0F;
    BOOL muted = FALSE;
    HRESULT read_result = volume->GetMasterVolume(&scalar);
    if (SUCCEEDED(read_result)) {
        read_result = volume->GetMute(&muted);
    }
    if (SUCCEEDED(read_result)) {
        requested_db = session_scalar_to_db(scalar);
        mute = muted != FALSE;
    }
    volume->Release();
    return read_result;
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
