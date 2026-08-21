#include "hibiki/windows_volume_link.hpp"

#if defined(_WIN32)

#include <cmath>

namespace hibiki {

namespace {

constexpr GUID kUiContext{0x5b1fbad1U, 0x8e7cU, 0x4e8aU,
                          {0x91U, 0x0dU, 0x2aU, 0x65U, 0x4fU, 0x93U, 0x7cU, 0x11U}};
constexpr GUID kSafetyContext{0x7c3c2e54U, 0x1a5fU, 0x4c2aU,
                              {0xa8U, 0x34U, 0x5dU, 0x76U, 0x81U, 0x2bU, 0x4eU, 0x90U}};
constexpr GUID kSceneContext{0x8f3d9b66U, 0x2c11U, 0x4fd0U,
                             {0xb7U, 0x42U, 0x1eU, 0x63U, 0x55U, 0x9aU, 0xc4U, 0x28U}};
constexpr GUID kSessionContext{0xa4e2c779U, 0x3d88U, 0x421bU,
                               {0x9cU, 0x0aU, 0x72U, 0x4dU, 0x18U, 0xefU, 0x6bU, 0x35U}};

}  // namespace

const GUID& WindowsVolumeEventContextsV1::ui() noexcept { return kUiContext; }
const GUID& WindowsVolumeEventContextsV1::safety() noexcept { return kSafetyContext; }
const GUID& WindowsVolumeEventContextsV1::scene() noexcept { return kSceneContext; }
const GUID& WindowsVolumeEventContextsV1::session() noexcept { return kSessionContext; }

WindowsVolumeLinkV1::WindowsVolumeLinkV1() noexcept {
    (void)add_ignored_context(WindowsVolumeEventContextsV1::ui());
    (void)add_ignored_context(WindowsVolumeEventContextsV1::safety());
    (void)add_ignored_context(WindowsVolumeEventContextsV1::scene());
    (void)add_ignored_context(WindowsVolumeEventContextsV1::session());
}

bool WindowsVolumeLinkV1::add_ignored_context(const GUID& context) noexcept {
    if (is_ignored(context)) return true;
    if (ignored_context_count_ >= ignored_contexts_.size()) return false;
    ignored_contexts_[ignored_context_count_] = context;
    ++ignored_context_count_;
    return true;
}

void WindowsVolumeLinkV1::clear_ignored_contexts() noexcept {
    ignored_contexts_.fill(GUID{});
    ignored_context_count_ = 0U;
}

bool WindowsVolumeLinkV1::is_ignored(const GUID& context) const noexcept {
    for (std::size_t index = 0U; index < ignored_context_count_; ++index) {
        if (IsEqualGUID(ignored_contexts_[index], context)) return true;
    }
    return false;
}

WindowsVolumeSyncResultV1 WindowsVolumeLinkV1::apply(
    AudioEngineModel& engine,
    const std::string_view output_group,
    const WindowsVolumeNotificationSnapshotV1& snapshot) const noexcept {
    if (snapshot.generation == 0U || !std::isfinite(snapshot.requested_db) ||
        snapshot.requested_db < -144.0 || snapshot.requested_db > 12.0) {
        return WindowsVolumeSyncResultV1::Invalid;
    }
    if (is_ignored(snapshot.event_context)) {
        return WindowsVolumeSyncResultV1::IgnoredSelf;
    }
    const VolumeNotificationV1 notification{
        snapshot.requested_db, snapshot.mute, snapshot.generation};
    switch (engine.apply_windows_volume(output_group, notification)) {
        case VolumeNotificationResult::Accepted:
            return WindowsVolumeSyncResultV1::Applied;
        case VolumeNotificationResult::StaleGeneration:
            return WindowsVolumeSyncResultV1::StaleGeneration;
        case VolumeNotificationResult::Invalid:
            return WindowsVolumeSyncResultV1::Invalid;
    }
    return WindowsVolumeSyncResultV1::Invalid;
}

WindowsVolumeSyncResultV1 WindowsVolumeLinkV1::poll(
    WindowsControlRuntimeV1& runtime,
    AudioEngineModel& engine,
    const std::string_view output_group) const noexcept {
    WindowsVolumeNotificationSnapshotV1 snapshot;
    if (!runtime.poll_volume(snapshot)) return WindowsVolumeSyncResultV1::NoUpdate;
    return apply(engine, output_group, snapshot);
}

}  // namespace hibiki

#endif  // defined(_WIN32)
