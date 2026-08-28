// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/audio_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

constexpr std::size_t kMaxCrossfadeFrames = 23040U;
constexpr std::size_t kBlockFrames = 4096U;
constexpr std::uint32_t kChannels = 8U;

}  // namespace

int run_audio_engine_loudness_geometry_tests() {
    auto engine = std::make_unique<hibiki::AudioEngineModel>();
    hibiki::GraphConfigV1 graph;
    graph.output_channels = kChannels;
    graph.lanes.push_back(
        hibiki::LaneConfigV1{"loudness-geometry", "main", kChannels, 0.0, true});
    CHECK(engine->prepare_graph(graph, 1U));
    CHECK(engine->commit_graph());
    engine->set_sample_rate(192000U);

    const std::array<hibiki::EqualLoudnessFormulaPointV1, 3> points{{
        {100.0, 0.35, 50.0, 0.0},
        {1000.0, 0.30, 2.4, 0.0},
        {8000.0, 0.25, 50.0, 0.0},
    }};
    hibiki::EqualLoudnessPolicyV1 policy{};
    policy.reference_phon = 80.0;
    policy.strength = 1.0;
    policy.max_boost_db = 6.0;

    CHECK(engine->prepare_loudness_peq("main", points, 60.0, policy));
    CHECK(engine->commit_loudness_peq());

    const auto sample_count = kBlockFrames * kChannels;
    std::vector<float> input(sample_count, 0.01F);
    std::vector<float> output(sample_count, -9.0F);
    const hibiki::RtLaneInputV1 input_view{input.data(), kChannels};
    const std::array<hibiki::RtLaneInputV1, 1> inputs{{input_view}};

    // Replace the same group to arm the full 120 ms crossfade. Six bounded
    // blocks cover the full frame budget, with the final block overshooting
    // the budget in the same way a normal render callback may do.
    auto changed_policy = policy;
    changed_policy.strength = 0.55;
    CHECK(engine->prepare_loudness_peq("main", points, 60.0, changed_policy));
    CHECK(engine->commit_loudness_peq());
    CHECK(!engine->loudness_peq_transition_complete());

    constexpr auto block_count =
        (kMaxCrossfadeFrames + kBlockFrames - 1U) / kBlockFrames;
    for (std::size_t block = 0U; block < block_count; ++block) {
        output.assign(sample_count, -9.0F);
        CHECK(engine->process_output_group("main", inputs, output.data(), kBlockFrames));
        CHECK(std::all_of(output.begin(), output.end(),
                          [](const float value) { return std::isfinite(value); }));
    }
    CHECK(engine->loudness_peq_transition_complete());

    return 0;
}
