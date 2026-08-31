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

public enum PhysicalDeviceFlowV1
{
    Render,
    Capture
}

public enum PhysicalDeviceAvailabilityV1
{
    Active,
    Disabled,
    Unplugged,
    Unknown
}

public sealed record PhysicalDeviceCard(
    string EndpointId,
    string DisplayName,
    PhysicalDeviceFlowV1 Flow,
    PhysicalDeviceAvailabilityV1 Availability,
    int Channels,
    int SampleRate,
    int BufferFrames,
    bool IsDefault,
    ulong LastSequence)
{
    public bool IsSelectable => Flow == PhysicalDeviceFlowV1.Render &&
                                Availability == PhysicalDeviceAvailabilityV1.Active &&
                                Channels is 1 or 2 or 6 or 8;
}

// UI/control-plane mirror of the C++ catalog. It is populated by a future
// endpoint metadata snapshot; it never invents a local device or claims that
// a switch reached WASAPI until the engine acknowledges the command.
public sealed class PhysicalDeviceCatalogV1
{
    public const int Capacity = 32;
    private readonly List<PhysicalDeviceCard> _devices = new(Capacity);
    private ulong _catalogSequence;

    public IReadOnlyList<PhysicalDeviceCard> Devices => _devices;
    public ulong CatalogSequence => _catalogSequence;
    public PhysicalDeviceCard? DefaultRender => _devices.FirstOrDefault(device =>
        device.Flow == PhysicalDeviceFlowV1.Render && device.IsDefault && device.IsSelectable);

    public bool TryGet(string endpointId, out PhysicalDeviceCard? device)
    {
        device = _devices.FirstOrDefault(item => item.EndpointId == endpointId);
        return device is not null;
    }

    public bool Upsert(PhysicalDeviceCard device, out string error)
    {
        error = string.Empty;
        if (!Validate(device, out error)) return false;
        var index = _devices.FindIndex(item => item.EndpointId == device.EndpointId);
        if (index >= 0)
        {
            if (device.LastSequence != 0 && device.LastSequence < _devices[index].LastSequence)
            {
                error = "裝置事件已過期";
                return false;
            }
            _devices[index] = device;
        }
        else
        {
            if (_devices.Count >= Capacity)
            {
                error = "裝置目錄已滿";
                return false;
            }
            _devices.Add(device);
        }
        _catalogSequence = Math.Max(_catalogSequence, device.LastSequence);
        if (device.IsDefault)
            ClearDefaults(device.Flow, device.EndpointId);
        return true;
    }

    public bool ReplaceSnapshot(IReadOnlyList<PhysicalDeviceCard> devices,
                                ulong catalogSequence,
                                out string error)
    {
        error = string.Empty;
        if (devices is null || devices.Count > Capacity)
        { error = "裝置快照超過容量"; return false; }
        if (catalogSequence != 0 && catalogSequence < _catalogSequence)
        { error = "裝置快照已過期"; return false; }
        var replacement = new List<PhysicalDeviceCard>(devices.Count);
        var seen = new HashSet<string>(StringComparer.Ordinal);
        var defaults = new HashSet<PhysicalDeviceFlowV1>();
        foreach (var device in devices)
        {
            if (!Validate(device, out error) || !seen.Add(device.EndpointId))
            {
                if (string.IsNullOrEmpty(error)) error = "裝置快照含有重複身份";
                return false;
            }
            if (device.IsDefault && !defaults.Add(device.Flow))
            { error = "同一方向只能有一個預設裝置"; return false; }
            replacement.Add(device);
        }
        _devices.Clear();
        _devices.AddRange(replacement);
        _catalogSequence = Math.Max(_catalogSequence, catalogSequence);
        return true;
    }

    public bool SetAvailability(string endpointId,
                                PhysicalDeviceAvailabilityV1 availability,
                                ulong sequence,
                                out string error)
    {
        error = string.Empty;
        if (!Enum.IsDefined(availability))
        {
            error = "裝置狀態不受支援";
            return false;
        }
        var index = _devices.FindIndex(item => item.EndpointId == endpointId);
        if (index < 0) { error = "找不到裝置"; return false; }
        var current = _devices[index];
        if (sequence != 0 && sequence < current.LastSequence)
        {
            error = "裝置事件已過期";
            return false;
        }
        _devices[index] = current with
        {
            Availability = availability,
            IsDefault = availability == PhysicalDeviceAvailabilityV1.Active && current.IsDefault,
            LastSequence = Math.Max(current.LastSequence, sequence)
        };
        if (availability != PhysicalDeviceAvailabilityV1.Active)
            _devices[index] = _devices[index] with { IsDefault = false };
        _catalogSequence = Math.Max(_catalogSequence, sequence);
        return true;
    }

    public bool MarkDefault(string endpointId, ulong sequence, out string error)
    {
        error = string.Empty;
        if (!TryGet(endpointId, out var device) || device is null)
        { error = "找不到裝置"; return false; }
        if (!device.IsSelectable)
        { error = "裝置目前不可選取"; return false; }
        if (sequence != 0 && sequence < device.LastSequence)
        { error = "裝置事件已過期"; return false; }
        ClearDefaults(device.Flow, endpointId);
        var index = _devices.FindIndex(item => item.EndpointId == endpointId);
        _devices[index] = device with { IsDefault = true, LastSequence = Math.Max(device.LastSequence, sequence) };
        _catalogSequence = Math.Max(_catalogSequence, sequence);
        return true;
    }

    public bool Remove(string endpointId) =>
        _devices.RemoveAll(item => item.EndpointId == endpointId) != 0;

    private void ClearDefaults(PhysicalDeviceFlowV1 flow, string exceptEndpointId)
    {
        for (var index = 0; index < _devices.Count; index++)
        {
            var item = _devices[index];
            if (item.Flow == flow && item.EndpointId != exceptEndpointId && item.IsDefault)
                _devices[index] = item with { IsDefault = false };
        }
    }

    private static bool Validate(PhysicalDeviceCard device, out string error)
    {
        error = string.Empty;
        static bool Printable(string value, int maxBytes) =>
            !string.IsNullOrWhiteSpace(value) &&
            Utf8TextValidation.IsPrintable(value, maxBytes, allowEmpty: false);
        if (!Printable(device.EndpointId, 260) || !Printable(device.DisplayName, 128))
        { error = "裝置身份或名稱無效"; return false; }
        if (!Enum.IsDefined(device.Flow) || !Enum.IsDefined(device.Availability) ||
            device.Channels is not (1 or 2 or 6 or 8) ||
            device.SampleRate is not (44100 or 48000 or 96000 or 192000) ||
            device.BufferFrames is < 16 or > 4096)
        { error = "裝置格式不受支援"; return false; }
        if (device.IsDefault && device.Availability != PhysicalDeviceAvailabilityV1.Active)
        { error = "只有 Active 裝置可以是預設"; return false; }
        return true;
    }
}

public sealed record SceneCard(
    string Id,
    string Name,
    string Description,
    string LatencyLabel,
    bool SafetyEnabled,
    string IrReference = "",
    bool LoudnessLiveUpdate = false);

public static class ScenePresetCatalog
{
    public static IReadOnlyList<SceneCard> EasyDefaults { get; } =
    [
        new("game", "遊戲", "快速反應、低延遲與智慧保護", "零額外緩衝", true),
        new("movie", "電影", "等響度 EQ、內容驅動低頻自適應與安全音量", "影音同步補償", true),
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
    public SessionRouteRuleCatalogV1 RouteRules { get; } = new();
    public PhysicalDeviceCatalogV1 PhysicalDevices { get; } = new();
    public DeviceSwitchModel DeviceSwitch { get; } = new();
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

    public bool Prepare(PhysicalDeviceCard device) =>
        device.IsSelectable && Prepare(device.EndpointId);

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
