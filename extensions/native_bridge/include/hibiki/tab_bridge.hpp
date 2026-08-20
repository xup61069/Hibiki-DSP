#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstddef>
#include <cstdint>
#include <span>
#include <atomic>
#include <thread>

namespace hibiki {

enum class TabPacketError : std::uint8_t {
    None,
    Truncated,
    InvalidMagic,
    UnsupportedVersion,
    InvalidChannels,
    InvalidSampleRate,
    InvalidFrameCount,
    LengthMismatch,
    NonFiniteSample,
};

struct TabCapturePacketViewV1 {
    std::uint16_t channels{0};
    std::uint32_t frames{0};
    std::uint32_t sample_rate{0};
    const std::uint8_t* samples_bytes{nullptr};
    std::size_t sample_count{0};

    [[nodiscard]] float sample(std::size_t index) const noexcept;
};

[[nodiscard]] bool decode_tab_capture_packet_v1(
    std::span<const std::uint8_t> packet,
    TabCapturePacketViewV1& view,
    TabPacketError& error) noexcept;

using TabCapturePacketCallbackV1 = void (*)(const TabCapturePacketViewV1& view, void* context);

struct TabBridgeServerConfigV1 {
    std::uint16_t port{17842};
    std::size_t max_websocket_payload_bytes{256U * 1024U};
};

// Windows loopback WebSocket receiver for the MV3 HIBT packetizer. The
// callback runs on this control thread and must enqueue/copy into an engine
// lane; it is never the Hibiki RT thread.
class TabBridgeServer final {
public:
    TabBridgeServer() noexcept = default;
    ~TabBridgeServer();

    TabBridgeServer(const TabBridgeServer&) = delete;
    TabBridgeServer& operator=(const TabBridgeServer&) = delete;

    [[nodiscard]] bool start(const TabBridgeServerConfigV1& config,
                             TabCapturePacketCallbackV1 callback,
                             void* context) noexcept;
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept { return running_.load(std::memory_order_acquire); }

private:
    void run() noexcept;
    void close_listen_socket() noexcept;

    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> running_{false};
    std::atomic<std::uintptr_t> listen_socket_{0};
    std::atomic<std::uintptr_t> client_socket_{0};
    std::size_t max_payload_bytes_{256U * 1024U};
    TabCapturePacketCallbackV1 callback_{nullptr};
    void* callback_context_{nullptr};
    std::thread worker_;
};

}  // namespace hibiki
