// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/lane_latency.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <vector>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

float marker_frame_value(const std::size_t global_frame,
                         const std::uint32_t channel) noexcept {
    // Distinct value per (frame, channel); small enough to stay exact in
    // float for the ranges used here.
    return static_cast<float>((global_frame + 1U) * 16U + channel);
}

bool block_finite(const std::vector<float>& block) noexcept {
    for (const auto sample : block) {
        if (!std::isfinite(sample)) return false;
    }
    return true;
}

}  // namespace

int main() {
    using hibiki::kLaneLatencyMaxFramesV1;
    using LaneBank = hibiki::LaneLatencyBankV1;
    using Config = hibiki::LaneLatencyConfigV1;

    // ---- prepare() validation ----------------------------------------------
    {
        LaneBank bank;
        CHECK(bank.prepare({}) && !bank.prepared(0U));

        const Config bad_zero_channels{0U, 4U, true};
        CHECK(!bank.prepare(std::span(&bad_zero_channels, 1U)));

        const Config bad_nine_channels{9U, 4U, true};
        CHECK(!bank.prepare(std::span(&bad_nine_channels, 1U)));

        const Config bad_delay{2U, hibiki::kLaneLatencyMaxSamplesV1 + 1U, true};
        CHECK(!bank.prepare(std::span(&bad_delay, 1U)));

        std::array<Config, hibiki::kLaneLatencyMaxLanesV1 + 1U> too_many{};
        too_many.fill(Config{2U, 1U, true});
        CHECK(!bank.prepare(std::span(too_many)));
    }

    // A failed prepare() must not corrupt an already usable bank: the
    // implementation stages a candidate and swaps only on success.
    {
        LaneBank bank;
        const std::array<Config, 1U> good{Config{2U, 3U, true}};
        CHECK(bank.prepare(std::span(good)));
        const std::array<Config, 1U> bad{Config{2U, 999999U, true}};
        CHECK(!bank.prepare(std::span(bad)));
        CHECK(bank.prepared(0U));

        std::vector<float> probe{0.25F, -0.25F};
        CHECK(bank.process_lane(0U, probe.data(), 2U, 1U));
    }

    // ---- main behavior fixture ---------------------------------------------
    // lane 0: stereo, 3-sample delay. lane 1: mono, bypass. lane 2: stereo
    // with a configured delay but disabled, so it must pass through too.
    LaneBank bank;
    const std::array<Config, 3U> configs{
        Config{2U, 3U, true},
        Config{1U, 0U, true},
        Config{2U, 4U, false},
    };
    CHECK(bank.prepare(std::span(configs)));
    CHECK(bank.prepared(0U) && bank.prepared(1U) && bank.prepared(2U));
    CHECK(!bank.prepared(3U));
    CHECK(bank.delay_samples(0U) == 3U);
    CHECK(bank.delay_samples(1U) == 0U);
    CHECK(bank.delay_samples(2U) == 0U);

    // Delayed lane: output is the input from exactly N frames ago, including
    // across process_lane call boundaries; the first N frames are silence.
    {
        constexpr std::size_t kFrames = 8U;
        std::vector<float> in(kFrames * 2U);
        std::vector<float> out(kFrames * 2U);
        for (std::size_t frame = 0U; frame < kFrames; ++frame) {
            for (std::uint32_t ch = 0U; ch < 2U; ++ch) {
                in[frame * 2U + ch] = marker_frame_value(frame, ch);
            }
        }
        CHECK(bank.process_lane(0U, in.data(), 2U, kFrames));
        const auto* lane_out = bank.output(0U);
        CHECK(lane_out != nullptr);
        for (std::size_t frame = 0U; frame < kFrames; ++frame) {
            for (std::uint32_t ch = 0U; ch < 2U; ++ch) {
                const float got = lane_out[frame * 2U + ch];
                if (frame < 3U) {
                    CHECK(got == 0.0F);
                } else {
                    CHECK(got == marker_frame_value(frame - 3U, ch));
                }
            }
        }

        // Second call: the ring must continue seamlessly (write_index kept).
        for (std::size_t frame = 0U; frame < kFrames; ++frame) {
            for (std::uint32_t ch = 0U; ch < 2U; ++ch) {
                in[frame * 2U + ch] = marker_frame_value(kFrames + frame, ch);
            }
        }
        CHECK(bank.process_lane(0U, in.data(), 2U, kFrames));
        lane_out = bank.output(0U);
        for (std::size_t frame = 0U; frame < kFrames; ++frame) {
            for (std::uint32_t ch = 0U; ch < 2U; ++ch) {
                const auto global = kFrames + frame;
                CHECK(lane_out[frame * 2U + ch] ==
                      marker_frame_value(global - 3U, ch));
            }
        }
    }

    // Bypass lane (delay 0): bitwise passthrough.
    {
        std::vector<float> in{0.5F, -0.5F, 2.0F};
        CHECK(bank.process_lane(1U, in.data(), 1U, 3U));
        const auto* lane_out = bank.output(1U);
        CHECK(lane_out != nullptr);
        CHECK(lane_out[0] == 0.5F && lane_out[1] == -0.5F && lane_out[2] == 2.0F);
    }

    // Disabled lane with a configured delay: treated as pure passthrough.
    {
        std::vector<float> in{0.125F, -0.125F, 0.375F, -0.375F};
        CHECK(bank.process_lane(2U, in.data(), 2U, 2U));
        const auto* lane_out = bank.output(2U);
        CHECK(lane_out != nullptr);
        CHECK(lane_out[0] == 0.125F && lane_out[1] == -0.125F &&
              lane_out[2] == 0.375F && lane_out[3] == -0.375F);
    }

    // Lanes are independent: lane 0's history does not leak into lane 1/2.
    CHECK(bank.output(0U) != bank.output(1U));
    CHECK(bank.output(0U) != bank.output(2U));

    // ---- reset() ------------------------------------------------------------
    {
        LaneBank resettable;
        const std::array<Config, 1U> cfg{Config{1U, 2U, true}};
        CHECK(resettable.prepare(std::span(cfg)));
        std::vector<float> hot(64U, 0.75F);
        CHECK(resettable.process_lane(0U, hot.data(), 1U, hot.size()));
        resettable.reset();
        std::vector<float> again(hot.size(), 0.75F);
        CHECK(resettable.process_lane(0U, again.data(), 1U, again.size()));
        const auto* lane_out = resettable.output(0U);
        // The ring was cleared, so the first two frames are silence again.
        CHECK(lane_out[0] == 0.0F && lane_out[1] == 0.0F);
        CHECK(lane_out[2] == 0.75F);
    }

    // ---- fail-closed paths ---------------------------------------------------
    LaneBank guard;
    const std::array<Config, 1U> cfg{Config{2U, 1U, true}};
    CHECK(guard.prepare(std::span(cfg)));

    std::vector<float> ok(4U, 0.5F);
    CHECK(!guard.process_lane(1U, ok.data(), 2U, 1U));          // unprepared lane
    CHECK(!guard.process_lane(0U, nullptr, 2U, 1U));            // null buffer
    CHECK(!guard.process_lane(0U, ok.data(), 2U, 0U));          // empty block
    CHECK(!guard.process_lane(0U, ok.data(), 1U, 1U));          // channel mismatch
    CHECK(!guard.process_lane(0U, ok.data(), 2U,
                              kLaneLatencyMaxFramesV1 + 1U));   // oversized block

    // Non-finite input fails the whole block AND clears stored state so a
    // poisoned ring cannot leak into later audio.
    {
        LaneBank sanitizer;
        const std::array<Config, 1U> scfg{Config{1U, 2U, true}};
        CHECK(sanitizer.prepare(std::span(scfg)));
        std::vector<float> warm(16U, 0.5F);
        CHECK(sanitizer.process_lane(0U, warm.data(), 1U, warm.size()));
        std::vector<float> poison(warm.size(), 0.25F);
        poison[7U] = std::numeric_limits<float>::quiet_NaN();
        CHECK(!sanitizer.process_lane(0U, poison.data(), 1U, poison.size()));
        CHECK(sanitizer.process_lane(0U, warm.data(), 1U, warm.size()));
        const auto* lane_out = sanitizer.output(0U);
        CHECK(block_finite(std::vector<float>(lane_out, lane_out + warm.size())));
        // State was cleared by the poisoning attempt: first two frames silent.
        CHECK(lane_out[0] == 0.0F && lane_out[1] == 0.0F);
    }

    std::fputs("lane latency tests passed\n", stdout);
    return 0;
}
