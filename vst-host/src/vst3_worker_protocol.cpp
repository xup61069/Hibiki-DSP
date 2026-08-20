// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_worker_protocol.hpp"

#include <cmath>
#include <cstring>

namespace hibiki {
namespace {

void write_u16(std::uint8_t* bytes, std::uint16_t value) noexcept {
  bytes[0] = static_cast<std::uint8_t>(value & 0xffU);
  bytes[1] = static_cast<std::uint8_t>(value >> 8U);
}
void write_u32(std::uint8_t* bytes, std::uint32_t value) noexcept {
  for (std::uint32_t index = 0U; index < 4U; ++index) {
    bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
  }
}
void write_u64(std::uint8_t* bytes, std::uint64_t value) noexcept {
  for (std::uint32_t index = 0U; index < 8U; ++index) {
    bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
  }
}
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
std::uint64_t read_u64(const std::uint8_t* bytes) noexcept {
  std::uint64_t result = 0U;
  for (std::uint32_t index = 0U; index < 8U; ++index) {
    result |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
  }
  return result;
}
bool valid_type(Vst3WorkerMessageTypeV1 type) noexcept {
  switch (type) {
    case Vst3WorkerMessageTypeV1::Hello:
    case Vst3WorkerMessageTypeV1::HelloAck:
    case Vst3WorkerMessageTypeV1::Heartbeat:
    case Vst3WorkerMessageTypeV1::ProcessBlock:
    case Vst3WorkerMessageTypeV1::ProcessBlockResponse:
    case Vst3WorkerMessageTypeV1::Shutdown:
    case Vst3WorkerMessageTypeV1::Error:
      return true;
  }
  return false;
}
bool audio_type(Vst3WorkerMessageTypeV1 type) noexcept {
  return type == Vst3WorkerMessageTypeV1::ProcessBlock ||
         type == Vst3WorkerMessageTypeV1::ProcessBlockResponse;
}
bool valid_channels(std::uint32_t channels) noexcept {
  return channels == 2U || channels == 6U || channels == 8U;
}
bool valid_audio_format(const Vst3WorkerFrameV1& frame) noexcept {
  return valid_channels(frame.channels) && frame.frames > 0U &&
         frame.frames <= kVst3WorkerMaxFramesV1 &&
         frame.payload_bytes == frame.channels * frame.frames * sizeof(float);
}

}  // namespace

bool encode_vst3_worker_frame_v1(const Vst3WorkerFrameV1& frame,
                                 const std::span<std::uint8_t> destination,
                                 std::size_t& bytes_written) noexcept {
  bytes_written = 0U;
  if (!valid_type(frame.type) || destination.size() < kVst3WorkerHeaderBytesV1 ||
      (audio_type(frame.type) && !valid_audio_format(frame))) {
    return false;
  }
  auto* bytes = destination.data();
  write_u32(bytes, kVst3WorkerProtocolMagicV1);
  write_u16(bytes + 4U, kVst3WorkerProtocolVersionV1);
  write_u16(bytes + 6U, static_cast<std::uint16_t>(frame.type));
  write_u64(bytes + 8U, frame.request_id);
  write_u32(bytes + 16U, frame.channels);
  write_u32(bytes + 20U, frame.frames);
  write_u32(bytes + 24U, frame.payload_bytes);
  write_u32(bytes + 28U, frame.flags);
  write_u32(bytes + 32U, 0U);
  bytes_written = kVst3WorkerHeaderBytesV1;
  return true;
}

bool decode_vst3_worker_frame_v1(const std::span<const std::uint8_t> packet,
                                 Vst3WorkerFrameV1& frame,
                                 Vst3WorkerProtocolErrorV1& error) noexcept {
  frame = {};
  error = Vst3WorkerProtocolErrorV1::None;
  if (packet.size() < kVst3WorkerHeaderBytesV1) {
    error = Vst3WorkerProtocolErrorV1::Truncated;
    return false;
  }
  const auto* bytes = packet.data();
  if (read_u32(bytes) != kVst3WorkerProtocolMagicV1) {
    error = Vst3WorkerProtocolErrorV1::InvalidMagic;
    return false;
  }
  if (read_u16(bytes + 4U) != kVst3WorkerProtocolVersionV1) {
    error = Vst3WorkerProtocolErrorV1::UnsupportedVersion;
    return false;
  }
  frame.type = static_cast<Vst3WorkerMessageTypeV1>(read_u16(bytes + 6U));
  frame.request_id = read_u64(bytes + 8U);
  frame.channels = read_u32(bytes + 16U);
  frame.frames = read_u32(bytes + 20U);
  frame.payload_bytes = read_u32(bytes + 24U);
  frame.flags = read_u32(bytes + 28U);
  if (!valid_type(frame.type)) {
    error = Vst3WorkerProtocolErrorV1::InvalidType;
    return false;
  }
  if (audio_type(frame.type) && !valid_audio_format(frame)) {
    error = Vst3WorkerProtocolErrorV1::InvalidFormat;
    return false;
  }
  if (frame.payload_bytes > packet.size() - kVst3WorkerHeaderBytesV1 ||
      packet.size() != kVst3WorkerHeaderBytesV1 + frame.payload_bytes) {
    error = Vst3WorkerProtocolErrorV1::PayloadMismatch;
    return false;
  }
  return true;
}

bool validate_vst3_worker_audio_frame_v1(const std::span<const std::uint8_t> packet,
                                         Vst3WorkerFrameV1& frame,
                                         std::span<const float>& samples,
                                         Vst3WorkerProtocolErrorV1& error) noexcept {
  samples = {};
  if (!decode_vst3_worker_frame_v1(packet, frame, error) || !audio_type(frame.type)) {
    if (error == Vst3WorkerProtocolErrorV1::None) error = Vst3WorkerProtocolErrorV1::InvalidFormat;
    return false;
  }
  const auto* payload = packet.data() + kVst3WorkerHeaderBytesV1;
  const auto sample_count = static_cast<std::size_t>(frame.channels) * frame.frames;
  for (std::size_t index = 0U; index < sample_count; ++index) {
    float value = 0.0F;
    std::memcpy(&value, payload + index * sizeof(float), sizeof(float));
    if (!std::isfinite(value)) {
      error = Vst3WorkerProtocolErrorV1::NonFiniteSample;
      return false;
    }
  }
  samples = std::span<const float>(reinterpret_cast<const float*>(payload), sample_count);
  return true;
}

}  // namespace hibiki
