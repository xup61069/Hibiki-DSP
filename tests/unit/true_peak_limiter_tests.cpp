// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/true_peak_limiter.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

bool near_relative(float got, float want, float tol) noexcept {
    return std::fabs(got - want) <= tol * std::fabs(want);
}

double dbtp_to_linear(double dbtp) noexcept {
    return std::pow(10.0, dbtp / 20.0);
}

}  // namespace

int main() {
    using hibiki::TruePeakLimiterV1;

    // ---- invalid parameters are rejected without touching state ------------
    {
        TruePeakLimiterV1 limiter;
        std::vector<float> samples(16U, 0.5F);

        CHECK(limiter.limit_in_place(nullptr, 4U, 2U, -1.0, 48000U) == 1.0F);
        CHECK(limiter.limit_in_place(samples.data(), 0U, 2U, -1.0, 48000U) == 1.0F);
        CHECK(limiter.limit_in_place(samples.data(), 4U, 0U, -1.0, 48000U) == 1.0F);
        CHECK(limiter.limit_in_place(samples.data(), 4U, 9U, -1.0, 48000U) == 1.0F);
        const double nan_ceiling = std::numeric_limits<double>::quiet_NaN();
        const double inf_ceiling = std::numeric_limits<double>::infinity();
        CHECK(limiter.limit_in_place(samples.data(), 4U, 2U, nan_ceiling, 48000U) == 1.0F);
        CHECK(limiter.limit_in_place(samples.data(), 4U, 2U, inf_ceiling, 48000U) == 1.0F);
        CHECK(limiter.limit_in_place(samples.data(), 4U, 2U, -1.0, 7999U) == 1.0F);
        CHECK(limiter.limit_in_place(samples.data(), 4U, 2U, -1.0, 192001U) == 1.0F);

        // Rejected calls never engage the limiter.
        CHECK(limiter.applied_gain_for_test() == 1.0F);
        // Documented rate boundaries stay valid.
        CHECK(limiter.limit_in_place(samples.data(), 4U, 2U, -1.0, 8000U) == 1.0F);
        CHECK(limiter.limit_in_place(samples.data(), 4U, 2U, -1.0, 192000U) == 1.0F);
    }

    // ---- ceiling clamps into [-144, 0] dBTP ---------------------------------
    {
        TruePeakLimiterV1 limiter;
        std::vector<float> hot(32U, 2.0F);

        // -200 dBTP clamps to -144 dBTP: output must sit at the clamp line.
        const auto gain_low = limiter.limit_in_place(hot.data(), 16U, 2U,
                                                     -200.0, 48000U);
        CHECK(gain_low < 1.0F);
        CHECK(near_relative(hot[0], static_cast<float>(dbtp_to_linear(-144.0)),
                            1.0e-3F));
        limiter.reset();

        // A positive ceiling clamps to 0 dBTP and must not amplify.
        std::vector<float> mild(16U, 0.5F);
        const auto gain_high = limiter.limit_in_place(mild.data(), 8U, 2U,
                                                      3.0, 48000U);
        CHECK(gain_high == 1.0F);
        CHECK(mild[0] == 0.5F && mild[mild.size() - 1U] == 0.5F);
    }

    // ---- levels below the ceiling pass through untouched --------------------
    {
        TruePeakLimiterV1 limiter;
        std::vector<float> quiet{0.5F, -0.5F, 0.25F, -0.25F, 0.125F, -0.125F,
                                 0.0625F, -0.0625F};
        const auto gain = limiter.limit_in_place(quiet.data(), 4U, 2U,
                                                 -1.0, 48000U);
        CHECK(gain == 1.0F);
        CHECK(quiet[0] == 0.5F && quiet[1] == -0.5F && quiet[6] == 0.0625F);
        CHECK(limiter.applied_gain_for_test() == 1.0F);
    }

    // ---- attenuation is immediate and coherent across channels --------------
    {
        TruePeakLimiterV1 limiter;
        // Channel 0 sits well above the ceiling, channel 1 far below it, but
        // one shared block gain must scale both.
        std::vector<float> mixed{
            1.5F, 0.3F,  1.5F, 0.3F,  1.5F, 0.3F,  1.5F, 0.3F,
            1.5F, 0.3F,  1.5F, 0.3F,  1.5F, 0.3F,  1.5F, 0.3F};
        constexpr double kCeilingLin = 0.5011872336272722;  // -6 dBTP
        const auto gain = limiter.limit_in_place(mixed.data(), 8U, 2U,
                                                 -6.0, 48000U);
        CHECK(gain < 1.0F);
        CHECK(near_relative(gain, static_cast<float>(kCeilingLin / 1.5),
                            1.0e-4F));
        CHECK(limiter.applied_gain_for_test() == gain);
        for (std::size_t i = 0U; i < mixed.size(); i += 2U) {
            CHECK(mixed[i] == 1.5F * gain);
            CHECK(mixed[i + 1U] == 0.3F * gain);
            CHECK(mixed[i] <= static_cast<float>(kCeilingLin) + 1.0e-6F);
            CHECK(mixed[i + 1U] <= static_cast<float>(kCeilingLin) + 1.0e-6F);
        }
    }

    // ---- non-finite samples are sanitized, not propagated -------------------
    {
        TruePeakLimiterV1 limiter;
        const float nan_value = std::numeric_limits<float>::quiet_NaN();
        const float inf_value = std::numeric_limits<float>::infinity();
        std::vector<float> poisoned{nan_value, inf_value, 0.25F, -0.25F};
        const auto gain = limiter.limit_in_place(poisoned.data(), 4U, 1U,
                                                 0.0, 48000U);
        CHECK(gain == 1.0F);
        CHECK(poisoned[0] == 0.0F);
        CHECK(poisoned[1] == 0.0F);
        CHECK(poisoned[2] == 0.25F && poisoned[3] == -0.25F);
        // The limiter stays healthy afterwards.
        std::vector<float> followup(8U, 0.5F);
        CHECK(limiter.limit_in_place(followup.data(), 4U, 2U, -1.0, 48000U)
              == 1.0F);
    }

    // ---- recovery is capped at +6.0206 dB per millisecond -------------------
    {
        TruePeakLimiterV1 limiter;
        std::vector<float> engage(96U, 2.0F);  // 48 frames x 2 ch @ 48 kHz
        const auto gain_engage = limiter.limit_in_place(
            engage.data(), 48U, 2U, -6.0, 48000U);
        CHECK(gain_engage < 1.0F);

        // One quiet block may recover at most x2 (+6.02 dB over 1 ms).
        std::vector<float> quiet(96U, 0.05F);
        const auto gain_quiet = limiter.limit_in_place(
            quiet.data(), 48U, 2U, -6.0, 48000U);
        CHECK(gain_quiet > gain_engage);
        CHECK(near_relative(gain_quiet, gain_engage * 2.0F, 1.0e-3F));

        // Recovery keeps rising until it reaches unity and stops there.
        std::vector<float> silence(96U, 0.0F);
        const auto gain_final = limiter.limit_in_place(
            silence.data(), 48U, 2U, -6.0, 48000U);
        CHECK(gain_final == 1.0F);
        CHECK(limiter.applied_gain_for_test() == 1.0F);
    }

    // ---- recovery depends only on elapsed audio, not block size -------------
    {
        TruePeakLimiterV1 small_blocks;
        TruePeakLimiterV1 large_blocks;
        std::vector<float> engage_s(96U, 2.0F);
        std::vector<float> engage_l(96U, 2.0F);
        const auto engage_s_gain = small_blocks.limit_in_place(
            engage_s.data(), 48U, 2U, -6.0, 48000U);
        const auto engage_l_gain = large_blocks.limit_in_place(
            engage_l.data(), 48U, 2U, -6.0, 48000U);
        CHECK(engage_s_gain == engage_l_gain);

        // Same total recovery span: 96 frames as 3x32 vs 1x96 (mono keeps the
        // arithmetic obvious).
        std::vector<float> tail_s(96U, 0.0F);
        std::vector<float> tail_l(96U, 0.0F);
        auto gain_s = small_blocks.limit_in_place(tail_s.data(), 32U, 1U,
                                                  -6.0, 48000U);
        gain_s = small_blocks.limit_in_place(tail_s.data() + 32U, 32U, 1U,
                                             -6.0, 48000U);
        gain_s = small_blocks.limit_in_place(tail_s.data() + 64U, 32U, 1U,
                                             -6.0, 48000U);
        const auto gain_l = large_blocks.limit_in_place(tail_l.data(), 96U, 1U,
                                                        -6.0, 48000U);
        CHECK(near_relative(gain_s, gain_l, 1.0e-3F));
        CHECK(near_relative(small_blocks.applied_gain_for_test(),
                            large_blocks.applied_gain_for_test(), 1.0e-3F));
    }

    // ---- reset() restores factory behaviour ----------------------------------
    {
        TruePeakLimiterV1 limiter;
        std::vector<float> hot(64U, 2.0F);
        CHECK(limiter.limit_in_place(hot.data(), 32U, 2U, -6.0, 48000U) < 1.0F);

        limiter.reset();
        CHECK(limiter.applied_gain_for_test() == 1.0F);

        // Post-reset attenuation matches a fresh object bit for bit.
        TruePeakLimiterV1 fresh;
        std::vector<float> again(64U, 2.0F);
        std::vector<float> reference(64U, 2.0F);
        const auto gain_again = limiter.limit_in_place(
            again.data(), 32U, 2U, -6.0, 48000U);
        const auto gain_fresh = fresh.limit_in_place(
            reference.data(), 32U, 2U, -6.0, 48000U);
        CHECK(gain_again == gain_fresh);
        for (std::size_t i = 0U; i < again.size(); ++i) {
            CHECK(again[i] == reference[i]);
        }
    }

    std::fputs("true peak limiter tests passed\n", stdout);
    return 0;
}
