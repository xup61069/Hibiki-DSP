// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/noise_suppressor.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

constexpr std::uint32_t kSampleRate = 48000U;
constexpr std::uint32_t kChannels = 1U;

hibiki::BasicNoiseSuppressorPolicyV1 make_gate_policy() {
    hibiki::BasicNoiseSuppressorPolicyV1 policy{};
    policy.enabled = true;
    policy.threshold_dbfs = -40.0;   // linear 0.01; reopen level ~0.012589
    policy.floor_db = -30.0;         // linear ~0.031623
    policy.attack_ms = 2.0;
    policy.release_ms = 20.0;
    policy.highpass_hz = 0.0;        // bypass filter: output == input * gain
    return policy;
}

bool run_constant(hibiki::BasicNoiseSuppressorV1& suppressor,
                  std::vector<float>& block,
                  const float value,
                  const std::size_t frames) noexcept {
    block.assign(frames, value);
    return suppressor.process_interleaved(block.data(), frames);
}

bool all_finite(const std::vector<float>& block) noexcept {
    for (const auto sample : block) {
        if (!std::isfinite(sample)) return false;
    }
    return true;
}

// After reset() the very next frame must be bit-for-bit identical to what a
// freshly configured instance produces from the same input. Any residual
// envelope, gain, gate, high-pass or previous-input state would change the
// first-frame result, so this catches incomplete state restoration exactly.
bool reset_restores_initial_response(const double highpass_hz) noexcept {
    auto policy = make_gate_policy();
    policy.highpass_hz = highpass_hz;

    // Drive the reused instance into a loud, open-gate steady state so every
    // piece of per-channel state differs from the initial one before reset.
    std::vector<float> warm(5000U, 0.5F);

    hibiki::BasicNoiseSuppressorV1 fresh;
    if (!fresh.configure(policy, kSampleRate, kChannels)) return false;
    hibiki::BasicNoiseSuppressorV1 warmed;
    if (!warmed.configure(policy, kSampleRate, kChannels)) return false;
    if (!warmed.process_interleaved(warm.data(), warm.size())) return false;
    warmed.reset();

    // process_interleaved writes output in-place over the input, so each
    // instance needs its own probe buffer holding the same input sample.
    std::vector<float> probe_fresh{0.5F};
    std::vector<float> probe_warm{0.5F};
    if (!fresh.process_interleaved(probe_fresh.data(), probe_fresh.size()))
        return false;
    if (!warmed.process_interleaved(probe_warm.data(), probe_warm.size()))
        return false;

    return probe_fresh[0] == probe_warm[0];
}

}  // namespace

int main() {
    // A disabled policy is not a valid configuration: configure() must reject
    // it fail-closed instead of silently behaving like a bypass.
    const hibiki::BasicNoiseSuppressorPolicyV1 disabled{};
    CHECK(!hibiki::validate_noise_suppressor_policy(disabled));

    hibiki::BasicNoiseSuppressorV1 suppressor;
    CHECK(!suppressor.configured());
    CHECK(!suppressor.configure(disabled, 48000U, 2U));
    CHECK(!suppressor.configured());

    std::array<float, 8> block{};
    block.fill(0.01F);
    CHECK(!suppressor.process_interleaved(block.data(), block.size()));

    // An enabled policy is processed normally with unchanged DSP math.
    const hibiki::BasicNoiseSuppressorPolicyV1 enabled{
        1U, true, -40.0, -30.0, 1.0, 10.0, 0.0};
    CHECK(hibiki::validate_noise_suppressor_policy(enabled));
    hibiki::BasicNoiseSuppressorV1 active;
    CHECK(active.configure(enabled, 48000U, 2U));
    CHECK(active.process_interleaved(block.data(), block.size()));

    // Configured instances still reject null buffers and empty blocks.
    CHECK(!active.process_interleaved(nullptr, 100U));
    CHECK(!active.process_interleaved(block.data(), 0U));

    // Interleaved geometry must be checked before the first caller-buffer read
    // or state update. Use the first overflowing eight-channel frame count
    // without allocating a correspondingly huge buffer, then compare the next
    // valid block with an untouched control instance.
    constexpr std::uint32_t kEightChannels = 8U;
    const auto overflow_frames =
        std::numeric_limits<std::size_t>::max() /
            static_cast<std::size_t>(kEightChannels) +
        1U;
    hibiki::BasicNoiseSuppressorV1 overflow_candidate;
    hibiki::BasicNoiseSuppressorV1 overflow_control;
    CHECK(overflow_candidate.configure(enabled, 48000U, kEightChannels));
    CHECK(overflow_control.configure(enabled, 48000U, kEightChannels));
    std::array<float, 16U> warmup{};
    for (std::size_t index = 0U; index < warmup.size(); ++index) {
        warmup[index] = 0.25F + static_cast<float>(index) * 0.01F;
    }
    auto control_warmup = warmup;
    CHECK(overflow_candidate.process_interleaved(warmup.data(), 2U));
    CHECK(overflow_control.process_interleaved(control_warmup.data(), 2U));
    const std::array<float, 8U> overflow_sentinels{
        11.0F, 12.0F, 13.0F, 14.0F, 15.0F, 16.0F, 17.0F, 18.0F};
    auto overflow_buffer = overflow_sentinels;
    CHECK(!overflow_candidate.process_interleaved(overflow_buffer.data(),
                                                  overflow_frames));
    CHECK(overflow_buffer == overflow_sentinels);
    std::array<float, 16U> next_candidate{};
    std::array<float, 16U> next_control{};
    next_candidate.fill(0.125F);
    next_control.fill(0.125F);
    CHECK(overflow_candidate.process_interleaved(next_candidate.data(), 2U));
    CHECK(overflow_control.process_interleaved(next_control.data(), 2U));
    CHECK(next_candidate == next_control);

    // ---- policy boundaries -------------------------------------------------
    auto boundary = make_gate_policy();
    boundary.threshold_dbfs = 0.0;
    CHECK(!hibiki::validate_noise_suppressor_policy(boundary));
    hibiki::BasicNoiseSuppressorV1 reject_zero_threshold;
    CHECK(!reject_zero_threshold.configure(boundary, kSampleRate, kChannels));

    boundary = make_gate_policy();
    boundary.floor_db = -96.0;
    CHECK(hibiki::validate_noise_suppressor_policy(boundary));
    hibiki::BasicNoiseSuppressorV1 accept_floor_limit;
    CHECK(accept_floor_limit.configure(boundary, kSampleRate, kChannels));

    boundary = make_gate_policy();
    boundary.highpass_hz = 2000.5;
    CHECK(!hibiki::validate_noise_suppressor_policy(boundary));

    boundary = make_gate_policy();
    boundary.highpass_hz = 2000.0;
    CHECK(hibiki::validate_noise_suppressor_policy(boundary));
    // 2000 Hz is the validation ceiling. At the minimum supported rate the
    // configure-time Nyquist guard (highpass >= rate / 2) stays defense-in-
    // depth only: 8000 / 2 = 4000 Hz exceeds every value validate accepts,
    // so this legal edge must configure successfully.
    hibiki::BasicNoiseSuppressorV1 accept_highpass_ceiling;
    CHECK(accept_highpass_ceiling.configure(boundary, 8000U, kChannels));

    // ---- gate closes: quiet input converges toward the floor ---------------
    // threshold linear is 0.01; 0.001 sits far below it. After the gain has
    // settled the output approaches input * floor_linear ~= 3.16e-5.
    hibiki::BasicNoiseSuppressorV1 quieter;
    std::vector<float> samples;
    CHECK(quieter.configure(make_gate_policy(), kSampleRate, kChannels));
    CHECK(run_constant(quieter, samples, 0.001F, 20000U));
    CHECK(all_finite(samples));
    CHECK(samples.back() < 5.0e-5F);

    // ---- gate opens: sustained loud input converges toward unity -----------
    hibiki::BasicNoiseSuppressorV1 louder;
    CHECK(louder.configure(make_gate_policy(), kSampleRate, kChannels));
    CHECK(run_constant(louder, samples, 0.5F, 5000U));
    CHECK(all_finite(samples));
    CHECK(samples.back() > 0.495F);

    // ---- upper-only hysteresis: hold band keeps an open gate open ----------
    // Drop from 0.5 into the 0.01..0.012589 hold band. The envelope decays
    // toward 0.011, which stays above the close threshold forever, so the
    // gate must remain open and the output must stay near unity gain.
    CHECK(run_constant(louder, samples, 0.011F, 8000U));
    CHECK(all_finite(samples));
    CHECK(samples.back() > 0.01045F);

    // ---- hysteresis: a closed gate must not self-open inside the band ------
    // A fresh instance fed only the hold-band amplitude never crosses the
    // reopen level, so it must converge to the floor-attenuated output.
    hibiki::BasicNoiseSuppressorV1 hold_band_closed;
    CHECK(hold_band_closed.configure(make_gate_policy(), kSampleRate, kChannels));
    CHECK(run_constant(hold_band_closed, samples, 0.011F, 20000U));
    CHECK(all_finite(samples));
    CHECK(samples.back() < 5.5e-4F);

    // ---- reset restores the exact initial response -------------------------
    CHECK(reset_restores_initial_response(0.0));
    CHECK(reset_restores_initial_response(80.0));

    // ---- non-finite samples are neutralized without poisoning state --------
    hibiki::BasicNoiseSuppressorV1 sanitizer;
    CHECK(sanitizer.configure(make_gate_policy(), kSampleRate, kChannels));
    samples = {std::numeric_limits<float>::quiet_NaN(),
               std::numeric_limits<float>::infinity(),
               -std::numeric_limits<float>::infinity(),
               0.25F,
               -0.25F};
    CHECK(sanitizer.process_interleaved(samples.data(), samples.size()));
    CHECK(all_finite(samples));
    CHECK(run_constant(sanitizer, samples, 0.5F, 200U));
    CHECK(all_finite(samples));
    CHECK(samples.back() > 0.4F);

    return 0;
}
