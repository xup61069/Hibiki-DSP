#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/windows_wasapi_handoff.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace hibiki {

struct WasapiFanoutSinkConfigV1 {
    bool enabled{false};
    WasapiOutputConfigV1 output{};
};

struct WasapiFanoutSnapshotV1 {
    std::uint32_t sink_count{0U};
    std::uint32_t enabled_count{0U};
    bool degraded{false};
    std::array<WasapiSinkHandoffSnapshotV1, 8U> sinks{};
};

// Control-plane owner for concurrent physical sinks. Every enabled sink has
// an independent dual-worker handoff; process() only copies one already mixed
// graph block into those bounded queues and never touches COM or allocates.
class WindowsWasapiFanoutV1 final {
public:
    static constexpr std::size_t kMaxSinks = 8U;

    WindowsWasapiFanoutV1() noexcept = default;
    ~WindowsWasapiFanoutV1();

    WindowsWasapiFanoutV1(const WindowsWasapiFanoutV1&) = delete;
    WindowsWasapiFanoutV1& operator=(const WindowsWasapiFanoutV1&) = delete;

    [[nodiscard]] bool prepare(std::span<const WasapiFanoutSinkConfigV1> configs,
                               std::uint32_t block_frames = 128U) noexcept;
    [[nodiscard]] bool begin_handoff(std::size_t sink_index,
                                     const WasapiOutputConfigV1& candidate,
                                     std::uint32_t block_frames = 128U,
                                     std::uint32_t fade_ms = 30U) noexcept;
    [[nodiscard]] bool prepare_handoff(std::size_t sink_index) noexcept;
    [[nodiscard]] bool commit_handoff(std::size_t sink_index) noexcept;
    void rollback_handoff(std::size_t sink_index) noexcept;
    [[nodiscard]] bool process(const float* interleaved,
                               std::uint32_t frames,
                               std::uint32_t channels) noexcept;
    void stop() noexcept;

    [[nodiscard]] WasapiFanoutSnapshotV1 snapshot() const noexcept;

private:
    std::array<WindowsWasapiSinkHandoffV1, kMaxSinks> sinks_{};
    std::array<bool, kMaxSinks> enabled_{};
    std::uint32_t sink_count_{0U};
    std::uint32_t enabled_count_{0U};
    bool degraded_{false};
};

}  // namespace hibiki
