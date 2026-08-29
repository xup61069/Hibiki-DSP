// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/windows_volume_broker.hpp"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::OutputGroupVolumeStateV1;
using hibiki::WindowsVolumeCallback;
using hibiki::WindowsVolumeBroker;
using hibiki::WindowsVolumeNotificationSnapshotV1;

constexpr GUID kEventContext{0x12345678, 0x9abc, 0x4def, {0x81, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef}};
const IID kUnknownInterfaceId{0x5b2d4d31, 0x1a2b, 0x4c7d, {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77}};

AUDIO_VOLUME_NOTIFICATION_DATA* make_notification(void* buffer,
                                                  std::size_t buffer_bytes,
                                                  float master_scalar,
                                                  bool muted,
                                                  std::uint32_t channels) {
    if (buffer_bytes < sizeof(AUDIO_VOLUME_NOTIFICATION_DATA) +
                           sizeof(float) * (channels > 0 ? channels - 1 : 0)) {
        return nullptr;
    }
    auto* data = static_cast<AUDIO_VOLUME_NOTIFICATION_DATA*>(buffer);
    data->guidEventContext = kEventContext;
    data->bMuted = muted ? TRUE : FALSE;
    data->fMasterVolume = master_scalar;
    data->nChannels = channels;
    for (std::uint32_t index = 0; index < channels; ++index) {
        data->afChannelVolumes[index] = 0.25F + static_cast<float>(index) * 0.01F;
    }
    return data;
}

class FakeEndpointVolume final : public IAudioEndpointVolume {
public:
    explicit FakeEndpointVolume(float master_db = -12.0F, BOOL muted = FALSE) noexcept
        : master_db_(master_db), muted_(muted) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == __uuidof(IAudioEndpointVolume)) {
            *object = static_cast<IAudioEndpointVolume*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }

    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --references_;
        if (remaining == 0U) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE RegisterControlChangeNotify(
        IAudioEndpointVolumeCallback* callback) override {
        callback_ = callback;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE UnregisterControlChangeNotify(
        IAudioEndpointVolumeCallback* callback) override {
        if (callback_ == callback) callback_ = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetChannelCount(UINT* channel_count) override {
        if (channel_count == nullptr) return E_POINTER;
        *channel_count = 2U;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetMasterVolumeLevel(float level, LPCGUID) override {
        ++master_set_calls_;
        if (master_set_calls_ == fail_master_on_call_) return E_FAIL;
        master_db_ = level;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetMasterVolumeLevelScalar(float, LPCGUID) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetMasterVolumeLevel(float* level) override {
        if (level == nullptr) return E_POINTER;
        ++master_get_calls_;
        if (master_get_calls_ == fail_master_get_on_call_) return E_FAIL;
        *level = master_db_;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetMasterVolumeLevelScalar(float* level) override {
        if (level == nullptr) return E_POINTER;
        *level = 0.5F;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetChannelVolumeLevel(UINT, float, LPCGUID) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE SetChannelVolumeLevelScalar(UINT, float, LPCGUID) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetChannelVolumeLevel(UINT, float* level) override {
        if (level == nullptr) return E_POINTER;
        *level = master_db_;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetChannelVolumeLevelScalar(UINT, float* level) override {
        if (level == nullptr) return E_POINTER;
        *level = 0.5F;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetMute(BOOL muted, LPCGUID) override {
        ++mute_set_calls_;
        if (mute_set_calls_ == fail_mute_on_call_) return E_FAIL;
        muted_ = muted;
        return mute_result_;
    }

    HRESULT STDMETHODCALLTYPE GetMute(BOOL* muted) override {
        if (muted == nullptr) return E_POINTER;
        ++mute_get_calls_;
        if (mute_get_calls_ == fail_mute_get_on_call_) return E_FAIL;
        *muted = muted_;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetVolumeStepInfo(UINT* step, UINT* step_count) override {
        if (step == nullptr || step_count == nullptr) return E_POINTER;
        *step = 0U;
        *step_count = 100U;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE VolumeStepUp(LPCGUID) override { return E_NOTIMPL; }

    HRESULT STDMETHODCALLTYPE VolumeStepDown(LPCGUID) override { return E_NOTIMPL; }

    HRESULT STDMETHODCALLTYPE QueryHardwareSupport(DWORD* support_mask) override {
        if (support_mask == nullptr) return E_POINTER;
        *support_mask = 0U;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetVolumeRange(float* minimum_db,
                                              float* maximum_db,
                                              float* increment_db) override {
        if (minimum_db == nullptr || maximum_db == nullptr || increment_db == nullptr) {
            return E_POINTER;
        }
        *minimum_db = -144.0F;
        *maximum_db = 12.0F;
        *increment_db = 0.5F;
        return S_OK;
    }

    HRESULT notify(float master_scalar, BOOL muted = FALSE) noexcept {
        struct NotificationWithEightChannels {
            GUID guidEventContext;
            BOOL bMuted;
            float fMasterVolume;
            UINT nChannels;
            float afChannelVolumes[8];
        } notification{};
        notification.guidEventContext = kEventContext;
        notification.bMuted = muted;
        notification.fMasterVolume = master_scalar;
        notification.nChannels = 2U;
        notification.afChannelVolumes[0] = 0.5F;
        notification.afChannelVolumes[1] = 0.25F;
        return callback_ == nullptr
                   ? E_UNEXPECTED
                   : callback_->OnNotify(
                         reinterpret_cast<AUDIO_VOLUME_NOTIFICATION_DATA*>(&notification));
    }

    float master_db_;
    BOOL muted_;
    std::uint32_t master_set_calls_{0U};
    std::uint32_t mute_set_calls_{0U};
    std::uint32_t master_get_calls_{0U};
    std::uint32_t mute_get_calls_{0U};
    std::uint32_t fail_master_on_call_{0U};
    std::uint32_t fail_mute_on_call_{0U};
    std::uint32_t fail_master_get_on_call_{0U};
    std::uint32_t fail_mute_get_on_call_{0U};
    HRESULT mute_result_{S_OK};

private:
    IAudioEndpointVolumeCallback* callback_{nullptr};
    ULONG references_{1U};
};

class FakeDevice final : public IMMDevice {
public:
    explicit FakeDevice(bool provide_id = true,
                        IAudioEndpointVolume* endpoint = nullptr) noexcept
        : provide_id_(provide_id), endpoint_(endpoint) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        *object = nullptr;
        if (iid == IID_IUnknown || iid == __uuidof(IMMDevice)) {
            *object = static_cast<IMMDevice*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }

    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --references_;
        if (remaining == 0U) {
            delete this;
        }
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE GetId(LPWSTR* identifier) override {
        if (!provide_id_ || identifier == nullptr) {
            return E_FAIL;
        }
        const wchar_t* text = L"{0.0.0.00000000}\\fake-endpoint";
        const auto bytes = (std::wcslen(text) + 1U) * sizeof(wchar_t);
        *identifier = static_cast<LPWSTR>(CoTaskMemAlloc(bytes));
        if (*identifier == nullptr) {
            return E_OUTOFMEMORY;
        }
        if (wcscpy_s(*identifier, bytes / sizeof(wchar_t), text) != 0) {
            CoTaskMemFree(*identifier);
            *identifier = nullptr;
            return E_FAIL;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetState(DWORD* state) override {
        if (state == nullptr) {
            return E_POINTER;
        }
        *state = DEVICE_STATE_ACTIVE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OpenPropertyStore(DWORD /*access*/, IPropertyStore** store) override {
        if (store != nullptr) {
            *store = nullptr;
        }
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE Activate(REFIID interface_id,
                                      DWORD /*class_context*/,
                                      PROPVARIANT* /*activation_parameters*/,
                                      void** interface_pointer) override {
        if (interface_pointer == nullptr) return E_POINTER;
        *interface_pointer = nullptr;
        if (endpoint_ != nullptr && interface_id == __uuidof(IAudioEndpointVolume)) {
            endpoint_->AddRef();
            *interface_pointer = endpoint_;
            return S_OK;
        }
        return E_NOINTERFACE;
    }

private:
    bool provide_id_;
    IAudioEndpointVolume* endpoint_;
    ULONG references_{1U};
};

int run_callback_tests() {
    // Fresh callback: read succeeds with the zero-sequence baseline snapshot.
    {
        auto* callback = new WindowsVolumeCallback();
        WindowsVolumeNotificationSnapshotV1 snapshot;
        CHECK(callback->read(snapshot));
        CHECK(snapshot.sequence == 0U);
        CHECK(snapshot.generation == 0U);
        CHECK(snapshot.mute == false);
        CHECK(snapshot.channel_count == 0U);
        CHECK(callback->Release() == 0U);
    }
    // OnNotify rejects a null notification payload.
    {
        auto* callback = new WindowsVolumeCallback();
        CHECK(callback->OnNotify(nullptr) == E_INVALIDARG);
        WindowsVolumeNotificationSnapshotV1 snapshot;
        CHECK(callback->read(snapshot));
        CHECK(snapshot.sequence == 0U);
        CHECK(callback->Release() == 0U);
    }
    // The callback rejects values outside the documented normalized scalar
    // range instead of allowing them to cross a unit boundary.
    {
        auto* callback = new WindowsVolumeCallback();
        std::array<unsigned char, sizeof(AUDIO_VOLUME_NOTIFICATION_DATA) + sizeof(float) * 16>
            buffer{};
        auto* notification =
            make_notification(buffer.data(), buffer.size(), -0.1F, false, 1U);
        CHECK(notification != nullptr);
        CHECK(callback->OnNotify(notification) == E_INVALIDARG);
        WindowsVolumeNotificationSnapshotV1 snapshot;
        CHECK(callback->read(snapshot));
        CHECK(snapshot.sequence == 0U && !snapshot.master_scalar_valid);
        CHECK(callback->Release() == 0U);
    }
    // A valid notification copies master scalar, mute, channels, and context bytes.
    {
        auto* callback = new WindowsVolumeCallback();
        std::array<unsigned char, sizeof(AUDIO_VOLUME_NOTIFICATION_DATA) + sizeof(float) * 16>
            buffer{};
        auto* notification =
            make_notification(buffer.data(), buffer.size(), 0.5F, true, 2U);
        CHECK(notification != nullptr);
        CHECK(callback->OnNotify(notification) == S_OK);
        WindowsVolumeNotificationSnapshotV1 snapshot;
        CHECK(callback->read(snapshot));
        CHECK(snapshot.sequence == 2U);
        CHECK(snapshot.master_scalar == 0.5F);
        CHECK(snapshot.master_scalar_valid);
        CHECK(snapshot.requested_db == -144.0);
        CHECK(snapshot.mute);
        CHECK(snapshot.channel_count == 2U);
        CHECK(snapshot.channel_scalars[0] == 0.25F);
        CHECK(snapshot.channel_scalars[1] == 0.26F);
        const auto* context = reinterpret_cast<const unsigned char*>(&snapshot.event_context);
        const auto* expected = reinterpret_cast<const unsigned char*>(&kEventContext);
        CHECK(std::memcmp(context, expected, sizeof(GUID)) == 0);
        CHECK(callback->Release() == 0U);
    }
    // Channel count above the fixed capacity clamps to eight entries.
    {
        auto* callback = new WindowsVolumeCallback();
        std::array<unsigned char, sizeof(AUDIO_VOLUME_NOTIFICATION_DATA) + sizeof(float) * 16>
            buffer{};
        auto* notification =
            make_notification(buffer.data(), buffer.size(), 1.0F, false, 12U);
        CHECK(notification != nullptr);
        CHECK(callback->OnNotify(notification) == S_OK);
        WindowsVolumeNotificationSnapshotV1 snapshot;
        CHECK(callback->read(snapshot));
        CHECK(snapshot.sequence == 2U);
        CHECK(snapshot.channel_count == 8U);
        CHECK(snapshot.channel_scalars[7] == 0.32F);
        CHECK(callback->Release() == 0U);
    }
    // Repeated notifications advance the even sequence and refresh values.
    {
        auto* callback = new WindowsVolumeCallback();
        std::array<unsigned char, sizeof(AUDIO_VOLUME_NOTIFICATION_DATA) + sizeof(float) * 16>
            first_buffer{};
        std::array<unsigned char, sizeof(AUDIO_VOLUME_NOTIFICATION_DATA) + sizeof(float) * 16>
            second_buffer{};
        auto* first = make_notification(first_buffer.data(), first_buffer.size(), 0.25F, false, 1U);
        auto* second = make_notification(second_buffer.data(), second_buffer.size(), 1.0F, true, 1U);
        CHECK(callback->OnNotify(first) == S_OK);
        CHECK(callback->OnNotify(second) == S_OK);
        WindowsVolumeNotificationSnapshotV1 snapshot;
        CHECK(callback->read(snapshot));
        CHECK(snapshot.sequence == 4U);
        CHECK(snapshot.master_scalar == 1.0F);
        CHECK(snapshot.master_scalar_valid);
        CHECK(snapshot.requested_db == -144.0);
        CHECK(snapshot.mute);
        CHECK(callback->Release() == 0U);
    }
    // QueryInterface honours null out-pointers, known ids, and rejection.
    {
        auto* callback = new WindowsVolumeCallback();
        void* object = nullptr;
        CHECK(callback->QueryInterface(kUnknownInterfaceId, &object) == E_NOINTERFACE);
        CHECK(object == nullptr);
        CHECK(callback->QueryInterface(IID_IUnknown, nullptr) == E_POINTER);
        IUnknown* unknown = nullptr;
        CHECK(callback->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&unknown)) == S_OK);
        CHECK(unknown != nullptr);
        CHECK(unknown->Release() == 1U);
        IAudioEndpointVolumeCallback* typed = nullptr;
        CHECK(callback->QueryInterface(__uuidof(IAudioEndpointVolumeCallback),
                                       reinterpret_cast<void**>(&typed)) == S_OK);
        CHECK(typed == static_cast<IAudioEndpointVolumeCallback*>(callback));
        CHECK(typed->Release() == 1U);
        CHECK(callback->AddRef() == 2U);
        CHECK(callback->Release() == 1U);
        CHECK(callback->Release() == 0U);
    }
    return 0;
}

int run_broker_tests() {
    OutputGroupVolumeStateV1 state;
    WindowsVolumeNotificationSnapshotV1 snapshot;

    // An unbound broker fails closed for every device-backed operation.
    {
        WindowsVolumeBroker broker;
        CHECK(!broker.is_bound());
        CHECK(broker.bind(nullptr) == E_INVALIDARG);
        CHECK(broker.bind_if_changed(nullptr) == E_INVALIDARG);
        CHECK(!broker.poll(snapshot));
        CHECK(broker.write(state, kEventContext) == AUDCLNT_E_DEVICE_INVALIDATED);
        CHECK(broker.read_state(state) == AUDCLNT_E_DEVICE_INVALIDATED);
    }
    // Activation failure propagates and keeps the broker unbound.
    {
        WindowsVolumeBroker broker;
        auto* device = new FakeDevice(true);
        CHECK(broker.bind(device) == E_NOINTERFACE);
        CHECK(!broker.is_bound());
        CHECK(!broker.poll(snapshot));
        CHECK(broker.write(state, kEventContext) == AUDCLNT_E_DEVICE_INVALIDATED);
        CHECK(broker.read_state(state) == AUDCLNT_E_DEVICE_INVALIDATED);
        CHECK(broker.bind_if_changed(device) == E_NOINTERFACE);
        device->Release();
    }
    // A missing endpoint identity still reaches the activation attempt and fails closed.
    {
        WindowsVolumeBroker broker;
        auto* device = new FakeDevice(false);
        CHECK(broker.bind(device) == E_NOINTERFACE);
        CHECK(!broker.is_bound());
        CHECK(broker.bind_if_changed(device) == E_NOINTERFACE);
        device->Release();
    }
    // Broker polling resolves the callback scalar through the endpoint dB
    // readback, preserves the notification metadata, and retries a failed
    // read without consuming the callback sequence.
    {
        auto* endpoint = new FakeEndpointVolume(-37.0F, FALSE);
        auto* device = new FakeDevice(true, endpoint);
        WindowsVolumeBroker broker;
        CHECK(broker.bind(device) == S_OK);
        CHECK(endpoint->notify(0.0F, FALSE) == S_OK);
        CHECK(broker.poll(snapshot));
        CHECK(snapshot.master_scalar == 0.0F && snapshot.master_scalar_valid);
        CHECK(snapshot.requested_db == -37.0);
        CHECK(snapshot.sequence == 2U && snapshot.generation == 1U);
        CHECK(endpoint->master_get_calls_ == 1U);

        endpoint->master_db_ = -12.0F;
        CHECK(endpoint->notify(0.5F, TRUE) == S_OK);
        CHECK(broker.poll(snapshot));
        CHECK(snapshot.master_scalar == 0.5F && snapshot.requested_db == -12.0 &&
              snapshot.mute && snapshot.sequence == 4U && snapshot.generation == 2U);
        CHECK(endpoint->master_get_calls_ == 2U);

        endpoint->master_db_ = -6.0F;
        endpoint->fail_master_get_on_call_ = 3U;
        CHECK(endpoint->notify(1.0F, FALSE) == S_OK);
        const auto previous_snapshot = snapshot;
        CHECK(!broker.poll(snapshot));
        CHECK(snapshot.sequence == previous_snapshot.sequence &&
              snapshot.generation == previous_snapshot.generation &&
              snapshot.requested_db == previous_snapshot.requested_db);
        endpoint->fail_master_get_on_call_ = 0U;
        CHECK(broker.poll(snapshot));
        CHECK(snapshot.master_scalar == 1.0F && snapshot.requested_db == -6.0 &&
              !snapshot.mute && snapshot.sequence == 6U && snapshot.generation == 3U);
        CHECK(endpoint->master_get_calls_ == 4U);
        broker.unbind();
        CHECK(endpoint->Release() == 0U);
        CHECK(device->Release() == 0U);
    }
    // A successful canonical dB/mute write applies both setters.
    {
        auto* endpoint = new FakeEndpointVolume(-12.0F, FALSE);
        auto* device = new FakeDevice(true, endpoint);
        WindowsVolumeBroker broker;
        CHECK(broker.bind(device) == S_OK);
        OutputGroupVolumeStateV1 requested{};
        requested.requested_db = -6.0;
        requested.safety_ceiling_db = 0.0;
        requested.mute = true;
        CHECK(broker.write(requested, kEventContext) == S_OK);
        CHECK(endpoint->master_db_ == -6.0F);
        CHECK(endpoint->muted_ == TRUE);
        CHECK(endpoint->master_set_calls_ == 1U);
        CHECK(endpoint->mute_set_calls_ == 1U);
        broker.unbind();
        CHECK(endpoint->Release() == 0U);
        CHECK(device->Release() == 0U);
    }
    // A successful second setter preserves its successful HRESULT.
    {
        auto* endpoint = new FakeEndpointVolume(-12.0F, FALSE);
        endpoint->mute_result_ = S_FALSE;
        auto* device = new FakeDevice(true, endpoint);
        WindowsVolumeBroker broker;
        CHECK(broker.bind(device) == S_OK);
        OutputGroupVolumeStateV1 requested{};
        requested.requested_db = -6.0;
        requested.safety_ceiling_db = 0.0;
        requested.mute = true;
        CHECK(broker.write(requested, kEventContext) == S_FALSE);
        CHECK(endpoint->master_db_ == -6.0F);
        CHECK(endpoint->muted_ == TRUE);
        broker.unbind();
        CHECK(endpoint->Release() == 0U);
        CHECK(device->Release() == 0U);
    }
    // A first-setter failure restores the complete pre-write pair and never
    // reports success.
    {
        auto* endpoint = new FakeEndpointVolume(-12.0F, FALSE);
        endpoint->fail_master_on_call_ = 1U;
        auto* device = new FakeDevice(true, endpoint);
        WindowsVolumeBroker broker;
        CHECK(broker.bind(device) == S_OK);
        OutputGroupVolumeStateV1 requested{};
        requested.requested_db = -3.0;
        requested.safety_ceiling_db = 0.0;
        requested.mute = true;
        CHECK(FAILED(broker.write(requested, kEventContext)));
        CHECK(endpoint->master_db_ == -12.0F);
        CHECK(endpoint->muted_ == FALSE);
        CHECK(endpoint->master_set_calls_ == 2U);
        CHECK(endpoint->mute_set_calls_ == 1U);
        broker.unbind();
        CHECK(endpoint->Release() == 0U);
        CHECK(device->Release() == 0U);
    }
    // A second-setter failure rolls back the first setter and the mute pair.
    {
        auto* endpoint = new FakeEndpointVolume(-12.0F, FALSE);
        endpoint->fail_mute_on_call_ = 1U;
        auto* device = new FakeDevice(true, endpoint);
        WindowsVolumeBroker broker;
        CHECK(broker.bind(device) == S_OK);
        OutputGroupVolumeStateV1 requested{};
        requested.requested_db = -3.0;
        requested.safety_ceiling_db = 0.0;
        requested.mute = true;
        CHECK(FAILED(broker.write(requested, kEventContext)));
        CHECK(endpoint->master_db_ == -12.0F);
        CHECK(endpoint->muted_ == FALSE);
        CHECK(endpoint->master_set_calls_ == 2U);
        CHECK(endpoint->mute_set_calls_ == 2U);
        broker.unbind();
        CHECK(endpoint->Release() == 0U);
        CHECK(device->Release() == 0U);
    }
    // Rollback setter failure is surfaced as fail-closed and cannot be
    // mistaken for a successful endpoint update.
    {
        auto* endpoint = new FakeEndpointVolume(-12.0F, FALSE);
        endpoint->fail_mute_on_call_ = 1U;
        endpoint->fail_master_on_call_ = 2U;
        auto* device = new FakeDevice(true, endpoint);
        WindowsVolumeBroker broker;
        CHECK(broker.bind(device) == S_OK);
        OutputGroupVolumeStateV1 requested{};
        requested.requested_db = -3.0;
        requested.safety_ceiling_db = 0.0;
        requested.mute = true;
        CHECK(FAILED(broker.write(requested, kEventContext)));
        CHECK(endpoint->master_db_ == -3.0F);
        CHECK(endpoint->muted_ == FALSE);
        broker.unbind();
        CHECK(endpoint->Release() == 0U);
        CHECK(device->Release() == 0U);
    }
    // Read-back failure after otherwise successful rollback remains
    // fail-closed.
    {
        auto* endpoint = new FakeEndpointVolume(-12.0F, FALSE);
        endpoint->fail_mute_on_call_ = 1U;
        endpoint->fail_master_get_on_call_ = 2U;
        auto* device = new FakeDevice(true, endpoint);
        WindowsVolumeBroker broker;
        CHECK(broker.bind(device) == S_OK);
        OutputGroupVolumeStateV1 requested{};
        requested.requested_db = -3.0;
        requested.safety_ceiling_db = 0.0;
        requested.mute = true;
        CHECK(FAILED(broker.write(requested, kEventContext)));
        CHECK(endpoint->master_db_ == -12.0F);
        CHECK(endpoint->muted_ == FALSE);
        broker.unbind();
        CHECK(endpoint->Release() == 0U);
        CHECK(device->Release() == 0U);
    }
    return 0;
}

}  // namespace

int main() {
    int result = run_callback_tests();
    if (result != 0) {
        return result;
    }
    result = run_broker_tests();
    if (result != 0) {
        return result;
    }
    std::fputs("windows volume callback tests passed\n", stdout);
    return 0;
}
