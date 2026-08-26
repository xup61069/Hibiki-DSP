// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/output_crossfade.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#define CHECK(expr)                                                             \
    do {                                                                        \
        if (!(expr)) {                                                          \
            std::fputs("FAILED: " #expr "\n", stderr);                         \
            return 1;                                                           \
        }                                                                       \
    } while (false)

namespace {

constexpr std::uint32_t kStereo = 2U;
constexpr std::uint32_t kRate48k = 48000U;

// 10 ms at 48 kHz divides evenly: 480 frames total.
constexpr std::size_t kTotalFrames = 480U;

std::vector<float> make_signal(const std::size_t samples, const float value) {
    return std::vector<float>(samples, value);
}

}  // namespace

int main() {
    // ---- fresh state -------------------------------------------------------
    hibiki::OutputCrossfade fresh;
    CHECK(!fresh.snapshot().active);
    CHECK(fresh.snapshot().channels == 0U);
    CHECK(fresh.snapshot().total_frames == 0U);
    std::vector<float> silence(kStereo, 0.0F);
    std::vector<float> sink(kStereo, 0.0F);
    CHECK(!fresh.process(silence.data(), silence.data(), sink.data(), 1U));

    // ---- configuration boundary rejection ----------------------------------
    hibiki::OutputCrossfade rejector;
    for (const auto channels : {0U, 1U, 3U, 4U, 5U, 7U, 9U}) {
        CHECK(!rejector.begin(channels, kRate48k, 30U));
    }
    for (const auto rate : {0U, 8000U, 22050U, 44101U, 96001U}) {
        CHECK(!rejector.begin(kStereo, rate, 30U));
    }
    for (const auto duration : {0U, 201U}) {
        CHECK(!rejector.begin(kStereo, kRate48k, duration));
    }
    CHECK(!rejector.snapshot().active);

    // ---- accepted edges produce ceiling-divided total_frames ---------------
    struct EdgeCase {
        std::uint32_t channels;
        std::uint32_t rate;
        std::uint32_t duration_ms;
        std::size_t expected_frames;
    };
    constexpr EdgeCase edges[]{
        {2U, 44100U, 1U, 45U},     // ceil(44.1)
        {2U, 48000U, 30U, 1440U},
        {6U, 96000U, 200U, 19200U},
        {8U, 192000U, 1U, 192U},
    };
    for (const auto& edge : edges) {
        hibiki::OutputCrossfade acceptor;
        CHECK(acceptor.begin(edge.channels, edge.rate, edge.duration_ms));
        CHECK(acceptor.snapshot().active);
        CHECK(acceptor.snapshot().channels == edge.channels);
        CHECK(acceptor.snapshot().total_frames == edge.expected_frames);
        CHECK(acceptor.snapshot().processed_frames == 0U);
    }

    // ---- equal-power endpoints ---------------------------------------------
    hibiki::OutputCrossfade endpoints;
    CHECK(endpoints.begin(kStereo, kRate48k, 10U));
    const auto old_signal = make_signal(kTotalFrames * kStereo, 1.0F);
    const auto new_signal = make_signal(kTotalFrames * kStereo, 0.0F);
    std::vector<float> mixed(kTotalFrames * kStereo, 0.0F);
    CHECK(endpoints.process(old_signal.data(), new_signal.data(), mixed.data(),
                            kTotalFrames));
    // Frame 0: position 0 -> old gain exactly 1, new gain exactly 0.
    CHECK(mixed[0] == 1.0F);
    CHECK(mixed[1] == 1.0F);
    // Near-final frame: old gain decays toward zero but never fully reaches it.
    constexpr std::size_t kLastSampleIndex = (kTotalFrames - 1U) * kStereo;
    CHECK(std::abs(mixed[kLastSampleIndex]) < 0.02F);
    CHECK(endpoints.snapshot().processed_frames == kTotalFrames);
    CHECK(!endpoints.snapshot().active);

    // Post-completion processing must be rejected fail-closed.
    CHECK(!endpoints.process(old_signal.data(), new_signal.data(), mixed.data(), 1U));

    // ---- correlated unity midpoint sums to sqrt(2) -------------------------
    hibiki::OutputCrossfade midpoint;
    CHECK(midpoint.begin(kStereo, kRate48k, 10U));
    const auto unity_old = make_signal(kTotalFrames * kStereo, 1.0F);
    const auto unity_new = make_signal(kTotalFrames * kStereo, 1.0F);
    std::vector<float> summed(kTotalFrames * kStereo, 0.0F);
    CHECK(midpoint.process(unity_old.data(), unity_new.data(), summed.data(),
                           kTotalFrames));
    // Frame 240 sits at position 0.5: both gains are sqrt(2)/2.
    constexpr std::size_t kMidSampleIndex = 240U * kStereo;
    constexpr double kSqrtTwo = 1.41421356237309504880;
    CHECK(std::abs(static_cast<double>(summed[kMidSampleIndex]) - kSqrtTwo) < 1e-4);

    // ---- partial-block accumulation ----------------------------------------
    hibiki::OutputCrossfade partial;
    CHECK(partial.begin(kStereo, kRate48k, 10U));
    for (std::size_t call = 0U; call < 4U; ++call) {
        CHECK(partial.process(unity_old.data(), unity_new.data(), summed.data(), 100U));
        CHECK(partial.snapshot().processed_frames == (call + 1U) * 100U);
        CHECK(partial.snapshot().active);
    }
    CHECK(partial.process(unity_old.data(), unity_new.data(), summed.data(), 100U));
    CHECK(partial.snapshot().processed_frames == kTotalFrames);
    CHECK(!partial.snapshot().active);

    // ---- overshoot frames clamp to pure new-signal output -------------------
    hibiki::OutputCrossfade overshoot;
    CHECK(overshoot.begin(kStereo, kRate48k, 10U));
    const auto loud_old = make_signal(kTotalFrames * kStereo, 1.0F);
    const auto loud_new = make_signal(kTotalFrames * kStereo, 2.0F);
    // The final call advances the write pointer past 400 frames and supplies
    // 160 more; allocate enough room for both segments.
    constexpr std::size_t kBlendedCapacity = 560U * kStereo;
    std::vector<float> blended(kBlendedCapacity, 0.0F);
    // First 400 frames consume most of the budget.
    CHECK(overshoot.process(loud_old.data(), loud_new.data(), blended.data(), 400U));
    // Final call supplies 160 frames but only 80 remain; the excess 80 clamp
    // to position 1.0 where old gain vanishes and new gain is exactly 1.
    // Advance the write pointer because process() always writes from the
    // start of the given output span. Inputs restart from the buffer origin:
    // both signals are constant, so re-reading from index 0 is equivalent.
    CHECK(overshoot.process(loud_old.data(), loud_new.data(),
                            blended.data() + 400U * kStereo, 160U));
    CHECK(overshoot.snapshot().processed_frames == kTotalFrames);
    constexpr std::size_t kClampedIndex = 500U * kStereo;
    CHECK(std::abs(blended[kClampedIndex] - 2.0F) < 1e-6F);

    // ---- multichannel interleaving ------------------------------------------
    hibiki::OutputCrossfade surround;
    constexpr std::uint32_t kSixCh = 6U;
    CHECK(surround.begin(kSixCh, kRate48k, 1U));
    constexpr std::size_t kSurroundTotal = 48U;  // 48000 * 1 ms / 1000
    std::vector<float> ch_old(kSurroundTotal * kSixCh);
    std::vector<float> ch_new(kSurroundTotal * kSixCh);
    for (std::size_t sample = 0U; sample < ch_old.size(); ++sample) {
        ch_old[sample] = static_cast<float>(sample % kSixCh);
        ch_new[sample] = -static_cast<float>(sample % kSixCh);
    }
    std::vector<float> ch_mixed(ch_old.size(), 0.0F);
    CHECK(surround.process(ch_old.data(), ch_new.data(), ch_mixed.data(), 24U));
    // Frame 0: old gain 1, new gain 0 -> output preserves per-channel identity.
    for (std::uint32_t channel = 0U; channel < kSixCh; ++channel) {
        CHECK(ch_mixed[channel] == static_cast<float>(channel));
    }
    // Frame 23 at position 23/48: old and new gains are fixed scalars shared
    // across all channels; compute them directly from the equal-power curve.
    constexpr std::size_t kFrame23Offset = 23U * kSixCh;
    constexpr double kHalfPi = 1.57079632679489661923;
    const double position23 = static_cast<double>(23U) /
                              static_cast<double>(kSurroundTotal);
    const double old_gain_23 = std::cos(position23 * kHalfPi);
    const double new_gain_23 = std::sin(position23 * kHalfPi);
    for (std::uint32_t channel = 1U; channel < kSixCh; ++channel) {
        const auto expected_channel_value =
            static_cast<float>(old_gain_23 - new_gain_23) *
            static_cast<float>(channel);
        CHECK(std::abs(ch_mixed[kFrame23Offset + channel] -
                       expected_channel_value) < 1e-3F);
    }

    // ---- reset returns to fresh state and permits reuse ---------------------
    hibiki::OutputCrossfade reusable;
    CHECK(reusable.begin(kStereo, kRate48k, 10U));
    CHECK(reusable.process(unity_old.data(), unity_new.data(), summed.data(), kTotalFrames));
    CHECK(!reusable.snapshot().active);
    reusable.reset();
    CHECK(!reusable.snapshot().active);
    CHECK(reusable.snapshot().total_frames == 0U);
    CHECK(!reusable.process(unity_old.data(), unity_new.data(), summed.data(), 1U));
    CHECK(reusable.begin(kStereo, kRate48k, 10U));
    CHECK(reusable.snapshot().active);
    CHECK(reusable.snapshot().processed_frames == 0U);
    CHECK(reusable.process(unity_old.data(), unity_new.data(), summed.data(), kTotalFrames));
    CHECK(reusable.snapshot().processed_frames == kTotalFrames);

    return 0;
}
