// SPDX-License-Identifier: GPL-3.0-only

using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Hibiki.ControlModel;

public sealed record CalibrationPointV1(
    double FrequencyHz,
    double MeasuredDb,
    double TargetDb)
{
    public bool IsValid =>
        double.IsFinite(FrequencyHz) && FrequencyHz is > 0.0 and <= 24000.0 &&
        double.IsFinite(MeasuredDb) && MeasuredDb is >= -144.0 and <= 12.0 &&
        double.IsFinite(TargetDb) && TargetDb is >= -144.0 and <= 12.0;
}

public sealed record CalibrationResponseV1(
    uint SchemaVersion,
    double SampleRate,
    int Channels,
    string? DeviceId,
    IReadOnlyList<CalibrationPointV1> Points)
{
    public const int MaxPoints = 512;

    public bool IsValid
    {
        get
        {
            if (SchemaVersion != 1U ||
                !double.IsFinite(SampleRate) || SampleRate is < 8000.0 or > 384000.0 ||
                Channels is not (1 or 2 or 6 or 8) ||
                Points is null || Points.Count is < 1 or > MaxPoints)
            {
                return false;
            }

            if (DeviceId is not null && (DeviceId.Length < 1 || DeviceId.Any(char.IsControl)))
            {
                return false;
            }

            var previousFrequency = 0.0;
            foreach (var point in Points)
            {
                if (point is null || !point.IsValid || point.FrequencyHz <= previousFrequency)
                {
                    return false;
                }
                previousFrequency = point.FrequencyHz;
            }

            return true;
        }
    }
}

public sealed record PeqFilterV1(
    string Type,
    double FrequencyHz,
    double GainDb,
    double Q)
{
    public const string PeakingType = "peaking";

    public bool IsValid =>
        string.Equals(Type, PeakingType, StringComparison.OrdinalIgnoreCase) &&
        double.IsFinite(FrequencyHz) && FrequencyHz is >= 10.0 and <= 48000.0 &&
        double.IsFinite(GainDb) && GainDb is >= -44.0 and <= 24.0 &&
        double.IsFinite(Q) && Q is >= 0.1 and <= 100.0;
}

public sealed record PeqPresetV1(
    uint SchemaVersion,
    IReadOnlyList<PeqFilterV1> Filters)
{
    public const int MaxFilters = 64;

    public bool IsValid
    {
        get
        {
            if (SchemaVersion != 1U || Filters is null || Filters.Count > MaxFilters)
            {
                return false;
            }

            foreach (var filter in Filters)
            {
                if (filter is null || !filter.IsValid)
                {
                    return false;
                }
            }

            return true;
        }
    }
}

public sealed record CalibrationCompilePolicyV1
{
    public uint SchemaVersion { get; init; } = 1U;
    public uint MaxFilters { get; init; } = 16U;
    public double MinFrequencyHz { get; init; } = 20.0;
    public double MaxFrequencyHz { get; init; } = 20000.0;
    public double MaxBoostDb { get; init; } = 6.0;
    public double MaxCutDb { get; init; } = 12.0;
    public double MinQ { get; init; } = 0.3;
    public double MaxQ { get; init; } = 12.0;
    public double MinSpacingOctaves { get; init; } = 1.0 / 12.0;
    public double IgnoreErrorDb { get; init; } = 0.25;

    public bool IsValid =>
        SchemaVersion == 1U && MaxFilters is > 0U and <= 16U &&
        double.IsFinite(MinFrequencyHz) && MinFrequencyHz >= 10.0 &&
        double.IsFinite(MaxFrequencyHz) && MaxFrequencyHz <= 24000.0 &&
        MinFrequencyHz < MaxFrequencyHz &&
        double.IsFinite(MaxBoostDb) && MaxBoostDb is >= 0.0 and <= 24.0 &&
        double.IsFinite(MaxCutDb) && MaxCutDb is >= 0.0 and <= 44.0 &&
        double.IsFinite(MinQ) && MinQ >= 0.1 &&
        double.IsFinite(MaxQ) && MaxQ <= 100.0 &&
        MinQ <= MaxQ &&
        double.IsFinite(MinSpacingOctaves) && MinSpacingOctaves is >= 0.01 and <= 4.0 &&
        double.IsFinite(IgnoreErrorDb) && IgnoreErrorDb is >= 0.0 and <= 12.0;
}

public sealed record CalibrationCompileResultV1(
    IReadOnlyList<PeqFilterV1> Filters,
    bool Limited,
    double MaximumRequestedCorrectionDb,
    string Diagnostic);

public static class CalibrationCompilerV1
{
    public const int MaxResponsePoints = 512;
    public const int MaxFilters = 16;
    public const double MinQ = 0.1;
    public const double MaxQ = 100.0;
    private const long MaxFileBytes = 1024 * 1024;

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = null
    };

    public static bool ValidatePolicy(CalibrationCompilePolicyV1? policy) =>
        policy is not null && policy.IsValid;

    public static bool ValidateResponse(
        IReadOnlyList<CalibrationPointV1>? response,
        CalibrationCompilePolicyV1? policy)
    {
        if (policy is null || !policy.IsValid || response is null ||
            response.Count < 1 || response.Count > MaxResponsePoints)
        {
            return false;
        }

        var previousFrequency = 0.0;
        foreach (var point in response)
        {
            if (point is null || !point.IsValid ||
                point.FrequencyHz < policy.MinFrequencyHz ||
                point.FrequencyHz > policy.MaxFrequencyHz ||
                (previousFrequency > 0.0 && point.FrequencyHz <= previousFrequency))
            {
                return false;
            }
            previousFrequency = point.FrequencyHz;
        }

        return true;
    }

    public static CalibrationCompileResultV1 CompileBoundedPeqCorrection(
        IReadOnlyList<CalibrationPointV1>? response,
        CalibrationCompilePolicyV1? policy = null)
    {
        var effectivePolicy = policy ?? new CalibrationCompilePolicyV1();
        if (!ValidateResponse(response, effectivePolicy))
        {
            return new CalibrationCompileResultV1(
                Array.Empty<PeqFilterV1>(),
                false,
                0.0,
                "invalid calibration response or compile policy");
        }

        var maxRequestedCorrection = 0.0;
        foreach (var point in response!)
        {
            var correction = CorrectionDb(point);
            maxRequestedCorrection = Math.Max(maxRequestedCorrection, Math.Abs(correction));
        }

        var candidates = new List<int>(response.Count);
        for (var i = 0; i < response.Count; ++i)
        {
            if (Math.Abs(CorrectionDb(response[i])) > effectivePolicy.IgnoreErrorDb)
            {
                candidates.Add(i);
            }
        }

        if (candidates.Count == 0)
        {
            return new CalibrationCompileResultV1(
                Array.Empty<PeqFilterV1>(),
                false,
                maxRequestedCorrection,
                "response is within ignore threshold; no PEQ required");
        }

        var selected = new List<int>((int)effectivePolicy.MaxFilters);
        while (selected.Count < effectivePolicy.MaxFilters && selected.Count < candidates.Count)
        {
            var best = -1;
            var bestError = -1.0;
            foreach (var candidate in candidates)
            {
                if (selected.Contains(candidate) ||
                    CloseToSelected(response, selected, candidate, effectivePolicy.MinSpacingOctaves))
                {
                    continue;
                }

                var error = Math.Abs(CorrectionDb(response[candidate]));
                if (error > bestError)
                {
                    bestError = error;
                    best = candidate;
                }
            }

            if (best < 0) break;
            selected.Add(best);
        }

        var limited = selected.Count < candidates.Count;
        selected.Sort();

        var filters = new List<PeqFilterV1>(selected.Count);
        foreach (var index in selected)
        {
            var requested = CorrectionDb(response[index]);
            var gain = Math.Clamp(requested, -effectivePolicy.MaxCutDb, effectivePolicy.MaxBoostDb);
            if (Math.Abs(gain - requested) > 1e-12)
            {
                limited = true;
            }

            var q = QForPoint(response, index, effectivePolicy);
            filters.Add(new PeqFilterV1(PeqFilterV1.PeakingType, response[index].FrequencyHz, gain, q));
        }

        var diagnostic = limited
            ? "bounded PEQ compiled; review clipped or unrepresented residuals"
            : "bounded PEQ compiled; verify with a second measurement";

        return new CalibrationCompileResultV1(filters, limited, maxRequestedCorrection, diagnostic);
    }

    public static string ExportEqualizerApo(IReadOnlyList<PeqFilterV1>? filters)
    {
        var sb = new StringBuilder();
        sb.AppendLine("# Hibiki DSP PEQ export (input coefficients supplied by user)");
        sb.AppendLine("Preamp: -1 dB");
        if (filters is null) return sb.ToString();

        var index = 1;
        foreach (var filter in filters)
        {
            if (filter is not null && filter.IsValid)
            {
                sb.AppendLine(CultureInfo.InvariantCulture,
                    $"Filter {index++}: ON PK Fc {filter.FrequencyHz:F3} Hz Gain {filter.GainDb:F3} dB Q {filter.Q:F3}");
            }
        }
        return sb.ToString();
    }

    public static string ExportCamillaDspYaml(IReadOnlyList<PeqFilterV1>? filters)
    {
        var sb = new StringBuilder();
        sb.AppendLine("# Hibiki DSP PEQ export");
        sb.AppendLine("filters:");
        if (filters is null) return sb.ToString();

        foreach (var filter in filters)
        {
            if (filter is not null && filter.IsValid)
            {
                sb.AppendLine(CultureInfo.InvariantCulture,
                    $"  - type: Peaking\n    freq: {filter.FrequencyHz:F3}\n    gain: {filter.GainDb:F3}\n    q: {filter.Q:F3}");
            }
        }
        return sb.ToString();
    }

    public static string ExportRewFilterList(IReadOnlyList<PeqFilterV1>? filters)
    {
        var sb = new StringBuilder();
        sb.AppendLine("Filter\tType\tFreq (Hz)\tGain (dB)\tQ");
        if (filters is null) return sb.ToString();

        var index = 1;
        foreach (var filter in filters)
        {
            if (filter is not null && filter.IsValid)
            {
                sb.AppendLine(CultureInfo.InvariantCulture,
                    $"{index++}\tPK\t{filter.FrequencyHz:F3}\t{filter.GainDb:F3}\t{filter.Q:F3}");
            }
        }
        return sb.ToString();
    }

    public static string ExportHibikiProfile(IReadOnlyList<PeqFilterV1>? filters)
    {
        var sb = new StringBuilder();
        sb.AppendLine("{\n  \"schema_version\": 1,\n  \"filters\": [");
        if (filters is not null)
        {
            var first = true;
            foreach (var filter in filters)
            {
                if (filter is null || !filter.IsValid) continue;
                if (!first) sb.AppendLine(",");
                first = false;
                sb.Append(CultureInfo.InvariantCulture,
                    $"    {{\"type\": \"peaking\", \"frequency_hz\": {filter.FrequencyHz:F3}, \"gain_db\": {filter.GainDb:F3}, \"q\": {filter.Q:F3}}}");
            }
        }
        sb.AppendLine("\n  ]\n}");
        return sb.ToString();
    }

    public static bool TrySaveResponse(string filePath, CalibrationResponseV1 response, out string error)
    {
        error = string.Empty;
        if (string.IsNullOrWhiteSpace(filePath))
        {
            error = "校正頻響路徑不可為空";
            return false;
        }

        if (response is null || !response.IsValid)
        {
            error = "校正頻響資料無效";
            return false;
        }

        var temporaryPath = string.Empty;
        try
        {
            var fullPath = Path.GetFullPath(filePath);
            var directory = Path.GetDirectoryName(fullPath);
            if (string.IsNullOrWhiteSpace(directory))
            {
                error = "校正頻響路徑缺少資料夾";
                return false;
            }
            Directory.CreateDirectory(directory);

            var doc = new CalibrationResponseDocument
            {
                SchemaVersion = (int)response.SchemaVersion,
                SampleRate = response.SampleRate,
                Channels = response.Channels,
                DeviceId = response.DeviceId,
                Points = response.Points.Select(p => (CalibrationPointDocument?)new CalibrationPointDocument
                {
                    FrequencyHz = p.FrequencyHz,
                    MeasuredDb = p.MeasuredDb,
                    TargetDb = p.TargetDb
                }).ToList()
            };

            var json = JsonSerializer.Serialize(doc, JsonOptions);
            if (Encoding.UTF8.GetByteCount(json) > MaxFileBytes)
            {
                error = "校正頻響檔案超過大小上限";
                return false;
            }

            temporaryPath = Path.Combine(directory, $".{Path.GetFileName(fullPath)}.{Guid.NewGuid():N}.tmp");
            File.WriteAllText(temporaryPath, json, new UTF8Encoding(false));
            File.Move(temporaryPath, fullPath, true);
            return true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
        {
            error = "校正頻響儲存失敗";
            return false;
        }
        finally
        {
            if (!string.IsNullOrEmpty(temporaryPath))
            {
                try { if (File.Exists(temporaryPath)) File.Delete(temporaryPath); }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
            }
        }
    }

    public static bool TryLoadResponse(string filePath, out CalibrationResponseV1? response, out string error)
    {
        response = null;
        error = string.Empty;
        if (string.IsNullOrWhiteSpace(filePath))
        {
            error = "校正頻響路徑不可為空";
            return false;
        }

        try
        {
            var fullPath = Path.GetFullPath(filePath);
            var info = new FileInfo(fullPath);
            if (!info.Exists || info.Length < 1 || info.Length > MaxFileBytes)
            {
                error = "校正頻響檔案不存在或超過大小上限";
                return false;
            }

            var doc = JsonSerializer.Deserialize<CalibrationResponseDocument>(
                File.ReadAllText(fullPath, Encoding.UTF8));

            if (doc is null || doc.SchemaVersion != 1 ||
                doc.SampleRate is < 8000.0 or > 384000.0 ||
                doc.Channels is not (1 or 2 or 6 or 8) ||
                doc.Points is null || doc.Points.Count is < 1 or > MaxResponsePoints)
            {
                error = "校正頻響 schema 版本或內容無效";
                return false;
            }

            var points = new List<CalibrationPointV1>(doc.Points.Count);
            var previousFrequency = 0.0;
            foreach (var p in doc.Points)
            {
                if (p is null || !double.IsFinite(p.FrequencyHz) || p.FrequencyHz is <= 0.0 or > 24000.0 ||
                    !double.IsFinite(p.MeasuredDb) || !double.IsFinite(p.TargetDb) ||
                    p.FrequencyHz <= previousFrequency)
                {
                    error = "校正頻響含有無效或未遞增之頻率點";
                    return false;
                }
                points.Add(new CalibrationPointV1(p.FrequencyHz, p.MeasuredDb, p.TargetDb));
                previousFrequency = p.FrequencyHz;
            }

            var result = new CalibrationResponseV1(
                (uint)doc.SchemaVersion,
                doc.SampleRate,
                doc.Channels,
                doc.DeviceId,
                points);

            if (!result.IsValid)
            {
                error = "校正頻響驗證失敗";
                return false;
            }

            response = result;
            return true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException or NotSupportedException or ArgumentException)
        {
            error = "校正頻響載入失敗";
            return false;
        }
    }

    public static bool TrySavePreset(string filePath, PeqPresetV1 preset, out string error)
    {
        error = string.Empty;
        if (string.IsNullOrWhiteSpace(filePath))
        {
            error = "PEQ 預設路徑不可為空";
            return false;
        }

        if (preset is null || !preset.IsValid)
        {
            error = "PEQ 預設資料無效";
            return false;
        }

        var temporaryPath = string.Empty;
        try
        {
            var fullPath = Path.GetFullPath(filePath);
            var directory = Path.GetDirectoryName(fullPath);
            if (string.IsNullOrWhiteSpace(directory))
            {
                error = "PEQ 預設路徑缺少資料夾";
                return false;
            }
            Directory.CreateDirectory(directory);

            var doc = new PeqPresetDocument
            {
                SchemaVersion = (int)preset.SchemaVersion,
                Filters = preset.Filters.Select(f => (PeqFilterDocument?)new PeqFilterDocument
                {
                    Type = f.Type,
                    FrequencyHz = f.FrequencyHz,
                    GainDb = f.GainDb,
                    Q = f.Q
                }).ToList()
            };

            var json = JsonSerializer.Serialize(doc, JsonOptions);
            if (Encoding.UTF8.GetByteCount(json) > MaxFileBytes)
            {
                error = "PEQ 預設檔案超過大小上限";
                return false;
            }

            temporaryPath = Path.Combine(directory, $".{Path.GetFileName(fullPath)}.{Guid.NewGuid():N}.tmp");
            File.WriteAllText(temporaryPath, json, new UTF8Encoding(false));
            File.Move(temporaryPath, fullPath, true);
            return true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
        {
            error = "PEQ 預設儲存失敗";
            return false;
        }
        finally
        {
            if (!string.IsNullOrEmpty(temporaryPath))
            {
                try { if (File.Exists(temporaryPath)) File.Delete(temporaryPath); }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
            }
        }
    }

    public static bool TryLoadPreset(string filePath, out PeqPresetV1? preset, out string error)
    {
        preset = null;
        error = string.Empty;
        if (string.IsNullOrWhiteSpace(filePath))
        {
            error = "PEQ 預設路徑不可為空";
            return false;
        }

        try
        {
            var fullPath = Path.GetFullPath(filePath);
            var info = new FileInfo(fullPath);
            if (!info.Exists || info.Length < 1 || info.Length > MaxFileBytes)
            {
                error = "PEQ 預設檔案不存在或超過大小上限";
                return false;
            }

            var doc = JsonSerializer.Deserialize<PeqPresetDocument>(
                File.ReadAllText(fullPath, Encoding.UTF8));

            if (doc is null || doc.SchemaVersion != 1 ||
                doc.Filters is null || doc.Filters.Count > PeqPresetV1.MaxFilters)
            {
                error = "PEQ 預設 schema 版本或容量無效";
                return false;
            }

            var filters = new List<PeqFilterV1>(doc.Filters.Count);
            foreach (var f in doc.Filters)
            {
                if (f is null || !string.Equals(f.Type, PeqFilterV1.PeakingType, StringComparison.OrdinalIgnoreCase) ||
                    !double.IsFinite(f.FrequencyHz) || f.FrequencyHz is < 10.0 or > 48000.0 ||
                    !double.IsFinite(f.GainDb) || f.GainDb is < -44.0 or > 24.0 ||
                    !double.IsFinite(f.Q) || f.Q is < 0.1 or > 100.0)
                {
                    error = "PEQ 預設含有無效濾波器參數";
                    return false;
                }
                filters.Add(new PeqFilterV1(PeqFilterV1.PeakingType, f.FrequencyHz, f.GainDb, f.Q));
            }

            var result = new PeqPresetV1((uint)doc.SchemaVersion, filters);
            if (!result.IsValid)
            {
                error = "PEQ 預設驗證失敗";
                return false;
            }

            preset = result;
            return true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException or NotSupportedException or ArgumentException)
        {
            error = "PEQ 預設載入失敗";
            return false;
        }
    }

    private static double CorrectionDb(CalibrationPointV1 point) =>
        point.TargetDb - point.MeasuredDb;

    private static double DistanceOctaves(double leftHz, double rightHz) =>
        Math.Abs(Math.Log2(leftHz / rightHz));

    private static double QForPoint(
        IReadOnlyList<CalibrationPointV1> response,
        int index,
        CalibrationCompilePolicyV1 policy)
    {
        var point = response[index];
        var lower = index == 0
            ? DistanceOctaves(response[Math.Min(index + 1, response.Count - 1)].FrequencyHz, point.FrequencyHz)
            : DistanceOctaves(point.FrequencyHz, response[index - 1].FrequencyHz);
        var upper = index + 1 >= response.Count
            ? lower
            : DistanceOctaves(response[index + 1].FrequencyHz, point.FrequencyHz);
        var bandwidth = Math.Clamp((lower + upper) * 0.5, 0.125, 2.0);
        var q = 1.0 / (2.0 * Math.Sinh(Math.Log(2.0) * bandwidth * 0.5));
        return Math.Clamp(q, policy.MinQ, policy.MaxQ);
    }

    private static bool CloseToSelected(
        IReadOnlyList<CalibrationPointV1> response,
        List<int> selected,
        int candidate,
        double spacing)
    {
        foreach (var index in selected)
        {
            if (DistanceOctaves(response[index].FrequencyHz, response[candidate].FrequencyHz) < spacing)
            {
                return true;
            }
        }
        return false;
    }

    private sealed class CalibrationResponseDocument
    {
        [JsonPropertyName("schema_version")]
        public int SchemaVersion { get; set; }

        [JsonPropertyName("sample_rate")]
        public double SampleRate { get; set; }

        [JsonPropertyName("channels")]
        public int Channels { get; set; }

        [JsonPropertyName("device_id")]
        public string? DeviceId { get; set; }

        [JsonPropertyName("points")]
        public List<CalibrationPointDocument?>? Points { get; set; }
    }

    private sealed class CalibrationPointDocument
    {
        [JsonPropertyName("frequency_hz")]
        public double FrequencyHz { get; set; }

        [JsonPropertyName("measured_db")]
        public double MeasuredDb { get; set; }

        [JsonPropertyName("target_db")]
        public double TargetDb { get; set; }
    }

    private sealed class PeqPresetDocument
    {
        [JsonPropertyName("schema_version")]
        public int SchemaVersion { get; set; }

        [JsonPropertyName("filters")]
        public List<PeqFilterDocument?>? Filters { get; set; }
    }

    private sealed class PeqFilterDocument
    {
        [JsonPropertyName("type")]
        public string? Type { get; set; }

        [JsonPropertyName("frequency_hz")]
        public double FrequencyHz { get; set; }

        [JsonPropertyName("gain_db")]
        public double GainDb { get; set; }

        [JsonPropertyName("q")]
        public double Q { get; set; }
    }
}
