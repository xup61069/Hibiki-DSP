// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/windows_device_watcher.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::WindowsDeviceChangeKind;
using hibiki::WindowsDeviceChangeSnapshotV1;
using hibiki::WindowsDeviceWatcher;

std::wstring id_from(const wchar_t* text) {
    return std::wstring(text == nullptr ? L"" : text);
}

std::wstring snapshot_id(const WindowsDeviceChangeSnapshotV1& snapshot) {
    return std::wstring(snapshot.endpoint_id.data());
}

}  // namespace

int main() {
    // poll: no published event returns false and leaves the sequence at zero.
    {
        auto* watcher = new WindowsDeviceWatcher();
        WindowsDeviceChangeSnapshotV1 snapshot;
        CHECK(!watcher->poll(snapshot));
        CHECK(watcher->Release() == 0U);
    }
    // OnDeviceAdded: publishes Added kind with active state and copies the id.
    {
        auto* watcher = new WindowsDeviceWatcher();
        const std::wstring endpoint = L"{0.0.0.00000000}.\\device-add";
        CHECK(watcher->OnDeviceAdded(endpoint.c_str()) == S_OK);
        WindowsDeviceChangeSnapshotV1 snapshot;
        CHECK(watcher->poll(snapshot));
        const std::uint64_t sequence = snapshot.sequence;
        CHECK((sequence & 1U) == 0U);
        CHECK(sequence == 2U);
        CHECK(snapshot.kind == WindowsDeviceChangeKind::Added);
        CHECK(snapshot.flow == eAll);
        CHECK(snapshot.role == eConsole);
        CHECK(snapshot.state == DEVICE_STATE_ACTIVE);
        CHECK(id_from(snapshot.endpoint_id.data()) == endpoint);
        CHECK(watcher->Release() == 0U);
    }
    // OnDeviceRemoved: publishes Removed kind with not-present state.
    {
        auto* watcher = new WindowsDeviceWatcher();
        const std::wstring endpoint = L"{0.0.0.00000000}.\\device-remove";
        CHECK(watcher->OnDeviceRemoved(endpoint.c_str()) == S_OK);
        WindowsDeviceChangeSnapshotV1 snapshot;
        CHECK(watcher->poll(snapshot));
        CHECK(snapshot.kind == WindowsDeviceChangeKind::Removed);
        CHECK(snapshot.state == DEVICE_STATE_NOTPRESENT);
        CHECK(id_from(snapshot.endpoint_id.data()) == endpoint);
        CHECK(watcher->Release() == 0U);
    }
    // OnDefaultDeviceChanged: carries flow, role, and id through unchanged.
    {
        auto* watcher = new WindowsDeviceWatcher();
        const std::wstring endpoint = L"{0.0.0.00000000}.\\default-render";
        CHECK(watcher->OnDefaultDeviceChanged(eRender, eMultimedia, endpoint.c_str()) == S_OK);
        WindowsDeviceChangeSnapshotV1 snapshot;
        CHECK(watcher->poll(snapshot));
        CHECK(snapshot.kind == WindowsDeviceChangeKind::DefaultChanged);
        CHECK(snapshot.flow == eRender);
        CHECK(snapshot.role == eMultimedia);
        CHECK(id_from(snapshot.endpoint_id.data()) == endpoint);
        CHECK(watcher->Release() == 0U);
    }
    // OnPropertyValueChanged: publishes PropertyChanged kind with zero state.
    {
        auto* watcher = new WindowsDeviceWatcher();
        const std::wstring endpoint = L"{0.0.0.00000000}.\\property";
        PROPERTYKEY key{};
        CHECK(watcher->OnPropertyValueChanged(endpoint.c_str(), key) == S_OK);
        WindowsDeviceChangeSnapshotV1 snapshot;
        CHECK(watcher->poll(snapshot));
        CHECK(snapshot.kind == WindowsDeviceChangeKind::PropertyChanged);
        CHECK(snapshot.state == 0U);
        CHECK(id_from(snapshot.endpoint_id.data()) == endpoint);
        CHECK(watcher->Release() == 0U);
    }
    // OnDeviceStateChanged: carries the raw DWORD state value through.
    {
        auto* watcher = new WindowsDeviceWatcher();
        const std::wstring endpoint = L"{0.0.0.00000000}.\\state";
        constexpr DWORD kState = DEVICE_STATE_DISABLED;
        CHECK(watcher->OnDeviceStateChanged(endpoint.c_str(), kState) == S_OK);
        WindowsDeviceChangeSnapshotV1 snapshot;
        CHECK(watcher->poll(snapshot));
        CHECK(snapshot.kind == WindowsDeviceChangeKind::StateChanged);
        CHECK(snapshot.state == kState);
        CHECK(id_from(snapshot.endpoint_id.data()) == endpoint);
        CHECK(watcher->Release() == 0U);
    }
    // bounded copy: a long id is truncated to fit 259 characters + NUL.
    {
        auto* watcher = new WindowsDeviceWatcher();
        std::wstring long_endpoint(400, L'x');
        CHECK(watcher->OnDeviceAdded(long_endpoint.c_str()) == S_OK);
        WindowsDeviceChangeSnapshotV1 snapshot;
        CHECK(watcher->poll(snapshot));
        const auto truncated = id_from(snapshot.endpoint_id.data());
        CHECK(truncated.size() == 259U);
        CHECK(truncated == std::wstring(259, L'x'));
        CHECK(watcher->Release() == 0U);
    }
    // capacity boundary: an id of exactly 259 characters plus NUL fits unchanged.
    {
        auto* watcher = new WindowsDeviceWatcher();
        const std::wstring exact_endpoint(259, L'y');
        CHECK(watcher->OnDeviceAdded(exact_endpoint.c_str()) == S_OK);
        WindowsDeviceChangeSnapshotV1 snapshot;
        CHECK(watcher->poll(snapshot));
        CHECK(id_from(snapshot.endpoint_id.data()) == exact_endpoint);
        CHECK(watcher->Release() == 0U);
    }
    // null id: callbacks tolerate nullptr by storing an empty endpoint id.
    {
        auto* watcher = new WindowsDeviceWatcher();
        CHECK(watcher->OnDeviceRemoved(nullptr) == S_OK);
        WindowsDeviceChangeSnapshotV1 snapshot;
        CHECK(watcher->poll(snapshot));
        CHECK(snapshot.kind == WindowsDeviceChangeKind::Removed);
        CHECK(id_from(snapshot.endpoint_id.data()).empty());
        CHECK(watcher->Release() == 0U);
    }
    // stale poll: consuming a sequence makes the next poll on it return false.
    {
        auto* watcher = new WindowsDeviceWatcher();
        const std::wstring endpoint = L"{0.0.0.00000000}.\\stale";
        CHECK(watcher->OnDeviceAdded(endpoint.c_str()) == S_OK);
        WindowsDeviceChangeSnapshotV1 snapshot;
        CHECK(watcher->poll(snapshot));
        CHECK(!watcher->poll(snapshot));
        CHECK(watcher->Release() == 0U);
    }
    // sequence parity: every publish leaves an even sequence for stable reads.
    {
        auto* watcher = new WindowsDeviceWatcher();
        const std::wstring first = L"{a}";
        const std::wstring second = L"{b}";
        CHECK(watcher->OnDeviceAdded(first.c_str()) == S_OK);
        CHECK(watcher->OnDeviceRemoved(second.c_str()) == S_OK);
        WindowsDeviceChangeSnapshotV1 snapshot;
        CHECK(watcher->poll(snapshot));
        const std::uint64_t sequence = snapshot.sequence;
        CHECK((sequence & 1U) == 0U);
        CHECK(sequence == 4U);
        CHECK(id_from(snapshot.endpoint_id.data()) == second);
        // The second event must be visible without another publish.
        CHECK(!watcher->poll(snapshot));
        CHECK(watcher->Release() == 0U);
    }
    // QueryInterface: IUnknown and IMMNotificationClient succeed and AddRef.
    {
        WindowsDeviceWatcher* watcher_ptr = new WindowsDeviceWatcher();
        IMMNotificationClient* client = watcher_ptr;
        void* queried = nullptr;
        CHECK(client->QueryInterface(IID_IUnknown, &queried) == S_OK && queried != nullptr);
        static_cast<IUnknown*>(queried)->Release();
        CHECK(client->QueryInterface(__uuidof(IMMNotificationClient), &queried) == S_OK &&
              queried != nullptr);
        static_cast<IMMNotificationClient*>(queried)->Release();
        CHECK(client->QueryInterface(IID_IPersist, &queried) == E_NOINTERFACE &&
              queried == nullptr);
        CHECK(client->QueryInterface(IID_IUnknown, nullptr) == E_POINTER);
        // Balanced AddRef/Release keeps one reference until final release deletes it.
        CHECK(client->AddRef() >= 2U);
        CHECK(client->Release() >= 1U);
        CHECK(client->Release() == 0U);  // deletes the watcher
    }
    // register_with(nullptr): rejects the missing enumerator fail-closed.
    {
        auto* watcher = new WindowsDeviceWatcher();
        CHECK(watcher->register_with(nullptr) == E_INVALIDARG);
        WindowsDeviceChangeSnapshotV1 snapshot;
        CHECK(!watcher->poll(snapshot));
        CHECK(watcher->Release() == 0U);
    }
    // unregister before register is safe (destructor path with no enumerator).
    {
        auto* watcher = new WindowsDeviceWatcher();
        watcher->unregister();
        WindowsDeviceChangeSnapshotV1 snapshot;
        CHECK(!watcher->poll(snapshot));
        CHECK(watcher->Release() == 0U);
    }
    return 0;
}
