#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/exporters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hibiki {

constexpr std::size_t kMaxRealtimePeqFiltersV1 = 16U;

// Fixed-capacity RBJ peaking-EQ processor. Filter coefficients and state are
// owned by this object; process_interleaved never allocates or waits.
class PeqProcessorV1 final {
public:
    [[nodiscard]] bool prepare(std::span<const PeqFilterV1> filters,
                               std::uint32_t sample_rate,
                               std::uint32_t channels) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool process_interleaved(float* interleaved,
                                           std::size_t frames) const noexcept;
    [[nodiscard]] bool prepared() const noexcept { return prepared_; }
    [[nodiscard]] std::uint32_t sample_rate() const noexcept { return sample_rate_; }
    [[nodiscard]] std::uint32_t channels() const noexcept { return channels_; }
    [[nodiscard]] std::size_t filter_count() const noexcept { return filter_count_; }

private:
    struct Section {
        float b0{1.0F};
        float b1{0.0F};
        float b2{0.0F};
        float a1{0.0F};
        float a2{0.0F};
    };

    struct State {
        mutable float x1{0.0F};
        mutable float x2{0.0F};
        mutable float y1{0.0F};
        mutable float y2{0.0F};
    };

    std::array<Section, kMaxRealtimePeqFiltersV1> sections_{};
    std::array<std::array<State, 8U>, kMaxRealtimePeqFiltersV1> state_{};
    std::size_t filter_count_{0U};
    std::uint32_t sample_rate_{0U};
    std::uint32_t channels_{0U};
    bool prepared_{false};
};

}  // namespace hibiki
