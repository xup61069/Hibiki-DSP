// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/tab_bridge.hpp"

#include <cmath>
#include <cstring>

namespace hibiki {
namespace {

constexpr std::size_t kHeaderBytes = 16U;
constexpr std::size_t kMaxFrames = 4096U;

std::uint16_t read_u16(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

bool supported_channels(const std::uint16_t channels) noexcept {
    return channels == 1U || channels == 2U || channels == 6U || channels == 8U;
}

bool supported_rate(const std::uint32_t rate) noexcept {
    return rate == 44100U || rate == 48000U || rate == 96000U || rate == 192000U;
}

}  // namespace

float TabCapturePacketViewV1::sample(const std::size_t index) const noexcept {
    float value = 0.0F;
    if (samples_bytes == nullptr || index >= sample_count) return value;
    std::memcpy(&value, samples_bytes + index * sizeof(float), sizeof(float));
    return value;
}

bool decode_tab_capture_packet_v1(const std::span<const std::uint8_t> packet,
                                  TabCapturePacketViewV1& view,
                                  TabPacketError& error) noexcept {
    view = {};
    error = TabPacketError::None;
    if (packet.size() < kHeaderBytes) {
        error = TabPacketError::Truncated;
        return false;
    }
    const auto* raw = packet.data();
    if (raw[0] != 0x48U || raw[1] != 0x49U || raw[2] != 0x42U || raw[3] != 0x54U) {
        error = TabPacketError::InvalidMagic;
        return false;
    }
    if (read_u16(raw + 4U) != 1U) {
        error = TabPacketError::UnsupportedVersion;
        return false;
    }
    const auto channels = read_u16(raw + 6U);
    const auto frames = read_u32(raw + 8U);
    const auto rate = read_u32(raw + 12U);
    if (!supported_channels(channels)) {
        error = TabPacketError::InvalidChannels;
        return false;
    }
    if (!supported_rate(rate)) {
        error = TabPacketError::InvalidSampleRate;
        return false;
    }
    if (frames == 0U || frames > kMaxFrames) {
        error = TabPacketError::InvalidFrameCount;
        return false;
    }
    const auto sample_count = static_cast<std::size_t>(channels) * frames;
    if (sample_count > (SIZE_MAX - kHeaderBytes) / sizeof(float) ||
        packet.size() != kHeaderBytes + sample_count * sizeof(float)) {
        error = TabPacketError::LengthMismatch;
        return false;
    }
    const auto* samples = raw + kHeaderBytes;
    for (std::size_t index = 0; index < sample_count; ++index) {
        float value = 0.0F;
        std::memcpy(&value, samples + index * sizeof(float), sizeof(float));
        if (!std::isfinite(value)) {
            error = TabPacketError::NonFiniteSample;
            return false;
        }
    }
    view.channels = channels;
    view.frames = frames;
    view.sample_rate = rate;
    view.samples_bytes = samples;
    view.sample_count = sample_count;
    return true;
}

}  // namespace hibiki

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <vector>

namespace hibiki {
namespace {

constexpr std::size_t kMaxHandshakeBytes = 8192U;
constexpr char kWebSocketGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

SOCKET as_socket(const std::uintptr_t value) noexcept { return static_cast<SOCKET>(value); }
std::uintptr_t as_integer(const SOCKET value) noexcept { return static_cast<std::uintptr_t>(value); }

bool send_all(const SOCKET socket, const void* data, const std::size_t bytes) noexcept {
    const auto* raw = static_cast<const char*>(data);
    std::size_t sent = 0U;
    while (sent < bytes) {
        const auto result = send(socket, raw + sent, static_cast<int>(bytes - sent), 0);
        if (result <= 0) return false;
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

bool recv_all(const SOCKET socket, void* data, const std::size_t bytes) noexcept {
    auto* raw = static_cast<char*>(data);
    std::size_t received = 0U;
    while (received < bytes) {
        const auto result = recv(socket, raw + received, static_cast<int>(bytes - received), 0);
        if (result <= 0) return false;
        received += static_cast<std::size_t>(result);
    }
    return true;
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

std::string websocket_accept(const std::string& key) {
    std::string source = key;
    source += kWebSocketGuid;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_length = 0U;
    DWORD bytes_returned = 0U;
    std::string result;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0U) != 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_length),
                          sizeof(object_length), &bytes_returned, 0U) != 0) {
        if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0U);
        return result;
    }
    std::vector<std::uint8_t> object(object_length);
    std::array<std::uint8_t, 20> digest{};
    if (BCryptCreateHash(algorithm, &hash, object.data(), object_length, nullptr, 0U, 0U) == 0 &&
        BCryptHashData(hash, reinterpret_cast<PUCHAR>(source.data()),
                       static_cast<ULONG>(source.size()), 0U) == 0 &&
        BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0U) == 0) {
        result = base64(digest.data(), digest.size());
    }
    if (hash != nullptr) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0U);
    return result;
}

bool receive_handshake(const SOCKET socket) {
    std::string request;
    std::array<char, 1024> chunk{};
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < kMaxHandshakeBytes) {
        const auto received = recv(socket, chunk.data(), static_cast<int>(chunk.size()), 0);
        if (received <= 0) return false;
        request.append(chunk.data(), static_cast<std::size_t>(received));
    }
    const auto end = request.find("\r\n\r\n");
    if (end == std::string::npos) return false;
    std::string lower = request.substr(0U, end);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    const auto key_position = lower.find("sec-websocket-key:");
    if (key_position == std::string::npos) return false;
    const auto value_start = key_position + std::strlen("sec-websocket-key:");
    const auto line_end = request.find("\r\n", value_start);
    if (line_end == std::string::npos) return false;
    auto key = request.substr(value_start, line_end - value_start);
    const auto first = key.find_first_not_of(" \t");
    const auto last = key.find_last_not_of(" \t");
    if (first == std::string::npos || last == std::string::npos) return false;
    key = key.substr(first, last - first + 1U);
    const auto accept = websocket_accept(key);
    if (accept.empty()) return false;
    const std::string response = "HTTP/1.1 101 Switching Protocols\r\n"
                                 "Upgrade: websocket\r\n"
                                 "Connection: Upgrade\r\n"
                                 "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
    return send_all(socket, response.data(), response.size());
}

bool send_control_frame(const SOCKET socket,
                        const std::uint8_t opcode,
                        const std::span<const std::uint8_t> payload) noexcept {
    if (payload.size() > 125U) return false;
    std::array<std::uint8_t, 2> header{static_cast<std::uint8_t>(0x80U | opcode),
                                       static_cast<std::uint8_t>(payload.size())};
    return send_all(socket, header.data(), header.size()) &&
           (payload.empty() || send_all(socket, payload.data(), payload.size()));
}

bool read_frame(const SOCKET socket,
                const std::size_t max_payload,
                std::vector<std::uint8_t>& payload,
                std::uint8_t& opcode) noexcept {
    std::array<std::uint8_t, 2> header{};
    if (!recv_all(socket, header.data(), header.size())) return false;
    if ((header[0] & 0x70U) != 0U || (header[0] & 0x80U) == 0U) return false;
    opcode = static_cast<std::uint8_t>(header[0] & 0x0fU);
    const bool masked = (header[1] & 0x80U) != 0U;
    if (!masked) return false;
    std::uint64_t length = header[1] & 0x7fU;
    if (length == 126U) {
        std::array<std::uint8_t, 2> extended{};
        if (!recv_all(socket, extended.data(), extended.size())) return false;
        length = (static_cast<std::uint64_t>(extended[0]) << 8U) | extended[1];
    } else if (length == 127U) {
        std::array<std::uint8_t, 8> extended{};
        if (!recv_all(socket, extended.data(), extended.size())) return false;
        length = 0U;
        for (const auto byte : extended) length = (length << 8U) | byte;
    }
    if (length > max_payload || length > static_cast<std::uint64_t>(SIZE_MAX)) return false;
    std::array<std::uint8_t, 4> mask{};
    if (!recv_all(socket, mask.data(), mask.size())) return false;
    payload.resize(static_cast<std::size_t>(length));
    if (!payload.empty() && !recv_all(socket, payload.data(), payload.size())) return false;
    for (std::size_t index = 0U; index < payload.size(); ++index) payload[index] ^= mask[index % 4U];
    return true;
}

void serve_client(const SOCKET socket,
                  const std::size_t max_payload,
                  const TabCapturePacketCallbackV1 callback,
                  void* const context) noexcept {
    if (!receive_handshake(socket)) return;
    std::vector<std::uint8_t> payload;
    std::size_t packet_count = 0U;
    while (true) {
        std::uint8_t opcode = 0U;
        if (!read_frame(socket, max_payload, payload, opcode)) return;
        if (opcode == 0x8U) {
            send_control_frame(socket, 0x8U, std::span<const std::uint8_t>{});
            return;
        }
        if (opcode == 0x9U) {
            if (!send_control_frame(socket, 0xAU, payload)) return;
            continue;
        }
        if (opcode != 0x2U) return;
        TabCapturePacketViewV1 view{};
        TabPacketError error{TabPacketError::None};
        if (decode_tab_capture_packet_v1(payload, view, error) && callback != nullptr) {
            callback(view, context);
            ++packet_count;
        }
        // Keep the local counter observable to a debugger without adding a
        // logging dependency to this source-only transport.
        (void)packet_count;
    }
}

}  // namespace

TabBridgeServer::~TabBridgeServer() { stop(); }

bool TabBridgeServer::start(const TabBridgeServerConfigV1& config,
                            const TabCapturePacketCallbackV1 callback,
                            void* const context) noexcept {
    stop();
    if (callback == nullptr || config.port == 0U || config.max_websocket_payload_bytes < 16U ||
        config.max_websocket_payload_bytes > 1024U * 1024U) {
        return false;
    }
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    const auto listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }
    BOOL reuse = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(config.port);
    if (bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(listener, 1) == SOCKET_ERROR) {
        closesocket(listener);
        WSACleanup();
        return false;
    }
    max_payload_bytes_ = config.max_websocket_payload_bytes;
    callback_ = callback;
    callback_context_ = context;
    stop_requested_.store(false, std::memory_order_release);
    listen_socket_.store(as_integer(listener), std::memory_order_release);
    running_.store(true, std::memory_order_release);
    try {
        worker_ = std::thread([this] { run(); });
    } catch (...) {
        running_.store(false, std::memory_order_release);
        close_listen_socket();
        WSACleanup();
        callback_ = nullptr;
        callback_context_ = nullptr;
        return false;
    }
    return true;
}

void TabBridgeServer::close_listen_socket() noexcept {
    const auto value = listen_socket_.exchange(0U, std::memory_order_acq_rel);
    if (value != 0U) closesocket(as_socket(value));
    const auto client = client_socket_.exchange(0U, std::memory_order_acq_rel);
    if (client != 0U) {
        shutdown(as_socket(client), SD_BOTH);
        closesocket(as_socket(client));
    }
}

void TabBridgeServer::stop() noexcept {
    stop_requested_.store(true, std::memory_order_release);
    close_listen_socket();
    if (worker_.joinable()) worker_.join();
    running_.store(false, std::memory_order_release);
    callback_ = nullptr;
    callback_context_ = nullptr;
}

void TabBridgeServer::run() noexcept {
    const auto listener = as_socket(listen_socket_.load(std::memory_order_acquire));
    while (!stop_requested_.load(std::memory_order_acquire)) {
        sockaddr_in client_address{};
        int address_size = sizeof(client_address);
        const auto client = accept(listener, reinterpret_cast<sockaddr*>(&client_address), &address_size);
        if (client == INVALID_SOCKET) break;
        client_socket_.store(as_integer(client), std::memory_order_release);
        serve_client(client, max_payload_bytes_, callback_, callback_context_);
        const auto owned_client = client_socket_.exchange(0U, std::memory_order_acq_rel);
        if (owned_client != 0U) closesocket(as_socket(owned_client));
    }
    const auto owned_listener = listen_socket_.exchange(0U, std::memory_order_acq_rel);
    if (owned_listener != 0U) closesocket(as_socket(owned_listener));
    running_.store(false, std::memory_order_release);
    WSACleanup();
}

}  // namespace hibiki

#else

namespace hibiki {

TabBridgeServer::~TabBridgeServer() { stop(); }

bool TabBridgeServer::start(const TabBridgeServerConfigV1&,
                            const TabCapturePacketCallbackV1,
                            void*) noexcept {
    return false;
}

void TabBridgeServer::stop() noexcept {
    stop_requested_.store(true, std::memory_order_release);
    running_.store(false, std::memory_order_release);
}

void TabBridgeServer::run() noexcept {}
void TabBridgeServer::close_listen_socket() noexcept {}

}  // namespace hibiki

#endif
