#include "hibiki/windows_audio_session_watcher.hpp"

#if defined(_WIN32)

#include <windows.h>

#include <cwchar>
#include <cmath>
#include <algorithm>
#include <string>
#include <string_view>
#include <thread>
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

WindowsAudioSessionWatcher::WindowsAudioSessionWatcher() noexcept {
    reset_pending_sessions();
}

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

bool WindowsAudioSessionWatcher::enqueue_pending_session(
    IAudioSessionControl* const control) noexcept {
    if (control == nullptr) return false;

    auto position = pending_enqueue_.load(std::memory_order_relaxed);
    for (;;) {
        auto& slot = pending_sessions_[position % kPendingSessionCapacity];
        const auto slot_sequence = slot.sequence.load(std::memory_order_acquire);
        const auto difference = static_cast<std::int64_t>(slot_sequence) -
                                static_cast<std::int64_t>(position);
        if (difference == 0) {
            if (pending_enqueue_.compare_exchange_weak(
                    position, position + 1U, std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                // The callback is not an RT audio callback. Retain only the
                // control pointer here; all COM queries stay on the worker.
                control->AddRef();
                slot.control = control;
                slot.sequence.store(position + 1U, std::memory_order_release);
                return true;
            }
            continue;
        }
        if (difference < 0) return false;
        position = pending_enqueue_.load(std::memory_order_relaxed);
    }
}

bool WindowsAudioSessionWatcher::try_enter_callback() noexcept {
    auto state = callbacks_state_.load(std::memory_order_relaxed);
    for (;;) {
        if ((state & kCallbacksBlocked) != 0U || state == kCallbacksCountMask) {
            return false;
        }
        if (callbacks_state_.compare_exchange_weak(
                state, state + 1U, std::memory_order_seq_cst,
                std::memory_order_relaxed)) {
            return true;
        }
    }
}

void WindowsAudioSessionWatcher::leave_callback() noexcept {
    (void)callbacks_state_.fetch_sub(1U, std::memory_order_seq_cst);
}

void WindowsAudioSessionWatcher::block_callbacks() noexcept {
    auto state = callbacks_state_.load(std::memory_order_relaxed);
    for (;;) {
        if ((state & kCallbacksBlocked) != 0U) return;
        if (callbacks_state_.compare_exchange_weak(
                state, state | kCallbacksBlocked, std::memory_order_seq_cst,
                std::memory_order_relaxed)) {
            return;
        }
    }
}

bool WindowsAudioSessionWatcher::dequeue_pending_session(
    IAudioSessionControl*& control) noexcept {
    auto position = pending_dequeue_.load(std::memory_order_relaxed);
    for (;;) {
        auto& slot = pending_sessions_[position % kPendingSessionCapacity];
        const auto slot_sequence = slot.sequence.load(std::memory_order_acquire);
        const auto difference = static_cast<std::int64_t>(slot_sequence) -
                                static_cast<std::int64_t>(position + 1U);
        if (difference == 0) {
            if (pending_dequeue_.compare_exchange_weak(
                    position, position + 1U, std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                control = slot.control;
                slot.control = nullptr;
                slot.sequence.store(position + kPendingSessionCapacity,
                                    std::memory_order_release);
                return true;
            }
            continue;
        }
        if (difference < 0) return false;
        position = pending_dequeue_.load(std::memory_order_relaxed);
    }
}

void WindowsAudioSessionWatcher::reset_pending_sessions() noexcept {
    for (std::size_t index = 0U; index < kPendingSessionCapacity; ++index) {
        pending_sessions_[index].control = nullptr;
        pending_sessions_[index].sequence.store(index, std::memory_order_relaxed);
    }
    pending_enqueue_.store(0U, std::memory_order_relaxed);
    pending_dequeue_.store(0U, std::memory_order_relaxed);
}

void WindowsAudioSessionWatcher::release_pending_sessions() noexcept {
    IAudioSessionControl* control = nullptr;
    while (dequeue_pending_session(control)) {
        if (control != nullptr) control->Release();
        control = nullptr;
    }
}

void WindowsAudioSessionWatcher::release_cached_session_controls() noexcept {
    for (auto& cached : cached_session_controls_) {
        if (cached.control != nullptr) cached.control->Release();
        cached.control = nullptr;
    }
    cached_session_controls_.clear();
}

bool WindowsAudioSessionWatcher::cache_session_control(
    const std::string_view session_instance_id,
    IAudioSessionControl* const control) noexcept {
    if (session_instance_id.empty() || control == nullptr) return false;
    for (auto& cached : cached_session_controls_) {
        if (cached.session_instance_id != session_instance_id) continue;
        if (cached.control != control) {
            control->AddRef();
            if (cached.control != nullptr) cached.control->Release();
            cached.control = control;
        }
        return true;
    }
    CachedSessionControl cached;
    IAudioSessionControl* retained = nullptr;
    try {
        cached.session_instance_id = std::string(session_instance_id);
        control->AddRef();
        retained = control;
        cached.control = control;
        if (cached_session_controls_.size() >= kSessionControlCacheCapacity) {
            auto& oldest = cached_session_controls_.front();
            if (oldest.control != nullptr) oldest.control->Release();
            cached_session_controls_.erase(cached_session_controls_.begin());
        }
        cached_session_controls_.push_back(std::move(cached));
        retained = nullptr;
        return true;
    } catch (...) {
        if (retained != nullptr) retained->Release();
        return false;
    }
}

IAudioSessionControl* WindowsAudioSessionWatcher::find_cached_session_control(
    const std::string_view session_instance_id) const noexcept {
    for (const auto& cached : cached_session_controls_) {
        if (cached.session_instance_id == session_instance_id) return cached.control;
    }
    return nullptr;
}

HRESULT STDMETHODCALLTYPE WindowsAudioSessionWatcher::OnSessionCreated(
    IAudioSessionControl* const new_session) {
    if (!try_enter_callback()) return S_OK;
    if (new_session != nullptr) (void)enqueue_pending_session(new_session);
    sequence_.fetch_add(1, std::memory_order_release);
    leave_callback();
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
        if (endpoint != nullptr) CoTaskMemFree(endpoint);
        return result;
    }
    endpoint_id_ = take_wide_string(endpoint);
    if (endpoint_id_.empty()) {
        return E_FAIL;
    }

    result = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(&manager_));
    if (SUCCEEDED(result) && manager_ == nullptr) result = E_POINTER;
    if (FAILED(result)) {
        if (manager_ != nullptr) {
            manager_->Release();
            manager_ = nullptr;
        }
        endpoint_id_.clear();
        return result;
    }
    // Open callback admission before registration. The session manager may
    // deliver a notification synchronously (or from another thread) before
    // RegisterSessionNotification returns; closing the gate until after the
    // call would lose that session when the enumerator snapshot lags.
    last_sequence_ = 0;
    sequence_.store(0U, std::memory_order_release);
    callbacks_state_.store(0U, std::memory_order_seq_cst);
    result = manager_->RegisterSessionNotification(this);
    if (FAILED(result)) {
        // Registration can fail after a callback has entered. Reuse the
        // callback-safe teardown path so any retained control is released.
        unbind();
        return result;
    }
    registered_ = true;
    return S_OK;
}

void WindowsAudioSessionWatcher::unbind() noexcept {
    block_callbacks();
    if (manager_ != nullptr) {
        if (registered_) {
            manager_->UnregisterSessionNotification(this);
        }
        manager_->Release();
        manager_ = nullptr;
    }
    while ((callbacks_state_.load(std::memory_order_seq_cst) &
            kCallbacksCountMask) != 0U) {
        std::this_thread::yield();
    }
    release_pending_sessions();
    release_cached_session_controls();
    reset_pending_sessions();
    registered_ = false;
    endpoint_id_.clear();
    last_sequence_ = 0;
    sequence_.store(0U, std::memory_order_release);
}

HRESULT WindowsAudioSessionWatcher::upsert_session_control(
    AudioSessionRegistry& registry,
    IAudioSessionControl* const control) {
    if (control == nullptr || endpoint_id_.empty()) return E_INVALIDARG;

    IAudioSessionControl2* control2 = nullptr;
    AudioSessionState state = AudioSessionStateExpired;
    DWORD process_id = 0;
    LPWSTR instance_id = nullptr;
    LPWSTR session_id = nullptr;
    LPWSTR display_name = nullptr;

    HRESULT result = control->QueryInterface(__uuidof(IAudioSessionControl2),
                                             reinterpret_cast<void**>(&control2));
    if (SUCCEEDED(result) && control2 == nullptr) result = E_POINTER;
    if (SUCCEEDED(result)) result = control->GetState(&state);
    if (SUCCEEDED(result)) result = control2->GetSessionInstanceIdentifier(&instance_id);
    if (SUCCEEDED(result)) result = control2->GetSessionIdentifier(&session_id);
    if (SUCCEEDED(result)) result = control2->GetProcessId(&process_id);
    if (SUCCEEDED(result)) {
        // Display name is optional; failure must not hide a usable session.
        control->GetDisplayName(&display_name);
    }

    bool rule_error = false;
    if (SUCCEEDED(result)) {
        const auto instance = take_wide_string(instance_id);
        instance_id = nullptr;
        auto app_id = take_wide_string(session_id);
        session_id = nullptr;
        if (app_id.empty()) app_id = instance;
        const bool control_cached = cache_session_control(instance, control);

        AudioSessionDescriptorV1 descriptor;
        descriptor.identity = AudioSessionIdentityV1{endpoint_id_, instance, process_id};
        descriptor.display_name = take_wide_string(display_name);
        display_name = nullptr;
        descriptor.app_id = std::move(app_id);
        descriptor.active = state == AudioSessionStateActive;
        if (route_rules_ != nullptr) {
            const auto rule_result = route_rules_->apply(descriptor);
            if (rule_result == SessionRouteRuleResultV1::ambiguous ||
                rule_result == SessionRouteRuleResultV1::invalid_argument ||
                rule_result == SessionRouteRuleResultV1::capacity_exhausted) {
                // Keep the session visible but leave it unbound. A bad rule
                // must never silently pick a route or alter the Windows
                // session gain owner.
                rule_error = true;
            }
        }
        if (instance.empty() || !control_cached || !registry.upsert(std::move(descriptor))) {
            result = E_FAIL;
        } else if (rule_error) {
            result = E_FAIL;
        }
    }

    if (instance_id != nullptr) CoTaskMemFree(instance_id);
    if (session_id != nullptr) CoTaskMemFree(session_id);
    if (display_name != nullptr) CoTaskMemFree(display_name);
    if (control2 != nullptr) control2->Release();
    return result;
}

HRESULT WindowsAudioSessionWatcher::enumerate(AudioSessionRegistry& registry) {
    if (manager_ == nullptr || endpoint_id_.empty()) {
        return E_UNEXPECTED;
    }
    registry.mark_endpoint_sessions_inactive(endpoint_id_);

    // GetSessionEnumerator is not guaranteed to include sessions delivered by
    // IAudioSessionNotification. Process the retained controls first so a new
    // session remains discoverable even when the enumerator snapshot lags.
    HRESULT first_error = S_OK;
    IAudioSessionControl* pending_control = nullptr;
    while (dequeue_pending_session(pending_control)) {
        if (pending_control != nullptr) {
            const auto result = upsert_session_control(registry, pending_control);
            if (FAILED(result) && SUCCEEDED(first_error)) first_error = result;
            pending_control->Release();
        }
        pending_control = nullptr;
    }

    IAudioSessionEnumerator* enumerator = nullptr;
    HRESULT result = manager_->GetSessionEnumerator(&enumerator);
    if (FAILED(result)) {
        if (enumerator != nullptr) enumerator->Release();
        return SUCCEEDED(first_error) ? result : first_error;
    }
    if (enumerator == nullptr) return SUCCEEDED(first_error) ? E_POINTER : first_error;

    int session_count = 0;
    result = enumerator->GetCount(&session_count);
    if (SUCCEEDED(result) && session_count < 0) result = E_INVALIDARG;
    if (FAILED(result)) {
        enumerator->Release();
        return SUCCEEDED(first_error) ? result : first_error;
    }

    for (int index = 0; index < session_count; ++index) {
        IAudioSessionControl* control = nullptr;

        result = enumerator->GetSession(index, &control);
        if (SUCCEEDED(result) && control == nullptr) result = E_POINTER;
        if (SUCCEEDED(result)) {
            result = upsert_session_control(registry, control);
        }
        if (FAILED(result) && SUCCEEDED(first_error)) first_error = result;
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
        if (enumerator != nullptr) enumerator->Release();
        return result;
    }
    if (enumerator == nullptr) return E_POINTER;
    int session_count = 0;
    result = enumerator->GetCount(&session_count);
    if (SUCCEEDED(result) && session_count < 0) result = E_INVALIDARG;
    if (FAILED(result)) {
        enumerator->Release();
        return result;
    }
    result = E_INVALIDARG;
    for (int index = 0; index < session_count; ++index) {
        IAudioSessionControl* control = nullptr;
        IAudioSessionControl2* control2 = nullptr;
        LPWSTR instance_id = nullptr;
        HRESULT session_result = enumerator->GetSession(index, &control);
        if (SUCCEEDED(session_result) && control == nullptr) session_result = E_POINTER;
        if (SUCCEEDED(session_result)) {
            session_result = control->QueryInterface(
                __uuidof(IAudioSessionControl2), reinterpret_cast<void**>(&control2));
        }
        if (SUCCEEDED(session_result) && control2 == nullptr) session_result = E_POINTER;
        if (SUCCEEDED(session_result)) {
            session_result = control2->GetSessionInstanceIdentifier(&instance_id);
        }
        if (SUCCEEDED(session_result)) {
            const auto instance = take_wide_string(instance_id);
            instance_id = nullptr;
            if (instance == session_instance_id) {
                result = control->QueryInterface(
                    __uuidof(ISimpleAudioVolume), reinterpret_cast<void**>(volume));
                if (FAILED(result) && *volume != nullptr) {
                    (*volume)->Release();
                    *volume = nullptr;
                }
                if (SUCCEEDED(result) && *volume == nullptr) result = E_POINTER;
                session_result = result;
            } else {
                // A successful metadata read for a different session is not
                // a match; keep searching instead of treating it as success.
                session_result = E_INVALIDARG;
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
        if (SUCCEEDED(result) && SUCCEEDED(session_result)) {
            break;
        }
    }
    enumerator->Release();
    (void)endpoint_id;
    return result;
}

bool restore_session_volume(ISimpleAudioVolume* const volume,
                            const float scalar,
                            const BOOL muted,
                            const GUID& event_context) noexcept {
    if (volume == nullptr || FAILED(volume->SetMasterVolume(scalar, &event_context)) ||
        FAILED(volume->SetMute(muted, &event_context))) {
        return false;
    }
    float verified_scalar = 0.0F;
    BOOL verified_mute = FALSE;
    if (FAILED(volume->GetMasterVolume(&verified_scalar)) ||
        FAILED(volume->GetMute(&verified_mute))) {
        return false;
    }
    return std::isfinite(verified_scalar) &&
           std::abs(verified_scalar - scalar) <= 1e-5F && verified_mute == muted;
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
    HRESULT result = E_INVALIDARG;
    if (auto* const cached = find_cached_session_control(session_instance_id);
        cached != nullptr) {
        result = cached->QueryInterface(__uuidof(ISimpleAudioVolume),
                                        reinterpret_cast<void**>(&volume));
        if (SUCCEEDED(result) && volume == nullptr) result = E_POINTER;
    }
    if (FAILED(result)) {
        if (volume != nullptr) {
            volume->Release();
            volume = nullptr;
        }
        result = acquire_session_volume(manager_, endpoint_id_, session_instance_id, &volume);
    }
    if (SUCCEEDED(result) && volume == nullptr) result = E_POINTER;
    if (SUCCEEDED(result)) {
        float previous_scalar = 0.0F;
        BOOL previous_mute = FALSE;
        bool write_started = false;
        // ISimpleAudioVolume has separate setters. Snapshot both values so a
        // partial setter failure can be rolled back and verified before the
        // failure is returned to the caller.
        result = volume->GetMasterVolume(&previous_scalar);
        if (SUCCEEDED(result) &&
            (!std::isfinite(previous_scalar) || previous_scalar < 0.0F ||
             previous_scalar > 1.0F)) {
            result = E_FAIL;
        }
        if (SUCCEEDED(result)) result = volume->GetMute(&previous_mute);
        if (SUCCEEDED(result)) {
            write_started = true;
            result = volume->SetMasterVolume(scalar, &event_context);
        }
        if (SUCCEEDED(result)) result = volume->SetMute(mute ? TRUE : FALSE, &event_context);
        if (write_started && FAILED(result) &&
            !restore_session_volume(volume, previous_scalar, previous_mute, event_context)) {
            result = E_FAIL;
        }
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
    HRESULT result = E_INVALIDARG;
    if (auto* const cached = find_cached_session_control(session_instance_id);
        cached != nullptr) {
        result = cached->QueryInterface(__uuidof(ISimpleAudioVolume),
                                        reinterpret_cast<void**>(&volume));
        if (SUCCEEDED(result) && volume == nullptr) result = E_POINTER;
    }
    if (FAILED(result)) {
        if (volume != nullptr) {
            volume->Release();
            volume = nullptr;
        }
        result = acquire_session_volume(manager_, endpoint_id_, session_instance_id, &volume);
    }
    if (SUCCEEDED(result) && volume == nullptr) result = E_POINTER;
    if (FAILED(result)) {
        return result;
    }
    float scalar = 0.0F;
    BOOL muted = FALSE;
    HRESULT read_result = volume->GetMasterVolume(&scalar);
    if (SUCCEEDED(read_result) &&
        (!std::isfinite(scalar) || scalar < 0.0F || scalar > 1.0F)) {
        read_result = E_FAIL;
    }
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
