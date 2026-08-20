// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_worker_pipe.hpp"
#include "hibiki/vst3_worker_protocol.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t kMaxFrameBytes =
    hibiki::kVst3WorkerHeaderBytesV1 +
    static_cast<std::size_t>(hibiki::kVst3WorkerMaxChannelsV1) *
        hibiki::kVst3WorkerMaxFramesV1 * sizeof(float);

bool parse_pipe(int argc, wchar_t** argv, std::wstring& pipe_name) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (std::wstring_view(argv[index]) == L"--hibiki-pipe") {
      pipe_name = argv[index + 1];
      return !pipe_name.empty();
    }
  }
  return false;
}

bool make_control_frame(const hibiki::Vst3WorkerMessageTypeV1 type,
                        const std::uint64_t request_id,
                        std::vector<std::uint8_t>& packet) {
  packet.assign(hibiki::kVst3WorkerHeaderBytesV1, 0U);
  hibiki::Vst3WorkerFrameV1 frame{};
  frame.type = type;
  frame.request_id = request_id;
  std::size_t written = 0U;
  return hibiki::encode_vst3_worker_frame_v1(frame, packet, written) &&
         written == hibiki::kVst3WorkerHeaderBytesV1;
}

}  // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) {
  std::wstring pipe_name;
  if (!parse_pipe(argc, argv, pipe_name)) return 2;

  hibiki::Vst3WorkerPipeV1 pipe;
  if (!pipe.connect_client(pipe_name, 1000U)) return 3;
  std::vector<std::uint8_t> request(kMaxFrameBytes);
  std::vector<std::uint8_t> response;
  for (;;) {
    std::size_t bytes_read = 0U;
    if (!pipe.receive(request, bytes_read)) return 4;
    hibiki::Vst3WorkerFrameV1 frame{};
    hibiki::Vst3WorkerProtocolErrorV1 error{hibiki::Vst3WorkerProtocolErrorV1::None};
    const std::span<const std::uint8_t> packet(request.data(), bytes_read);
    if (!hibiki::decode_vst3_worker_frame_v1(packet, frame, error)) return 5;

    if (frame.type == hibiki::Vst3WorkerMessageTypeV1::Shutdown) break;
    if (frame.type == hibiki::Vst3WorkerMessageTypeV1::Hello) {
      if (!make_control_frame(hibiki::Vst3WorkerMessageTypeV1::HelloAck, frame.request_id,
                              response) || !pipe.send(response)) return 6;
      continue;
    }
    if (frame.type == hibiki::Vst3WorkerMessageTypeV1::Heartbeat) {
      if (!make_control_frame(hibiki::Vst3WorkerMessageTypeV1::Heartbeat, frame.request_id,
                              response) || !pipe.send(response)) return 7;
      continue;
    }
    if (frame.type == hibiki::Vst3WorkerMessageTypeV1::ProcessBlock) {
      std::span<const float> samples;
      if (!hibiki::validate_vst3_worker_audio_frame_v1(packet, frame, samples, error)) return 8;
      response.assign(packet.begin(), packet.end());
      auto* response_bytes = response.data();
      hibiki::Vst3WorkerFrameV1 response_frame = frame;
      response_frame.type = hibiki::Vst3WorkerMessageTypeV1::ProcessBlockResponse;
      std::size_t written = 0U;
      if (!hibiki::encode_vst3_worker_frame_v1(response_frame,
                                               std::span<std::uint8_t>(response_bytes,
                                                                       hibiki::kVst3WorkerHeaderBytesV1),
                                               written) ||
          !pipe.send(response)) return 9;
      continue;
    }
    if (!make_control_frame(hibiki::Vst3WorkerMessageTypeV1::Error, frame.request_id, response) ||
        !pipe.send(response)) return 10;
  }
  pipe.close();
  return 0;
}
#else
int main() { return 1; }
#endif
