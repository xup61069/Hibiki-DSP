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
                                                  float master_db,
                                                  bool muted,
                                                  std::uint32_t channels) {
    if (buffer_bytes < sizeof(AUDIO_VOLUME_NOTIFICATION_DATA) +
                           sizeof(float) * (channels > 0 ? channels - 1 : 0)) {
        return nullptr;
    }
    auto* data = static_cast<AUDIO_VOLUME_NOTIFICATION_DATA*>(buffer);
    data->guidEventContext = kEventContext;
    data->bMuted = muted ? TRUE : FALSE;
    data->fMasterVolume = master_db;
    data->nChannels = channels;
    for (std::uint32_t index = 0; index < channels; ++index) {
        data->afChannelVolumes[index] = 0.25F + static_cast<float>(index) * 0.01F;
    }
    return data;
}

class FakeDevice final : public IMMDevice {
public:
    explicit FakeDevice(bool provide_id = true) noexcept : provide_id_(provide_id) {}

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

    HRESULT STDMETHODCALLTYPE Activate(REFIID /*interface_id*/,
                                      DWORD /*class_context*/,
                                      PROPVARIANT* /*activation_parameters*/,
                                      void** interface_pointer) override {
        if (interface_pointer != nullptr) {
            *interface_pointer = nullptr;
        }
        return E_NOINTERFACE;
    }

private:
    bool provide_id_;
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
    // A valid notification copies master level, mute, channels, and context bytes.
    {
        auto* callback = new WindowsVolumeCallback();
        std::array<unsigned char, sizeof(AUDIO_VOLUME_NOTIFICATION_DATA) + sizeof(float) * 16>
            buffer{};
        auto* notification =
            make_notification(buffer.data(), buffer.size(), -6.5F, true, 2U);
        CHECK(notification != nullptr);
        CHECK(callback->OnNotify(notification) == S_OK);
        WindowsVolumeNotificationSnapshotV1 snapshot;
        CHECK(callback->read(snapshot));
        CHECK(snapshot.sequence == 2U);
        CHECK(snapshot.requested_db == static_cast<double>(-6.5F));
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
            make_notification(buffer.data(), buffer.size(), -1.0F, false, 12U);
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
        auto* first = make_notification(first_buffer.data(), first_buffer.size(), -3.0F, false, 1U);
        auto* second = make_notification(second_buffer.data(), second_buffer.size(), -24.0F, true, 1U);
        CHECK(callback->OnNotify(first) == S_OK);
        CHECK(callback->OnNotify(second) == S_OK);
        WindowsVolumeNotificationSnapshotV1 snapshot;
        CHECK(callback->read(snapshot));
        CHECK(snapshot.sequence == 4U);
        CHECK(snapshot.requested_db == static_cast<double>(-24.0F));
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
