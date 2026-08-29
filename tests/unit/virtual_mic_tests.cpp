// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/virtual_mic.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <thread>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::VirtualMicConfigV1;
using hibiki::VirtualMicDspPolicyV1;
using hibiki::VirtualMicDspV1;
using hibiki::VirtualMicRouteModel;

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

float residual_after(const std::size_t blocks,
                     const VirtualMicDspPolicyV1& policy) {
    VirtualMicDspV1 dsp;
    CHECK(dsp.prepare(policy, 1U, 48000U));
    std::array<float, 64> capture{};
    std::array<float, 64> reference{};
    std::array<float, 64> output{};
    for (std::size_t frame = 0; frame < capture.size(); ++frame) {
        reference[frame] = 0.25F * static_cast<float>(frame % 8);
        capture[frame] = 0.5F * reference[frame];
    }
    for (std::size_t block = 0; block < blocks; ++block) {
        CHECK(dsp.process(capture.data(), reference.data(), output.data(),
                          capture.size()));
    }
    float energy = 0.0F;
    for (const float value : output) {
        energy += value * value;
    }
    return energy;
}

}  // namespace

int main() {
    // Interleaved sample-count arithmetic must fail closed before any
    // caller-owned buffer or DSP state is touched.
    {
        constexpr std::size_t kOverflowFrames =
            std::numeric_limits<std::size_t>::max() / 2U + 1U;
        VirtualMicDspV1 dsp;
        CHECK(dsp.prepare(VirtualMicDspPolicyV1{}, 2U, 48000U));
        std::array<float, 2> input{0.25F, 0.5F};
        std::array<float, 2> output{7.0F, 7.0F};
        CHECK(!dsp.process(input.data(), nullptr, output.data(),
                           kOverflowFrames));
        CHECK(output[0] == 7.0F && output[1] == 7.0F);

        VirtualMicRouteModel route;
        CHECK(route.prepare(VirtualMicConfigV1{2U, 48000U, true}));
        std::array<float, 2> reference{0.75F, 1.0F};
        output = {7.0F, 7.0F};
        CHECK(!route.process_capture(input.data(), output.data(),
                                      kOverflowFrames));
        CHECK(output[0] == 7.0F && output[1] == 7.0F);
        CHECK(!route.process_capture_with_reference(
            input.data(), reference.data(), output.data(), kOverflowFrames));
        CHECK(output[0] == 7.0F && output[1] == 7.0F);
        reference = {7.0F, 7.0F};
        CHECK(!route.process_echo_reference(input.data(), reference.data(),
                                            kOverflowFrames));
        CHECK(reference[0] == 7.0F && reference[1] == 7.0F);
    }

    // prepare: channel/sample-rate/filter bounds and policy validation are
    // fail-closed; the boundary values themselves stay accepted.
    {
        VirtualMicDspPolicyV1 policy{};
        VirtualMicDspV1 dsp;
        CHECK(!dsp.prepared());
        CHECK(!dsp.prepare(policy, 0U, 48000U));
        CHECK(!dsp.prepare(policy, 3U, 48000U));
        CHECK(!dsp.prepare(policy, 1U, 0U));
        policy.filter_length = 0U;
        CHECK(!dsp.prepare(policy, 1U, 48000U));
        policy.filter_length = 129U;
        CHECK(!dsp.prepare(policy, 1U, 48000U));
        policy = {};
        policy.adaptation_rate = -0.01F;
        CHECK(!dsp.prepare(policy, 1U, 48000U));
        policy.adaptation_rate = 1.01F;
        CHECK(!dsp.prepare(policy, 1U, 48000U));
        policy.adaptation_rate = std::numeric_limits<float>::quiet_NaN();
        CHECK(!dsp.prepare(policy, 1U, 48000U));
        policy = {};
        policy.noise_gate_threshold_dbfs = 1.0F;
        CHECK(!dsp.prepare(policy, 1U, 48000U));
        policy.noise_gate_threshold_dbfs = -144.5F;
        CHECK(!dsp.prepare(policy, 1U, 48000U));
        policy = {};
        policy.noise_gate_floor = -0.01F;
        CHECK(!dsp.prepare(policy, 1U, 48000U));
        policy.noise_gate_floor = 1.01F;
        CHECK(!dsp.prepare(policy, 1U, 48000U));
        policy = {};
        policy.attack_ms = 0.0F;
        CHECK(!dsp.prepare(policy, 1U, 48000U));
        policy.release_ms = -1.0F;
        CHECK(!dsp.prepare(policy, 1U, 48000U));
        policy = {};
        policy.filter_length = VirtualMicDspV1::kMaxTaps;
        CHECK(dsp.prepare(policy, VirtualMicDspV1::kMaxChannels, 48000U) &&
              dsp.prepared());
    }

    // NLMS cancellation: feeding a correlated reference drives the output
    // residual down block over block; adaptation rate 0 must leave it flat.
    {
        VirtualMicDspPolicyV1 adaptive{};
        adaptive.echo_cancellation_enabled = true;
        adaptive.adaptation_rate = 0.5F;
        const float first = residual_after(1U, adaptive);
        const float later = residual_after(24U, adaptive);
        CHECK(std::isfinite(first) && first > 0.0F);
        CHECK(later < first * 0.5F);

        VirtualMicDspPolicyV1 frozen = adaptive;
        frozen.adaptation_rate = 0.0F;
        const float frozen_first = residual_after(1U, frozen);
        const float frozen_later = residual_after(24U, frozen);
        CHECK(frozen_first > 0.0F);
        CHECK(frozen_later == frozen_first);
    }

    // process fail-closed paths zero the buffer before returning.
    {
        VirtualMicDspPolicyV1 policy{};
        VirtualMicDspV1 dsp;
        CHECK(dsp.prepare(policy, 2U, 48000U));
        std::array<float, 16> capture{};
        capture.fill(0.5F);
        std::array<float, 16> reference{};
        std::array<float, 16> output{};
        CHECK(!dsp.process(nullptr, reference.data(), output.data(), 8U));
        CHECK(!dsp.process(capture.data(), reference.data(), nullptr, 8U));
        CHECK(!dsp.process(capture.data(), reference.data(), output.data(), 0U));
        capture[3] = kNaN;
        CHECK(!dsp.process(capture.data(), nullptr, output.data(), 8U));
        bool all_zero = true;
        for (const float value : output) {
            all_zero = all_zero && value == 0.0F;
        }
        CHECK(all_zero);
    }

    // Noise gate: opens above the reopen threshold with fast attack, closes
    // below the configured threshold with slow release, and stays quiet on
    // sub-threshold input from a fresh (closed) state.
    {
        VirtualMicDspPolicyV1 gate{};
        gate.noise_gate_enabled = true;
        gate.noise_gate_threshold_dbfs = -20.0F;
        gate.noise_gate_floor = 0.05F;
        gate.attack_ms = 2.0F;
        gate.release_ms = 200.0F;
        VirtualMicDspV1 dsp;
        CHECK(dsp.prepare(gate, 1U, 48000U));

        std::array<float, 64> loud{};
        loud.fill(0.9F);
        std::array<float, 64> quiet{};
        quiet.fill(0.0001F);
        std::array<float, 64> output{};

        CHECK(dsp.process(quiet.data(), nullptr, output.data(), quiet.size()));
        CHECK(output[output.size() - 1] <= 0.0002F);

        for (int block = 0; block < 12; ++block) {
            CHECK(dsp.process(loud.data(), nullptr, output.data(), loud.size()));
        }
        CHECK(output[output.size() - 1] > 0.7F);

        for (int block = 0; block < 40; ++block) {
            CHECK(dsp.process(quiet.data(), nullptr, output.data(), quiet.size()));
        }
        CHECK(output[output.size() - 1] <= 0.02F);

        dsp.reset();
        CHECK(dsp.process(quiet.data(), nullptr, output.data(), quiet.size()));
        CHECK(output[output.size() - 1] <= 0.0002F);
    }

    // Route model: prepare validates channels/sample rates, privacy mute is
    // the default and fails closed to silence, echo reference requires both
    // sides enabled, and reset restores the muted snapshot.
    {
        VirtualMicRouteModel model;
        CHECK(model.snapshot().prepared == false &&
              model.snapshot().privacy_muted == true);

        VirtualMicConfigV1 config{};
        config.channels = 3U;
        CHECK(!model.prepare(config));
        config.channels = 2U;
        config.sample_rate = 44101U;
        CHECK(!model.prepare(config));
        config.sample_rate = 192000U;
        CHECK(model.prepare(config));
        CHECK(model.snapshot().prepared && model.snapshot().privacy_muted &&
              model.snapshot().channels == 2U &&
              model.snapshot().sample_rate == 192000U &&
              model.snapshot().echo_reference_enabled);

        std::array<float, 64> input{};
        input.fill(0.4F);
        std::array<float, 64> reference{};
        std::array<float, 64> capture{};
        constexpr std::size_t kRouteFrames = 32U;
        CHECK(model.process_capture(input.data(), capture.data(), kRouteFrames));
        bool silent = true;
        for (const float value : capture) {
            silent = silent && value == 0.0F;
        }
        CHECK(silent);
        CHECK(model.process_capture_with_reference(input.data(), reference.data(),
                                                   capture.data(), kRouteFrames));
        silent = true;
        for (const float value : capture) {
            silent = silent && value == 0.0F;
        }
        CHECK(silent);

        model.set_privacy_mute(false);
        CHECK(model.snapshot().privacy_muted == false);
        CHECK(model.process_capture(input.data(), capture.data(), kRouteFrames) &&
              capture.front() != 0.0F);
        CHECK(model.process_capture_with_reference(input.data(), reference.data(),
                                                   capture.data(), kRouteFrames));
        CHECK(model.process_capture_with_reference(input.data(), nullptr, capture.data(),
                                                   kRouteFrames) &&
              capture.front() != 0.0F);
        CHECK(model.process_echo_reference(reference.data(), capture.data(),
                                           kRouteFrames));
        bool echoed = true;
        for (std::size_t index = 0; index < capture.size(); ++index) {
            echoed = echoed && capture[index] == reference[index];
        }
        CHECK(echoed);

        model.reset();
        CHECK(model.snapshot().prepared == false &&
              model.snapshot().privacy_muted == true);

        VirtualMicConfigV1 no_reference_config{};
        no_reference_config.channels = 1U;
        no_reference_config.sample_rate = 48000U;
        no_reference_config.echo_reference_enabled = false;
        VirtualMicRouteModel no_reference_model;
        CHECK(no_reference_model.prepare(no_reference_config));
        no_reference_model.set_privacy_mute(false);
        CHECK(!no_reference_model.process_capture_with_reference(
            input.data(), reference.data(), capture.data(), kRouteFrames));
        CHECK(no_reference_model.process_capture_with_reference(
            input.data(), nullptr, capture.data(), kRouteFrames));
    }

    // Privacy mute publication is race-free across the control toggle, both
    // capture entry points, and a concurrent value snapshot. Each capture
    // block must observe one complete privacy decision: silence or the finite
    // input block, never a partially changed output.
    {
        constexpr std::size_t kFrames = 32U;
        constexpr std::size_t kSamples = kFrames * 2U;
        VirtualMicRouteModel model;
        VirtualMicConfigV1 config{};
        config.channels = 2U;
        config.sample_rate = 48000U;
        config.echo_reference_enabled = true;
        CHECK(model.prepare(config));

        std::array<float, kSamples> input{};
        std::array<float, kSamples> reference{};
        for (std::size_t index = 0U; index < kSamples; ++index) {
            input[index] = (index & 1U) == 0U ? 0.25F : -0.5F;
            reference[index] = 0.125F;
        }
        std::array<float, kSamples> output{};
        std::atomic<bool> start{false};
        std::atomic<bool> capture_done{false};
        std::atomic<bool> toggle_done{false};
        std::atomic<bool> failed{false};

        std::thread capture([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (std::size_t iteration = 0U; iteration < 512U; ++iteration) {
                output.fill(9.0F);
                const bool processed = (iteration & 1U) == 0U
                                           ? model.process_capture(input.data(), output.data(), kFrames)
                                           : model.process_capture_with_reference(
                                                 input.data(), reference.data(), output.data(), kFrames);
                if (!processed) {
                    failed.store(true, std::memory_order_release);
                    break;
                }
                bool muted_block = true;
                bool input_block = true;
                for (std::size_t index = 0U; index < kSamples; ++index) {
                    if (!std::isfinite(output[index])) {
                        failed.store(true, std::memory_order_release);
                        break;
                    }
                    muted_block = muted_block && output[index] == 0.0F;
                    input_block = input_block && output[index] == input[index];
                }
                if (failed.load(std::memory_order_acquire) || (!muted_block && !input_block)) {
                    failed.store(true, std::memory_order_release);
                    break;
                }
            }
            capture_done.store(true, std::memory_order_release);
        });

        std::thread toggler([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (std::size_t iteration = 0U; iteration < 512U; ++iteration) {
                model.set_privacy_mute((iteration & 1U) == 0U);
                std::this_thread::yield();
            }
            model.set_privacy_mute(true);
            toggle_done.store(true, std::memory_order_release);
        });

        std::thread reader([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            do {
                const auto snapshot = model.snapshot();
                if (!snapshot.prepared || !snapshot.echo_reference_enabled ||
                    snapshot.channels != 2U || snapshot.sample_rate != 48000U) {
                    failed.store(true, std::memory_order_release);
                    break;
                }
                std::this_thread::yield();
            } while (!capture_done.load(std::memory_order_acquire) ||
                     !toggle_done.load(std::memory_order_acquire));
        });

        start.store(true, std::memory_order_release);
        capture.join();
        toggler.join();
        reader.join();
        CHECK(!failed.load(std::memory_order_acquire));
        CHECK(model.snapshot().privacy_muted);
    }

    return 0;
}
