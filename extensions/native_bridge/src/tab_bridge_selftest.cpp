// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/tab_bridge.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

namespace {

using hibiki::TabCapturePacketViewV1;
using hibiki::TabCaptureQueueV1;
using hibiki::TabPacketError;

constexpr std::size_t kHeaderBytes = 16U;

void write_u16(std::vector<std::uint8_t>& packet, const std::size_t offset,
               const std::uint16_t value) {
    packet[offset] = static_cast<std::uint8_t>(value & 0xffU);
    packet[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

void write_u32(std::vector<std::uint8_t>& packet, const std::size_t offset,
               const std::uint32_t value) {
    packet[offset] = static_cast<std::uint8_t>(value & 0xffU);
    packet[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    packet[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    packet[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

std::vector<std::uint8_t> make_packet(const std::uint16_t channels = 2U,
                                      const std::uint32_t frames = 2U,
                                      const std::uint32_t sample_rate = 48000U,
                                      const float first_sample = 0.25F) {
    const auto sample_count = static_cast<std::size_t>(channels) * frames;
    std::vector<std::uint8_t> packet(kHeaderBytes + sample_count * sizeof(float), 0U);
    packet[0] = 'H';
    packet[1] = 'I';
    packet[2] = 'B';
    packet[3] = 'T';
    write_u16(packet, 4U, 1U);
    write_u16(packet, 6U, channels);
    write_u32(packet, 8U, frames);
    write_u32(packet, 12U, sample_rate);
    for (std::size_t index = 0U; index < sample_count; ++index) {
        const float sample = index == 0U ? first_sample : -first_sample;
        std::memcpy(packet.data() + kHeaderBytes + index * sizeof(float), &sample,
                    sizeof(sample));
    }
    return packet;
}

bool expect_decode_failure(const std::vector<std::uint8_t>& packet,
                           const TabPacketError expected_error) {
    TabCapturePacketViewV1 view{};
    TabPacketError error{TabPacketError::None};
    return !hibiki::decode_tab_capture_packet_v1(packet, view, error) && error == expected_error;
}

bool expect(bool condition, const char* label) {
    if (!condition) {
        std::cerr << "tab bridge self-test failed: " << label << '\n';
        return false;
    }
    return true;
}

bool test_packet_boundaries() {
    auto valid_packet = make_packet();
    TabCapturePacketViewV1 view{};
    TabPacketError error{TabPacketError::None};
    if (!expect(hibiki::decode_tab_capture_packet_v1(valid_packet, view, error),
                "valid packet decodes") ||
        !expect(view.channels == 2U && view.frames == 2U && view.sample_rate == 48000U,
                "valid metadata round-trips") ||
        !expect(std::abs(view.sample(0U) - 0.25F) < 1e-6F && view.sample(99U) == 0.0F,
                "sample view is bounded")) {
        return false;
    }

    if (!expect_decode_failure(std::vector<std::uint8_t>(valid_packet.begin(),
                                                          valid_packet.begin() + 15),
                               TabPacketError::Truncated) ||
        !expect_decode_failure([&] {
            auto packet = valid_packet;
            packet[0] = 0U;
            return packet;
        }(), TabPacketError::InvalidMagic) ||
        !expect_decode_failure([&] {
            auto packet = valid_packet;
            packet[4] = 2U;
            return packet;
        }(), TabPacketError::UnsupportedVersion) ||
        !expect_decode_failure(make_packet(3U), TabPacketError::InvalidChannels) ||
        !expect_decode_failure(make_packet(2U, 2U, 22222U), TabPacketError::InvalidSampleRate)) {
        return false;
    }

    auto length_mismatch = valid_packet;
    length_mismatch.pop_back();
    if (!expect_decode_failure(length_mismatch, TabPacketError::LengthMismatch) ||
        !expect_decode_failure(make_packet(2U, 0U), TabPacketError::InvalidFrameCount)) {
        return false;
    }

    auto non_finite = valid_packet;
    const float nan_sample = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(non_finite.data() + kHeaderBytes, &nan_sample, sizeof(nan_sample));
    return expect_decode_failure(non_finite, TabPacketError::NonFiniteSample);
}

bool test_queue_boundaries() {
    auto first_packet = make_packet(2U, 2U, 48000U, 0.25F);
    auto second_packet = make_packet(2U, 2U, 48000U, 0.75F);
    TabCapturePacketViewV1 first_view{};
    TabCapturePacketViewV1 second_view{};
    TabPacketError error{TabPacketError::None};
    if (!expect(hibiki::decode_tab_capture_packet_v1(first_packet, first_view, error),
                "first queue fixture decodes") ||
        !expect(hibiki::decode_tab_capture_packet_v1(second_packet, second_view, error),
                "second queue fixture decodes")) {
        return false;
    }

    auto queue = std::make_unique<TabCaptureQueueV1>();
    if (!expect(queue->push(first_view) && queue->push(second_view),
                "queue accepts FIFO fixtures")) {
        return false;
    }
    std::array<float, 4U> output{};
    hibiki::TabCaptureBlockV1 block{};
    if (!expect(queue->pop(output.data(), 2U, block), "queue pops first fixture") ||
        !expect(block.frames == 2U && block.channels == 2U && output[0] == 0.25F,
                "first queue item remains first") ||
        !expect(queue->pop(output.data(), 2U, block) && output[0] == 0.75F,
                "queue preserves FIFO order") ||
        !expect(!queue->pop(output.data(), 2U, block) && block.frames == 0U,
                "empty queue fails closed")) {
        return false;
    }

    auto capacity_queue = std::make_unique<TabCaptureQueueV1>();
    if (!expect(capacity_queue->push(first_view), "capacity fixture is queued") ||
        !expect(!capacity_queue->pop(output.data(), 1U, block),
                "insufficient output capacity fails closed") ||
        !expect(capacity_queue->pop(output.data(), 2U, block) && output[0] == 0.25F,
                "capacity rejection does not consume")) {
        return false;
    }

    auto full_queue = std::make_unique<TabCaptureQueueV1>();
    for (std::size_t index = 0U; index < 4U; ++index) {
        if (!expect(full_queue->push(first_view), "queue accepts bounded capacity")) return false;
    }
    if (!expect(!full_queue->push(second_view) && full_queue->dropped_blocks() == 1U,
                "queue accounts for one bounded drop")) {
        return false;
    }
    std::array<float, 4U> drain{};
    for (std::size_t index = 0U; index < 4U; ++index) {
        if (!expect(full_queue->pop(drain.data(), 2U, block), "full queue drains")) return false;
    }
    return expect(!full_queue->pop(drain.data(), 2U, block), "drained queue is empty");
}

bool test_queue_input_output_guards() {
    auto packet = make_packet();
    TabCapturePacketViewV1 view{};
    TabPacketError error{TabPacketError::None};
    if (!expect(hibiki::decode_tab_capture_packet_v1(packet, view, error),
                "guard fixture decodes")) {
        return false;
    }

    auto rejected_queue = std::make_unique<TabCaptureQueueV1>();
    auto null_samples = view;
    null_samples.samples_bytes = nullptr;
    auto short_sample_count = view;
    short_sample_count.sample_count -= 1U;
    auto unsupported_channels = view;
    unsupported_channels.channels = 3U;
    auto unsupported_rate = view;
    unsupported_rate.sample_rate = 22222U;
    std::array<float, 4U> non_finite_samples{0.25F, -0.25F, 0.5F, -0.5F};
    non_finite_samples[2] = std::numeric_limits<float>::quiet_NaN();
    const TabCapturePacketViewV1 non_finite_view{
        2U, 2U, 48000U, reinterpret_cast<const std::uint8_t*>(non_finite_samples.data()), 4U};

    if (!expect(!rejected_queue->push(null_samples), "null sample pointer is rejected") ||
        !expect(!rejected_queue->push(short_sample_count), "sample-count mismatch is rejected") ||
        !expect(!rejected_queue->push(unsupported_channels), "unsupported channels are rejected") ||
        !expect(!rejected_queue->push(unsupported_rate), "unsupported sample rate is rejected") ||
        !expect(!rejected_queue->push(non_finite_view), "non-finite samples are rejected") ||
        !expect(rejected_queue->dropped_blocks() == 0U,
                "rejected pushes do not increment dropped blocks")) {
        return false;
    }

    auto pop_queue = std::make_unique<TabCaptureQueueV1>();
    if (!expect(pop_queue->push(view), "valid pop guard fixture is queued")) return false;
    std::array<float, 4U> output{};
    hibiki::TabCaptureBlockV1 block{};
    if (!expect(!pop_queue->pop(nullptr, 2U, block) && block.frames == 0U,
                "null output is rejected without a block") ||
        !expect(!pop_queue->pop(output.data(), 0U, block) && block.frames == 0U,
                "zero output capacity is rejected without a block") ||
        !expect(!pop_queue->pop(output.data(), 1U, block) && block.frames == 0U,
                "insufficient output capacity is rejected without a block") ||
        !expect(pop_queue->pop(output.data(), 2U, block) && block.frames == 2U &&
                    output[0] == 0.25F,
                "valid pop remains available after rejected guards") ||
        !expect(!pop_queue->pop(output.data(), 2U, block), "guard queue is empty after valid pop")) {
        return false;
    }
    return true;
}

void noop_callback(const TabCapturePacketViewV1&, void*) noexcept {}

bool expect_rejected_server_config(const hibiki::TabBridgeServerConfigV1 config,
                                   const hibiki::TabCapturePacketCallbackV1 callback,
                                   const char* label) {
    hibiki::TabBridgeServer server;
    if (!expect(!server.start(config, callback, nullptr), label)) return false;
    return expect(!server.running(), "rejected server config does not start a listener");
}

bool test_server_config_boundaries() {
    constexpr std::uint16_t kNominalPort = 17842U;
    constexpr std::size_t kNominalPayload = 256U * 1024U;
    if (!expect_rejected_server_config(
            hibiki::TabBridgeServerConfigV1{kNominalPort, kNominalPayload}, nullptr,
            "null callback is rejected before socket setup") ||
        !expect_rejected_server_config(
            hibiki::TabBridgeServerConfigV1{0U, kNominalPayload}, &noop_callback,
            "zero port is rejected before socket setup") ||
        !expect_rejected_server_config(
            hibiki::TabBridgeServerConfigV1{kNominalPort, 15U}, &noop_callback,
            "payload below the HIBT header is rejected") ||
        !expect_rejected_server_config(
            hibiki::TabBridgeServerConfigV1{kNominalPort, 1024U * 1024U + 1U}, &noop_callback,
            "payload above the one MiB ceiling is rejected")) {
        return false;
    }
    return true;
}

}  // namespace

int main() {
    if (!test_packet_boundaries() || !test_queue_boundaries() ||
        !test_queue_input_output_guards() || !test_server_config_boundaries()) {
        return 1;
    }
    std::cout << "hibiki_tab_bridge_selftest passed (packet boundaries, FIFO queue, input/output guards, drops and server guards).\n";
    return 0;
}
