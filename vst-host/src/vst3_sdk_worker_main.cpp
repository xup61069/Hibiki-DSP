// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_sdk_processor.hpp"
#include "hibiki/vst3_worker_pipe.hpp"
#include "hibiki/vst3_worker_protocol.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

constexpr std::size_t kMaxPayload =
    hibiki::kVst3WorkerMaxPayloadBytesV1;
constexpr std::size_t kMaxPacket = hibiki::kVst3WorkerHeaderBytesV1 + kMaxPayload;

struct Arguments {
  std::wstring pipe;
  std::wstring module;
  std::wstring class_id;
  std::uint32_t channels{0U};
  double sample_rate{48000.0};
};

bool parse_u32(std::wstring_view text, std::uint32_t& value) {
  if (text.empty()) return false;
  std::uint64_t parsed = 0U;
  for (const wchar_t character : text) {
    if (character < L'0' || character > L'9') return false;
    parsed = parsed * 10U + static_cast<std::uint64_t>(character - L'0');
    if (parsed > 0xffffffffU) return false;
  }
  value = static_cast<std::uint32_t>(parsed);
  return true;
}

bool parse_rate(std::wstring_view text, double& value) {
  if (text.empty()) return false;
  std::wstring copy(text);
  wchar_t* end = nullptr;
  const double parsed = std::wcstod(copy.c_str(), &end);
  if (end == copy.c_str() || *end != L'\0') return false;
  value = parsed;
  return true;
}

bool parse_arguments(int argc, wchar_t** argv, Arguments& arguments) {
  for (int index = 1; index + 1 < argc; index += 2) {
    const std::wstring_view key(argv[index]);
    const std::wstring_view value(argv[index + 1]);
    if (key == L"--hibiki-pipe") arguments.pipe.assign(value);
    else if (key == L"--plugin") arguments.module.assign(value);
    else if (key == L"--vst3-module") arguments.module.assign(value);
    else if (key == L"--vst3-class") arguments.class_id.assign(value);
    else if (key == L"--vst3-channels") {
      if (!parse_u32(value, arguments.channels)) return false;
    } else if (key == L"--vst3-rate") {
      if (!parse_rate(value, arguments.sample_rate)) return false;
    } else {
      return false;
    }
  }
  return !arguments.pipe.empty() && !arguments.module.empty() &&
         !arguments.class_id.empty() && arguments.channels != 0U;
}

#if defined(_WIN32)
std::string utf8(std::wstring_view value) {
  if (value.empty()) return {};
  const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                           value.data(), static_cast<int>(value.size()),
                                           nullptr, 0, nullptr, nullptr);
  if (required <= 0) return {};
  std::string result(static_cast<std::size_t>(required), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), result.data(), required,
                          nullptr, nullptr) != required) {
    return {};
  }
  return result;
}
#endif

bool make_control_frame(hibiki::Vst3WorkerMessageTypeV1 type,
                        std::uint64_t request_id,
                        std::vector<std::uint8_t>& packet) {
  packet.assign(hibiki::kVst3WorkerHeaderBytesV1, 0U);
  hibiki::Vst3WorkerFrameV1 frame{};
  frame.type = type;
  frame.request_id = request_id;
  std::size_t written = 0U;
  return hibiki::encode_vst3_worker_frame_v1(frame, packet, written) &&
         written == hibiki::kVst3WorkerHeaderBytesV1;
}

bool make_error(std::uint64_t request_id, std::vector<std::uint8_t>& packet) {
  return make_control_frame(hibiki::Vst3WorkerMessageTypeV1::Error, request_id, packet);
}

bool set_layout(std::uint32_t channels, hibiki::Vst3SdkAudioLayoutV1& layout) {
  if (channels == 2U) layout = hibiki::Vst3SdkAudioLayoutV1::stereo;
  else if (channels == 6U) layout = hibiki::Vst3SdkAudioLayoutV1::surround_51;
  else if (channels == 8U) layout = hibiki::Vst3SdkAudioLayoutV1::surround_71;
  else return false;
  return true;
}

}  // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) {
  Arguments arguments{};
  if (!parse_arguments(argc, argv, arguments)) return 2;
  hibiki::Vst3SdkAudioLayoutV1 layout{};
  if (!set_layout(arguments.channels, layout)) return 2;

  const std::string module = utf8(arguments.module);
  const std::string class_id = utf8(arguments.class_id);
  if (module.empty() || class_id.empty()) return 2;
  hibiki::Vst3SdkProcessorV1 processor;
  hibiki::Vst3SdkProcessorConfigV1 config{};
  config.sample_rate = arguments.sample_rate;
  config.layout = layout;
  std::string error;
  if (!processor.open(module, class_id, config, error)) return 3;

  hibiki::Vst3WorkerPipeV1 pipe;
  if (!pipe.connect_client(arguments.pipe, 1000U)) return 4;
  std::vector<std::uint8_t> request(kMaxPacket);
  std::vector<std::uint8_t> response;
  std::array<float, hibiki::kVst3WorkerMaxChannelsV1 * hibiki::kVst3WorkerMaxFramesV1> input{};
  std::array<float, hibiki::kVst3WorkerMaxChannelsV1 * hibiki::kVst3WorkerMaxFramesV1> output{};
  std::array<hibiki::Vst3WorkerParameterPointV1,
             hibiki::kVst3WorkerMaxParameterPointsV1> worker_parameters{};
  std::array<hibiki::Vst3SdkParameterPointV1,
             hibiki::kVst3WorkerMaxParameterPointsV1> sdk_parameters{};
  for (;;) {
    std::size_t bytes_read = 0U;
    if (!pipe.receive(request, bytes_read)) return 5;
    hibiki::Vst3WorkerFrameV1 frame{};
    hibiki::Vst3WorkerProtocolErrorV1 protocol_error{hibiki::Vst3WorkerProtocolErrorV1::None};
    const std::span<const std::uint8_t> packet(request.data(), bytes_read);
    if (!hibiki::decode_vst3_worker_frame_v1(packet, frame, protocol_error)) return 6;
    if (frame.type == hibiki::Vst3WorkerMessageTypeV1::Shutdown) break;
    if (frame.type == hibiki::Vst3WorkerMessageTypeV1::Hello ||
        frame.type == hibiki::Vst3WorkerMessageTypeV1::Heartbeat) {
      if (!make_control_frame(frame.type == hibiki::Vst3WorkerMessageTypeV1::Hello
                                  ? hibiki::Vst3WorkerMessageTypeV1::HelloAck
                                  : hibiki::Vst3WorkerMessageTypeV1::Heartbeat,
                              frame.request_id, response) || !pipe.send(response)) {
        return 7;
      }
      continue;
    }
    if (frame.type != hibiki::Vst3WorkerMessageTypeV1::ProcessBlock &&
        frame.type != hibiki::Vst3WorkerMessageTypeV1::ProcessBlockWithParameters) {
      if (!make_error(frame.request_id, response) || !pipe.send(response)) return 8;
      continue;
    }
    std::span<const float> samples;
    std::size_t parameter_count = 0U;
    bool valid_frame = false;
    if (frame.type == hibiki::Vst3WorkerMessageTypeV1::ProcessBlockWithParameters) {
      valid_frame = hibiki::validate_vst3_worker_parameter_frame_v1(
          packet, frame, std::span<hibiki::Vst3WorkerParameterPointV1>(worker_parameters),
          parameter_count, samples, protocol_error);
      if (valid_frame) {
        for (std::size_t index = 0U; index < parameter_count; ++index) {
          sdk_parameters[index].parameter_id = worker_parameters[index].parameter_id;
          sdk_parameters[index].sample_offset = worker_parameters[index].sample_offset;
          sdk_parameters[index].normalized_value = worker_parameters[index].normalized_value;
        }
      }
    } else {
      valid_frame = hibiki::validate_vst3_worker_audio_frame_v1(packet, frame, samples,
                                                                 protocol_error);
    }
    if (!valid_frame || frame.channels != arguments.channels) {
      if (!make_error(frame.request_id, response) || !pipe.send(response)) return 9;
      continue;
    }
    const std::size_t sample_count = static_cast<std::size_t>(frame.channels) * frame.frames;
    std::copy_n(samples.data(), sample_count, input.data());
    const auto process_result = processor.process(
        input.data(), output.data(), frame.frames,
        std::span<const hibiki::Vst3SdkParameterPointV1>(sdk_parameters.data(), parameter_count));
    if (process_result != hibiki::Vst3SdkProcessResultV1::ok) {
      if (!make_error(frame.request_id, response) || !pipe.send(response)) return 10;
      continue;
    }
    response.assign(hibiki::kVst3WorkerHeaderBytesV1 + sample_count * sizeof(float), 0U);
    hibiki::Vst3WorkerFrameV1 response_frame = frame;
    response_frame.type = hibiki::Vst3WorkerMessageTypeV1::ProcessBlockResponse;
    response_frame.payload_bytes = static_cast<std::uint32_t>(sample_count * sizeof(float));
    std::size_t written = 0U;
    if (!hibiki::encode_vst3_worker_frame_v1(response_frame,
                                             std::span<std::uint8_t>(response.data(),
                                                                     hibiki::kVst3WorkerHeaderBytesV1),
                                             written)) {
      return 11;
    }
    std::memcpy(response.data() + hibiki::kVst3WorkerHeaderBytesV1, output.data(),
                sample_count * sizeof(float));
    if (!pipe.send(response)) return 12;
  }
  pipe.close();
  processor.close();
  return 0;
}
#else
int main() { return 1; }
#endif
