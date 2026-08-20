#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hibiki {

class InterleavedRingBuffer final {
public:
    InterleavedRingBuffer(std::span<float> storage, std::uint32_t channels) noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::size_t capacity_frames() const noexcept { return capacity_frames_; }
    [[nodiscard]] std::size_t available_frames() const noexcept { return available_frames_; }
    [[nodiscard]] std::size_t free_frames() const noexcept {
        return capacity_frames_ - available_frames_;
    }
    [[nodiscard]] bool push(const float* interleaved, std::size_t frames) noexcept;
    [[nodiscard]] bool pop(float* interleaved, std::size_t frames) noexcept;
    void clear() noexcept;

private:
    std::span<float> storage_{};
    std::uint32_t channels_{0};
    std::size_t capacity_frames_{0};
    std::size_t read_frame_{0};
    std::size_t write_frame_{0};
    std::size_t available_frames_{0};
};

class ClockDriftEstimator final {
public:
    void reset() noexcept;
    void observe(double source_frames, double sink_frames, double elapsed_seconds) noexcept;
    [[nodiscard]] double ratio() const noexcept { return ratio_; }
    [[nodiscard]] double drift_ppm() const noexcept { return (ratio_ - 1.0) * 1.0e6; }

private:
    double ratio_{1.0};
};

// Stateless block resampler used by the output-sink prototype. A production
// sink will add persistent source position and a higher-quality filter behind
// the same no-allocation call boundary.
[[nodiscard]] bool linear_resample_interleaved(const float* input,
                                               std::size_t input_frames,
                                               float* output,
                                               std::size_t output_frames,
                                               std::uint32_t channels,
                                               double source_step) noexcept;

// Persistent no-allocation linear SRC. The caller supplies a whole input
// block and enough output storage for that block; the class carries phase and
// one boundary frame into the next call so clock-ratio changes do not reset
// the stream at every block.
class PersistentLinearResampler final {
public:
    [[nodiscard]] bool prepare(std::uint32_t channels, double source_step) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool process(const float* input,
                               std::size_t input_frames,
                               float* output,
                               std::size_t output_capacity_frames,
                               std::size_t& output_frames) noexcept;
    [[nodiscard]] double phase() const noexcept { return phase_; }
    [[nodiscard]] double source_step() const noexcept { return source_step_; }

private:
    std::array<float, 8> previous_{};
    std::uint32_t channels_{0};
    double source_step_{1.0};
    double phase_{0.0};
    bool has_previous_{false};
};

}  // namespace hibiki
