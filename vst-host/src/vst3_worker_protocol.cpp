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
    case Vst3WorkerMessageTypeV1::ProcessBlockWithParameters:
    case Vst3WorkerMessageTypeV1::ProcessBlockMultiBus:
    case Vst3WorkerMessageTypeV1::ProcessBlockMultiBusResponse:
      return true;
  }
  return false;
}
bool audio_type(Vst3WorkerMessageTypeV1 type) noexcept {
  return type == Vst3WorkerMessageTypeV1::ProcessBlock ||
         type == Vst3WorkerMessageTypeV1::ProcessBlockResponse ||
         type == Vst3WorkerMessageTypeV1::ProcessBlockWithParameters ||
         type == Vst3WorkerMessageTypeV1::ProcessBlockMultiBus ||
         type == Vst3WorkerMessageTypeV1::ProcessBlockMultiBusResponse;
}
bool valid_channels(std::uint32_t channels) noexcept {
  return channels == 2U || channels == 6U || channels == 8U;
}
bool valid_audio_format(const Vst3WorkerFrameV1& frame) noexcept {
  return valid_channels(frame.channels) && frame.frames > 0U &&
         frame.frames <= kVst3WorkerMaxFramesV1 &&
         frame.payload_bytes == frame.channels * frame.frames * sizeof(float);
}

bool valid_audio_shape(const Vst3WorkerFrameV1& frame) noexcept {
  return valid_channels(frame.channels) && frame.frames > 0U &&
         frame.frames <= kVst3WorkerMaxFramesV1;
}

bool valid_parameter_points(
    const std::span<const Vst3WorkerParameterPointV1> points,
    const std::uint32_t frames) noexcept {
  if (frames == 0U || points.size() > kVst3WorkerMaxParameterPointsV1) return false;
  for (std::size_t index = 0U; index < points.size(); ++index) {
    const auto& point = points[index];
    if (point.sample_offset < 0 || static_cast<std::uint32_t>(point.sample_offset) >= frames ||
        !std::isfinite(point.normalized_value) || point.normalized_value < 0.0 ||
        point.normalized_value > 1.0) {
      return false;
    }
    std::size_t same_parameter = 0U;
    std::size_t unique_parameters = 0U;
    bool seen_current = false;
    for (std::size_t prior = 0U; prior <= index; ++prior) {
      if (points[prior].parameter_id == point.parameter_id) ++same_parameter;
      bool seen = false;
      for (std::size_t before = 0U; before < prior; ++before) {
        if (points[before].parameter_id == points[prior].parameter_id) {
          seen = true;
          break;
        }
      }
      if (!seen) ++unique_parameters;
      if (prior < index && points[prior].parameter_id == point.parameter_id) seen_current = true;
    }
    if (same_parameter > 5U || (!seen_current && unique_parameters > 16U)) return false;
  }
  return true;
}

void write_i32(std::uint8_t* bytes, const std::int32_t value) noexcept {
  write_u32(bytes, static_cast<std::uint32_t>(value));
}

std::int32_t read_i32(const std::uint8_t* bytes) noexcept {
  return static_cast<std::int32_t>(read_u32(bytes));
}

}  // namespace

bool encode_vst3_worker_frame_v1(const Vst3WorkerFrameV1& frame,
                                 const std::span<std::uint8_t> destination,
                                 std::size_t& bytes_written) noexcept {
  bytes_written = 0U;
  if (!valid_type(frame.type) || destination.size() < kVst3WorkerHeaderBytesV1 ||
      frame.payload_bytes > kVst3WorkerMaxPayloadBytesV1 ||
      (frame.type == Vst3WorkerMessageTypeV1::ProcessBlock &&
       !valid_audio_format(frame)) ||
      (frame.type == Vst3WorkerMessageTypeV1::ProcessBlockWithParameters &&
       (!valid_audio_shape(frame) || frame.payload_bytes < kVst3WorkerParameterPrefixBytesV1))) {
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
  if (frame.type == Vst3WorkerMessageTypeV1::ProcessBlock && !valid_audio_format(frame)) {
    error = Vst3WorkerProtocolErrorV1::InvalidFormat;
    return false;
  }
  if (frame.type == Vst3WorkerMessageTypeV1::ProcessBlockWithParameters &&
      (!valid_audio_shape(frame) || frame.payload_bytes > kVst3WorkerMaxPayloadBytesV1)) {
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
  if (!decode_vst3_worker_frame_v1(packet, frame, error) ||
      (frame.type != Vst3WorkerMessageTypeV1::ProcessBlock &&
       frame.type != Vst3WorkerMessageTypeV1::ProcessBlockResponse)) {
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

bool encode_vst3_worker_parameter_frame_v1(
    const Vst3WorkerFrameV1& input_frame,
    const std::span<const Vst3WorkerParameterPointV1> points,
    const std::span<const float> samples,
    const std::span<std::uint8_t> destination,
    std::size_t& bytes_written) noexcept {
  bytes_written = 0U;
  if (input_frame.type != Vst3WorkerMessageTypeV1::ProcessBlockWithParameters ||
      !valid_audio_shape(input_frame) ||
      samples.size() != static_cast<std::size_t>(input_frame.channels) * input_frame.frames ||
      !valid_parameter_points(points, input_frame.frames)) {
    return false;
  }
  const auto payload_bytes = kVst3WorkerParameterPrefixBytesV1 +
      points.size() * kVst3WorkerParameterPointBytesV1 + samples.size() * sizeof(float);
  if (payload_bytes != input_frame.payload_bytes ||
      payload_bytes > kVst3WorkerMaxPayloadBytesV1 ||
      destination.size() < kVst3WorkerHeaderBytesV1 + payload_bytes) {
    return false;
  }
  for (const auto value : samples) {
    if (!std::isfinite(value)) return false;
  }
  std::size_t header_bytes = 0U;
  if (!encode_vst3_worker_frame_v1(input_frame,
                                   destination.subspan(0U, kVst3WorkerHeaderBytesV1),
                                   header_bytes)) {
    return false;
  }
  auto* payload = destination.data() + kVst3WorkerHeaderBytesV1;
  write_u32(payload, static_cast<std::uint32_t>(points.size()));
  write_u32(payload + 4U, 0U);
  for (std::size_t index = 0U; index < points.size(); ++index) {
    auto* record = payload + kVst3WorkerParameterPrefixBytesV1 +
                   index * kVst3WorkerParameterPointBytesV1;
    write_u32(record, points[index].parameter_id);
    write_i32(record + 4U, points[index].sample_offset);
    std::memcpy(record + 8U, &points[index].normalized_value, sizeof(double));
  }
  std::memcpy(payload + kVst3WorkerParameterPrefixBytesV1 +
                  points.size() * kVst3WorkerParameterPointBytesV1,
              samples.data(), samples.size() * sizeof(float));
  bytes_written = kVst3WorkerHeaderBytesV1 + payload_bytes;
  return true;
}

bool validate_vst3_worker_parameter_frame_v1(
    const std::span<const std::uint8_t> packet,
    Vst3WorkerFrameV1& frame,
    const std::span<Vst3WorkerParameterPointV1> points_destination,
    std::size_t& point_count,
    std::span<const float>& samples,
    Vst3WorkerProtocolErrorV1& error) noexcept {
  point_count = 0U;
  samples = {};
  if (!decode_vst3_worker_frame_v1(packet, frame, error) ||
      frame.type != Vst3WorkerMessageTypeV1::ProcessBlockWithParameters) {
    if (error == Vst3WorkerProtocolErrorV1::None) error = Vst3WorkerProtocolErrorV1::InvalidFormat;
    return false;
  }
  const auto* payload = packet.data() + kVst3WorkerHeaderBytesV1;
  if (frame.payload_bytes < kVst3WorkerParameterPrefixBytesV1) {
    error = Vst3WorkerProtocolErrorV1::PayloadMismatch;
    return false;
  }
  const auto count = read_u32(payload);
  if (read_u32(payload + 4U) != 0U || count > kVst3WorkerMaxParameterPointsV1 ||
      count > points_destination.size()) {
    error = Vst3WorkerProtocolErrorV1::InvalidFormat;
    return false;
  }
  const auto point_bytes = static_cast<std::size_t>(count) * kVst3WorkerParameterPointBytesV1;
  const auto sample_bytes = static_cast<std::size_t>(frame.channels) * frame.frames * sizeof(float);
  if (frame.payload_bytes != kVst3WorkerParameterPrefixBytesV1 + point_bytes + sample_bytes) {
    error = Vst3WorkerProtocolErrorV1::PayloadMismatch;
    return false;
  }
  for (std::uint32_t index = 0U; index < count; ++index) {
    const auto* record = payload + kVst3WorkerParameterPrefixBytesV1 +
                         static_cast<std::size_t>(index) * kVst3WorkerParameterPointBytesV1;
    auto& point = points_destination[index];
    point.parameter_id = read_u32(record);
    point.sample_offset = read_i32(record + 4U);
    std::memcpy(&point.normalized_value, record + 8U, sizeof(double));
  }
  if (!valid_parameter_points(points_destination.first(count), frame.frames)) {
    error = Vst3WorkerProtocolErrorV1::InvalidFormat;
    return false;
  }
  const auto* sample_bytes_ptr = payload + kVst3WorkerParameterPrefixBytesV1 + point_bytes;
  for (std::size_t index = 0U; index < sample_bytes / sizeof(float); ++index) {
    float value = 0.0F;
    std::memcpy(&value, sample_bytes_ptr + index * sizeof(float), sizeof(float));
    if (!std::isfinite(value)) {
      error = Vst3WorkerProtocolErrorV1::NonFiniteSample;
      return false;
    }
  }
  point_count = count;
  samples = std::span<const float>(reinterpret_cast<const float*>(sample_bytes_ptr),
                                   sample_bytes / sizeof(float));
  return true;
}

bool encode_vst3_worker_multibus_frame_v1(
    const Vst3WorkerFrameV1& input_frame,
    const Vst3BusLayoutV1& layout,
    std::span<const float> samples,
    std::span<std::uint8_t> destination,
    std::size_t& bytes_written) noexcept {
  bytes_written = 0U;
  if (input_frame.type != Vst3WorkerMessageTypeV1::ProcessBlockMultiBus &&
      input_frame.type != Vst3WorkerMessageTypeV1::ProcessBlockMultiBusResponse) {
    return false;
  }
  if (validate_vst3_bus_layout_v1(layout) != Vst3BusLayoutResultV1::Valid) return false;
  if (input_frame.frames == 0U || input_frame.frames > kVst3WorkerMultibusMaxFramesV1) {
    return false;
  }
  // Recompute geometry from the validated layout; the caller-supplied frame
  // must agree with it exactly.
  std::size_t input_channels = 0U;
  for (std::uint32_t index = 0U; index < layout.input_bus_count; ++index) {
    if (layout.inputs[index].active == 1U) input_channels += layout.inputs[index].channels;
  }
  std::size_t output_channels = 0U;
  for (std::uint32_t index = 0U; index < layout.output_bus_count; ++index) {
    if (layout.outputs[index].active == 1U) output_channels += layout.outputs[index].channels;
  }
  const auto total_samples = (input_channels + output_channels) * input_frame.frames;
  if (samples.size() != total_samples) return false;
  for (const auto value : samples) {
    if (!std::isfinite(value)) return false;
  }
  const auto payload_bytes = kVst3WorkerMultibusPrefixBytesV1 +
                             kVst3WorkerMultibusBusTableBytesV1 +
                             total_samples * sizeof(float);
  if (payload_bytes > kVst3WorkerMaxPayloadBytesV1 ||
      payload_bytes != input_frame.payload_bytes ||
      destination.size() < kVst3WorkerHeaderBytesV1 + payload_bytes) {
    return false;
  }
  Vst3WorkerFrameV1 header = input_frame;
  std::size_t header_written = 0U;
  if (!encode_vst3_worker_frame_v1(header, destination.subspan(0U, kVst3WorkerHeaderBytesV1),
                                   header_written)) {
    return false;
  }
  auto* payload = destination.data() + kVst3WorkerHeaderBytesV1;
  write_u32(payload, 1U);
  write_u32(payload + 4U, layout.input_bus_count);
  write_u32(payload + 8U, layout.output_bus_count);
  for (std::size_t index = 12U; index < kVst3WorkerMultibusPrefixBytesV1; ++index) {
    payload[index] = 0U;
  }
  auto* table = payload + kVst3WorkerMultibusPrefixBytesV1;
  for (std::size_t index = 0U; index < kVst3MaxAudioBusesV1; ++index) {
    auto* record = table + index * kVst3WorkerMultibusBusRecordBytesV1;
    record[0] = static_cast<std::uint8_t>(layout.inputs[index].role);
    record[1] = layout.inputs[index].active;
    record[2] = static_cast<std::uint8_t>(layout.inputs[index].reserved & 0xffU);
    record[3] = static_cast<std::uint8_t>(layout.inputs[index].reserved >> 8U);
    write_u32(record + 4U, layout.inputs[index].channels);
  }
  auto* out_table = table + kVst3MaxAudioBusesV1 * kVst3WorkerMultibusBusRecordBytesV1;
  for (std::size_t index = 0U; index < kVst3MaxAudioBusesV1; ++index) {
    auto* record = out_table + index * kVst3WorkerMultibusBusRecordBytesV1;
    record[0] = static_cast<std::uint8_t>(layout.outputs[index].role);
    record[1] = layout.outputs[index].active;
    record[2] = static_cast<std::uint8_t>(layout.outputs[index].reserved & 0xffU);
    record[3] = static_cast<std::uint8_t>(layout.outputs[index].reserved >> 8U);
    write_u32(record + 4U, layout.outputs[index].channels);
  }
  std::memcpy(payload + kVst3WorkerMultibusPrefixBytesV1 + kVst3WorkerMultibusBusTableBytesV1,
              samples.data(), samples.size() * sizeof(float));
  bytes_written = kVst3WorkerHeaderBytesV1 + payload_bytes;
  return true;
}

bool validate_vst3_worker_multibus_frame_v1(
    const std::span<const std::uint8_t> packet,
    Vst3WorkerFrameV1& frame,
    Vst3WorkerMultibusViewV1& view,
    std::span<const float>& samples,
    Vst3WorkerProtocolErrorV1& error) noexcept {
  samples = {};
  view = {};
  if (!decode_vst3_worker_frame_v1(packet, frame, error) ||
      (frame.type != Vst3WorkerMessageTypeV1::ProcessBlockMultiBus &&
       frame.type != Vst3WorkerMessageTypeV1::ProcessBlockMultiBusResponse)) {
    if (error == Vst3WorkerProtocolErrorV1::None) error = Vst3WorkerProtocolErrorV1::InvalidFormat;
    return false;
  }
  if (frame.payload_bytes < kVst3WorkerMultibusPrefixBytesV1 + kVst3WorkerMultibusBusTableBytesV1 ||
      frame.frames == 0U || frame.frames > kVst3WorkerMultibusMaxFramesV1) {
    error = Vst3WorkerProtocolErrorV1::PayloadMismatch;
    return false;
  }
  const auto* payload = packet.data() + kVst3WorkerHeaderBytesV1;
  if (read_u32(payload) != 1U) {
    error = Vst3WorkerProtocolErrorV1::InvalidFormat;
    return false;
  }
  Vst3BusLayoutV1 layout{};
  layout.schema_version = 1U;
  layout.input_bus_count = read_u32(payload + 4U);
  layout.output_bus_count = read_u32(payload + 8U);
  for (std::size_t index = 12U; index < kVst3WorkerMultibusPrefixBytesV1; ++index) {
    if (payload[index] != 0U) {
      error = Vst3WorkerProtocolErrorV1::InvalidFormat;
      return false;
    }
  }
  const auto* table = payload + kVst3WorkerMultibusPrefixBytesV1;
  for (std::size_t index = 0U; index < kVst3MaxAudioBusesV1; ++index) {
    const auto* record = table + index * kVst3WorkerMultibusBusRecordBytesV1;
    layout.inputs[index].role = static_cast<Vst3BusRoleV1>(record[0]);
    layout.inputs[index].active = record[1];
    layout.inputs[index].reserved =
        static_cast<std::uint16_t>(record[2]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(record[3]) << 8U);
    layout.inputs[index].channels = read_u32(record + 4U);
  }
  const auto* out_table = table + kVst3MaxAudioBusesV1 * kVst3WorkerMultibusBusRecordBytesV1;
  for (std::size_t index = 0U; index < kVst3MaxAudioBusesV1; ++index) {
    const auto* record = out_table + index * kVst3WorkerMultibusBusRecordBytesV1;
    layout.outputs[index].role = static_cast<Vst3BusRoleV1>(record[0]);
    layout.outputs[index].active = record[1];
    layout.outputs[index].reserved =
        static_cast<std::uint16_t>(record[2]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(record[3]) << 8U);
    layout.outputs[index].channels = read_u32(record + 4U);
  }
  if (validate_vst3_bus_layout_v1(layout) != Vst3BusLayoutResultV1::Valid) {
    error = Vst3WorkerProtocolErrorV1::InvalidFormat;
    return false;
  }
  // Recompute sample geometry from the validated embedded layout.
  std::size_t input_channels = 0U;
  for (std::uint32_t index = 0U; index < layout.input_bus_count; ++index) {
    if (layout.inputs[index].active == 1U) input_channels += layout.inputs[index].channels;
  }
  std::size_t output_channels = 0U;
  for (std::uint32_t index = 0U; index < layout.output_bus_count; ++index) {
    if (layout.outputs[index].active == 1U) output_channels += layout.outputs[index].channels;
  }
  const std::size_t input_sample_count = input_channels * frame.frames;
  const std::size_t output_sample_count = output_channels * frame.frames;
  const auto expected_payload = kVst3WorkerMultibusPrefixBytesV1 +
                                kVst3WorkerMultibusBusTableBytesV1 +
                                (input_sample_count + output_sample_count) * sizeof(float);
  if (frame.payload_bytes != expected_payload) {
    error = Vst3WorkerProtocolErrorV1::PayloadMismatch;
    return false;
  }
  const auto* sample_bytes_ptr = payload + kVst3WorkerMultibusPrefixBytesV1 +
                                 kVst3WorkerMultibusBusTableBytesV1;
  const std::size_t total_samples = input_sample_count + output_sample_count;
  for (std::size_t index = 0U; index < total_samples; ++index) {
    float value = 0.0F;
    std::memcpy(&value, sample_bytes_ptr + index * sizeof(float), sizeof(float));
    if (!std::isfinite(value)) {
      error = Vst3WorkerProtocolErrorV1::NonFiniteSample;
      return false;
    }
  }
  view.layout = layout;
  view.frames = frame.frames;
  view.input_sample_count = input_sample_count;
  view.output_sample_offset = input_sample_count;
  view.output_sample_count = output_sample_count;
  samples = std::span<const float>(
      reinterpret_cast<const float*>(sample_bytes_ptr), total_samples);
  return true;
}

bool vst3_worker_multibus_bus_samples_v1(
    const Vst3WorkerMultibusViewV1& view,
    const std::span<const float> samples,
    const bool input,
    const std::uint32_t bus_index,
    std::span<const float>& bus_samples) noexcept {
  bus_samples = {};
  if (view.frames == 0U || view.frames > kVst3WorkerMultibusMaxFramesV1) return false;
  if (samples.size() < view.input_sample_count + view.output_sample_count) return false;
  const auto& buses = input ? view.layout.inputs : view.layout.outputs;
  const auto bus_count =
      input ? view.layout.input_bus_count : view.layout.output_bus_count;
  if (bus_index >= bus_count) return false;
  const auto& bus = buses[bus_index];
  if (bus.active != 1U || bus.channels == 0U) return false;
  // Walk active slots before the requested one to find its offset.
  std::size_t offset_samples = 0U;
  for (std::uint32_t index = 0U; index < bus_index; ++index) {
    if (buses[index].active == 1U) {
      offset_samples += static_cast<std::size_t>(buses[index].channels) * view.frames;
    }
  }
  if (!input) offset_samples += view.output_sample_offset;
  const std::size_t count = static_cast<std::size_t>(bus.channels) * view.frames;
  if (offset_samples + count > samples.size()) return false;
  bus_samples = samples.subspan(offset_samples, count);
  return true;
}

}  // namespace hibiki
