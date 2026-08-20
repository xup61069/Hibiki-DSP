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

bool PersistentLinearResampler::prepare(const std::uint32_t channels,
                                        const double source_step) noexcept {
    if (channels == 0 || channels > previous_.size() || !std::isfinite(source_step) ||
        source_step < 0.25 || source_step > 4.0) {
        return false;
    }
    channels_ = channels;
    source_step_ = source_step;
    reset();
    return true;
}

bool PersistentLinearResampler::set_source_step(const double source_step) noexcept {
    if (!std::isfinite(source_step) || source_step < 0.25 || source_step > 4.0) {
        return false;
    }
    source_step_ = source_step;
    return true;
}

void PersistentLinearResampler::reset() noexcept {
    previous_.fill(0.0F);
    phase_ = 0.0;
    has_previous_ = false;
}

bool PersistentLinearResampler::process(const float* const input,
                                        const std::size_t input_frames,
                                        float* const output,
                                        const std::size_t output_capacity_frames,
                                        std::size_t& output_frames) noexcept {
    output_frames = 0;
    if (channels_ == 0 || input == nullptr || output == nullptr || input_frames == 0 ||
        !std::isfinite(phase_) || phase_ < 0.0 || phase_ >= 2.0 ||
        !std::isfinite(source_step_) || source_step_ <= 0.0) {
        return false;
    }

    const std::size_t virtual_frames = input_frames + (has_previous_ ? 1U : 0U);
    if (virtual_frames < 2U) {
        std::copy_n(input, channels_, previous_.data());
        has_previous_ = true;
        return true;
    }
    // Interpolation needs both a left and a right sample, so the last valid
    // left index is virtual_frames - 2.
    const auto available = static_cast<double>(virtual_frames - 2U) - phase_;
    const auto expected = available < 0.0
                              ? 0U
                              : static_cast<std::size_t>(std::floor(available / source_step_)) + 1U;
    if (expected > output_capacity_frames) {
        return false;
    }

    const bool previous_at_zero = has_previous_;
    for (std::size_t frame = 0; frame < expected; ++frame) {
        const auto left = static_cast<std::size_t>(phase_);
        const auto right = left + 1U;
        const float fraction = static_cast<float>(phase_ - static_cast<double>(left));
        for (std::uint32_t channel = 0; channel < channels_; ++channel) {
            const auto sample_at = [&](const std::size_t index) noexcept -> float {
                if (previous_at_zero) {
                    return index == 0U ? previous_[channel] : input[(index - 1U) * channels_ + channel];
                }
                return input[index * channels_ + channel];
            };
            const float a = sample_at(left);
            const float b = sample_at(right);
            output[frame * channels_ + channel] = a + ((b - a) * fraction);
        }
        phase_ += source_step_;
        ++output_frames;
    }

    std::copy_n(input + (input_frames - 1U) * channels_, channels_, previous_.data());
    if (previous_at_zero) {
        phase_ -= static_cast<double>(input_frames);
    } else {
        phase_ -= static_cast<double>(input_frames - 1U);
    }
    phase_ = std::clamp(phase_, 0.0, source_step_);
    has_previous_ = true;
    return true;
}

bool OutputSinkModel::prepare(const std::uint32_t channels,
                              const double source_step) noexcept {
    if (!std::isfinite(source_step) || source_step < 0.25 || source_step > 4.0 ||
        !resampler_.prepare(channels, source_step)) {
        return false;
    }
    base_source_step_ = source_step;
    drift_.reset();
    snapshot_ = OutputSinkClockSnapshotV1{1.0, 0.0, source_step, true};
    return true;
}

void OutputSinkModel::reset() noexcept {
    drift_.reset();
    resampler_.reset();
    snapshot_.ratio = 1.0;
    snapshot_.drift_ppm = 0.0;
    snapshot_.source_step = base_source_step_;
}

void OutputSinkModel::observe_clock(const double source_frames,
                                    const double sink_frames,
                                    const double elapsed_seconds) noexcept {
    if (!snapshot_.prepared) {
        return;
    }
    drift_.observe(source_frames, sink_frames, elapsed_seconds);
    const double ratio = drift_.ratio();
    const double effective_step = base_source_step_ / ratio;
    if (resampler_.set_source_step(effective_step)) {
        snapshot_.ratio = ratio;
        snapshot_.drift_ppm = drift_.drift_ppm();
        snapshot_.source_step = effective_step;
    }
}

bool OutputSinkModel::process(const float* const input,
                              const std::size_t input_frames,
                              float* const output,
                              const std::size_t output_capacity_frames,
                              std::size_t& output_frames) noexcept {
    return snapshot_.prepared && resampler_.process(
                                     input, input_frames, output, output_capacity_frames, output_frames);
}

}  // namespace hibiki
