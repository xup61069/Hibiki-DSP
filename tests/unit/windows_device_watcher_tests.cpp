// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/windows_device_watcher.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

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

bool is_complete_concurrent_tuple(const WindowsDeviceChangeSnapshotV1& snapshot,
                                  const std::wstring& added_id,
                                  const std::wstring& default_id) {
    return (snapshot.kind == WindowsDeviceChangeKind::Added &&
            snapshot.flow == eAll && snapshot.role == eConsole &&
            snapshot.state == DEVICE_STATE_ACTIVE && snapshot_id(snapshot) == added_id) ||
           (snapshot.kind == WindowsDeviceChangeKind::DefaultChanged &&
            snapshot.flow == eCapture && snapshot.role == eCommunications &&
            snapshot.state == DEVICE_STATE_ACTIVE && snapshot_id(snapshot) == default_id);
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
    // concurrent callbacks: every accepted snapshot is one complete known tuple.
    {
        auto* watcher = new WindowsDeviceWatcher();
        const std::wstring added_id(259U, L'a');
        const std::wstring default_id(259U, L'd');
        std::atomic<bool> start{false};
        std::atomic<bool> callback_failed{false};
        std::atomic<std::uint32_t> completed{0U};
        constexpr std::uint32_t kPublishesPerWriter = 512U;
        std::thread added_writer([&] {
            while (!start.load(std::memory_order_acquire)) {}
            for (std::uint32_t index = 0U; index < kPublishesPerWriter; ++index) {
                if (watcher->OnDeviceAdded(added_id.c_str()) != S_OK) {
                    callback_failed.store(true, std::memory_order_relaxed);
                }
            }
            completed.fetch_add(1U, std::memory_order_release);
        });
        std::thread default_writer([&] {
            while (!start.load(std::memory_order_acquire)) {}
            for (std::uint32_t index = 0U; index < kPublishesPerWriter; ++index) {
                if (watcher->OnDefaultDeviceChanged(eCapture, eCommunications,
                                                    default_id.c_str()) != S_OK) {
                    callback_failed.store(true, std::memory_order_relaxed);
                }
            }
            completed.fetch_add(1U, std::memory_order_release);
        });
        start.store(true, std::memory_order_release);
        std::uint32_t accepted = 0U;
        WindowsDeviceChangeSnapshotV1 snapshot;
        while (completed.load(std::memory_order_acquire) != 2U) {
            if (watcher->poll(snapshot)) {
                CHECK(is_complete_concurrent_tuple(snapshot, added_id, default_id));
                ++accepted;
            }
        }
        added_writer.join();
        default_writer.join();
        CHECK(!callback_failed.load(std::memory_order_relaxed));
        for (std::uint32_t attempt = 0U; attempt < 4U; ++attempt) {
            if (watcher->poll(snapshot)) {
                CHECK(is_complete_concurrent_tuple(snapshot, added_id, default_id));
                ++accepted;
            }
        }
        CHECK(accepted > 0U);
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
