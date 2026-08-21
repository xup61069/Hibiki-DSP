using Hibiki.ControlModel;

static void Check(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

Check(ScenePresetCatalog.EasyDefaults.Count == 4, "Expected four Easy defaults.");
Check(ScenePresetCatalog.EasyDefaults[0].Id == "game", "Game preset missing.");
var customScenes = new CustomSceneCatalogV1();
Check(customScenes.Upsert(new SceneCard("quiet-game", "安靜遊戲", "遊戲音量保護",
                                         "零額外緩衝", true)) && customScenes.Count == 1,
    "Custom Scene card was not accepted.");
Check(!customScenes.Upsert(new SceneCard("game", "覆寫遊戲", "", "", true)) &&
      !customScenes.Upsert(new SceneCard("Bad ID", "錯誤", "", "", true)),
    "Custom Scene catalog must reject reserved or invalid IDs.");
Check(customScenes.Remove("quiet-game") && customScenes.Count == 0,
    "Custom Scene card removal failed.");
var physicalDevices = new PhysicalDeviceCatalogV1();
var speakers = new PhysicalDeviceCard("endpoint-a", "客廳喇叭",
    PhysicalDeviceFlowV1.Render, PhysicalDeviceAvailabilityV1.Active, 8, 48000, 128, true, 10UL);
Check(physicalDevices.Upsert(speakers, out _) && physicalDevices.DefaultRender?.EndpointId == "endpoint-a" &&
      physicalDevices.Devices[0].IsSelectable, "Physical device catalog insert failed.");
var headphones = speakers with
{
    EndpointId = "endpoint-b", DisplayName = "耳機", Channels = 2, IsDefault = true, LastSequence = 11UL
};
Check(physicalDevices.Upsert(headphones, out _) && physicalDevices.DefaultRender?.EndpointId == "endpoint-b" &&
      !physicalDevices.Devices[0].IsDefault, "Physical device default uniqueness failed.");
Check(physicalDevices.SetAvailability("endpoint-b", PhysicalDeviceAvailabilityV1.Unplugged, 12UL, out _) &&
      physicalDevices.DefaultRender is null &&
      !physicalDevices.SetAvailability("endpoint-b", PhysicalDeviceAvailabilityV1.Active, 9UL, out _),
    "Physical device stale/unplugged guard failed.");
Check(physicalDevices.SetAvailability("endpoint-b", PhysicalDeviceAvailabilityV1.Active, 13UL, out _) &&
      physicalDevices.MarkDefault("endpoint-b", 14UL, out _),
    "Physical device recovery to Active failed.");
var invalidDefault = speakers with
{
    EndpointId = "endpoint-c", Availability = PhysicalDeviceAvailabilityV1.Unplugged
};
Check(!physicalDevices.Upsert(invalidDefault, out _),
    "Unplugged device must not be accepted as default.");
var customScenePath = Path.Combine(Path.GetTempPath(), $"hibiki-scene-check-{Guid.NewGuid():N}.json");
try
{
    Check(customScenes.Upsert(new SceneCard("quiet-game", "安靜遊戲", "遊戲音量保護",
                                             "零額外緩衝", true)) &&
          customScenes.TrySave(customScenePath, out _),
        "Custom Scene catalog save failed.");
    var loadedScenes = new CustomSceneCatalogV1();
    Check(loadedScenes.TryLoad(customScenePath, out _) && loadedScenes.Count == 1 &&
          loadedScenes.Scenes[0].Id == "quiet-game",
        "Custom Scene catalog load failed.");
    File.WriteAllText(customScenePath,
        "{\"schema_version\":1,\"scenes\":[{\"id\":\"Bad ID\",\"name\":\"x\",\"description\":\"\",\"latency_label\":\"\",\"safety_enabled\":true}]}");
    Check(!loadedScenes.TryLoad(customScenePath, out _) && loadedScenes.Count == 1,
        "Invalid custom Scene load must preserve the previous catalog.");
}
finally
{
    if (File.Exists(customScenePath)) File.Delete(customScenePath);
}
Check(OutputGroupCatalog.Fixed.Count == 3 &&
      OutputGroupCatalog.Fixed.Select(group => group.Id).SequenceEqual(
          ["main", "low-latency", "surround"]),
    "Fixed output-group catalog changed unexpectedly.");
var snapshot = new ControlSnapshot(UiMode.Easy, AudioControlStatus.Controlled, "main", -8.5, -8.5, false, null, null);
Check(snapshot.DisplayVolume == "-8.5 dB", "dB display must use the effective value.");
var device = new DeviceSwitchModel();
Check(device.Prepare("endpoint-a") && device.State == DeviceSwitchModel.SwitchState.Preparing,
    "Device A prepare state failed.");
Check(device.MarkPrepared() && device.State == DeviceSwitchModel.SwitchState.Fading,
    "Device A warm-up state failed.");
Check(device.MarkCrossfadeComplete() && device.CanCommit && device.Commit(),
    "Device A crossfade commit failed.");
Check(device.Prepare("endpoint-b"), "Device B prepare failed.");
Check(!device.Commit(), "Device B must not commit before crossfade.");
device.Rollback();
Check(device.ActiveDevice == "endpoint-a" &&
      device.State == DeviceSwitchModel.SwitchState.RolledBack,
    "Rollback replaced the active endpoint.");
var session = new EasyControlSession();
var blockedEnhance = session.OneTapEnhance(null);
Check(!blockedEnhance.Succeeded && blockedEnhance.Status == AudioControlStatus.Degraded,
    "Enhance must fail closed without an output group.");
var enhanced = session.OneTapEnhance("main");
Check(enhanced.Succeeded && enhanced.Scene?.Id == "game" &&
      enhanced.Status == AudioControlStatus.Controlled &&
      session.ActiveOutputGroup == "main", "One-tap enhance did not control the scene/output.");
session.SetMode(UiMode.Expert);
Check(session.Mode == UiMode.Expert && session.SelectScene("movie"),
    "Expert scene selection failed.");
var ipcRequest = new IpcRequestSession().Create(ControlMessageType.Hello);
var ipcBytes = IpcCodecV1.Encode(ipcRequest);
Check(IpcCodecV1.TryDecode(ipcBytes, out var decodedIpc, out var ipcError) &&
      ipcError == IpcDecodeError.None && decodedIpc?.Type == ControlMessageType.Hello &&
      decodedIpc.RequestId == ipcRequest.RequestId,
    "C# IPC envelope round-trip failed.");
var knownHello = new byte[] {
    0x48, 0x49, 0x4B, 0x31, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x2A, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};
Check(IpcCodecV1.TryDecode(knownHello, out var knownEnvelope, out _ ) &&
      knownEnvelope?.RequestId == 42UL &&
      knownEnvelope.Type == ControlMessageType.Hello,
    "C# IPC bytes are not compatible with the C++ envelope.");
var malformedIpc = knownHello[..^1];
Check(!IpcCodecV1.TryDecode(malformedIpc, out _, out var malformedError) &&
      malformedError == IpcDecodeError.Truncated,
    "C# IPC decoder must reject truncated payloads.");
await using var pipeClient = new NamedPipeControlClientV1();
Check(!pipeClient.IsConnected, "Control pipe client must start disconnected.");
var pipeNameRejected = false;
try
{
    _ = new NamedPipeControlClientV1("bad/name");
}
catch (ArgumentException)
{
    pipeNameRejected = true;
}
Check(pipeNameRejected, "Control pipe client must reject path-like names.");
var commandFactory = new ControlCommandFactoryV1();
var volumeCommand = commandFactory.SetVolume(-6.0205999, true, 9UL);
Check(volumeCommand.Type == ControlMessageType.VolumeNotification &&
      ControlPayloadsV1.TryDecodeVolumeNotification(volumeCommand.Payload.Span,
          out var commandDb, out var commandMute, out var commandGeneration) &&
      Math.Abs(commandDb + 6.020599365234375) < 1e-6 && commandMute && commandGeneration == 9UL,
    "Control volume payload did not round-trip with the v1 contract.");
var groupedVolumeCommand = commandFactory.SetVolume(-9.0, false, 10UL, "movie");
Check(ControlPayloadsV1.TryDecodeGroupedVolumeNotification(groupedVolumeCommand.Payload.Span,
          out var groupedOutput, out var groupedDb, out var groupedMute, out var groupedGeneration) &&
      groupedOutput == "movie" && Math.Abs(groupedDb + 9.0) < 1e-6 && !groupedMute &&
      groupedGeneration == 10UL,
    "Grouped volume payload did not round-trip with the v1 contract.");
Check(IpcRequestSession.IsReplyTo(volumeCommand,
        new IpcEnvelopeV1(ControlMessageType.Ack, volumeCommand.RequestId, ReadOnlyMemory<byte>.Empty)),
    "Control command request correlation failed.");
var viewModel = new EasyControlViewModel { SelectedOutputGroup = " main " };
Check(viewModel.ConnectionState == ControlConnectionState.Disconnected &&
      !viewModel.IsConnected && !viewModel.IsBusy,
    "ViewModel must start disconnected and idle.");
Check(viewModel.OneTapEnhance() && viewModel.SelectedScene?.Id == "game" &&
      viewModel.Status == AudioControlStatus.Controlled &&
      viewModel.SelectedOutputGroup == "main" && viewModel.LastCommand?.Type == ControlMessageType.SceneApply,
    "ViewModel One-Tap Enhance should control the selected output.");
Check(viewModel.UpsertCustomScene(new SceneCard("quiet-game", "安靜遊戲", "遊戲音量保護",
                                                "零額外緩衝", true)) &&
      viewModel.Scenes.Count == 5 && viewModel.SelectScene("quiet-game") &&
      viewModel.SelectedScene?.Id == "quiet-game",
    "ViewModel custom Scene selection failed.");
Check(ControlPayloadsV1.TryDecodeSceneApply(viewModel.LastCommand!.Payload.Span,
          out var selectedSceneId, out var selectedOutput) &&
      selectedSceneId == "quiet-game" && selectedOutput == "main",
    "ViewModel scene command payload was not valid.");
var encodedSceneCommand = IpcCodecV1.Encode(viewModel.LastCommand);
Check(IpcCodecV1.TryDecode(encodedSceneCommand, out var decodedSceneCommand, out _) &&
      decodedSceneCommand?.Type == ControlMessageType.SceneApply,
    "SceneApply must be accepted by the C# envelope encoder.");
var invalidUtf8Scene = viewModel.LastCommand.Payload.ToArray();
invalidUtf8Scene[1] = 0xFF;
Check(!ControlPayloadsV1.TryDecodeSceneApply(invalidUtf8Scene, out _, out _),
    "SceneApply decoder must reject invalid UTF-8 rather than substitute characters.");
Check(viewModel.UpsertPhysicalDevice(speakers, out _) &&
      viewModel.SelectPhysicalDevice("endpoint-a") &&
      viewModel.SelectedPhysicalDevice?.DisplayName == "客廳喇叭" &&
      viewModel.LastCommand?.Type == ControlMessageType.DeviceSwitch,
    "ViewModel physical device selection command failed.");
Check(ControlPayloadsV1.TryDecodeDeviceSwitch(viewModel.LastCommand!.Payload.Span,
          out var selectedEndpoint, out var selectedChannels, out var selectedRate,
          out var selectedFrames, out var selectedSequence) &&
      selectedEndpoint == "endpoint-a" && selectedChannels == 8 && selectedRate == 48000 &&
      selectedFrames == 128 && selectedSequence == 10UL,
    "Physical device switch payload did not round-trip.");
viewModel.IsExpert = true;
Check(viewModel.Mode == UiMode.Expert && viewModel.SelectScene("movie"),
    "ViewModel Expert scene selection failed.");
Check(viewModel.Expert.IsVisible && viewModel.Expert.MatrixRoutes.Count == 4 &&
      viewModel.Expert.DspGraph.Any(node => node.Id == "limiter" && node.Enabled) &&
      viewModel.Expert.Vst3Lanes.All(lane => !lane.Trusted) &&
      viewModel.Expert.Calibration.Mode == "Relative Compensation" &&
      viewModel.Expert.StatusText.Contains("唯讀"),
    "Expert surface must expose bounded read-only graph details.");
viewModel.IsExpert = false;
Check(!viewModel.Expert.IsVisible && viewModel.Expert.StatusText.Contains("隱藏"),
    "Expert surface must hide when Easy mode is selected.");
viewModel.RequestedVolumeDb = -6.0205999;
viewModel.Muted = true;
var viewModelVolume = viewModel.BuildVolumeCommand();
Check(viewModelVolume.Type == ControlMessageType.VolumeNotification &&
      viewModelVolume.RequestId > 1,
    "ViewModel volume command was not generated.");
viewModel.IrPhaseMode = IrPhaseMode.LinearPhase;
viewModel.IrPhaseStrength = 0.5;
Check(viewModel.IrPhasePolicy.IsValid && viewModel.IrPhasePolicy.UsesFir &&
      Math.Abs(viewModel.IrAddedDelayMs - 80.0) < 1e-9,
    "ViewModel IR phase slider did not resolve the linear-phase delay.");
viewModel.IrPhaseMode = IrPhaseMode.Bypass;
Check(viewModel.IrPhaseStrength == 0.0 && viewModel.IrAddedDelayMs == 0.0,
    "IR phase bypass must clear the slider and added delay.");
var noEngine = new EasyControlViewModel("HibikiDSP_v1_control_model_check_missing");
var connectedToMissingEngine = await noEngine.ConnectAsync(TimeSpan.FromMilliseconds(50));
Check(!connectedToMissingEngine && noEngine.ConnectionState == ControlConnectionState.Degraded &&
      noEngine.StatusText.Contains("找不到 Hibiki"),
    "Missing engine must fail closed with a bounded degraded status.");
Check(!await noEngine.OneTapEnhanceAsync(),
    "One-Tap command must not be reported as applied while disconnected.");
Check(!await noEngine.QueueVolumeAsync(TimeSpan.FromMilliseconds(1)),
    "Disconnected volume debounce must fail closed.");
var invalidDebounceRejected = false;
try
{
    await noEngine.QueueVolumeAsync(TimeSpan.FromSeconds(2));
}
catch (ArgumentOutOfRangeException)
{
    invalidDebounceRejected = true;
}
Check(invalidDebounceRejected, "Volume debounce must enforce a bounded control interval.");
Console.WriteLine("Control model checks passed.");
