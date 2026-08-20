// SPDX-License-Identifier: GPL-3.0-only

#ifndef HIBIKI_VST3_WORKER_PROTOCOL_HPP
#define HIBIKI_VST3_WORKER_PROTOCOL_HPP

#include <cstddef>
#include <cstdint>
#include <span>

namespace hibiki {

constexpr std::uint32_t kVst3WorkerProtocolMagicV1 = 0x53564948U;  // HIVS
constexpr std::uint16_t kVst3WorkerProtocolVersionV1 = 1U;
constexpr std::size_t kVst3WorkerHeaderBytesV1 = 36U;
constexpr std::uint32_t kVst3WorkerMaxChannelsV1 = 8U;
constexpr std::uint32_t kVst3WorkerMaxFramesV1 = 4096U;

enum class Vst3WorkerMessageTypeV1 : std::uint16_t {
  Hello = 1U,
  HelloAck = 2U,
  Heartbeat = 3U,
  ProcessBlock = 4U,
  ProcessBlockResponse = 5U,
  Shutdown = 6U,
  Error = 7U,
};

struct Vst3WorkerFrameV1 {
  Vst3WorkerMessageTypeV1 type{Vst3WorkerMessageTypeV1::Error};
  std::uint64_t request_id{0U};
  std::uint32_t channels{0U};
  std::uint32_t frames{0U};
  std::uint32_t payload_bytes{0U};
  std::uint32_t flags{0U};
};

enum class Vst3WorkerProtocolErrorV1 : std::uint8_t {
  None,
  Truncated,
  InvalidMagic,
  UnsupportedVersion,
  InvalidType,
  InvalidFormat,
  PayloadMismatch,
  NonFiniteSample,
};

[[nodiscard]] bool encode_vst3_worker_frame_v1(
    const Vst3WorkerFrameV1& frame,
    std::span<std::uint8_t> destination,
    std::size_t& bytes_written) noexcept;

[[nodiscard]] bool decode_vst3_worker_frame_v1(
    std::span<const std::uint8_t> packet,
    Vst3WorkerFrameV1& frame,
    Vst3WorkerProtocolErrorV1& error) noexcept;

[[nodiscard]] bool validate_vst3_worker_audio_frame_v1(
    std::span<const std::uint8_t> packet,
    Vst3WorkerFrameV1& frame,
    std::span<const float>& samples,
    Vst3WorkerProtocolErrorV1& error) noexcept;

}  // namespace hibiki

#endif
