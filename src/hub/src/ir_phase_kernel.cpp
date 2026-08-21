// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/ir_phase_kernel.hpp"

#include "hibiki/ir_convolver.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <utility>

namespace hibiki {
namespace {

using Complex = std::complex<double>;
constexpr double kPi = std::numbers::pi_v<double>;
constexpr double kMagnitudeFloor = 1.0e-12;

IrPhaseKernelResultV1 failure(std::string diagnostic) noexcept {
    IrPhaseKernelResultV1 result{};
    result.diagnostic = std::move(diagnostic);
    return result;
}

std::size_t next_power_of_two(std::size_t value) noexcept {
    std::size_t result = 1U;
    while (result < value && result <= (std::numeric_limits<std::size_t>::max() / 2U)) {
        result *= 2U;
    }
    return result;
}

void fft_in_place(std::vector<Complex>& values, const bool inverse) noexcept {
    const auto count = values.size();
    for (std::size_t i = 1U, j = 0U; i < count; ++i) {
        std::size_t bit = count >> 1U;
        for (; (j & bit) != 0U; bit >>= 1U) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(values[i], values[j]);
    }
    for (std::size_t length = 2U; length <= count; length <<= 1U) {
        const auto sign = inverse ? 1.0 : -1.0;
        const Complex step = std::polar(1.0, sign * 2.0 * kPi / static_cast<double>(length));
        for (std::size_t start = 0U; start < count; start += length) {
            Complex twiddle{1.0, 0.0};
            const auto half = length >> 1U;
            for (std::size_t i = 0U; i < half; ++i) {
                const auto even = values[start + i];
                const auto odd = twiddle * values[start + i + half];
                values[start + i] = even + odd;
                values[start + i + half] = even - odd;
                twiddle *= step;
            }
        }
        if (length == count) break;
    }
    if (inverse) {
        const auto scale = 1.0 / static_cast<double>(count);
        for (auto& value : values) value *= scale;
    }
}

double wrap_phase(double value) noexcept {
    while (value > kPi) value -= 2.0 * kPi;
    while (value < -kPi) value += 2.0 * kPi;
    return value;
}

bool finite_vector(const std::vector<float>& values) noexcept {
    return std::all_of(values.begin(), values.end(),
                       [](const float value) { return std::isfinite(value); });
}

std::vector<Complex> minimum_phase_spectrum(const std::vector<Complex>& source) {
    std::vector<Complex> cepstrum(source.size());
    for (std::size_t index = 0U; index < source.size(); ++index) {
        const auto magnitude = std::max(std::abs(source[index]), kMagnitudeFloor);
        cepstrum[index] = Complex{std::log(magnitude), 0.0};
    }
    fft_in_place(cepstrum, true);
    const auto half = source.size() >> 1U;
    for (std::size_t index = 1U; index < source.size(); ++index) {
        if (index < half) {
            cepstrum[index] *= 2.0;
        } else if (index > half) {
            cepstrum[index] = Complex{0.0, 0.0};
        }
    }
    fft_in_place(cepstrum, false);
    for (auto& value : cepstrum) value = std::exp(value);
    return cepstrum;
}

std::vector<Complex> linear_phase_spectrum(const std::vector<Complex>& source,
                                           const std::size_t taps) {
    std::vector<Complex> target(source.size());
    // An integer delay keeps the Nyquist bin real for both odd and even tap
    // counts. The graph reports the policy budget separately.
    const auto delay = taps / 2U;
    for (std::size_t index = 0U; index < source.size(); ++index) {
        const auto magnitude = std::abs(source[index]);
        const auto angle = -2.0 * kPi * static_cast<double>(index * delay) /
                           static_cast<double>(source.size());
        target[index] = std::polar(magnitude, angle);
    }
    // Explicitly restore conjugate symmetry after floating-point polar math.
    target[0U] = Complex{target[0U].real(), 0.0};
    for (std::size_t index = 1U; index < source.size() / 2U; ++index) {
        target[source.size() - index] = std::conj(target[index]);
    }
    if ((source.size() & 1U) == 0U) {
        target[source.size() / 2U] = Complex{target[source.size() / 2U].real(), 0.0};
    }
    return target;
}

std::vector<Complex> phase_mix(const std::vector<Complex>& source,
                               const std::vector<Complex>& target,
                               const double strength) {
    std::vector<Complex> mixed(source.size());
    const auto half = source.size() / 2U;
    for (std::size_t index = 0U; index <= half; ++index) {
        const auto magnitude = std::abs(source[index]);
        const auto source_phase = std::atan2(source[index].imag(), source[index].real());
        const auto target_phase = std::atan2(target[index].imag(), target[index].real());
        const auto phase = source_phase +
                           strength * wrap_phase(target_phase - source_phase);
        mixed[index] = Complex{magnitude * std::cos(phase), magnitude * std::sin(phase)};
        if (index == 0U || (index == half && (source.size() & 1U) == 0U)) {
            mixed[index] = Complex{mixed[index].real(), 0.0};
        } else if (index < source.size()) {
            mixed[source.size() - index] = std::conj(mixed[index]);
        }
    }
    return mixed;
}

}  // namespace

IrPhaseKernelResultV1 build_ir_phase_kernel_v1(
    const std::span<const float> source_channel_major,
    const std::size_t taps,
    const std::uint32_t kernel_channels,
    const std::uint32_t sample_rate,
    const IrPhaseResolutionV1& resolution) noexcept {
    try {
        if (taps == 0U || taps > kMaxRealtimeIrTapsV1 || kernel_channels == 0U ||
            kernel_channels > 8U || sample_rate < 8000U || sample_rate > 192000U ||
            source_channel_major.size() != taps * static_cast<std::size_t>(kernel_channels) ||
            !resolution.valid || !std::isfinite(resolution.strength) || resolution.strength < 0.0 ||
            resolution.strength > 1.0 || !std::isfinite(resolution.added_delay_ms) ||
            resolution.added_delay_ms < 0.0) {
            return failure("IR phase kernel input or bound is invalid");
        }
        for (const auto value : source_channel_major) {
            if (!std::isfinite(value)) return failure("IR phase kernel contains a non-finite sample");
        }

        IrPhaseKernelResultV1 result{};
        result.sample_rate = sample_rate;
        result.kernel_channels = kernel_channels;
        result.taps = taps;
        result.resolution = resolution;
        result.channel_major.assign(source_channel_major.begin(), source_channel_major.end());
        if (resolution.mode == IrPhaseMode::Bypass || resolution.strength == 0.0) {
            result.valid = true;
            result.diagnostic = "source kernel retained";
            return result;
        }

        const auto fft_size = next_power_of_two(std::max<std::size_t>(8U, taps * 2U));
        if (fft_size < taps || fft_size == 0U) return failure("IR phase FFT bound is invalid");
        for (std::uint32_t channel = 0U; channel < kernel_channels; ++channel) {
            std::vector<Complex> source(fft_size);
            for (std::size_t tap = 0U; tap < taps; ++tap) {
                source[tap] = Complex{static_cast<double>(source_channel_major[
                                                static_cast<std::size_t>(channel) * taps + tap]),
                                      0.0};
            }
            fft_in_place(source, false);

            std::vector<Complex> target;
            switch (resolution.mode) {
                case IrPhaseMode::MinimumPhase:
                    target = minimum_phase_spectrum(source);
                    break;
                case IrPhaseMode::MixedPhase:
                case IrPhaseMode::LinearPhase:
                    target = linear_phase_spectrum(source, taps);
                    break;
                case IrPhaseMode::Bypass:
                    break;
            }
            const auto mixed = phase_mix(source, target, resolution.strength);
            auto transformed = mixed;
            fft_in_place(transformed, true);
            for (std::size_t tap = 0U; tap < taps; ++tap) {
                const auto value = static_cast<float>(transformed[tap].real());
                if (!std::isfinite(value)) return failure("IR phase transform became non-finite");
                result.channel_major[static_cast<std::size_t>(channel) * taps + tap] = value;
            }
        }
        if (!finite_vector(result.channel_major)) return failure("IR phase output is non-finite");
        result.valid = true;
        result.diagnostic = "ok";
        return result;
    } catch (const std::bad_alloc&) {
        return failure("IR phase allocation failed within the control-plane bound");
    } catch (...) {
        return failure("IR phase transform failed");
    }
}

}  // namespace hibiki
