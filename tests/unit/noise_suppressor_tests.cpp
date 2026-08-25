// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/noise_suppressor.hpp"

#include <array>
#include <cstdio>

#define CHECK(expr)                                                             \
    do {                                                                        \
        if (!(expr)) {                                                          \
            std::fputs("FAILED: " #expr "\n", stderr);                          \
            return 1;                                                           \
        }                                                                       \
    } while (false)

int main() {
    // A disabled policy is not a valid configuration: configure() must reject
    // it fail-closed instead of silently behaving like a bypass.
    const hibiki::BasicNoiseSuppressorPolicyV1 disabled{};
    CHECK(!hibiki::validate_noise_suppressor_policy(disabled));

    hibiki::BasicNoiseSuppressorV1 suppressor;
    CHECK(!suppressor.configured());
    CHECK(!suppressor.configure(disabled, 48000U, 2U));
    CHECK(!suppressor.configured());

    std::array<float, 8> block{};
    block.fill(0.01F);
    CHECK(!suppressor.process_interleaved(block.data(), block.size()));

    // An enabled policy is processed normally with unchanged DSP math.
    const hibiki::BasicNoiseSuppressorPolicyV1 enabled{
        1U, true, -40.0, -30.0, 1.0, 10.0, 0.0};
    CHECK(hibiki::validate_noise_suppressor_policy(enabled));
    hibiki::BasicNoiseSuppressorV1 active;
    CHECK(active.configure(enabled, 48000U, 2U));
    CHECK(active.process_interleaved(block.data(), block.size()));

    return 0;
}
