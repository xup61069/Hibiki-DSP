// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/windows_device_catalog.hpp"
#include "hibiki/windows_volume_link.hpp"

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
#include <cstdio>
#include <cstring>
#include <exception>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace hibiki {
namespace {

ControlRouteHealthEntryV1 make_status_route(const std::string_view id,
                                             const std::string_view name,
                                             const std::string_view detail,
                                             const ControlRouteHealthStateV1 state,
                                             const bool requires_action) noexcept {
    ControlRouteHealthEntryV1 route{};
    route.id_bytes = static_cast<std::uint8_t>(id.size());
    route.name_bytes = static_cast<std::uint16_t>(name.size());
    route.detail_bytes = static_cast<std::uint16_t>(detail.size());
    route.state = state;
    route.flags = requires_action ? 1U : 0U;
    std::copy(id.begin(), id.end(), route.id.begin());
    std::copy(name.begin(), name.end(), route.name.begin());
    std::copy(detail.begin(), detail.end(), route.detail.begin());
    return route;
}

ControlStatusSnapshotV1 make_initial_status(const std::uint64_t sequence) noexcept {
    ControlStatusSnapshotV1 snapshot{};
    snapshot.sequence = sequence;
    snapshot.route_count = 4U;
    snapshot.routes[0] = make_status_route(
        "windows-session", "Windows session", "Waiting for active session report",
        ControlRouteHealthStateV1::Pending, false);
    snapshot.routes[1] = make_status_route(
        "process-loopback", "Process loopback", "Requires supported process-tree capture",
        ControlRouteHealthStateV1::Pending, false);
    snapshot.routes[2] = make_status_route(
        "browser-tab", "Browser tab", "Requires a user-gesture MV3 extension",
        ControlRouteHealthStateV1::Pending, true);
    snapshot.routes[3] = make_status_route(
        "direct-path", "Vendor ASIO / WASAPI Exclusive", "Path bypasses Hibiki",
        ControlRouteHealthStateV1::Bypassed, false);
    return snapshot;
}

bool update_status_route(ControlStatusSnapshotV1& snapshot,
                         const std::string_view id,
                         const ControlRouteHealthStateV1 state,
                         const bool requires_action,
                         const char* const detail) noexcept {
    if (detail == nullptr) return false;
    for (std::size_t index = 0U; index < snapshot.route_count; ++index) {
        auto& route = snapshot.routes[index];
        if (std::string_view(route.id.data(), route.id_bytes) != id) continue;
        char bounded[kControlStatusSnapshotEntryBytesV1]{};
        const int written = std::snprintf(bounded, sizeof(bounded), "%s", detail);
        if (written < 0) return false;
        const auto bytes = static_cast<std::size_t>(written) < route.detail.size()
                               ? static_cast<std::size_t>(written)
                               : route.detail.size() - 1U;
        const auto expected_flags = static_cast<std::uint16_t>(requires_action ? 1U : 0U);
        const bool changed = route.state != state || route.flags != expected_flags ||
                             route.detail_bytes != bytes ||
                             std::memcmp(route.detail.data(), bounded, bytes) != 0;
        route.state = state;
        route.flags = expected_flags;
        route.detail.fill('\0');
        std::memcpy(route.detail.data(), bounded, bytes);
        route.detail_bytes = static_cast<std::uint16_t>(bytes);
        return changed;
    }
    return false;
}

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

bool WindowsControlRuntimeV1::start(
    IMMDeviceEnumerator* const enumerator,
    const IpcNamedPipeConfigV1& config) noexcept {
    stop();
    worker_thread_id_ = std::this_thread::get_id();
    if (FAILED(catalog_service_.bind(enumerator))) return false;
    const auto initial_sequence = status_store_.sequence() == UINT64_MAX
                                      ? UINT64_MAX
                                      : status_store_.sequence() + 1U;
    status_snapshot_ = make_initial_status(initial_sequence);
    SessionCatalogSnapshotV1 initial_session_catalog{};
    const auto initial_session_sequence = session_catalog_store_.sequence() == UINT64_MAX
                                              ? UINT64_MAX
                                              : session_catalog_store_.sequence() + 1U;
    initial_session_catalog.sequence = initial_session_sequence;
    if (!status_store_.publish(status_snapshot_) ||
        !session_catalog_store_.publish(initial_session_catalog) ||
        !host_.start_with_queue(config, catalog_service_.snapshot_store(), &status_store_,
                                &session_catalog_store_)) {
        catalog_service_.unbind();
        return false;
    }
    (void)refresh_default_volume(enumerator);
    OutputGroupVolumeStateV1 initial_volume{};
    (void)read_volume(initial_volume);
    return true;
}

void WindowsControlRuntimeV1::stop() noexcept {
    host_.stop();
    session_routes_.unbind();
    volume_broker_.unbind();
    catalog_service_.unbind();
    worker_thread_id_ = {};
}

HRESULT WindowsControlRuntimeV1::refresh_now() noexcept {
    if (!running()) return E_UNEXPECTED;
    const auto result = catalog_service_.refresh_now();
    if (SUCCEEDED(result)) {
        (void)session_routes_.refresh();
        (void)publish_session_route_status();
        (void)publish_session_catalog();
    }
    return result;
}

HRESULT WindowsControlRuntimeV1::poll_and_refresh() noexcept {
    if (!running()) return E_UNEXPECTED;
    const auto result = catalog_service_.poll_and_refresh();
    if (SUCCEEDED(result)) {
        (void)session_routes_.poll_and_refresh();
        (void)publish_session_route_status();
        (void)publish_session_catalog();
    }
    return result;
}

HRESULT WindowsControlRuntimeV1::refresh_default_volume(
    IMMDeviceEnumerator* const enumerator) noexcept {
    if (!running() || enumerator == nullptr) return E_INVALIDARG;
    IMMDevice* device = nullptr;
    const auto result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(result) || device == nullptr) return FAILED(result) ? result : E_FAIL;
    const auto bind_result = volume_broker_.bind(device);
    device->Release();
    // Volume and session control are independent Windows interfaces. Keep
    // route health truthful even when the endpoint volume node is unavailable.
    (void)refresh_default_session_routes(enumerator);
    return bind_result;
}

HRESULT WindowsControlRuntimeV1::refresh_default_volume_if_changed(
    IMMDeviceEnumerator* const enumerator) noexcept {
    if (!running() || enumerator == nullptr) return E_INVALIDARG;
    IMMDevice* device = nullptr;
    const auto result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(result) || device == nullptr) return FAILED(result) ? result : E_FAIL;
    const auto bind_result = volume_broker_.bind_if_changed(device);
    device->Release();
    // S_FALSE means the volume endpoint identity is unchanged. Retry the
    // route bind only when it was previously unbound/degraded, otherwise keep
    // the existing watcher registration and session metadata continuity.
    if (bind_result == S_OK || !session_routes_.bound()) {
        (void)refresh_default_session_routes(enumerator);
    }
    return bind_result;
}

HRESULT WindowsControlRuntimeV1::refresh_default_session_routes(
    IMMDeviceEnumerator* const enumerator) noexcept {
    if (!running() || enumerator == nullptr) return E_INVALIDARG;
    IMMDevice* device = nullptr;
    const auto result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(result) || device == nullptr) return FAILED(result) ? result : E_FAIL;
    const auto bind_result = session_routes_.bind(device);
    device->Release();
    if (FAILED(bind_result)) return bind_result;
    const auto refresh_result = session_routes_.refresh();
    if (refresh_result == WindowsAudioSessionRouteRefreshResultV1::Degraded) {
        (void)publish_session_route_status();
        return E_FAIL;
    }
    (void)publish_session_route_status();
    (void)publish_session_catalog();
    return S_OK;
}

HRESULT WindowsControlRuntimeV1::read_volume(OutputGroupVolumeStateV1& state) noexcept {
    if (!running()) return E_UNEXPECTED;
    const auto result = volume_broker_.read_state(state);
    if (SUCCEEDED(result)) (void)publish_status_volume(state);
    return result;
}

HRESULT WindowsControlRuntimeV1::write_volume(
    const OutputGroupVolumeStateV1& state,
    const GUID& event_context) noexcept {
    if (!running()) return E_UNEXPECTED;
    const auto result = volume_broker_.write(state, event_context);
    if (SUCCEEDED(result)) (void)publish_status_volume(state);
    return result;
}

bool WindowsControlRuntimeV1::poll_volume(
    WindowsVolumeNotificationSnapshotV1& snapshot) noexcept {
    if (!running() || !volume_broker_.poll(snapshot)) return false;
    auto state = status_snapshot_.volume;
    state.requested_db = snapshot.requested_db;
    state.effective_db = (std::min)(state.requested_db, state.safety_ceiling_db);
    state.mute = snapshot.mute;
    state.generation = snapshot.generation;
    state.origin = VolumeOrigin::Windows;
    (void)publish_status_volume(state);
    return true;
}

HRESULT WindowsControlRuntimeV1::write_session_volume(
    const std::string_view session_instance_id,
    const double requested_db,
    const bool mute,
    const GUID& event_context) noexcept {
    if (!running()) return E_UNEXPECTED;
    if (!on_worker_thread()) return RPC_E_WRONG_THREAD;
    return session_routes_.write_session_volume(session_instance_id, requested_db, mute,
                                                 event_context);
}

HRESULT WindowsControlRuntimeV1::read_session_volume(
    const std::string_view session_instance_id,
    double& requested_db,
    bool& mute) noexcept {
    if (!running()) return E_UNEXPECTED;
    if (!on_worker_thread()) return RPC_E_WRONG_THREAD;
    return session_routes_.read_session_volume(session_instance_id, requested_db, mute);
}

HRESULT WindowsControlRuntimeV1::write_session_volume_handle(
    const std::uint64_t handle,
    const std::uint64_t catalog_sequence,
    const double requested_db,
    const bool mute,
    const GUID& event_context) noexcept {
    if (!running()) return E_UNEXPECTED;
    if (!on_worker_thread()) return RPC_E_WRONG_THREAD;
    if (catalog_sequence == 0U || catalog_sequence != session_catalog_store_.sequence()) {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    return session_routes_.write_session_volume_handle(handle, requested_db, mute,
                                                        event_context);
}

HRESULT WindowsControlRuntimeV1::read_session_volume_handle(
    const std::uint64_t handle,
    const std::uint64_t catalog_sequence,
    double& requested_db,
    bool& mute) noexcept {
    if (!running()) return E_UNEXPECTED;
    if (!on_worker_thread()) return RPC_E_WRONG_THREAD;
    if (catalog_sequence == 0U || catalog_sequence != session_catalog_store_.sequence()) {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    return session_routes_.read_session_volume_handle(handle, requested_db, mute);
}

HRESULT WindowsControlRuntimeV1::bind_session_route_handle(
    const std::uint64_t handle,
    const std::uint64_t catalog_sequence,
    const std::string_view lane_id,
    const std::string_view output_group) noexcept {
    if (!running()) return E_UNEXPECTED;
    if (!on_worker_thread()) return RPC_E_WRONG_THREAD;
    if (catalog_sequence == 0U || catalog_sequence != session_catalog_store_.sequence()) {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    const auto result = session_routes_.bind_session_route_handle(handle, lane_id, output_group);
    if (SUCCEEDED(result)) {
        (void)publish_session_route_status();
        (void)publish_session_catalog();
    }
    return result;
}

bool WindowsControlRuntimeV1::publish_status_snapshot(
    const ControlStatusSnapshotV1& snapshot) noexcept {
    if (!status_store_.publish(snapshot)) return false;
    status_snapshot_ = snapshot;
    return true;
}

bool WindowsControlRuntimeV1::publish_status_volume(
    const OutputGroupVolumeStateV1& state) noexcept {
    auto candidate = status_snapshot_;
    if (candidate.sequence == UINT64_MAX) return false;
    candidate.sequence += 1U;
    candidate.volume = state;
    if (!status_store_.publish(candidate)) return false;
    status_snapshot_ = candidate;
    return true;
}

bool WindowsControlRuntimeV1::publish_session_route_status() noexcept {
    if (!status_store_.has_snapshot()) return false;
    const auto route_snapshot = session_routes_.snapshot();
    auto candidate = status_snapshot_;
    const auto session_state = route_snapshot.degraded
                                   ? ControlRouteHealthStateV1::Degraded
                                   : route_snapshot.has_graph
                                       ? ControlRouteHealthStateV1::Ready
                                       : route_snapshot.session_count > 0U
                                           ? ControlRouteHealthStateV1::Pending
                                           : ControlRouteHealthStateV1::Unavailable;
    const auto process_state = route_snapshot.degraded
                                    ? ControlRouteHealthStateV1::Degraded
                                    : route_snapshot.routed_count > 0U
                                        ? ControlRouteHealthStateV1::Pending
                                        : ControlRouteHealthStateV1::Unavailable;
    char session_detail[120U]{};
    char process_detail[120U]{};
    const int session_written = std::snprintf(
        session_detail, sizeof(session_detail),
        "sessions=%zu active=%zu routed=%zu; graph=%s; physical delivery unverified",
        route_snapshot.session_count, route_snapshot.active_count, route_snapshot.routed_count,
        route_snapshot.has_graph ? "ready" : "idle");
    const int process_written = std::snprintf(
        process_detail, sizeof(process_detail),
        "routed sessions=%zu; process-tree source remains worker-owned and unverified",
        route_snapshot.routed_count);
    if (session_written < 0 || process_written < 0) return false;
    const bool session_changed = update_status_route(
        candidate, "windows-session", session_state, false, session_detail);
    const bool process_changed = update_status_route(
        candidate, "process-loopback", process_state, false, process_detail);
    if (!session_changed && !process_changed) return true;
    if (candidate.sequence == UINT64_MAX) return false;
    ++candidate.sequence;
    if (!status_store_.publish(candidate)) return false;
    status_snapshot_ = candidate;
    return true;
}

bool WindowsControlRuntimeV1::publish_session_catalog() noexcept {
    if (!session_routes_.bound()) return false;
    const auto previous = session_catalog_store_.sequence();
    if (previous == UINT64_MAX) return false;
    SessionCatalogSnapshotV1 candidate{};
    if (!session_routes_.make_session_catalog_snapshot(previous + 1U, candidate)) return false;
    return session_catalog_store_.publish(candidate);
}

bool apply_session_volume_command_v1(const SessionVolumeCommandV1& request,
                                     void* const context) noexcept {
    if (context == nullptr) return false;
    auto* runtime = static_cast<WindowsControlRuntimeV1*>(context);
    const auto requested_db = static_cast<double>(request.requested_db_q16_16) / 65536.0;
    return SUCCEEDED(runtime->write_session_volume_handle(
        request.handle, request.catalog_sequence, requested_db, request.mute != 0U,
        WindowsVolumeEventContextsV1::session()));
}

bool apply_session_route_command_v1(const SessionRouteCommandV1& request,
                                    void* const context) noexcept {
    if (context == nullptr) return false;
    auto* runtime = static_cast<WindowsControlRuntimeV1*>(context);
    return SUCCEEDED(runtime->bind_session_route_handle(
        request.handle, request.catalog_sequence,
        std::string_view(request.lane.data(), request.lane_bytes),
        std::string_view(request.output_group.data(), request.output_group_bytes)));
}

}  // namespace hibiki

#endif  // defined(_WIN32)
