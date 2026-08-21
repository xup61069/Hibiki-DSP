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
    private IrPhaseMode _irPhaseMode = IrPhaseMode.MinimumPhase;
    private double _irPhaseStrength;
    private ControlConnectionState _connectionState = ControlConnectionState.Disconnected;
    private bool _isBusy;
    private CancellationTokenSource? _volumeDebounce;
    private string _customSceneId = string.Empty;
    private string _customSceneName = string.Empty;
    private string _customSceneDescription = string.Empty;

    public ExpertSurfaceModel Expert { get; } = new();

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
        }
    }

    public IrPhasePolicyV1 IrPhasePolicy => new(IrPhaseMode, IrPhaseStrength);
    public double IrAddedDelayMs => IrPhasePolicy.AddedDelayMs;

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
        return await SendLastCommandAsync(cancellationToken).ConfigureAwait(true);
    }

    public async Task<bool> SelectSceneAsync(string sceneId,
                                              CancellationToken cancellationToken = default)
    {
        if (!SelectScene(sceneId)) return false;
        return await SendLastCommandAsync(cancellationToken).ConfigureAwait(true);
    }

    public async Task<bool> PushVolumeAsync(CancellationToken cancellationToken = default)
    {
        return await SendCommandAsync(BuildVolumeCommand, cancellationToken).ConfigureAwait(true);
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
