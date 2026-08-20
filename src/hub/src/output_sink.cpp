#include "hibiki/output_sink.hpp"

#include <cmath>

namespace hibiki {

InterleavedRingBuffer::InterleavedRingBuffer(const std::span<float> storage,
                                             const std::uint32_t channels) noexcept
    : storage_(storage), channels_(channels), capacity_frames_(channels == 0 ? 0 : storage.size() / channels) {}

bool InterleavedRingBuffer::valid() const noexcept {
    return channels_ > 0 && channels_ <= 8 && capacity_frames_ > 1 &&
           capacity_frames_ * static_cast<std::size_t>(channels_) <= storage_.size();
}

bool InterleavedRingBuffer::push(const float* const interleaved,
                                 const std::size_t frames) noexcept {
    if (!valid() || interleaved == nullptr || frames > free_frames()) {
        return false;
    }
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto source = interleaved + frame * channels_;
        auto* destination = storage_.data() + write_frame_ * channels_;
        std::copy_n(source, channels_, destination);
        write_frame_ = (write_frame_ + 1) % capacity_frames_;
    }
    available_frames_ += frames;
    return true;
}

bool InterleavedRingBuffer::pop(float* const interleaved, const std::size_t frames) noexcept {
    if (!valid() || interleaved == nullptr || frames > available_frames_) {
        return false;
    }
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto* source = storage_.data() + read_frame_ * channels_;
        auto* destination = interleaved + frame * channels_;
        std::copy_n(source, channels_, destination);
        read_frame_ = (read_frame_ + 1) % capacity_frames_;
    }
    available_frames_ -= frames;
    return true;
}

void InterleavedRingBuffer::clear() noexcept {
    read_frame_ = 0;
    write_frame_ = 0;
    available_frames_ = 0;
}

void ClockDriftEstimator::reset() noexcept {
    ratio_ = 1.0;
}

void ClockDriftEstimator::observe(const double source_frames,
                                  const double sink_frames,
                                  const double elapsed_seconds) noexcept {
    if (!std::isfinite(source_frames) || !std::isfinite(sink_frames) ||
        !std::isfinite(elapsed_seconds) || source_frames <= 0.0 || sink_frames <= 0.0 ||
        elapsed_seconds <= 0.0) {
        return;
    }
    const double target = std::clamp(sink_frames / source_frames, 1.0 - 500.0e-6, 1.0 + 500.0e-6);
    ratio_ = std::clamp((ratio_ * 0.99) + (target * 0.01), 1.0 - 500.0e-6, 1.0 + 500.0e-6);
}

bool linear_resample_interleaved(const float* const input,
                                 const std::size_t input_frames,
                                 float* const output,
                                 const std::size_t output_frames,
                                 const std::uint32_t channels,
                                 const double source_step) noexcept {
    if (input == nullptr || output == nullptr || input_frames == 0 || output_frames == 0 ||
        channels == 0 || channels > 8 || !std::isfinite(source_step) || source_step <= 0.0) {
        return false;
    }
    for (std::size_t output_frame = 0; output_frame < output_frames; ++output_frame) {
        const double source_position = static_cast<double>(output_frame) * source_step;
        const auto left = static_cast<std::size_t>(source_position);
        const auto right = std::min(left + 1, input_frames - 1);
        const float fraction = static_cast<float>(source_position - static_cast<double>(left));
        if (left >= input_frames) {
            return false;
        }
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            const float a = input[left * channels + channel];
            const float b = input[right * channels + channel];
            output[output_frame * channels + channel] = a + ((b - a) * fraction);
        }
    }
    return true;
}

}  // namespace hibiki
