#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_worker_protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hibiki {

constexpr std::size_t kVst3TimelineMaxEventsV1 = 256U;
constexpr std::uint32_t kVst3TimelineMaxBlockFramesV1 = kVst3WorkerMaxFramesV1;

struct Vst3ParameterTimelineEventV1 {
    std::uint32_t parameter_id{0U};
    std::uint64_t sample_position{0U};
    double normalized_value{0.0};
};

struct Vst3ParameterTimelineSnapshotV1 {
    std::uint32_t schema_version{1U};
    std::uint32_t event_count{0U};
    std::array<Vst3ParameterTimelineEventV1, kVst3TimelineMaxEventsV1> events{};
};

[[nodiscard]] bool validate_vst3_parameter_timeline_v1(
    const Vst3ParameterTimelineSnapshotV1& snapshot) noexcept;

class Vst3ParameterTimelineV1 final {
public:
    [[nodiscard]] bool append(const Vst3ParameterTimelineEventV1& event) noexcept;
    [[nodiscard]] bool erase(std::size_t index) noexcept;
    void clear() noexcept;

    [[nodiscard]] const Vst3ParameterTimelineSnapshotV1& snapshot() const noexcept {
        return snapshot_;
    }

    // Collect events in [block_start, block_start + frames) and convert their
    // absolute positions to bounded worker sample offsets. The destination is
    // caller-owned and the result is deterministic by timeline order.
    [[nodiscard]] bool collect_block(
        std::uint64_t block_start,
        std::uint32_t frames,
        std::span<Vst3WorkerParameterPointV1> destination,
        std::size_t& count) const noexcept;

private:
    Vst3ParameterTimelineSnapshotV1 snapshot_{};
};

}  // namespace hibiki
