// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/ws_transport.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace hibiki {
namespace {

constexpr char kWebSocketGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::string_view trim_ows(std::string_view value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1U);
}

bool contains_header_token(std::string_view value, const std::string_view token) {
    while (!value.empty()) {
        const auto delimiter = value.find(',');
        const auto candidate = trim_ows(value.substr(0U, delimiter));
        if (candidate == token) return true;
        if (delimiter == std::string_view::npos) return false;
        value.remove_prefix(delimiter + 1U);
    }
    return false;
}

bool find_single_header_value(const std::string_view request,
                              const std::string_view lower_headers,
                              const std::string_view header_name,
                              std::string_view& value) {
    auto line_start = lower_headers.find("\r\n");
    if (line_start == std::string_view::npos) return false;
    line_start += 2U;
    bool found = false;
    while (line_start < lower_headers.size()) {
        const auto line_end = lower_headers.find("\r\n", line_start);
        const auto end = line_end == std::string_view::npos ? lower_headers.size() : line_end;
        const auto separator = lower_headers.find(':', line_start);
        if (separator != std::string_view::npos && separator < end &&
            lower_headers.substr(line_start, separator - line_start) == header_name) {
            if (found) return false;
            value = trim_ows(request.substr(separator + 1U, end - separator - 1U));
            found = true;
        }
        if (line_end == std::string_view::npos) return found;
        line_start = line_end + 2U;
    }
    return found;
}

bool has_header_token(const std::string_view lower_headers,
                      const std::string_view header_name,
                      const std::string_view token) {
    auto line_start = lower_headers.find("\r\n");
    if (line_start == std::string_view::npos) return false;
    line_start += 2U;
    while (line_start < lower_headers.size()) {
        const auto line_end = lower_headers.find("\r\n", line_start);
        const auto end = line_end == std::string_view::npos ? lower_headers.size() : line_end;
        const auto separator = lower_headers.find(':', line_start);
        if (separator != std::string_view::npos && separator < end &&
            lower_headers.substr(line_start, separator - line_start) == header_name &&
            contains_header_token(lower_headers.substr(separator + 1U, end - separator - 1U), token)) {
            return true;
        }
        if (line_end == std::string_view::npos) return false;
        line_start = line_end + 2U;
    }
    return false;
}

bool has_valid_websocket_request_line(const std::string_view request) {
    const auto line_end = request.find("\r\n");
    if (line_end == std::string_view::npos) return false;
    const auto line = request.substr(0U, line_end);
    if (!line.starts_with("GET ")) return false;
    const auto version_start = line.rfind(' ');
    return version_start > 4U && line.find(' ', 4U) == version_start &&
           line.substr(version_start) == " HTTP/1.1";
}

int base64_value(const char character) {
    if (character >= 'A' && character <= 'Z') return character - 'A';
    if (character >= 'a' && character <= 'z') return character - 'a' + 26;
    if (character >= '0' && character <= '9') return character - '0' + 52;
    if (character == '+') return 62;
    if (character == '/') return 63;
    return -1;
}

bool has_valid_websocket_key(const std::string_view key) {
    constexpr std::size_t kEncodedNonceSize = 24U;
    constexpr std::size_t kUnpaddedNonceSize = kEncodedNonceSize - 2U;
    if (key.size() != kEncodedNonceSize || key[kUnpaddedNonceSize] != '=' ||
        key[kUnpaddedNonceSize + 1U] != '=') {
        return false;
    }
    for (std::size_t index = 0U; index < kUnpaddedNonceSize; ++index) {
        if (base64_value(key[index]) < 0) return false;
    }
    return (base64_value(key[kUnpaddedNonceSize - 1U]) & 0x0f) == 0;
}

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
    if (!has_valid_websocket_request_line(request)) return false;
    std::string lower(request.substr(0U, end));
    std::transform(lower.begin(), lower.end(), lower.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (!has_header_token(lower, "upgrade", "websocket") ||
        !has_header_token(lower, "connection", "upgrade")) {
        return false;
    }
    std::string_view host;
    std::string_view key;
    std::string_view version;
    if (!find_single_header_value(request, lower, "host", host) || host.empty() ||
        !find_single_header_value(request, lower, "sec-websocket-key", key) ||
        !has_valid_websocket_key(key) ||
        !find_single_header_value(request, lower, "sec-websocket-version", version) ||
        version != "13") {
        return false;
    }
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
    if ((frame.opcode & 0x08U) != 0U && length > 125U) {
        error = WsFrameError::PayloadTooLarge;
        return false;
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

bool has_valid_ws_close_status(const std::span<const std::uint8_t> payload) {
    if (payload.size() < 2U) return true;
    const auto status = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(payload[0]) << 8U) | static_cast<std::uint16_t>(payload[1]));
    return status >= 1000U && status <= 4999U && status != 1004U && status != 1005U &&
           status != 1006U && status != 1015U;
}

bool is_utf8_continuation(const std::uint8_t byte) {
    return (byte & 0xc0U) == 0x80U;
}

bool has_valid_utf8(const std::span<const std::uint8_t> bytes) {
    for (std::size_t index = 0U; index < bytes.size();) {
        const auto first = bytes[index];
        if (first <= 0x7fU) {
            ++index;
        } else if (first >= 0xc2U && first <= 0xdfU) {
            if (index + 1U >= bytes.size() || !is_utf8_continuation(bytes[index + 1U])) return false;
            index += 2U;
        } else if (first == 0xe0U) {
            if (index + 2U >= bytes.size() || bytes[index + 1U] < 0xa0U ||
                bytes[index + 1U] > 0xbfU || !is_utf8_continuation(bytes[index + 2U])) {
                return false;
            }
            index += 3U;
        } else if ((first >= 0xe1U && first <= 0xecU) || (first >= 0xeeU && first <= 0xefU)) {
            if (index + 2U >= bytes.size() || !is_utf8_continuation(bytes[index + 1U]) ||
                !is_utf8_continuation(bytes[index + 2U])) {
                return false;
            }
            index += 3U;
        } else if (first == 0xedU) {
            if (index + 2U >= bytes.size() || bytes[index + 1U] < 0x80U ||
                bytes[index + 1U] > 0x9fU || !is_utf8_continuation(bytes[index + 2U])) {
                return false;
            }
            index += 3U;
        } else if (first == 0xf0U) {
            if (index + 3U >= bytes.size() || bytes[index + 1U] < 0x90U ||
                bytes[index + 1U] > 0xbfU || !is_utf8_continuation(bytes[index + 2U]) ||
                !is_utf8_continuation(bytes[index + 3U])) {
                return false;
            }
            index += 4U;
        } else if (first >= 0xf1U && first <= 0xf3U) {
            if (index + 3U >= bytes.size() || !is_utf8_continuation(bytes[index + 1U]) ||
                !is_utf8_continuation(bytes[index + 2U]) || !is_utf8_continuation(bytes[index + 3U])) {
                return false;
            }
            index += 4U;
        } else if (first == 0xf4U) {
            if (index + 3U >= bytes.size() || bytes[index + 1U] < 0x80U ||
                bytes[index + 1U] > 0x8fU || !is_utf8_continuation(bytes[index + 2U]) ||
                !is_utf8_continuation(bytes[index + 3U])) {
                return false;
            }
            index += 4U;
        } else {
            return false;
        }
    }
    return true;
}

bool has_valid_ws_close_payload(const std::span<const std::uint8_t> payload) {
    return payload.empty() ||
           (payload.size() >= 2U && has_valid_ws_close_status(payload) &&
            has_valid_utf8(payload.subspan(2U)));
}

bool send_ws_control_frame(const WsStreamWrite& writer,
                           const std::uint8_t opcode,
                           const std::span<const std::uint8_t> payload) {
    if (opcode < 0x8U || opcode > 0xAU) return false;
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
        if (!has_valid_ws_close_payload(frame.payload)) return false;
        if (!send_ws_control_frame(writer, 0x8U, {})) return false;
        kind = WsMessageKind::Close;
        return true;
    }
    if (frame.opcode == 0x9U) {
        if (!send_ws_control_frame(writer, 0xAU, frame.payload)) return false;
        kind = WsMessageKind::Ping;
        return true;
    }
    if (frame.opcode == 0xAU) {
        kind = WsMessageKind::Pong;
        return true;
    }
    if (frame.opcode != 0x2U) return false;
    binary_payload = std::move(frame.payload);
    kind = WsMessageKind::Binary;
    return true;
}

}  // namespace hibiki
