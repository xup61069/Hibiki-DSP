// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/audio_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

#define CHECK(expr)                                                             \
    do {                                                                        \
        if (!(expr)) {                                                          \
            std::fputs("FAILED: " #expr "\n", stderr);                         \
            return 1;                                                           \
        }                                                                       \
    } while (false)

namespace {

constexpr std::uint32_t kChannels = 2U;
constexpr std::size_t kValidFrames = 16U;

std::vector<float> make_lane_block(const std::size_t frames) {
    std::vector<float> block(frames * kChannels, 0.0F);
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        block[frame * kChannels] = 0.25F;
        block[frame * kChannels + 1U] = -0.25F;
    }
    return block;
}

bool has_processed_stereo_shape(const std::vector<float>& block) {
    if (block.size() != kValidFrames * kChannels) return false;
    for (std::size_t frame = 0U; frame < kValidFrames; ++frame) {
        const auto left = block[frame * kChannels];
        const auto right = block[frame * kChannels + 1U];
        if (!std::isfinite(left) || !std::isfinite(right) || left <= 0.0F ||
            right >= 0.0F || left != -right) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    hibiki::AudioEngineModel engine;
    hibiki::GraphConfigV1 graph;
    graph.lanes.push_back(hibiki::LaneConfigV1{
        "vst3-engine-test", "main", kChannels, 0.0, true});
    graph.strict_direct = false;
    CHECK(engine.prepare_graph(graph, 1U));
    CHECK(engine.commit_graph());
    CHECK(engine.transaction_state() == hibiki::EngineTransactionState::Ready);

    std::vector<float> ring_storage(
        hibiki::kMaxVst3RingFramesV1 * kChannels, 0.0F);
    CHECK(engine.prepare_vst3_lane("main", kChannels,
                                   std::span<float>(ring_storage)));
    CHECK(engine.commit_vst3_lane());
    CHECK(engine.vst3_lane_active("main"));

    std::vector<float> silence(kValidFrames * kChannels, 0.0F);
    const std::array<hibiki::RtLaneInputV1, 1U> inputs{{
        {silence.data(), kChannels},
    }};
    const auto processed_block = make_lane_block(kValidFrames);
    CHECK(engine.push_vst3_lane("main", processed_block.data(), kValidFrames));

    std::vector<float> output(kValidFrames * kChannels, -9.0F);
    CHECK(engine.process_output_group("main", inputs, output.data(),
                                      kValidFrames));
    CHECK(has_processed_stereo_shape(output));

    std::vector<float> tap_output(
        hibiki::kMaxVst3TapFramesV1 * hibiki::kMaxVst3TapChannelsV1, 0.0F);
    std::uint32_t tap_channels = 0U;
    std::size_t tap_frames = 0U;
    std::uint64_t tap_sequence = 0U;
    CHECK(engine.read_vst3_tap(
        "main", tap_output.data(), hibiki::kMaxVst3TapFramesV1,
        tap_channels, tap_frames, tap_sequence));
    CHECK(tap_channels == kChannels && tap_frames == kValidFrames &&
          tap_sequence != 0U);
    const auto sequence_before_oversized = tap_sequence;
    const auto attempts_before_oversized = engine.vst3_tap_publish_attempts();

    constexpr auto kOversizedFrames = hibiki::kMaxVst3RingFramesV1 + 1U;
    std::vector<float> oversized_output(kOversizedFrames * kChannels, -7.0F);
    CHECK(!engine.process_output_group("main", inputs, oversized_output.data(),
                                       kOversizedFrames));
    CHECK(engine.transaction_state() == hibiki::EngineTransactionState::Degraded);
    CHECK(std::all_of(oversized_output.begin(), oversized_output.end(),
                      [](const float value) { return value == -7.0F; }));
    CHECK(engine.vst3_tap_publish_attempts() == attempts_before_oversized);

    tap_channels = 0U;
    tap_frames = 0U;
    tap_sequence = 0U;
    CHECK(engine.read_vst3_tap(
        "main", tap_output.data(), hibiki::kMaxVst3TapFramesV1,
        tap_channels, tap_frames, tap_sequence));
    CHECK(tap_channels == kChannels && tap_frames == kValidFrames &&
          tap_sequence == sequence_before_oversized);
    CHECK(engine.vst3_lane_active("main"));

    // A rejected oversized exchange must not poison the active lane. A new
    // prepared block is still accepted and consumed normally afterwards.
    CHECK(engine.push_vst3_lane("main", processed_block.data(), kValidFrames));
    output.assign(kValidFrames * kChannels, -8.0F);
    CHECK(engine.process_output_group("main", inputs, output.data(),
                                      kValidFrames));
    CHECK(has_processed_stereo_shape(output));

    std::fputs("all checks passed\n", stdout);
    return 0;
}
