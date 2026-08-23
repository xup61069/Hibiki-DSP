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
// attenuation is deliberately capped at +6 dB per processed block so a
// transient cannot make the guard itself jump back audibly.
class TruePeakLimiterV1 final {
public:
    [[nodiscard]] float limit_in_place(float* interleaved,
                                       std::size_t frames,
                                       std::uint32_t channels,
                                       double ceiling_dbtp) noexcept;
    void reset() noexcept;

private:
    static constexpr float kMaxRecoveryGainPerBlock{2.0F};

    std::array<float, 8> previous_{};
    bool has_previous_{false};
    float applied_gain_{1.0F};
};

}  // namespace hibiki
