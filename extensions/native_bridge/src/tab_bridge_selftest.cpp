// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/tab_bridge.hpp"
#include "hibiki/ws_transport.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
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

    auto rate_queue = std::make_unique<TabCaptureQueueV1>();
    if (!expect(!rate_queue->set_expected_sample_rate(22222U),
                "unsupported expected sample rate is rejected") ||
        !expect(rate_queue->set_expected_sample_rate(48000U),
                "supported expected sample rate is accepted") ||
        !expect(rate_queue->expected_sample_rate() == 48000U,
                "expected sample rate is observable")) {
        return false;
    }
    auto mismatched_44100 = view;
    mismatched_44100.sample_rate = 44100U;
    auto mismatched_96000 = view;
    mismatched_96000.sample_rate = 96000U;
    auto mismatched_192000 = view;
    mismatched_192000.sample_rate = 192000U;
    if (!expect(!rate_queue->push(mismatched_44100), "44100 source is rejected") ||
        !expect(!rate_queue->push(mismatched_96000), "96000 source is rejected") ||
        !expect(!rate_queue->push(mismatched_192000), "192000 source is rejected") ||
        !expect(rate_queue->sample_rate_mismatch_blocks() == 3U,
                "rate mismatches are counted separately") ||
        !expect(rate_queue->push(view), "matching source rate is accepted") ||
        !expect(!rate_queue->set_expected_sample_rate(44100U),
                "rate binding cannot change after producer activity")) {
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

std::size_t read_cursor_bytes(std::span<const std::uint8_t> source,
                              std::size_t& offset,
                              std::span<std::uint8_t> destination) {
    if (destination.size() > source.size() - offset) return 0U;
    std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(offset),
                static_cast<std::ptrdiff_t>(destination.size()), destination.begin());
    offset += destination.size();
    return destination.size();
}

void append_client_frame(std::vector<std::uint8_t>& stream,
                         const std::uint8_t opcode,
                         const std::span<const std::uint8_t> payload,
                         const bool masked = true,
                         const bool reserved_bits = false,
                         const bool fin_bit = true,
                         const bool force_127_length = false) {
    std::uint8_t first = static_cast<std::uint8_t>((fin_bit ? 0x80U : 0x00U) | opcode);
    if (reserved_bits) first = static_cast<std::uint8_t>(first | 0x70U);
    stream.push_back(first);
    const auto size = payload.size();
    if (force_127_length) {
        stream.push_back(static_cast<std::uint8_t>((masked ? 0x80U : 0x00U) | 127U));
        for (int shift = 7; shift >= 0; --shift) {
            stream.push_back(static_cast<std::uint8_t>(
                (static_cast<std::uint64_t>(size) >> (shift * 8U)) & 0xffU));
        }
    } else if (size <= 125U) {
        stream.push_back(static_cast<std::uint8_t>((masked ? 0x80U : 0x00U) | size));
    } else {
        stream.push_back(static_cast<std::uint8_t>((masked ? 0x80U : 0x00U) | 126U));
        stream.push_back(static_cast<std::uint8_t>((size >> 8U) & 0xffU));
        stream.push_back(static_cast<std::uint8_t>(size & 0xffU));
    }
    const std::array<std::uint8_t, 4> mask{0x11U, 0x22U, 0x33U, 0x44U};
    if (masked) stream.insert(stream.end(), mask.begin(), mask.end());
    for (std::size_t index = 0U; index < size; ++index) {
        stream.push_back(masked ? static_cast<std::uint8_t>(payload[index] ^ mask[index % 4U])
                                : payload[index]);
    }
}

bool test_websocket_handshake() {
    constexpr std::string_view kValidRequest =
        "GET /v1/tab HTTP/1.1\r\n"
        "Host: 127.0.0.1:17842\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    std::string accept;
    if (!expect(hibiki::websocket_compute_accept("dGhlIHNhbXBsZSBub25jZQ==", accept),
                "websocket accept computes") ||
        !expect(accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=",
                "websocket accept matches the known vector")) {
        return false;
    }

    std::string response;
    if (!expect(hibiki::parse_websocket_handshake(kValidRequest, response),
                "valid handshake parses") ||
        !expect(response.rfind("HTTP/1.1 101 Switching Protocols\r\n", 0U) == 0U,
                "handshake responds with 101") ||
        !expect(response.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") !=
                    std::string::npos,
                "handshake carries the computed accept")) {
        return false;
    }

    if (!expect(!hibiki::parse_websocket_handshake("GET / HTTP/1.1\r\nHost: x\r\n", response),
                "truncated handshake is rejected") ||
        !expect(!hibiki::parse_websocket_handshake(
                    "GET / HTTP/1.1\r\nUpgrade: websocket\r\n\r\n", response),
                "missing key header is rejected") ||
        !expect(!hibiki::parse_websocket_handshake(
                    "GET / HTTP/1.1\r\nSec-WebSocket-Key:   \t \r\n\r\n", response),
                "blank key is rejected")) {
        return false;
    }
    return true;
}

bool expect_frame_failure(const std::vector<std::uint8_t>& stream,
                          const std::size_t max_payload,
                          const hibiki::WsFrameError expected_error,
                          const char* label) {
    hibiki::WsDecodedFrameV1 frame{};
    hibiki::WsFrameError error{hibiki::WsFrameError::None};
    std::size_t offset = 0U;
    const auto reader = [&](std::span<std::uint8_t> destination) {
        return read_cursor_bytes(stream, offset, destination) == destination.size();
    };
    return expect(!hibiki::read_ws_client_frame(reader, max_payload, frame, error) &&
                      error == expected_error,
                  label);
}

bool expect_frame_success(const std::vector<std::uint8_t>& stream,
                          const std::uint8_t expected_opcode,
                          const std::span<const std::uint8_t> expected_payload,
                          const char* label) {
    hibiki::WsDecodedFrameV1 frame{};
    hibiki::WsFrameError error{hibiki::WsFrameError::None};
    std::size_t offset = 0U;
    const auto reader = [&](std::span<std::uint8_t> destination) {
        return read_cursor_bytes(stream, offset, destination) == destination.size();
    };
    if (!expect(hibiki::read_ws_client_frame(reader, 1024U * 1024U, frame, error), label)) {
        return false;
    }
    if (!expect(frame.opcode == expected_opcode && frame.payload.size() == expected_payload.size(),
                "decoded frame metadata matches")) {
        return false;
    }
    for (std::size_t index = 0U; index < expected_payload.size(); ++index) {
        if (!expect(frame.payload[index] == expected_payload[index], "unmasked payload matches")) {
            return false;
        }
    }
    return true;
}

bool test_websocket_frames() {
    const std::array<std::uint8_t, 3> small{0xABU, 0xCDU, 0xEFU};
    std::vector<std::uint8_t> small_stream;
    append_client_frame(small_stream, 0x2U, small);
    if (!expect_frame_success(small_stream, 0x2U, small, "small masked frame decodes")) return false;

    std::vector<std::uint8_t> large_payload(300U);
    for (std::size_t index = 0U; index < large_payload.size(); ++index) {
        large_payload[index] = static_cast<std::uint8_t>(index & 0xffU);
    }
    std::vector<std::uint8_t> extended_stream;
    append_client_frame(extended_stream, 0x2U, large_payload);
    if (!expect_frame_success(extended_stream, 0x2U, large_payload,
                              "extended 126-bit length decodes")) {
        return false;
    }

    std::vector<std::uint8_t> long_form_stream;
    append_client_frame(long_form_stream, 0x2U, small, true, false, true, true);
    if (!expect_frame_success(long_form_stream, 0x2U, small,
                              "127-bit length form decodes")) {
        return false;
    }

    if (!expect_frame_failure(small_stream, 2U, hibiki::WsFrameError::PayloadTooLarge,
                              "oversized payload fails closed")) {
        return false;
    }

    std::vector<std::uint8_t> unmasked;
    append_client_frame(unmasked, 0x2U, small, false);
    if (!expect_frame_failure(unmasked, 1024U, hibiki::WsFrameError::UnmaskedClientFrame,
                              "unmasked client frames are rejected")) {
        return false;
    }

    std::vector<std::uint8_t> reserved;
    append_client_frame(reserved, 0x2U, small, true, true);
    if (!expect_frame_failure(reserved, 1024U, hibiki::WsFrameError::ReservedBitsSet,
                              "reserved bits are rejected")) {
        return false;
    }

    std::vector<std::uint8_t> missing_fin;
    append_client_frame(missing_fin, 0x2U, small, true, false, false);
    if (!expect_frame_failure(missing_fin, 1024U, hibiki::WsFrameError::ReservedBitsSet,
                              "clear FIN bit is rejected")) {
        return false;
    }

    std::vector<std::uint8_t> truncated_payload = small_stream;
    truncated_payload.pop_back();
    if (!expect_frame_failure(truncated_payload, 1024U, hibiki::WsFrameError::TruncatedPayload,
                              "truncated payload fails closed")) {
        return false;
    }

    std::vector<std::uint8_t> truncated_length;
    truncated_length.push_back(0x82U);
    truncated_length.push_back(0xFEU);
    truncated_length.push_back(0x01U);
    if (!expect_frame_failure(truncated_length, 1024U, hibiki::WsFrameError::TruncatedPayload,
                              "truncated extended length fails closed")) {
        return false;
    }

    hibiki::WsDecodedFrameV1 frame{};
    hibiki::WsFrameError error{hibiki::WsFrameError::None};
    const auto failing_reader = [](std::span<std::uint8_t>) { return false; };
    if (!expect(!hibiki::read_ws_client_frame(failing_reader, 1024U, frame, error) &&
                    error == hibiki::WsFrameError::IncompleteFrame,
                "stream failure before the header fails closed")) {
        return false;
    }

    std::vector<std::uint8_t> control_output;
    const auto collecting_writer = [&control_output](std::span<const std::uint8_t> bytes) {
        control_output.insert(control_output.end(), bytes.begin(), bytes.end());
        return true;
    };
    const auto rejecting_writer = [](std::span<const std::uint8_t>) { return false; };
    if (!expect(hibiki::send_ws_control_frame(collecting_writer, 0x9U, small) &&
                    control_output.size() == 5U && control_output[0] == 0x89U &&
                    control_output[1] == 0x03U,
                "ping control frame encodes") ||
        !expect(!hibiki::send_ws_control_frame(rejecting_writer, 0xAU,
                                               std::vector<std::uint8_t>(126U, 0U)),
                "control payloads above 125 bytes are rejected") ||
        !expect(hibiki::send_ws_control_frame(collecting_writer, 0x8U, {}),
                "empty close frame encodes")) {
        return false;
    }
    return true;
}

bool test_serve_loop_semantics() {
    const auto tab_packet = make_packet();
    const std::array<std::uint8_t, 2> ping_payload{'h', 'i'};

    std::vector<std::uint8_t> client_stream;
    append_client_frame(client_stream, 0x9U, ping_payload);
    append_client_frame(client_stream, 0x2U,
                        std::span<const std::uint8_t>{tab_packet.data(), tab_packet.size()});
    append_client_frame(client_stream, 0x8U, {});

    std::size_t offset = 0U;
    std::vector<std::uint8_t> server_output;
    std::size_t callback_count = 0U;
    float first_sample = 0.0F;
    struct CallbackContext {
        std::size_t* count;
        float* sample;
    } callback_context{&callback_count, &first_sample};
    const auto lambda_callback = [](const TabCapturePacketViewV1& view, void* raw_context) noexcept {
        auto* context = static_cast<CallbackContext*>(raw_context);
        ++(*context->count);
        *context->sample = view.sample(0U);
    };
    const hibiki::TabCapturePacketCallbackV1 typed_callback = lambda_callback;
    const auto reader = [&](std::span<std::uint8_t> destination) {
        return read_cursor_bytes(client_stream, offset, destination) == destination.size();
    };
    const auto writer = [&](std::span<const std::uint8_t> bytes) {
        server_output.insert(server_output.end(), bytes.begin(), bytes.end());
        return true;
    };

    bool closed = false;
    while (!closed) {
        hibiki::WsMessageKind kind{hibiki::WsMessageKind::Close};
        std::vector<std::uint8_t> payload;
        if (!expect(hibiki::next_ws_binary_message(reader, writer, 64U * 1024U, kind, payload),
                    "serve step succeeds inside the loop")) {
            return false;
        }
        if (kind == hibiki::WsMessageKind::Close) {
            closed = true;
        } else if (kind == hibiki::WsMessageKind::Ping) {
            if (!expect(payload.empty(), "ping payload is consumed by the pong reply")) return false;
            if (!expect(server_output.size() >= 4U && server_output[0] == 0x8AU &&
                            server_output[1] == 0x02U && server_output[2] == 'h' &&
                            server_output[3] == 'i',
                        "pong echoes the ping payload")) {
                return false;
            }
        } else {
            TabCapturePacketViewV1 view{};
            TabPacketError error{TabPacketError::None};
            if (!expect(decode_tab_capture_packet_v1(payload, view, error),
                        "binary frame reaches callback path")) return false;
            typed_callback(view, &callback_context);
        }
    }

    if (!expect(callback_count == 1U && std::abs(first_sample - 0.25F) < 1e-6F,
                "exactly one valid packet reached the callback with its sample") ||
        !expect(offset == client_stream.size(), "close frame terminates the loop at the end") ||
        !expect(server_output.size() >= 6U && server_output[0] == 0x8AU &&
                    server_output[1] == 0x02U && server_output[2] == 'h' &&
                    server_output[server_output.size() - 2U] == 0x88U &&
                    server_output[server_output.size() - 1U] == 0x00U,
                "pong then close are written back")) {
        return false;
    }

    std::vector<std::uint8_t> text_stream;
    const std::array<std::uint8_t, 1> text{'x'};
    append_client_frame(text_stream, 0x1U, text);
    client_stream.swap(text_stream);
    offset = 0U;
    hibiki::WsMessageKind kind{hibiki::WsMessageKind::Close};
    std::vector<std::uint8_t> payload;
    if (!expect(!hibiki::next_ws_binary_message(reader, writer, 64U * 1024U, kind, payload),
                "non-binary non-control opcode fails closed")) {
        return false;
    }

    std::vector<std::uint8_t> empty_binary_stream;
    append_client_frame(empty_binary_stream, 0x2U, {});
    client_stream.swap(empty_binary_stream);
    offset = 0U;
    server_output.clear();
    if (!expect(hibiki::next_ws_binary_message(reader, writer, 64U * 1024U, kind, payload) &&
                    kind == hibiki::WsMessageKind::Binary && payload.empty(),
                "empty binary frame stays distinct from close")) {
        return false;
    }
    return true;
}

}  // namespace

int main() {
    if (!test_packet_boundaries() || !test_queue_boundaries() ||
        !test_queue_input_output_guards() || !test_server_config_boundaries() ||
        !test_websocket_handshake() || !test_websocket_frames() ||
        !test_serve_loop_semantics()) {
        return 1;
    }
    std::cout << "hibiki_tab_bridge_selftest passed (packet boundaries, FIFO queue, rate guard, "
                 "guards, server config, websocket handshake, framing and serve-loop semantics).\n";
    return 0;
}
