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
class TruePeakLimiterV1 final {
public:
    [[nodiscard]] float limit_in_place(float* interleaved,
                                       std::size_t frames,
                                       std::uint32_t channels,
                                       double ceiling_dbtp) noexcept;
    void reset() noexcept;

private:
    std::array<float, 8> previous_{};
    bool has_previous_{false};
};

}  // namespace hibiki
