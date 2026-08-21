#include "hibiki/windows_volume_link.hpp"

#if defined(_WIN32)

#include <cmath>

namespace hibiki {

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
