#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/windows_wasapi_output.hpp"

#include <cstdint>

namespace hibiki {

enum class WasapiSinkHandoffStateV1 : std::uint8_t {
    Unbound,
    Preparing,
    Fading,
    ReadyToCommit,
    Synced,
    RolledBack,
    Degraded,
};

struct WasapiSinkHandoffSnapshotV1 {
    WasapiSinkHandoffStateV1 state{WasapiSinkHandoffStateV1::Unbound};
    std::uint8_t active_slot{0U};
    std::uint64_t fade_frames{0U};
    std::uint64_t fade_total_frames{0U};
    WasapiSinkWorkerSnapshotV1 primary{};
    WasapiSinkWorkerSnapshotV1 secondary{};
};

// Owns two sink workers so a candidate endpoint can warm up before the active
// endpoint is faded out. The graph caller submits each block once; this class
// applies equal-power gains at the bounded worker queue boundary and commits
// or rolls back without restarting the engine.
class WindowsWasapiSinkHandoffV1 final {
public:
    WindowsWasapiSinkHandoffV1() noexcept = default;
    ~WindowsWasapiSinkHandoffV1();

    WindowsWasapiSinkHandoffV1(const WindowsWasapiSinkHandoffV1&) = delete;
    WindowsWasapiSinkHandoffV1& operator=(const WindowsWasapiSinkHandoffV1&) = delete;

    [[nodiscard]] bool start_initial(const WasapiOutputConfigV1& config,
                                     std::uint32_t block_frames = 128U) noexcept;
    [[nodiscard]] bool begin(const WasapiOutputConfigV1& candidate,
                             std::uint32_t block_frames = 128U,
                             std::uint32_t fade_ms = 30U) noexcept;
    [[nodiscard]] bool prepare() noexcept;
    [[nodiscard]] bool process(const float* interleaved,
                               std::uint32_t frames,
                               std::uint32_t channels) noexcept;
    [[nodiscard]] bool commit() noexcept;
    void rollback() noexcept;
    void stop() noexcept;

    [[nodiscard]] WasapiSinkHandoffSnapshotV1 snapshot() const noexcept;
    [[nodiscard]] WasapiSinkHandoffStateV1 state() const noexcept { return state_; }

private:
    [[nodiscard]] WindowsWasapiSinkWorkerV1& active_worker() noexcept;
    [[nodiscard]] WindowsWasapiSinkWorkerV1& candidate_worker() noexcept;
    [[nodiscard]] const WindowsWasapiSinkWorkerV1& active_worker() const noexcept;
    [[nodiscard]] const WindowsWasapiSinkWorkerV1& candidate_worker() const noexcept;

    WindowsWasapiSinkWorkerV1 primary_{};
    WindowsWasapiSinkWorkerV1 secondary_{};
    std::uint8_t active_slot_{0U};
    std::uint64_t fade_frames_{0U};
    std::uint64_t fade_total_frames_{0U};
    std::uint32_t channels_{0U};
    WasapiSinkHandoffStateV1 state_{WasapiSinkHandoffStateV1::Unbound};
};

}  // namespace hibiki
