#pragma once

// SPDX-License-Identifier: GPL-3.0-only

// Testable WebSocket transport primitives for the loopback tab bridge.
// These helpers operate on caller-supplied byte streams so the handshake and
// framing rules can be exercised offline without opening sockets.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hibiki {

inline constexpr std::size_t kMaxHandshakeBytes = 8192U;

struct WsDecodedFrameV1 {
    std::uint8_t opcode{0U};
    std::vector<std::uint8_t> payload;
};

enum class WsFrameError : std::uint8_t {
    None,
    IncompleteFrame,
    ReservedBitsSet,
    UnmaskedClientFrame,
    PayloadTooLarge,
    TruncatedPayload,
};

enum class WsMessageKind : std::uint8_t {
    Binary,
    Close,
    Ping,
    Pong,
};

using WsStreamRead = std::function<bool(std::span<std::uint8_t>)>;
using WsStreamWrite = std::function<bool(std::span<const std::uint8_t>)>;

// Computes the RFC 6455 Sec-WebSocket-Accept value for a handshake key.
[[nodiscard]] bool websocket_compute_accept(std::string_view key, std::string& accept);

// Validates an HTTP upgrade request and produces the 101 response. Returns
// false when the request is truncated, oversized or lacks a usable
// Sec-WebSocket-Key; the response is left untouched in those cases.
[[nodiscard]] bool parse_websocket_handshake(std::string_view request, std::string& response);

// Reads one masked client frame. Fails closed on reserved bits, unmasked
// frames, payloads above max_payload and truncated streams.
[[nodiscard]] bool read_ws_client_frame(const WsStreamRead& reader,
                                        std::size_t max_payload,
                                        WsDecodedFrameV1& frame,
                                        WsFrameError& error);

// Writes one FIN-set control frame (close/ping/pong); payloads above 125
// bytes are rejected without writing anything.
[[nodiscard]] bool send_ws_control_frame(const WsStreamWrite& writer,
                                         std::uint8_t opcode,
                                         std::span<const std::uint8_t> payload);

// Runs one step of the serve loop against injectable streams. Pings are
// answered inline (kind=Ping); Pongs are accepted without a reply
// (kind=Pong); closes are answered (kind=Close); binary frames deliver their
// unmasked payload (kind=Binary); any other opcode fails closed.
[[nodiscard]] bool next_ws_binary_message(const WsStreamRead& reader,
                                          const WsStreamWrite& writer,
                                          std::size_t max_payload,
                                          WsMessageKind& kind,
                                          std::vector<std::uint8_t>& binary_payload);

}  // namespace hibiki
