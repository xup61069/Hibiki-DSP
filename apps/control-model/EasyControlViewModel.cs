// SPDX-License-Identifier: GPL-3.0-only

using System.ComponentModel;
using System.Globalization;
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
    private bool _customSceneLoudnessLiveUpdate;
    private string _customSceneCatalogPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "Hibiki DSP", "scene-cards-v1.json");
    private string _customSceneQueuePath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "Hibiki DSP", "scene-sync-queue-v1.json");
    private readonly Queue<PendingSceneCatalogOp> _pendingSceneCatalogOps = new();
    private int _droppedSceneCatalogOperations;
    private CancellationTokenSource? _eqPollCts;
    private Task? _eqPollLoop;
    private CalibrationTargetCurveIdV1 _wizardTargetCurve = CalibrationTargetCurveIdV1.Flat;
    private string _wizardMeasurementPath = string.Empty;
    private string _wizardStatus = "尚未載入量測；請先選擇 CSV 或 REW 文字檔";
    private int _wizardImportedPointCount;
    private bool _wizardHasResult;
    private string _wizardExportedPath = string.Empty;
    private IReadOnlyList<PeqFilterV1> _wizardCompiledFilters =
        Array.Empty<PeqFilterV1>();
    private double[]? _wizardMeasurementFrequencies;
    private double[]? _wizardMeasurementLevels;
    private IReadOnlyList<WizardPeqRow> _wizardPreviewRows =
        Array.Empty<WizardPeqRow>();
    private bool _wizardMultiChannel;
    private int _wizardChannelCount = 2;

    private const int EqPollIntervalMs = 1000;

    public ExpertSurfaceModel Expert { get; } = new();

    public EqVisualSurfaceModelV1 EqSurface { get; } = new();

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
    public IReadOnlyList<SceneCard> CustomSceneCards => _session.CustomScenes.Scenes;
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
    public string SessionCatalogSequenceDisplayText =>
        $"清單序號：{SessionCatalogSequence}；刷新後 handle 會更新";
    public ulong SelectedSessionHandle
    {
        get => _selectedSessionHandle;
        private set
        {
            if (value == _selectedSessionHandle) return;
            _selectedSessionHandle = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(SelectedSession));
            OnPropertyChanged(nameof(SelectedSessionDisplayText));
            OnPropertyChanged(nameof(HasSelectedSession));
        }
    }
    public SessionCatalogEntryV1? SelectedSession =>
        _sessionCatalog.FirstOrDefault(item => item.Handle == _selectedSessionHandle);
    public string SelectedSessionDisplayText =>
        $"目前選取：{SelectedSession?.DisplayName ?? string.Empty}";
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
            OnPropertyChanged(nameof(SessionVolumeDisplayText));
        }
    }
    public string SessionVolumeDisplayText => $"App 音量：{SessionVolumeDb:0.0} dB";
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
    public string CustomSceneCatalogPath
    {
        get => _customSceneCatalogPath;
        set
        {
            var normalized = value ?? string.Empty;
            if (normalized == _customSceneCatalogPath) return;
            _customSceneCatalogPath = normalized;
            OnPropertyChanged();
        }
    }
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
    public ListeningDoseModelV1 ListeningDose { get; } = new();
    public string EffectiveVolumeDisplayText => $"實際有效音量：{EffectiveVolumeDb:0.0} dB";
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

    // #278: last swallowed send-failure exception, exposed for diagnostics.
    public string LastSendDiagnostics { get; private set; } = string.Empty;

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

    public bool CustomSceneLoudnessLiveUpdate
    {
        get => _customSceneLoudnessLiveUpdate;
        set { if (value != _customSceneLoudnessLiveUpdate) { _customSceneLoudnessLiveUpdate = value; OnPropertyChanged(); } }
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
            OnPropertyChanged(nameof(RequestedVolumeDisplayText));
        }
    }

    public string RequestedVolumeDisplayText => $"{RequestedVolumeDb:0.0} dB";

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
            OnPropertyChanged(nameof(IrAddedDelayDisplayText));
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
            OnPropertyChanged(nameof(IrAddedDelayDisplayText));
            OnPropertyChanged(nameof(IrPhaseUsesFir));
            MarkIrPrepareStale();
        }
    }

    public IrPhasePolicyV1 IrPhasePolicy => new(IrPhaseMode, IrPhaseStrength);
    public double IrAddedDelayMs => IrPhasePolicy.AddedDelayMs;
    public string IrAddedDelayDisplayText => $"IR 額外延遲：{IrAddedDelayMs:0.0} ms";
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

    // ---- #1615 calibration wizard -------------------------------------

    public IrPhaseModeOption[] WizardCurveOptions { get; } =
        Enum.GetValues<CalibrationTargetCurveIdV1>()
            .Where(Enum.IsDefined)
            .Select(id => new IrPhaseModeOption(
                (IrPhaseMode)(int)id,
                id switch
                {
                    CalibrationTargetCurveIdV1.Flat => "Flat／平直",
                    CalibrationTargetCurveIdV1.HarmanInEar => "Harman 入耳",
                    CalibrationTargetCurveIdV1.HarmanOverEar => "Harman 耳罩",
                    _ => "未知目標曲線"
                },
                id switch
                {
                    CalibrationTargetCurveIdV1.Flat => "不做音色修飾，只校正量測誤差",
                    CalibrationTargetCurveIdV1.HarmanInEar => "套用 Harman 入耳目標曲線",
                    CalibrationTargetCurveIdV1.HarmanOverEar => "套用 Harman 耳罩目標曲線",
                    _ => ""
                }))
            .ToArray();

    public CalibrationTargetCurveIdV1 WizardTargetCurve
    {
        get => _wizardTargetCurve;
        set
        {
            if (!Enum.IsDefined(value) || value == _wizardTargetCurve) return;
            _wizardTargetCurve = value;
            OnPropertyChanged();
        }
    }

    public string WizardMeasurementPath => _wizardMeasurementPath;

    public string WizardStatus
    {
        get => _wizardStatus;
        private set
        {
            if (_wizardStatus == value) return;
            _wizardStatus = value;
            OnPropertyChanged();
        }
    }

    public int WizardImportedPointCount => _wizardImportedPointCount;
    public bool WizardHasMeasurement => _wizardMeasurementPath.Length > 0;
    public string WizardImportedCountText => $"量測點數：{_wizardImportedPointCount}";

    public bool WizardMultiChannel
    {
        get => _wizardMultiChannel;
        set
        {
            if (value == _wizardMultiChannel) return;
            _wizardMultiChannel = value;
            OnPropertyChanged();
        }
    }

    public int WizardChannelCount
    {
        get => _wizardChannelCount;
        set
        {
            if (value == _wizardChannelCount || value is not (1 or 2 or 6 or 8)) return;
            _wizardChannelCount = value;
            OnPropertyChanged();
        }
    }

    public bool WizardHasResult
    {
        get => _wizardHasResult;
        private set
        {
            if (_wizardHasResult == value) return;
            _wizardHasResult = value;
            OnPropertyChanged();
        }
    }

    public string WizardExportedPath => _wizardExportedPath;

    public sealed record WizardPeqRow(
        int Index, string FrequencyText, string GainText, string QText);

    public IReadOnlyList<WizardPeqRow> WizardPreviewFilters => _wizardPreviewRows;

    public void ResetWizardState()
    {
        _wizardMeasurementPath = string.Empty;
        _wizardImportedPointCount = 0;
        _wizardHasResult = false;
        _wizardExportedPath = string.Empty;
        _wizardPreviewRows = Array.Empty<WizardPeqRow>();
        _wizardStatus = "尚未載入量測；請先選擇 CSV 或 REW 文字檔";
        OnPropertyChanged(nameof(WizardMeasurementPath));
        OnPropertyChanged(nameof(WizardImportedPointCount));
        OnPropertyChanged(nameof(WizardHasMeasurement));
        OnPropertyChanged(nameof(WizardImportedCountText));
        OnPropertyChanged(nameof(WizardHasResult));
        OnPropertyChanged(nameof(WizardExportedPath));
        OnPropertyChanged(nameof(WizardPreviewFilters));
        OnPropertyChanged(nameof(WizardStatus));
    }

    private static List<double>[] ReadMeasurementFile(string fullPath)
    {
        List<double> frequencies = [];
        List<double> levels = [];
        foreach (var rawLine in File.ReadLines(fullPath))
        {
            var line = rawLine.Trim();
            if (line.Length == 0 ||
                line.StartsWith('*') || line.StartsWith('#') || line.StartsWith(';'))
            {
                continue;
            }
            var parts = line.Split([' ', '\t', ',', ';'],
                StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
            if (parts.Length < 2 ||
                !TryParseInvariantDouble(parts[0], out var frequencyHz) ||
                !TryParseInvariantDouble(parts[1], out var levelDb))
            {
                continue;
            }
            frequencies.Add(frequencyHz);
            levels.Add(levelDb);
            if (frequencies.Count >= CalibrationResponseV1.MaxPoints) break;
        }

        return [frequencies, levels];
    }

    private static bool TryParseInvariantDouble(string token, out double value) =>
        double.TryParse(token, NumberStyles.Float, CultureInfo.InvariantCulture, out value);

    public bool ImportWizardMeasurement(string filePath)
    {
        WizardHasResult = false;
        string fullPath;
        try
        {
            fullPath = Path.GetFullPath(filePath ?? string.Empty);
            var info = new FileInfo(fullPath);
            if (!info.Exists || info.Length is < 1 ||
                info.Length > CalibrationCompilerV1.MaxMeasurementFileBytes)
                throw new InvalidDataException("量測檔不存在或超過大小上限");
        }
        catch (Exception exception) when (exception is ArgumentException or IOException or
                                          UnauthorizedAccessException or InvalidDataException or
                                          NotSupportedException)
        {
            _wizardMeasurementPath = string.Empty;
            _wizardImportedPointCount = 0;
            WizardStatus = $"量測檔無法讀取：{exception.Message}";
            OnPropertyChanged(nameof(WizardMeasurementPath));
            OnPropertyChanged(nameof(WizardImportedPointCount));
            OnPropertyChanged(nameof(WizardHasMeasurement));
            OnPropertyChanged(nameof(WizardImportedCountText));
            return false;
        }

        List<double>[] parsed;
        try
        {
            parsed = ReadMeasurementFile(fullPath);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            _wizardMeasurementPath = string.Empty;
            _wizardImportedPointCount = 0;
            WizardStatus = $"量測檔讀取失敗：{exception.Message}";
            OnPropertyChanged(nameof(WizardMeasurementPath));
            OnPropertyChanged(nameof(WizardImportedPointCount));
            OnPropertyChanged(nameof(WizardHasMeasurement));
            OnPropertyChanged(nameof(WizardImportedCountText));
            return false;
        }

        if (parsed[0].Count == 0)
        {
            _wizardMeasurementPath = string.Empty;
            _wizardImportedPointCount = 0;
            WizardStatus = "沒有可解析的「頻率 量級」資料列；請使用 CSV 或 REW 匯出格式";
            OnPropertyChanged(nameof(WizardMeasurementPath));
            OnPropertyChanged(nameof(WizardImportedPointCount));
            OnPropertyChanged(nameof(WizardHasMeasurement));
            OnPropertyChanged(nameof(WizardImportedCountText));
            return false;
        }

        _wizardMeasurementPath = fullPath;
        _wizardMeasurementFrequencies = [.. parsed[0]];
        _wizardMeasurementLevels = [.. parsed[1]];
        _wizardImportedPointCount = Math.Min(parsed[0].Count, CalibrationResponseV1.MaxPoints);
        WizardStatus = $"已解析 {_wizardImportedPointCount} 個頻率點；尚未編譯校正";
        OnPropertyChanged(nameof(WizardMeasurementPath));
        OnPropertyChanged(nameof(WizardImportedPointCount));
        OnPropertyChanged(nameof(WizardHasMeasurement));
        OnPropertyChanged(nameof(WizardImportedCountText));
        return true;
    }

    public bool CompileWizardCorrection()
    {
        if (_wizardMeasurementPath.Length == 0)
        {
            WizardHasResult = false;
            WizardStatus = "尚未載入量測；請先選擇 CSV 或 REW 文字檔";
            return false;
        }

        var frequencies = _wizardMeasurementFrequencies;
        var levels = _wizardMeasurementLevels;
        if (frequencies is null || levels is null)
        {
            ResetWizardState();
            WizardStatus = "量測資料遺失；請重新選擇量測檔";
            return false;
        }

        if (_wizardMultiChannel)
        {
            var perChannelPoints = new List<CalibrationPointV1[]>();
            for (var channel = 0; channel < _wizardChannelCount; ++channel)
            {
                var channelResponse = CalibrationCompilerV1.BuildTargetedResponse(
                    null, 48000.0, frequencies, levels, _wizardTargetCurve);
                if (channelResponse is null)
                {
                    WizardHasResult = false;
                    WizardStatus = "量測點超出有效範圍或排序錯誤（頻率需遞增、20 Hz–24 kHz）";
                    return false;
                }
                perChannelPoints.Add(channelResponse.Points.ToArray());
            }

            var batch = CalibrationCompilerV1.CompileMultiChannelBatch(
                _wizardChannelCount, perChannelPoints);
            if (!batch.Success)
            {
                WizardHasResult = false;
                WizardStatus = "多聲道編譯失敗：" + batch.Diagnostic;
                return false;
            }

            _wizardCompiledFilters = [.. batch.ChannelFilters[0]];
            _wizardPreviewRows = batch.ChannelFilters[0]
                .Select((filter, index) => new WizardPeqRow(
                    index + 1,
                    $"{filter.FrequencyHz:0.#} Hz",
                    $"{(filter.GainDb >= 0 ? "+" : string.Empty)}{filter.GainDb:0.##} dB",
                    $"Q {filter.Q:0.##}"))
                .ToArray();
            OnPropertyChanged(nameof(WizardPreviewFilters));
            WizardHasResult = true;
            WizardStatus = $"已為 {_wizardChannelCount} 聲道各編譯 {batch.ChannelFilters[0].Count} 個 PEQ 濾波器" +
                           (batch.ChannelLimited.Any(static limited => limited)
                               ? "；部分聲道受限於安全上限"
                               : "") +
                           $"。{batch.Diagnostic}";
            return true;
        }

        var response = CalibrationCompilerV1.BuildTargetedResponse(
            null, 48000.0, frequencies, levels, _wizardTargetCurve);
        if (response is null)
        {
            WizardHasResult = false;
            WizardStatus = "量測點超出有效範圍或排序錯誤（頻率需遞增、20 Hz–24 kHz）";
            return false;
        }

        var compile = CalibrationCompilerV1.CompileBoundedPeqCorrection(response.Points);
        if (compile.Filters.Count == 0 &&
            compile.Diagnostic.Contains("invalid", StringComparison.Ordinal))
        {
            WizardHasResult = false;
            WizardStatus = "校正編譯失敗：量測資料無效";
            return false;
        }

        _wizardCompiledFilters = [.. compile.Filters];
        _wizardPreviewRows = compile.Filters
            .Select((filter, index) => new WizardPeqRow(
                index + 1,
                $"{filter.FrequencyHz:0.#} Hz",
                $"{(filter.GainDb >= 0 ? "+" : string.Empty)}{filter.GainDb:0.##} dB",
                $"Q {filter.Q:0.##}"))
            .ToArray();
        WizardHasResult = true;
        WizardStatus = $"已編譯 {compile.Filters.Count} 個 PEQ 濾波器" +
                       (compile.Limited ? "；部分修正已受限於安全上限" : "") +
                       $"。{compile.Diagnostic}";
        return true;
    }

    public bool ExportWizardProfile(string filePath)
    {
        if (!_wizardHasResult)
        {
            WizardStatus = "尚無校正結果可匯出；請先載入量測並編譯";
            return false;
        }

        if (!CalibrationCompilerV1.TrySavePreset(
                filePath, new PeqPresetV1(1U, _wizardCompiledFilters), out var error))
        {
            _wizardExportedPath = string.Empty;
            OnPropertyChanged(nameof(WizardExportedPath));
            WizardStatus = $"匯出失敗：{error}";
            return false;
        }

        _wizardExportedPath = Path.GetFullPath(filePath);
        WizardStatus = $"校正設定已匯出：{_wizardExportedPath}";
        OnPropertyChanged(nameof(WizardExportedPath));
        return true;
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

    public const int MaxPendingSceneCatalogOps = 64;

    public int PendingSceneCatalogOpsCount => _pendingSceneCatalogOps.Count;
    public int DroppedSceneCatalogOperations => _droppedSceneCatalogOperations;

    // Read-only seam for the control-model check host; it verifies replay
    // payload preservation without exposing mutable queue state.
    internal IReadOnlyList<PendingSceneCatalogOp> PendingSceneCatalogOpTestsOnly =>
        _pendingSceneCatalogOps.ToArray();

    internal sealed record PendingSceneCatalogOp(
        bool IsUpsert, string SceneId, string Name, string OutputGroup,
        string IrReference = "",
        bool LoudnessLiveUpdate = false);

    public string CustomSceneQueuePath
    {
        get => _customSceneQueuePath;
        set
        {
            var normalized = value?.Trim() ?? string.Empty;
            if (normalized == _customSceneQueuePath) return;
            if (string.IsNullOrWhiteSpace(normalized))
                throw new ArgumentException("Custom scene queue path cannot be empty.");
            _customSceneQueuePath = normalized;
        }
    }

    public bool UpsertCustomScene(SceneCard scene)
    {
        if (!_session.CustomScenes.Upsert(scene)) return false;
        OnPropertyChanged(nameof(Scenes));
        OnPropertyChanged(nameof(CustomSceneCards));
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

    public async Task<bool> AddCustomSceneAsync(
        CancellationToken cancellationToken = default)
    {
        return await AddCustomSceneCoreAsync(
            new SceneCard(CustomSceneId.Trim(), CustomSceneName.Trim(),
                          CustomSceneDescription.Trim(), "平衡", true,
                          LoudnessLiveUpdate: CustomSceneLoudnessLiveUpdate),
            cancellationToken).ConfigureAwait(true);
    }

    public async Task<bool> AddCustomSceneAsync(SceneCard scene,
                                                CancellationToken cancellationToken = default)
    {
        return await AddCustomSceneCoreAsync(scene, cancellationToken)
            .ConfigureAwait(true);
    }

    private async Task<bool> AddCustomSceneCoreAsync(SceneCard scene,
                                                     CancellationToken cancellationToken)
    {
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
            OnPropertyChanged(nameof(CustomSceneCards));
            StatusText = $"自訂場景未保存：{saveError}";
            return false;
        }
        if (!IsConnected &&
            !TryPersistOfflineSceneOp(new PendingSceneCatalogOp(
                true, scene.Id, scene.Name, _session.ActiveOutputGroup ?? "main",
                scene.IrReference, scene.LoudnessLiveUpdate)))
        {
            _session.CustomScenes.Remove(scene.Id);
            if (previous is not null) _session.CustomScenes.Upsert(previous);
            OnPropertyChanged(nameof(Scenes));
            OnPropertyChanged(nameof(CustomSceneCards));
            return false;
        }
        CustomSceneId = string.Empty;
        CustomSceneName = string.Empty;
        CustomSceneDescription = string.Empty;
        CustomSceneLoudnessLiveUpdate = false;

        if (IsConnected)
        {
            var sent = await SendSceneCatalogCommandAsync(
                _commands.UpsertSceneCatalog(scene.Id, scene.Name,
                    _session.ActiveOutputGroup ?? "main", scene.IrReference,
                    scene.LoudnessLiveUpdate),
                cancellationToken).ConfigureAwait(true);
            if (!sent)
            {
                EnqueueSceneCatalogOp(new PendingSceneCatalogOp(
                    true, scene.Id, scene.Name, _session.ActiveOutputGroup ?? "main",
                    scene.IrReference, scene.LoudnessLiveUpdate));
                StatusText = $"已加入自訂場景：{scene.Name}；同步未完成，連線恢復後自動重試";
                return false;
            }
            StatusText = $"已加入自訂場景：{scene.Name}；引擎已同步";
            return true;
        }

        ReportSceneCatalogStatus(
            $"已加入自訂場景：{scene.Name}（離線；連線後自動同步）");
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
        OnPropertyChanged(nameof(SessionCatalogSequenceDisplayText));
        OnPropertyChanged(nameof(SelectedSession));
        OnPropertyChanged(nameof(SelectedSessionDisplayText));
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
        OnPropertyChanged(nameof(SessionVolumeDisplayText));
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
        // The dose indicator is a pure observer of confirmed volume state:
        // rejection or failure above never reaches this line.
        ListeningDose.AddSample(DateTimeOffset.UtcNow, state.EffectiveDb,
                                state.Muted);
        OnPropertyChanged(nameof(ListeningDose));
        OnPropertyChanged(nameof(RequestedVolumeDb));
        OnPropertyChanged(nameof(RequestedVolumeDisplayText));
        OnPropertyChanged(nameof(Muted));
        OnPropertyChanged(nameof(VolumeState));
        OnPropertyChanged(nameof(EffectiveVolumeDb));
        OnPropertyChanged(nameof(EffectiveVolumeDisplayText));
        OnPropertyChanged(nameof(SafetyCeilingDb));
        OnPropertyChanged(nameof(SafetyStatusText));
        OnPropertyChanged(nameof(VolumeOriginText));
        OnPropertyChanged(nameof(VolumeActuatorText));
        OnPropertyChanged(nameof(VolumeGeneration));
        return true;
    }

    // Applies a confirmed engine visual frame. Invalid or out-of-order frames
    // fail closed and preserve the previous surface exactly.
    public bool ApplyEqVisualFrame(EqVisualFrameV1 frame, out string error)
    {
        error = string.Empty;
        if (frame is null || !frame.IsValid)
        {
            error = "EQ visual frame invalid";
            return false;
        }
        if (frame.Sequence <= EqSurface.LastAppliedSequence && EqSurface.HasConfirmedFrame)
        {
            error = "EQ visual frame stale";
            return false;
        }
        if (!EqSurface.ApplyFrame(frame))
        {
            error = "EQ visual transition rejected";
            return false;
        }
        OnPropertyChanged(nameof(EqSurface));
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

    // Pulls the latest engine-published EQ visual frame after a control-plane
    // change. A missing or malformed frame is intentionally quiet: the EQ is
    // optional UI evidence and must not overwrite the previous safe surface.
    public async Task<bool> RefreshEqVisualSnapshotAsync(
        CancellationToken cancellationToken = default)
    {
        if (_controlClient is null || !_controlClient.IsConnected) return false;
        var gateHeld = false;
        try
        {
            await _commandGate.WaitAsync(cancellationToken).ConfigureAwait(true);
            gateHeld = true;
            SetBusy(true);
            var reply = await _controlClient.RoundTripAsync(
                _commands.RequestEqVisualSnapshot(), cancellationToken)
                .ConfigureAwait(true);
            return ApplyEqVisualSnapshot(reply, out _);
        }
        catch (OperationCanceledException)
        {
            return false;
        }
        catch (Exception)
        {
            return false;
        }
        finally
        {
            SetBusy(false);
            if (gateHeld) _commandGate.Release();
        }
    }

    private bool ApplyEqVisualSnapshot(IpcEnvelopeV1 reply, out string error)
    {
        error = string.Empty;
        if (reply.Type != ControlMessageType.EqVisualSnapshot ||
            !ControlPayloadsV1.TryDecodeEqVisualSnapshot(reply.Payload.Span,
                                                          out var sequence,
                                                          out var source,
                                                          out var points))
        {
            error = "EQ visual snapshot format invalid";
            return false;
        }

        var frame = new EqVisualFrameV1(sequence, source, points);
        if (!ApplyEqVisualFrame(frame, out error)) return false;
        OnPropertyChanged(nameof(EqSurface));
        return true;
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

    // Product-surface wrapper around the bounded IPC refresh. The UI never
    // enumerates Windows endpoints itself; failures preserve the prior picker.
    public async Task<bool> RefreshPhysicalDevicePickerAsync(
        CancellationToken cancellationToken = default)
    {
        if (!IsConnected)
        {
            StatusText = "引擎未連線；無法重新掃描裝置清單";
            return false;
        }

        StatusText = "正在重新掃描實體輸出裝置…";
        var refreshed = await RefreshPhysicalDevicesAsync(cancellationToken).ConfigureAwait(true);
        if (refreshed)
            StatusText = $"已重新掃描裝置清單：{PhysicalDevices.Count} 個 Active render 裝置";
        return refreshed;
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
        if (loaded && !LoadPendingSceneCatalogOps(out var queueError))
        {
            error = $"場景卡片已載入，但同步佇列載入失敗：{queueError}";
            return false;
        }
        if (loaded)
        {
            OnPropertyChanged(nameof(Scenes));
            OnPropertyChanged(nameof(CustomSceneCards));
            OnPropertyChanged(nameof(PendingSceneCatalogOpsCount));
            OnPropertyChanged(nameof(DroppedSceneCatalogOperations));
        }
        return loaded;
    }

    public bool SaveCustomScenes(out string error) =>
        _session.CustomScenes.TrySave(CustomSceneCatalogPath, out error);

    private bool SavePendingSceneCatalogOps()
    {
        var queue = new CustomSceneSyncQueueV1();

        foreach (var operation in _pendingSceneCatalogOps)
        {
            if (!queue.Enqueue(new SceneCatalogQueueCard(
                    operation.IsUpsert, operation.SceneId, operation.Name,
                    operation.OutputGroup, operation.IrReference,
                    operation.LoudnessLiveUpdate)))
            {
                return false;
            }
        }

        return queue.TrySave(
            CustomSceneQueuePath,
            _droppedSceneCatalogOperations,
            out _);
    }

    // An offline scene edit is only accepted when both the visible card and
    // its replay operation survive a crash. If the queue file cannot be
    // saved, restore the exact previous bounded queue and let the caller
    // roll back the card change.
    private bool TryPersistOfflineSceneOp(PendingSceneCatalogOp operation)
    {
        var previousOps = _pendingSceneCatalogOps.ToArray();
        var previousDropped = _droppedSceneCatalogOperations;

        EnqueueSceneCatalogOp(operation);
        if (SavePendingSceneCatalogOps()) return true;

        _pendingSceneCatalogOps.Clear();
        foreach (var item in previousOps) _pendingSceneCatalogOps.Enqueue(item);
        _droppedSceneCatalogOperations = previousDropped;
        StatusText = "離線場景同步佇列保存失敗；變更未套用，音訊保持安全狀態";
        return false;
    }

    public bool LoadPendingSceneCatalogOps(out string error)
    {
        var queue = new CustomSceneSyncQueueV1();
        if (!queue.TryLoad(CustomSceneQueuePath, out var droppedCount, out error))
            return false;

        _pendingSceneCatalogOps.Clear();
        foreach (var operation in queue.Operations)
        {
            _pendingSceneCatalogOps.Enqueue(new PendingSceneCatalogOp(
                operation.IsUpsert, operation.SceneId, operation.Name,
                operation.OutputGroup, operation.IrReference,
                operation.LoudnessLiveUpdate));
        }
        _droppedSceneCatalogOperations = droppedCount;
        return true;
    }

    public bool LoadRouteRules(out string error)
    {
        var loaded = _session.RouteRules.TryLoad(RouteRuleCatalogPath, out error);
        if (loaded) OnPropertyChanged(nameof(RouteRules));
        return loaded;
    }

    public bool SaveRouteRules(out string error) =>
        _session.RouteRules.TrySave(RouteRuleCatalogPath, out error);

    public async Task<bool> RemoveCustomSceneAsync(
        string sceneId, CancellationToken cancellationToken = default)
    {
        var previous = _session.CustomScenes.Scenes.FirstOrDefault(
            item => item.Id == sceneId?.Trim());
        if (previous is null)
        {
            StatusText = "找不到這個自訂場景";
            return false;
        }

        var previousSelection = _selectedScene;
        var removesSelection = previousSelection?.Id == previous.Id;
        if (!_session.CustomScenes.Remove(previous.Id))
        {
            StatusText = "自訂場景移除失敗";
            return false;
        }

        if (removesSelection) _selectedScene = null;
        OnPropertyChanged(nameof(Scenes));
        OnPropertyChanged(nameof(CustomSceneCards));
        OnPropertyChanged(nameof(SelectedScene));

        if (!SaveCustomScenes(out var saveError))
        {
            _session.CustomScenes.Upsert(previous);
            _selectedScene = previousSelection;
            OnPropertyChanged(nameof(Scenes));
            OnPropertyChanged(nameof(CustomSceneCards));
            OnPropertyChanged(nameof(SelectedScene));
            StatusText = $"自訂場景未移除：{saveError}";
            return false;
        }

        StatusText = $"已移除自訂場景：{previous.Name}";
        if (IsConnected)
        {
            var sent = await SendSceneCatalogCommandAsync(
                _commands.RemoveSceneCatalog(previous.Id), cancellationToken)
                .ConfigureAwait(true);
            if (!sent) return false;
            StatusText = $"已移除自訂場景：{previous.Name}；引擎已同步";
        }
        else
        {
            if (!TryPersistOfflineSceneOp(new PendingSceneCatalogOp(
                    false, previous.Id, "", "")))
            {
                _session.CustomScenes.Upsert(previous);
                _selectedScene = previousSelection;
                OnPropertyChanged(nameof(Scenes));
                OnPropertyChanged(nameof(CustomSceneCards));
                OnPropertyChanged(nameof(SelectedScene));
                return false;
            }
            ReportSceneCatalogStatus(
                $"已移除自訂場景：{previous.Name}（離線；連線後自動同步）");
        }
        return true;
    }

    private bool EnqueueSceneCatalogOp(PendingSceneCatalogOp operation)
    {
        var droppedNow = false;
        while (_pendingSceneCatalogOps.Count >= MaxPendingSceneCatalogOps)
        {
            _pendingSceneCatalogOps.Dequeue();
            _droppedSceneCatalogOperations++;
            droppedNow = true;
        }
        _pendingSceneCatalogOps.Enqueue(operation);
        if (droppedNow)
        {
            StatusText =
                $"離線場景同步佇列已滿；已捨棄最舊的 {_droppedSceneCatalogOperations} 筆變更以維持有界容量";
        }
        return droppedNow;
    }

    // Dropping the oldest queued op is a durable user-visible fact for this
    // control-model lifetime. Later success messages may complete, but they
    // must not silently erase the earlier capacity loss.
    private void ReportSceneCatalogStatus(string status)
    {
        StatusText = _droppedSceneCatalogOperations == 0 ||
                     status.Contains("捨棄最舊", StringComparison.Ordinal)
            ? status
            : $"{status}；離線場景同步佇列已滿，已捨棄最舊的 {_droppedSceneCatalogOperations} 筆變更";
    }

    // Scene catalog sync is honest by construction: the engine must Ack the
    // exact request before any UI text claims the card reached the engine.
    private async Task<bool> SendSceneCatalogCommandAsync(IpcEnvelopeV1 command,
                                                          CancellationToken cancellationToken)
    {
        LastCommand = command;
        OnPropertyChanged(nameof(LastCommand));
        var sent = await SendCommandAsync(() => command, cancellationToken)
            .ConfigureAwait(true);
        return sent && IsConnected;
    }

    private async Task<bool> FlushPendingSceneCatalogOpsAsync(
        CancellationToken cancellationToken)
    {
        var total = _pendingSceneCatalogOps.Count;
        while (_pendingSceneCatalogOps.Count > 0)
        {
            var operation = _pendingSceneCatalogOps.Peek();
            IpcEnvelopeV1 command;
            try
            {
                command = operation.IsUpsert
                    ? _commands.UpsertSceneCatalog(operation.SceneId, operation.Name,
                          operation.OutputGroup, operation.IrReference,
                          operation.LoudnessLiveUpdate)
                    : _commands.RemoveSceneCatalog(operation.SceneId);
            }
            catch (ArgumentException)
            {
                _pendingSceneCatalogOps.Dequeue();
                SavePendingSceneCatalogOps();
                continue;
            }
            if (!await SendSceneCatalogCommandAsync(command, cancellationToken)
                    .ConfigureAwait(true))
            {
                ReportSceneCatalogStatus(
                    "離線場景同步未完成；保留佇列，稍後重新連線再試");
                return false;
            }
            _pendingSceneCatalogOps.Dequeue();
            SavePendingSceneCatalogOps();
        }
        if (total > 0)
            ReportSceneCatalogStatus(
                $"離線場景變更已補送（{total} 筆）；引擎已同步");
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
            if (IsConnected)
                _ = await RefreshEqVisualSnapshotAsync(cancellationToken).ConfigureAwait(true);
            StartEqVisualPolling();
            if (IsConnected && !await RefreshSessionCatalogAsync(cancellationToken).ConfigureAwait(true))
                StatusText = "引擎已連線；App 清單暫不可用，音訊保持安全狀態";
            if (IsConnected && _pendingSceneCatalogOps.Count > 0)
            {
                if (!await FlushPendingSceneCatalogOpsAsync(cancellationToken)
                        .ConfigureAwait(true))
                    ReportSceneCatalogStatus(
                        "引擎已連線；離線期間的場景變更尚未全部同步，音訊保持安全狀態");
            }
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
        StopEqVisualPolling();
        if (_controlClient is not null)
        {
            await _controlClient.DisposeAsync().ConfigureAwait(true);
            _controlClient = null;
        }
        SetConnectionState(ControlConnectionState.Disconnected);
        _ = EqSurface.Reset();
        OnPropertyChanged(nameof(EqSurface));
    }

    // Bounded periodic EQ visual polling keeps the modern-equalizer surface
    // current while content-driven adaptive correction changes the applied
    // curve. The loop lives only for the connected lifetime: disconnect
    // cancels it, and a failed poll leaves the previous confirmed frame in
    // place instead of corrupting the safe visual state.
    private void StartEqVisualPolling()
    {
        if (_eqPollCts is not null) return;
        _eqPollCts = new CancellationTokenSource();
        var token = _eqPollCts.Token;
        _eqPollLoop = Task.Run(async () =>
        {
            while (!token.IsCancellationRequested)
            {
                try
                {
                    await Task.Delay(EqPollIntervalMs, token).ConfigureAwait(true);
                }
                catch (OperationCanceledException)
                {
                    return;
                }
                if (_controlClient is null) return;
                try
                {
                    await RefreshEqVisualSnapshotAsync(token).ConfigureAwait(true);
                }
                catch
                {
                    // Silent by contract: poll failures never break the last
                    // confirmed safe frame and never crash the control plane.
                }
            }
        }, CancellationToken.None);
    }

    private void StopEqVisualPolling()
    {
        _eqPollCts?.Cancel();
        _eqPollCts?.Dispose();
        _eqPollCts = null;
        _eqPollLoop = null;
    }

    public async Task<bool> OneTapEnhanceAsync(CancellationToken cancellationToken = default)
    {
        if (!OneTapEnhance()) return false;
        var sent = await SendLastCommandAsync(cancellationToken).ConfigureAwait(true);
        if (sent) MarkIrClearedByScene();
        if (sent && IsConnected)
            _ = await RefreshControlStatusAsync(cancellationToken).ConfigureAwait(true);
        if (sent && IsConnected)
            _ = await RefreshEqVisualSnapshotAsync(cancellationToken).ConfigureAwait(true);
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
        if (sent && IsConnected)
            _ = await RefreshEqVisualSnapshotAsync(cancellationToken).ConfigureAwait(true);
        return sent;
    }

    public async Task<bool> PushVolumeAsync(CancellationToken cancellationToken = default)
    {
        var sent = await SendCommandAsync(BuildVolumeCommand, cancellationToken).ConfigureAwait(true);
        if (sent && IsConnected)
            _ = await RefreshControlStatusAsync(cancellationToken).ConfigureAwait(true);
        if (sent && IsConnected)
            _ = await RefreshEqVisualSnapshotAsync(cancellationToken).ConfigureAwait(true);
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
                ? "命令已完成"
                : $"已完成 {_selectedScene.Name} 於 {_selectedOutputGroup}";
            LastSendDiagnostics = string.Empty;
            return true;
        }
        catch (OperationCanceledException)
        {
            LastSendDiagnostics = "operation-canceled";
            StatusText = "命令已取消；保留上一個安全狀態";
            return false;
        }
        catch (Exception exception)
        {
            // #278: transient ack failures surface only as `false` today; keep the
            // swallowed exception observable so the intermittent scene-switch flake
            // can be root-caused instead of guessed at.
            LastSendDiagnostics = $"{exception.GetType().Name}: {exception.Message}";
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
        OnPropertyChanged(nameof(EffectiveVolumeDisplayText));
        OnPropertyChanged(nameof(SafetyCeilingDb));
        OnPropertyChanged(nameof(SafetyStatusText));
        OnPropertyChanged(nameof(VolumeOriginText));
        OnPropertyChanged(nameof(VolumeActuatorText));
        OnPropertyChanged(nameof(VolumeGeneration));
    }
}
