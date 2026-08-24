// SPDX-License-Identifier: GPL-3.0-only

#ifndef HIBIKI_VST3_WORKER_PROTOCOL_HPP
#define HIBIKI_VST3_WORKER_PROTOCOL_HPP

#include <cstddef>
#include <cstdint>
#include <span>

 #include "hibiki/vst3_bus_layout.hpp"

namespace hibiki {

constexpr std::uint32_t kVst3WorkerProtocolMagicV1 = 0x53564948U;  // HIVS
constexpr std::uint16_t kVst3WorkerProtocolVersionV1 = 1U;
constexpr std::size_t kVst3WorkerHeaderBytesV1 = 36U;
constexpr std::uint32_t kVst3WorkerMaxChannelsV1 = 8U;
constexpr std::uint32_t kVst3WorkerMaxFramesV1 = 4096U;
constexpr std::uint32_t kVst3WorkerMaxParameterPointsV1 = 64U;
constexpr std::size_t kVst3WorkerParameterPrefixBytesV1 = 8U;
constexpr std::size_t kVst3WorkerParameterPointBytesV1 = 16U;
// Multi-bus blocks keep the shared payload budget affordable: 32 input +
// 32 output channels at 512 frames stays below kVst3WorkerMaxPayloadBytesV1.
constexpr std::uint32_t kVst3WorkerMultibusMaxFramesV1 = 512U;
// Multi-bus payload prefix:
// [u32 schema_version=1][u32 input_bus_count][u32 output_bus_count]
// [16 bytes reserved, must be zero]
constexpr std::size_t kVst3WorkerMultibusPrefixBytesV1 = 28U;
// One wire record per bus slot, matching Vst3AudioBusV1 field order:
// [u8 role][u8 active][u16 reserved=0][u32 channels], little-endian.
constexpr std::size_t kVst3WorkerMultibusBusRecordBytesV1 = 8U;
constexpr std::size_t kVst3WorkerMultibusBusTableBytesV1 =
    2U * kVst3MaxAudioBusesV1 * kVst3WorkerMultibusBusRecordBytesV1;
constexpr std::size_t kVst3WorkerMaxPayloadBytesV1 =
    kVst3WorkerParameterPrefixBytesV1 +
    static_cast<std::size_t>(kVst3WorkerMaxParameterPointsV1) *
        kVst3WorkerParameterPointBytesV1 +
    static_cast<std::size_t>(kVst3WorkerMaxChannelsV1) *
        kVst3WorkerMaxFramesV1 * sizeof(float);
static_assert(
    kVst3WorkerMultibusPrefixBytesV1 + kVst3WorkerMultibusBusTableBytesV1 +
            64U * kVst3WorkerMultibusMaxFramesV1 * sizeof(float) <=
        kVst3WorkerMaxPayloadBytesV1,
    "multi-bus worst case must stay inside the shared v1 payload budget");

// Decoded multi-bus geometry. Samples travel bus-ordered: every active input
// bus in slot order, then every active output bus in slot order. Inactive
// slots contribute nothing.
struct Vst3WorkerMultibusViewV1 {
  Vst3BusLayoutV1 layout{};
  std::uint32_t frames{0U};
  std::size_t input_sample_count{0U};
  std::size_t output_sample_offset{0U};
  std::size_t output_sample_count{0U};
};

enum class Vst3WorkerMessageTypeV1 : std::uint16_t {
  Hello = 1U,
  HelloAck = 2U,
  Heartbeat = 3U,
  ProcessBlock = 4U,
  ProcessBlockResponse = 5U,
  Shutdown = 6U,
  Error = 7U,
  ProcessBlockWithParameters = 8U,
  ProcessBlockMultiBus = 9U,
  ProcessBlockMultiBusResponse = 10U,
};

struct Vst3WorkerParameterPointV1 {
  std::uint32_t parameter_id{0U};
  std::int32_t sample_offset{0};
  double normalized_value{0.0};
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

// Parameter frames extend the v1 audio payload without changing the existing
// ProcessBlock bytes: [u32 point_count][u32 reserved][point records][Float32].
// The destination array is caller-owned and bounded; no allocation occurs.
[[nodiscard]] bool encode_vst3_worker_parameter_frame_v1(
    const Vst3WorkerFrameV1& frame,
    std::span<const Vst3WorkerParameterPointV1> points,
    std::span<const float> samples,
    std::span<std::uint8_t> destination,
    std::size_t& bytes_written) noexcept;

[[nodiscard]] bool validate_vst3_worker_parameter_frame_v1(
    std::span<const std::uint8_t> packet,
    Vst3WorkerFrameV1& frame,
    std::span<Vst3WorkerParameterPointV1> points_destination,
    std::size_t& point_count,
    std::span<const float>& samples,
    Vst3WorkerProtocolErrorV1& error) noexcept;

// Side-chain/multi-bus extension of the v1 audio frame. The wire payload is
// self-describing: fixed prefix + 16 bus records + interleaved Float32
// samples in bus order (active inputs, then active outputs). The embedded
// layout must satisfy validate_vst3_bus_layout_v1 exactly, including the
// main-input/main-output-first rules; anything else fails closed without
// touching caller-owned state. No allocation, locking, waiting, or I/O.
[[nodiscard]] bool encode_vst3_worker_multibus_frame_v1(
    const Vst3WorkerFrameV1& frame,
    const Vst3BusLayoutV1& layout,
    std::span<const float> samples,
    std::span<std::uint8_t> destination,
    std::size_t& bytes_written) noexcept;

[[nodiscard]] bool validate_vst3_worker_multibus_frame_v1(
    std::span<const std::uint8_t> packet,
    Vst3WorkerFrameV1& frame,
    Vst3WorkerMultibusViewV1& view,
    std::span<const float>& samples,
    Vst3WorkerProtocolErrorV1& error) noexcept;

// Slices one bus out of a validated multi-bus sample span. Fails closed for
// inactive, out-of-range, or non-audio bus slots.
[[nodiscard]] bool vst3_worker_multibus_bus_samples_v1(
    const Vst3WorkerMultibusViewV1& view,
    std::span<const float> samples,
    bool input,
    std::uint32_t bus_index,
    std::span<const float>& bus_samples) noexcept;

}  // namespace hibiki

#endif
