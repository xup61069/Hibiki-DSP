#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#if defined(_WIN32)

#include <audioclient.h>
#include <cstdint>
#include <windows.h>

namespace hibiki {

enum class WindowsProcessLoopbackStateV1 : std::uint8_t {
    Unbound,
    Activating,
    Ready,
    Running,
    Stopped,
    Degraded,
};

struct WindowsProcessLoopbackConfigV1 {
    std::uint32_t process_id{0U};
    bool include_process_tree{true};
    std::uint32_t requested_sample_rate{0U};
    std::uint32_t requested_channels{0U};
};

struct WindowsProcessLoopbackSnapshotV1 {
    WindowsProcessLoopbackStateV1 state{WindowsProcessLoopbackStateV1::Unbound};
    std::uint32_t process_id{0U};
    std::uint32_t sample_rate{0U};
    std::uint32_t channels{0U};
    std::uint32_t frames_per_buffer{0U};
    std::uint64_t captured_frames{0U};
    std::uint64_t dropped_frames{0U};
    HRESULT last_error{S_OK};
};

// Official Windows process-tree loopback capture boundary. All COM setup and
// capture calls belong to the owning worker; the graph/RT thread must only
// consume caller-owned Float32 blocks after this source has produced them.
// This is process-level capture, not Chrome tabCapture and not a physical
// per-App routing API.
class WindowsProcessLoopbackSourceV1 final {
public:
    WindowsProcessLoopbackSourceV1() noexcept = default;
    ~WindowsProcessLoopbackSourceV1();

    WindowsProcessLoopbackSourceV1(const WindowsProcessLoopbackSourceV1&) = delete;
    WindowsProcessLoopbackSourceV1& operator=(const WindowsProcessLoopbackSourceV1&) = delete;

    [[nodiscard]] HRESULT start(const WindowsProcessLoopbackConfigV1& config);
    void stop() noexcept;
    // Non-blocking: reads at most one WASAPI capture packet. A zero-frame
    // success means that no packet is currently queued.
    [[nodiscard]] bool read(float* interleaved,
                            std::uint32_t capacity_frames,
                            std::uint32_t& frames_read) noexcept;
    [[nodiscard]] HANDLE event_handle() const noexcept { return event_handle_; }
    [[nodiscard]] WindowsProcessLoopbackSnapshotV1 snapshot() const noexcept;

private:
#if defined(HIBIKI_PROCESS_LOOPBACK_TEST_SEAM)
    friend struct WindowsProcessLoopbackSourceTestAccessV1;
#endif

    void set_degraded(HRESULT error) noexcept;

    IAudioClient* audio_client_{nullptr};
    IAudioCaptureClient* capture_client_{nullptr};
    HANDLE event_handle_{nullptr};
    WindowsProcessLoopbackConfigV1 config_{};
    WindowsProcessLoopbackStateV1 state_{WindowsProcessLoopbackStateV1::Unbound};
    std::uint32_t sample_rate_{0U};
    std::uint32_t channels_{0U};
    std::uint32_t frames_per_buffer_{0U};
    std::uint64_t captured_frames_{0U};
    std::uint64_t dropped_frames_{0U};
    HRESULT last_error_{S_OK};
};

}  // namespace hibiki

#endif  // defined(_WIN32)
