// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/ws_transport.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace hibiki {
namespace {

constexpr char kWebSocketGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::string base64(const std::uint8_t* bytes, const std::size_t size) {
    constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((size + 2U) / 3U) * 4U);
    for (std::size_t index = 0U; index < size; index += 3U) {
        const auto a = bytes[index];
        const auto b = index + 1U < size ? bytes[index + 1U] : 0U;
        const auto c = index + 2U < size ? bytes[index + 2U] : 0U;
        result.push_back(alphabet[(a >> 2U) & 0x3fU]);
        result.push_back(alphabet[((a & 0x3U) << 4U) | (b >> 4U)]);
        result.push_back(index + 1U < size ? alphabet[((b & 0xfU) << 2U) | (c >> 6U)] : '=');
        result.push_back(index + 2U < size ? alphabet[c & 0x3fU] : '=');
    }
    return result;
}

void sha1(const std::uint8_t* data, const std::size_t size, std::uint8_t digest[20]) {
    constexpr std::uint32_t kInitialState[5] = {
        0x67452301U, 0xEFCDAB89U, 0x98BADCFEU, 0x10325476U, 0xC3D2E1F0U};
    std::uint32_t state[5]{};
    std::copy(kInitialState, kInitialState + 5U, state);

    std::uint64_t bit_length = static_cast<std::uint64_t>(size) * 8U;
    std::vector<std::uint8_t> message(data, data + size);
    message.push_back(0x80U);
    while (message.size() % 64U != 56U) message.push_back(0U);
    for (int shift = 7; shift >= 0; --shift) {
        message.push_back(static_cast<std::uint8_t>((bit_length >> (shift * 8U)) & 0xffU));
    }

    const auto rotl = [](std::uint32_t value, unsigned bits) {
        return (value << bits) | (value >> (32U - bits));
    };
    std::array<std::uint32_t, 80> schedule{};
    for (std::size_t offset = 0U; offset < message.size(); offset += 64U) {
        for (std::size_t word = 0U; word < 16U; ++word) {
            const auto base = offset + word * 4U;
            schedule[word] = (static_cast<std::uint32_t>(message[base]) << 24U) |
                             (static_cast<std::uint32_t>(message[base + 1U]) << 16U) |
                             (static_cast<std::uint32_t>(message[base + 2U]) << 8U) |
                             static_cast<std::uint32_t>(message[base + 3U]);
        }
        for (std::size_t word = 16U; word < 80U; ++word) {
            schedule[word] = rotl(schedule[word - 3U] ^ schedule[word - 8U] ^
                                      schedule[word - 14U] ^ schedule[word - 16U],
                                  1U);
        }
        auto a = state[0];
        auto b = state[1];
        auto c = state[2];
        auto d = state[3];
        auto e = state[4];
        for (std::size_t word = 0U; word < 80U; ++word) {
            std::uint32_t f = 0U;
            std::uint32_t k = 0U;
            if (word < 20U) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999U;
            } else if (word < 40U) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1U;
            } else if (word < 60U) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCU;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6U;
            }
            const auto temp = rotl(a, 5U) + f + e + k + schedule[word];
            e = d;
            d = c;
            c = rotl(b, 30U);
            b = a;
            a = temp;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
    }
    for (std::size_t word = 0U; word < 5U; ++word) {
        digest[word * 4U] = static_cast<std::uint8_t>((state[word] >> 24U) & 0xffU);
        digest[word * 4U + 1U] = static_cast<std::uint8_t>((state[word] >> 16U) & 0xffU);
        digest[word * 4U + 2U] = static_cast<std::uint8_t>((state[word] >> 8U) & 0xffU);
        digest[word * 4U + 3U] = static_cast<std::uint8_t>(state[word] & 0xffU);
    }
}

}  // namespace

bool websocket_compute_accept(const std::string_view key, std::string& accept) {
    std::string source(key);
    source += kWebSocketGuid;
    std::array<std::uint8_t, 20> hash{};
    sha1(reinterpret_cast<const std::uint8_t*>(source.data()), source.size(), hash.data());
    accept = base64(hash.data(), hash.size());
    return true;
}

bool parse_websocket_handshake(const std::string_view request, std::string& response) {
    if (request.size() > kMaxHandshakeBytes) return false;
    const auto end = request.find("\r\n\r\n");
    if (end == std::string_view::npos) return false;
    std::string lower(request.substr(0U, end));
    std::transform(lower.begin(), lower.end(), lower.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    constexpr std::string_view kKeyHeader = "sec-websocket-key:";
    const auto key_position = lower.find(kKeyHeader);
    if (key_position == std::string_view::npos) return false;
    const auto value_start = key_position + kKeyHeader.size();
    const auto line_end = request.find("\r\n", value_start);
    if (line_end == std::string_view::npos) return false;
    auto key = request.substr(value_start, line_end - value_start);
    const auto first = key.find_first_not_of(" \t");
    const auto last = key.find_last_not_of(" \t");
    if (first == std::string_view::npos || last == std::string_view::npos) return false;
    key = key.substr(first, last - first + 1U);
    std::string accept;
    if (!websocket_compute_accept(key, accept)) return false;
    response.clear();
    response += "HTTP/1.1 101 Switching Protocols\r\n";
    response += "Upgrade: websocket\r\n";
    response += "Connection: Upgrade\r\n";
    response += "Sec-WebSocket-Accept: ";
    response += accept;
    response += "\r\n\r\n";
    return true;
}

bool read_ws_client_frame(const WsStreamRead& reader,
                          const std::size_t max_payload,
                          WsDecodedFrameV1& frame,
                          WsFrameError& error) {
    std::array<std::uint8_t, 2> header{};
    if (!reader({header.data(), header.size()})) {
        error = WsFrameError::IncompleteFrame;
        return false;
    }
    if ((header[0] & 0x70U) != 0U || (header[0] & 0x80U) == 0U) {
        error = WsFrameError::ReservedBitsSet;
        return false;
    }
    frame.opcode = static_cast<std::uint8_t>(header[0] & 0x0fU);
    const bool masked = (header[1] & 0x80U) != 0U;
    if (!masked) {
        error = WsFrameError::UnmaskedClientFrame;
        return false;
    }
    std::uint64_t length = header[1] & 0x7fU;
    if (length == 126U) {
        std::array<std::uint8_t, 2> extended{};
        if (!reader({extended.data(), extended.size()})) {
            error = WsFrameError::TruncatedPayload;
            return false;
        }
        length = (static_cast<std::uint64_t>(extended[0]) << 8U) | extended[1];
    } else if (length == 127U) {
        std::array<std::uint8_t, 8> extended{};
        if (!reader({extended.data(), extended.size()})) {
            error = WsFrameError::TruncatedPayload;
            return false;
        }
        length = 0U;
        for (const auto byte : extended) length = (length << 8U) | byte;
    }
    if (length > max_payload || length > static_cast<std::uint64_t>(SIZE_MAX)) {
        error = WsFrameError::PayloadTooLarge;
        return false;
    }
    std::array<std::uint8_t, 4> mask{};
    if (!reader({mask.data(), mask.size()})) {
        error = WsFrameError::TruncatedPayload;
        return false;
    }
    frame.payload.resize(static_cast<std::size_t>(length));
    if (!frame.payload.empty() && !reader({frame.payload.data(), frame.payload.size()})) {
        error = WsFrameError::TruncatedPayload;
        return false;
    }
    for (std::size_t index = 0U; index < frame.payload.size(); ++index) {
        frame.payload[index] ^= mask[index % 4U];
    }
    error = WsFrameError::None;
    return true;
}

bool send_ws_control_frame(const WsStreamWrite& writer,
                           const std::uint8_t opcode,
                           const std::span<const std::uint8_t> payload) {
    if (payload.size() > 125U) return false;
    std::array<std::uint8_t, 2> header{static_cast<std::uint8_t>(0x80U | opcode),
                                       static_cast<std::uint8_t>(payload.size())};
    return writer({header.data(), header.size()}) &&
           (payload.empty() || writer(payload));
}

bool next_ws_binary_message(const WsStreamRead& reader,
                            const WsStreamWrite& writer,
                            const std::size_t max_payload,
                            WsMessageKind& kind,
                            std::vector<std::uint8_t>& binary_payload) {
    kind = WsMessageKind::Close;
    binary_payload.clear();
    WsDecodedFrameV1 frame{};
    WsFrameError error{WsFrameError::None};
    if (!read_ws_client_frame(reader, max_payload, frame, error)) return false;
    if (frame.opcode == 0x8U) {
        if (!send_ws_control_frame(writer, 0x8U, {})) return false;
        kind = WsMessageKind::Close;
        return true;
    }
    if (frame.opcode == 0x9U) {
        if (!send_ws_control_frame(writer, 0xAU, frame.payload)) return false;
        kind = WsMessageKind::Ping;
        return true;
    }
    if (frame.opcode != 0x2U) return false;
    binary_payload = std::move(frame.payload);
    kind = WsMessageKind::Binary;
    return true;
}

}  // namespace hibiki
