// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/ir_phase_kernel.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <vector>

#define CHECK(expr)                                                             \
    do {                                                                        \
        if (!(expr)) {                                                          \
            std::fputs("FAILED: " #expr "\n", stderr);                         \
            return 1;                                                           \
        }                                                                       \
    } while (false)

namespace {

using hibiki::build_ir_phase_kernel_v1;
using hibiki::IrPhaseMode;
using hibiki::IrPhasePolicyV1;
using hibiki::IrPhaseResolutionV1;

constexpr std::size_t kTaps = 64U;
constexpr std::uint32_t kChannels = 2U;
constexpr std::uint32_t kRate = 48000U;
constexpr double kTolerance = 1e-6;

IrPhasePolicyV1 make_policy(const IrPhaseMode mode, const double strength) {
    IrPhasePolicyV1 policy{};
    policy.schema_version = 1;
    policy.mode = mode;
    policy.strength = strength;
    return policy;
}

std::vector<float> make_source_kernel() {
    // A deterministic asymmetric kernel: a decaying sine burst with a
    // non-trivial phase response.
    std::vector<float> samples(kTaps * kChannels);
    for (std::size_t tap = 0; tap < kTaps; ++tap) {
        const auto decay = std::exp(-0.08 * static_cast<double>(tap));
        const auto wave =
            std::sin(2.0 * std::numbers::pi_v<double> * 0.06 *
                     static_cast<double>(tap));
        const auto value = decay * wave;
        samples[tap] = static_cast<float>(value);
        samples[kTaps + tap] = static_cast<float>(value * 0.5);
    }
    return samples;
}

bool all_finite(const std::vector<float>& values) {
    for (const auto v : values) {
        if (!std::isfinite(v)) return false;
    }
    return true;
}

}  // namespace

int main() {
    const auto qnan = std::numeric_limits<double>::quiet_NaN();
    const auto pinf = std::numeric_limits<double>::infinity();

    // ---- policy validation edges --------------------------------------------
    {
        CHECK(hibiki::validate_ir_phase_policy(make_policy(IrPhaseMode::MinimumPhase, 0.0)));
        CHECK(hibiki::validate_ir_phase_policy(make_policy(IrPhaseMode::MinimumPhase, 0.25)));
        CHECK(hibiki::validate_ir_phase_policy(make_policy(IrPhaseMode::MixedPhase, 1.0)));
        CHECK(hibiki::validate_ir_phase_policy(make_policy(IrPhaseMode::LinearPhase, 0.75)));
        CHECK(hibiki::validate_ir_phase_policy(make_policy(IrPhaseMode::Bypass, 0.0)));
        CHECK(!hibiki::validate_ir_phase_policy(
            make_policy(IrPhaseMode::Bypass, 0.001)));
        CHECK(!hibiki::validate_ir_phase_policy(
            make_policy(IrPhaseMode::MinimumPhase, -0.01)));
        CHECK(!hibiki::validate_ir_phase_policy(
            make_policy(IrPhaseMode::MinimumPhase, 1.01)));
        CHECK(!hibiki::validate_ir_phase_policy(
            make_policy(IrPhaseMode::MinimumPhase, qnan)));
        CHECK(!hibiki::validate_ir_phase_policy(
            make_policy(IrPhaseMode::MinimumPhase, pinf)));
        CHECK(!hibiki::validate_ir_phase_policy(
            make_policy(IrPhaseMode::MinimumPhase, -pinf)));
        auto bad_schema = make_policy(IrPhaseMode::MinimumPhase, 0.5);
        bad_schema.schema_version = 2;
        CHECK(!hibiki::validate_ir_phase_policy(bad_schema));
    }

    // ---- policy resolution ---------------------------------------------------
    {
        const auto min0 =
            hibiki::resolve_ir_phase_policy(make_policy(IrPhaseMode::MinimumPhase, 0.0));
        CHECK(min0.valid);
        CHECK(min0.added_delay_ms == 0.0);
        CHECK(!min0.uses_fir);
        const auto mixed_half =
            hibiki::resolve_ir_phase_policy(make_policy(IrPhaseMode::MixedPhase, 0.5));
        CHECK(mixed_half.valid);
        CHECK(std::abs(mixed_half.added_delay_ms - 40.0) < kTolerance);
        CHECK(mixed_half.uses_fir);
        const auto linear_full =
            hibiki::resolve_ir_phase_policy(make_policy(IrPhaseMode::LinearPhase, 1.0));
        CHECK(linear_full.valid);
        CHECK(std::abs(linear_full.added_delay_ms - 160.0) < kTolerance);
        CHECK(linear_full.uses_fir);
        const auto bypass =
            hibiki::resolve_ir_phase_policy(make_policy(IrPhaseMode::Bypass, 0.0));
        CHECK(bypass.valid);
        CHECK(bypass.added_delay_ms == 0.0);
        CHECK(!bypass.uses_fir);
    }

    // ---- kernel builder input rejection --------------------------------------
    {
        const auto source = make_source_kernel();
        IrPhaseResolutionV1 resolution{};
        resolution.schema_version = 1;
        resolution.mode = IrPhaseMode::MinimumPhase;
        resolution.strength = 1.0;
        resolution.added_delay_ms = 0.0;
        resolution.uses_fir = false;
        resolution.valid = true;

        CHECK(!build_ir_phase_kernel_v1(source, 0U, kChannels, kRate, resolution).valid);
        CHECK(!build_ir_phase_kernel_v1(source, 4097U, kChannels, kRate, resolution).valid);
        CHECK(!build_ir_phase_kernel_v1(source, kTaps, 0U, kRate, resolution).valid);
        CHECK(!build_ir_phase_kernel_v1(source, kTaps, 9U, kRate, resolution).valid);
        CHECK(!build_ir_phase_kernel_v1(source, kTaps, kChannels, 7999U, resolution).valid);
        CHECK(!build_ir_phase_kernel_v1(source, kTaps, kChannels, 192001U, resolution).valid);

        auto short_source = source;
        short_source.pop_back();
        CHECK(!build_ir_phase_kernel_v1(short_source, kTaps, kChannels, kRate,
                                        resolution).valid);
        auto bad_resolution = resolution;
        bad_resolution.valid = false;
        CHECK(!build_ir_phase_kernel_v1(source, kTaps, kChannels, kRate,
                                        bad_resolution).valid);
        bad_resolution = resolution;
        bad_resolution.strength = qnan;
        CHECK(!build_ir_phase_kernel_v1(source, kTaps, kChannels, kRate,
                                        bad_resolution).valid);
        bad_resolution.strength = pinf;
        CHECK(!build_ir_phase_kernel_v1(source, kTaps, kChannels, kRate,
                                        bad_resolution).valid);

        auto nonfinite = make_source_kernel();
        nonfinite[3] = std::numeric_limits<float>::quiet_NaN();
        CHECK(!build_ir_phase_kernel_v1(nonfinite, kTaps, kChannels, kRate,
                                        resolution).valid);
        nonfinite[3] = std::numeric_limits<float>::infinity();
        CHECK(!build_ir_phase_kernel_v1(nonfinite, kTaps, kChannels, kRate,
                                        resolution).valid);
    }

    // ---- bypass and zero-strength short-circuit ------------------------------
    {
        const auto source = make_source_kernel();

        IrPhaseResolutionV1 bypass_resolution{};
        bypass_resolution.schema_version = 1;
        bypass_resolution.mode = IrPhaseMode::Bypass;
        bypass_resolution.strength = 0.0;
        bypass_resolution.added_delay_ms = 0.0;
        bypass_resolution.uses_fir = false;
        bypass_resolution.valid = true;
        const auto bypass_result =
            build_ir_phase_kernel_v1(source, kTaps, kChannels, kRate, bypass_resolution);
        CHECK(bypass_result.valid);
        CHECK(bypass_result.channel_major.size() == source.size());
        for (std::size_t i = 0; i < source.size(); ++i) {
            CHECK(bypass_result.channel_major[i] == source[i]);
        }

        for (const auto mode : {IrPhaseMode::MinimumPhase,
                                IrPhaseMode::MixedPhase,
                                IrPhaseMode::LinearPhase}) {
            IrPhaseResolutionV1 zero{};
            zero.schema_version = 1;
            zero.mode = mode;
            zero.strength = 0.0;
            zero.added_delay_ms = 0.0;
            zero.uses_fir = false;
            zero.valid = true;
            const auto result = build_ir_phase_kernel_v1(
                source, kTaps, kChannels, kRate, zero);
            CHECK(result.valid);
            for (std::size_t i = 0; i < source.size(); ++i) {
                CHECK(result.channel_major[i] == source[i]);
            }
        }
    }

    // ---- minimum phase output properties -------------------------------------
    {
        const auto source = make_source_kernel();
        IrPhaseResolutionV1 resolution{};
        resolution.schema_version = 1;
        resolution.mode = IrPhaseMode::MinimumPhase;
        resolution.strength = 1.0;
        resolution.added_delay_ms = 0.0;
        resolution.uses_fir = false;
        resolution.valid = true;

        const auto result =
            build_ir_phase_kernel_v1(source, kTaps, kChannels, kRate, resolution);
        CHECK(result.valid);
        CHECK(all_finite(result.channel_major));

        for (std::uint32_t ch = 0U; ch < kChannels; ++ch) {
            double early_energy = 0.0;
            double late_energy = 0.0;
            for (std::size_t tap = 0; tap < kTaps; ++tap) {
                const auto value =
                    static_cast<double>(result.channel_major[ch * kTaps + tap]);
                if (tap < kTaps / 2U) {
                    early_energy += value * value;
                } else {
                    late_energy += value * value;
                }
            }
            CHECK(early_energy > late_energy);
        }
    }

    // ---- linear phase output symmetry ----------------------------------------
    {
        const auto source = make_source_kernel();
        IrPhaseResolutionV1 resolution{};
        resolution.schema_version = 1;
        resolution.mode = IrPhaseMode::LinearPhase;
        resolution.strength = 1.0;
        resolution.added_delay_ms = 0.0;
        resolution.uses_fir = true;
        resolution.valid = true;

        const auto result =
            build_ir_phase_kernel_v1(source, kTaps, kChannels, kRate, resolution);
        CHECK(result.valid);
        CHECK(all_finite(result.channel_major));

        // Symmetry axis is the group-delay tap (taps / 2); window
        // truncation keeps that axis intact inside the retained region.
        constexpr std::size_t kAxis = kTaps / 2U;
        for (std::uint32_t ch = 0U; ch < kChannels; ++ch) {
            for (std::size_t d = 1U; d < kAxis; ++d) {
                const auto front = static_cast<double>(
                    result.channel_major[ch * kTaps + (kAxis - d)]);
                const auto mirror = static_cast<double>(
                    result.channel_major[ch * kTaps + (kAxis + d)]);
                CHECK(std::abs(front - mirror) < 1e-4);
            }
        }
    }

    // ---- multi-channel independence ------------------------------------------
    {
        const auto source = make_source_kernel();
        std::vector<float> solo(
            source.begin(), source.begin() + static_cast<std::ptrdiff_t>(kTaps));

        IrPhaseResolutionV1 resolution{};
        resolution.schema_version = 1;
        resolution.mode = IrPhaseMode::MinimumPhase;
        resolution.strength = 1.0;
        resolution.added_delay_ms = 0.0;
        resolution.uses_fir = false;
        resolution.valid = true;

        const auto solo_result =
            build_ir_phase_kernel_v1(solo, kTaps, 1U, kRate, resolution);
        CHECK(solo_result.valid);

        const auto stereo_result =
            build_ir_phase_kernel_v1(source, kTaps, kChannels, kRate, resolution);
        CHECK(stereo_result.valid);
        for (std::size_t i = 0; i < kTaps; ++i) {
            CHECK(solo_result.channel_major[i] == stereo_result.channel_major[i]);
        }
    }

    // ---- result metadata ------------------------------------------------------
    {
        const auto source = make_source_kernel();
        IrPhaseResolutionV1 resolution{};
        resolution.schema_version = 1;
        resolution.mode = IrPhaseMode::MixedPhase;
        resolution.strength = 0.5;
        resolution.added_delay_ms = 40.0;
        resolution.uses_fir = true;
        resolution.valid = true;

        const auto result =
            build_ir_phase_kernel_v1(source, kTaps, kChannels, kRate, resolution);
        CHECK(result.valid);
        CHECK(result.schema_version == 1U);
        CHECK(result.sample_rate == kRate);
        CHECK(result.kernel_channels == kChannels);
        CHECK(result.taps == kTaps);
        CHECK(result.resolution.schema_version == 1U);
        CHECK(result.resolution.mode == IrPhaseMode::MixedPhase);
        CHECK(std::abs(result.resolution.added_delay_ms - 40.0) < kTolerance);
    }

    // ---- all legal modes produce finite output -------------------------------
    {
        const auto source = make_source_kernel();
        for (const auto mode : {IrPhaseMode::MinimumPhase,
                                IrPhaseMode::MixedPhase,
                                IrPhaseMode::LinearPhase}) {
            for (const auto strength : {0.25, 0.5, 0.75, 1.0}) {
                IrPhaseResolutionV1 resolution{};
                resolution.schema_version = 1;
                resolution.mode = mode;
                resolution.strength = strength;
                resolution.added_delay_ms = strength * 80.0;
                resolution.uses_fir = true;
                resolution.valid = true;
                const auto result =
                    build_ir_phase_kernel_v1(source, kTaps, kChannels, kRate, resolution);
                CHECK(result.valid);
                CHECK(all_finite(result.channel_major));
            }
        }
    }

    return 0;
}
