namespace Hibiki.ControlModel;

public enum UiMode
{
    Easy,
    Expert
}

public enum AudioControlStatus
{
    Controlled,
    Bypassed,
    Degraded
}

public sealed record SceneCard(
    string Id,
    string Name,
    string Description,
    string LatencyLabel,
    bool SafetyEnabled);

public static class ScenePresetCatalog
{
    public static IReadOnlyList<SceneCard> EasyDefaults { get; } =
    [
        new("game", "遊戲", "快速反應、低延遲與智慧保護", "零額外緩衝", true),
        new("movie", "電影", "完整校正、對白清楚與安全音量", "影音同步補償", true),
        new("voice", "人聲", "降低風聲與背景干擾", "平衡", true),
        new("studio", "Studio / Direct", "維持原始訊號，不加入處理", "Strict Direct", false)
    ];
}

public sealed record ControlSnapshot(
    UiMode Mode,
    AudioControlStatus Status,
    string OutputGroup,
    double RequestedDb,
    double EffectiveDb,
    bool Muted,
    string? BypassReason,
    string? DegradedReason)
{
    public string DisplayVolume => Muted ? "靜音" : $"{EffectiveDb:0.0} dB";
}

public sealed class DeviceSwitchModel
{
    private string? _active;
    private string? _prepared;

    public string? ActiveDevice => _active;

    public bool Prepare(string deviceId)
    {
        if (string.IsNullOrWhiteSpace(deviceId)) return false;
        _prepared = deviceId;
        return true;
    }

    public bool Commit()
    {
        if (_prepared is null) return false;
        _active = _prepared;
        _prepared = null;
        return true;
    }

    public void Rollback() => _prepared = null;
}
