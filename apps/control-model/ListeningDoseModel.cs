// SPDX-License-Identifier: GPL-3.0-only

namespace Hibiki.ControlModel;

// Local listening-dose reference indicator. The model folds periodic effective
// volume samples into a bounded daily percentage using a conservative full-
// scale assumption (0 dBFS ~ 94 dBA) and the 85 dBA / 3 dB exchange baseline.
// This is an uncalibrated UI-only hint: it never measures headphones or the
// room, does not touch the audio path, and stays honest about being a
// reference. Memory use is fixed: only the previous sample is retained.
public sealed class ListeningDoseModelV1
{
    // 0 dBFS program material is commonly near 94 dBA at the ear on consumer
    // playback chains. Combined with engine attenuation this is deliberately
    // conservative: real listening levels are often lower.
    public const double ReferenceFullScaleDba = 94.0;
    public const double CriterionDba = 85.0;
    public const double ExchangeDb = 3.0;
    private const double MinValidDb = -144.0;

    private DateTimeOffset _lastSampleUtc;
    private double _lastRatePerSecond;
    private bool _hasLastSample;
    private double _accumulatedDosePercent;
    private TimeSpan _lastGap;
    private DateTimeOffset _windowStartUtc = DateTimeOffset.UtcNow;

    // A sample arriving after longer than this bound ends accumulation for
    // that interval; the silent gap contributes nothing. Sleep, suspend or a
    // stopped status poll therefore never counts as continuous exposure.
    public static readonly TimeSpan MaxSampleInterval = TimeSpan.FromMinutes(5);

    public double AccumulatedDosePercent => _accumulatedDosePercent;
    public bool IsAccumulating => _hasLastSample && _lastRatePerSecond > 0.0;
    public TimeSpan LastSampleGap => _lastGap;
    public DateTimeOffset WindowStartUtc => _windowStartUtc;

    // There is no automatic local-date rollover yet, so the visible label stays
    // honest about the accumulation window instead of claiming a day that has
    // not actually been reset.

    public string StateText =>
        _accumulatedDosePercent >= 100.0
            ? $"聆聽劑量（自啟動起）：{_accumulatedDosePercent:0}%（過量；建議讓耳朵休息）"
            : _accumulatedDosePercent >= 50.0
                ? $"聆聽劑量（自啟動起）：{_accumulatedDosePercent:0}%（注意；接近每日上限）"
                : $"聆聽劑量（自啟動起）：{_accumulatedDosePercent:0}%（安全；未校正參考值）";

    /// <summary>
    /// Shows how long until the accumulated dose reaches 100% at the current
    /// loudness rate. Returns an honest label when there is no data yet, when
    /// the rate is zero (safe or muted), or when the budget is already spent.
    /// </summary>
    public string RemainingSafeTimeText
    {
        get
        {
            if (!_hasLastSample)
                return "尚無資料";

            if (_lastRatePerSecond <= 0.0)
                return "安全範圍內；無倒數限制";

            if (_accumulatedDosePercent >= 100.0)
                return "已過量；建議休息";

            var remainingPercent = Math.Max(0.0, 100.0 - _accumulatedDosePercent);
            var seconds = remainingPercent / _lastRatePerSecond;
            if (seconds < 60.0)
                return $"約 {seconds:0} 秒";
            if (seconds < 3600.0)
                return $"約 {seconds / 60.0:0} 分鐘";
            return $"約 {seconds / 3600.0:0.#} 小時";
        }
    }

    /// <summary>
    /// Folds one confirmed effective-volume sample taken now. Invalid samples
    /// are rejected fail-closed without touching accumulated state. Muted
    /// samples fold with a zero dose rate so the silent interval is never
    /// retroactively billed at the previous loud rate; the accumulator still
    /// advances its time anchor so the next unmuted sample starts fresh.
    /// </summary>
    public bool AddSample(DateTimeOffset atUtc, double effectiveVolumeDb,
                          bool muted)
    {
        if (!double.IsFinite(effectiveVolumeDb) ||
            effectiveVolumeDb <= MinValidDb)
        {
            return false;
        }

        var earLevel = ReferenceFullScaleDba + effectiveVolumeDb;
        var excessDb = Math.Max(0.0, earLevel - CriterionDba);
        // 85 dBA sustained for 8 h equals 100%; every +3 dB halves the safe
        // exposure time. Levels at or below the criterion contribute nothing.
        var doseRatePerHour = excessDb <= 0.0
            ? 0.0
            : 100.0 * Math.Pow(2.0, excessDb / ExchangeDb) / 8.0;
        var ratePerSecond = muted ? 0.0 : doseRatePerHour / 3600.0;

        if (_hasLastSample)
        {
            var delta = atUtc - _lastSampleUtc;
            _lastGap = delta;
            if (delta > MaxSampleInterval)
            {
                // Long silence: keep what was already earned, then restart
                // the integration window from this sample onward. The gap
                // itself adds nothing.
            }
            else if (delta > TimeSpan.Zero)
            {
                var seconds = delta.TotalSeconds;
                // Trapezoid between the previous and current dose rates.
                _accumulatedDosePercent +=
                    seconds * (_lastRatePerSecond + ratePerSecond) / 2.0;
            }
        }

        _lastSampleUtc = atUtc;
        _lastRatePerSecond = ratePerSecond;
        _hasLastSample = true;
        return true;
    }

    /// <summary>Clears today's dose (new day or explicit reset).</summary>
    public void ResetDaily()
    {
        _hasLastSample = false;
        _lastRatePerSecond = 0.0;
        _lastGap = TimeSpan.Zero;
        _accumulatedDosePercent = 0.0;
        _windowStartUtc = DateTimeOffset.UtcNow;
    }
}
