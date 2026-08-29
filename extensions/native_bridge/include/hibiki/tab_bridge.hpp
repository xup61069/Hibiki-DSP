#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstddef>
#include <cstdint>
#include <span>
#include <array>
#include <atomic>
#include <thread>

#include "hibiki/audio_engine.hpp"
#include "hibiki/program_loudness.hpp"
#include "hibiki/peq_dsp.hpp"
#include "hibiki/ir_convolver.hpp"
#include "hibiki/noise_suppressor.hpp"

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

struct TabCaptureBlockV1 {
    std::uint32_t frames{0};
    std::uint32_t channels{0};
    std::uint32_t sample_rate{0};
};

struct TabLaneEffectsV1 {
    PeqProcessorV1* peq{nullptr};
    IrConvolverV1* ir{nullptr};
    BasicNoiseSuppressorV1* noise_suppressor{nullptr};
    ProgramAwareLevelControllerV1* program_level{nullptr};
};

// Fixed-capacity SPSC handoff. The WebSocket/control thread may push a
// validated packet; an engine lane may pop into a caller-owned buffer without
// allocation or waiting. Four blocks bound memory and expose overruns.
class TabCaptureQueueV1 final {
public:
    TabCaptureQueueV1() noexcept = default;
    TabCaptureQueueV1(const TabCaptureQueueV1&) = delete;
    TabCaptureQueueV1& operator=(const TabCaptureQueueV1&) = delete;

    [[nodiscard]] bool push(const TabCapturePacketViewV1& view) noexcept;
    [[nodiscard]] bool pop(float* interleaved,
                           std::uint32_t output_capacity_frames,
                           TabCaptureBlockV1& block) noexcept;
    [[nodiscard]] std::uint32_t dropped_blocks() const noexcept;
    // A host may bind the queue before starting its WebSocket producer. A
    // zero rate clears the constraint for portable callers without a sink.
    [[nodiscard]] bool set_expected_sample_rate(std::uint32_t sample_rate) noexcept;
    [[nodiscard]] std::uint32_t expected_sample_rate() const noexcept;
    [[nodiscard]] std::uint32_t sample_rate_mismatch_blocks() const noexcept;

private:
    static constexpr std::uint32_t kSlotCount = 4U;
    static constexpr std::uint32_t kMaxSamples = 8U * 4096U;
    struct Slot {
        std::atomic<std::uint32_t> ready_sequence{0U};
        std::uint32_t frames{0U};
        std::uint32_t channels{0U};
        std::uint32_t sample_rate{0U};
        std::array<float, kMaxSamples> samples{};
    };

    std::atomic<std::uint32_t> producer_sequence_{0U};
    std::atomic<std::uint32_t> consumer_sequence_{0U};
    std::atomic<std::uint32_t> dropped_blocks_{0U};
    std::atomic<std::uint32_t> expected_sample_rate_{0U};
    std::atomic<std::uint32_t> sample_rate_mismatch_blocks_{0U};
    std::array<Slot, kSlotCount> slots_{};
};

// Bridges one queued browser block into the same immutable engine graph used
// by ASIO and other external lanes. The adapter owns no audio storage: the
// caller supplies scratch/input and output buffers.
[[nodiscard]] bool process_tab_capture_lane_v1(
    AudioEngineModel& engine,
    std::size_t lane_index,
    TabCaptureQueueV1& queue,
    float* input_interleaved,
    std::uint32_t input_capacity_frames,
    std::span<RtLaneInputV1> lane_inputs,
    float* output_interleaved,
    std::uint32_t output_capacity_frames,
    TabCaptureBlockV1& block,
    TabLaneEffectsV1* effects = nullptr) noexcept;

// Same tab effects and lane validation, followed by one bounded submit to the
// engine's active WASAPI handoff. Browser capture remains user-gesture gated;
// a missing sink leaves playback/queue state fail-closed.
[[nodiscard]] bool process_tab_capture_lane_to_wasapi_v1(
    AudioEngineModel& engine,
    std::size_t lane_index,
    TabCaptureQueueV1& queue,
    float* input_interleaved,
    std::uint32_t input_capacity_frames,
    std::span<RtLaneInputV1> lane_inputs,
    float* output_interleaved,
    std::uint32_t output_capacity_frames,
    TabCaptureBlockV1& block,
    TabLaneEffectsV1* effects = nullptr) noexcept;

void enqueue_tab_capture_packet_v1(const TabCapturePacketViewV1& view, void* context) noexcept;

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
