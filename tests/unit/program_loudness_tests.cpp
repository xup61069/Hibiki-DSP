// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/program_loudness.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

constexpr double kPi = 3.14159265358979323846;

hibiki::ProgramAwareLevelPolicyV1 base_policy() noexcept {
    hibiki::ProgramAwareLevelPolicyV1 policy;
    policy.enabled = true;
    return policy;
}

double sine_sample(const std::size_t index, const double frequency_hz,
                   const std::uint32_t sample_rate) noexcept {
    return std::sin(2.0 * kPi * frequency_hz *
                    static_cast<double>(index) / static_cast<double>(sample_rate));
}

// Refills one interleaved block with a continuous sine so every processed
// block presents identical program material to the component under test.
void fill_sine(std::vector<float>& block, const std::size_t frames,
               const std::uint32_t channels, std::size_t start_index,
               const double frequency_hz, const std::uint32_t sample_rate,
               const float amplitude) noexcept {
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        const float scaled =
            amplitude * static_cast<float>(sine_sample(
                            start_index + frame, frequency_hz, sample_rate));
        for (std::uint32_t channel = 0U; channel < channels; ++channel) {
            block[frame * channels + channel] = scaled;
        }
    }
}

}  // namespace

int main() {
    using hibiki::BassExcessDetectorV1;
    using hibiki::ProgramAwareLevelBankV1;
    using hibiki::ProgramAwareLevelControllerV1;
    using hibiki::ProgramAwareLevelPolicyV1;
    using hibiki::ProgramAwareMeterModeV1;

    // ---- policy validation -------------------------------------------------
    {
        auto policy = base_policy();
        CHECK(hibiki::validate_program_aware_policy(policy));

        policy.schema_version = 0U;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.schema_version = 2U;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.schema_version = 1U;
        CHECK(hibiki::validate_program_aware_policy(policy));

        policy.meter_mode = ProgramAwareMeterModeV1::KWeightedProxy;
        CHECK(hibiki::validate_program_aware_policy(policy));
        policy.meter_mode = static_cast<ProgramAwareMeterModeV1>(2U);
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.meter_mode = ProgramAwareMeterModeV1::RmsProxy;

        policy.excluded_channel = -2;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.excluded_channel = 8;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.excluded_channel = 7;
        CHECK(hibiki::validate_program_aware_policy(policy));
        policy.excluded_channel = -1;
        CHECK(hibiki::validate_program_aware_policy(policy));

        policy.target_dbfs = -60.001;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.target_dbfs = 0.001;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.target_dbfs = -60.0;
        CHECK(hibiki::validate_program_aware_policy(policy));
        policy.target_dbfs = 0.0;
        CHECK(hibiki::validate_program_aware_policy(policy));
        policy.target_dbfs = -23.0;

        policy.max_boost_db = -0.001;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.max_boost_db = 12.001;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.max_boost_db = 0.0;
        CHECK(hibiki::validate_program_aware_policy(policy));
        policy.max_boost_db = 12.0;
        CHECK(hibiki::validate_program_aware_policy(policy));
        policy.max_boost_db = 6.0;

        policy.max_cut_db = -0.001;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.max_cut_db = 24.001;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.max_cut_db = 0.0;
        CHECK(hibiki::validate_program_aware_policy(policy));
        policy.max_cut_db = 24.0;
        CHECK(hibiki::validate_program_aware_policy(policy));
        policy.max_cut_db = 12.0;

        policy.analysis_window_ms = 99.999;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.analysis_window_ms = 10000.001;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.analysis_window_ms = 100.0;
        CHECK(hibiki::validate_program_aware_policy(policy));
        policy.analysis_window_ms = 10000.0;
        CHECK(hibiki::validate_program_aware_policy(policy));
        policy.analysis_window_ms = 3000.0;

        policy.max_rate_db_per_second = 0.0;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.max_rate_db_per_second = 60.001;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.max_rate_db_per_second = 60.0;
        CHECK(hibiki::validate_program_aware_policy(policy));
        policy.max_rate_db_per_second = 6.0;

        policy.silence_gate_dbfs = -144.001;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.silence_gate_dbfs = 0.0;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.silence_gate_dbfs = -144.0;
        CHECK(hibiki::validate_program_aware_policy(policy));
        policy.silence_gate_dbfs = -0.001;
        CHECK(hibiki::validate_program_aware_policy(policy));
        policy.silence_gate_dbfs = -70.0;

        policy.bass_max_cut_db = -0.001;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.bass_max_cut_db = 12.001;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.bass_max_cut_db = 0.0;
        CHECK(hibiki::validate_program_aware_policy(policy));
        policy.bass_max_cut_db = 12.0;
        CHECK(hibiki::validate_program_aware_policy(policy));
        policy.bass_max_cut_db = 6.0;

        policy.night_compression_max_reduction_db = -0.001;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.night_compression_max_reduction_db = 24.001;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.night_compression_max_reduction_db = 0.0;
        CHECK(hibiki::validate_program_aware_policy(policy));
        policy.night_compression_max_reduction_db = 24.0;
        CHECK(hibiki::validate_program_aware_policy(policy));
        policy.night_compression_max_reduction_db = 9.0;

        policy.night_compression_knee_db = 5.999;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.night_compression_knee_db = 30.001;
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.night_compression_knee_db = 6.0;
        CHECK(hibiki::validate_program_aware_policy(policy));
        policy.night_compression_knee_db = 30.0;
        CHECK(hibiki::validate_program_aware_policy(policy));

        policy.target_dbfs =
            std::numeric_limits<double>::quiet_NaN();
        CHECK(!hibiki::validate_program_aware_policy(policy));
        policy.target_dbfs = -23.0;
        policy.max_rate_db_per_second =
            std::numeric_limits<double>::infinity();
        CHECK(!hibiki::validate_program_aware_policy(policy));
    }

    // ---- controller configure / process fail-closed -------------------------
    {
        ProgramAwareLevelControllerV1 controller;
        std::vector<float> block(128U, 0.1F);

        CHECK(!controller.process_interleaved(nullptr, 0U, 0U));
        CHECK(!controller.process_interleaved(block.data(), 64U, 2U));
        CHECK(controller.sample_rate() == 0U);

        auto policy = base_policy();
        CHECK(!controller.configure(policy, 7999U));
        CHECK(!controller.configure(policy, 192001U));
        CHECK(controller.sample_rate() == 0U);

        policy.schema_version = 9U;
        CHECK(!controller.configure(policy, 48000U));
        CHECK(controller.sample_rate() == 0U);
        CHECK(!controller.process_interleaved(block.data(), 64U, 2U));

        policy = base_policy();
        CHECK(controller.configure(policy, 48000U));
        CHECK(controller.sample_rate() == 48000U);

        CHECK(!controller.process_interleaved(nullptr, 64U, 2U));
        CHECK(!controller.process_interleaved(block.data(), 0U, 2U));
        CHECK(!controller.process_interleaved(block.data(), 64U, 0U));
        CHECK(!controller.process_interleaved(block.data(), 64U, 9U));

        constexpr auto max_frames_for_eight_channels =
            std::numeric_limits<std::size_t>::max() / 8U;
        CHECK(max_frames_for_eight_channels * 8U <=
              std::numeric_limits<std::size_t>::max());
        CHECK(max_frames_for_eight_channels + 1U >
              std::numeric_limits<std::size_t>::max() / 8U);
        std::vector<float> eight_channel_boundary(8U, 0.0F);
        CHECK(controller.process_interleaved(eight_channel_boundary.data(), 1U,
                                             8U));
        const auto telemetry_before = controller.read_telemetry();
        const auto status_before = controller.status();
        std::vector<float> overflow_guard{0.125F, -0.25F, 0.5F, -0.75F,
                                          0.875F, -1.0F, 0.25F, -0.5F};
        CHECK(!controller.process_interleaved(
            overflow_guard.data(), max_frames_for_eight_channels + 1U, 8U));
        CHECK(controller.read_telemetry().sequence == telemetry_before.sequence);
        CHECK(controller.status().valid == status_before.valid &&
              controller.status().measured_dbfs == status_before.measured_dbfs &&
              controller.status().applied_gain_db == status_before.applied_gain_db);
        CHECK(overflow_guard[0] == 0.125F && overflow_guard[7] == -0.5F);
    }

    // ---- disabled policy is a bit-exact passthrough -------------------------
    {
        ProgramAwareLevelControllerV1 controller;
        auto policy = base_policy();
        policy.enabled = false;
        CHECK(controller.configure(policy, 48000U));

        constexpr std::size_t kFrames = 480U;
        constexpr std::uint32_t kChannels = 2U;
        std::vector<float> work(kFrames * kChannels, 0.0F);
        fill_sine(work, kFrames, kChannels, 0U, 1000.0, 48000U, 0.5F);
        work[5U] = std::numeric_limits<float>::quiet_NaN();
        const std::vector<float> expected = work;

        CHECK(controller.process_interleaved(work.data(), kFrames, kChannels));

        for (std::size_t index = 0U; index < work.size(); ++index) {
            if (index == 5U) {
                CHECK(work[index] == 0.0F);
            } else {
                CHECK(work[index] == expected[index]);
            }
        }

        const auto& status = controller.status();
        CHECK(status.valid);
        CHECK(!status.enabled);
        CHECK(!status.silence_gated);
        CHECK(status.desired_gain_db == 0.0);
        CHECK(status.applied_gain_db == 0.0);
        CHECK(status.measured_dbfs > policy.silence_gate_dbfs);
        CHECK(status.measured_dbfs < 0.0);
    }

    // ---- gain converges toward target under slew limiting -------------------
    {
        constexpr std::uint32_t kSampleRate = 48000U;
        constexpr std::size_t kFrames = 4800U;  // 100 ms blocks
        constexpr double kMaxStepDb = 0.6;      // 6 dB/s across 100 ms

        auto policy = base_policy();
        policy.target_dbfs = -23.0;
        policy.analysis_window_ms = 100.0;
        policy.max_rate_db_per_second = 6.0;

        ProgramAwareLevelControllerV1 controller;
        CHECK(controller.configure(policy, kSampleRate));

        constexpr float kAmplitude = 0.25F;
        const double source_db =
            10.0 * std::log10(static_cast<double>(kAmplitude) *
                              static_cast<double>(kAmplitude) / 2.0);
        const double desired_offset =
            std::clamp(policy.target_dbfs - source_db,
                       -policy.max_cut_db, policy.max_boost_db);

        std::vector<float> work(kFrames, 0.0F);
        double previous_applied = 0.0;
        std::size_t phase = 0U;
        for (int block_index = 0; block_index < 40; ++block_index) {
            fill_sine(work, kFrames, 1U, phase, 1000.0, kSampleRate, kAmplitude);
            phase += kFrames;
            CHECK(controller.process_interleaved(work.data(), kFrames, 1U));

            const auto& status = controller.status();
            CHECK(status.valid);
            CHECK(std::abs(status.measured_dbfs - source_db) < 0.01);
            CHECK(status.applied_gain_db >= -policy.max_cut_db - 1.0e-6);
            CHECK(status.applied_gain_db <= policy.max_boost_db + 1.0e-6);
            CHECK(std::abs(status.applied_gain_db - previous_applied) <=
                  kMaxStepDb + 1.0e-6);
            if (block_index == 0) {
                CHECK(std::abs(status.applied_gain_db + kMaxStepDb) < 1.0e-9);
            }
            previous_applied = status.applied_gain_db;
        }

        const auto& status = controller.status();
        CHECK(std::abs(status.applied_gain_db - desired_offset) < 0.02);
        CHECK(std::abs(status.desired_gain_db - desired_offset) < 1.0e-6);
    }

    // ---- boost is clamped by max_boost_db ------------------------------------
    {
        constexpr std::uint32_t kSampleRate = 48000U;
        constexpr std::size_t kFrames = 4800U;

        auto policy = base_policy();
        policy.target_dbfs = -23.0;
        policy.analysis_window_ms = 100.0;
        policy.max_rate_db_per_second = 6.0;

        ProgramAwareLevelControllerV1 controller;
        CHECK(controller.configure(policy, kSampleRate));

        constexpr float kQuietAmplitude = 0.01F;
        const double source_db =
            10.0 * std::log10(static_cast<double>(kQuietAmplitude) *
                              static_cast<double>(kQuietAmplitude) / 2.0);

        std::vector<float> work(kFrames, 0.0F);
        double previous_applied = 0.0;
        std::size_t phase = 0U;
        for (int block_index = 0; block_index < 30; ++block_index) {
            fill_sine(work, kFrames, 1U, phase, 1000.0, kSampleRate,
                      kQuietAmplitude);
            phase += kFrames;
            CHECK(controller.process_interleaved(work.data(), kFrames, 1U));

            const auto& status = controller.status();
            CHECK(status.applied_gain_db <= policy.max_boost_db + 1.0e-6);
            CHECK(std::abs(status.applied_gain_db - previous_applied) <=
                  0.6 + 1.0e-6);
            previous_applied = status.applied_gain_db;
        }

        const auto& status = controller.status();
        CHECK(std::abs(status.desired_gain_db - policy.max_boost_db) < 1.0e-6);
        CHECK(std::abs(status.applied_gain_db - policy.max_boost_db) < 1.0e-4);
        CHECK(std::abs(status.measured_dbfs - source_db) < 0.01);
    }

    // ---- cut is clamped by max_cut_db ----------------------------------------
    {
        constexpr std::uint32_t kSampleRate = 48000U;
        constexpr std::size_t kFrames = 4800U;

        auto policy = base_policy();
        policy.target_dbfs = -23.0;
        policy.analysis_window_ms = 100.0;
        policy.max_rate_db_per_second = 6.0;

        ProgramAwareLevelControllerV1 controller;
        CHECK(controller.configure(policy, kSampleRate));

        std::vector<float> work(kFrames, 0.0F);
        double previous_applied = 0.0;
        std::size_t phase = 0U;
        for (int block_index = 0; block_index < 30; ++block_index) {
            fill_sine(work, kFrames, 1U, phase, 1000.0, kSampleRate, 1.0F);
            phase += kFrames;
            CHECK(controller.process_interleaved(work.data(), kFrames, 1U));

            const auto& status = controller.status();
            CHECK(status.applied_gain_db >= -policy.max_cut_db - 1.0e-6);
            CHECK(std::abs(status.applied_gain_db - previous_applied) <=
                  0.6 + 1.0e-6);
            previous_applied = status.applied_gain_db;
        }

        const auto& status = controller.status();
        CHECK(std::abs(status.desired_gain_db + policy.max_cut_db) < 1.0e-6);
        CHECK(std::abs(status.applied_gain_db + policy.max_cut_db) < 1.0e-4);
    }

    // ---- silence gate holds gain at unity ------------------------------------
    {
        ProgramAwareLevelControllerV1 controller;
        auto policy = base_policy();
        policy.enabled = true;
        policy.analysis_window_ms = 100.0;
        CHECK(controller.configure(policy, 48000U));

        std::vector<float> silence(4800U, 0.0F);
        CHECK(controller.process_interleaved(silence.data(), 4800U, 1U));

        const auto& status = controller.status();
        CHECK(status.valid);
        CHECK(status.silence_gated);
        CHECK(status.measured_dbfs == -144.0);
        CHECK(status.desired_gain_db == 0.0);
        CHECK(status.applied_gain_db == 0.0);
    }

    // ---- K-weighted proxy responds sensibly to a 1 kHz sine ------------------
    {
        auto rms_policy = base_policy();
        rms_policy.enabled = false;
        auto k_policy = rms_policy;
        k_policy.meter_mode = ProgramAwareMeterModeV1::KWeightedProxy;

        ProgramAwareLevelControllerV1 rms_controller;
        ProgramAwareLevelControllerV1 k_controller;
        CHECK(rms_controller.configure(rms_policy, 48000U));
        CHECK(k_controller.configure(k_policy, 48000U));

        constexpr std::size_t kFrames = 4800U;
        std::vector<float> work(kFrames, 0.0F);
        std::size_t phase = 0U;
        for (int block_index = 0; block_index < 20; ++block_index) {
            fill_sine(work, kFrames, 1U, phase, 1000.0, 48000U, 0.25F);
            phase += kFrames;
            CHECK(rms_controller.process_interleaved(work.data(), kFrames, 1U));
            fill_sine(work, kFrames, 1U, phase, 1000.0, 48000U, 0.25F);
            phase += kFrames;
            CHECK(k_controller.process_interleaved(work.data(), kFrames, 1U));
        }

        const auto& rms_status = rms_controller.status();
        const auto& k_status = k_controller.status();
        CHECK(rms_status.valid);
        CHECK(k_status.valid);
        CHECK(k_status.meter_mode == ProgramAwareMeterModeV1::KWeightedProxy);
        CHECK(rms_status.measured_dbfs > -144.0);
        CHECK(rms_status.measured_dbfs < 24.0);
        CHECK(k_status.measured_dbfs > rms_status.measured_dbfs + 0.2);
        CHECK(k_status.measured_dbfs < rms_status.measured_dbfs + 8.0);
    }

    // ---- excluded channel is removed from the measurement ---------------------
    {
        auto all_policy = base_policy();
        all_policy.enabled = false;
        auto exclude_policy = all_policy;
        exclude_policy.excluded_channel = 1;

        ProgramAwareLevelControllerV1 all_controller;
        ProgramAwareLevelControllerV1 exclude_controller;
        CHECK(all_controller.configure(all_policy, 48000U));
        CHECK(exclude_controller.configure(exclude_policy, 48000U));

        constexpr std::size_t kFrames = 4800U;
        constexpr std::uint32_t kChannels = 2U;
        std::vector<float> work(kFrames * kChannels, 0.0F);
        std::size_t phase = 0U;
        for (int block_index = 0; block_index < 10; ++block_index) {
            for (std::size_t frame = 0U; frame < kFrames; ++frame) {
                work[frame * kChannels] = static_cast<float>(
                    0.002 * sine_sample(phase + frame, 1000.0, 48000U));
                work[frame * kChannels + 1U] = static_cast<float>(
                    0.5 * sine_sample(phase + frame, 1000.0, 48000U));
            }
            phase += kFrames;
            CHECK(all_controller.process_interleaved(work.data(), kFrames,
                                                     kChannels));
            CHECK(exclude_controller.process_interleaved(work.data(), kFrames,
                                                         kChannels));
        }

        const double all_measured = all_controller.status().measured_dbfs;
        const double excluded_measured =
            exclude_controller.status().measured_dbfs;
        CHECK(all_measured > -20.0);
        CHECK(excluded_measured < -50.0);
        CHECK(excluded_measured > -62.0);
        CHECK(all_measured - excluded_measured > 35.0);
        CHECK(!exclude_controller.status().silence_gated);
    }

    // ---- bass excess detector -------------------------------------------------
    {
        BassExcessDetectorV1 detector;
        std::vector<float> small(480U * 2U, 0.0F);

        CHECK(!detector.process(small.data(), 480U, 2U));  // unconfigured
        CHECK(!detector.configure(7999U));
        CHECK(!detector.configure(192001U));
        CHECK(!detector.process(small.data(), 480U, 2U));
        CHECK(detector.configure(48000U));

        CHECK(!detector.process(nullptr, 480U, 2U));
        CHECK(!detector.process(small.data(), 0U, 2U));
        CHECK(!detector.process(small.data(), 480U, 0U));
        CHECK(!detector.process(small.data(), 480U, 9U));
        CHECK(detector.smoothed_excess_db() == -144.0);

        constexpr auto max_frames_for_eight_channels =
            std::numeric_limits<std::size_t>::max() / 8U;
        CHECK(max_frames_for_eight_channels * 8U <=
              std::numeric_limits<std::size_t>::max());
        CHECK(max_frames_for_eight_channels + 1U >
              std::numeric_limits<std::size_t>::max() / 8U);
        std::vector<float> eight_channel_boundary(8U, 0.25F);
        CHECK(detector.process(eight_channel_boundary.data(), 1U, 8U));
        const auto excess_before_overflow = detector.smoothed_excess_db();
        std::vector<float> overflow_guard{0.125F, -0.25F, 0.5F, -0.75F,
                                          0.875F, -1.0F, 0.25F, -0.5F};
        CHECK(!detector.process(overflow_guard.data(),
                                max_frames_for_eight_channels + 1U, 8U));
        CHECK(detector.smoothed_excess_db() == excess_before_overflow);
        CHECK(overflow_guard[0] == 0.125F && overflow_guard[7] == -0.5F);

        constexpr std::size_t kFrames = 4800U;
        std::vector<float> work(kFrames, 0.0F);
        std::size_t phase = 0U;
        for (int block_index = 0; block_index < 20; ++block_index) {
            fill_sine(work, kFrames, 1U, phase, 60.0, 48000U, 0.5F);
            phase += kFrames;
            CHECK(detector.process(work.data(), kFrames, 1U));
        }
        const double bass_excess = detector.smoothed_excess_db();
        CHECK(bass_excess <= 0.0);
        CHECK(bass_excess > -3.0);

        detector.reset();
        CHECK(detector.smoothed_excess_db() == -144.0);

        phase = 0U;
        for (int block_index = 0; block_index < 20; ++block_index) {
            fill_sine(work, kFrames, 1U, phase, 8000.0, 48000U, 0.5F);
            phase += kFrames;
            CHECK(detector.process(work.data(), kFrames, 1U));
        }
        const double treble_excess = detector.smoothed_excess_db();
        CHECK(treble_excess < -25.0);
        CHECK(treble_excess < bass_excess - 20.0);
    }

    // ---- low-frequency correction reaches the bounded RT projection ---------
    {
        constexpr std::uint32_t kSampleRate = 48000U;
        constexpr std::size_t kFrames = 4800U;  // 100 ms blocks
        constexpr double kMaxStepDb = 0.6;      // 6 dB/s across 100 ms

        auto policy = base_policy();
        policy.target_dbfs = -9.0;
        policy.analysis_window_ms = 100.0;
        policy.max_rate_db_per_second = 6.0;
        policy.bass_correction_enabled = true;
        policy.bass_max_cut_db = 4.0;

        ProgramAwareLevelControllerV1 controller;
        CHECK(controller.configure(policy, kSampleRate));

        std::vector<float> source(kFrames, 0.0F);
        std::vector<float> work(kFrames, 0.0F);
        std::size_t phase = 0U;
        double previous_cut = 0.0;
        bool correction_engaged = false;
        bool output_changed = false;
        for (int block_index = 0; block_index < 24; ++block_index) {
            fill_sine(source, kFrames, 1U, phase, 60.0, kSampleRate, 0.5F);
            phase += kFrames;
            work = source;
            CHECK(controller.process_interleaved(work.data(), kFrames, 1U));

            const auto& status = controller.status();
            CHECK(status.valid);
            CHECK(status.bass_correction_gain_db <= 0.0);
            CHECK(status.bass_correction_gain_db >=
                  -policy.bass_max_cut_db - 1.0e-6);
            CHECK(std::abs(status.bass_correction_gain_db - previous_cut) <=
                  kMaxStepDb + 1.0e-6);
            if (status.bass_correction_gain_db < -0.25) {
                correction_engaged = true;
            }
            for (std::size_t index = 0; index < work.size(); ++index) {
                if (std::abs(static_cast<double>(work[index]) -
                             static_cast<double>(source[index])) > 1.0e-5) {
                    output_changed = true;
                    break;
                }
            }
            previous_cut = status.bass_correction_gain_db;
        }

        CHECK(correction_engaged);
        CHECK(output_changed);
        const auto& final_status = controller.status();
        const auto telemetry = controller.read_telemetry();
        CHECK(telemetry.valid);
        CHECK(telemetry.enabled);
        CHECK(telemetry.bass_correction_gain_db < -0.25);
        CHECK(std::abs(telemetry.bass_correction_gain_db -
                       final_status.bass_correction_gain_db) < 1.0e-9);
    }

    // ---- night compression engages bounded and silence does not amplify -------
    {
        auto policy = base_policy();
        policy.enabled = false;
        policy.analysis_window_ms = 100.0;
        policy.max_rate_db_per_second = 6.0;
        policy.night_compression_enabled = true;
        policy.night_compression_knee_db = 6.0;
        policy.night_compression_max_reduction_db = 9.0;

        ProgramAwareLevelControllerV1 controller;
        CHECK(controller.configure(policy, 48000U));

        constexpr std::size_t kFrames = 4800U;
        constexpr std::size_t kBurstFrames = 240U;  // 5 ms loud burst
        std::vector<float> source(kFrames, 0.0F);
        for (std::size_t frame = 0U; frame < kBurstFrames; ++frame) {
            source[frame] = static_cast<float>(
                0.9 * sine_sample(frame, 1000.0, 48000U));
        }

        std::vector<float> work(kFrames, 0.0F);
        double previous_reduction = 0.0;
        for (int block_index = 0; block_index < 20; ++block_index) {
            work = source;
            CHECK(controller.process_interleaved(work.data(), kFrames, 1U));
            const auto& status = controller.status();
            CHECK(status.night_compression_gain_db <= 1.0e-6);
            CHECK(status.night_compression_gain_db >=
                  -policy.night_compression_max_reduction_db - 1.0e-6);
            CHECK(std::abs(status.night_compression_gain_db -
                           previous_reduction) <= 0.6 + 1.0e-6);
            previous_reduction = status.night_compression_gain_db;
        }
        CHECK(previous_reduction <= -5.0);

        std::fill(work.begin(), work.end(), 0.0F);
        bool became_gated = false;
        for (int block_index = 0; block_index < 30; ++block_index) {
            CHECK(controller.process_interleaved(work.data(), kFrames, 1U));
            const auto& status = controller.status();
            if (status.silence_gated) {
                became_gated = true;
            }
            if (became_gated) {
                CHECK(status.night_compression_gain_db == 0.0);
                CHECK(status.desired_gain_db == 0.0);
                CHECK(status.applied_gain_db == 0.0);
            }
        }
        CHECK(became_gated);
    }

    // ---- telemetry projection ---------------------------------------------------
    {
        ProgramAwareLevelControllerV1 controller;
        auto snapshot = controller.read_telemetry();
        CHECK(!snapshot.valid);
        CHECK(snapshot.sequence == 0ULL);

        auto policy = base_policy();
        policy.analysis_window_ms = 100.0;
        CHECK(controller.configure(policy, 48000U));
        snapshot = controller.read_telemetry();
        CHECK(!snapshot.valid);

        std::vector<float> work(4800U, 0.0F);
        fill_sine(work, 4800U, 1U, 0U, 1000.0, 48000U, 0.25F);
        CHECK(controller.process_interleaved(work.data(), 4800U, 1U));
        snapshot = controller.read_telemetry();
        CHECK(snapshot.valid);
        CHECK(snapshot.enabled);
        CHECK(!snapshot.silence_gated);
        CHECK(snapshot.sequence == 1ULL);
        CHECK(std::isfinite(snapshot.measured_dbfs));
        CHECK(std::isfinite(snapshot.applied_gain_db));

        CHECK(controller.process_interleaved(work.data(), 4800U, 1U));
        snapshot = controller.read_telemetry();
        CHECK(snapshot.sequence == 2ULL);

        controller.reset();
        snapshot = controller.read_telemetry();
        CHECK(!snapshot.valid);
    }

    // ---- concurrent telemetry publication keeps one generation ---------------
    {
        constexpr std::uint32_t kSampleRate = 48000U;
        constexpr std::size_t kFrames = 480U;
        constexpr std::size_t kConcurrentBlocks = 256U;

        auto policy = base_policy();
        policy.analysis_window_ms = 100.0;
        policy.max_rate_db_per_second = 60.0;
        policy.night_compression_enabled = true;
        policy.night_compression_knee_db = 6.0;
        policy.night_compression_max_reduction_db = 9.0;

        std::vector<float> high_block(kFrames, 0.0F);
        std::vector<float> low_block(kFrames, 0.0F);
        for (std::size_t frame = 0U; frame < kFrames; ++frame) {
            const auto sample = static_cast<float>(sine_sample(
                frame, 1000.0, kSampleRate));
            high_block[frame] = (frame < 24U ? 0.95F : 0.01F) * sample;
            low_block[frame] = 0.05F * sample;
        }

        // Build the exact set of valid generations once, then replay the same
        // blocks on the concurrent writer. The reader can validate a returned
        // sequence against the complete expected tuple without touching the
        // writer-owned controller status from the reader thread.
        ProgramAwareLevelControllerV1 expected_controller;
        CHECK(expected_controller.configure(policy, kSampleRate));
        std::vector<hibiki::ProgramAwareLevelStatusV1> expected_statuses;
        expected_statuses.reserve(kConcurrentBlocks);
        std::vector<float> expected_work(kFrames, 0.0F);
        for (std::size_t block = 0U; block < kConcurrentBlocks; ++block) {
            expected_work = (block & 1U) == 0U ? high_block : low_block;
            CHECK(expected_controller.process_interleaved(
                expected_work.data(), kFrames, 1U));
            expected_statuses.push_back(expected_controller.status());
        }

        ProgramAwareLevelControllerV1 concurrent_controller;
        CHECK(concurrent_controller.configure(policy, kSampleRate));
        std::atomic<bool> start{false};
        std::atomic<bool> writer_done{false};
        std::atomic<bool> writer_failed{false};
        std::atomic<bool> reader_failed{false};
        std::thread writer([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            std::vector<float> work(kFrames, 0.0F);
            for (std::size_t block = 0U; block < kConcurrentBlocks; ++block) {
                work = (block & 1U) == 0U ? high_block : low_block;
                if (!concurrent_controller.process_interleaved(
                        work.data(), kFrames, 1U)) {
                    writer_failed.store(true, std::memory_order_release);
                    break;
                }
                std::this_thread::yield();
            }
            writer_done.store(true, std::memory_order_release);
        });
        start.store(true, std::memory_order_release);

        const auto matches_expected = [](const hibiki::ProgramAwareTelemetrySnapshotV1& actual,
                                         const hibiki::ProgramAwareLevelStatusV1& expected,
                                         const std::uint64_t sequence) noexcept {
            return actual.valid == expected.valid &&
                   actual.enabled == expected.enabled &&
                   actual.silence_gated == expected.silence_gated &&
                   actual.measured_dbfs == expected.measured_dbfs &&
                   actual.applied_gain_db == expected.applied_gain_db &&
                   actual.bass_correction_gain_db == expected.bass_correction_gain_db &&
                   actual.night_compression_gain_db == expected.night_compression_gain_db &&
                   actual.sequence == sequence;
        };

        while (!writer_done.load(std::memory_order_acquire)) {
            const auto snapshot = concurrent_controller.read_telemetry();
            if (!snapshot.valid) {
                std::this_thread::yield();
                continue;
            }
            if (snapshot.sequence == 0U ||
                snapshot.sequence > expected_statuses.size() ||
                !matches_expected(snapshot,
                                  expected_statuses[snapshot.sequence - 1U],
                                  snapshot.sequence)) {
                reader_failed.store(true, std::memory_order_release);
                break;
            }
        }
        writer.join();
        CHECK(!writer_failed.load(std::memory_order_acquire));
        CHECK(!reader_failed.load(std::memory_order_acquire));
        const auto final_snapshot = concurrent_controller.read_telemetry();
        CHECK(final_snapshot.valid);
        CHECK(matches_expected(final_snapshot,
                                expected_statuses.back(),
                                kConcurrentBlocks));
    }

    // ---- program-aware level bank -----------------------------------------------
    {
        ProgramAwareLevelBankV1 bank;
        CHECK(!bank.register_group(""));
        const std::string embedded_nul("bad\0label", 9U);
        CHECK(!bank.register_group(embedded_nul));
        const std::string too_long(65U, 'x');
        CHECK(!bank.register_group(too_long));
        CHECK(bank.group_count() == 0U);

        CHECK(bank.register_group("speakers"));
        CHECK(bank.has_group("speakers"));
        CHECK(bank.register_group("speakers"));
        CHECK(bank.group_count() == 1U);

        const std::string longest_allowed(63U, 'L');
        CHECK(bank.register_group(longest_allowed));
        CHECK(bank.has_group(longest_allowed));
        CHECK(bank.group_count() == 2U);

        const std::string max_label(64U, 'M');
        CHECK(bank.register_group(max_label));
        CHECK(bank.has_group(max_label));
        CHECK(bank.controller_for_group(max_label) != nullptr);
        CHECK(bank.group_count() == 3U);

        CHECK(!bank.has_group("missing"));
        CHECK(bank.controller_for_group("missing") == nullptr);
        CHECK(!bank.configure_group("missing", base_policy(), 48000U));

        auto* controller = bank.controller_for_group("speakers");
        CHECK(controller != nullptr);
        std::vector<float> work(480U, 0.1F);
        CHECK(!controller->process_interleaved(work.data(), 480U, 1U));

        auto policy = base_policy();
        policy.schema_version = 9U;
        CHECK(!bank.configure_group("speakers", policy, 48000U));
        CHECK(!controller->process_interleaved(work.data(), 480U, 1U));

        policy = base_policy();
        CHECK(bank.configure_group("speakers", policy, 48000U));
        CHECK(controller->sample_rate() == 48000U);
        CHECK(controller->process_interleaved(work.data(), 480U, 1U));

        bank.reset_all();
        // reset_all() keeps labels registered while clearing every policy
        // and controller state; the public counter keeps matching the number
        // of live slots.
        CHECK(bank.group_count() == 3U);
        CHECK(bank.has_group("speakers"));
        CHECK(bank.has_group(longest_allowed));
        CHECK(bank.has_group(max_label));
        auto* reset_controller = bank.controller_for_group("speakers");
        CHECK(reset_controller != nullptr);
        CHECK(!reset_controller->process_interleaved(work.data(), 480U, 1U));
    }

    // ---- bank capacity is fixed and fail-closed ----------------------------------
    {
        ProgramAwareLevelBankV1 bank;
        for (std::size_t index = 0U;
             index <= hibiki::kMaxProgramAwareGroupsV1; ++index) {
            const std::string label = "group-" + std::to_string(index);
            const bool registered = bank.register_group(label);
            CHECK(registered == (index < hibiki::kMaxProgramAwareGroupsV1));
        }
        CHECK(bank.group_count() == hibiki::kMaxProgramAwareGroupsV1);
        CHECK(bank.has_group("group-0"));
        CHECK(bank.has_group("group-31"));
        CHECK(!bank.has_group("group-32"));
    }

    return 0;
}
