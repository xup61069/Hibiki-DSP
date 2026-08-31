// SPDX-License-Identifier: GPL-3.0-only

using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Hibiki.ControlModel;

// User-facing Expert presets. This catalog deliberately stores only bounded
// matching/routing metadata; the engine remains authoritative for the active
// session registry and graph transaction.
public sealed record SessionRouteRuleCard(
    string RuleId,
    int Priority,
    bool Enabled,
    SessionRouteRuleGainOwnerV1 GainOwner,
    double MakeupGainDb,
    string AppId,
    string DisplayName,
    string LaneId,
    string OutputGroup)
{
    public string GainOwnerLabel => GainOwner switch
    {
        SessionRouteRuleGainOwnerV1.WindowsSession => "Windows 工作階段",
        SessionRouteRuleGainOwnerV1.HibikiInternal => "Hibiki 內部",
        _ => "未知"
    };

    public string MatchSummary => string.Join("／",
        new[] { AppId, DisplayName }.Where(value => !string.IsNullOrWhiteSpace(value)));

    public string Summary =>
        $"{(Enabled ? "啟用" : "停用")}｜優先 {Priority}｜{MatchSummary} → " +
        $"{LaneId}／{OutputGroup}｜{MakeupGainDb:0.0} dB（{GainOwnerLabel}）";
}

public enum SessionRouteRuleResolutionV1
{
    NoMatch,
    Applied,
    Ambiguous
}

public sealed class SessionRouteRuleCatalogV1
{
    public const int MaxRules = 64;
    public const int SchemaVersion = 1;
    private const long MaxFileBytes = 1024 * 1024;
    private const int MaxRuleIdBytes = 64;
    private const int MaxMatcherBytes = 128;
    private const int MaxRouteBytes = 64;
    private readonly List<SessionRouteRuleCard> _rules = new(MaxRules);
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = null,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow
    };

    public IReadOnlyList<SessionRouteRuleCard> Rules => _rules;
    public int Count => _rules.Count;

    public bool Upsert(SessionRouteRuleCard? rule)
    {
        if (rule is null || !IsValid(rule)) return false;
        var index = _rules.FindIndex(item => item.RuleId == rule.RuleId);
        if (index >= 0)
        {
            _rules[index] = rule;
            SortRules();
            return true;
        }
        if (_rules.Count >= MaxRules) return false;
        _rules.Add(rule);
        SortRules();
        return true;
    }

    public bool Remove(string? ruleId)
    {
        if (string.IsNullOrWhiteSpace(ruleId)) return false;
        var index = _rules.FindIndex(item => item.RuleId == ruleId.Trim());
        if (index < 0) return false;
        _rules.RemoveAt(index);
        return true;
    }

    public void Clear() => _rules.Clear();

    // Mirrors the engine's control-plane resolver for a safe UI preview. A
    // matching App ID is exact (case-insensitive); a display matcher is a
    // case-insensitive substring. Multiple equal-priority matches are
    // deliberately ambiguous instead of depending on catalog order.
    public SessionRouteRuleResolutionV1 TryResolve(
        SessionCatalogEntryV1 session, out SessionRouteRuleCard? selected)
    {
        selected = null;
        foreach (var rule in _rules)
        {
            if (!rule.Enabled || !Matches(rule, session)) continue;
            if (selected is null || rule.Priority > selected.Priority)
            {
                selected = rule;
                continue;
            }
            if (rule.Priority == selected.Priority)
            {
                selected = null;
                return SessionRouteRuleResolutionV1.Ambiguous;
            }
        }
        return selected is null
            ? SessionRouteRuleResolutionV1.NoMatch
            : SessionRouteRuleResolutionV1.Applied;
    }

    public bool TrySave(string filePath, out string error)
    {
        error = string.Empty;
        if (string.IsNullOrWhiteSpace(filePath))
        {
            error = "路由預設路徑不可為空";
            return false;
        }

        var temporaryPath = string.Empty;
        try
        {
            var fullPath = Path.GetFullPath(filePath);
            var directory = Path.GetDirectoryName(fullPath);
            if (string.IsNullOrWhiteSpace(directory))
            {
                error = "路由預設路徑缺少資料夾";
                return false;
            }
            Directory.CreateDirectory(directory);
            var document = new CatalogDocument
            {
                SchemaVersion = SchemaVersion,
                Rules = _rules.Select(item => (CatalogRule?)ToDocument(item)).ToList()
            };
            var json = JsonSerializer.Serialize(document, JsonOptions);
            if (Encoding.UTF8.GetByteCount(json) > MaxFileBytes)
            {
                error = "路由預設檔案超過大小上限";
                return false;
            }
            temporaryPath = Path.Combine(directory,
                $".{Path.GetFileName(fullPath)}.{Guid.NewGuid():N}.tmp");
            File.WriteAllText(temporaryPath, json, new UTF8Encoding(false));
            File.Move(temporaryPath, fullPath, true);
            return true;
        }
        catch (Exception exception) when (exception is IOException or
                                          UnauthorizedAccessException or
                                          ArgumentException or NotSupportedException)
        {
            error = "路由預設儲存失敗";
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

    // A complete candidate is validated before replacing the live list.
    public bool TryLoad(string filePath, out string error)
    {
        error = string.Empty;
        if (string.IsNullOrWhiteSpace(filePath))
        {
            error = "路由預設路徑不可為空";
            return false;
        }
        try
        {
            var fullPath = Path.GetFullPath(filePath);
            var info = new FileInfo(fullPath);
            if (!info.Exists || info.Length < 1 || info.Length > MaxFileBytes)
            {
                error = "路由預設檔案不存在或超過大小上限";
                return false;
            }
            var document = JsonSerializer.Deserialize<CatalogDocument>(
                File.ReadAllText(fullPath, Encoding.UTF8), JsonOptions);
            if (document is null || document.SchemaVersion != SchemaVersion ||
                document.Rules is null || document.Rules.Count > MaxRules)
            {
                error = "路由預設 schema 版本或容量無效";
                return false;
            }

            var candidate = new List<SessionRouteRuleCard>(MaxRules);
            var seen = new HashSet<string>(StringComparer.Ordinal);
            foreach (var item in document.Rules)
            {
                if (item is null)
                {
                    error = "路由預設含有空規則";
                    return false;
                }
                var rule = new SessionRouteRuleCard(
                    item.RuleId ?? string.Empty, item.Priority, item.Enabled,
                    item.GainOwner, item.MakeupGainDb, item.AppId ?? string.Empty,
                    item.DisplayName ?? string.Empty, item.LaneId ?? string.Empty,
                    item.OutputGroup ?? string.Empty);
                if (!IsValid(rule) || !seen.Add(rule.RuleId))
                {
                    error = "路由預設含有無效或重複規則";
                    return false;
                }
                candidate.Add(rule);
            }
            candidate.Sort(CompareRules);
            _rules.Clear();
            _rules.AddRange(candidate);
            return true;
        }
        catch (Exception exception) when (exception is IOException or
                                          UnauthorizedAccessException or JsonException or
                                          NotSupportedException or ArgumentException)
        {
            error = "路由預設載入失敗";
            return false;
        }
    }

    // Serializes the current rules using the same bounded document format as
    // TrySave so an export stays byte-compatible with the on-disk catalog.
    public string ToJsonForImportExport()
    {
        var document = new CatalogDocument
        {
            SchemaVersion = SchemaVersion,
            Rules = _rules.Select(item => (CatalogRule?)ToDocument(item)).ToList()
        };
        return JsonSerializer.Serialize(document, JsonOptions);
    }

    private static CatalogRule ToDocument(SessionRouteRuleCard rule) => new()
    {
        RuleId = rule.RuleId,
        Priority = rule.Priority,
        Enabled = rule.Enabled,
        GainOwner = rule.GainOwner,
        MakeupGainDb = rule.MakeupGainDb,
        AppId = rule.AppId,
        DisplayName = rule.DisplayName,
        LaneId = rule.LaneId,
        OutputGroup = rule.OutputGroup
    };

    private void SortRules() => _rules.Sort(CompareRules);

    private static int CompareRules(SessionRouteRuleCard left, SessionRouteRuleCard right)
    {
        var priority = right.Priority.CompareTo(left.Priority);
        return priority != 0 ? priority : string.CompareOrdinal(left.RuleId, right.RuleId);
    }

    private static bool IsValid(SessionRouteRuleCard rule)
    {
        if (!Bounded(rule.RuleId, MaxRuleIdBytes) ||
            !BoundedOptional(rule.AppId, MaxMatcherBytes) ||
            !BoundedOptional(rule.DisplayName, MaxMatcherBytes) ||
            !Bounded(rule.LaneId, MaxRouteBytes) ||
            !Bounded(rule.OutputGroup, MaxRouteBytes) ||
            (string.IsNullOrWhiteSpace(rule.AppId) &&
             string.IsNullOrWhiteSpace(rule.DisplayName)) ||
            rule.Priority is < -1_000_000 or > 1_000_000 ||
            !double.IsFinite(rule.MakeupGainDb) || rule.MakeupGainDb is < -144.0 or > 12.0 ||
            !Enum.IsDefined(rule.GainOwner))
            return false;

        for (var index = 0; index < rule.RuleId.Length; index++)
        {
            var value = rule.RuleId[index];
            var lower = value is >= 'a' and <= 'z';
            var digit = value is >= '0' and <= '9';
            var separator = value is '.' or '_' or '-';
            if ((!lower && !digit && !separator) ||
                (index == 0 && !lower && !digit)) return false;
        }
        return true;
    }

    private static bool Matches(SessionRouteRuleCard rule, SessionCatalogEntryV1 session)
    {
        if (!string.IsNullOrWhiteSpace(rule.AppId) &&
            !string.Equals(rule.AppId, session.AppId, StringComparison.OrdinalIgnoreCase))
            return false;
        if (!string.IsNullOrWhiteSpace(rule.DisplayName) &&
            (string.IsNullOrEmpty(session.Name) ||
             session.Name.IndexOf(rule.DisplayName, StringComparison.OrdinalIgnoreCase) < 0))
            return false;
        return true;
    }

    private static bool Bounded(string value, int maxBytes) =>
        Utf8TextValidation.IsPrintable(value, maxBytes, allowEmpty: false);

    private static bool BoundedOptional(string value, int maxBytes) =>
        Utf8TextValidation.IsPrintable(value, maxBytes);

    private sealed class CatalogDocument
    {
        [JsonPropertyName("schema_version")]
        public int SchemaVersion { get; set; }

        [JsonPropertyName("rules")]
        public List<CatalogRule?>? Rules { get; set; }
    }

    private sealed class CatalogRule
    {
        [JsonPropertyName("rule_id")]
        public string? RuleId { get; set; }

        [JsonPropertyName("priority")]
        public int Priority { get; set; }

        [JsonPropertyName("enabled")]
        public bool Enabled { get; set; }

        [JsonPropertyName("gain_owner")]
        public SessionRouteRuleGainOwnerV1 GainOwner { get; set; }

        [JsonPropertyName("makeup_gain_db")]
        public double MakeupGainDb { get; set; }

        [JsonPropertyName("app_id")]
        public string? AppId { get; set; }

        [JsonPropertyName("display_name")]
        public string? DisplayName { get; set; }

        [JsonPropertyName("lane_id")]
        public string? LaneId { get; set; }

        [JsonPropertyName("output_group")]
        public string? OutputGroup { get; set; }
    }
}
