// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/windows_device_catalog.hpp"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <propidl.h>
#include <propsys.h>

#include <algorithm>
#include <cwchar>
#include <exception>
#include <new>
#include <string>
#include <utility>

namespace hibiki {
namespace {

std::string utf8_from_wide(const wchar_t* value) {
    if (value == nullptr || *value == L'\0') return {};
    const int source_length = static_cast<int>(wcslen(value));
    const int output_length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value,
                                                   source_length, nullptr, 0, nullptr, nullptr);
    if (output_length <= 0) return {};
    std::string result(static_cast<std::size_t>(output_length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, source_length,
                            result.data(), output_length, nullptr, nullptr) != output_length) {
        return {};
    }
    return result;
}

std::string take_task_string(LPWSTR value) {
    std::string result = utf8_from_wide(value);
    if (value != nullptr) CoTaskMemFree(value);
    return result;
}

PhysicalDeviceAvailabilityV1 availability_from_state(const DWORD state) noexcept {
    switch (state) {
        case DEVICE_STATE_ACTIVE: return PhysicalDeviceAvailabilityV1::Active;
        case DEVICE_STATE_DISABLED: return PhysicalDeviceAvailabilityV1::Disabled;
        case DEVICE_STATE_NOTPRESENT:
        case DEVICE_STATE_UNPLUGGED: return PhysicalDeviceAvailabilityV1::Unplugged;
        default: return PhysicalDeviceAvailabilityV1::Unknown;
    }
}

bool supported_channels(const std::uint32_t channels) noexcept {
    return channels == 1U || channels == 2U || channels == 6U || channels == 8U;
}

bool supported_rate(const std::uint32_t rate) noexcept {
    return rate == 44100U || rate == 48000U || rate == 96000U || rate == 192000U;
}

std::uint32_t bounded_frames(const std::uint32_t sample_rate,
                             const REFERENCE_TIME period) noexcept {
    if (sample_rate == 0U || period <= 0) return 128U;
    const auto raw = (static_cast<std::uint64_t>(sample_rate) *
                      static_cast<std::uint64_t>(period) + 5000000ULL) /
                     10000000ULL;
    if (raw < 16ULL) return 16U;
    if (raw > 4096ULL) return 4096U;
    return static_cast<std::uint32_t>(raw);
}

bool read_friendly_name(IMMDevice* const device, std::string& name) {
    name.clear();
    IPropertyStore* store = nullptr;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &store)) || store == nullptr) return false;
    PROPVARIANT value;
    PropVariantInit(&value);
    const auto result = store->GetValue(PKEY_Device_FriendlyName, &value);
    if (SUCCEEDED(result)) {
        if (value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
            name = utf8_from_wide(value.pwszVal);
        } else if (value.vt == VT_BSTR && value.bstrVal != nullptr) {
            name = utf8_from_wide(value.bstrVal);
        }
    }
    PropVariantClear(&value);
    store->Release();
    return !name.empty();
}

bool read_audio_format(IMMDevice* const device,
                       std::uint32_t& channels,
                       std::uint32_t& sample_rate,
                       std::uint32_t& buffer_frames) {
    channels = 2U;
    sample_rate = 48000U;
    buffer_frames = 128U;
    IAudioClient* client = nullptr;
    WAVEFORMATEX* format = nullptr;
    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(&client))) || client == nullptr) {
        return false;
    }
    const auto format_result = client->GetMixFormat(&format);
    REFERENCE_TIME default_period = 0;
    REFERENCE_TIME minimum_period = 0;
    const auto period_result = client->GetDevicePeriod(&default_period, &minimum_period);
    if (SUCCEEDED(format_result) && format != nullptr) {
        channels = format->nChannels;
        sample_rate = format->nSamplesPerSec;
        buffer_frames = bounded_frames(sample_rate, default_period);
    }
    if (format != nullptr) CoTaskMemFree(format);
    client->Release();
    return SUCCEEDED(format_result) && supported_channels(channels) && supported_rate(sample_rate) &&
           SUCCEEDED(period_result);
}

std::string default_endpoint_id(IMMDeviceEnumerator* const enumerator,
                                const EDataFlow flow) {
    IMMDevice* device = nullptr;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device)) || device == nullptr) {
        return {};
    }
    LPWSTR id = nullptr;
    const auto result = device->GetId(&id);
    device->Release();
    return SUCCEEDED(result) ? take_task_string(id) : std::string{};
}

HRESULT enumerate_flow(IMMDeviceEnumerator* const enumerator,
                       const EDataFlow flow,
                       const std::uint64_t sequence,
                       PhysicalDeviceCatalogV1& candidate) {
    IMMDeviceCollection* collection = nullptr;
    const auto collection_result = enumerator->EnumAudioEndpoints(
        flow, DEVICE_STATEMASK_ALL, &collection);
    if (FAILED(collection_result) || collection == nullptr) return collection_result;

    const auto default_id = default_endpoint_id(enumerator, flow);
    UINT count = 0U;
    auto result = collection->GetCount(&count);
    if (FAILED(result)) {
        collection->Release();
        return result;
    }
    for (UINT index = 0U; index < count; ++index) {
        IMMDevice* device = nullptr;
        result = collection->Item(index, &device);
        if (FAILED(result) || device == nullptr) {
            collection->Release();
            return FAILED(result) ? result : E_FAIL;
        }
        DWORD state = DEVICE_STATE_NOTPRESENT;
        LPWSTR id = nullptr;
        result = device->GetState(&state);
        if (SUCCEEDED(result)) result = device->GetId(&id);
        std::string endpoint_id = SUCCEEDED(result) ? take_task_string(id) : std::string{};
        std::string display_name;
        if (SUCCEEDED(result)) (void)read_friendly_name(device, display_name);
        if (display_name.empty()) display_name = endpoint_id;
        std::uint32_t channels = 2U;
        std::uint32_t sample_rate = 48000U;
        std::uint32_t buffer_frames = 128U;
        const bool format_ok = SUCCEEDED(result) && read_audio_format(
            device, channels, sample_rate, buffer_frames);
        device->Release();
        // A single endpoint can disappear or expose a format outside Hibiki's
        // LPCM contract while the rest of the collection remains usable. Skip
        // that descriptor; a complete worker refresh must never publish an
        // invented format or discard healthy endpoints because of it.
        if (FAILED(result) || endpoint_id.empty() || display_name.empty() || !format_ok) {
            continue;
        }
        PhysicalDeviceDescriptorV1 descriptor;
        descriptor.endpoint_id = std::move(endpoint_id);
        descriptor.display_name = std::move(display_name);
        descriptor.flow = flow == eRender ? PhysicalDeviceFlowV1::Render
                                          : PhysicalDeviceFlowV1::Capture;
        descriptor.availability = availability_from_state(state);
        descriptor.channels = channels;
        descriptor.sample_rate = sample_rate;
        descriptor.buffer_frames = buffer_frames;
        descriptor.is_default = descriptor.availability == PhysicalDeviceAvailabilityV1::Active &&
                                descriptor.endpoint_id == default_id;
        descriptor.last_sequence = sequence;
        if (candidate.upsert(descriptor) == PhysicalDeviceCatalogResultV1::InvalidDescriptor) {
            collection->Release();
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    collection->Release();
    return S_OK;
}

}  // namespace

WindowsPhysicalDeviceCatalogWorker::~WindowsPhysicalDeviceCatalogWorker() { unbind(); }

HRESULT WindowsPhysicalDeviceCatalogWorker::bind(
    IMMDeviceEnumerator* const enumerator) noexcept {
    if (enumerator == nullptr) return E_INVALIDARG;
    unbind();
    enumerator_ = enumerator;
    enumerator_->AddRef();
    return S_OK;
}

void WindowsPhysicalDeviceCatalogWorker::unbind() noexcept {
    if (enumerator_ != nullptr) {
        enumerator_->Release();
        enumerator_ = nullptr;
    }
}

HRESULT WindowsPhysicalDeviceCatalogWorker::enumerate_candidate(
    PhysicalDeviceCatalogV1& candidate,
    const std::uint64_t sequence) const noexcept {
    if (enumerator_ == nullptr) return E_UNEXPECTED;
    try {
        auto result = enumerate_flow(enumerator_, eRender, sequence, candidate);
        if (FAILED(result)) return result;
        return enumerate_flow(enumerator_, eCapture, sequence, candidate);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

HRESULT WindowsPhysicalDeviceCatalogWorker::refresh(
    PhysicalDeviceCatalogV1& catalog,
    std::uint64_t& catalog_sequence) noexcept {
    PhysicalDeviceCatalogV1 candidate;
    auto next_sequence = catalog_sequence + 1U;
    if (next_sequence == 0U) next_sequence = 1U;
    const auto result = enumerate_candidate(candidate, next_sequence);
    if (FAILED(result)) return result;
    catalog.swap(candidate);
    catalog_sequence = next_sequence;
    return S_OK;
}

HRESULT WindowsPhysicalDeviceCatalogWorker::refresh_snapshot(
    PhysicalDeviceCatalogV1& catalog,
    std::uint64_t& catalog_sequence,
    std::array<std::uint8_t, kDeviceCatalogSnapshotPayloadBytesV1>& payload,
    std::size_t& payload_bytes) noexcept {
    payload.fill(0U);
    payload_bytes = 0U;
    PhysicalDeviceCatalogV1 candidate;
    auto next_sequence = catalog_sequence + 1U;
    if (next_sequence == 0U) next_sequence = 1U;
    const auto result = enumerate_candidate(candidate, next_sequence);
    if (FAILED(result)) return result;
    if (!publisher_.publish(candidate, next_sequence, payload, payload_bytes)) return E_FAIL;
    catalog.swap(candidate);
    catalog_sequence = next_sequence;
    return S_OK;
}

WindowsPhysicalDeviceCatalogCoordinator::~WindowsPhysicalDeviceCatalogCoordinator() { unbind(); }

HRESULT WindowsPhysicalDeviceCatalogCoordinator::bind(
    IMMDeviceEnumerator* const enumerator) noexcept {
    if (enumerator == nullptr) return E_INVALIDARG;
    unbind();
    auto* watcher = new (std::nothrow) WindowsDeviceWatcher();
    if (watcher == nullptr) return E_OUTOFMEMORY;
    auto result = worker_.bind(enumerator);
    if (FAILED(result)) {
        (void)watcher->Release();
        return result;
    }
    result = watcher->register_with(enumerator);
    if (FAILED(result)) {
        worker_.unbind();
        (void)watcher->Release();
        return result;
    }
    watcher_ = watcher;
    return S_OK;
}

void WindowsPhysicalDeviceCatalogCoordinator::unbind() noexcept {
    if (watcher_ != nullptr) {
        (void)watcher_->Release();
        watcher_ = nullptr;
    }
    worker_.unbind();
}

HRESULT WindowsPhysicalDeviceCatalogCoordinator::refresh_now(
    PhysicalDeviceCatalogV1& catalog,
    std::uint64_t& catalog_sequence,
    std::array<std::uint8_t, kDeviceCatalogSnapshotPayloadBytesV1>& payload,
    std::size_t& payload_bytes) noexcept {
    if (watcher_ == nullptr) return E_UNEXPECTED;
    return worker_.refresh_snapshot(catalog, catalog_sequence, payload, payload_bytes);
}

HRESULT WindowsPhysicalDeviceCatalogCoordinator::poll_and_refresh(
    PhysicalDeviceCatalogV1& catalog,
    std::uint64_t& catalog_sequence,
    std::array<std::uint8_t, kDeviceCatalogSnapshotPayloadBytesV1>& payload,
    std::size_t& payload_bytes) noexcept {
    if (watcher_ == nullptr) return E_UNEXPECTED;
    WindowsDeviceChangeSnapshotV1 change;
    if (!watcher_->poll(change)) return S_FALSE;
    return worker_.refresh_snapshot(catalog, catalog_sequence, payload, payload_bytes);
}

HRESULT WindowsPhysicalDeviceCatalogServiceV1::bind(
    IMMDeviceEnumerator* const enumerator) noexcept {
    return coordinator_.bind(enumerator);
}

void WindowsPhysicalDeviceCatalogServiceV1::unbind() noexcept {
    coordinator_.unbind();
}

HRESULT WindowsPhysicalDeviceCatalogServiceV1::refresh_now() noexcept {
    return refresh_impl(false);
}

HRESULT WindowsPhysicalDeviceCatalogServiceV1::poll_and_refresh() noexcept {
    return refresh_impl(true);
}

HRESULT WindowsPhysicalDeviceCatalogServiceV1::refresh_impl(const bool poll) noexcept {
    try {
        PhysicalDeviceCatalogV1 candidate = catalog_;
        auto next_sequence = catalog_sequence_;
        std::array<std::uint8_t, kDeviceCatalogSnapshotPayloadBytesV1> next_payload{};
        std::size_t next_payload_bytes = 0U;
        const auto result = poll
                                ? coordinator_.poll_and_refresh(candidate, next_sequence,
                                                                 next_payload, next_payload_bytes)
                                : coordinator_.refresh_now(candidate, next_sequence,
                                                            next_payload, next_payload_bytes);
        if (result == S_FALSE) return result;
        if (FAILED(result)) return result;
        if (!snapshot_store_.publish(
                std::span<const std::uint8_t>(next_payload.data(), next_payload_bytes),
                next_sequence)) {
            return E_FAIL;
        }
        catalog_.swap(candidate);
        catalog_sequence_ = next_sequence;
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (const std::exception&) {
        return E_FAIL;
    } catch (...) {
        return E_FAIL;
    }
}

}  // namespace hibiki

#endif  // defined(_WIN32)
