// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_lane_bridge.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>
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

    // ---- shared VST3 block-frame boundary -----------------------------------
    // The ring may have enough physical storage for more than one maximum
    // block, but each exchange still obeys the worker's 4096-frame limit.
    constexpr std::size_t kOversizedFrames =
        hibiki::kMaxVst3RingFramesV1 + 1U;
    std::vector<float> bounded_storage(kOversizedFrames * kStereo, 0.0F);
    std::vector<float> bounded_block(kOversizedFrames * kStereo, 0.75F);
    std::vector<float> bounded_max_block(
        hibiki::kMaxVst3RingFramesV1 * kStereo, 0.5F);
    std::vector<float> bounded_sink(kOversizedFrames * kStereo, -1.0F);
    hibiki::Vst3LaneRingBridgeV1 bounded_bridge;
    CHECK(bounded_bridge.prepare_lane("bounded", kStereo,
                                      bounded_storage));
    CHECK(!bounded_bridge.push("bounded", bounded_block.data(),
                               kOversizedFrames));
    CHECK(bounded_bridge.push("bounded", bounded_max_block.data(),
                              hibiki::kMaxVst3RingFramesV1));
    CHECK(bounded_bridge.push("bounded", bounded_block.data(), 1U));
    CHECK(!bounded_bridge.pop("bounded", bounded_sink.data(),
                             kOversizedFrames));
    CHECK(bounded_bridge.pop("bounded", bounded_sink.data(),
                            hibiki::kMaxVst3RingFramesV1));
    for (std::size_t i = 0U;
         i < hibiki::kMaxVst3RingFramesV1 * kStereo; ++i) {
        CHECK(bounded_sink[i] == 0.5F);
    }
    CHECK(bounded_bridge.pop("bounded", bounded_sink.data(), 1U));
    CHECK(bounded_sink[0] == 0.75F && bounded_sink[1] == 0.75F);

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

    // ---- concurrent tap publication/read --------------------------------------
    // Every successful read must be one complete constant-valued block. A
    // reader may reject a concurrent publication, but it must never accept a
    // mixed payload or metadata generation.
    {
        hibiki::Vst3TapBufferV1 concurrent_tap;
        const auto low_block = make_block(kTapFrames, kStereo, -0.25F);
        const auto high_block = make_block(kTapFrames, kStereo, 0.75F);
        std::atomic<bool> start{false};
        std::atomic<bool> stop_writer{false};
        std::atomic<bool> writer_failed{false};
        std::atomic<bool> reader_failed{false};
        std::thread writer([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            std::size_t iteration = 0U;
            while (!stop_writer.load(std::memory_order_acquire)) {
                const auto& block = (iteration & 1U) == 0U ? low_block : high_block;
                if (!concurrent_tap.publish("tap-group", block.data(), kTapFrames,
                                            kStereo)) {
                    writer_failed.store(true, std::memory_order_release);
                    break;
                }
                ++iteration;
            }
        });
        start.store(true, std::memory_order_release);

        std::array<float, kTapFrames * kStereo> concurrent_destination{};
        for (std::size_t iteration = 0U; iteration < 20000U; ++iteration) {
            std::uint32_t channels = 0U;
            std::size_t frames = 0U;
            std::uint64_t sequence = 0U;
            if (!concurrent_tap.read("tap-group", concurrent_destination.data(),
                                     hibiki::kMaxVst3TapFramesV1, channels, frames,
                                     sequence)) {
                continue;
            }
            if (channels != kStereo || frames != kTapFrames || sequence == 0U) {
                reader_failed.store(true, std::memory_order_release);
                break;
            }
            const auto value = concurrent_destination[0];
            for (const auto sample : concurrent_destination) {
                if (sample != value || (sample != -0.25F && sample != 0.75F)) {
                    reader_failed.store(true, std::memory_order_release);
                    break;
                }
            }
            if (reader_failed.load(std::memory_order_acquire)) break;
        }
        stop_writer.store(true, std::memory_order_release);
        writer.join();
        CHECK(!writer_failed.load(std::memory_order_acquire));
        CHECK(!reader_failed.load(std::memory_order_acquire));
    }

    // A caller capacity whose interleaved product wraps must fail before the
    // snapshot copy. The wrapped product is deliberately larger than this
    // snapshot so the old unchecked comparison would accept it.
    constexpr auto kMaxFramesForStereo =
        std::numeric_limits<std::size_t>::max() / kStereo;
    constexpr auto kWrappingCapacity =
        kMaxFramesForStereo + kTapFrames + 2U;
    CHECK(kWrappingCapacity > kMaxFramesForStereo);
    CHECK(kWrappingCapacity * kStereo > kTapFrames * kStereo);
    std::vector<float> overflow_destination(tap_dest.size(), -3.0F);
    const auto overflow_untouched = overflow_destination;
    const auto channels_before_overflow = tap_channels;
    const auto frames_before_overflow = tap_frames;
    const auto sequence_before_overflow = tap_seq;
    CHECK(!tap.read("tap-group", overflow_destination.data(), kWrappingCapacity,
                    tap_channels, tap_frames, tap_seq));
    CHECK(overflow_destination == overflow_untouched);
    CHECK(tap_channels == channels_before_overflow);
    CHECK(tap_frames == frames_before_overflow);
    CHECK(tap_seq == sequence_before_overflow);
    CHECK(tap.read("tap-group", tap_dest.data(), hibiki::kMaxVst3TapFramesV1,
                   tap_channels, tap_frames, tap_seq));

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
