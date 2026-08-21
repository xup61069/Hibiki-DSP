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

public enum ControlConnectionState
{
    Disconnected,
    Connecting,
    Connected,
    Degraded
}

public sealed record OutputGroupCard(
    string Id,
    string Name,
    int Channels,
    bool IsAvailable = true);

public static class OutputGroupCatalog
{
    public static IReadOnlyList<OutputGroupCard> Fixed { get; } =
    [
        new("main", "Main", 2),
        new("low-latency", "Low Latency", 2),
        new("surround", "Surround 7.1", 8)
    ];
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

public sealed record EnhanceResult(
    bool Succeeded,
    SceneCard? Scene,
    AudioControlStatus Status,
    string? Message);

// UI-independent behavior contract for the first-run experience. WinUI may
// render this model, but it must not silently claim control when no output
// group is selected.
public sealed class EasyControlSession
{
    public CustomSceneCatalogV1 CustomScenes { get; } = new();
    public UiMode Mode { get; private set; } = UiMode.Easy;
    public SceneCard? ActiveScene { get; private set; }
    public string? ActiveOutputGroup { get; private set; }
    public AudioControlStatus Status { get; private set; } = AudioControlStatus.Degraded;

    public IReadOnlyList<SceneCard> Scenes =>
        ScenePresetCatalog.EasyDefaults.Concat(CustomScenes.Scenes).ToArray();

    public void SetMode(UiMode mode) => Mode = mode;

    public EnhanceResult OneTapEnhance(string? outputGroup)
    {
        if (string.IsNullOrWhiteSpace(outputGroup))
        {
            Status = AudioControlStatus.Degraded;
            return new(false, null, Status, "尚未選擇輸出裝置");
        }
        ActiveOutputGroup = outputGroup.Trim();
        ActiveScene = ScenePresetCatalog.EasyDefaults[0];
        Status = AudioControlStatus.Controlled;
        return new(true, ActiveScene, Status, "已套用遊戲低延遲與音量保護");
    }

    public bool SelectScene(string sceneId)
    {
        var scene = Scenes.FirstOrDefault(item => item.Id == sceneId);
        if (scene is null) return false;
        ActiveScene = scene;
        return true;
    }

    public void MarkDegraded() => Status = AudioControlStatus.Degraded;
}

public sealed class DeviceSwitchModel
{
    public enum SwitchState
    {
        Unbound,
        Preparing,
        Fading,
        ReadyToCommit,
        Synced,
        RolledBack,
        Degraded
    }

    private string? _active;
    private string? _prepared;

    public string? ActiveDevice => _active;
    public string? PreparedDevice => _prepared;
    public SwitchState State { get; private set; } = SwitchState.Unbound;
    public bool CanCommit => State == SwitchState.ReadyToCommit && _prepared is not null;

    public bool Prepare(string deviceId)
    {
        if (string.IsNullOrWhiteSpace(deviceId) ||
            State is SwitchState.Preparing or SwitchState.Fading or SwitchState.ReadyToCommit)
            return false;
        _prepared = deviceId;
        State = SwitchState.Preparing;
        return true;
    }

    public bool MarkPrepared()
    {
        if (State != SwitchState.Preparing || _prepared is null) return false;
        State = SwitchState.Fading;
        return true;
    }

    public bool MarkCrossfadeComplete()
    {
        if (State != SwitchState.Fading || _prepared is null) return false;
        State = SwitchState.ReadyToCommit;
        return true;
    }

    public bool Commit()
    {
        if (!CanCommit) return false;
        _active = _prepared;
        _prepared = null;
        State = SwitchState.Synced;
        return true;
    }

    public void Rollback()
    {
        _prepared = null;
        State = _active is null ? SwitchState.Unbound : SwitchState.RolledBack;
    }
}
