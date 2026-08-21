#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#if defined(_WIN32)

#include "hibiki/audio_engine.hpp"
#include "hibiki/windows_device_catalog.hpp"

#include <array>
#include <cstddef>
#include <guiddef.h>
#include <string_view>

namespace hibiki {

struct WindowsVolumeEventContextsV1 final {
    [[nodiscard]] static const GUID& ui() noexcept;
    [[nodiscard]] static const GUID& safety() noexcept;
    [[nodiscard]] static const GUID& scene() noexcept;
    [[nodiscard]] static const GUID& session() noexcept;
};

enum class WindowsVolumeSyncResultV1 : std::uint8_t {
    NoUpdate,
    IgnoredSelf,
    Applied,
    StaleGeneration,
    Invalid,
};

// Control-thread adapter from IAudioEndpointVolume notifications to the
// engine's canonical output-group volume bank. It is deliberately separate
// from the COM broker: callers can register the GUIDs used by Hibiki UI,
// Scene and Safety writes, then poll this adapter from the same control worker
// that drains EngineControlWorkerV1. It never runs from an audio callback.
class WindowsVolumeLinkV1 final {
public:
    static constexpr std::size_t kMaxIgnoredContexts = 8U;

    WindowsVolumeLinkV1() noexcept;

    [[nodiscard]] bool add_ignored_context(const GUID& context) noexcept;
    void clear_ignored_contexts() noexcept;

    [[nodiscard]] WindowsVolumeSyncResultV1 apply(
        AudioEngineModel& engine,
        std::string_view output_group,
        const WindowsVolumeNotificationSnapshotV1& snapshot) const noexcept;

    [[nodiscard]] WindowsVolumeSyncResultV1 poll(
        WindowsControlRuntimeV1& runtime,
        AudioEngineModel& engine,
        std::string_view output_group) const noexcept;

private:
    [[nodiscard]] bool is_ignored(const GUID& context) const noexcept;

    std::array<GUID, kMaxIgnoredContexts> ignored_contexts_{};
    std::size_t ignored_context_count_{0U};
};

}  // namespace hibiki

#endif  // defined(_WIN32)
