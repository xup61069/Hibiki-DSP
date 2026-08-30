// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/volume_state.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <limits>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {
using namespace hibiki;
constexpr double kInfD = std::numeric_limits<double>::infinity();
}  // namespace

int main() {
    const auto qnan_d = std::numeric_limits<double>::quiet_NaN();

    // ---- effective_gain_db ---------------------------------------------------
    {
        CHECK(effective_gain_db(0.0, 0.0) == 0.0);
        CHECK(effective_gain_db(-6.0, -3.0) == -6.0);   // requested < ceiling
        CHECK(effective_gain_db(-1.0, -6.0) == -6.0);   // ceiling wins
        CHECK(effective_gain_db(qnan_d, 0.0) == -144.0);
        CHECK(effective_gain_db(0.0, kInfD) == -144.0);
        CHECK(effective_gain_db(-200.0, 12.0) == -144.0);  // clamped low
        CHECK(effective_gain_db(24.0, 24.0) == 12.0);       // clamped high
    }

    // ---- db_to_q16_16 / q16_16_to_db round-trip -------------------------------
    {
        CHECK(db_to_q16_16(-60.0) == -60 * 65536);
        CHECK(db_to_q16_16(0.0) == 0);
        CHECK(db_to_q16_16(12.0) == 12 * 65536);
        CHECK(db_to_q16_16(-144.0) == -144 * 65536);
        CHECK(db_to_q16_16(qnan_d) == -144 * 65536);

        CHECK(q16_16_to_db(-60 * 65536) == -60.0);
        CHECK(q16_16_to_db(12 * 65536 + 99999) == 12.0);  // clamped
        CHECK(q16_16_to_db(-144 * 65536 - 1) == -144.0);  // clamped
    }

    // ---- reconcile() ------------------------------------------------------------
    {
        OutputGroupVolumeStateV1 s{};
        s.schema_version = 99U;
        s.requested_db = -10.0;
        s.safety_ceiling_db = -5.0;
        s.actuator = ActuatorMode::InternalDsp;
        const auto r = reconcile(s);
        CHECK(r.schema_version == 1U);
        CHECK(r.effective_db == -10.0);  // min(requested, safety)

        s.actuator = ActuatorMode::StrictDirect;
        const auto r2 = reconcile(s);
        CHECK(r2.effective_db == 0.0);
    }

    // ---- make_ramp() -----------------------------------------------------------------
    {
        OutputGroupVolumeStateV1 before{};
        before.effective_db = -20.0;
        before.mute = false;
        OutputGroupVolumeStateV1 after_mute{};
        after_mute.effective_db = -20.0;
        after_mute.mute = true;
        OutputGroupVolumeStateV1 after_unmute{};
        after_unmute.effective_db = -15.0;
        after_unmute.mute = false;

        const auto r_normal = make_ramp(before, after_unmute);
        CHECK(r_normal.duration_ms == 8U);
        const auto r_to_mute = make_ramp(before, after_mute);
        CHECK(r_to_mute.duration_ms == 5U);
        const auto r_from_mute = make_ramp(after_mute, after_unmute);
        CHECK(r_from_mute.duration_ms == 15U);
    }

    // ---- VolumeRampProcessorV1 basic behavior -------------------------------------------
    {
        VolumeRampProcessorV1 ramp;
        ramp.reset(0.0, false);
        CHECK(ramp.current_db() == 0.0);

        // Observe target at same position: no change needed.
        ramp.observe_target(0, false, 48000U);
        // First gain call should produce ~1.0 (0 dB)
        const auto g = ramp.next_gain();
        CHECK(g > 0.9F && g <= 1.01F);

        // Ramp down to -40 dB
        ramp.observe_target(-40 * 65536, false, 48000U);
        CHECK(ramp.remaining_frames() > 0U);
        float min_g = 2.0F;
        while (ramp.remaining_frames() > 0U) {
            const auto v = ramp.next_gain();
            if (v < min_g) min_g = v;
        }
        // After ramp completes, current_db should be near -40
        CHECK(ramp.current_db() < -39.0 && ramp.current_db() > -41.0);
        // The minimum gain during the ramp should be less than the starting gain
        CHECK(min_g < 0.95F);
    }

    // ---- VolumeRampProcessorV1 mute behavior ----------------------------------------------
    {
        VolumeRampProcessorV1 ramp;
        ramp.reset(0.0, false);
        ramp.observe_target(0, true, 48000U);
        // Mute duration is 5ms at 48k = 240 frames
        std::uint32_t count = 0U;
        float last = 1.0F;
        while (ramp.remaining_frames() > 0U) { last = ramp.next_gain(); ++count; }
        // Final gain must be exactly zero when muted and ramp done
        CHECK(ramp.next_gain() == 0.0F);
        CHECK(count > 0U);
        CHECK(last >= 0.0F);
    }

    // ---- apply_windows_notification free function ------------------------------------------
    {
        OutputGroupVolumeStateV1 empty{};
        CHECK(apply_windows_notification(empty, VolumeNotificationV1{-12.0, false, 0U}) ==
              VolumeNotificationResult::Invalid);
        CHECK(empty.requested_db == -60.0 && empty.effective_db == -60.0 &&
              empty.generation == 0U && !empty.mute && empty.origin == VolumeOrigin::Windows);

        OutputGroupVolumeStateV1 state{};
        state.generation = 5U;
        state.safety_ceiling_db = -6.0;

        // Valid notification accepted
        VolumeNotificationV1 n{-12.0, false, 6U};
        CHECK(apply_windows_notification(state, n) == VolumeNotificationResult::Accepted);
        CHECK(state.requested_db == -12.0);
        CHECK(state.generation == 6U);
        CHECK(state.origin == VolumeOrigin::Windows);
        CHECK(state.effective_db == -12.0);

        // Same generation is not stale
        n.generation = 6U;
        n.requested_db = -8.0;
        CHECK(apply_windows_notification(state, n) == VolumeNotificationResult::Accepted);

        // Stale generation rejected
        n.generation = 5U;
        CHECK(apply_windows_notification(state, n) == VolumeNotificationResult::StaleGeneration);

        // Out of range rejected
        n.generation = 7U;
        n.requested_db = -145.0;
        CHECK(apply_windows_notification(state, n) == VolumeNotificationResult::Invalid);
        n.requested_db = 13.0;
        CHECK(apply_windows_notification(state, n) == VolumeNotificationResult::Invalid);
        n.requested_db = qnan_d;
        CHECK(apply_windows_notification(state, n) == VolumeNotificationResult::Invalid);
    }

    // ---- OutputGroupVolumeBankV1 registration ---------------------------------------------
    {
        OutputGroupVolumeBankV1 bank;  // registers "main" in constructor
        CHECK(bank.has_group("main"));
        CHECK(bank.group_count() == 1U);
        CHECK(bank.register_group("headphones"));
        CHECK(bank.group_count() == 2U);

        // Idempotent re-register
        CHECK(bank.register_group("main"));
        CHECK(bank.group_count() == 2U);

        // Invalid labels
        CHECK(!bank.register_group(""));
        const char nul_label[] = {'a', '\0', 'b'};
        CHECK(!bank.register_group(std::string_view(nul_label, 3)));

        // Oversized label (>64 bytes)
        char big_buf[101];
        for (auto& c : big_buf) c = 'x';
        big_buf[100] = '\0';
        const std::string_view big(big_buf, 100U);
        CHECK(!bank.register_group(big));

        // Unregistered group lookup
        CHECK(!bank.has_group("nonexistent"));
        CHECK(bank.limiter_for_group("nonexistent") == nullptr);

        // Registered group limiter
        CHECK(bank.limiter_for_group("main") != nullptr);
    }

    // ---- Bank capacity limit (32 groups max) -----------------------------------------------
    {
        OutputGroupVolumeBankV1 bank;
        for (std::size_t i = 1U; i <= 31U; ++i) {
            char label[8];
            std::snprintf(label, sizeof(label), "g%zu", i);
            CHECK(bank.register_group(label));
        }
        CHECK(bank.group_count() == 32U);
        // 33rd group fails
        CHECK(!bank.register_group("overflow"));
    }

    // ---- Bank apply_windows_notification ----------------------------------------------------
    {
        OutputGroupVolumeBankV1 bank;
        (void)bank.register_group("spk");

        VolumeNotificationV1 n{-20.0, false, 5U};
        CHECK(bank.apply_windows_notification("spk", n) == VolumeNotificationResult::Accepted);

        n.generation = 0U;
        CHECK(bank.apply_windows_notification("spk", n) == VolumeNotificationResult::Invalid);
        CHECK(bank.state("spk").generation == 5U &&
              bank.state("spk").requested_db == -20.0);

        // Stale
        n.generation = 4U;
        CHECK(bank.apply_windows_notification("spk", n) == VolumeNotificationResult::StaleGeneration);

        // Unregistered group
        n.generation = 2U;
        CHECK(bank.apply_windows_notification("ghost", n) == VolumeNotificationResult::Invalid);

        // State reflects update
        const auto s = bank.state("spk");
        CHECK(s.requested_db == -20.0);
        CHECK(s.generation == 5U);
    }

    // ---- Bank apply_to_interleaved ------------------------------------------------------------
    {
        OutputGroupVolumeBankV1 bank;
        (void)bank.register_group("out");
        // Set volume to 0 dB (unity); default state starts at -60 dB, so the
        // ramp needs to settle before samples pass through at full gain.
        (void)bank.apply_windows_notification("out", {0.0, false, 1U});
        float warmup[8] = {};
        for (int i = 0; i < 4000; ++i) {
            CHECK(bank.apply_to_interleaved("out", warmup, 4U, 2U, 48000U));
        }
        const auto settled = bank.state("out");
        CHECK(settled.effective_db == 0.0);

        float buf[4] = {1.0F, -1.0F, 0.5F, -0.5F};

        // Invalid calls
        CHECK(!bank.apply_to_interleaved("out", nullptr, 2U, 2U, 48000U));
        float untouched[4] = {1.0F, -1.0F, 0.5F, -0.5F};
        CHECK(!bank.apply_to_interleaved("out", untouched, 0U, 2U, 48000U));
        CHECK(!bank.apply_to_interleaved("out", untouched, 2U, 0U, 48000U));
        CHECK(!bank.apply_to_interleaved("out", buf, 2U, 9U, 48000U));
        CHECK(!bank.apply_to_interleaved("missing", buf, 2U, 2U, 48000U));

        // Buffer unchanged by invalid calls
        CHECK(buf[0] == 1.0F && buf[3] == -0.5F);

        // Interleaved geometry must be rejected before the first caller-buffer
        // access or VolumeRamp state update. Compare the next valid block with
        // an untouched control bank after the first overflowing eight-channel
        // request.
        constexpr std::uint32_t kEightChannels = 8U;
        const auto overflow_frames =
            std::numeric_limits<std::size_t>::max() /
                static_cast<std::size_t>(kEightChannels) +
            1U;
        OutputGroupVolumeBankV1 overflow_candidate;
        OutputGroupVolumeBankV1 overflow_control;
        CHECK(overflow_candidate.register_group("out"));
        CHECK(overflow_control.register_group("out"));
        const VolumeNotificationV1 target{-12.0, false, 1U};
        CHECK(overflow_candidate.apply_windows_notification("out", target) ==
              VolumeNotificationResult::Accepted);
        CHECK(overflow_control.apply_windows_notification("out", target) ==
              VolumeNotificationResult::Accepted);
        const std::array<float, 8U> overflow_sentinels{
            11.0F, 12.0F, 13.0F, 14.0F, 15.0F, 16.0F, 17.0F, 18.0F};
        auto overflow_buffer = overflow_sentinels;
        CHECK(!overflow_candidate.apply_to_interleaved("out", overflow_buffer.data(),
                                                       overflow_frames, kEightChannels,
                                                       48000U));
        CHECK(overflow_buffer == overflow_sentinels);
        std::array<float, 16U> next_candidate{};
        std::array<float, 16U> next_control{};
        next_candidate.fill(0.25F);
        next_control.fill(0.25F);
        CHECK(overflow_candidate.apply_to_interleaved("out", next_candidate.data(),
                                                       2U, kEightChannels, 48000U));
        CHECK(overflow_control.apply_to_interleaved("out", next_control.data(),
                                                    2U, kEightChannels, 48000U));
        CHECK(next_candidate == next_control);

        // Non-finite samples must be rejected before the caller buffer is
        // touched or the VolumeRamp state advances.  Compare the candidate's
        // next finite block with an untouched control bank to cover both.
        const std::array<float, 3U> invalid_samples{
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity()};
        for (const auto invalid : invalid_samples) {
            OutputGroupVolumeBankV1 nonfinite_candidate;
            OutputGroupVolumeBankV1 nonfinite_control;
            CHECK(nonfinite_candidate.register_group("out"));
            CHECK(nonfinite_control.register_group("out"));
            CHECK(nonfinite_candidate.apply_windows_notification("out", target) ==
                  VolumeNotificationResult::Accepted);
            CHECK(nonfinite_control.apply_windows_notification("out", target) ==
                  VolumeNotificationResult::Accepted);
            std::array<float, 8U> rejected_buffer{
                invalid, 0.5F, -0.5F, 0.25F, 0.75F, -0.75F, 0.125F, -0.125F};
            const auto rejected_sentinels = rejected_buffer;
            CHECK(!nonfinite_candidate.apply_to_interleaved("out", rejected_buffer.data(),
                                                             2U, 4U, 48000U));
            CHECK(std::memcmp(rejected_buffer.data(), rejected_sentinels.data(),
                              sizeof(rejected_buffer)) == 0);
            std::array<float, 8U> finite_candidate{};
            std::array<float, 8U> finite_control{};
            finite_candidate.fill(0.25F);
            finite_control.fill(0.25F);
            CHECK(nonfinite_candidate.apply_to_interleaved("out", finite_candidate.data(),
                                                             2U, 4U, 48000U));
            CHECK(nonfinite_control.apply_to_interleaved("out", finite_control.data(),
                                                          2U, 4U, 48000U));
            CHECK(finite_candidate == finite_control);
        }

        // Valid call after ramp warm-up should approximately preserve values
        CHECK(bank.apply_to_interleaved("out", buf, 2U, 2U, 48000U));
        CHECK(buf[0] > 0.99F && buf[0] <= 1.01F);
        CHECK(buf[3] > -0.505F && buf[3] < -0.495F);
    }

    // ---- reset_limiters() ------------------------------------------------------------------------
    {
        OutputGroupVolumeBankV1 bank;
        (void)bank.register_group("a");
        auto* lim = bank.limiter_for_group("a");
        CHECK(lim != nullptr);
        // Push some samples through to change limiter internal state
        float samples[4] = {2.0F, -2.0F, 2.0F, -2.0F};
        const auto lim_result = lim->limit_in_place(samples, 4U, 1U, -1.0, 48000U);
        CHECK(lim_result > 0.0F && lim_result <= 1.01F);
        bank.reset_limiters();
        // After reset, limiter should pass through unity for normal levels
        float clean[2] = {0.25F, -0.25F};
        const auto g = lim->limit_in_place(clean, 2U, 1U, -1.0, 48000U);
        CHECK(g == 1.0F);
    }

    return 0;
}
