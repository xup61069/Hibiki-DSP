#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/ir_convolver.hpp"
#include "hibiki/ir_phase_kernel.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace hibiki {

constexpr std::size_t kMaxIrWavBytesV1 = 64U * 1024U * 1024U;

// Playback/offline source decoding shares the RIFF parser with IR loading,
// but a source is not convolver kernel material: callers decoding playback
// sources pass kMaxSourceWavFramesV1 (byte-bound derived), while IR kernel
// loading passes kMaxRealtimeIrTapsV1 explicitly. The default keeps legacy
// one-argument call sites on the wide source bound; prepare_ir_convolver_
// from_wav_v1 still re-validates the realtime tap limit fail-closed.
constexpr std::size_t kMaxSourceWavFramesV1 =
    kMaxIrWavBytesV1 / (sizeof(float) * 1U);

// Control-plane representation of a bounded IR WAV. Samples are interleaved
// by frame, matching the file, while IrConvolverV1 receives a channel-major
// copy during prepare. The RT thread never owns this object or reads a file.
struct IrWavDataV1 {
    std::uint32_t schema_version{1U};
    std::uint32_t sample_rate{0U};
    std::uint16_t channels{0U};
    std::vector<float> interleaved_samples{};

    [[nodiscard]] std::size_t frames() const noexcept {
        return channels == 0U ? 0U : interleaved_samples.size() / channels;
    }
};

struct IrWavDecodeResultV1 {
    IrWavDataV1 data{};
    bool valid{false};
    std::string diagnostic{};
};

// Decode a RIFF/WAVE IR with a fixed memory/tap bound. Supported audio formats
// are IEEE Float32 and signed PCM 16/24/32; malformed chunks, non-finite
// samples, unsupported layouts and oversized files fail closed.
[[nodiscard]] IrWavDecodeResultV1 decode_ir_wav_v1(
    std::span<const std::uint8_t> bytes,
    std::size_t max_frames = kMaxSourceWavFramesV1) noexcept;

// Copy decoded interleaved samples into the convolver's bounded channel-major
// kernel and apply an already validated phase resolution. A mono file may be
// broadcast to the requested render channel count; multi-channel files must
// match it. This is a control-plane prepare operation; the caller atomically
// swaps/commits the convolver.
[[nodiscard]] bool prepare_ir_convolver_from_wav_v1(
    IrConvolverV1& convolver,
    const IrWavDataV1& data,
    const IrPhaseResolutionV1& phase,
    std::uint32_t render_channels = 0U) noexcept;

}  // namespace hibiki
