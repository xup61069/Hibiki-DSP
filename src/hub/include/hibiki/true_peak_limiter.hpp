#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <array>
#include <cstddef>
#include <cstdint>

namespace hibiki {

// Bounded output guard. It estimates inter-sample peaks with three linear
// interpolation points per sample interval, then applies one block gain so
// channels remain coherent. It is deliberately not presented as a certified
// BS.1770/ITU true-peak meter; a production conformance meter can replace this
// fixed boundary without changing the graph contract.
//
// Attenuation is immediate whenever a block needs it. Recovery after that
// attenuation is capped at +6.02 dB per millisecond of audio processed, so a
// transient cannot make the guard itself jump back audibly, and the effective
// release time is the same regardless of how large each render callback is.
class TruePeakLimiterV1 final {
public:
    [[nodiscard]] float limit_in_place(float* interleaved,
                                       std::size_t frames,
                                       std::uint32_t channels,
                                       double ceiling_dbtp,
                                       std::uint32_t sample_rate) noexcept;
    void reset() noexcept;
    [[nodiscard]] float applied_gain_for_test() const noexcept
    {
        return applied_gain_;
    }

private:
    std::array<float, 8> previous_{};
    bool has_previous_{false};
    float applied_gain_{1.0F};
};

}  // namespace hibiki
