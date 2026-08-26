// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_lane_bridge.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
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
constexpr std::size_t kRingFrames = 16U;

std::vector<float> make_block(const std::size_t frames,
                              const std::uint32_t channels,
                              const float value) {
    return std::vector<float>(static_cast<std::size_t>(frames) * channels, value);
}

}  // namespace

int main() {
    // ---- ring bridge: fresh state ------------------------------------------
    hibiki::Vst3LaneRingBridgeV1 bridge;
    CHECK(!bridge.has_lane("main"));
    CHECK(bridge.channel_count("main") == 0U);
    CHECK(!bridge.pop("main", nullptr, 1U));
    std::vector<float> sink(kStereo * kRingFrames, 0.0F);
    CHECK(!bridge.pop("main", sink.data(), 1U));
    CHECK(!bridge.clear_lane("main"));

    // ---- prepare_lane boundary rejection ------------------------------------
    std::vector<float> storage(kStereo * kRingFrames, 0.0F);
    CHECK(!bridge.prepare_lane("", kStereo, storage));
    const std::string_view too_long("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", hibiki::kMaxOutputGroupBytesV1 + 1);
    CHECK(!bridge.prepare_lane(too_long, kStereo, storage));
    CHECK(!bridge.prepare_lane("main", 0U, storage));
    CHECK(!bridge.prepare_lane("main", 9U, storage));
    CHECK(!bridge.prepare_lane("main", kStereo, {}));
    CHECK(!bridge.has_lane("main"));

    // ---- prepare_lane accepted + duplicate rejection ------------------------
    CHECK(bridge.prepare_lane("main", kStereo, storage));
    CHECK(bridge.has_lane("main"));
    CHECK(bridge.channel_count("main") == kStereo);
    CHECK(!bridge.prepare_lane("main", kStereo, storage));

    // ---- push/pop round-trip -------------------------------------------------
    const auto block_a = make_block(4U, kStereo, 0.25F);
    CHECK(bridge.push("main", block_a.data(), 4U));
    CHECK(bridge.pop("main", sink.data(), 2U));
    for (std::size_t i = 0; i < 2U * kStereo; ++i) {
        CHECK(sink[i] == 0.25F);
    }
    CHECK(bridge.pop("main", sink.data(), 2U));
    for (std::size_t i = 0; i < 2U * kStereo; ++i) {
        CHECK(sink[i] == 0.25F);
    }
    CHECK(!bridge.pop("main", sink.data(), 1U));

    // ---- wrap-around ---------------------------------------------------------
    // Capacity is 16 stereo frames. Push 12, pop 8, push 8 (wraps), pop all.
    auto fill = [](std::vector<float>& v, const float base) {
        for (std::size_t f = 0; f < kRingFrames; ++f) {
            v[f * kStereo] = base;
            v[f * kStereo + 1] = base + 1.0F;
        }
    };
    std::vector<float> big(kStereo * kRingFrames, 0.0F);
    fill(big, 10.0F);
    CHECK(bridge.push("main", big.data(), 12U));
    CHECK(bridge.pop("main", sink.data(), 8U));
    fill(big, 20.0F);
    CHECK(bridge.push("main", big.data(), 8U));
    CHECK(bridge.pop("main", sink.data(), 12U));
    for (std::size_t f = 0; f < 4U; ++f) {
        CHECK(sink[f * kStereo] == 10.0F);
        CHECK(sink[f * kStereo + 1] == 11.0F);
    }
    for (std::size_t f = 4; f < 12U; ++f) {
        CHECK(sink[f * kStereo] == 20.0F);
        CHECK(sink[f * kStereo + 1] == 21.0F);
    }

    // ---- ring full rejection --------------------------------------------------
    fill(big, 30.0F);
    CHECK(!bridge.push("main", big.data(), kRingFrames + 1U));
    CHECK(bridge.push("main", big.data(), kRingFrames));
    // Ring is now exactly full: any further push must fail.
    CHECK(!bridge.push("main", big.data(), 1U));
    // Drain everything to leave a clean state for the next section.
    CHECK(bridge.pop("main", sink.data(), kRingFrames));

    // ---- NaN/Inf rejection -----------------------------------------------------
    auto poison = big;
    poison[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!bridge.push("main", poison.data(), 1U));
    poison[0] = std::numeric_limits<float>::infinity();
    CHECK(!bridge.push("main", poison.data(), 1U));

    // ---- null pointer / zero-frame rejection -----------------------------------
    CHECK(!bridge.push("main", nullptr, 1U));
    CHECK(!bridge.push("main", big.data(), 0U));
    CHECK(!bridge.pop("main", nullptr, 1U));
    CHECK(!bridge.pop("main", sink.data(), 0U));

    // ---- clear_lane / clear_all / reset ---------------------------------------
    CHECK(bridge.clear_lane("main"));
    CHECK(!bridge.has_lane("main"));
    CHECK(bridge.channel_count("main") == 0U);
    CHECK(bridge.prepare_lane("aux", kStereo, storage));
    CHECK(bridge.has_lane("aux"));
    bridge.clear_all();
    CHECK(!bridge.has_lane("aux"));
    CHECK(bridge.prepare_lane("aux2", kStereo, storage));
    bridge.reset();
    CHECK(!bridge.has_lane("aux2"));

    // ---- multi-lane independence ----------------------------------------------
    std::vector<float> lane_a_storage(kStereo * kRingFrames, 0.0F);
    std::vector<float> lane_b_storage(8U * kRingFrames, 0.0F);
    CHECK(bridge.prepare_lane("lane-a", kStereo, lane_a_storage));
    CHECK(bridge.prepare_lane("lane-b", 8U, lane_b_storage));
    CHECK(bridge.channel_count("lane-a") == kStereo);
    CHECK(bridge.channel_count("lane-b") == 8U);
    const auto block_stereo = make_block(2U, kStereo, 1.0F);
    const auto block_8ch = make_block(2U, 8U, 2.0F);
    CHECK(bridge.push("lane-a", block_stereo.data(), 2U));
    CHECK(bridge.push("lane-b", block_8ch.data(), 2U));
    // Each lane reads back its own data independently.
    std::vector<float> lane_sink(8U * kRingFrames, 0.0F);
    CHECK(bridge.pop("lane-a", lane_sink.data(), 2U));
    for (std::size_t i = 0; i < 2U * kStereo; ++i) {
        CHECK(lane_sink[i] == 1.0F);
    }
    CHECK(bridge.pop("lane-b", lane_sink.data(), 2U));
    for (std::size_t i = 0; i < 2U * 8U; ++i) {
        CHECK(lane_sink[i] == 2.0F);
    }

    // ---- tap buffer: fresh state ----------------------------------------------
    hibiki::Vst3TapBufferV1 tap;
    std::uint32_t tap_channels = 0U;
    std::size_t tap_frames = 0U;
    std::uint64_t tap_seq = 0U;
    std::vector<float> tap_dest(
        hibiki::kMaxVst3TapFramesV1 * hibiki::kMaxVst3TapChannelsV1, 0.0F);
    CHECK(!tap.read("tap-group", tap_dest.data(), hibiki::kMaxVst3TapFramesV1,
                    tap_channels, tap_frames, tap_seq));

    // ---- tap publish/read round trip -------------------------------------------
    constexpr std::size_t kTapFrames = 32U;
    const auto tap_block = make_block(kTapFrames, kStereo, 0.5F);
    CHECK(tap.publish("tap-group", tap_block.data(), kTapFrames, kStereo));
    CHECK(tap.read("tap-group", tap_dest.data(), hibiki::kMaxVst3TapFramesV1,
                   tap_channels, tap_frames, tap_seq));
    CHECK(tap_channels == kStereo);
    CHECK(tap_frames == kTapFrames);
    CHECK(tap_seq > 0U);
    for (std::size_t i = 0; i < kTapFrames * kStereo; ++i) {
        CHECK(tap_dest[i] == 0.5F);
    }

    // ---- tap group mismatch ------------------------------------------------------
    CHECK(!tap.read("wrong-group", tap_dest.data(), hibiki::kMaxVst3TapFramesV1,
                    tap_channels, tap_frames, tap_seq));

    // ---- tap publish boundary rejection ---------------------------------------------
    CHECK(!tap.publish("", tap_block.data(), kTapFrames, kStereo));
    CHECK(!tap.publish("tap-group", nullptr, kTapFrames, kStereo));
    CHECK(!tap.publish("tap-group", tap_block.data(), 0U, kStereo));
    CHECK(!tap.publish("tap-group", tap_block.data(),
                       hibiki::kMaxVst3TapFramesV1 + 1U, kStereo));
    CHECK(!tap.publish("tap-group", tap_block.data(), kTapFrames, 0U));
    CHECK(!tap.publish("tap-group", tap_block.data(), kTapFrames, 9U));

    // ---- tap NaN/Inf rejection --------------------------------------------------------
    auto tap_poison = tap_block;
    tap_poison[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!tap.publish("tap-group", tap_poison.data(), kTapFrames, kStereo));
    tap_poison[0] = -std::numeric_limits<float>::infinity();
    CHECK(!tap.publish("tap-group", tap_poison.data(), kTapFrames, kStereo));

    // ---- tap torn-read rejection via forced odd sequence ------------------------------
    CHECK(tap.publish("tap-group", tap_block.data(), kTapFrames, kStereo));
    tap.force_sequence_odd_for_tests();
    CHECK(!tap.read("tap-group", tap_dest.data(), hibiki::kMaxVst3TapFramesV1,
                    tap_channels, tap_frames, tap_seq));

    // ---- tap reset --------------------------------------------------------------------
    tap.reset();
    CHECK(!tap.read("tap-group", tap_dest.data(), hibiki::kMaxVst3TapFramesV1,
                    tap_channels, tap_frames, tap_seq));

    std::fputs("all checks passed\n", stdout);
    return 0;
}
