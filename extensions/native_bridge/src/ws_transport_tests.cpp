// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/ws_transport.hpp"

#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>

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

bool test_upgrade_header_semantics() {
    constexpr std::string_view kMissingUpgrade =
        "GET /v1/tab HTTP/1.1\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n";
    constexpr std::string_view kMissingConnection =
        "GET /v1/tab HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n";
    constexpr std::string_view kLookalikeUpgrade =
        "GET /v1/tab HTTP/1.1\r\n"
        "X-Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n";
    constexpr std::string_view kLookalikeKey =
        "GET /v1/tab HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "X-Value: sec-websocket-key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n";
    constexpr std::string_view kNonTokenConnection =
        "GET /v1/tab HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: keep-alive, xupgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n";
    constexpr std::string_view kTokenListRequest =
        "GET /v1/tab HTTP/1.1\r\n"
        "Upgrade: WebSocket\r\n"
        "Connection: keep-alive, Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
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

}  // namespace

int main() {
    if (!test_handshake_size_boundary() || !test_upgrade_header_semantics()) return 1;
    std::cout << "hibiki_ws_transport_tests passed (handshake size and upgrade semantics).\n";
    return 0;
}
