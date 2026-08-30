// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/ws_transport.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool expect(const bool condition, const char* const label) {
    if (!condition) {
        std::cerr << "ws transport test failed: " << label << '\n';
        return false;
    }
    return true;
}

std::string handshake_with_size(const std::size_t size) {
    constexpr std::string_view kPrefix =
        "GET /v1/tab HTTP/1.1\r\n"
        "Host: 127.0.0.1:17842\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "X-Pad: ";
    constexpr std::string_view kSuffix = "\r\n\r\n";
    if (size < kPrefix.size() + kSuffix.size()) return {};

    std::string request{kPrefix};
    request.append(size - kPrefix.size() - kSuffix.size(), 'x');
    request += kSuffix;
    return request;
}

bool test_handshake_size_boundary() {
    const auto at_limit = handshake_with_size(hibiki::kMaxHandshakeBytes);
    std::string response;
    if (!expect(at_limit.size() == hibiki::kMaxHandshakeBytes,
                "fixture reaches the maximum handshake size") ||
        !expect(hibiki::parse_websocket_handshake(at_limit, response),
                "maximum-size valid handshake is accepted") ||
        !expect(response.rfind("HTTP/1.1 101 Switching Protocols\r\n", 0U) == 0U,
                "accepted handshake produces a switching-protocols response")) {
        return false;
    }

    const auto over_limit = handshake_with_size(hibiki::kMaxHandshakeBytes + 1U);
    response = "unchanged";
    return expect(over_limit.size() == hibiki::kMaxHandshakeBytes + 1U,
                  "fixture crosses the maximum handshake size by one byte") &&
           expect(!hibiki::parse_websocket_handshake(over_limit, response),
                  "oversized valid handshake is rejected") &&
           expect(response == "unchanged", "rejected oversized handshake preserves response");
}

bool expect_rejected_handshake(const std::string_view request, const char* const label) {
    std::string response{"unchanged"};
    return expect(!hibiki::parse_websocket_handshake(request, response), label) &&
           expect(response == "unchanged", "rejected handshake preserves response");
}

std::string handshake_with_key(const std::string_view key) {
    std::string request{
        "GET /v1/tab HTTP/1.1\r\n"
        "Host: 127.0.0.1:17842\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: "};
    request += key;
    request +=
        "\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    return request;
}

bool test_upgrade_header_semantics() {
    constexpr std::string_view kMissingUpgrade =
        "GET /v1/tab HTTP/1.1\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    constexpr std::string_view kMissingConnection =
        "GET /v1/tab HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    constexpr std::string_view kLookalikeUpgrade =
        "GET /v1/tab HTTP/1.1\r\n"
        "X-Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    constexpr std::string_view kLookalikeKey =
        "GET /v1/tab HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "X-Value: sec-websocket-key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    constexpr std::string_view kNonTokenConnection =
        "GET /v1/tab HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: keep-alive, xupgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    constexpr std::string_view kTokenListRequest =
        "GET /v1/tab HTTP/1.1\r\n"
        "hOsT: \t127.0.0.1:17842 \r\n"
        "Upgrade: WebSocket\r\n"
        "Connection: keep-alive, Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "sEc-WeBsOcKeT-VeRsIoN: \t13 \r\n"
        "\r\n";
    if (!expect_rejected_handshake(kMissingUpgrade, "missing Upgrade header is rejected") ||
        !expect_rejected_handshake(kMissingConnection, "missing Connection header is rejected") ||
        !expect_rejected_handshake(kLookalikeUpgrade, "lookalike Upgrade header is rejected") ||
        !expect_rejected_handshake(kLookalikeKey, "lookalike key header is rejected") ||
        !expect_rejected_handshake(kNonTokenConnection, "non-token Connection value is rejected")) {
        return false;
    }

    std::string response;
    return expect(hibiki::parse_websocket_handshake(kTokenListRequest, response),
                  "case-insensitive upgrade and connection token list are accepted");
}

bool test_request_line_and_version_semantics() {
    constexpr std::string_view kPostRequest =
        "POST /v1/tab HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    constexpr std::string_view kHttp10Request =
        "GET /v1/tab HTTP/1.0\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    constexpr std::string_view kMissingVersion =
        "GET /v1/tab HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n";
    constexpr std::string_view kMissingHost =
        "GET /v1/tab HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    constexpr std::string_view kBlankHost =
        "GET /v1/tab HTTP/1.1\r\n"
        "Host: \t \r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    constexpr std::string_view kUnsupportedVersion =
        "GET /v1/tab HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 12\r\n"
        "\r\n";
    constexpr std::string_view kConflictingDuplicateVersion =
        "GET /v1/tab HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Version: 12\r\n"
        "\r\n";
    constexpr std::string_view kRepeatedDuplicateVersion =
        "GET /v1/tab HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    constexpr std::string_view kConflictingDuplicateKey =
        "GET /v1/tab HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Key: MDEyMzQ1Njc4OWFiY2RlZg==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    constexpr std::string_view kRepeatedDuplicateKey =
        "GET /v1/tab HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    return expect_rejected_handshake(kPostRequest, "non-GET request is rejected") &&
           expect_rejected_handshake(kHttp10Request, "HTTP/1.0 request is rejected") &&
           expect_rejected_handshake(kMissingHost, "missing Host header is rejected") &&
           expect_rejected_handshake(kBlankHost, "blank Host header is rejected") &&
           expect_rejected_handshake(kMissingVersion, "missing WebSocket version is rejected") &&
           expect_rejected_handshake(kUnsupportedVersion, "unsupported WebSocket version is rejected") &&
           expect_rejected_handshake(kConflictingDuplicateVersion,
                                     "conflicting duplicate WebSocket versions are rejected") &&
           expect_rejected_handshake(kRepeatedDuplicateVersion,
                                     "repeated duplicate WebSocket versions are rejected") &&
           expect_rejected_handshake(kConflictingDuplicateKey,
                                     "conflicting duplicate WebSocket keys are rejected") &&
           expect_rejected_handshake(kRepeatedDuplicateKey,
                                     "repeated duplicate WebSocket keys are rejected") &&
           expect_rejected_handshake(handshake_with_key("*GhlIHNhbXBsZSBub25jZQ=="),
                                     "non-Base64 WebSocket key is rejected") &&
           expect_rejected_handshake(handshake_with_key("YQ=="),
                                     "non-16-byte WebSocket key is rejected") &&
           expect_rejected_handshake(handshake_with_key("dGhlIHNhbXBsZSBub25jZQ=A"),
                                     "incorrect WebSocket key padding is rejected") &&
           expect_rejected_handshake(handshake_with_key("dGhlIHNhbXBsZSBub25jZR=="),
                                     "non-canonical WebSocket key padding bits are rejected");
}

bool test_control_frame_opcode_validation() {
    constexpr std::array<std::uint8_t, 1> kPayload{'x'};
    std::string output;
    const auto writer = [&output](const std::span<const std::uint8_t> bytes) {
        output.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return true;
    };
    if (!expect(hibiki::send_ws_control_frame(writer, 0xAU, kPayload) &&
                    output == std::string{"\x8a\x01x", 3U},
                "valid Pong frame is written")) {
        return false;
    }
    output.clear();
    return expect(!hibiki::send_ws_control_frame(writer, 0x2U, kPayload) && output.empty(),
                  "binary opcode is rejected without writing") &&
           expect(!hibiki::send_ws_control_frame(writer, 0xBU, kPayload) && output.empty(),
                  "reserved control opcode is rejected without writing") &&
           expect(!hibiki::send_ws_control_frame(writer, 0x88U, kPayload) && output.empty(),
                  "high-bit opcode is rejected without writing");
}

bool test_fragmented_control_frame_rejection() {
    const auto rejects_without_reading_mask_or_reply = [](const std::uint8_t opcode) {
        const std::vector<std::uint8_t> stream{opcode, 0x81U, 0U, 0U, 0U, 0U, 'x'};
        std::size_t cursor{0U};
        std::size_t reads{0U};
        const auto reader = [&stream, &cursor, &reads](const std::span<std::uint8_t> bytes) {
            ++reads;
            if (cursor + bytes.size() > stream.size()) return false;
            std::copy_n(stream.data() + cursor, bytes.size(), bytes.data());
            cursor += bytes.size();
            return true;
        };
        std::string output;
        const auto writer = [&output](const std::span<const std::uint8_t> bytes) {
            output.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return true;
        };
        hibiki::WsMessageKind kind{hibiki::WsMessageKind::Binary};
        std::vector<std::uint8_t> payload;
        return !hibiki::next_ws_binary_message(reader, writer, 64U, kind, payload) &&
               reads == 1U && cursor == 2U && output.empty();
    };

    return expect(rejects_without_reading_mask_or_reply(0x8U),
                  "fragmented Close is rejected before a reply") &&
           expect(rejects_without_reading_mask_or_reply(0x9U),
                  "fragmented Ping is rejected before a Pong reply") &&
           expect(rejects_without_reading_mask_or_reply(0xAU),
                  "fragmented Pong is rejected without a reply") &&
           expect(rejects_without_reading_mask_or_reply(0xBU),
                  "fragmented reserved control opcode is rejected without a reply");
}

bool test_close_payload_validation() {
    const std::vector<std::uint8_t> stream{0x88U, 0x81U, 0U, 0U, 0U, 0U, 'x'};
    std::size_t offset = 0U;
    const auto reader = [&stream, &offset](std::span<std::uint8_t> destination) {
        if (offset + destination.size() > stream.size()) return false;
        for (std::size_t index = 0U; index < destination.size(); ++index) {
            destination[index] = stream[offset + index];
        }
        offset += destination.size();
        return true;
    };
    std::string output;
    const auto writer = [&output](const std::span<const std::uint8_t> bytes) {
        output.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return true;
    };
    hibiki::WsMessageKind kind{hibiki::WsMessageKind::Binary};
    std::vector<std::uint8_t> payload;
    return expect(!hibiki::next_ws_binary_message(reader, writer, 64U, kind, payload) &&
                      output.empty(),
                  "one-byte Close payload is rejected without a Close reply");
}

bool test_close_status_validation() {
    const auto run_close = [](const std::uint16_t status,
                              hibiki::WsMessageKind& kind,
                              std::string& output) {
        const std::vector<std::uint8_t> stream{0x88U,
                                               0x82U,
                                               0U,
                                               0U,
                                               0U,
                                               0U,
                                               static_cast<std::uint8_t>(status >> 8U),
                                               static_cast<std::uint8_t>(status)};
        std::size_t offset = 0U;
        const auto reader = [&stream, &offset](std::span<std::uint8_t> destination) {
            if (offset + destination.size() > stream.size()) return false;
            for (std::size_t index = 0U; index < destination.size(); ++index) {
                destination[index] = stream[offset + index];
            }
            offset += destination.size();
            return true;
        };
        const auto writer = [&output](const std::span<const std::uint8_t> bytes) {
            output.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return true;
        };
        std::vector<std::uint8_t> payload;
        return hibiki::next_ws_binary_message(reader, writer, 64U, kind, payload);
    };

    hibiki::WsMessageKind kind{hibiki::WsMessageKind::Binary};
    std::string output;
    if (!expect(run_close(1000U, kind, output) && kind == hibiki::WsMessageKind::Close &&
                    output == std::string{"\x88\x00", 2U},
                "valid Close status receives an empty Close reply")) {
        return false;
    }
    for (const auto status : {999U, 1004U, 1005U, 1006U, 1015U, 5000U}) {
        kind = hibiki::WsMessageKind::Binary;
        output.clear();
        if (!expect(!run_close(static_cast<std::uint16_t>(status), kind, output) && output.empty(),
                    "invalid Close status is rejected without a Close reply")) {
            return false;
        }
    }
    return true;
}

bool test_close_reason_utf8_validation() {
    const auto run_close = [](const std::vector<std::uint8_t>& close_payload,
                              hibiki::WsMessageKind& kind,
                              std::string& output) {
        std::vector<std::uint8_t> stream{0x88U,
                                         static_cast<std::uint8_t>(0x80U | close_payload.size()),
                                         0U,
                                         0U,
                                         0U,
                                         0U};
        stream.insert(stream.end(), close_payload.begin(), close_payload.end());
        std::size_t offset = 0U;
        const auto reader = [&stream, &offset](std::span<std::uint8_t> destination) {
            if (offset + destination.size() > stream.size()) return false;
            for (std::size_t index = 0U; index < destination.size(); ++index) {
                destination[index] = stream[offset + index];
            }
            offset += destination.size();
            return true;
        };
        const auto writer = [&output](const std::span<const std::uint8_t> bytes) {
            output.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return true;
        };
        std::vector<std::uint8_t> payload;
        return hibiki::next_ws_binary_message(reader, writer, 64U, kind, payload);
    };

    hibiki::WsMessageKind kind{hibiki::WsMessageKind::Binary};
    std::string output;
    const std::vector<std::uint8_t> valid_reason{0x03U, 0xe8U, 0xe2U, 0x82U, 0xacU};
    if (!expect(run_close(valid_reason, kind, output) && kind == hibiki::WsMessageKind::Close &&
                    output == std::string{"\x88\x00", 2U},
                "valid UTF-8 Close reason receives an empty Close reply")) {
        return false;
    }
    for (const auto& invalid_reason : {std::vector<std::uint8_t>{0x03U, 0xe8U, 0xc0U, 0x80U},
                                       std::vector<std::uint8_t>{0x03U, 0xe8U, 0xe2U, 0x82U},
                                       std::vector<std::uint8_t>{0x03U, 0xe8U, 0xedU, 0xa0U, 0x80U},
                                       std::vector<std::uint8_t>{0x03U, 0xe8U, 0xf4U, 0x90U, 0x80U,
                                                                 0x80U}}) {
        kind = hibiki::WsMessageKind::Binary;
        output.clear();
        if (!expect(!run_close(invalid_reason, kind, output) && output.empty(),
                    "invalid UTF-8 Close reason is rejected without a Close reply")) {
            return false;
        }
    }
    return true;
}

bool test_unsolicited_pong_continues_stream() {
    const std::vector<std::uint8_t> stream{0x8aU,
                                           0x81U,
                                           0U,
                                           0U,
                                           0U,
                                           0U,
                                           'p',
                                           0x82U,
                                           0x81U,
                                           0U,
                                           0U,
                                           0U,
                                           0U,
                                           'b'};
    std::size_t offset = 0U;
    const auto reader = [&stream, &offset](std::span<std::uint8_t> destination) {
        if (offset + destination.size() > stream.size()) return false;
        for (std::size_t index = 0U; index < destination.size(); ++index) {
            destination[index] = stream[offset + index];
        }
        offset += destination.size();
        return true;
    };
    std::string output;
    const auto writer = [&output](const std::span<const std::uint8_t> bytes) {
        output.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return true;
    };
    hibiki::WsMessageKind kind{hibiki::WsMessageKind::Binary};
    std::vector<std::uint8_t> payload;
    if (!expect(hibiki::next_ws_binary_message(reader, writer, 64U, kind, payload) &&
                    kind == hibiki::WsMessageKind::Pong && payload.empty() && output.empty(),
                "unsolicited Pong is accepted without a reply")) {
        return false;
    }
    return expect(hibiki::next_ws_binary_message(reader, writer, 64U, kind, payload) &&
                      kind == hibiki::WsMessageKind::Binary &&
                      payload == std::vector<std::uint8_t>{'b'} && output.empty(),
                  "binary frame after unsolicited Pong remains deliverable");
}

}  // namespace

int main() {
    if (!test_handshake_size_boundary() || !test_upgrade_header_semantics() ||
        !test_request_line_and_version_semantics() || !test_control_frame_opcode_validation() ||
        !test_fragmented_control_frame_rejection() ||
        !test_close_payload_validation() || !test_close_status_validation() ||
        !test_close_reason_utf8_validation() || !test_unsolicited_pong_continues_stream()) {
        return 1;
    }
    std::cout << "hibiki_ws_transport_tests passed (handshake, request and control-frame semantics).\n";
    return 0;
}
