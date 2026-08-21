#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/control_payloads.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hibiki {

enum class SessionCommandKindV1 : std::uint8_t {
    Volume = 1U,
    Route = 2U,
};

// This is an in-process handoff record, not an IPC layout. The two payloads
// intentionally remain fixed-size so enqueue/dequeue never allocates and can
// be used by the control worker without entering the COM boundary.
struct SessionCommandWorkItemV1 {
    SessionCommandKindV1 kind{SessionCommandKindV1::Volume};
    std::uint8_t reserved[7U]{};
    SessionVolumeCommandV1 volume{};
    SessionRouteCommandV1 route{};
};

// Single-producer (EngineControl worker), single-consumer (Windows COM
// worker) queue. A full queue fails closed and increments dropped(); callers
// must ask the UI to retry after the next catalog/status refresh. No mutex,
// wait, heap allocation, or COM call is allowed on either side.
class SessionCommandQueueV1 final {
public:
    static constexpr std::size_t kCapacity = 64U;

    [[nodiscard]] bool try_push(const SessionCommandWorkItemV1& item) noexcept;
    [[nodiscard]] bool try_push_volume(const SessionVolumeCommandV1& command) noexcept;
    [[nodiscard]] bool try_push_route(const SessionRouteCommandV1& command) noexcept;
    [[nodiscard]] bool try_pop(SessionCommandWorkItemV1& item) noexcept;
    [[nodiscard]] std::uint64_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

    // Called only after the producer has stopped (runtime start/stop
    // lifecycle); it prevents commands from a prior host instance leaking
    // into a new device binding.
    void reset() noexcept;

private:
    std::array<SessionCommandWorkItemV1, kCapacity> slots_{};
    std::atomic<std::uint64_t> head_{0U};
    std::atomic<std::uint64_t> tail_{0U};
    std::atomic<std::uint64_t> dropped_{0U};
};

}  // namespace hibiki
