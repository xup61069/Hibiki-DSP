// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/output_sink.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::ClockDriftEstimator;
using hibiki::InterleavedRingBuffer;
using hibiki::linear_resample_interleaved;
using hibiki::OutputSinkModel;
using hibiki::PersistentPolyphaseResampler;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

}  // namespace

int main() {
    // Ring buffer: constructor validation is fail-closed for degenerate
    // channel counts and undersized storage.
    {
        std::array<float, 8> storage{};
        const InterleavedRingBuffer zero_channels(std::span<float>(storage), 0U);
        CHECK(!zero_channels.valid());
        const InterleavedRingBuffer too_many_channels(std::span<float>(storage), 9U);
        CHECK(!too_many_channels.valid());
        std::array<float, 3> tiny{};
        const InterleavedRingBuffer subminimal(std::span<float>(tiny), 2U);
        CHECK(!subminimal.valid());
        const InterleavedRingBuffer empty_storage(std::span<float>{}, 2U);
        CHECK(!empty_storage.valid());
        const InterleavedRingBuffer mono(std::span<float>(storage), 1U);
        CHECK(mono.valid() && mono.capacity_frames() == storage.size());
    }

    // Ring buffer: null pointers and over/underflow are rejected without
    // mutating counters; data survives a write-index wraparound intact.
    {
        std::array<float, 8> storage{};
        InterleavedRingBuffer ring(std::span<float>(storage), 2U);
        CHECK(ring.valid() && ring.capacity_frames() == 4U);
        float frame[2] = {1.0F, 2.0F};
        CHECK(!ring.push(nullptr, 1U));
        CHECK(!ring.pop(nullptr, 1U));
        CHECK(!ring.pop(frame, 1U) && ring.available_frames() == 0U);

        const float first_three[6] = {1.0F, 1.5F, 2.0F, 2.5F, 3.0F, 3.5F};
        CHECK(ring.push(first_three, 3U) && ring.available_frames() == 3U &&
              ring.free_frames() == 1U);
        CHECK(!ring.push(first_three, 2U) && ring.available_frames() == 3U);
        float drained[4]{};
        CHECK(ring.pop(drained, 2U));
        CHECK(drained[0] == 1.0F && drained[1] == 1.5F && drained[2] == 2.0F &&
              drained[3] == 2.5F);

        const float wrapped[6] = {4.0F, 4.5F, 5.0F, 5.5F, 6.0F, 6.5F};
        CHECK(ring.push(wrapped, 3U) && ring.available_frames() == 4U &&
              ring.free_frames() == 0U);
        CHECK(!ring.push(wrapped, 1U) && ring.available_frames() == 4U);
        float sequence[8]{};
        CHECK(ring.pop(sequence, 4U) && ring.available_frames() == 0U);
        CHECK(sequence[0] == 3.0F && sequence[1] == 3.5F && sequence[2] == 4.0F &&
              sequence[3] == 4.5F && sequence[4] == 5.0F && sequence[5] == 5.5F &&
              sequence[6] == 6.0F && sequence[7] == 6.5F);

        ring.clear();
        CHECK(ring.available_frames() == 0U && ring.free_frames() == 4U);
        CHECK(ring.push(first_three, 1U) && ring.pop(sequence, 1U) &&
              sequence[0] == 1.0F && sequence[1] == 1.5F);
    }

    // Clock drift estimator: EMA moves monotonically toward the observed
    // ratio, invalid observations are complete no-ops, and extreme clocks
    // stay clamped to +/-500 ppm.
    {
        ClockDriftEstimator drift;
        CHECK(drift.ratio() == 1.0 && drift.drift_ppm() == 0.0);
        drift.observe(kNaN, 48000.0, 1.0);
        drift.observe(48000.0, kNaN, 1.0);
        drift.observe(48000.0, 48000.0, kNaN);
        drift.observe(48000.0, 48000.0, 0.0);
        drift.observe(0.0, 48000.0, 1.0);
        drift.observe(48000.0, -1.0, 1.0);
        CHECK(drift.ratio() == 1.0 && drift.drift_ppm() == 0.0);

        drift.observe(48000.0, 48012.0, 1.0);
        const double after_one = drift.ratio();
        CHECK(after_one > 1.0 && after_one < 48012.0 / 48000.0);
        drift.observe(48000.0, 48012.0, 1.0);
        CHECK(drift.ratio() > after_one && drift.ratio() < 48012.0 / 48000.0);
        CHECK(drift.drift_ppm() > 0.0 && drift.drift_ppm() <= 500.0);

        for (int iteration = 0; iteration < 256; ++iteration) {
            drift.observe(1000.0, 4000.0, 1.0);
        }
        CHECK(drift.ratio() <= 1.0 + 500.0e-6);
        for (int iteration = 0; iteration < 256; ++iteration) {
            drift.observe(4000.0, 1000.0, 1.0);
        }
        CHECK(drift.ratio() >= 1.0 - 500.0e-6 && drift.drift_ppm() < 0.0);
        drift.reset();
        CHECK(drift.ratio() == 1.0 && drift.drift_ppm() == 0.0);
    }

    // Linear resampler: argument validation is fail-closed, identity step
    // copies, fractional steps interpolate per channel independently, and
    // reads never pass the end of the input.
    {
        const float input[4] = {0.0F, 1.0F, 2.0F, 3.0F};
        float output[8]{};
        CHECK(!linear_resample_interleaved(nullptr, 4U, output, 2U, 1U, 1.0));
        CHECK(!linear_resample_interleaved(input, 4U, nullptr, 2U, 1U, 1.0));
        CHECK(!linear_resample_interleaved(input, 0U, output, 2U, 1U, 1.0));
        CHECK(!linear_resample_interleaved(input, 4U, output, 0U, 1U, 1.0));
        CHECK(!linear_resample_interleaved(input, 4U, output, 2U, 0U, 1.0));
        CHECK(!linear_resample_interleaved(input, 4U, output, 2U, 9U, 1.0));
        CHECK(!linear_resample_interleaved(input, 4U, output, 2U, 1U, 0.0));
        CHECK(!linear_resample_interleaved(input, 4U, output, 2U, 1U, -1.0));
        CHECK(!linear_resample_interleaved(input, 4U, output, 2U, 1U, kNaN));

        CHECK(linear_resample_interleaved(input, 4U, output, 4U, 1U, 1.0));
        CHECK(output[0] == 0.0F && output[1] == 1.0F && output[2] == 2.0F &&
              output[3] == 3.0F);

        const float stereo[6] = {1.0F, 10.0F, 2.0F, 20.0F, 3.0F, 30.0F};
        float stereo_out[6]{};
        CHECK(linear_resample_interleaved(stereo, 3U, stereo_out, 3U, 2U, 1.0));
        CHECK(stereo_out[0] == 1.0F && stereo_out[1] == 10.0F &&
              stereo_out[2] == 2.0F && stereo_out[3] == 20.0F &&
              stereo_out[4] == 3.0F && stereo_out[5] == 30.0F);

        const float halfway_input[4] = {0.0F, 2.0F, 4.0F, 6.0F};
        float interpolated[2]{};
        CHECK(linear_resample_interleaved(halfway_input, 4U, interpolated, 2U, 1U, 1.5));
        CHECK(std::abs(interpolated[0] - 0.0F) < 1e-6F &&
              std::abs(interpolated[1] - 3.0F) < 1e-6F);

        float upsampled[4]{};
        CHECK(linear_resample_interleaved(halfway_input, 2U, upsampled, 3U, 1U, 0.5));
        CHECK(std::abs(upsampled[0] - 0.0F) < 1e-6F &&
              std::abs(upsampled[1] - 1.0F) < 1e-6F &&
              std::abs(upsampled[2] - 2.0F) < 1e-6F);

        const float single[2] = {7.0F, 9.0F};
        CHECK(!linear_resample_interleaved(single, 1U, output, 2U, 1U, 2.0));
    }

    // Persistent polyphase resampler: exact step bounds are accepted,
    // required_output_frames is a conservative capacity bound (actual output
    // may be smaller), insufficient bound fails closed, and a constant input
    // converges to unity gain.
    {
        PersistentPolyphaseResampler resampler;
        constexpr std::uint32_t kChannels = 2U;
        CHECK(!resampler.prepare(kChannels, 0.249999));
        CHECK(!resampler.prepare(kChannels, 4.000001));
        CHECK(resampler.prepare(kChannels, 0.25));
        CHECK(resampler.source_step() == 0.25);
        CHECK(resampler.prepare(kChannels, 4.0));
        CHECK(resampler.source_step() == 4.0);
        CHECK(resampler.prepare(kChannels, 1.0));

        CHECK(resampler.required_output_frames(0U) == 0U);
        std::array<float, 128> input{};
        input.fill(1.0F);
        std::array<float, 256> output{};
        std::size_t output_frames = 0U;

        const std::size_t required_first =
            resampler.required_output_frames(input.size());
        CHECK(required_first > 0U);
        CHECK(!resampler.process(input.data(), input.size(), output.data(),
                                 required_first - 1U, output_frames));
        CHECK(output_frames == 0U);
        CHECK(resampler.process(input.data(), input.size(), output.data(),
                                required_first, output_frames));
        CHECK(output_frames > 0U && output_frames <= required_first);
        CHECK(std::all_of(output.begin(),
                          output.begin() + static_cast<std::ptrdiff_t>(
                                                output_frames * kChannels),
                          [](const float value) { return std::isfinite(value); }));

        CHECK(!resampler.set_source_step(0.1) && resampler.source_step() == 1.0);
        CHECK(!resampler.set_source_step(kNaN) && resampler.source_step() == 1.0);
        CHECK(resampler.set_source_step(0.5) && resampler.source_step() == 0.5);
        CHECK(resampler.set_source_step(1.0));

        for (int block = 0; block < 6; ++block) {
            const std::size_t required =
                resampler.required_output_frames(input.size());
            CHECK(required > 0U);
            CHECK(resampler.process(input.data(), input.size(), output.data(),
                                    required, output_frames));
            CHECK(output_frames > 0U && output_frames <= required);
        }
        const auto begin = output.begin();
        const auto end = output.begin() +
            static_cast<std::ptrdiff_t>(output_frames * kChannels);
        const bool converged = std::all_of(begin, end, [](const float value) {
            return std::abs(value - 1.0F) < 0.05F;
        });
        CHECK(converged);
    }

    // Output sink model: unprepared instances fail closed everywhere,
    // invalid prepares never arm the snapshot, and clock adaptation steers
    // the effective source step in the right direction before reset
    // restores the base step.
    {
        OutputSinkModel model;
        std::array<float, 64> input{};
        input.fill(0.5F);
        std::array<float, 64> output{};
        std::size_t output_frames = 0U;
        CHECK(model.snapshot().prepared == false);
        CHECK(!model.process(input.data(), input.size(), output.data(),
                             output.size(), output_frames));
        CHECK(model.required_output_frames(16U) == 0U);
        model.observe_clock(48000.0, 48012.0, 1.0);
        CHECK(model.snapshot().prepared == false &&
              model.snapshot().ratio == 1.0);

        CHECK(!model.prepare(0U, 1.0));
        CHECK(!model.prepare(9U, 1.0));
        CHECK(!model.prepare(2U, 0.2));
        CHECK(!model.prepare(2U, 4.5));
        CHECK(!model.prepare(2U, kNaN));
        CHECK(model.snapshot().prepared == false);

        CHECK(model.prepare(2U, 1.0));
        CHECK(model.snapshot().prepared && model.snapshot().ratio == 1.0 &&
              model.snapshot().drift_ppm == 0.0 &&
              model.snapshot().source_step == 1.0);

        model.observe_clock(48000.0, 48012.0, 1.0);
        const double fast_ratio = model.snapshot().ratio;
        CHECK(fast_ratio > 1.0 && model.snapshot().drift_ppm > 0.0 &&
              model.snapshot().source_step < 1.0);
        const std::size_t adapted_required =
            model.required_output_frames(input.size());
        CHECK(adapted_required > 0U);
        CHECK(model.process(input.data(), input.size(), output.data(),
                            adapted_required, output_frames));
        CHECK(output_frames > 0U && output_frames <= adapted_required);
        CHECK(!model.process(input.data(), input.size(), output.data(),
                             adapted_required - 1U, output_frames));

        model.reset();
        CHECK(model.snapshot().prepared && model.snapshot().ratio == 1.0 &&
              model.snapshot().drift_ppm == 0.0 &&
              model.snapshot().source_step == 1.0);
    }

    return 0;
}
