// SPDX-License-Identifier: GPL-3.0-only

using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace Hibiki.ControlModel;

// WinUI-independent binding surface. The future Window can bind to these
// properties/commands without putting pipe I/O or DSP work on the audio thread.
public sealed class EasyControlViewModel : INotifyPropertyChanged
{
    private readonly EasyControlSession _session = new();
    private readonly ControlCommandFactoryV1 _commands = new();
    private readonly SemaphoreSlim _commandGate = new(1, 1);
    private readonly string _pipeName;
    private NamedPipeControlClientV1? _controlClient;
    private string? _selectedOutputGroup;
    private string? _selectedPhysicalDeviceId;
    private SceneCard? _selectedScene;
    private string _statusText = "尚未連接 Hibiki 音訊引擎";
    private bool _isExpert;
    private double _requestedVolumeDb = -12.0;
    private bool _muted;
    private ulong _generation;
    private VolumeSafetyStateV1 _volumeState = VolumeSafetyStateV1.Initial();
    private ulong _statusSequence;
    private ulong _sessionCatalogSequence;
    private IReadOnlyList<SessionCatalogEntryV1> _sessionCatalog =
        Array.Empty<SessionCatalogEntryV1>();
    private ulong _selectedSessionHandle;
    private string _sessionRouteLaneId = "app-lane";
    private string _sessionRouteOutputGroup = "main";
    private string _selectedRouteRuleSummary = "尚未選取 App；不會自動套用路由";
    private double _sessionVolumeDb = -12.0;
    private bool _sessionMuted;
    private string _routeRuleId = string.Empty;
    private string _routeRuleAppId = string.Empty;
    private string _routeRuleDisplayName = string.Empty;
    private string _routeRuleLaneId = "app-lane";
    private string _routeRuleOutputGroup = "main";
    private int _routeRulePriority;
    private double _routeRuleMakeupGainDb;
    private bool _routeRuleEnabled = true;
    private SessionRouteRuleGainOwnerV1 _routeRuleGainOwner =
        SessionRouteRuleGainOwnerV1.WindowsSession;
    private IrPhaseMode _irPhaseMode = IrPhaseMode.MinimumPhase;
    private double _irPhaseStrength;
    private string _irFilePath = string.Empty;
    private string _irPrepareStatus = "尚未載入 IR WAV";
    private ControlConnectionState _connectionState = ControlConnectionState.Disconnected;
    private bool _isBusy;
    private CancellationTokenSource? _volumeDebounce;
    private string _customSceneId = string.Empty;
    private string _customSceneName = string.Empty;
    private string _customSceneDescription = string.Empty;

    public ExpertSurfaceModel Expert { get; } = new();

    public IReadOnlyList<IrPhaseModeOption> IrPhaseModeOptions { get; } =
    [
        new(IrPhaseMode.MinimumPhase, "Game／Minimum Phase", "0 ms 額外緩衝；最低延遲"),
        new(IrPhaseMode.MixedPhase, "Balanced／Mixed Phase", "最多 80 ms；延遲與相位折衷"),
        new(IrPhaseMode.LinearPhase, "Movie／Linear Phase", "最多 160 ms；影音同步需補償"),
        new(IrPhaseMode.Bypass, "Bypass／不套用 IR", "不套用 IR 相位處理")
    ];

    public EasyControlViewModel(string pipeName = NamedPipeControlClientV1.DefaultPipeName)
    {
        if (string.IsNullOrWhiteSpace(pipeName) || pipeName.IndexOfAny(['\\', '/']) >= 0)
            throw new ArgumentException("Pipe name must be a stable logical name.", nameof(pipeName));
        _pipeName = pipeName;
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public IReadOnlyList<SceneCard> Scenes => _session.Scenes;
    public IReadOnlyList<OutputGroupCard> OutputGroups => OutputGroupCatalog.Fixed;
    public IReadOnlyList<PhysicalDeviceCard> PhysicalDevices => _session.PhysicalDevices.Devices;
    // The engine publishes ephemeral handles instead of raw Windows session
    // identifiers. A refresh replaces the list atomically; callers must not
    // retain a handle across a newer sequence.
    public IReadOnlyList<SessionCatalogEntryV1> SessionCatalog => _sessionCatalog;
    public IReadOnlyList<SessionRouteRuleCard> RouteRules => _session.RouteRules.Rules;
    public IReadOnlyList<SessionRouteRuleGainOwnerV1> RouteRuleGainOwners { get; } =
        Enum.GetValues<SessionRouteRuleGainOwnerV1>();
    public ulong SessionCatalogSequence => _sessionCatalogSequence;
    public ulong SelectedSessionHandle
    {
        get => _selectedSessionHandle;
        private set
        {
            if (value == _selectedSessionHandle) return;
            _selectedSessionHandle = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(SelectedSession));
            OnPropertyChanged(nameof(HasSelectedSession));
        }
    }
    public SessionCatalogEntryV1? SelectedSession =>
        _sessionCatalog.FirstOrDefault(item => item.Handle == _selectedSessionHandle);
    public bool HasSelectedSession => SelectedSession is not null;
    public string SessionRouteLaneId
    {
        get => _sessionRouteLaneId;
        set
        {
            var normalized = value ?? string.Empty;
            if (normalized == _sessionRouteLaneId) return;
            _sessionRouteLaneId = normalized;
            OnPropertyChanged();
        }
    }
    public string SessionRouteOutputGroup
    {
        get => _sessionRouteOutputGroup;
        set
        {
            var normalized = value ?? string.Empty;
            if (normalized == _sessionRouteOutputGroup) return;
            _sessionRouteOutputGroup = normalized;
            OnPropertyChanged();
        }
    }
    public string SelectedRouteRuleSummary => _selectedRouteRuleSummary;
    public double SessionVolumeDb
    {
        get => _sessionVolumeDb;
        set
        {
            if (!double.IsFinite(value)) return;
            var clamped = Math.Clamp(value, -144.0, 12.0);
            if (Math.Abs(clamped - _sessionVolumeDb) < 1e-9) return;
            _sessionVolumeDb = clamped;
            OnPropertyChanged();
        }
    }
    public bool SessionMuted
    {
        get => _sessionMuted;
        set
        {
            if (value == _sessionMuted) return;
            _sessionMuted = value;
            OnPropertyChanged();
        }
    }
    public string RouteRuleCatalogPath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "Hibiki DSP", "session-route-rules-v1.json");
    public string RouteRuleId
    {
        get => _routeRuleId;
        set { var normalized = value ?? string.Empty; if (normalized != _routeRuleId) { _routeRuleId = normalized; OnPropertyChanged(); } }
    }
    public string RouteRuleAppId
    {
        get => _routeRuleAppId;
        set { var normalized = value ?? string.Empty; if (normalized != _routeRuleAppId) { _routeRuleAppId = normalized; OnPropertyChanged(); } }
    }
    public string RouteRuleDisplayName
    {
        get => _routeRuleDisplayName;
        set { var normalized = value ?? string.Empty; if (normalized != _routeRuleDisplayName) { _routeRuleDisplayName = normalized; OnPropertyChanged(); } }
    }
    public string RouteRuleLaneId
    {
        get => _routeRuleLaneId;
        set { var normalized = value ?? string.Empty; if (normalized != _routeRuleLaneId) { _routeRuleLaneId = normalized; OnPropertyChanged(); } }
    }
    public string RouteRuleOutputGroup
    {
        get => _routeRuleOutputGroup;
        set { var normalized = value ?? string.Empty; if (normalized != _routeRuleOutputGroup) { _routeRuleOutputGroup = normalized; OnPropertyChanged(); } }
    }
    public int RouteRulePriority
    {
        get => _routeRulePriority;
        set { var clamped = Math.Clamp(value, -1_000_000, 1_000_000); if (clamped != _routeRulePriority) { _routeRulePriority = clamped; OnPropertyChanged(); } }
    }
    public double RouteRuleMakeupGainDb
    {
        get => _routeRuleMakeupGainDb;
        set { if (!double.IsFinite(value)) return; var clamped = Math.Clamp(value, -144.0, 12.0); if (Math.Abs(clamped - _routeRuleMakeupGainDb) >= 1e-9) { _routeRuleMakeupGainDb = clamped; OnPropertyChanged(); } }
    }
    public bool RouteRuleEnabled
    {
        get => _routeRuleEnabled;
        set { if (value != _routeRuleEnabled) { _routeRuleEnabled = value; OnPropertyChanged(); } }
    }
    public SessionRouteRuleGainOwnerV1 RouteRuleGainOwner
    {
        get => _routeRuleGainOwner;
        set { if (!Enum.IsDefined(value) || value == _routeRuleGainOwner) return; _routeRuleGainOwner = value; OnPropertyChanged(); }
    }
    public string CustomSceneCatalogPath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "Hibiki DSP", "scene-cards-v1.json");
    public UiMode Mode => _isExpert ? UiMode.Expert : UiMode.Easy;
    public AudioControlStatus Status => _session.Status;
    public ControlConnectionState ConnectionState => _connectionState;
    public bool IsConnected => _connectionState == ControlConnectionState.Connected;
    public bool IsBusy => _isBusy;
    public string ConnectionStatusText => _connectionState switch
    {
        ControlConnectionState.Connecting => "正在連接 Hibiki 音訊引擎…",
        ControlConnectionState.Connected => "Hibiki 已連線",
        ControlConnectionState.Degraded => "引擎未可用（音訊保持安全狀態）",
        _ => "尚未連接 Hibiki 音訊引擎"
    };
    public SceneCard? SelectedScene => _selectedScene;
    public string? SelectedOutputGroup
    {
        get => _selectedOutputGroup;
        set
        {
            var normalized = string.IsNullOrWhiteSpace(value) ? null : value.Trim();
            if (normalized == _selectedOutputGroup) return;
            _selectedOutputGroup = normalized;
            OnPropertyChanged();
        }
    }

    public string? SelectedPhysicalDeviceId
    {
        get => _selectedPhysicalDeviceId;
        set
        {
            var normalized = string.IsNullOrWhiteSpace(value) ? null : value.Trim();
            if (normalized == _selectedPhysicalDeviceId) return;
            _selectedPhysicalDeviceId = normalized;
            OnPropertyChanged();
            OnPropertyChanged(nameof(SelectedPhysicalDevice));
        }
    }

    public PhysicalDeviceCard? SelectedPhysicalDevice =>
        _selectedPhysicalDeviceId is not null &&
        _session.PhysicalDevices.TryGet(_selectedPhysicalDeviceId, out var device)
            ? device
            : null;

    public VolumeSafetyStateV1 VolumeState => _volumeState;
    public double EffectiveVolumeDb => _volumeState.EffectiveDb;
    public double SafetyCeilingDb => _volumeState.SafetyCeilingDb;
    public string SafetyStatusText => _volumeState.SafetyStatusText;
    public string VolumeOriginText => $"來源：{_volumeState.OriginLabel}";
    public string VolumeActuatorText => $"致動器：{_volumeState.ActuatorLabel}";
    public ulong VolumeGeneration => _volumeState.Generation;
    public ulong StatusSequence => _statusSequence;

    public string DeviceSwitchStatusText => _session.DeviceSwitch.State switch
    {
        DeviceSwitchModel.SwitchState.Preparing => "裝置預熱中…",
        DeviceSwitchModel.SwitchState.Fading => "裝置交叉淡化中…",
        DeviceSwitchModel.SwitchState.ReadyToCommit => "裝置已準備，等待引擎提交",
        DeviceSwitchModel.SwitchState.Synced => "裝置已同步",
        DeviceSwitchModel.SwitchState.RolledBack => "裝置切換已回復",
        DeviceSwitchModel.SwitchState.Degraded => "裝置切換降級；保留上一個安全輸出",
        _ => "尚未選擇實體輸出裝置"
    };

    public string StatusText
    {
        get => _statusText;
        private set
        {
            if (value == _statusText) return;
            _statusText = value;
            OnPropertyChanged();
        }
    }

    public string CustomSceneId
    {
        get => _customSceneId;
        set { if (value != _customSceneId) { _customSceneId = value; OnPropertyChanged(); } }
    }

    public string CustomSceneName
    {
        get => _customSceneName;
        set { if (value != _customSceneName) { _customSceneName = value; OnPropertyChanged(); } }
    }

    public string CustomSceneDescription
    {
        get => _customSceneDescription;
        set { if (value != _customSceneDescription) { _customSceneDescription = value; OnPropertyChanged(); } }
    }

    public bool IsExpert
    {
        get => _isExpert;
        set
        {
            if (value == _isExpert) return;
            _isExpert = value;
            _session.SetMode(Mode);
            Expert.SetVisible(value);
            OnPropertyChanged();
            OnPropertyChanged(nameof(Mode));
        }
    }

    public double RequestedVolumeDb
    {
        get => _requestedVolumeDb;
        set
        {
            if (!double.IsFinite(value)) return;
            var clamped = Math.Clamp(value, -144.0, 12.0);
            if (Math.Abs(clamped - _requestedVolumeDb) < 1e-9) return;
            _requestedVolumeDb = clamped;
            UpdateLocalVolumeState(VolumeStateOriginV1.HibikiUi);
            OnPropertyChanged();
        }
    }

    public bool Muted
    {
        get => _muted;
        set
        {
            if (value == _muted) return;
            _muted = value;
            UpdateLocalVolumeState(VolumeStateOriginV1.HibikiUi);
            OnPropertyChanged();
        }
    }

    public IrPhaseMode IrPhaseMode
    {
        get => _irPhaseMode;
        set
        {
            if (!Enum.IsDefined(value) || value == _irPhaseMode) return;
            _irPhaseMode = value;
            if (value is IrPhaseMode.MinimumPhase or IrPhaseMode.Bypass)
                IrPhaseStrength = 0.0;
            OnPropertyChanged();
            OnPropertyChanged(nameof(IrPhasePolicy));
            OnPropertyChanged(nameof(IrAddedDelayMs));
            OnPropertyChanged(nameof(IrPhaseUsesFir));
            OnPropertyChanged(nameof(IrPhaseModeText));
            MarkIrPrepareStale();
        }
    }

    public double IrPhaseStrength
    {
        get => _irPhaseStrength;
        set
        {
            if (!double.IsFinite(value)) return;
            var clamped = Math.Clamp(value, 0.0, 1.0);
            if (Math.Abs(clamped - _irPhaseStrength) < 1e-9) return;
            _irPhaseStrength = clamped;
            OnPropertyChanged();
            OnPropertyChanged(nameof(IrPhasePolicy));
            OnPropertyChanged(nameof(IrAddedDelayMs));
            OnPropertyChanged(nameof(IrPhaseUsesFir));
            MarkIrPrepareStale();
        }
    }

    public IrPhasePolicyV1 IrPhasePolicy => new(IrPhaseMode, IrPhaseStrength);
    public double IrAddedDelayMs => IrPhasePolicy.AddedDelayMs;
    public bool IrPhaseUsesFir => IrPhasePolicy.UsesFir;
    public string IrPhaseModeText => IrPhaseMode switch
    {
        IrPhaseMode.MinimumPhase => "Minimum-phase／0 ms 額外緩衝",
        IrPhaseMode.MixedPhase => "Mixed-phase／最多 80 ms",
        IrPhaseMode.LinearPhase => "Linear-phase／最多 160 ms",
        IrPhaseMode.Bypass => "Bypass／不套用 IR",
        _ => "未知相位模式"
    };

    public string IrFilePath => _irFilePath;
    public string IrPrepareStatus => _irPrepareStatus;
    public bool HasPreparedIr => _irFilePath.Length > 0 &&
                                 !_irPrepareStatus.Contains("需重新載入", StringComparison.Ordinal);

    private void MarkIrPrepareStale()
    {
        if (_irFilePath.Length == 0) return;
        _irPrepareStatus = "相位 policy 已變更；需重新載入 IR WAV";
        OnPropertyChanged(nameof(IrPrepareStatus));
        OnPropertyChanged(nameof(HasPreparedIr));
    }

    private void MarkIrClearedByScene()
    {
        if (_irFilePath.Length == 0 && _irPrepareStatus == "尚未載入 IR WAV") return;
        _irPrepareStatus = "Scene 已切換；IR 已清除，需重新載入";
        OnPropertyChanged(nameof(IrPrepareStatus));
        OnPropertyChanged(nameof(HasPreparedIr));
    }

    public async Task<bool> PrepareIrAsync(string filePath,
                                            CancellationToken cancellationToken = default)
    {
        if (IrPhaseMode == IrPhaseMode.Bypass)
        {
            _irPrepareStatus = "Bypass 不會載入 IR；請先選擇相位模式";
            OnPropertyChanged(nameof(IrPrepareStatus));
            return false;
        }
        if (!IsConnected)
        {
            _irPrepareStatus = "引擎未連線；IR 尚未送出";
            OnPropertyChanged(nameof(IrPrepareStatus));
            return false;
        }
        string fullPath;
        try
        {
            fullPath = Path.GetFullPath(filePath ?? string.Empty);
            if (!File.Exists(fullPath)) throw new FileNotFoundException();
            if (new FileInfo(fullPath).Length > 64L * 1024L * 1024L)
                throw new InvalidDataException("IR WAV 超過 64 MiB 上限");
            LastCommand = _commands.PrepareIr(fullPath, IrPhasePolicy);
            OnPropertyChanged(nameof(LastCommand));
        }
        catch (Exception exception) when (exception is ArgumentException or IOException or
                                           UnauthorizedAccessException or InvalidDataException)
        {
            _irPrepareStatus = $"IR 檔案無法準備：{exception.Message}";
            OnPropertyChanged(nameof(IrPrepareStatus));
            return false;
        }

        var sent = await SendLastCommandAsync(cancellationToken).ConfigureAwait(true);
        if (sent)
        {
            _irFilePath = fullPath;
            _irPrepareStatus = $"已在引擎 control-plane prepare：{Path.GetFileName(fullPath)}";
            OnPropertyChanged(nameof(IrFilePath));
            OnPropertyChanged(nameof(IrPrepareStatus));
            OnPropertyChanged(nameof(HasPreparedIr));
        }
        else
        {
            _irPrepareStatus = "引擎拒絕 IR；保留上一個安全 kernel";
            OnPropertyChanged(nameof(IrPrepareStatus));
        }
        return sent;
    }

    public IpcEnvelopeV1? LastCommand { get; private set; }

    public bool OneTapEnhance()
    {
        var result = _session.OneTapEnhance(SelectedOutputGroup);
        _selectedScene = result.Scene;
        StatusText = result.Message ?? (result.Succeeded ? "已控制" : "控制失敗");
        OnPropertyChanged(nameof(SelectedScene));
        OnPropertyChanged(nameof(Status));
        if (!result.Succeeded) return false;
        LastCommand = _commands.ApplyScene(_selectedScene!.Id, _session.ActiveOutputGroup!);
        OnPropertyChanged(nameof(LastCommand));
        return true;
    }

    public bool UpsertCustomScene(SceneCard scene)
    {
        if (!_session.CustomScenes.Upsert(scene)) return false;
        OnPropertyChanged(nameof(Scenes));
        return true;
    }

    // In-memory helper for a host that has already loaded or edited a preset;
    // AddOrUpdateRouteRule remains the persistence-aware UI operation.
    public bool UpsertRouteRule(SessionRouteRuleCard rule)
    {
        if (!_session.RouteRules.Upsert(rule)) return false;
        OnPropertyChanged(nameof(RouteRules));
        if (SelectedSession is not null) ApplySelectedRouteRulePreview();
        return true;
    }

    public bool AddCustomScene()
    {
        var scene = new SceneCard(CustomSceneId.Trim(), CustomSceneName.Trim(),
                                  CustomSceneDescription.Trim(), "平衡", true);
        var previous = _session.CustomScenes.Scenes.FirstOrDefault(item => item.Id == scene.Id);
        if (!UpsertCustomScene(scene))
        {
            StatusText = "自訂場景無效、重複或已達 32 筆上限";
            return false;
        }
        if (!SaveCustomScenes(out var saveError))
        {
            if (previous is null) _session.CustomScenes.Remove(scene.Id);
            else _session.CustomScenes.Upsert(previous);
            OnPropertyChanged(nameof(Scenes));
            StatusText = $"自訂場景未保存：{saveError}";
            return false;
        }
        CustomSceneId = string.Empty;
        CustomSceneName = string.Empty;
        CustomSceneDescription = string.Empty;
        StatusText = $"已加入自訂場景：{scene.Name}";
        return true;
    }

    // Endpoint metadata is supplied by the engine worker; this mirror never
    // fabricates a device and never claims a switch before an Ack arrives.
    public bool UpsertPhysicalDevice(PhysicalDeviceCard device, out string error)
    {
        var accepted = _session.PhysicalDevices.Upsert(device, out error);
        if (accepted)
        {
            OnPropertyChanged(nameof(PhysicalDevices));
            OnPropertyChanged(nameof(SelectedPhysicalDevice));
        }
        return accepted;
    }

    public bool SetPhysicalDeviceAvailability(string endpointId,
                                               PhysicalDeviceAvailabilityV1 availability,
                                               ulong sequence,
                                               out string error)
    {
        var accepted = _session.PhysicalDevices.SetAvailability(endpointId, availability,
                                                                  sequence, out error);
        if (accepted)
        {
            OnPropertyChanged(nameof(PhysicalDevices));
            OnPropertyChanged(nameof(SelectedPhysicalDevice));
        }
        return accepted;
    }

    // A control-worker reader may forward unsolicited DeviceCatalogSnapshot
    // frames here. The snapshot is validated and atomically swapped; it never
    // implies that a physical endpoint is bound or that a switch was ACKed.
    public bool ApplyPhysicalDeviceSnapshot(IpcEnvelopeV1 frame, out string error)
    {
        error = string.Empty;
        if (frame.Type != ControlMessageType.DeviceCatalogSnapshot ||
            !ControlPayloadsV1.TryDecodeDeviceCatalogSnapshot(frame.Payload.Span,
                                                               out var sequence,
                                                               out var devices))
        {
            error = "裝置快照格式無效";
            return false;
        }
        if (!_session.PhysicalDevices.ReplaceSnapshot(devices, sequence, out error))
            return false;
        if (_selectedPhysicalDeviceId is not null && SelectedPhysicalDevice is null)
            SelectedPhysicalDeviceId = null;
        OnPropertyChanged(nameof(PhysicalDevices));
        OnPropertyChanged(nameof(SelectedPhysicalDevice));
        StatusText = "裝置清單已更新；可選擇輸出裝置";
        return true;
    }

    // Applies the bounded App/session catalog without exposing endpoint IDs,
    // PIDs or Windows session-instance strings to the UI. Malformed or stale
    // frames leave the previous visible catalog untouched.
    public bool ApplySessionCatalogSnapshot(IpcEnvelopeV1 frame, out string error)
    {
        error = string.Empty;
        if (frame.Type != ControlMessageType.SessionCatalogSnapshot ||
            !ControlPayloadsV1.TryDecodeSessionCatalogSnapshot(frame.Payload.Span,
                                                                 out var sequence,
                                                                 out _,
                                                                 out var sessions))
        {
            error = "工作階段清單格式無效";
            return false;
        }
        if (sequence < _sessionCatalogSequence)
        {
            error = "工作階段清單已過期";
            return false;
        }
        _sessionCatalog = sessions.ToArray();
        _sessionCatalogSequence = sequence;
        if (SelectedSession is null) SelectedSessionHandle = 0UL;
        SyncSelectedSessionVolume();
        ApplySelectedRouteRulePreview();
        OnPropertyChanged(nameof(SessionCatalog));
        OnPropertyChanged(nameof(SessionCatalogSequence));
        OnPropertyChanged(nameof(SelectedSession));
        StatusText = "App 工作階段清單已更新；可選擇每個 App 的路由";
        return true;
    }

    public bool SelectSession(ulong handle)
    {
        if (handle == 0UL || !_sessionCatalog.Any(item => item.Handle == handle))
        {
            StatusText = "App 工作階段不存在或清單已刷新";
            return false;
        }
        SelectedSessionHandle = handle;
        SyncSelectedSessionVolume();
        ApplySelectedRouteRulePreview();
        StatusText = $"已選擇 {SelectedSession?.DisplayName ?? "App 工作階段"}";
        return true;
    }

    private void SyncSelectedSessionVolume()
    {
        var selected = SelectedSession;
        if (selected is null || !selected.VolumeAvailable) return;
        _sessionVolumeDb = Math.Clamp(selected.RequestedDb, -144.0, 12.0);
        _sessionMuted = selected.Muted;
        OnPropertyChanged(nameof(SessionVolumeDb));
        OnPropertyChanged(nameof(SessionMuted));
    }

    private void ApplySelectedRouteRulePreview()
    {
        var selected = SelectedSession;
        if (selected is null)
        {
            _selectedRouteRuleSummary = "尚未選取 App；不會自動套用路由";
            OnPropertyChanged(nameof(SelectedRouteRuleSummary));
            return;
        }
        var resolution = _session.RouteRules.TryResolve(selected, out var rule);
        switch (resolution)
        {
            case SessionRouteRuleResolutionV1.Applied when rule is not null:
                SessionRouteLaneId = rule.LaneId;
                SessionRouteOutputGroup = rule.OutputGroup;
                _selectedRouteRuleSummary =
                    $"預覽預設：{rule.RuleId} → {rule.LaneId}／{rule.OutputGroup}；按套用後才送出命令";
                break;
            case SessionRouteRuleResolutionV1.Ambiguous:
                _selectedRouteRuleSummary = "有同優先級路由預設同時符合；已停用自動預覽，請調整優先級";
                break;
            default:
                _selectedRouteRuleSummary = "沒有符合的路由預設；保留目前 Lane／Output 設定";
                break;
        }
        OnPropertyChanged(nameof(SelectedRouteRuleSummary));
    }

    public IpcEnvelopeV1 BuildSessionVolumeCommand(ulong handle,
                                                     double requestedDb,
                                                     bool mute)
    {
        if (!SelectSession(handle)) throw new InvalidOperationException("App 工作階段已過期");
        LastCommand = _commands.SetSessionVolume(handle, requestedDb, mute,
                                                  _sessionCatalogSequence);
        OnPropertyChanged(nameof(LastCommand));
        return LastCommand!;
    }

    public async Task<bool> PushSessionVolumeAsync(ulong handle,
                                                    double requestedDb,
                                                    bool mute,
                                                    CancellationToken cancellationToken = default)
    {
        try
        {
            BuildSessionVolumeCommand(handle, requestedDb, mute);
        }
        catch (ArgumentException)
        {
            StatusText = "App 音量超出安全範圍；命令未送出";
            return false;
        }
        catch (InvalidOperationException)
        {
            StatusText = "App 工作階段已過期；請先刷新清單";
            return false;
        }
        return await SendLastCommandAsync(cancellationToken).ConfigureAwait(true);
    }

    public IpcEnvelopeV1 BuildSessionRouteCommand(ulong handle,
                                                    string laneId,
                                                    string outputGroup)
    {
        if (!SelectSession(handle)) throw new InvalidOperationException("App 工作階段已過期");
        LastCommand = _commands.SetSessionRoute(handle, _sessionCatalogSequence, laneId,
                                                 outputGroup);
        OnPropertyChanged(nameof(LastCommand));
        return LastCommand!;
    }

    public async Task<bool> PushSessionRouteAsync(ulong handle,
                                                   string laneId,
                                                   string outputGroup,
                                                   CancellationToken cancellationToken = default)
    {
        try
        {
            BuildSessionRouteCommand(handle, laneId, outputGroup);
        }
        catch (ArgumentException)
        {
            StatusText = "App 路由名稱無效；命令未送出";
            return false;
        }
        catch (InvalidOperationException)
        {
            StatusText = "App 工作階段已過期；請先刷新清單";
            return false;
        }
        return await SendLastCommandAsync(cancellationToken).ConfigureAwait(true);
    }

    public Task<bool> ApplySelectedSessionRouteAsync(
        CancellationToken cancellationToken = default) =>
        HasSelectedSession
            ? PushSessionRouteAsync(SelectedSessionHandle, SessionRouteLaneId,
                                    SessionRouteOutputGroup, cancellationToken)
            : Task.FromResult(false);

    public Task<bool> ApplySelectedSessionVolumeAsync(
        CancellationToken cancellationToken = default)
    {
        if (!HasSelectedSession || SelectedSession?.VolumeAvailable != true)
        {
            StatusText = "此 App 工作階段目前沒有可用的 Windows 音量控制";
            return Task.FromResult(false);
        }
        return PushSessionVolumeAsync(SelectedSessionHandle, SessionVolumeDb, SessionMuted,
                                      cancellationToken);
    }

    public IpcEnvelopeV1 BuildUpsertSessionRouteRuleCommand(SessionRouteRuleCard rule)
    {
        if (_sessionCatalogSequence == 0UL)
            throw new InvalidOperationException("App 清單尚未同步");
        var validator = new SessionRouteRuleCatalogV1();
        if (!validator.Upsert(rule))
            throw new ArgumentException("App 路由預設內容無效", nameof(rule));
        LastCommand = _commands.UpsertSessionRouteRule(
            _sessionCatalogSequence, rule.RuleId, rule.AppId, rule.DisplayName,
            rule.LaneId, rule.OutputGroup, rule.Priority, rule.MakeupGainDb,
            rule.Enabled, rule.GainOwner);
        OnPropertyChanged(nameof(LastCommand));
        return LastCommand;
    }

    public IpcEnvelopeV1 BuildRemoveSessionRouteRuleCommand(string ruleId)
    {
        if (_sessionCatalogSequence == 0UL)
            throw new InvalidOperationException("App 清單尚未同步");
        if (string.IsNullOrWhiteSpace(ruleId))
            throw new ArgumentException("規則身份不可為空", nameof(ruleId));
        LastCommand = _commands.RemoveSessionRouteRule(_sessionCatalogSequence, ruleId.Trim());
        OnPropertyChanged(nameof(LastCommand));
        return LastCommand;
    }

    public IpcEnvelopeV1 BuildClearSessionRouteRulesCommand()
    {
        if (_sessionCatalogSequence == 0UL)
            throw new InvalidOperationException("App 清單尚未同步");
        LastCommand = _commands.ClearSessionRouteRules(_sessionCatalogSequence);
        OnPropertyChanged(nameof(LastCommand));
        return LastCommand;
    }

    public bool AddOrUpdateRouteRule()
    {
        var rule = new SessionRouteRuleCard(
            RouteRuleId.Trim(), RouteRulePriority, RouteRuleEnabled, RouteRuleGainOwner,
            RouteRuleMakeupGainDb, RouteRuleAppId.Trim(), RouteRuleDisplayName.Trim(),
            RouteRuleLaneId.Trim(), RouteRuleOutputGroup.Trim());
        var previous = _session.RouteRules.Rules.FirstOrDefault(item =>
            item.RuleId == rule.RuleId);
        if (!_session.RouteRules.Upsert(rule))
        {
            StatusText = "路由預設無效、需要 App ID/名稱，或已達 64 筆上限";
            return false;
        }
        if (!SaveRouteRules(out var saveError))
        {
            if (previous is null) _session.RouteRules.Remove(rule.RuleId);
            else _session.RouteRules.Upsert(previous);
            OnPropertyChanged(nameof(RouteRules));
            StatusText = $"路由預設未保存：{saveError}";
            return false;
        }
        OnPropertyChanged(nameof(RouteRules));
        LastCommand = null;
        OnPropertyChanged(nameof(LastCommand));
        if (_sessionCatalogSequence != 0UL)
        {
            try { BuildUpsertSessionRouteRuleCommand(rule); }
            catch (ArgumentException)
            {
                StatusText = "路由預設已保存，但命令內容無效；未送出";
                return true;
            }
        }
        StatusText = _sessionCatalogSequence == 0UL
            ? $"已保存路由預設：{rule.RuleId}；刷新 App 清單後才能套用"
            : $"已保存路由預設：{rule.RuleId}；等待引擎確認套用";
        return true;
    }

    public async Task<bool> ApplyRouteRuleAsync(
        CancellationToken cancellationToken = default)
    {
        if (!AddOrUpdateRouteRule()) return false;
        if (LastCommand is null)
        {
            StatusText = "路由預設已保存；引擎尚未同步 App 清單，尚未套用";
            return false;
        }
        return await SendLastCommandAsync(cancellationToken).ConfigureAwait(true);
    }

    public bool RemoveRouteRule(string ruleId)
    {
        var previous = _session.RouteRules.Rules.FirstOrDefault(item =>
            item.RuleId == ruleId?.Trim());
        if (previous is null)
        {
            StatusText = "找不到這個路由預設";
            return false;
        }
        if (!_session.RouteRules.Remove(previous.RuleId)) return false;
        if (!SaveRouteRules(out var saveError))
        {
            _session.RouteRules.Upsert(previous);
            OnPropertyChanged(nameof(RouteRules));
            StatusText = $"路由預設未保存：{saveError}";
            return false;
        }
        OnPropertyChanged(nameof(RouteRules));
        LastCommand = null;
        OnPropertyChanged(nameof(LastCommand));
        if (_sessionCatalogSequence != 0UL)
        {
            try { BuildRemoveSessionRouteRuleCommand(previous.RuleId); }
            catch (ArgumentException)
            {
                StatusText = "路由預設已移除，但命令內容無效；未送出";
                return true;
            }
        }
        StatusText = _sessionCatalogSequence == 0UL
            ? "已移除路由預設；刷新 App 清單後才會同步引擎"
            : "已移除路由預設；等待引擎確認套用";
        return true;
    }

    public async Task<bool> ApplyRemoveRouteRuleAsync(
        string ruleId, CancellationToken cancellationToken = default)
    {
        if (!RemoveRouteRule(ruleId)) return false;
        if (LastCommand is null)
        {
            StatusText = "已移除並保存路由預設；引擎尚未同步 App 清單";
            return false;
        }
        return await SendLastCommandAsync(cancellationToken).ConfigureAwait(true);
    }

    public bool ClearRouteRules()
    {
        var previous = _session.RouteRules.Rules.ToArray();
        if (previous.Length == 0)
        {
            StatusText = "目前沒有路由預設";
            return false;
        }
        _session.RouteRules.Clear();
        if (!SaveRouteRules(out var saveError))
        {
            foreach (var rule in previous) _session.RouteRules.Upsert(rule);
            OnPropertyChanged(nameof(RouteRules));
            StatusText = $"路由預設未保存：{saveError}";
            return false;
        }
        OnPropertyChanged(nameof(RouteRules));
        LastCommand = null;
        OnPropertyChanged(nameof(LastCommand));
        if (_sessionCatalogSequence != 0UL) BuildClearSessionRouteRulesCommand();
        StatusText = _sessionCatalogSequence == 0UL
            ? "已清除路由預設；刷新 App 清單後才會同步引擎"
            : "已清除路由預設；等待引擎確認套用";
        return true;
    }

    public async Task<bool> ApplyClearRouteRulesAsync(
        CancellationToken cancellationToken = default)
    {
        if (!ClearRouteRules()) return false;
        if (LastCommand is null)
        {
            StatusText = "已清除並保存路由預設；引擎尚未同步 App 清單";
            return false;
        }
        return await SendLastCommandAsync(cancellationToken).ConfigureAwait(true);
    }

    // A future versioned status frame can call this after the engine has
    // reconciled Windows dB, the safety ceiling and the actual actuator. It is
    // intentionally fail-closed and never writes Windows or the audio graph.
    public bool ApplyVolumeSafetyState(VolumeSafetyStateV1 state, out string error)
    {
        error = string.Empty;
        if (!state.IsValid)
        {
            error = "音量安全狀態無效";
            return false;
        }
        if (state.Generation < _volumeState.Generation)
        {
            error = "音量安全狀態已過期";
            return false;
        }
        _volumeState = state;
        _requestedVolumeDb = state.RequestedDb;
        _muted = state.Muted;
        _generation = state.Generation;
        OnPropertyChanged(nameof(RequestedVolumeDb));
        OnPropertyChanged(nameof(Muted));
        OnPropertyChanged(nameof(VolumeState));
        OnPropertyChanged(nameof(EffectiveVolumeDb));
        OnPropertyChanged(nameof(SafetyCeilingDb));
        OnPropertyChanged(nameof(SafetyStatusText));
        OnPropertyChanged(nameof(VolumeOriginText));
        OnPropertyChanged(nameof(VolumeActuatorText));
        OnPropertyChanged(nameof(VolumeGeneration));
        return true;
    }

    public bool ApplyRouteHealth(IReadOnlyList<RouteHealthCardV1> cards, out string error)
    {
        if (!Expert.TryApplyRouteHealth(cards, out error)) return false;
        OnPropertyChanged(nameof(Expert));
        return true;
    }

    // Applies one complete engine status transaction. Route validation happens
    // against a temporary model first so a malformed snapshot cannot partially
    // replace the visible volume or route state.
    public bool ApplyControlStatusSnapshot(IpcEnvelopeV1 frame, out string error)
    {
        error = string.Empty;
        if (frame.Type != ControlMessageType.ControlStatusSnapshot ||
            !ControlPayloadsV1.TryDecodeControlStatusSnapshot(frame.Payload.Span,
                                                               out var sequence,
                                                               out var volume,
                                                               out var routes))
        {
            error = "控制狀態快照格式無效";
            return false;
        }
        if (sequence < _statusSequence)
        {
            error = "控制狀態快照已過期";
            return false;
        }
        var validator = new ExpertSurfaceModel();
        if (!validator.TryApplyRouteHealth(routes, out error)) return false;
        if (!ApplyVolumeSafetyState(volume, out error)) return false;
        if (!Expert.TryApplyRouteHealth(routes, out error)) return false;
        _statusSequence = sequence;
        OnPropertyChanged(nameof(StatusSequence));
        OnPropertyChanged(nameof(Expert));
        StatusText = "引擎狀態已同步；音量安全與來源路由已更新";
        return true;
    }

    public async Task<bool> RefreshControlStatusAsync(
        CancellationToken cancellationToken = default)
    {
        if (_controlClient is null || !_controlClient.IsConnected)
        {
            StatusText = "引擎未連線；無法更新控制狀態";
            return false;
        }
        var gateHeld = false;
        try
        {
            await _commandGate.WaitAsync(cancellationToken).ConfigureAwait(true);
            gateHeld = true;
            SetBusy(true);
            var request = _commands.RequestControlStatus();
            var reply = await _controlClient.RoundTripAsync(request, cancellationToken)
                .ConfigureAwait(true);
            var snapshotError = string.Empty;
            if (!ApplyControlStatusSnapshot(reply, out snapshotError))
            {
                StatusText = reply.Type == ControlMessageType.Error
                    ? "引擎暫未提供控制狀態；保留上一個安全狀態"
                    : $"控制狀態無效：{snapshotError}";
                return false;
            }
            return true;
        }
        catch (OperationCanceledException)
        {
            StatusText = "控制狀態更新已取消；保留上一個安全狀態";
            return false;
        }
        catch (Exception)
        {
            SetConnectionState(ControlConnectionState.Degraded);
            StatusText = "控制狀態更新失敗；保留上一個安全狀態";
            return false;
        }
        finally
        {
            SetBusy(false);
            if (gateHeld) _commandGate.Release();
        }
    }

    public async Task<bool> RefreshPhysicalDevicesAsync(
        CancellationToken cancellationToken = default)
    {
        if (_controlClient is null || !_controlClient.IsConnected)
        {
            StatusText = "引擎未連線；無法更新裝置清單";
            return false;
        }
        var gateHeld = false;
        try
        {
            await _commandGate.WaitAsync(cancellationToken).ConfigureAwait(true);
            gateHeld = true;
            SetBusy(true);
            var reply = await _controlClient.RoundTripAsync(_commands.RequestDeviceCatalog(),
                                                             cancellationToken)
                .ConfigureAwait(true);
            var snapshotError = string.Empty;
            var applied = reply.Type == ControlMessageType.DeviceCatalogSnapshot &&
                          ApplyPhysicalDeviceSnapshot(reply, out snapshotError);
            if (!applied)
            {
                StatusText = reply.Type == ControlMessageType.Error
                    ? "引擎拒絕裝置清單要求"
                    : $"裝置清單無效：{snapshotError}";
                return false;
            }
            return true;
        }
        catch (OperationCanceledException)
        {
            StatusText = "裝置清單更新已取消";
            return false;
        }
        catch (Exception)
        {
            SetConnectionState(ControlConnectionState.Degraded);
            StatusText = "裝置清單更新失敗；保留上一個安全輸出";
            return false;
        }
        finally
        {
            SetBusy(false);
            if (gateHeld) _commandGate.Release();
        }
    }

    public async Task<bool> RefreshSessionCatalogAsync(
        CancellationToken cancellationToken = default)
    {
        if (_controlClient is null || !_controlClient.IsConnected)
        {
            StatusText = "引擎未連線；無法更新 App 清單";
            return false;
        }
        var gateHeld = false;
        try
        {
            await _commandGate.WaitAsync(cancellationToken).ConfigureAwait(true);
            gateHeld = true;
            SetBusy(true);
            var reply = await _controlClient.RoundTripAsync(_commands.RequestSessionCatalog(),
                                                              cancellationToken)
                .ConfigureAwait(true);
            var snapshotError = string.Empty;
            var applied = reply.Type == ControlMessageType.SessionCatalogSnapshot &&
                          ApplySessionCatalogSnapshot(reply, out snapshotError);
            if (!applied)
            {
                StatusText = reply.Type == ControlMessageType.Error
                    ? "引擎拒絕 App 清單要求"
                    : $"App 清單無效：{snapshotError}";
                return false;
            }
            return true;
        }
        catch (OperationCanceledException)
        {
            StatusText = "App 清單更新已取消；保留上一份清單";
            return false;
        }
        catch (Exception)
        {
            SetConnectionState(ControlConnectionState.Degraded);
            StatusText = "App 清單更新失敗；保留上一份清單";
            return false;
        }
        finally
        {
            SetBusy(false);
            if (gateHeld) _commandGate.Release();
        }
    }

    public bool SelectPhysicalDevice(string endpointId)
    {
        if (!_session.PhysicalDevices.TryGet(endpointId, out var device) || device is null ||
            !device.IsSelectable)
        {
            StatusText = "裝置不存在、已拔除或尚未準備完成";
            return false;
        }
        SelectedPhysicalDeviceId = device.EndpointId;
        if (!_session.DeviceSwitch.Prepare(device))
        {
            StatusText = "上一個裝置切換仍在進行；請稍候";
            return false;
        }
        LastCommand = _commands.SwitchDevice(device);
        StatusText = $"正在切換到 {device.DisplayName}；等待引擎預熱與交叉淡化";
        OnPropertyChanged(nameof(LastCommand));
        OnPropertyChanged(nameof(DeviceSwitchStatusText));
        return true;
    }

    public async Task<bool> SwitchPhysicalDeviceAsync(string endpointId,
                                                        CancellationToken cancellationToken = default)
    {
        if (!SelectPhysicalDevice(endpointId)) return false;
        var sent = await SendLastCommandAsync(cancellationToken).ConfigureAwait(true);
        if (!sent)
        {
            _session.DeviceSwitch.Rollback();
            OnPropertyChanged(nameof(DeviceSwitchStatusText));
        }
        return sent;
    }

    public bool LoadCustomScenes(out string error)
    {
        var loaded = _session.CustomScenes.TryLoad(CustomSceneCatalogPath, out error);
        if (loaded) OnPropertyChanged(nameof(Scenes));
        return loaded;
    }

    public bool SaveCustomScenes(out string error) =>
        _session.CustomScenes.TrySave(CustomSceneCatalogPath, out error);

    public bool LoadRouteRules(out string error)
    {
        var loaded = _session.RouteRules.TryLoad(RouteRuleCatalogPath, out error);
        if (loaded) OnPropertyChanged(nameof(RouteRules));
        return loaded;
    }

    public bool SaveRouteRules(out string error) =>
        _session.RouteRules.TrySave(RouteRuleCatalogPath, out error);

    public bool RemoveCustomScene(string sceneId)
    {
        if (!_session.CustomScenes.Remove(sceneId)) return false;
        if (_selectedScene?.Id == sceneId) _selectedScene = null;
        OnPropertyChanged(nameof(Scenes));
        OnPropertyChanged(nameof(SelectedScene));
        return true;
    }

    public async Task<bool> ConnectAsync(TimeSpan timeout,
                                          CancellationToken cancellationToken = default)
    {
        if (_connectionState == ControlConnectionState.Connecting) return false;
        await DisconnectAsync().ConfigureAwait(true);
        SetConnectionState(ControlConnectionState.Connecting);
        SetBusy(true);
        var client = new NamedPipeControlClientV1(_pipeName);
        try
        {
            await client.ConnectAsync(timeout, cancellationToken).ConfigureAwait(true);
            var reply = await client.RoundTripAsync(_commands.Hello(), cancellationToken)
                .ConfigureAwait(true);
            if (reply.Type != ControlMessageType.Ack)
                throw new InvalidDataException("Hibiki engine rejected the Hello request.");
            _controlClient = client;
            SetConnectionState(ControlConnectionState.Connected);
            StatusText = "引擎已連線；正在更新輸出裝置清單…";
            if (!await RefreshPhysicalDevicesAsync(cancellationToken).ConfigureAwait(true) &&
                IsConnected)
                StatusText = "引擎已連線；裝置清單暫不可用，音訊保持安全狀態";
            if (IsConnected && !await RefreshControlStatusAsync(cancellationToken).ConfigureAwait(true))
                StatusText = "引擎已連線；控制狀態暫不可用，音訊保持安全狀態";
            if (IsConnected && !await RefreshSessionCatalogAsync(cancellationToken).ConfigureAwait(true))
                StatusText = "引擎已連線；App 清單暫不可用，音訊保持安全狀態";
            return true;
        }
        catch (OperationCanceledException)
        {
            await client.DisposeAsync().ConfigureAwait(true);
            SetConnectionState(ControlConnectionState.Degraded);
            StatusText = "連線已取消；音訊保持原狀";
            return false;
        }
        catch (Exception)
        {
            await client.DisposeAsync().ConfigureAwait(true);
            SetConnectionState(ControlConnectionState.Degraded);
            StatusText = "找不到 Hibiki 引擎；請確認服務已啟動";
            return false;
        }
        finally
        {
            SetBusy(false);
        }
    }

    public async Task DisconnectAsync()
    {
        if (_controlClient is not null)
        {
            await _controlClient.DisposeAsync().ConfigureAwait(true);
            _controlClient = null;
        }
        SetConnectionState(ControlConnectionState.Disconnected);
    }

    public async Task<bool> OneTapEnhanceAsync(CancellationToken cancellationToken = default)
    {
        if (!OneTapEnhance()) return false;
        var sent = await SendLastCommandAsync(cancellationToken).ConfigureAwait(true);
        if (sent) MarkIrClearedByScene();
        if (sent && IsConnected)
            _ = await RefreshControlStatusAsync(cancellationToken).ConfigureAwait(true);
        return sent;
    }

    public async Task<bool> SelectSceneAsync(string sceneId,
                                              CancellationToken cancellationToken = default)
    {
        if (!SelectScene(sceneId)) return false;
        var sent = await SendLastCommandAsync(cancellationToken).ConfigureAwait(true);
        if (sent) MarkIrClearedByScene();
        if (sent && IsConnected)
            _ = await RefreshControlStatusAsync(cancellationToken).ConfigureAwait(true);
        return sent;
    }

    public async Task<bool> PushVolumeAsync(CancellationToken cancellationToken = default)
    {
        var sent = await SendCommandAsync(BuildVolumeCommand, cancellationToken).ConfigureAwait(true);
        if (sent && IsConnected)
            _ = await RefreshControlStatusAsync(cancellationToken).ConfigureAwait(true);
        return sent;
    }

    public async Task<bool> QueueVolumeAsync(TimeSpan debounce = default,
                                              CancellationToken cancellationToken = default)
    {
        if (debounce == default) debounce = TimeSpan.FromMilliseconds(40);
        if (debounce <= TimeSpan.Zero || debounce > TimeSpan.FromSeconds(1))
            throw new ArgumentOutOfRangeException(nameof(debounce));
        var replacement = new CancellationTokenSource();
        var previous = Interlocked.Exchange(ref _volumeDebounce, replacement);
        previous?.Cancel();
        try
        {
            using var linked = CancellationTokenSource.CreateLinkedTokenSource(
                replacement.Token, cancellationToken);
            await Task.Delay(debounce, linked.Token).ConfigureAwait(true);
            if (replacement.IsCancellationRequested) return false;
            return await PushVolumeAsync(linked.Token).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            return false;
        }
        finally
        {
            Interlocked.CompareExchange(ref _volumeDebounce, null, replacement);
            replacement.Dispose();
        }
    }

    public bool SelectScene(string sceneId)
    {
        if (!_session.SelectScene(sceneId))
        {
            StatusText = "找不到這個場景";
            return false;
        }
        _selectedScene = _session.Scenes.First(item => item.Id == sceneId);
        StatusText = $"已選擇 {_selectedScene.Name}";
        OnPropertyChanged(nameof(SelectedScene));
        if (_session.ActiveOutputGroup is not null)
        {
            LastCommand = _commands.ApplyScene(_selectedScene.Id, _session.ActiveOutputGroup);
            OnPropertyChanged(nameof(LastCommand));
        }
        return true;
    }

    public IpcEnvelopeV1 BuildVolumeCommand()
    {
        _generation++;
        UpdateLocalVolumeState(VolumeStateOriginV1.HibikiUi);
        LastCommand = _commands.SetVolume(RequestedVolumeDb, Muted, _generation,
                                           _selectedOutputGroup);
        OnPropertyChanged(nameof(LastCommand));
        return LastCommand;
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));

    private Task<bool> SendLastCommandAsync(CancellationToken cancellationToken) =>
        SendCommandAsync(() => LastCommand, cancellationToken);

    private async Task<bool> SendCommandAsync(Func<IpcEnvelopeV1?> commandFactory,
                                               CancellationToken cancellationToken)
    {
        var client = _controlClient;
        if (client is null || !IsConnected)
        {
            _session.MarkDegraded();
            StatusText = "尚未連接 Hibiki 引擎；命令未送出";
            OnPropertyChanged(nameof(Status));
            return false;
        }

        var gateHeld = false;
        try
        {
            await _commandGate.WaitAsync(cancellationToken).ConfigureAwait(true);
            gateHeld = true;
            if (_controlClient is null || !IsConnected) return false;
            var command = commandFactory();
            if (command is null)
            {
                _session.MarkDegraded();
                StatusText = "命令內容無效；未送出";
                OnPropertyChanged(nameof(Status));
                return false;
            }
            SetBusy(true);
            var reply = await client.RoundTripAsync(command, cancellationToken)
                .ConfigureAwait(true);
            if (reply.Type != ControlMessageType.Ack)
                throw new InvalidDataException("Hibiki engine rejected the command.");
            StatusText = _selectedScene is null
                ? "命令已套用"
                : $"已套用 {_selectedScene.Name} 到 {_selectedOutputGroup}";
            return true;
        }
        catch (OperationCanceledException)
        {
            StatusText = "命令已取消；保留上一個安全狀態";
            return false;
        }
        catch (Exception)
        {
            _session.MarkDegraded();
            SetConnectionState(ControlConnectionState.Degraded);
            StatusText = "引擎連線中斷；已回到安全狀態";
            OnPropertyChanged(nameof(Status));
            return false;
        }
        finally
        {
            SetBusy(false);
            if (gateHeld) _commandGate.Release();
        }
    }

    private void SetConnectionState(ControlConnectionState state)
    {
        if (_connectionState == state) return;
        _connectionState = state;
        OnPropertyChanged(nameof(ConnectionState));
        OnPropertyChanged(nameof(IsConnected));
        OnPropertyChanged(nameof(ConnectionStatusText));
    }

    private void SetBusy(bool value)
    {
        if (_isBusy == value) return;
        _isBusy = value;
        OnPropertyChanged(nameof(IsBusy));
    }

    private void UpdateLocalVolumeState(VolumeStateOriginV1 origin)
    {
        _volumeState = _volumeState with
        {
            RequestedDb = _requestedVolumeDb,
            EffectiveDb = Math.Min(_requestedVolumeDb, _volumeState.SafetyCeilingDb),
            Muted = _muted,
            Generation = _generation,
            Origin = origin
        };
        OnPropertyChanged(nameof(VolumeState));
        OnPropertyChanged(nameof(EffectiveVolumeDb));
        OnPropertyChanged(nameof(SafetyCeilingDb));
        OnPropertyChanged(nameof(SafetyStatusText));
        OnPropertyChanged(nameof(VolumeOriginText));
        OnPropertyChanged(nameof(VolumeActuatorText));
        OnPropertyChanged(nameof(VolumeGeneration));
    }
}
