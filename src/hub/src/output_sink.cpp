#include "hibiki/output_sink.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr std::size_t kPolyphasePhasesV1 = 8U;
constexpr std::size_t kPolyphaseTapsPerPhaseV1 = 16U;

struct PolyphaseCoefficientSetV1 {
    std::array<float, kPolyphasePhasesV1 * kPolyphaseTapsPerPhaseV1> values{};
};

// Generated offline for the fixed 8-phase/16-tap filter. Each phase is
// normalized before conversion to float; no coefficients are constructed on
// the RT path.
constexpr PolyphaseCoefficientSetV1 kPolyphaseCoefficientsV1{{
    // phase 0
    0.00272439187f, -0.00446147332f, -0.00613798527f, 0.0272478927f, 0.000897719001f, -0.0956714004f, 0.0825827494f, 0.492818117f,
    0.492818117f, 0.0825827494f, -0.0956714004f, 0.000897719001f, 0.0272478927f, -0.00613798527f, -0.00446147332f, 0.00272439187f,
    // phase 1
    0.00305678579f, -0.00343931722f, -0.00845834054f, 0.0243746396f, 0.0123529620f, -0.0932664424f, 0.0383577533f, 0.454429179f,
    0.524551690f, 0.130427301f, -0.0933871418f, -0.0114517501f, 0.0289098807f, -0.00340146688f, -0.00530046504f, 0.00224475982f,
    // phase 2
    0.00323166139f, -0.00228659133f, -0.0102777211f, 0.0204804726f, 0.0224880520f, -0.0867102370f, -0.00139627315f, 0.410362005f,
    0.548803091f, 0.180911019f, -0.0859831199f, -0.0242019258f, 0.0292094685f, -0.000355407246f, -0.00590939913f, 0.00163489545f,
    // phase 3
    0.00324563403f, -0.00105941040f, -0.0115352450f, 0.0157863032f, 0.0309538674f, -0.0766290948f, -0.0359748490f, 0.361719131f,
    0.564917862f, 0.232946023f, -0.0731533319f, -0.0368032008f, 0.0280426405f, 0.00287471013f, -0.00624915957f, 0.000918124511f,
    // phase 4
    0.00139364938f, 0.0000830259887f, -0.00547875464f, 0.00473264698f, 0.0168402195f, -0.0286250915f, -0.0291272067f, 0.139134109f,
    0.807913721f, 0.128199548f, -0.0245880634f, -0.0218622498f, 0.0113928095f, 0.00276213186f, -0.00282595051f, 0.0000554573708f,
    // phase 5
    0.00281117205f, 0.00138954632f, -0.0122477310f, 0.00497932453f, 0.0419037156f, -0.0487140566f, -0.0876042694f, 0.255575448f,
    0.571086645f, 0.336931050f, -0.0307001974f, -0.0591641106f, 0.0211696979f, 0.00931402575f, -0.00601486955f, -0.000715362490f,
    // phase 6
    0.00238870201f, 0.00250129774f, -0.0117079616f, -0.000622309744f, 0.0441226140f, -0.0323834121f, -0.104089834f, 0.200647056f,
    0.560837686f, 0.386403173f, -0.00120452698f, -0.0676766708f, 0.0155473202f, 0.0122152092f, -0.00541751552f, -0.00156082585f,
    // phase 7
    0.00185603101f, 0.00347155891f, -0.0106152743f, -0.00602040719f, 0.0441431329f, -0.0154890148f, -0.114277698f, 0.146222934f,
    0.541861355f, 0.432532758f, 0.0334486477f, -0.0735822916f, 0.00863284804f, 0.0146952998f, -0.00450679474f, -0.00237310724f
}};

}  // namespace

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

bool PersistentPolyphaseResampler::prepare(const std::uint32_t channels,
                                        const double source_step) noexcept {
    if (channels == 0 || channels > history_.size() / kPolyphaseHistoryFramesV1 || !std::isfinite(source_step) ||
        source_step < 0.25 || source_step > 4.0) {
        return false;
    }
    channels_ = channels;
    source_step_ = source_step;
    reset();
    return true;
}

bool PersistentPolyphaseResampler::set_source_step(const double source_step) noexcept {
    if (!std::isfinite(source_step) || source_step < 0.25 || source_step > 4.0) {
        return false;
    }
    source_step_ = source_step;
    return true;
}

void PersistentPolyphaseResampler::reset() noexcept {
    history_.fill(0.0F);
    phase_ = 0.0;
    has_previous_ = false;
}

bool PersistentPolyphaseResampler::process(const float* const input,
                                        const std::size_t input_frames,
                                        float* const output,
                                        const std::size_t output_capacity_frames,
                                        std::size_t& output_frames) noexcept {
    output_frames = 0;
    if (channels_ == 0 || input == nullptr || output == nullptr || input_frames == 0 ||
        !std::isfinite(phase_) || phase_ < 0.0 ||
        !std::isfinite(source_step_) || source_step_ <= 0.0 || source_step_ > 4.0) {
        return false;
    }

    const auto expected = required_output_frames(input_frames);
    if (expected > output_capacity_frames) {
        return false;
    }

    constexpr std::size_t half = kPolyphaseTapsPerPhaseV1 / 2U;
    // Passthrough fast path: at step=1.0 with integer phase, the polyphase
    // kernel reduces to identity (no filtering needed).
    if (source_step_ == 1.0 && phase_ == std::floor(phase_)) {
        const auto start = static_cast<std::size_t>(phase_);
        for (std::size_t frame = 0; frame < expected; ++frame) {
            const auto src_idx = has_previous_
                                     ? kPolyphaseHistoryFramesV1 + start + frame - half + half
                                     : start + frame;
            const auto input_offset = has_previous_ ? src_idx - kPolyphaseHistoryFramesV1 : src_idx;
            if (input_offset >= input_frames) { break; }
            std::memcpy(output + frame * channels_,
                        input + input_offset * channels_,
                        channels_ * sizeof(float));
            ++output_frames;
        }
        phase_ += static_cast<double>(output_frames);
        append_history(input, input_frames);
        phase_ -= static_cast<double>(input_frames);
        if (phase_ < 0.0) { phase_ = 0.0; }
        has_previous_ = true;
        return true;
    }
    for (std::size_t frame = 0; frame < expected; ++frame) {
        const auto int_pos = static_cast<std::size_t>(phase_);
        const double frac = phase_ - static_cast<double>(int_pos);
        const auto phase_index =
            static_cast<std::size_t>(frac * static_cast<double>(kPolyphasePhasesV1));
        const auto clamped_phase = std::min(phase_index, kPolyphasePhasesV1 - 1U);
        const float* coefficients =
            &kPolyphaseCoefficientsV1.values[clamped_phase * kPolyphaseTapsPerPhaseV1];

        for (std::uint32_t channel = 0; channel < channels_; ++channel) {
            float accumulator = 0.0F;
            for (std::size_t tap = 0; tap < kPolyphaseTapsPerPhaseV1; ++tap) {
                const std::size_t center = int_pos + half;
                const std::size_t sample_pos = (center >= tap) ? (center - tap) : 0U;
                float sample = 0.0F;
                if (has_previous_) {
                    if (sample_pos < kPolyphaseHistoryFramesV1) {
                        sample = history_[sample_pos * channels_ + channel];
                    } else {
                        const auto input_idx = sample_pos - kPolyphaseHistoryFramesV1;
                        if (input_idx < input_frames) {
                            sample = input[input_idx * channels_ + channel];
                        }
                    }
                } else {
                    if (sample_pos < input_frames) {
                        sample = input[sample_pos * channels_ + channel];
                    }
                }
                accumulator += coefficients[tap] * sample;
            }
            output[frame * channels_ + channel] = accumulator;
        }
        phase_ += source_step_;
        ++output_frames;
    }

    append_history(input, input_frames);
    phase_ -= static_cast<double>(input_frames);
    if (phase_ < 0.0) {
        phase_ = 0.0;
    }
    has_previous_ = true;
    return true;
}

void PersistentPolyphaseResampler::append_history(
    const float* input, const std::size_t input_frames) noexcept {
    if (input_frames >= kPolyphaseHistoryFramesV1) {
        std::memcpy(history_.data(),
                    input + (input_frames - kPolyphaseHistoryFramesV1) * channels_,
                    kPolyphaseHistoryFramesV1 * channels_ * sizeof(float));
    } else {
        const auto shift = kPolyphaseHistoryFramesV1 - input_frames;
        std::memmove(history_.data(), history_.data() + input_frames * channels_,
                     shift * channels_ * sizeof(float));
        std::memcpy(history_.data() + shift * channels_, input,
                    input_frames * channels_ * sizeof(float));
    }
}

std::size_t PersistentPolyphaseResampler::required_output_frames(
    const std::size_t input_frames) const noexcept {
    if (channels_ == 0U || input_frames == 0U || !std::isfinite(phase_) || phase_ < 0.0 ||
        !std::isfinite(source_step_) || source_step_ <= 0.0 || source_step_ > 4.0) {
        return 0U;
    }
    const std::size_t virtual_end = has_previous_
                                        ? kPolyphaseHistoryFramesV1 + input_frames
                                        : input_frames;
    if (virtual_end < 2U) {
        return 0U;
    }
    const auto last_center = static_cast<double>(virtual_end) - 2.0;
    const auto available = last_center - phase_;
    return available < 0.0
               ? 0U
               : static_cast<std::size_t>(std::floor(available / source_step_)) + 1U;
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

std::size_t OutputSinkModel::required_output_frames(const std::size_t input_frames) const noexcept {
    return snapshot_.prepared ? resampler_.required_output_frames(input_frames) : 0U;
}

}  // namespace hibiki
