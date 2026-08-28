// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/exporters.hpp"
#include "hibiki/peq_dsp.hpp"

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

constexpr double kPi = 3.14159265358979323846;

double sine_sample(const std::size_t index, const double frequency_hz,
                   const std::uint32_t sample_rate) noexcept {
    return std::sin(2.0 * kPi * frequency_hz *
                    static_cast<double>(index) / static_cast<double>(sample_rate));
}

float peak_amplitude(const std::vector<float>& samples, std::size_t skip) noexcept {
    float peak = 0.0F;
    for (std::size_t index = skip; index < samples.size(); ++index) {
        peak = std::max(peak, std::abs(samples[index]));
    }
    return peak;
}

}  // namespace

int main() {
    using hibiki::kMaxRealtimePeqFiltersV1;
    using hibiki::PeqFilterV1;
    using Processor = hibiki::PeqProcessorV1;

    // ---- prepare() validation ----------------------------------------------
    {
        Processor processor;
        CHECK(!processor.prepared());

        const PeqFilterV1 identity{1000.0, 0.0, 1.0};
        CHECK(processor.prepare(std::span(&identity, 1U), 8000U, 1U));
        CHECK(!processor.prepare(std::span(&identity, 1U), 7999U, 1U));
        CHECK(!processor.prepare(std::span(&identity, 1U), 192001U, 1U));
        CHECK(!processor.prepare(std::span(&identity, 1U), 48000U, 0U));
        CHECK(!processor.prepare(std::span(&identity, 1U), 48000U, 9U));

        std::vector<PeqFilterV1> too_many(kMaxRealtimePeqFiltersV1 + 1U, identity);
        CHECK(!processor.prepare(std::span(too_many), 48000U, 2U));

        const PeqFilterV1 at_nyquist{24000.0, 6.0, 1.0};   // == Nyquist for 48k
        CHECK(!processor.prepare(std::span(&at_nyquist, 1U), 48000U, 2U));
        const PeqFilterV1 above_nyquist{30000.0, 6.0, 1.0};
        CHECK(!processor.prepare(std::span(&above_nyquist, 1U), 48000U, 2U));

        const PeqFilterV1 bad_q{1000.0, 6.0, 0.0};
        CHECK(!processor.prepare(std::span(&bad_q, 1U), 48000U, 2U));

        // A failed prepare() must leave the last good configuration intact.
        CHECK(processor.prepared());
        CHECK(processor.sample_rate() == 8000U);
        CHECK(processor.filter_count() == 1U);
    }

    // ---- gain=0 dB is a passthrough section --------------------------------
    {
        Processor processor;
        const PeqFilterV1 flat{1000.0, 0.0, 1.0};
        CHECK(processor.prepare(std::span(&flat, 1U), 48000U, 2U));

        std::vector<float> block{0.25F, -0.5F, 0.75F, -0.125F};
        const auto original = block;
        CHECK(processor.process_interleaved(block.data(), 2U));
        for (std::size_t index = 0; index < block.size(); ++index) {
            CHECK(block[index] == original[index]);
        }
    }

    // ---- +12 dB boost @ 1 kHz tracks the RBJ design amplitude -------------
    {
        constexpr std::size_t kFrames = 4096U;
        Processor processor;
        const PeqFilterV1 boost{1000.0, 12.0, 1.4};
        CHECK(processor.prepare(std::span(&boost, 1U), 48000U, 1U));

        std::vector<float> signal(kFrames);
        for (std::size_t index = 0; index < kFrames; ++index) {
            signal[index] = static_cast<float>(
                sine_sample(index, 1000.0, 48000U));
        }
        CHECK(processor.process_interleaved(signal.data(), kFrames));
        // Skip the filter transient; steady-state peak should be ~3.98x.
        const auto steady_peak =
            static_cast<double>(peak_amplitude(signal, kFrames / 2U));
        CHECK(steady_peak > 3.4 && steady_peak < 4.6);
    }

    // ---- cascaded filters behave like sequential application --------------
    {
        constexpr std::size_t kFrames = 2048U;
        Processor cascade;
        const std::array<PeqFilterV1, 2U> pair{
            PeqFilterV1{200.0, 8.0, 1.2}, PeqFilterV1{4000.0, -8.0, 1.2}};
        CHECK(cascade.prepare(std::span(pair), 48000U, 1U));

        std::vector<float> through_cascade(kFrames);
        for (std::size_t index = 0; index < kFrames; ++index) {
            through_cascade[index] = static_cast<float>(
                sine_sample(index, 200.0, 48000U));
        }
        CHECK(cascade.process_interleaved(through_cascade.data(), kFrames));

        Processor single_pass;
        const PeqFilterV1 first{200.0, 8.0, 1.2};
        CHECK(single_pass.prepare(std::span(&first, 1U), 48000U, 1U));
        std::vector<float> through_first = through_cascade;
        // Re-run from the same input to compare against manual staging.
        for (std::size_t index = 0; index < kFrames; ++index) {
            through_first[index] = static_cast<float>(
                sine_sample(index, 200.0, 48000U));
        }
        CHECK(single_pass.process_interleaved(through_first.data(), kFrames));

        Processor second_pass;
        const PeqFilterV1 second{4000.0, -8.0, 1.2};
        CHECK(second_pass.prepare(std::span(&second, 1U), 48000U, 1U));
        CHECK(second_pass.process_interleaved(through_first.data(), kFrames));

        // Steady-state region must match within float round-off tolerance.
        for (std::size_t index = kFrames / 2U; index < kFrames; ++index) {
            if (std::abs(static_cast<double>(through_cascade[index]) -
                         static_cast<double>(through_first[index])) > 1e-3) {
                std::fputs("cascade mismatch\n", stderr);
                return 1;
            }
        }
    }

    // ---- channel independence ---------------------------------------------
    {
        constexpr std::size_t kFrames = 512U;
        Processor processor;
        const PeqFilterV1 boost{1000.0, 9.0, 1.0};
        CHECK(processor.prepare(std::span(&boost, 1U), 48000U, 2U));

        std::vector<float> interleaved(kFrames * 2U);
        for (std::size_t frame = 0; frame < kFrames; ++frame) {
            interleaved[frame * 2U] = static_cast<float>(
                sine_sample(frame, 1000.0, 48000U));
            interleaved[frame * 2U + 1U] = 0.5F * static_cast<float>(
                sine_sample(frame, 1000.0, 48000U));
        }
        CHECK(processor.process_interleaved(interleaved.data(), kFrames));
        for (std::size_t frame = kFrames / 2U; frame < kFrames; ++frame) {
            const auto left = static_cast<double>(interleaved[frame * 2U]);
            const auto right = static_cast<double>(interleaved[frame * 2U + 1U]);
            if (std::abs(left - 2.0 * right) > 1e-3) {
                std::fputs("channel state leaked\n", stderr);
                return 1;
            }
        }
    }

    // ---- reset() clears DF1 state -----------------------------------------
    {
        constexpr std::size_t kFrames = 256U;
        Processor processor;
        const PeqFilterV1 boost{1000.0, 12.0, 1.4};
        CHECK(processor.prepare(std::span(&boost, 1U), 48000U, 1U));

        std::vector<float> warmup(kFrames);
        for (std::size_t index = 0; index < kFrames; ++index) {
            warmup[index] = static_cast<float>(sine_sample(index, 1000.0, 48000U));
        }
        CHECK(processor.process_interleaved(warmup.data(), kFrames));
        processor.reset();
        std::vector<float> again(kFrames);
        for (std::size_t index = 0; index < kFrames; ++index) {
            again[index] = static_cast<float>(sine_sample(index, 1000.0, 48000U));
        }
        CHECK(processor.process_interleaved(again.data(), kFrames));
        // First sample after reset must equal first-sample-after-prepare.
        std::vector<float> fresh(1U);
        fresh[0] = static_cast<float>(sine_sample(0U, 1000.0, 48000U));
        CHECK(fresh[0] == again[0]);  // same input sample
    }

    // ---- fail-closed paths --------------------------------------------------
    {
        Processor processor;
        const PeqFilterV1 flat{500.0, 0.0, 1.0};
        CHECK(processor.prepare(std::span(&flat, 1U), 48000U, 1U));

        std::vector<float> ok(8U, 0.25F);
        CHECK(!processor.process_interleaved(nullptr, 1U));
        CHECK(!processor.process_interleaved(ok.data(), 0U));

        constexpr std::uint32_t kEightChannels = 8U;
        constexpr auto kMaxFramesForEightChannels =
            std::numeric_limits<std::size_t>::max() / kEightChannels;
        CHECK(kMaxFramesForEightChannels * kEightChannels <=
              std::numeric_limits<std::size_t>::max());
        CHECK(kMaxFramesForEightChannels + 1U >
              std::numeric_limits<std::size_t>::max() / kEightChannels);

        const PeqFilterV1 stateful_filter{1000.0, 6.0, 1.0};
        Processor baseline;
        Processor candidate;
        CHECK(baseline.prepare(std::span(&stateful_filter, 1U), 48000U,
                               kEightChannels));
        CHECK(candidate.prepare(std::span(&stateful_filter, 1U), 48000U,
                                kEightChannels));

        const std::array<float, kEightChannels> warmup_input{
            0.25F, -0.5F, 0.75F, -0.125F, 0.5F, -0.25F, 0.125F, -0.875F};
        auto baseline_warmup = warmup_input;
        auto candidate_warmup = warmup_input;
        CHECK(baseline.process_interleaved(baseline_warmup.data(), 1U));
        CHECK(candidate.process_interleaved(candidate_warmup.data(), 1U));

        std::array<float, kEightChannels> overflow_guard{1.0F, 2.0F, 3.0F, 4.0F,
                                                         5.0F, 6.0F, 7.0F, 8.0F};
        const auto untouched = overflow_guard;
        CHECK(!candidate.process_interleaved(
            overflow_guard.data(), kMaxFramesForEightChannels + 1U));
        CHECK(overflow_guard == untouched);

        auto baseline_next = warmup_input;
        auto candidate_next = warmup_input;
        CHECK(baseline.process_interleaved(baseline_next.data(), 1U));
        CHECK(candidate.process_interleaved(candidate_next.data(), 1U));
        CHECK(candidate_next == baseline_next);
    }

    // ---- non-finite sanitization -------------------------------------------
    {
        constexpr std::size_t kFrames = 64U;
        Processor processor;
        const PeqFilterV1 boost{1000.0, 6.0, 1.0};
        CHECK(processor.prepare(std::span(&boost, 1U), 48000U, 1U));

        std::vector<float> poisoned(kFrames, 0.25F);
        poisoned[10U] = std::numeric_limits<float>::quiet_NaN();
        CHECK(processor.process_interleaved(poisoned.data(), kFrames));
        for (const auto sample : poisoned) {
            CHECK(std::isfinite(sample));
        }
    }

    std::fputs("peq dsp tests passed\n", stdout);
    return 0;
}
