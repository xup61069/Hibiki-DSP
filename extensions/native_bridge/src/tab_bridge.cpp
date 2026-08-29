// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/tab_bridge.hpp"

#include "hibiki/ws_transport.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace hibiki {
namespace {

constexpr std::size_t kHeaderBytes = 16U;
constexpr std::size_t kMaxFrames = kTabCaptureMaxFramesV1;

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

bool TabCaptureQueueV1::push(const TabCapturePacketViewV1& view) noexcept {
    if (!supported_channels(view.channels) || !supported_rate(view.sample_rate) ||
        view.frames == 0U || view.frames > kTabCaptureMaxFramesV1 ||
        view.sample_rate == 0U || view.samples_bytes == nullptr ||
        view.sample_count != static_cast<std::size_t>(view.channels) * view.frames) {
        return false;
    }
    const auto expected_rate = expected_sample_rate_.load(std::memory_order_acquire);
    if (expected_rate != 0U && view.sample_rate != expected_rate) {
        sample_rate_mismatch_blocks_.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    const auto producer = producer_sequence_.load(std::memory_order_relaxed);
    const auto consumer = consumer_sequence_.load(std::memory_order_acquire);
    if (producer - consumer >= kSlotCount) {
        dropped_blocks_.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    auto& slot = slots_[producer % kSlotCount];
    for (std::size_t index = 0U; index < view.sample_count; ++index) {
        const auto value = view.sample(index);
        if (!std::isfinite(value)) return false;
        slot.samples[index] = value;
    }
    slot.frames = view.frames;
    slot.channels = view.channels;
    slot.sample_rate = view.sample_rate;
    slot.ready_sequence.store(producer + 1U, std::memory_order_release);
    producer_sequence_.store(producer + 1U, std::memory_order_release);
    return true;
}

bool TabCaptureQueueV1::pop(float* const interleaved,
                            const std::size_t output_capacity_samples,
                            TabCaptureBlockV1& block) noexcept {
    block = {};
    if (interleaved == nullptr) return false;
    const auto consumer = consumer_sequence_.load(std::memory_order_relaxed);
    const auto producer = producer_sequence_.load(std::memory_order_acquire);
    if (consumer == producer) return false;
    auto& slot = slots_[consumer % kSlotCount];
    if (slot.ready_sequence.load(std::memory_order_acquire) != consumer + 1U ||
        slot.frames == 0U || slot.frames > kTabCaptureMaxFramesV1 ||
        slot.channels == 0U || slot.channels > kTabCaptureMaxChannelsV1 ||
        static_cast<std::size_t>(slot.channels) >
            (std::numeric_limits<std::size_t>::max)() / slot.frames) {
        return false;
    }
    const auto sample_count = static_cast<std::size_t>(slot.frames) * slot.channels;
    if (sample_count > output_capacity_samples) return false;
    std::copy_n(slot.samples.data(), sample_count, interleaved);
    block.frames = slot.frames;
    block.channels = slot.channels;
    block.sample_rate = slot.sample_rate;
    consumer_sequence_.store(consumer + 1U, std::memory_order_release);
    return true;
}

std::uint32_t TabCaptureQueueV1::dropped_blocks() const noexcept {
    return dropped_blocks_.load(std::memory_order_relaxed);
}

bool TabCaptureQueueV1::set_expected_sample_rate(const std::uint32_t sample_rate) noexcept {
    if (sample_rate != 0U && !supported_rate(sample_rate)) return false;
    if (producer_sequence_.load(std::memory_order_acquire) != 0U ||
        consumer_sequence_.load(std::memory_order_acquire) != 0U) {
        return false;
    }
    expected_sample_rate_.store(sample_rate, std::memory_order_release);
    return true;
}

std::uint32_t TabCaptureQueueV1::expected_sample_rate() const noexcept {
    return expected_sample_rate_.load(std::memory_order_acquire);
}

std::uint32_t TabCaptureQueueV1::sample_rate_mismatch_blocks() const noexcept {
    return sample_rate_mismatch_blocks_.load(std::memory_order_relaxed);
}

static bool process_tab_capture_lane_impl(
    AudioEngineModel& engine,
    const std::size_t lane_index,
    TabCaptureQueueV1& queue,
    float* const input_interleaved,
    const std::size_t input_capacity_samples,
    const std::span<RtLaneInputV1> lane_inputs,
    float* const output_interleaved,
    const std::uint32_t output_capacity_frames,
    TabCaptureBlockV1& block,
    TabLaneEffectsV1* const effects,
    const bool to_wasapi) noexcept {
    block = {};
    if (input_interleaved == nullptr || output_interleaved == nullptr ||
        input_capacity_samples == 0U || output_capacity_frames == 0U) {
        return false;
    }
    if (!queue.pop(input_interleaved, input_capacity_samples, block) ||
        block.frames == 0U || block.frames > output_capacity_frames) {
        block = {};
        return false;
    }
    if (effects != nullptr) {
        if (effects->peq != nullptr &&
            (effects->peq->sample_rate() != block.sample_rate ||
             effects->peq->channels() != block.channels ||
             !effects->peq->process_interleaved(input_interleaved, block.frames))) {
            block = {};
            return false;
        }
        if (effects->ir != nullptr &&
            (effects->ir->sample_rate() != block.sample_rate ||
             effects->ir->channels() != block.channels ||
             !effects->ir->process_interleaved(input_interleaved, block.frames, block.channels))) {
            block = {};
            return false;
        }
        if (effects->noise_suppressor != nullptr &&
            (effects->noise_suppressor->sample_rate() != block.sample_rate ||
             effects->noise_suppressor->channels() != block.channels ||
             !effects->noise_suppressor->process_interleaved(input_interleaved, block.frames))) {
            block = {};
            return false;
        }
        if (effects->program_level != nullptr &&
            (effects->program_level->sample_rate() != block.sample_rate ||
             !effects->program_level->process_interleaved(input_interleaved, block.frames,
                                                          block.channels))) {
            block = {};
            return false;
        }
    }
    const bool processed = to_wasapi
                               ? engine.process_lane_block_to_wasapi(
                                     lane_index, input_interleaved, block.channels, block.frames,
                                     lane_inputs, output_interleaved)
                               : engine.process_lane_block(lane_index, input_interleaved,
                                                           block.channels, block.frames,
                                                           lane_inputs, output_interleaved);
    if (!processed) {
        block = {};
        return false;
    }
    return true;
}

bool process_tab_capture_lane_v1(
    AudioEngineModel& engine,
    const std::size_t lane_index,
    TabCaptureQueueV1& queue,
    float* const input_interleaved,
    const std::size_t input_capacity_samples,
    const std::span<RtLaneInputV1> lane_inputs,
    float* const output_interleaved,
    const std::uint32_t output_capacity_frames,
    TabCaptureBlockV1& block,
    TabLaneEffectsV1* const effects) noexcept {
    return process_tab_capture_lane_impl(engine, lane_index, queue, input_interleaved,
                                         input_capacity_samples, lane_inputs, output_interleaved,
                                         output_capacity_frames, block, effects, false);
}

bool process_tab_capture_lane_to_wasapi_v1(
    AudioEngineModel& engine,
    const std::size_t lane_index,
    TabCaptureQueueV1& queue,
    float* const input_interleaved,
    const std::size_t input_capacity_samples,
    const std::span<RtLaneInputV1> lane_inputs,
    float* const output_interleaved,
    const std::uint32_t output_capacity_frames,
    TabCaptureBlockV1& block,
    TabLaneEffectsV1* const effects) noexcept {
    return process_tab_capture_lane_impl(engine, lane_index, queue, input_interleaved,
                                         input_capacity_samples, lane_inputs, output_interleaved,
                                         output_capacity_frames, block, effects, true);
}

void enqueue_tab_capture_packet_v1(const TabCapturePacketViewV1& view, void* const context) noexcept {
    if (context != nullptr) {
        (void)static_cast<TabCaptureQueueV1*>(context)->push(view);
    }
}

}  // namespace hibiki

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <array>
#include <string>
#include <vector>

namespace hibiki {
namespace {

constexpr std::size_t kMaxHandshakeBytes = hibiki::kMaxHandshakeBytes;

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

bool receive_handshake(const SOCKET socket) {
    std::string request;
    std::array<char, 1024> chunk{};
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < kMaxHandshakeBytes) {
        const auto received = recv(socket, chunk.data(), static_cast<int>(chunk.size()), 0);
        if (received <= 0) return false;
        request.append(chunk.data(), static_cast<std::size_t>(received));
    }
    std::string response;
    if (!parse_websocket_handshake(request, response)) return false;
    return send_all(socket, response.data(), response.size());
}

void serve_client(const SOCKET socket,
                  const std::size_t max_payload,
                  const TabCapturePacketCallbackV1 callback,
                  void* const context) noexcept {
    if (!receive_handshake(socket)) return;
    const auto reader = [socket](std::span<std::uint8_t> destination) {
        return recv_all(socket, destination.data(), destination.size());
    };
    const auto writer = [socket](std::span<const std::uint8_t> source) {
        return send_all(socket, source.data(), source.size());
    };
    std::size_t packet_count = 0U;
    while (true) {
        WsMessageKind kind{WsMessageKind::Close};
        std::vector<std::uint8_t> payload;
        if (!next_ws_binary_message(reader, writer, max_payload, kind, payload)) return;
        if (kind == WsMessageKind::Close) return;
        if (kind == WsMessageKind::Ping) {
            continue;
        }
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
