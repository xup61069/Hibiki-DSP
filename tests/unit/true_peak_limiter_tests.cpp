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
using hibiki::TruePeakLimiterV1;
constexpr float kPi{3.14159265358979323846F};
}  // namespace

int main() {
    const auto qnan = std::numeric_limits<float>::quiet_NaN();
    const auto pinf = std::numeric_limits<float>::infinity();
    const auto ninf = -pinf;

    // ---- limit_in_place() parameter validation ------------------------------
    {
        TruePeakLimiterV1 limiter;
        float buffer[4] = {0.5F, -0.5F, 0.25F, -0.25F};
        CHECK(limiter.limit_in_place(nullptr, 2U, 2U, -1.0, 48000U) == 1.0F);
        CHECK(limiter.limit_in_place(buffer, 0U, 2U, -1.0, 48000U) == 1.0F);
        CHECK(limiter.limit_in_place(buffer, 2U, 0U, -1.0, 48000U) == 1.0F);
        CHECK(limiter.limit_in_place(buffer, 1U, 9U, -1.0, 48000U) == 1.0F);
        CHECK(limiter.limit_in_place(buffer, 2U, 2U, qnan, 48000U) == 1.0F);
        CHECK(limiter.limit_in_place(buffer, 2U, 2U,
                                     std::numeric_limits<double>::infinity(),
                                     48000U) == 1.0F);
        CHECK(limiter.limit_in_place(buffer, 2U, 2U, -1.0, 7999U) == 1.0F);
        CHECK(limiter.limit_in_place(buffer, 2U, 2U, -1.0, 192001U) == 1.0F);
        // Invalid calls must not mutate the buffer.
        CHECK(buffer[0] == 0.5F);
        CHECK(buffer[3] == -0.25F);
        // The eight-channel boundary is valid for one frame, while the
        // largest frame count whose interleaved product fits in size_t is
        // immediately followed by an unrepresentable request. The latter
        // must fail before touching caller storage or limiter state.
        constexpr auto max_frames_for_eight_channels =
            std::numeric_limits<std::size_t>::max() / 8U;
        CHECK(max_frames_for_eight_channels * 8U <=
              std::numeric_limits<std::size_t>::max());
        CHECK(max_frames_for_eight_channels + 1U >
              std::numeric_limits<std::size_t>::max() / 8U);
        float eight_channel_boundary[8] = {0.0F, 0.0F, 0.0F, 0.0F,
                                           0.0F, 0.0F, 0.0F, 0.0F};
        CHECK(limiter.limit_in_place(eight_channel_boundary, 1U, 8U,
                                     -1.0, 48000U) == 1.0F);
        const auto applied_before_overflow = limiter.applied_gain_for_test();
        float overflow_guard[8] = {0.125F, -0.25F, 0.5F, -0.75F,
                                   0.875F, -1.0F, 0.25F, -0.5F};
        const auto overflow_gain = limiter.limit_in_place(
            overflow_guard, max_frames_for_eight_channels + 1U, 8U,
            -1.0, 48000U);
        CHECK(overflow_gain == 1.0F);
        CHECK(limiter.applied_gain_for_test() == applied_before_overflow);
        CHECK(overflow_guard[0] == 0.125F && overflow_guard[7] == -0.5F);
        // Valid boundary sample rates.
        CHECK(limiter.limit_in_place(buffer, 2U, 2U, -1.0, 8000U) == 1.0F);
        CHECK(limiter.limit_in_place(buffer, 2U, 2U, -1.0, 192000U) == 1.0F);
    }

    // ---- non-finite input samples replaced with zero ------------------------
    {
        TruePeakLimiterV1 limiter;
        std::vector<float> samples{qnan, 0.5F, pinf, ninf, 0.25F};
        const auto gain =
            limiter.limit_in_place(samples.data(), samples.size(), 1U, -6.0, 48000U);
        CHECK(gain > 0.0F && gain <= 1.0F);
        for (const auto s : samples) {
            CHECK(std::isfinite(s));
            if (s != 0.0F) {
                CHECK(std::abs(s) <= 0.5012F);  // ceiling = -6 dBTP ~= 0.5012
            }
        }
    }
    // ---- finite extreme interpolation and zero-gain sanitation ------------
    {
        TruePeakLimiterV1 limiter;
        const auto max_float = std::numeric_limits<float>::max();
        std::vector<float> samples{max_float, -max_float};
        const auto gain =
            limiter.limit_in_place(samples.data(), samples.size(), 1U, -144.0, 48000U);
        CHECK(gain == 0.0F);
        CHECK(limiter.applied_gain_for_test() == 0.0F);
        for (const auto sample : samples) {
            CHECK(std::isfinite(sample));
            CHECK(sample == 0.0F);
        }
    }
    // ---- ceiling clamping to [-144, 0] dBTP ---------------------------------
    {
        TruePeakLimiterV1 limiter;
        std::vector<float> samples(100U);
        for (std::size_t i = 0U; i < samples.size(); ++i) {
            samples[i] = static_cast<float>(i % 10U) / 10.0F;
        }
        // ceiling = -200 dBTP clamps to -144; peak 0.9 > ceiling so gain < 1.
        const auto gain_low =
            limiter.limit_in_place(samples.data(), samples.size(), 1U, -200.0, 48000U);
        CHECK(gain_low < 1.0F);
        CHECK(gain_low > 0.0F);
        limiter.reset();
        // ceiling = +3 dBTP clamps to 0 (no boost); peak <= 1.0 so gain = 1.
        const auto gain_high =
            limiter.limit_in_place(samples.data(), samples.size(), 1U, 3.0, 48000U);
        CHECK(gain_high == 1.0F);
    }

    // ---- below-ceiling signal passes through bit-exact ----------------------
    {
        TruePeakLimiterV1 limiter;
        std::vector<float> original{0.0F, 0.1F, -0.2F, 0.3F, -0.4F,
                                    0.5F, -0.3F, 0.2F};
        std::vector<float> buffer = original;
        const auto gain =
            limiter.limit_in_place(buffer.data(), buffer.size(), 1U, 0.0, 44100U);
        CHECK(gain == 1.0F);
        for (std::size_t i = 0U; i < buffer.size(); ++i) {
            CHECK(buffer[i] == original[i]);  // bitwise passthrough
        }
    }
    // ---- inter-sample peak detection with interpolated points ---------------
    {
        TruePeakLimiterV1 limiter;
        // Constant sine near Nyquist: inter-sample peaks exceed sample peaks.
        constexpr std::size_t frames = 64U;
        constexpr std::uint32_t channels = 2U;
        std::vector<float> stereo(frames * channels);
        for (std::size_t f = 0U; f < frames; ++f) {
            const auto s = static_cast<float>(
                std::sin(2.0 * kPi * 23.0 * static_cast<double>(f) / 48.0));
            stereo[f * channels + 0U] = s;
            stereo[f * channels + 1U] = -s;
        }
        // Find raw peak before limiting.
        float raw_peak = 0.0F;
        for (const auto s : stereo) raw_peak = std::max(raw_peak, std::abs(s));
        CHECK(raw_peak > 0.0F && raw_peak <= 1.0F);

        // Use ceiling just below raw_peak to force attenuation.
        // Interpolation may detect a higher effective peak than raw_peak,
        // so the applied gain should be lower than ceiling/raw_peak.
        const double ceiling_db = -0.5;
        limiter.reset();
        std::vector<float> work = stereo;
        const auto gain =
            limiter.limit_in_place(work.data(), frames, channels, ceiling_db, 48000U);
        CHECK(gain < 1.0F);  // some attenuation was needed
        // After gain, all samples must be at or below ceiling.
        const auto ceiling_lin =
            static_cast<float>(std::pow(10.0, ceiling_db / 20.0));
        bool all_within_ceiling = true;
        for (const auto s : work) {
            if (std::abs(s) > ceiling_lin + 1e-6F) { all_within_ceiling = false; break; }
        }
        CHECK(all_within_ceiling);
        // Channel coherence: L and R samples have same absolute gain ratio.
        for (std::size_t f = 0U; f < frames; ++f) {
            const auto l_orig = stereo[f * channels + 0U];
            const auto r_orig = stereo[f * channels + 1U];
            if (l_orig != 0.0F) {
                const auto ratio_l = work[f * channels + 0U] / l_orig;
                const auto ratio_r = work[f * channels + 1U] / r_orig;
                CHECK(std::abs(ratio_l - gain) < 1e-5F);
                CHECK(std::abs(ratio_r - gain) < 1e-5F);
            }
        }
    }
    // ---- recovery capped at ~+6.02 dB per ms --------------------------------
    {
        TruePeakLimiterV1 limiter;
        // Force heavy attenuation first.
        std::vector<float> loud(480U, 0.9F);
        const auto attenuated =
            limiter.limit_in_place(loud.data(), 480U, 1U, -12.0, 48000U);
        // ceiling -12 dBTP / peak 0.9 => gain ~= 0.279.
        CHECK(attenuated < 0.5F);

        // Process exactly 48 frames (1 ms at 48 kHz) of silence.
        std::vector<float> silence(48U, 0.0F);
        const auto after_1ms =
            limiter.limit_in_place(silence.data(), 48U, 1U, -12.0, 48000U);
        // Recovery must make progress but never exceed +6.0206 dB (= x2.0000)
        // within one millisecond; without the cap it would jump straight to 1.
        const auto max_recovery_factor =
            static_cast<float>(std::pow(10.0, 6.0206 / 20.0));
        CHECK(after_1ms > attenuated);
        CHECK(after_1ms <= attenuated * max_recovery_factor + 1e-6F);
        CHECK(after_1ms < 1.0F);
    }

    // ---- block-size invariance for recovery ---------------------------------
    {
        TruePeakLimiterV1 limiter_a;
        TruePeakLimiterV1 limiter_b;

        // Same initial attenuation on independent copies of the same material;
        // limit_in_place() mutates its buffer, so sharing one would bias the
        // second limiter with an already-attenuated signal.
        std::vector<float> trigger_a(96U, 0.9F);
        std::vector<float> trigger_b = trigger_a;
        const auto g_init =
            limiter_a.limit_in_place(trigger_a.data(), trigger_a.size(), 1U,
                                     -12.0, 48000U);
        const auto g_init_b =
            limiter_b.limit_in_place(trigger_b.data(), trigger_b.size(), 1U,
                                     -12.0, 48000U);
        CHECK(g_init == g_init_b);

        // A recovers across one 1 ms block; B across two 0.5 ms blocks. The
        // +6.0206 dB/ms cap compounds exponentially, so both must land on the
        // same final gain while still staying below full recovery.
        std::vector<float> silence_a(48U, 0.0F);
        std::vector<float> silence_b(24U, 0.0F);
        const auto g_a =
            limiter_a.limit_in_place(silence_a.data(), silence_a.size(), 1U,
                                     -12.0, 48000U);
        const auto g_mid =
            limiter_b.limit_in_place(silence_b.data(), silence_b.size(), 1U,
                                     -12.0, 48000U);
        CHECK(g_mid > g_init);  // half-millisecond recovery already happened
        const auto g_b =
            limiter_b.limit_in_place(silence_b.data(), silence_b.size(), 1U,
                                     -12.0, 48000U);
        CHECK(g_a < 1.0F);  // the recovery cap actually binds
        CHECK(g_b < 1.0F);
        CHECK(std::abs(static_cast<double>(g_a) - static_cast<double>(g_b)) <
              0.01);
    }

    // ---- reset() returns to initial state -----------------------------------
    {
        TruePeakLimiterV1 limiter;
        // Attenuate first.
        std::vector<float> loud(100U, 0.8F);
        const auto attenuated =
            limiter.limit_in_place(loud.data(), 100U, 1U, -6.0, 44100U);
        CHECK(attenuated < 1.0F);

        limiter.reset();
        CHECK(limiter.applied_gain_for_test() == 1.0F);

        // After reset, below-ceiling input passes through unchanged.
        std::vector<float> original{0.1F, -0.2F, 0.3F};
        std::vector<float> buffer = original;
        const auto gain =
            limiter.limit_in_place(buffer.data(), buffer.size(), 1U, 0.0, 48000U);
        CHECK(gain == 1.0F);
        for (std::size_t i = 0U; i < buffer.size(); ++i) {
            CHECK(buffer[i] == original[i]);
        }
    }

    return 0;
}
