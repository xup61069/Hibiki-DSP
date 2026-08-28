// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/output_fanout.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <string>
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

    std::fputs("output_fanout_tests passed\n", stdout);
    return 0;
}
