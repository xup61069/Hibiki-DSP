// SPDX-License-Identifier: GPL-3.0-only

using Hibiki.ControlModel;

/// <summary>Offline self-test for the RemainingSafeTimeText property.</summary>
public static class DoseRemainingCheck
{
    public static void Run(Action<bool, string> check)
    {
        var dose = new ListeningDoseModelV1();
        check(dose.RemainingSafeTimeText == "尚無資料",
            "A fresh dose must report no data for the remaining safe time.");

        check(!dose.AddSample(new DateTimeOffset(2026, 8, 26, 10, 0, 0, TimeSpan.Zero), -200.0, false),
            "An out-of-range volume must be rejected.");
        check(dose.RemainingSafeTimeText == "尚無資料",
            "Rejected samples must not produce a remaining-time label.");

        check(dose.AddSample(new DateTimeOffset(2026, 8, 26, 10, 0, 0, TimeSpan.Zero), -6.0, true),
            "A muted loud sample must be accepted.");
        check(dose.RemainingSafeTimeText == "安全範圍內；無倒數限制",
            "A zero-rate state must show no countdown.");

        // Active loud listening at 0 dBFS (94 dBA): excess = 9 dB,
        // rate = 100 * 2^(9/3) / 8 = 100%/h. Use 4-minute steps.
        dose = new ListeningDoseModelV1();
        var t0 = new DateTimeOffset(2026, 8, 26, 11, 0, 0, TimeSpan.Zero);
        check(dose.AddSample(t0, 0.0, false),
            "An unmuted 0 dB sample must be accepted.");
        check(dose.AccumulatedDosePercent == 0.0 &&
              dose.RemainingSafeTimeText.Contains("小時", StringComparison.Ordinal) &&
              dose.RemainingSafeTimeText.StartsWith("約 1", StringComparison.Ordinal),
            $"At 100%/h with 0% used, expect about 1 hour, got '{dose.RemainingSafeTimeText}'.");

        // Three 4-min steps = 12 min total: 720s * (100/3600) = 20% consumed.
        // Remainder: 80%/100%per-h = 48 min.
        for (var m = 4; m <= 12; m += 4)
            dose.AddSample(t0.AddMinutes(m), 0.0, false);
        check(dose.AccumulatedDosePercent > 18.0 && dose.AccumulatedDosePercent < 22.0,
            $"Twelve minutes at 100%/h should accumulate about 20%, got {dose.AccumulatedDosePercent:0.#}%.");
        check(dose.RemainingSafeTimeText == "約 48 分鐘",
            $"After 20% used at 100%/h, expect 48 minutes, got '{dose.RemainingSafeTimeText}'.");

        // Continue to ~97% after 58 minutes.
        for (var m = 16; m <= 56; m += 4)
            dose.AddSample(t0.AddMinutes(m), 0.0, false);
        var tNear = t0.AddMinutes(58);
        dose.AddSample(tNear, 0.0, false);
        check(dose.AccumulatedDosePercent >= 94.0 && dose.AccumulatedDosePercent < 99.0,
            $"Fifty-eight minutes at 100%/h should accumulate about 97%, got {dose.AccumulatedDosePercent:0.#}%.");
        check(dose.RemainingSafeTimeText.Contains("分鐘", StringComparison.Ordinal),
            "Under an hour of remaining time should use minutes.");

        // Push past 100%.
        var tOver = t0.AddMinutes(62);
        dose.AddSample(tOver, 0.0, false);
        check(dose.AccumulatedDosePercent >= 100.0,
            "Sixty-two minutes at 100%/h must exceed 100%.");
        check(dose.RemainingSafeTimeText == "已過量；建議休息",
            $"Past 100% must show the rest message, got '{dose.RemainingSafeTimeText}'.");

        dose.ResetDaily();
        check(dose.RemainingSafeTimeText == "尚無資料",
            "ResetDaily must restore the no-data label.");

        // Daily rollover: samples carry their own UTC offset, so the local
        // wall-clock date is judged from the sample itself. Accumulation from
        // the previous day must not bill into the new day.
        var lateNight = new DateTimeOffset(2026, 8, 26, 23, 55, 0,
            TimeSpan.FromHours(8));
        var afterMidnight = new DateTimeOffset(2026, 8, 27, 0, 5, 0,
            TimeSpan.FromHours(8));
        var rollover = new ListeningDoseModelV1();
        check(rollover.AddSample(lateNight, 0.0, false),
            "A pre-midnight loud sample must be accepted.");
        for (var t = 1; t <= 4; ++t)
            rollover.AddSample(lateNight.AddMinutes(t), 0.0, false);
        check(rollover.AccumulatedDosePercent > 6.0 && rollover.AccumulatedDosePercent < 10.0,
            $"Five pre-midnight minutes at 100%/h should accumulate about 8.3%, got {rollover.AccumulatedDosePercent:0.#}%.");
        check(rollover.AddSample(afterMidnight, 0.0, false),
            "A post-midnight loud sample must be accepted.");
        check(rollover.AccumulatedDosePercent == 0.0,
            "Crossing local midnight must restart accumulation from zero at the new sample.");
        check(rollover.LastSampleGap == TimeSpan.Zero,
            "The rollover sample must not be billed as a gap from yesterday.");

        // Same-day continuation is unaffected by the rollover logic.
        for (var t = 5; t <= 9; ++t)
            rollover.AddSample(afterMidnight.AddMinutes(t), 0.0, false);
        check(rollover.AccumulatedDosePercent > 13.0 && rollover.AccumulatedDosePercent < 17.0,
            $"Nine post-midnight minutes at 100%/h should accumulate about 15%, got {rollover.AccumulatedDosePercent:0.#}%.");
        check(rollover.StateText.Contains("今日", StringComparison.Ordinal) &&
              !rollover.StateText.Contains("自啟動起", StringComparison.Ordinal),
            "The state label must say 今日 after a fresh-day window.");
    }
}
