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

}  // namespace

int main() {
    if (!test_handshake_size_boundary()) return 1;
    std::cout << "hibiki_ws_transport_tests passed (handshake size boundary).\n";
    return 0;
}
