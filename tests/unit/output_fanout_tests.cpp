// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/output_fanout.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <string>
#include <thread>
#include <vector>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::OutputFanoutPlanV1;
using hibiki::OutputFanoutSinkConfigV1;

constexpr std::size_t kMaxSinks = hibiki::kOutputFanoutMaxSinksV1;
constexpr std::size_t kMaxIdBytes = hibiki::kOutputFanoutMaxIdBytesV1;

OutputFanoutPlanV1 make_valid_plan() {
    OutputFanoutPlanV1 plan{};
    plan.schema_version = 1U;
    plan.revision = 1U;
    plan.output_channels = 2U;
    plan.sink_count = 2U;
    const std::string ids[] = {"Speakers", "Headphones"};
    for (std::size_t i = 0U; i < plan.sink_count; ++i) {
        auto& sink = plan.sinks[i];
        sink.id_bytes = static_cast<std::uint8_t>(ids[i].size());
        for (std::size_t b = 0U; b < ids[i].size(); ++b) {
            sink.sink_id[b] = ids[i][b];
        }
        sink.channels = 2U;
        sink.enabled = true;
    }
    return plan;
}

std::vector<OutputFanoutSinkConfigV1> valid_configs() {
    std::vector<OutputFanoutSinkConfigV1> configs;
    configs.push_back(OutputFanoutSinkConfigV1{"Speakers", 2U, true});
    configs.push_back(OutputFanoutSinkConfigV1{"Headphones", 2U, false});
    return configs;
}

}  // namespace

int main() {
    // validate: valid stereo plan with two enabled sinks is accepted.
    {
        const auto plan = make_valid_plan();
        CHECK(hibiki::validate_output_fanout_plan_v1(plan));
    }

    // validate: schema_version != 1 rejected.
    {
        auto plan = make_valid_plan();
        plan.schema_version = 2U;
        CHECK(!hibiki::validate_output_fanout_plan_v1(plan));
    }
    // validate: revision == 0 rejected.
    {
        auto plan = make_valid_plan();
        plan.revision = 0U;
        CHECK(!hibiki::validate_output_fanout_plan_v1(plan));
    }
    // validate: unsupported channel counts rejected.
    for (const std::uint32_t bad_channels : {1U, 3U, 4U, 5U, 7U, 0U}) {
        auto plan = make_valid_plan();
        plan.output_channels = bad_channels;
        CHECK(!hibiki::validate_output_fanout_plan_v1(plan));
    }
    // validate: sink_count == 0 rejected.
    {
        auto plan = make_valid_plan();
        plan.sink_count = 0U;
        CHECK(!hibiki::validate_output_fanout_plan_v1(plan));
    }
    // validate: sink_count > max rejected.
    {
        auto plan = make_valid_plan();
        plan.sink_count = kMaxSinks + 1U;
        CHECK(!hibiki::validate_output_fanout_plan_v1(plan));
    }
    // validate: id_bytes == 0 rejected.
    {
        auto plan = make_valid_plan();
        plan.sinks[0].id_bytes = 0U;
        CHECK(!hibiki::validate_output_fanout_plan_v1(plan));
    }
    // validate: id_bytes > max rejected.
    {
        auto plan = make_valid_plan();
        plan.sinks[0].id_bytes = static_cast<std::uint8_t>(kMaxIdBytes + 1U);
        CHECK(!hibiki::validate_output_fanout_plan_v1(plan));
    }
    // validate: sink channels mismatching plan output_channels rejected.
    {
        auto plan = make_valid_plan();
        plan.sinks[1].channels = 6U;
        CHECK(!hibiki::validate_output_fanout_plan_v1(plan));
    }
    // validate: non-printable sink_id rejected (control char).
    {
        auto plan = make_valid_plan();
        plan.sinks[0].sink_id[0] = static_cast<char>(0x01);
        CHECK(!hibiki::validate_output_fanout_plan_v1(plan));
    }
    // validate: non-printable sink_id rejected (DEL).
    {
        auto plan = make_valid_plan();
        plan.sinks[0].sink_id[0] = static_cast<char>(0x7F);
        CHECK(!hibiki::validate_output_fanout_plan_v1(plan));
    }
    // validate: non-printable sink_id rejected (C1 range).
    {
        auto plan = make_valid_plan();
        plan.sinks[0].sink_id[0] = static_cast<char>(0x85);
        CHECK(!hibiki::validate_output_fanout_plan_v1(plan));
    }
    // validate: duplicate sink IDs rejected.
    {
        auto plan = make_valid_plan();
        plan.sinks[1] = plan.sinks[0];
        CHECK(!hibiki::validate_output_fanout_plan_v1(plan));
    }
    // validate: all sinks disabled rejected.
    {
        auto plan = make_valid_plan();
        for (std::size_t i = 0U; i < plan.sink_count; ++i) {
            plan.sinks[i].enabled = false;
        }
        CHECK(!hibiki::validate_output_fanout_plan_v1(plan));
    }
    // prepare: valid configs preserve order, revision and enabled flags.
    {
        OutputFanoutPlanV1 plan{};
        const auto configs = valid_configs();
        CHECK(hibiki::prepare_output_fanout_plan_v1(configs, 2U, 5U, plan));
        CHECK(plan.schema_version == 1U);
        CHECK(plan.revision == 5U);
        CHECK(plan.output_channels == 2U);
        CHECK(plan.sink_count == 2U);
        CHECK(std::string(plan.sinks[0].sink_id.data(), plan.sinks[0].id_bytes) == "Speakers");
        CHECK(plan.sinks[0].enabled);
        CHECK(std::string(plan.sinks[1].sink_id.data(), plan.sinks[1].id_bytes) == "Headphones");
        CHECK(!plan.sinks[1].enabled);
        CHECK(hibiki::validate_output_fanout_plan_v1(plan));
    }
    // prepare: empty config span rejected.
    {
        OutputFanoutPlanV1 plan{};
        const std::vector<OutputFanoutSinkConfigV1> empty{};
        CHECK(!hibiki::prepare_output_fanout_plan_v1(empty, 2U, 1U, plan));
    }
    // prepare: oversized config span rejected.
    {
        OutputFanoutPlanV1 plan{};
        std::vector<OutputFanoutSinkConfigV1> too_many;
        for (std::size_t i = 0U; i <= kMaxSinks; ++i) {
            too_many.push_back(
                OutputFanoutSinkConfigV1{"s" + std::to_string(i), 2U, true});
        }
        CHECK(!hibiki::prepare_output_fanout_plan_v1(too_many, 2U, 1U, plan));
    }
    // prepare: revision == 0 rejected.
    {
        OutputFanoutPlanV1 plan{};
        const auto configs = valid_configs();
        CHECK(!hibiki::prepare_output_fanout_plan_v1(configs, 2U, 0U, plan));
    }
    // prepare: invalid channel count rejected.
    {
        OutputFanoutPlanV1 plan{};
        const auto configs = valid_configs();
        CHECK(!hibiki::prepare_output_fanout_plan_v1(configs, 4U, 1U, plan));
    }
    // prepare: empty and oversized sink_id strings rejected.
    {
        OutputFanoutPlanV1 plan{};
        const std::vector<OutputFanoutSinkConfigV1> empty_id{
            OutputFanoutSinkConfigV1{"", 2U, true}};
        CHECK(!hibiki::prepare_output_fanout_plan_v1(empty_id, 2U, 1U, plan));
        const std::string long_id(kMaxIdBytes + 1U, 'x');
        const std::vector<OutputFanoutSinkConfigV1> oversize_id{
            OutputFanoutSinkConfigV1{long_id, 2U, true}};
        CHECK(!hibiki::prepare_output_fanout_plan_v1(oversize_id, 2U, 1U, plan));
    }
    // fanout: copies input to enabled sinks; disabled sink untouched.
    {
        auto plan = make_valid_plan();
        plan.sinks[1].enabled = false;
        alignas(16) float input[8] = {0.5F, -0.25F, 0.75F, 1.0F,
                                      -1.0F, 0.125F, -0.375F, 0.625F};
        alignas(16) float out_a[8] = {};
        alignas(16) float out_b[8] = {1.0F, 1.0F, 1.0F, 1.0F,
                                      1.0F, 1.0F, 1.0F, 1.0F};
        float* outputs[kMaxSinks] = {};
        outputs[0] = out_a;
        outputs[1] = out_b;
        const std::size_t capacities[kMaxSinks] = {8U, 8U};
        CHECK(hibiki::fanout_interleaved_v1(
            plan, input, 4U, std::span<float* const>(outputs, 2U),
            std::span<const std::size_t>(capacities, 2U)));
        for (std::size_t i = 0U; i < 8U; ++i) {
            CHECK(out_a[i] == input[i]);
            CHECK(out_b[i] == 1.0F);
        }
    }
    // persistent runtime: the maximum bounded block remains processable at
    // the minimum source step after history has crossed a block boundary.
    {
        const auto plan = make_valid_plan();
        hibiki::OutputFanoutRuntimeV1 runtime;
        CHECK(runtime.prepare(plan, 0.25));
        const auto input_samples = hibiki::kOutputFanoutMaxInputFramesV1 * 2U;
        const std::vector<float> input(input_samples, 0.25F);
        const auto output_samples =
            hibiki::kOutputFanoutMaxResampledFramesV1 * 2U;
        const auto no_history_bound =
            (hibiki::kOutputFanoutMaxInputFramesV1 * 4U) + 1U;
        std::vector<float> output_a(output_samples, 0.0F);
        std::vector<float> output_b(output_samples, 0.0F);
        float* outputs[2] = {output_a.data(), output_b.data()};
        const std::size_t capacities[2] = {
            hibiki::kOutputFanoutMaxResampledFramesV1,
            hibiki::kOutputFanoutMaxResampledFramesV1};
        std::size_t output_frames[2] = {};
        for (std::size_t block = 0U; block < 2U; ++block) {
            CHECK(runtime.process(
                input.data(), hibiki::kOutputFanoutMaxInputFramesV1,
                std::span<float* const>(outputs, 2U),
                std::span<const std::size_t>(capacities, 2U),
                std::span<std::size_t>(output_frames, 2U)));
            CHECK(output_frames[0] > 0U &&
                  output_frames[0] <= hibiki::kOutputFanoutMaxResampledFramesV1 &&
                  output_frames[1] == output_frames[0]);
            if (block == 1U) {
                CHECK(output_frames[0] > no_history_bound);
            }
        }
    }

    // fanout: the standalone helper accepts the same bounded maximum as the
    // persistent runtime and copies a full maximum-size block.
    {
        const auto plan = make_valid_plan();
        const auto samples = hibiki::kOutputFanoutMaxInputFramesV1 * 2U;
        const std::vector<float> input(samples, 0.25F);
        std::vector<float> out_a(samples, 0.0F);
        std::vector<float> out_b(samples, 0.0F);
        float* outputs[kMaxSinks] = {out_a.data(), out_b.data()};
        const std::size_t capacities[kMaxSinks] = {
            hibiki::kOutputFanoutMaxInputFramesV1,
            hibiki::kOutputFanoutMaxInputFramesV1};
        CHECK(hibiki::fanout_interleaved_v1(
            plan, input.data(), hibiki::kOutputFanoutMaxInputFramesV1,
            std::span<float* const>(outputs, 2U),
            std::span<const std::size_t>(capacities, 2U)));
        CHECK(out_a == input);
        CHECK(out_b == input);
    }
    // fanout: an oversized standalone block is rejected before any read or
    // destination write, preserving all-or-nothing behavior.
    {
        const auto plan = make_valid_plan();
        const float input[2] = {0.25F, -0.25F};
        float out_a[2] = {7.0F, 7.0F};
        float out_b[2] = {9.0F, 9.0F};
        float* outputs[kMaxSinks] = {out_a, out_b};
        const std::size_t capacities[kMaxSinks] = {2U, 2U};
        CHECK(!hibiki::fanout_interleaved_v1(
            plan, input, hibiki::kOutputFanoutMaxInputFramesV1 + 1U,
            std::span<float* const>(outputs, 2U),
            std::span<const std::size_t>(capacities, 2U)));
        CHECK(out_a[0] == 7.0F && out_a[1] == 7.0F &&
              out_b[0] == 9.0F && out_b[1] == 9.0F);
    }
    // fanout: null enabled-sink pointer rejected without partial writes.
    {
        const auto plan = make_valid_plan();
        alignas(16) float input[4] = {0.0F, 0.0F, 0.0F, 0.0F};
        alignas(16) float out_a[4] = {7.0F, 7.0F, 7.0F, 7.0F};
        float* outputs[kMaxSinks] = {};
        outputs[0] = out_a;
        outputs[1] = nullptr;
        const std::size_t capacities[kMaxSinks] = {4U, 4U};
        CHECK(!hibiki::fanout_interleaved_v1(
            plan, input, 2U, std::span<float* const>(outputs, 2U),
            std::span<const std::size_t>(capacities, 2U)));
        for (const float sample : out_a) {
            CHECK(sample == 7.0F);
        }
    }
    // fanout: insufficient capacity on any enabled sink rejected.
    {
        const auto plan = make_valid_plan();
        alignas(16) float input[4] = {0.0F, 0.0F, 0.0F, 0.0F};
        alignas(16) float out_a[4] = {9.0F, 9.0F, 9.0F, 9.0F};
        alignas(16) float out_b[4] = {};
        float* outputs[kMaxSinks] = {};
        outputs[0] = out_a;
        outputs[1] = out_b;
        const std::size_t capacities[kMaxSinks] = {4U, 1U};
        CHECK(!hibiki::fanout_interleaved_v1(
            plan, input, 2U, std::span<float* const>(outputs, 2U),
            std::span<const std::size_t>(capacities, 2U)));
        for (const float sample : out_a) {
            CHECK(sample == 9.0F);
        }
    }

    // fanout: non-finite input anywhere rejects all-or-nothing.
    {
        const auto plan = make_valid_plan();
        alignas(16) float input[8] = {0.5F, 0.5F, 0.5F, 0.5F,
                                      0.5F, 0.5F, 0.5F,
                                      std::numeric_limits<float>::quiet_NaN()};
        alignas(16) float out_a[8] = {3.0F, 3.0F, 3.0F, 3.0F,
                                      3.0F, 3.0F, 3.0F, 3.0F};
        alignas(16) float out_b[8] = {3.0F, 3.0F, 3.0F, 3.0F,
                                      3.0F, 3.0F, 3.0F, 3.0F};
        float* outputs[kMaxSinks] = {};
        outputs[0] = out_a;
        outputs[1] = out_b;
        const std::size_t capacities[kMaxSinks] = {8U, 8U};
        CHECK(!hibiki::fanout_interleaved_v1(
            plan, input, 4U, std::span<float* const>(outputs, 2U),
            std::span<const std::size_t>(capacities, 2U)));
        for (const float sample : out_a) {
            CHECK(sample == 3.0F);
        }
        for (const float sample : out_b) {
            CHECK(sample == 3.0F);
        }
    }

    // fanout: outputs/capacities span shorter than sink_count rejected.
    {
        const auto plan = make_valid_plan();
        alignas(16) float input[4] = {0.0F, 0.0F, 0.0F, 0.0F};
        alignas(16) float out_a[4] = {};
        alignas(16) float out_b[4] = {};
        float* outputs[kMaxSinks] = {};
        outputs[0] = out_a;
        outputs[1] = out_b;
        const std::size_t capacities[kMaxSinks] = {4U, 4U};
        CHECK(!hibiki::fanout_interleaved_v1(
            plan, input, 2U, std::span<float* const>(outputs, 1U),
            std::span<const std::size_t>(capacities, 2U)));
        CHECK(!hibiki::fanout_interleaved_v1(
            plan, input, 2U, std::span<float* const>(outputs, 2U),
            std::span<const std::size_t>(capacities, 1U)));
    }

    // persistent runtime: clock observations are published across the
    // control/audio boundary, capacity rejection rolls back the observation,
    // and concurrent observation/process/snapshot activity stays finite and
    // internally coherent.
    {
        const auto plan = make_valid_plan();
        hibiki::OutputFanoutRuntimeV1 runtime;
        CHECK(runtime.prepare(plan, 1.0));

        std::array<float, 256> input{};
        input.fill(0.25F);
        std::array<float, 512> rollback_a{};
        std::array<float, 512> rollback_b{};
        float* rollback_outputs[2] = {rollback_a.data(), rollback_b.data()};
        const std::size_t no_capacity[2] = {256U, 0U};
        std::size_t rollback_frames[2] = {99U, 99U};
        const auto neutral = runtime.snapshot();
        CHECK(neutral.prepared && neutral.sinks[0].prepared &&
              neutral.sinks[0].ratio == 1.0 &&
              neutral.sinks[0].source_step == 1.0);
        std::size_t first_sink_frames[2] = {99U, 99U};
        const std::size_t first_sink_no_capacity[2] = {0U, 256U};
        CHECK(!runtime.process(
            input.data(), input.size() / 2U,
            std::span<float* const>(rollback_outputs, 2U),
            std::span<const std::size_t>(first_sink_no_capacity, 2U),
            std::span<std::size_t>(first_sink_frames, 2U)));
        CHECK(first_sink_frames[0] == 0U && first_sink_frames[1] == 0U);
        CHECK(runtime.observe_clock(0U, 48000.0, 48012.0, 1.0));
        CHECK(!runtime.process(
            input.data(), input.size() / 2U,
            std::span<float* const>(rollback_outputs, 2U),
            std::span<const std::size_t>(no_capacity, 2U),
            std::span<std::size_t>(rollback_frames, 2U)));
        const auto after_rollback = runtime.snapshot();
        CHECK(after_rollback.sinks[0].ratio == neutral.sinks[0].ratio &&
              after_rollback.sinks[0].source_step == neutral.sinks[0].source_step &&
              rollback_frames[0] == 0U && rollback_frames[1] == 0U);

        // Rejected input validation must clear metadata when the caller
        // supplied a complete output_frames span.
        const auto check_rejected_input = [&](const float* rejected_input,
                                              const std::size_t rejected_frames) {
            rollback_frames[0] = 17U;
            rollback_frames[1] = 23U;
            const bool rejected = !runtime.process(
                rejected_input, rejected_frames,
                std::span<float* const>(rollback_outputs, 2U),
                std::span<const std::size_t>(no_capacity, 2U),
                std::span<std::size_t>(rollback_frames, 2U));
            return rejected && rollback_frames[0] == 0U &&
                   rollback_frames[1] == 0U;
        };
        CHECK(check_rejected_input(nullptr, input.size() / 2U));
        CHECK(check_rejected_input(input.data(), 0U));
        CHECK(check_rejected_input(
            input.data(), hibiki::kOutputFanoutMaxInputFramesV1 + 1U));

        std::atomic<bool> failed{false};
        std::thread audio([&runtime, &input, &failed]() {
            std::array<float, 512> output_a{};
            std::array<float, 512> output_b{};
            float* outputs[2] = {output_a.data(), output_b.data()};
            const std::size_t capacities[2] = {256U, 256U};
            std::size_t output_frames[2] = {};
            for (std::size_t iteration = 0U; iteration < 1024U; ++iteration) {
                if (!runtime.process(
                        input.data(), input.size() / 2U,
                        std::span<float* const>(outputs, 2U),
                        std::span<const std::size_t>(capacities, 2U),
                        std::span<std::size_t>(output_frames, 2U))) {
                    failed.store(true, std::memory_order_release);
                    return;
                }
                for (std::size_t sink = 0U; sink < 2U; ++sink) {
                    if (output_frames[sink] == 0U ||
                        output_frames[sink] > capacities[sink]) {
                        failed.store(true, std::memory_order_release);
                        return;
                    }
                    const auto sample_count = output_frames[sink] * 2U;
                    const auto* output = sink == 0U ? output_a.data() : output_b.data();
                    for (std::size_t sample = 0U; sample < sample_count; ++sample) {
                        if (!std::isfinite(output[sample])) {
                            failed.store(true, std::memory_order_release);
                            return;
                        }
                    }
                }
                if ((iteration & 15U) == 0U) {
                    std::this_thread::yield();
                }
            }
        });
        std::thread control([&runtime, &failed]() {
            for (std::size_t iteration = 0U; iteration < 1024U; ++iteration) {
                const double sink_frames = (iteration & 1U) == 0U ? 48012.0 : 47988.0;
                if (!runtime.observe_clock(0U, 48000.0, sink_frames, 1.0)) {
                    failed.store(true, std::memory_order_release);
                    return;
                }
                const auto snapshot = runtime.snapshot();
                if (!snapshot.prepared) {
                    failed.store(true, std::memory_order_release);
                    return;
                }
                for (std::size_t sink = 0U; sink < 2U; ++sink) {
                    const auto& clock = snapshot.sinks[sink];
                    if (!clock.prepared) {
                        continue;
                    }
                    const double expected_step = 1.0 / clock.ratio;
                    if (!std::isfinite(clock.ratio) ||
                        !std::isfinite(clock.drift_ppm) ||
                        !std::isfinite(clock.source_step) ||
                        clock.ratio < 1.0 - 500.0e-6 ||
                        clock.ratio > 1.0 + 500.0e-6 ||
                        clock.drift_ppm < -500.0 || clock.drift_ppm > 500.0 ||
                        clock.source_step < 0.25 || clock.source_step > 4.0 ||
                        std::abs(clock.source_step - expected_step) > 1.0e-12 ||
                        std::abs(clock.drift_ppm - ((clock.ratio - 1.0) * 1.0e6)) >
                            1.0e-9) {
                        failed.store(true, std::memory_order_release);
                        return;
                    }
                }
                if ((iteration & 15U) == 0U) {
                    std::this_thread::yield();
                }
            }
        });
        audio.join();
        control.join();
        CHECK(!failed.load(std::memory_order_acquire));

        std::array<float, 512> final_a{};
        std::array<float, 512> final_b{};
        float* final_outputs[2] = {final_a.data(), final_b.data()};
        const std::size_t final_capacities[2] = {256U, 256U};
        std::size_t final_frames[2] = {};
        CHECK(runtime.process(
            input.data(), input.size() / 2U,
            std::span<float* const>(final_outputs, 2U),
            std::span<const std::size_t>(final_capacities, 2U),
            std::span<std::size_t>(final_frames, 2U)));
        const auto final_snapshot = runtime.snapshot();
        CHECK(final_snapshot.prepared && final_snapshot.sinks[0].prepared &&
              std::isfinite(final_snapshot.sinks[0].ratio) &&
              std::isfinite(final_snapshot.sinks[0].source_step) &&
              std::abs(final_snapshot.sinks[0].ratio - 1.0) > 1.0e-12);

        // Reset invalidates the active tokens but preserves any in-flight
        // reader hazard before publishing the neutral snapshots again.
        runtime.reset();
        const auto reset_snapshot = runtime.snapshot();
        CHECK(reset_snapshot.prepared && reset_snapshot.sinks[0].prepared &&
              reset_snapshot.sinks[0].ratio == 1.0 &&
              reset_snapshot.sinks[0].source_step == 1.0);
    }

    // The request and snapshot protocols must preserve complete
    // generation-distinct tuples, not merely mathematically related fields.
    // Coordinate one request with one audio process at a time so the expected
    // complete generations are independently reproducible, while a third
    // reader continuously samples the published snapshot concurrently.
    {
        const auto plan = make_valid_plan();
        hibiki::OutputFanoutRuntimeV1 runtime;
        CHECK(runtime.prepare(plan, 1.0));

        constexpr std::size_t kTrackedGenerations = 128U;
        std::array<hibiki::OutputSinkClockSnapshotV1,
                   kTrackedGenerations + 1U>
            expected{};
        expected[0] = hibiki::OutputSinkClockSnapshotV1{
            1.0, 0.0, 1.0, true};
        double expected_ratio = 1.0;
        for (std::size_t generation = 1U;
             generation <= kTrackedGenerations; ++generation) {
            const auto phase = static_cast<int>((generation - 1U) % 16U) - 8;
            const double source_frames = 48000.0 + phase;
            const double sink_frames = 48000.0 - phase;
            const double target = std::clamp(
                sink_frames / source_frames, 1.0 - 500.0e-6,
                1.0 + 500.0e-6);
            expected_ratio = std::clamp(
                (expected_ratio * 0.99) + (target * 0.01),
                1.0 - 500.0e-6, 1.0 + 500.0e-6);
            expected[generation] = hibiki::OutputSinkClockSnapshotV1{
                expected_ratio, (expected_ratio - 1.0) * 1.0e6,
                1.0 / expected_ratio, true};
        }

        std::array<float, 256> input{};
        input.fill(0.25F);
        std::atomic<std::size_t> requested_generation{0U};
        std::atomic<std::size_t> completed_generation{0U};
        std::atomic<bool> exact_stop{false};
        std::atomic<bool> exact_failed{false};

        std::thread exact_audio([&] {
            std::array<float, 512> output_a{};
            std::array<float, 512> output_b{};
            float* outputs[2] = {output_a.data(), output_b.data()};
            const std::size_t capacities[2] = {256U, 256U};
            std::size_t output_frames[2] = {};
            for (std::size_t generation = 1U;
                 generation <= kTrackedGenerations; ++generation) {
                while (!exact_stop.load(std::memory_order_acquire) &&
                       requested_generation.load(std::memory_order_acquire) <
                           generation) {
                    std::this_thread::yield();
                }
                if (exact_stop.load(std::memory_order_acquire)) return;
                if (!runtime.process(
                        input.data(), input.size() / 2U,
                        std::span<float* const>(outputs, 2U),
                        std::span<const std::size_t>(capacities, 2U),
                        std::span<std::size_t>(output_frames, 2U))) {
                    exact_failed.store(true, std::memory_order_release);
                    exact_stop.store(true, std::memory_order_release);
                    return;
                }
                completed_generation.store(generation,
                                          std::memory_order_release);
            }
        });

        std::thread exact_reader([&] {
            while (!exact_stop.load(std::memory_order_acquire)) {
                const auto snapshot = runtime.snapshot();
                if (!snapshot.prepared || !snapshot.sinks[0].prepared) {
                    continue;  // read_clock_snapshot failed closed mid-publish.
                }
                bool complete_generation = false;
                for (const auto& candidate : expected) {
                    if (snapshot.sinks[0].ratio == candidate.ratio &&
                        snapshot.sinks[0].drift_ppm == candidate.drift_ppm &&
                        snapshot.sinks[0].source_step == candidate.source_step &&
                        snapshot.sinks[0].prepared == candidate.prepared) {
                        complete_generation = true;
                        break;
                    }
                }
                if (!complete_generation) {
                    exact_failed.store(true, std::memory_order_release);
                    exact_stop.store(true, std::memory_order_release);
                    return;
                }
            }
        });

        for (std::size_t generation = 1U;
             generation <= kTrackedGenerations; ++generation) {
            if (exact_failed.load(std::memory_order_acquire)) break;
            const auto phase = static_cast<int>((generation - 1U) % 16U) - 8;
            if (!runtime.observe_clock(0U, 48000.0 + phase,
                                       48000.0 - phase, 1.0)) {
                exact_failed.store(true, std::memory_order_release);
                exact_stop.store(true, std::memory_order_release);
                break;
            }
            requested_generation.store(generation,
                                      std::memory_order_release);
            while (!exact_stop.load(std::memory_order_acquire) &&
                   !exact_failed.load(std::memory_order_acquire) &&
                   completed_generation.load(std::memory_order_acquire) <
                       generation) {
                std::this_thread::yield();
            }
        }
        exact_stop.store(true, std::memory_order_release);
        exact_audio.join();
        exact_reader.join();
        CHECK(!exact_failed.load(std::memory_order_acquire));
    }

    std::fputs("output_fanout_tests passed\n", stdout);
    return 0;
}
