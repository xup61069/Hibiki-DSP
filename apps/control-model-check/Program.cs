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
var routeRules = new SessionRouteRuleCatalogV1();
var quietRule = new SessionRouteRuleCard(
    "quiet-game", 20, true, SessionRouteRuleGainOwnerV1.WindowsSession, 3.5,
    "game.exe", "DJMAX", "game", "surround");
Check(routeRules.Upsert(quietRule) && routeRules.Count == 1 &&
      routeRules.Rules[0].Summary.Contains("DJMAX") &&
      routeRules.Rules[0].Summary.Contains("surround"),
    "Session route rule catalog insert failed.");
Check(routeRules.Upsert(quietRule with { Priority = 40 }) && routeRules.Count == 1 &&
      routeRules.Rules[0].Priority == 40 &&
      routeRules.Upsert(quietRule with { RuleId = "music", Priority = 10 }) &&
      routeRules.Rules[0].RuleId == "quiet-game" &&
      routeRules.Upsert(quietRule with { RuleId = "music", Priority = 50 }) &&
      routeRules.Rules[0].RuleId == "music" &&
      !routeRules.Upsert(quietRule with { RuleId = "Bad ID" }) &&
      !routeRules.Upsert(quietRule with { AppId = "", DisplayName = "" }) &&
      !routeRules.Upsert(quietRule with { LaneId = "" }),
    "Session route rule catalog validation/update failed.");
Check(routeRules.Remove("music") && routeRules.Count == 1,
    "Session route rule catalog cleanup failed.");
var previewSession = new SessionCatalogEntryV1(
    100UL, true, SessionCatalogRouteStateV1.Ready, true, -8.0, false,
    "DJMAX", "game.exe", "old-lane", "main");
Check(routeRules.TryResolve(previewSession, out var resolvedRule) ==
          SessionRouteRuleResolutionV1.Applied && resolvedRule?.RuleId == "quiet-game",
    "Session route rule resolver did not match App ID and display name.");
Check(routeRules.TryResolve(previewSession with { AppId = "other.exe", Name = "Other" },
                            out _) == SessionRouteRuleResolutionV1.NoMatch,
    "Session route rule resolver must reject non-matching sessions.");
Check(routeRules.Upsert(quietRule with { RuleId = "quiet-game-2", Priority = 40 }) &&
      routeRules.TryResolve(previewSession, out _) == SessionRouteRuleResolutionV1.Ambiguous &&
      routeRules.Remove("quiet-game-2"),
    "Equal-priority route rule matches must fail closed as ambiguous.");
var routeRulePath = Path.Combine(Path.GetTempPath(),
    $"hibiki-route-rule-check-{Guid.NewGuid():N}.json");
try
{
    Check(routeRules.TrySave(routeRulePath, out _),
        "Session route rule catalog save failed.");
    var loadedRules = new SessionRouteRuleCatalogV1();
    Check(loadedRules.TryLoad(routeRulePath, out _) && loadedRules.Count == 1 &&
          loadedRules.Rules[0].Priority == 40,
        "Session route rule catalog load failed.");
    File.WriteAllText(routeRulePath,
        "{\"schema_version\":1,\"rules\":[{\"rule_id\":\"Bad ID\",\"priority\":0,\"enabled\":true,\"gain_owner\":0,\"makeup_gain_db\":0,\"app_id\":\"x.exe\",\"display_name\":\"\",\"lane_id\":\"game\",\"output_group\":\"main\"}]}");
    Check(!loadedRules.TryLoad(routeRulePath, out _) && loadedRules.Count == 1 &&
          loadedRules.Rules[0].RuleId == "quiet-game",
        "Invalid route rule load must preserve the previous catalog.");
    Check(routeRules.Remove("quiet-game") && routeRules.Count == 0,
        "Session route rule catalog removal failed.");
}
finally
{
    if (File.Exists(routeRulePath)) File.Delete(routeRulePath);
}
Check(OutputGroupCatalog.Fixed.Count == 3 &&
      OutputGroupCatalog.Fixed.Select(group => group.Id).SequenceEqual(
          ["main", "low-latency", "surround"]),
    "Fixed output-group catalog changed unexpectedly.");
var snapshot = new ControlSnapshot(UiMode.Easy, AudioControlStatus.Controlled, "main", -8.5, -8.5, false, null, null);
Check(snapshot.DisplayVolume == "-8.5 dB", "dB display must use the effective value.");
var cappedVolume = new VolumeSafetyStateV1(-6.0, -12.0, -12.0, false, 4UL,
    VolumeStateOriginV1.Safety, VolumeActuatorV1.InternalDsp);
Check(cappedVolume.IsValid && cappedVolume.IsSafetyCapped &&
      cappedVolume.SafetyStatusText.Contains("安全限制") &&
      cappedVolume.ActuatorLabel.Contains("單次套用"),
    "Volume safety projection must expose the capped effective value.");
Check(!new VolumeSafetyStateV1(-6.0, -12.0, -5.0, false, 4UL,
    VolumeStateOriginV1.Safety, VolumeActuatorV1.InternalDsp).IsValid,
    "Volume safety projection must reject an effective value above the ceiling.");
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
var statusPayload = ControlPayloadsV1.EncodeControlStatusSnapshot(
    7UL, cappedVolume, RouteHealthCatalogV1.Defaults);
Check(ControlPayloadsV1.TryDecodeControlStatusSnapshot(statusPayload,
          out var statusSequence, out var statusVolume, out var statusRoutes) &&
      statusSequence == 7UL && statusVolume.IsSafetyCapped && statusRoutes.Count == 4 &&
      statusRoutes.Any(route => route.Id == "browser-tab" && route.RequiresUserAction),
    "Control status snapshot did not round-trip with route and volume state.");
var catalogRequest = commandFactory.RequestDeviceCatalog();
Check(catalogRequest.Type == ControlMessageType.DeviceCatalogRequest &&
      catalogRequest.Payload.IsEmpty,
    "Device catalog request must use an empty v1 payload.");
Check(IpcRequestSession.IsReplyTo(volumeCommand,
        new IpcEnvelopeV1(ControlMessageType.Ack, volumeCommand.RequestId, ReadOnlyMemory<byte>.Empty)),
    "Control command request correlation failed.");
Check(IpcRequestSession.IsReplyTo(catalogRequest,
        new IpcEnvelopeV1(ControlMessageType.DeviceCatalogSnapshot, catalogRequest.RequestId,
                          ReadOnlyMemory<byte>.Empty)),
    "Device catalog snapshot reply correlation failed.");
var statusRequest = commandFactory.RequestControlStatus();
Check(statusRequest.Type == ControlMessageType.ControlStatusRequest &&
      IpcRequestSession.IsReplyTo(statusRequest,
        new IpcEnvelopeV1(ControlMessageType.ControlStatusSnapshot, statusRequest.RequestId,
                          statusPayload)),
    "Control status snapshot reply correlation failed.");
var sessionRequest = commandFactory.RequestSessionCatalog();
Check(sessionRequest.Type == ControlMessageType.SessionCatalogRequest &&
      sessionRequest.Payload.IsEmpty &&
      IpcRequestSession.IsReplyTo(sessionRequest,
        new IpcEnvelopeV1(ControlMessageType.SessionCatalogSnapshot, sessionRequest.RequestId,
                          Array.Empty<byte>())),
    "Session catalog request/reply correlation failed.");
var sessionEntries = new[]
{
    new SessionCatalogEntryV1(0x0000000200000001UL, true,
        SessionCatalogRouteStateV1.Ready, true, -9.5, false,
        "DJMAX", "game.exe", "game", "main"),
    new SessionCatalogEntryV1(0x0000000200000002UL, false,
        SessionCatalogRouteStateV1.Unavailable, false, 0.0, true,
        "Chrome 分頁", "chrome.exe", "browser-tab", "main")
};
var sessionPayload = ControlPayloadsV1.EncodeSessionCatalogSnapshot(12UL, 2UL, sessionEntries);
Check(ControlPayloadsV1.TryDecodeSessionCatalogSnapshot(sessionPayload,
          out var sessionSequence, out var sessionGeneration, out var decodedSessions) &&
      sessionSequence == 12UL && sessionGeneration == 2UL && decodedSessions.Count == 2 &&
      decodedSessions[0].DisplayName == "DJMAX" && decodedSessions[1].RouteStateLabel == "目前不可用",
    "Session catalog snapshot did not round-trip.");
var malformedSessionPayload = sessionPayload.ToArray();
malformedSessionPayload[2] = 1;
Check(!ControlPayloadsV1.TryDecodeSessionCatalogSnapshot(malformedSessionPayload, out _, out _, out _),
    "Session catalog decoder must reject reserved bytes.");
var sessionVolumeBytes = ControlPayloadsV1.EncodeSessionVolumeCommand(
    sessionEntries[0].Handle, -9.5, true, 12UL);
Check(ControlPayloadsV1.TryDecodeSessionVolumeCommand(sessionVolumeBytes,
          out var sessionVolumeHandle, out var sessionVolumeDb,
          out var sessionVolumeMute, out var sessionVolumeSequence) &&
      sessionVolumeHandle == sessionEntries[0].Handle && Math.Abs(sessionVolumeDb + 9.5) < 1e-6 &&
      sessionVolumeMute && sessionVolumeSequence == 12UL,
    "Session volume command did not round-trip.");
var malformedSessionVolume = sessionVolumeBytes.ToArray();
malformedSessionVolume[13] = 1;
Check(!ControlPayloadsV1.TryDecodeSessionVolumeCommand(malformedSessionVolume,
                                                        out _, out _, out _, out _),
    "Session volume decoder must reject reserved bytes.");
var sessionRouteBytes = ControlPayloadsV1.EncodeSessionRouteCommand(
    sessionEntries[0].Handle, 12UL, "game", "surround");
Check(ControlPayloadsV1.TryDecodeSessionRouteCommand(sessionRouteBytes,
          out var sessionRouteHandle, out var sessionRouteSequence,
          out var sessionRouteLane, out var sessionRouteOutput) &&
      sessionRouteHandle == sessionEntries[0].Handle && sessionRouteSequence == 12UL &&
      sessionRouteLane == "game" && sessionRouteOutput == "surround",
    "Session route command did not round-trip.");
var malformedSessionRoute = sessionRouteBytes.ToArray();
malformedSessionRoute[18] = 1;
Check(!ControlPayloadsV1.TryDecodeSessionRouteCommand(malformedSessionRoute,
                                                       out _, out _, out _, out _),
    "Session route decoder must reject reserved bytes.");
var ruleCommand = new SessionRouteRuleCommandV1(
    1U, 20, 3.5, SessionRouteRuleOperationV1.Upsert, true,
    SessionRouteRuleGainOwnerV1.WindowsSession, 12UL, "quiet-game", "game.exe",
    "DJMAX", "game", "surround");
var ruleBytes = ControlPayloadsV1.EncodeSessionRouteRuleCommand(ruleCommand);
Check(ruleBytes.Length == ControlPayloadsV1.SessionRouteRuleCommandBytes &&
      ControlPayloadsV1.TryDecodeSessionRouteRuleCommand(ruleBytes, out var decodedRule) &&
      decodedRule is not null && decodedRule.RuleId == "quiet-game" &&
      decodedRule.AppId == "game.exe" && decodedRule.DisplayName == "DJMAX" &&
      decodedRule.LaneId == "game" && decodedRule.OutputGroup == "surround" &&
      decodedRule.Priority == 20 && Math.Abs(decodedRule.MakeupGainDb - 3.5) < 1e-6,
    "Session route rule command did not round-trip.");
var malformedRule = ruleBytes.ToArray();
malformedRule[29] = 1;
Check(!ControlPayloadsV1.TryDecodeSessionRouteRuleCommand(malformedRule, out _),
    "Session route rule decoder must reject reserved bytes.");
var removeRule = commandFactory.RemoveSessionRouteRule(13UL, "quiet-game");
Check(removeRule.Type == ControlMessageType.SessionRouteRuleCommand &&
      ControlPayloadsV1.TryDecodeSessionRouteRuleCommand(removeRule.Payload.Span,
          out var removedRule) && removedRule?.Operation == SessionRouteRuleOperationV1.Remove &&
      removedRule.RuleId == "quiet-game",
    "Session route rule remove command did not round-trip.");
var clearRules = commandFactory.ClearSessionRouteRules(14UL);
Check(clearRules.Type == ControlMessageType.SessionRouteRuleCommand &&
      ControlPayloadsV1.TryDecodeSessionRouteRuleCommand(clearRules.Payload.Span,
          out var clearedRules) && clearedRules?.Operation == SessionRouteRuleOperationV1.Clear &&
      IpcRequestSession.IsReplyTo(clearRules,
          new IpcEnvelopeV1(ControlMessageType.Ack, clearRules.RequestId,
                            ReadOnlyMemory<byte>.Empty)),
    "Session route rule clear command did not round-trip.");
var sessionVolumeRequest = commandFactory.SetSessionVolume(
    sessionEntries[0].Handle, -9.5, true, 12UL);
Check(sessionVolumeRequest.Type == ControlMessageType.SessionVolumeCommand &&
      IpcRequestSession.IsReplyTo(sessionVolumeRequest,
        new IpcEnvelopeV1(ControlMessageType.Ack, sessionVolumeRequest.RequestId,
                          Array.Empty<byte>())),
    "Session volume command request correlation failed.");
var sessionRouteRequest = commandFactory.SetSessionRoute(
    sessionEntries[0].Handle, 12UL, "game", "surround");
Check(sessionRouteRequest.Type == ControlMessageType.SessionRouteCommand &&
      IpcRequestSession.IsReplyTo(sessionRouteRequest,
        new IpcEnvelopeV1(ControlMessageType.Ack, sessionRouteRequest.RequestId,
                          Array.Empty<byte>())),
    "Session route command request correlation failed.");
var viewModel = new EasyControlViewModel { SelectedOutputGroup = " main " };
Check(viewModel.ConnectionState == ControlConnectionState.Disconnected &&
      !viewModel.IsConnected && !viewModel.IsBusy &&
      viewModel.EffectiveVolumeDb == -12.0 &&
      viewModel.SafetyStatusText.Contains("未截頂"),
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
var removableScenePath = Path.Combine(
    Path.GetTempPath(), $"hibiki-removable-scene-check-{Guid.NewGuid():N}.json");
try
{
    var removableSceneViewModel = new EasyControlViewModel
    {
        CustomSceneCatalogPath = removableScenePath,
        SelectedOutputGroup = "main"
    };
    var customSceneNotifications = new List<string>();
    removableSceneViewModel.PropertyChanged += (_, args) =>
        customSceneNotifications.Add(args.PropertyName ?? string.Empty);
    Check(removableSceneViewModel.UpsertCustomScene(new SceneCard(
              "removable-scene", "可移除遊戲", "測試本機卡片", "零額外緩衝", true)) &&
          removableSceneViewModel.CustomSceneCards.Count == 1 &&
          removableSceneViewModel.SelectScene("removable-scene") &&
          removableSceneViewModel.SelectedScene?.Id == "removable-scene",
        "ViewModel removable custom Scene fixture failed.");
    Check(removableSceneViewModel.RemoveCustomScene("removable-scene") &&
          removableSceneViewModel.Scenes.Count == 4 &&
          removableSceneViewModel.CustomSceneCards.Count == 0 &&
          removableSceneViewModel.SelectedScene is null &&
          removableSceneViewModel.StatusText.Contains("已移除") &&
          customSceneNotifications.Contains(nameof(EasyControlViewModel.CustomSceneCards)),
        "ViewModel must remove a selected custom Scene, notify cards and report success.");
    Check(removableSceneViewModel.SaveCustomScenes(out _),
        "Removed custom Scene catalog could not be saved.");
    var reloadedRemovableScenes = new CustomSceneCatalogV1();
    Check(reloadedRemovableScenes.TryLoad(removableScenePath, out _) &&
          reloadedRemovableScenes.Count == 0,
        "Removed custom Scene card must remain removed after reload.");
    Check(!removableSceneViewModel.RemoveCustomScene("missing-scene") &&
          removableSceneViewModel.StatusText.Contains("找不到"),
        "Unknown custom Scene removal must fail closed.");
    Check(!removableSceneViewModel.RemoveCustomScene("game") &&
          removableSceneViewModel.Scenes.Count == 4 &&
          removableSceneViewModel.SelectedScene is null,
        "Built-in Scene IDs must remain non-removable through the custom-card seam.");
}
finally
{
    if (File.Exists(removableScenePath)) File.Delete(removableScenePath);
}

var blockedSceneDirectory = Path.Combine(
    Path.GetTempPath(), $"hibiki-blocked-scene-check-{Guid.NewGuid():N}");
File.WriteAllText(blockedSceneDirectory, "blocked");
try
{
    var rollbackSceneViewModel = new EasyControlViewModel
    {
        CustomSceneCatalogPath = Path.Combine(blockedSceneDirectory, "scene-cards-v1.json"),
        SelectedOutputGroup = "main"
    };
    Check(rollbackSceneViewModel.UpsertCustomScene(new SceneCard(
              "rollback-scene", "回復測試", "保存失敗必須復原", "零額外緩衝", true)) &&
          rollbackSceneViewModel.SelectScene("rollback-scene") &&
          rollbackSceneViewModel.SelectedScene?.Id == "rollback-scene",
        "ViewModel custom Scene rollback fixture failed.");
    Check(!rollbackSceneViewModel.RemoveCustomScene("rollback-scene") &&
          rollbackSceneViewModel.Scenes.Count == 5 &&
          rollbackSceneViewModel.CustomSceneCards.Count == 1 &&
          rollbackSceneViewModel.CustomSceneCards[0].Id == "rollback-scene" &&
          rollbackSceneViewModel.SelectedScene?.Id == "rollback-scene" &&
          rollbackSceneViewModel.StatusText.Contains("自訂場景未移除"),
        "A failed custom Scene save must restore the prior card and selection.");
}
finally
{
    if (File.Exists(blockedSceneDirectory)) File.Delete(blockedSceneDirectory);
}

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
var snapshotSpeaker = speakers with { IsDefault = false };
var snapshotMicrophone = new PhysicalDeviceCard("endpoint-mic", "麥克風",
    PhysicalDeviceFlowV1.Capture, PhysicalDeviceAvailabilityV1.Active, 2, 48000, 128, true, 11UL);
var snapshotBytes = ControlPayloadsV1.EncodeDeviceCatalogSnapshot(
    [snapshotSpeaker, snapshotMicrophone], 30UL);
Check(ControlPayloadsV1.TryDecodeDeviceCatalogSnapshot(snapshotBytes,
          out var snapshotSequence, out var decodedDevices) && snapshotSequence == 30UL &&
      decodedDevices.Count == 2 && decodedDevices[1].IsDefault,
    "Physical render/capture default catalog snapshot did not round-trip.");
var snapshotFrame = new IpcEnvelopeV1(ControlMessageType.DeviceCatalogSnapshot, 0UL, snapshotBytes);
Check(viewModel.ApplyPhysicalDeviceSnapshot(snapshotFrame, out _) &&
      viewModel.PhysicalDevices.Count == 2 && viewModel.PhysicalDevices[1].IsDefault,
    "ViewModel did not atomically apply the device catalog snapshot.");
var pickerRefreshViewModel = new EasyControlViewModel { SelectedOutputGroup = "main" };
Check(pickerRefreshViewModel.ApplyPhysicalDeviceSnapshot(snapshotFrame, out _) &&
      pickerRefreshViewModel.PhysicalDevices.Count == 2,
    "Picker refresh fixture could not seed the bounded snapshot seam.");
Check(!await pickerRefreshViewModel.RefreshPhysicalDevicePickerAsync() &&
      pickerRefreshViewModel.StatusText.Contains("無法重新掃描"),
    "Disconnected device picker refresh must fail closed.");
var stalePickerSnapshot = new IpcEnvelopeV1(
    ControlMessageType.DeviceCatalogSnapshot, 0UL,
    ControlPayloadsV1.EncodeDeviceCatalogSnapshot([snapshotSpeaker], 29UL));
Check(!pickerRefreshViewModel.ApplyPhysicalDeviceSnapshot(stalePickerSnapshot, out var stalePickerError) &&
      stalePickerError.Contains("過期") &&
      pickerRefreshViewModel.PhysicalDevices.Count == 2 &&
      pickerRefreshViewModel.StatusText.Contains("無法重新掃描"),
    "Stale picker data must preserve the previous catalog and not fake a successful scan.");
var staleSnapshot = new IpcEnvelopeV1(
    ControlMessageType.DeviceCatalogSnapshot, 0UL,
    ControlPayloadsV1.EncodeDeviceCatalogSnapshot([snapshotSpeaker], 29UL));
Check(!viewModel.ApplyPhysicalDeviceSnapshot(staleSnapshot, out var staleSnapshotError) &&
      staleSnapshotError.Contains("過期") && viewModel.PhysicalDevices.Count == 2,
    "Stale device catalog snapshot must preserve the previous catalog.");
var malformedSnapshot = snapshotBytes.ToArray();
malformedSnapshot[2] = 1;
Check(!ControlPayloadsV1.TryDecodeDeviceCatalogSnapshot(malformedSnapshot, out _, out _),
    "Device catalog snapshot decoder must reject reserved bytes.");
var sessionFrame = new IpcEnvelopeV1(ControlMessageType.SessionCatalogSnapshot, 0UL, sessionPayload);
Check(viewModel.ApplySessionCatalogSnapshot(sessionFrame, out _) &&
      viewModel.SessionCatalogSequence == 12UL && viewModel.SessionCatalog.Count == 2 &&
      viewModel.SessionCatalog[0].AccessibleSummary.Contains("DJMAX") &&
      viewModel.SessionCatalog[1].VolumeAvailable == false,
    "ViewModel did not atomically apply the App session catalog.");
var staleSession = new IpcEnvelopeV1(
    ControlMessageType.SessionCatalogSnapshot, 0UL,
    ControlPayloadsV1.EncodeSessionCatalogSnapshot(11UL, 2UL, [sessionEntries[0]]));
Check(!viewModel.ApplySessionCatalogSnapshot(staleSession, out var staleSessionError) &&
      staleSessionError.Contains("過期") && viewModel.SessionCatalog.Count == 2,
    "Stale App session catalog must preserve the previous catalog.");
var viewModelRule = new SessionRouteRuleCard(
    "quiet-game", 20, true, SessionRouteRuleGainOwnerV1.WindowsSession, 3.5,
    "game.exe", "DJMAX", "game", "surround");
Check(viewModel.UpsertRouteRule(viewModelRule) &&
      viewModel.SelectSession(sessionEntries[0].Handle) &&
      viewModel.SelectedRouteRuleSummary.Contains("預覽預設") &&
      viewModel.SessionRouteLaneId == "game" &&
      viewModel.SessionRouteOutputGroup == "surround",
    "ViewModel must preview a matching App route preset without claiming it was applied.");
Check(viewModel.SelectSession(sessionEntries[0].Handle) &&
      viewModel.SelectedSession?.DisplayName == "DJMAX" &&
      viewModel.BuildSessionVolumeCommand(sessionEntries[0].Handle, -9.5, true).Type ==
          ControlMessageType.SessionVolumeCommand,
    "ViewModel App session volume command failed to bind the current handle.");
viewModel.SessionVolumeDb = -6.0;
viewModel.SessionMuted = true;
var selectedSessionVolume = viewModel.BuildSessionVolumeCommand(
    sessionEntries[0].Handle, viewModel.SessionVolumeDb, viewModel.SessionMuted);
Check(ControlPayloadsV1.TryDecodeSessionVolumeCommand(selectedSessionVolume.Payload.Span,
          out _, out var selectedSessionDb, out var selectedSessionMute, out _) &&
      Math.Abs(selectedSessionDb + 6.0) < 1e-6 && selectedSessionMute,
    "ViewModel selected App volume controls did not preserve dB/mute.");
Check(!await viewModel.ApplySelectedSessionVolumeAsync(),
    "Disconnected selected App volume must fail closed.");
var staleHandle = sessionEntries[0].Handle + 100UL;
Check(!viewModel.SelectSession(staleHandle),
    "ViewModel must reject an unknown or stale App session handle.");
Check(viewModel.BuildSessionRouteCommand(sessionEntries[0].Handle, "game", "surround").Type ==
          ControlMessageType.SessionRouteCommand,
    "ViewModel App session route command failed to bind the current handle.");
var viewModelRuleCommand = viewModel.BuildUpsertSessionRouteRuleCommand(viewModelRule);
Check(viewModelRuleCommand.Type == ControlMessageType.SessionRouteRuleCommand &&
      ControlPayloadsV1.TryDecodeSessionRouteRuleCommand(viewModelRuleCommand.Payload.Span,
          out var decodedViewModelRule) && decodedViewModelRule is not null &&
      decodedViewModelRule.RuleId == "quiet-game" &&
      decodedViewModelRule.CatalogSequence == 12UL &&
      decodedViewModelRule.OutputGroup == "surround",
    "ViewModel App route-rule command failed to bind the current catalog sequence.");
Check(!await viewModel.ApplyRemoveRouteRuleAsync("missing-rule"),
    "Unknown disconnected route-rule removal must fail closed.");
viewModel.SessionRouteLaneId = "game";
viewModel.SessionRouteOutputGroup = "surround";
Check(viewModel.HasSelectedSession && !await viewModel.ApplySelectedSessionRouteAsync(),
    "Disconnected selected App route must fail closed.");
viewModel.IsExpert = true;
Check(viewModel.Mode == UiMode.Expert && viewModel.SelectScene("movie"),
    "ViewModel Expert scene selection failed.");
Check(viewModel.Expert.IsVisible && viewModel.Expert.MatrixRoutes.Count == 4 &&
      viewModel.Expert.DspGraph.Any(node => node.Id == "limiter" && node.Enabled) &&
      viewModel.Expert.Vst3Lanes.All(lane => !lane.Trusted) &&
      viewModel.Expert.Calibration.Mode == "Relative Compensation" &&
      viewModel.Expert.StatusText.Contains("唯讀") &&
      viewModel.Expert.RouteHealth.Any(card => card.Id == "process-loopback" &&
                                               card.State == RouteHealthStateV1.Pending) &&
      viewModel.Expert.RouteHealth.Any(card => card.Id == "direct-path" &&
                                               card.State == RouteHealthStateV1.Bypassed),
    "Expert surface must expose bounded read-only graph details.");
var routeSnapshot = new[]
{
    new RouteHealthCardV1("process-loopback", "Process Loopback", RouteHealthStateV1.Ready,
                          "目前引擎已回報可用。"),
    new RouteHealthCardV1("browser-tab", "Chrome／Edge 單分頁", RouteHealthStateV1.Pending,
                          "需要擴充功能。", true)
};
Check(viewModel.ApplyRouteHealth(routeSnapshot, out _) &&
      viewModel.Expert.RouteHealth.Count == 2 &&
      viewModel.Expert.RouteHealth[0].StateLabel == "已可用",
    "Validated route-health snapshots must replace the conservative defaults.");
Check(routeSnapshot[0].AccessibleSummary == "Process Loopback：已可用。目前引擎已回報可用。" &&
      routeSnapshot[1].AccessibleSummary.StartsWith("Chrome／Edge 單分頁：等待引擎回報。") &&
      routeSnapshot[1].AccessibleSummary.EndsWith("需要擴充功能。"),
    "Route health cards must compose name, state and boundary detail for assistive technology.");
Check(RouteHealthCatalogV1.Defaults.Single(card => card.Id == "direct-path").
      AccessibleSummary.Contains("Vendor ASIO／WASAPI Exclusive：繞過 Hibiki。"),
    "Default bypass route must expose an honest accessible summary.");
Check(viewModel.Expert.RouteHealthAccessibleSummary.StartsWith("路由狀態：Process Loopback：已可用。目前引擎已回報可用。") &&
      viewModel.Expert.RouteHealthAccessibleSummary.EndsWith("Chrome／Edge 單分頁：等待引擎回報。需要擴充功能。") &&
      viewModel.Expert.RouteHealthAccessibleSummary.Contains("／"),
      "Route health projection must compose full accessible summaries (name, state and boundary detail) per card.");
var duplicateRoutes = new[] { routeSnapshot[0], routeSnapshot[0] };
Check(!viewModel.ApplyRouteHealth(duplicateRoutes, out var duplicateRouteError) &&
      duplicateRouteError.Contains("重複"),
    "Duplicate route-health identities must fail closed.");
viewModel.IsExpert = false;
Check(!viewModel.Expert.IsVisible && viewModel.Expert.StatusText.Contains("隱藏"),
    "Expert surface must hide when Easy mode is selected.");
viewModel.RequestedVolumeDb = -6.0205999;
viewModel.Muted = true;
var viewModelVolume = viewModel.BuildVolumeCommand();
Check(viewModelVolume.Type == ControlMessageType.VolumeNotification &&
      viewModelVolume.RequestId > 1,
    "ViewModel volume command was not generated.");
Check(viewModel.VolumeGeneration > 0 && viewModel.VolumeOriginText.Contains("Hibiki UI"),
    "ViewModel volume command must update the visible control-plane origin.");
Check(viewModel.ApplyVolumeSafetyState(cappedVolume, out _) &&
      Math.Abs(viewModel.EffectiveVolumeDb + 12.0) < 1e-9 &&
      viewModel.SafetyStatusText.Contains("安全限制"),
    "ViewModel must expose an engine-reconciled safety cap without writing it back.");
var statusFrame = new IpcEnvelopeV1(ControlMessageType.ControlStatusSnapshot, 0UL,
                                    statusPayload);
Check(viewModel.ApplyControlStatusSnapshot(statusFrame, out _) &&
      viewModel.StatusSequence == 7UL && viewModel.Expert.RouteHealth.Count == 4 &&
      viewModel.Expert.RouteHealth.Any(route => route.Id == "browser-tab" &&
                                                route.RequiresUserAction),
    "ViewModel must atomically apply a validated control status snapshot.");
var malformedStatus = statusPayload.ToArray();
malformedStatus[2] = 1;
Check(!viewModel.ApplyControlStatusSnapshot(
          new IpcEnvelopeV1(ControlMessageType.ControlStatusSnapshot, 0UL, malformedStatus),
          out _) && viewModel.StatusSequence == 7UL,
    "Malformed control status snapshot must preserve prior state.");
Check(!viewModel.ApplyVolumeSafetyState(cappedVolume with { Generation = 3UL }, out var staleVolumeError) &&
      staleVolumeError.Contains("過期"),
    "Stale volume safety state must be rejected.");
viewModel.IrPhaseMode = IrPhaseMode.LinearPhase;
viewModel.IrPhaseStrength = 0.5;
Check(viewModel.IrPhasePolicy.IsValid && viewModel.IrPhasePolicy.UsesFir &&
      Math.Abs(viewModel.IrAddedDelayMs - 80.0) < 1e-9,
    "ViewModel IR phase slider did not resolve the linear-phase delay.");
viewModel.IrPhaseMode = IrPhaseMode.Bypass;
Check(viewModel.IrPhaseStrength == 0.0 && viewModel.IrAddedDelayMs == 0.0,
    "IR phase bypass must clear the slider and added delay.");
Check(viewModel.IrPhaseModeOptions.Count == 4 &&
      viewModel.IrPhaseModeOptions.Any(option => option.Mode == IrPhaseMode.LinearPhase &&
                                                 option.Label.Contains("Movie")),
    "IR phase UI options must expose the bounded Game/Balanced/Movie/Bypass contract.");
viewModel.IrPhaseMode = IrPhaseMode.LinearPhase;
viewModel.IrPhaseStrength = 0.5;
var irCommand = new ControlCommandFactoryV1().PrepareIr(
    "C:/Hibiki/measurements/movie.wav", viewModel.IrPhasePolicy, 48000U, 2U);
Check(irCommand.Type == ControlMessageType.IrPrepareCommand &&
      ControlPayloadsV1.TryDecodeIrPrepare(irCommand.Payload.Span, out var irPath,
                                           out var irMode, out var irStrength,
                                           out var irRate, out var irChannels) &&
      irPath.EndsWith("movie.wav", StringComparison.OrdinalIgnoreCase) &&
      irMode == IrPhaseMode.LinearPhase && Math.Abs(irStrength - 0.5) < 1.0e-5 &&
      irRate == 48000U && irChannels == 2U,
    "IR prepare command must round-trip the bounded path and policy payload.");
var noEngine = new EasyControlViewModel("HibikiDSP_v1_control_model_check_missing");
var connectedToMissingEngine = await noEngine.ConnectAsync(TimeSpan.FromMilliseconds(50));
Check(!connectedToMissingEngine && noEngine.ConnectionState == ControlConnectionState.Degraded &&
      noEngine.StatusText.Contains("找不到 Hibiki"),
    "Missing engine must fail closed with a bounded degraded status.");
Check(!await noEngine.OneTapEnhanceAsync(),
    "One-Tap command must not be reported as applied while disconnected.");
Check(!await noEngine.RefreshPhysicalDevicesAsync() &&
      noEngine.StatusText.Contains("未連線"),
    "Disconnected device catalog refresh must fail closed.");
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
var validPoint = new CalibrationPointV1(1000.0, -3.0, 0.0);
Check(validPoint.IsValid, "Valid calibration point must be accepted.");
Check(!new CalibrationPointV1(0.0, 0.0, 0.0).IsValid &&
      !new CalibrationPointV1(25000.0, 0.0, 0.0).IsValid &&
      !new CalibrationPointV1(double.NaN, 0.0, 0.0).IsValid,
    "Invalid calibration point must fail closed.");
var calibPoints = new List<CalibrationPointV1>
{
    new(100.0, -8.0, 0.0),
    new(250.0, 4.0, 0.0),
    new(1000.0, -2.0, 0.0),
    new(4000.0, 5.0, 0.0),
    new(10000.0, -1.0, 0.0)
};
var validResponse = new CalibrationResponseV1(1U, 48000.0, 2, "test-endpoint", calibPoints);
Check(validResponse.IsValid, "Valid calibration response must be accepted.");
Check(!new CalibrationResponseV1(2U, 48000.0, 2, "test", calibPoints).IsValid &&
      !new CalibrationResponseV1(1U, 4000.0, 2, "test", calibPoints).IsValid &&
      !new CalibrationResponseV1(1U, 48000.0, 3, "test", calibPoints).IsValid &&
      !new CalibrationResponseV1(1U, 48000.0, 2, "test", [new(200.0, 0.0, 0.0), new(100.0, 0.0, 0.0)]).IsValid,
    "Invalid calibration response must fail closed.");
var validFilter = new PeqFilterV1("peaking", 1000.0, 3.0, 1.414);
Check(validFilter.IsValid, "Valid PEQ filter must be accepted.");
Check(!new PeqFilterV1("lowpass", 1000.0, 3.0, 1.414).IsValid &&
      !new PeqFilterV1("peaking", 5.0, 0.0, 1.0).IsValid &&
      !new PeqFilterV1("peaking", 1000.0, 30.0, 1.0).IsValid &&
      !new PeqFilterV1("peaking", 1000.0, 0.0, 0.05).IsValid,
    "Invalid PEQ filter must fail closed.");
var validPreset = new PeqPresetV1(1U, [validFilter]);
Check(validPreset.IsValid, "Valid PEQ preset must be accepted.");
Check(!new PeqPresetV1(2U, [validFilter]).IsValid &&
      !new PeqPresetV1(1U, [new PeqFilterV1("peaking", 5.0, 0.0, 1.0)]).IsValid,
    "Invalid PEQ preset must fail closed.");
var calibPolicy = new CalibrationCompilePolicyV1();
Check(calibPolicy.IsValid && CalibrationCompilerV1.ValidatePolicy(calibPolicy),
    "Default calibration compile policy must be valid.");
Check(!new CalibrationCompilePolicyV1 { MaxFilters = 0 }.IsValid &&
      !new CalibrationCompilePolicyV1 { MaxFilters = 20 }.IsValid &&
      !new CalibrationCompilePolicyV1 { MinFrequencyHz = 5000.0, MaxFrequencyHz = 1000.0 }.IsValid &&
      !new CalibrationCompilePolicyV1 { MaxBoostDb = 30.0 }.IsValid,
    "Invalid calibration policy must fail closed.");
var compileResult = CalibrationCompilerV1.CompileBoundedPeqCorrection(calibPoints, calibPolicy);
Check(compileResult.Filters.Count == 5 && compileResult.Limited &&
      Math.Abs(compileResult.MaximumRequestedCorrectionDb - 8.0) < 1e-6 &&
      compileResult.Filters[0].FrequencyHz == 100.0 &&
      compileResult.Filters[0].GainDb == 6.0 &&
      compileResult.Filters[1].GainDb == -4.0 &&
      compileResult.Diagnostic.Contains("clipped"),
    "Bounded PEQ compiler failed on standard response.");
var normalCompile = CalibrationCompilerV1.CompileBoundedPeqCorrection(
    [new(100.0, -3.0, 0.0), new(1000.0, 2.0, 0.0)],
    calibPolicy with { MaxBoostDb = 6.0, MaxCutDb = 12.0 });
Check(!normalCompile.Limited && normalCompile.Filters.Count == 2 &&
      normalCompile.Filters[0].GainDb == 3.0 &&
      normalCompile.Filters[1].GainDb == -2.0 &&
      normalCompile.Diagnostic.Contains("verify"),
    "Normal within-limit PEQ compilation must succeed without limited flag.");
var spacingCompile = CalibrationCompilerV1.CompileBoundedPeqCorrection(
    [new(1000.0, -5.0, 0.0), new(1020.0, -4.8, 0.0), new(2000.0, 3.0, 0.0)],
    calibPolicy with { MinSpacingOctaves = 0.5 });
Check(spacingCompile.Filters.Count == 2 && spacingCompile.Filters[0].FrequencyHz == 1000.0 &&
      spacingCompile.Filters[1].FrequencyHz == 2000.0,
    "Compiler min spacing octaves must prevent clustered filters.");
var ignoreCompile = CalibrationCompilerV1.CompileBoundedPeqCorrection(
    [new(1000.0, -0.1, 0.0)], calibPolicy with { IgnoreErrorDb = 0.25 });
Check(ignoreCompile.Filters.Count == 0 && !ignoreCompile.Limited &&
      ignoreCompile.Diagnostic.Contains("ignore threshold"),
    "Compiler must ignore errors within threshold.");
var apoExport = CalibrationCompilerV1.ExportEqualizerApo(compileResult.Filters);
Check(apoExport.Contains("Preamp: -1 dB") && apoExport.Contains("Filter 1: ON PK Fc 100.000 Hz"),
    "Equalizer APO export format failed.");
var camillaExport = CalibrationCompilerV1.ExportCamillaDspYaml(compileResult.Filters);
Check(camillaExport.Contains("filters:") && camillaExport.Contains("type: Peaking"),
    "CamillaDSP YAML export format failed.");
var rewExport = CalibrationCompilerV1.ExportRewFilterList(compileResult.Filters);
Check(rewExport.Contains("Filter\tType\tFreq (Hz)\tGain (dB)\tQ"),
    "REW filter list export format failed.");
var jsonExport = CalibrationCompilerV1.ExportHibikiProfile(compileResult.Filters);
Check(jsonExport.Contains("\"schema_version\": 1") && jsonExport.Contains("\"type\": \"peaking\""),
    "Hibiki profile export format failed.");
var calibPath = Path.Combine(Path.GetTempPath(), $"hibiki-calib-check-{Guid.NewGuid():N}.json");
try
{
    Check(CalibrationCompilerV1.TrySaveResponse(calibPath, validResponse, out _),
        "Calibration response save failed.");
    Check(CalibrationCompilerV1.TryLoadResponse(calibPath, out var loadedResponse, out _) &&
          loadedResponse is not null && loadedResponse.Points.Count == 5 &&
          loadedResponse.DeviceId == "test-endpoint",
        "Calibration response load failed.");
    File.WriteAllText(calibPath, "{\"schema_version\":1,\"sample_rate\":48000,\"channels\":2,\"points\":[{\"frequency_hz\":200,\"measured_db\":0,\"target_db\":0},{\"frequency_hz\":100,\"measured_db\":0,\"target_db\":0}]}");
    Check(!CalibrationCompilerV1.TryLoadResponse(calibPath, out _, out _),
        "Unsorted calibration response load must fail closed.");
}
finally
{
    if (File.Exists(calibPath)) File.Delete(calibPath);
}
var presetPath = Path.Combine(Path.GetTempPath(), $"hibiki-peq-check-{Guid.NewGuid():N}.json");
try
{
Check(CalibrationCompilerV1.TrySavePreset(presetPath, validPreset, out _),
        "PEQ preset save failed.");
    Check(CalibrationCompilerV1.TryLoadPreset(presetPath, out var loadedPreset, out _) &&
          loadedPreset is not null && loadedPreset.Filters.Count == 1 &&
          loadedPreset.Filters[0].FrequencyHz == 1000.0,
        "PEQ preset load failed.");
    File.WriteAllText(presetPath, "{\"schema_version\":1,\"filters\":[{\"type\":\"invalid\",\"frequency_hz\":1000,\"gain_db\":0,\"q\":1}]}");
    Check(!CalibrationCompilerV1.TryLoadPreset(presetPath, out _, out _),
        "Invalid PEQ preset load must fail closed.");
}
finally
{
    if (File.Exists(presetPath)) File.Delete(presetPath);
}
var timeline = new Vst3TimelineSurfaceModelV1();
Check(Vst3TimelineSurfaceModelV1.IsValidTimelineId("game-one") &&
      !Vst3TimelineSurfaceModelV1.IsValidTimelineId("") &&
      !Vst3TimelineSurfaceModelV1.IsValidTimelineId("-leading") &&
      !Vst3TimelineSurfaceModelV1.IsValidTimelineId("trailing.") &&
      !Vst3TimelineSurfaceModelV1.IsValidTimelineId("bad space") &&
      !Vst3TimelineSurfaceModelV1.IsValidTimelineId("CON") &&
      !Vst3TimelineSurfaceModelV1.IsValidTimelineId("com4") &&
      !Vst3TimelineSurfaceModelV1.IsValidTimelineId(new string('a', 65)) &&
      Vst3TimelineSurfaceModelV1.IsValidTimelineId(new string('a', 64)),
    "Timeline ID validation diverges from the store contract.");
for (var slot = 0; slot < Vst3TimelineSurfaceModelV1.MaxTimelineIds; ++slot)
{
    Check(timeline.RegisterTimeline($"timeline-{slot:D2}"), "Timeline registration failed.");
}
Check(!timeline.RegisterTimeline("overflow") && timeline.TimelineIdCount == 16,
    "Timeline ID capacity must be bounded at 16.");
Check(timeline.TimelineIds.SequenceEqual(timeline.TimelineIds.OrderBy(id => id, StringComparer.Ordinal)),
    "Timeline ID listing must stay sorted.");
Check(!timeline.Select("missing") && !timeline.HasSelection,
    "Selecting an unknown timeline must fail closed.");
Check(timeline.Select("timeline-00") && timeline.Published.Count == 0 && timeline.IsDirty() == false,
    "Empty timeline selection should start clean.");
Check(!timeline.Upsert(new Vst3TimelineSurfaceModelV1.TimelineEvent(7, 100, 0.5)),
    "Editing without an open draft must fail closed.");
Check(timeline.BeginEdit() && !timeline.BeginEdit(), "A second concurrent draft must be refused.");
Check(timeline.Upsert(new Vst3TimelineSurfaceModelV1.TimelineEvent(7, 480, 0.25)) &&
      timeline.Upsert(new Vst3TimelineSurfaceModelV1.TimelineEvent(3, 240, 0.75)) &&
      timeline.Upsert(new Vst3TimelineSurfaceModelV1.TimelineEvent(3, 480, 0.5)),
    "Draft upserts must succeed.");
Check(timeline.Draft![0].ParameterId == 3 && timeline.Draft[0].SamplePosition == 240 &&
      timeline.Draft[1].SamplePosition == 480 && timeline.Draft[1].ParameterId == 3 &&
      timeline.Draft[2].ParameterId == 7,
    "Draft ordering must follow (sample_position, parameter_id).");
Check(!timeline.Upsert(new Vst3TimelineSurfaceModelV1.TimelineEvent(9, 10, -0.1)) &&
      !timeline.Upsert(new Vst3TimelineSurfaceModelV1.TimelineEvent(9, 10, 1.1)) &&
      !timeline.SetValueAt(0, double.NaN) &&
      timeline.SetValueAt(0, 0.9) && timeline.Draft[0].NormalizedValue == 0.9,
    "Value bounds must match the native validator.");
Check(timeline.Commit() && !timeline.HasEditSession && timeline.IsDirty(),
    "Commit should publish the draft and mark derived dirty state.");
Check(timeline.SaveSelected() && !timeline.IsDirty(),
    "Save must re-baseline dirty tracking.");
Check(timeline.BeginEdit() && timeline.RemoveAt(1) && timeline.Commit() &&
      timeline.Published.Count == 2 && timeline.UndoDepth == 2,
    "Remove/commit/undo depth bookkeeping failed.");
Check(timeline.Undo() && timeline.Published.Count == 3 &&
      timeline.Redo() && timeline.Published.Count == 2,
    "Undo/redo round trip failed.");
Check(timeline.BeginEdit() && !timeline.Undo() && timeline.Discard() && !timeline.HasEditSession,
    "History must be refused while a draft is open.");
Check(timeline.Select("timeline-01") && timeline.Published.Count == 0 && timeline.UndoDepth == 0 &&
      !timeline.IsDirty(),
    "Re-selection must replace baseline and clear history.");
var overflow = new Vst3TimelineSurfaceModelV1();
Check(overflow.RegisterTimeline("overflow-case") && overflow.Select("overflow-case") &&
      overflow.BeginEdit(),
    "Overflow fixture setup failed.");
for (var index = 0; index < 16; ++index)
{
    Check(overflow.Upsert(new Vst3TimelineSurfaceModelV1.TimelineEvent((uint)index, (ulong)(index * 10), 0.5)),
        "Unique parameter insertion failed.");
}
Check(!overflow.Upsert(new Vst3TimelineSurfaceModelV1.TimelineEvent(99, 999, 0.5)),
    "The 17th unique parameter must be refused.");
Check(overflow.Commit() && !overflow.HasEditSession && overflow.Published.Count == 16,
    "A valid 16-parameter draft must commit.");
var bindingSurface = new Vst3TimelineSurfaceModelV1();
var notifications = new List<string>();
bindingSurface.PropertyChanged += (_, args) => notifications.Add(args.PropertyName ?? string.Empty);
var notificationCount = notifications.Count;
Check(!bindingSurface.RegisterTimeline("bad id") && notifications.Count == notificationCount,
    "Rejected timeline registration must not notify bindings.");
Check(bindingSurface.RegisterTimeline("bindable") &&
      notifications.SequenceEqual([nameof(Vst3TimelineSurfaceModelV1.TimelineIds),
                                    nameof(Vst3TimelineSurfaceModelV1.TimelineIdCount)]),
    "Accepted timeline registration must notify the catalog properties.");
notifications.Clear();
Check(bindingSurface.Select("bindable") &&
      notifications.Contains(nameof(Vst3TimelineSurfaceModelV1.SelectedTimelineId)) &&
      notifications.Contains(nameof(Vst3TimelineSurfaceModelV1.Published)) &&
      notifications.Contains(nameof(Vst3TimelineSurfaceModelV1.IsDirtyState)) &&
      !bindingSurface.IsDirtyState,
    "Selection must notify the binding state and start clean.");
notifications.Clear();
Check(bindingSurface.BeginEdit() &&
      notifications.SequenceEqual([nameof(Vst3TimelineSurfaceModelV1.Draft),
                                    nameof(Vst3TimelineSurfaceModelV1.HasEditSession)]),
    "Begin edit must notify the draft state.");
notificationCount = notifications.Count;
Check(!bindingSurface.BeginEdit() && notifications.Count == notificationCount,
    "Rejected concurrent edit must not notify bindings.");
notifications.Clear();
Check(bindingSurface.Upsert(new Vst3TimelineSurfaceModelV1.TimelineEvent(4, 20, 0.25)) &&
      notifications.SequenceEqual([nameof(Vst3TimelineSurfaceModelV1.Draft)]),
    "Accepted draft insertion must notify the draft property.");
notifications.Clear();
Check(bindingSurface.Upsert(new Vst3TimelineSurfaceModelV1.TimelineEvent(4, 20, 0.75)) &&
      notifications.SequenceEqual([nameof(Vst3TimelineSurfaceModelV1.Draft)]),
    "Accepted draft replacement must notify the draft property.");
notifications.Clear();
Check(bindingSurface.Commit() && bindingSurface.IsDirtyState &&
      notifications.SequenceEqual([
          nameof(Vst3TimelineSurfaceModelV1.Published),
          nameof(Vst3TimelineSurfaceModelV1.Draft),
          nameof(Vst3TimelineSurfaceModelV1.HasEditSession),
          nameof(Vst3TimelineSurfaceModelV1.CanUndo),
          nameof(Vst3TimelineSurfaceModelV1.UndoDepth),
          nameof(Vst3TimelineSurfaceModelV1.CanRedo),
          nameof(Vst3TimelineSurfaceModelV1.RedoDepth),
          nameof(Vst3TimelineSurfaceModelV1.IsDirtyState)]),
    "Commit must publish and notify all derived binding state.");
notifications.Clear();
Check(bindingSurface.SaveSelected() && !bindingSurface.IsDirtyState &&
      notifications.SequenceEqual([nameof(Vst3TimelineSurfaceModelV1.IsDirtyState)]),
    "Save must notify the dirty-state projection.");
notifications.Clear();
Check(bindingSurface.BeginEdit() && bindingSurface.RemoveAt(0) && bindingSurface.Commit(),
    "Second edit fixture for binding history failed.");
notifications.Clear();
Check(bindingSurface.Undo() && notifications.Contains(nameof(Vst3TimelineSurfaceModelV1.Published)) &&
      notifications.Contains(nameof(Vst3TimelineSurfaceModelV1.IsDirtyState)),
    "Undo must notify published and dirty-state projections.");
notifications.Clear();
Check(bindingSurface.Redo() && notifications.Contains(nameof(Vst3TimelineSurfaceModelV1.Published)) &&
      notifications.Contains(nameof(Vst3TimelineSurfaceModelV1.IsDirtyState)),
    "Redo must notify published and dirty-state projections.");
notifications.Clear();
Check(bindingSurface.BeginEdit() &&
      notifications.SequenceEqual([nameof(Vst3TimelineSurfaceModelV1.Draft),
                                    nameof(Vst3TimelineSurfaceModelV1.HasEditSession)]),
    "Begin edit fixture for discard failed.");
notifications.Clear();
Check(bindingSurface.Discard() &&
      notifications.SequenceEqual([nameof(Vst3TimelineSurfaceModelV1.Draft),
                                    nameof(Vst3TimelineSurfaceModelV1.HasEditSession)]),
    "Discard must notify the closed draft state.");
notificationCount = notifications.Count;
Check(!bindingSurface.Upsert(new Vst3TimelineSurfaceModelV1.TimelineEvent(5, 30, 0.5)) &&
      notifications.Count == notificationCount,
    "Rejected edit without a draft must not notify bindings.");
var editorViewModel = new Vst3TimelineEditorViewModelV1();
var editorNotifications = new List<string>();
editorViewModel.PropertyChanged += (_, args) =>
    editorNotifications.Add(args.PropertyName ?? string.Empty);
Check(!editorViewModel.RegisterTimeline("bad id") &&
      editorViewModel.StatusText.Contains("註冊失敗"),
    "Editor ViewModel must fail closed on invalid timeline registration.");
Check(editorViewModel.RegisterTimeline("editor-timeline") &&
      editorViewModel.TimelineIds.SequenceEqual(["editor-timeline"]) &&
      editorNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.TimelineIds)) &&
      editorNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.StatusText)),
    "Editor ViewModel must project accepted timeline registration.");
editorNotifications.Clear();
Check(!editorViewModel.Select("missing") &&
      editorViewModel.StatusText.Contains("選取失敗") &&
      editorNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.StatusText)),
    "Editor ViewModel must reject selection of an unknown timeline.");
editorNotifications.Clear();
Check(editorViewModel.Select("editor-timeline") &&
      editorViewModel.SelectedTimelineId == "editor-timeline" &&
      editorViewModel.Rows.Count == 0 && !editorViewModel.IsDirty &&
      editorNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.SelectedTimelineId)) &&
      editorNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.Rows)),
    "Editor ViewModel must project an empty selected timeline.");
editorNotifications.Clear();
Check(editorViewModel.BeginEdit() && editorViewModel.HasEditSession &&
      editorViewModel.Rows.Count == 0 &&
      editorNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.HasEditSession)),
    "Editor ViewModel must expose the accepted draft transition.");
Check(!editorViewModel.Select("missing") && editorViewModel.HasEditSession &&
      editorViewModel.StatusText.Contains("草稿進行中"),
    "Editor ViewModel must refuse selection changes during a draft.");
editorViewModel.NewParameterIdText = "7";
editorViewModel.NewPositionText = "480";
editorViewModel.NewValueText = "0.25";
editorNotifications.Clear();
Check(editorViewModel.UpsertFromFields() && editorViewModel.Rows.Count == 1 &&
      editorViewModel.Rows[0] == new Vst3TimelineEditorViewModelV1.TimelineEventRow(
          0, 7U, 480UL, 0.25) &&
      editorViewModel.StatusText.Contains("事件已加入") &&
      editorNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.Rows)),
    "Editor ViewModel must parse and project a valid event.");
editorViewModel.NewParameterIdText = "3";
editorViewModel.NewPositionText = "240";
editorViewModel.NewValueText = "0.75";
Check(editorViewModel.UpsertFromFields() && editorViewModel.Rows.Select(row =>
          (row.ParameterId, row.SamplePosition)).SequenceEqual([(3U, 240UL), (7U, 480UL)]),
    "Editor ViewModel rows must retain canonical sample/parameter ordering.");
var rowsBeforeInvalidInput = editorViewModel.Rows.ToArray();
editorViewModel.NewParameterIdText = "７";
editorViewModel.NewPositionText = "720";
editorViewModel.NewValueText = "0.5";
Check(!editorViewModel.UpsertFromFields() &&
      editorViewModel.Rows.SequenceEqual(rowsBeforeInvalidInput) &&
      editorViewModel.StatusText.Contains("欄位格式無效"),
    "Full-width parameter text must be refused without changing the draft.");
editorViewModel.NewParameterIdText = "8";
editorViewModel.NewPositionText = "720";
editorViewModel.NewValueText = "0,5";
Check(!editorViewModel.UpsertFromFields() &&
      editorViewModel.Rows.SequenceEqual(rowsBeforeInvalidInput),
    "Locale-specific decimal text must be refused without changing the draft.");
editorViewModel.NewParameterIdText = "8";
editorViewModel.NewPositionText = "720";
editorViewModel.NewValueText = "NaN";
Check(!editorViewModel.UpsertFromFields() &&
      editorViewModel.Rows.SequenceEqual(rowsBeforeInvalidInput),
    "Non-finite value text must be refused without changing the draft.");
editorViewModel.NewParameterIdText = "8";
editorViewModel.NewPositionText = "720";
editorViewModel.NewValueText = "1.1";
Check(!editorViewModel.UpsertFromFields() &&
      editorViewModel.Rows.SequenceEqual(rowsBeforeInvalidInput) &&
      editorViewModel.StatusText.Contains("事件超出限制"),
    "Out-of-range normalized value must be refused by the bounded model.");
editorViewModel.NewValueText = "0.15";
Check(editorViewModel.UpsertFromFields() && editorViewModel.Rows.Count == 3 &&
      editorViewModel.Rows[2].Index == 2 &&
      editorViewModel.Rows[2].ParameterId == 8U &&
      editorViewModel.Rows[2].SamplePosition == 720UL &&
      Math.Abs(editorViewModel.Rows[2].NormalizedValue - 0.15) < 1e-12,
    "Editor ViewModel must accept the repaired in-range event.");
editorViewModel.SelectedRowIndex = 0;
var firstRowBeforeValueEdit = editorViewModel.Rows[0];
Check(!editorViewModel.SetSelectedRowValue("NaN") &&
      editorViewModel.Rows[0] == firstRowBeforeValueEdit,
    "Non-finite selected-row value must be refused without changing the row.");
Check(editorViewModel.SetSelectedRowValue("0.9") &&
      editorViewModel.Rows[0] == firstRowBeforeValueEdit with { NormalizedValue = 0.9 },
    "Selected-row value edit must preserve its ordering key.");
editorViewModel.SelectedRowIndex = -1;
Check(!editorViewModel.SetSelectedRowValue("0.4") &&
      editorViewModel.StatusText.Contains("未選取列"),
    "Selected-row edit must refuse when no row is selected.");
editorViewModel.SelectedRowIndex = 0;
editorNotifications.Clear();
Check(editorViewModel.Commit() && !editorViewModel.HasEditSession &&
      editorViewModel.IsDirty && editorViewModel.UndoDepth == 1 &&
      editorViewModel.CanUndo && !editorViewModel.CanRedo &&
      editorNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.IsDirty)) &&
      editorNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.UndoDepth)),
    "Editor ViewModel commit must publish rows and history projections.");
Check(editorViewModel.SaveSelected() && !editorViewModel.IsDirty &&
      editorViewModel.StatusText.Contains("已保存"),
    "Editor ViewModel save must re-baseline dirty state.");
Check(editorViewModel.Undo() && editorViewModel.Rows.Count == 0 &&
      editorViewModel.IsDirty && editorViewModel.CanRedo,
    "Editor ViewModel undo must restore the previous published snapshot.");
Check(editorViewModel.Redo() && editorViewModel.Rows.Count == 3 &&
      !editorViewModel.IsDirty && !editorViewModel.CanRedo,
    "Editor ViewModel redo must restore the saved snapshot.");
Check(editorViewModel.BeginEdit(), "Second editor ViewModel draft fixture failed.");
editorViewModel.SelectedRowIndex = 1;
Check(editorViewModel.SetSelectedRowValue("0.1") && editorViewModel.Commit() &&
      editorViewModel.IsDirty && editorViewModel.UndoDepth == 2,
    "Editor ViewModel second commit must create bounded undo history.");
Check(editorViewModel.Undo() && !editorViewModel.IsDirty && editorViewModel.CanRedo &&
      editorViewModel.Redo() && editorViewModel.IsDirty,
    "Editor ViewModel undo/redo must expose dirty and redo transitions.");
Check(editorViewModel.BeginEdit() && editorViewModel.Discard() &&
      !editorViewModel.HasEditSession && editorViewModel.IsDirty,
    "Editor ViewModel discard must preserve the last published state.");
editorViewModel.SelectedRowIndex = 99;
Check(!editorViewModel.SetSelectedRowValue("0.2") &&
      editorViewModel.StatusText.Contains("數值修改被拒絕"),
    "Editor ViewModel must refuse an out-of-range selected row.");
var historyMirror = new Vst3TimelineSurfaceModelV1();
var historyNotifications = new List<string>();
historyMirror.PropertyChanged += (_, args) =>
    historyNotifications.Add(args.PropertyName ?? string.Empty);
Check(historyMirror.RegisterTimeline("history") && historyMirror.Select("history") &&
      historyMirror.BeginEdit() &&
      historyMirror.Upsert(new Vst3TimelineSurfaceModelV1.TimelineEvent(1, 10, 0.2)) &&
      historyMirror.Commit() && historyMirror.SaveSelected() &&
      historyMirror.BeginEdit() &&
      historyMirror.Upsert(new Vst3TimelineSurfaceModelV1.TimelineEvent(2, 20, 0.4)) &&
      historyMirror.Commit() && historyMirror.Undo() && historyMirror.CanRedo,
    "Managed history-clear fixture failed.");
var publishedBeforeHistoryClear = historyMirror.Published.ToArray();
var dirtyBeforeHistoryClear = historyMirror.IsDirtyState;
historyNotifications.Clear();
historyMirror.ClearHistory();
Check(!historyMirror.CanUndo && !historyMirror.CanRedo && historyMirror.UndoDepth == 0 &&
      historyMirror.RedoDepth == 0 &&
      historyMirror.Published.SequenceEqual(publishedBeforeHistoryClear) &&
      historyMirror.IsDirtyState == dirtyBeforeHistoryClear &&
      historyNotifications.SequenceEqual([
          nameof(Vst3TimelineSurfaceModelV1.CanUndo),
          nameof(Vst3TimelineSurfaceModelV1.UndoDepth),
          nameof(Vst3TimelineSurfaceModelV1.CanRedo),
          nameof(Vst3TimelineSurfaceModelV1.RedoDepth)]),
    "Managed clear-history must clear both stacks without changing published or dirty state.");
Check(historyMirror.BeginEdit() &&
      historyMirror.Upsert(new Vst3TimelineSurfaceModelV1.TimelineEvent(3, 30, 0.6)),
    "Managed open-draft history-clear fixture failed.");
var draftBeforeHistoryClear = historyMirror.Draft!.ToArray();
historyNotifications.Clear();
historyMirror.ClearHistory();
Check(historyMirror.HasEditSession && historyMirror.Draft!.SequenceEqual(draftBeforeHistoryClear) &&
      historyMirror.UndoDepth == 0 && historyMirror.RedoDepth == 0 &&
      historyNotifications.Count == 0,
    "Managed clear-history must preserve an open draft and stay quiet for empty history.");
Check(historyMirror.Discard(), "Managed history-clear draft cleanup failed.");
var rowsBeforeViewModelHistoryClear = editorViewModel.Rows.ToArray();
var dirtyBeforeViewModelHistoryClear = editorViewModel.IsDirty;
editorNotifications.Clear();
Check(editorViewModel.ClearHistory() && !editorViewModel.CanUndo &&
      !editorViewModel.CanRedo && editorViewModel.UndoDepth == 0 &&
      editorViewModel.RedoDepth == 0 &&
      editorViewModel.Rows.SequenceEqual(rowsBeforeViewModelHistoryClear) &&
      editorViewModel.IsDirty == dirtyBeforeViewModelHistoryClear &&
      !editorViewModel.HasEditSession &&
      editorViewModel.StatusText.Contains("編輯歷史已清除") &&
      editorNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.UndoDepth)) &&
      editorNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.RedoDepth)) &&
      editorNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.StatusText)),
    "Editor ViewModel clear-history must preserve rows/dirty state and notify history bindings.");
editorNotifications.Clear();
Check(editorViewModel.ClearHistory() && !editorViewModel.CanUndo &&
      !editorViewModel.CanRedo &&
      editorNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.StatusText)),
    "Editor ViewModel clear-history must remain safe with empty history.");
Check(editorViewModel.BeginEdit(), "Editor ViewModel open-draft clear-history fixture failed.");
var viewModelDraftBeforeHistoryClear = editorViewModel.Rows.ToArray();
Check(editorViewModel.ClearHistory() && editorViewModel.HasEditSession &&
      editorViewModel.Rows.SequenceEqual(viewModelDraftBeforeHistoryClear) &&
      editorViewModel.UndoDepth == 0 && editorViewModel.RedoDepth == 0 &&
      editorViewModel.Discard(),
    "Editor ViewModel clear-history must preserve an open draft.");
Check(!editorViewModel.RemoveSelectedRow() &&
      editorViewModel.StatusText.Contains("無法刪除"),
    "Editor ViewModel row removal must require an open draft.");
editorViewModel.SelectedRowIndex = -1;
Check(editorViewModel.BeginEdit(), "Editor ViewModel removal draft fixture failed.");
Check(!editorViewModel.RemoveSelectedRow() &&
      editorViewModel.StatusText.Contains("無法刪除"),
    "Editor ViewModel row removal must require a selected row.");
editorViewModel.SelectedRowIndex = 1;
var rowsBeforeRemoval = editorViewModel.Rows.ToArray();
editorNotifications.Clear();
Check(editorViewModel.RemoveSelectedRow(), "Editor ViewModel removal fixture failed.");
Check(editorViewModel.SelectedRowIndex == -1,
    "Editor ViewModel removal must clear the selected index.");
Check(editorViewModel.Rows.Count == 2,
    "Editor ViewModel removal must leave two rows.");
Check(editorViewModel.Rows[0] == rowsBeforeRemoval[0],
    "Editor ViewModel removal must keep the leading row.");
Check(editorViewModel.Rows[1] == new Vst3TimelineEditorViewModelV1.TimelineEventRow(
          1, 8U, 720UL, 0.15),
    "Editor ViewModel removal must renumber the trailing row.");
Check(editorViewModel.StatusText.Contains("已刪除選取列"),
    "Editor ViewModel removal must publish a status message.");
Check(editorNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.SelectedRowIndex)) &&
      editorNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.Rows)) &&
      editorNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.StatusText)),
    "Editor ViewModel removal must renumber rows and publish observable changes.");
Check(editorViewModel.Commit() && editorViewModel.Rows.Count == 2 &&
      !editorViewModel.HasEditSession,
    "Editor ViewModel removal must commit the reduced draft.");
Check(editorViewModel.BeginEdit(), "Stale-index removal draft fixture failed.");
editorViewModel.SelectedRowIndex = 99;
Check(!editorViewModel.RemoveSelectedRow() &&
      editorViewModel.StatusText.Contains("索引無效"),
    "Editor ViewModel removal must fail closed for a stale index.");
Check(editorViewModel.Discard(), "Stale-index removal cleanup failed.");
Check(editorViewModel.BeginEdit(), "Undo-after-remove draft fixture failed.");
editorViewModel.SelectedRowIndex = 1;
editorNotifications.Clear();
Check(editorViewModel.RemoveSelectedRow(), "Undo-after-remove fixture failed.");
Check(editorViewModel.Discard(),
    "Undo-after-remove must discard the draft before history moves.");
editorNotifications.Clear();
Check(editorViewModel.Undo(),
    "Editor ViewModel removal undo fixture failed.");
Check(editorViewModel.SelectedRowIndex == -1,
    "Editor ViewModel removal undo must clear the selection.");
Check(editorViewModel.Rows.Count == 3,
    "Editor ViewModel removal undo must restore three rows.");
Check(editorViewModel.Rows[1] == rowsBeforeRemoval[1],
    "Editor ViewModel removal undo must restore the removed row.");
Check(editorViewModel.IsDirty && editorViewModel.CanRedo,
    "Editor ViewModel removal undo must expose dirty and redo state.");
Check(editorNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.Rows)) &&
      editorNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.StatusText)),
    "Editor ViewModel removal undo must publish observable changes.");
Check(editorViewModel.Redo() &&
      editorViewModel.SelectedRowIndex == -1 &&
      editorViewModel.Rows.Count == 2 &&
      editorViewModel.Rows[0] == rowsBeforeRemoval[0] &&
      editorViewModel.Rows[1] == new Vst3TimelineEditorViewModelV1.TimelineEventRow(
          1, 8U, 720UL, 0.15),
    "Editor ViewModel removal redo must re-apply the row deletion.");
var removeMirror = new Vst3TimelineSurfaceModelV1();
var removeMirrorNotifications = new List<string>();
removeMirror.PropertyChanged += (_, args) =>
    removeMirrorNotifications.Add(args.PropertyName ?? string.Empty);
Check(removeMirror.RegisterTimeline("removable") &&
      removeMirror.RegisterTimeline("keeper"),
    "Managed remove-selected fixture failed to register timelines.");
Check(removeMirror.Select("removable") &&
      removeMirror.BeginEdit() &&
      removeMirror.Upsert(new Vst3TimelineSurfaceModelV1.TimelineEvent(5, 40, 0.35)) &&
      removeMirror.Commit(),
    "Managed remove-selected fixture failed to build a published snapshot.");
removeMirror.BeginEdit();
Check(!removeMirror.RemoveSelected() && removeMirror.HasSelection,
    "Managed remove-selected must refuse while an edit session is open.");
Check(removeMirror.Discard(), "Managed remove-selected draft cleanup failed.");
removeMirrorNotifications.Clear();
Check(removeMirror.RemoveSelected() && !removeMirror.HasSelection &&
      removeMirror.SelectedTimelineId is null && removeMirror.TimelineIdCount == 1 &&
      removeMirror.TimelineIds.SequenceEqual(["keeper"]) &&
      removeMirror.Published.Count == 0 && removeMirror.UndoDepth == 0 &&
      removeMirror.RedoDepth == 0 && !removeMirror.CanUndo && !removeMirror.CanRedo &&
      removeMirror.IsDirtyState == false &&
      removeMirrorNotifications.Contains(nameof(Vst3TimelineSurfaceModelV1.TimelineIds)) &&
      removeMirrorNotifications.Contains(nameof(Vst3TimelineSurfaceModelV1.TimelineIdCount)) &&
      removeMirrorNotifications.Contains(nameof(Vst3TimelineSurfaceModelV1.SelectedTimelineId)),
    "Managed remove-selected must clear selection/history and keep other stored timelines.");
Check(!removeMirror.RemoveSelected(),
    "Managed remove-selected without a selection must fail closed.");
var removeViewModel = new Vst3TimelineEditorViewModelV1();
var removeViewModelNotifications = new List<string>();
removeViewModel.PropertyChanged += (_, args) =>
    removeViewModelNotifications.Add(args.PropertyName ?? string.Empty);
Check(removeViewModel.RegisterTimeline("removable-vm") &&
      removeViewModel.Select("removable-vm") && removeViewModel.BeginEdit(),
    "Editor ViewModel timeline-removal fixture failed to prepare.");
removeViewModelNotifications.Clear();
Check(!removeViewModel.RemoveSelectedTimeline() &&
      removeViewModel.StatusText.Contains("刪除失敗") &&
      removeViewModel.HasSelection && removeViewModel.HasEditSession,
    "Editor ViewModel timeline removal must fail closed without a removable selection.");

Check(removeViewModel.Discard(), "Editor ViewModel timeline-removal discard failed.");
removeViewModelNotifications.Clear();
Check(removeViewModel.RemoveSelectedTimeline() &&
      !removeViewModel.HasSelection && removeViewModel.SelectedTimelineId is null &&
      removeViewModel.TimelineIds.Count == 0 && removeViewModel.Rows.Count == 0 &&
      removeViewModel.SelectedRowIndex == -1 && !removeViewModel.IsDirty &&
      removeViewModel.UndoDepth == 0 && removeViewModel.RedoDepth == 0 &&
      removeViewModel.StatusText.Contains("已刪除選取時間軸") &&
      removeViewModelNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.TimelineIds)) &&
      removeViewModelNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.SelectedTimelineId)) &&
      removeViewModelNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.Rows)) &&
      removeViewModelNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.StatusText)),
    "Editor ViewModel timeline removal must refresh projections and notify bindings.");
var selectionSeamViewModel = new Vst3TimelineEditorViewModelV1();
Check(selectionSeamViewModel.RegisterTimeline("compat-selection") &&
      selectionSeamViewModel.SelectedTimelineId is null,
    "Compatibility Preview selection seam must start with no timeline selected.");
Check(selectionSeamViewModel.Select("compat-selection"),
    "Compatibility Preview selection seam must accept a registered timeline.");
Check(selectionSeamViewModel.SelectedTimelineId == "compat-selection" &&
      selectionSeamViewModel.HasSelection && !selectionSeamViewModel.HasEditSession,
    "Compatibility Preview selection seam must reach the view model.");
Check(selectionSeamViewModel.BeginEdit() && selectionSeamViewModel.HasEditSession &&
      selectionSeamViewModel.Discard() && !selectionSeamViewModel.HasEditSession,
    "Selection seam must expose the same draft lifecycle as the formal shell.");
Check(!selectionSeamViewModel.Select("missing") &&
      selectionSeamViewModel.SelectedTimelineId == "compat-selection",
    "Selection seam must keep the selected timeline after a rejected change.");

var shellViewModel = new EasyControlViewModel();
Check(ReferenceEquals(shellViewModel.Vst3TimelineEditor, shellViewModel.Vst3TimelineEditor),
    "Easy control ViewModel must expose a stable VST3 timeline binding surface.");
var uiTimeline = shellViewModel.Vst3TimelineEditor;
var uiTimelineNotifications = new List<string>();
uiTimeline.PropertyChanged += (_, args) => uiTimelineNotifications.Add(args.PropertyName ?? string.Empty);
uiTimeline.NewTimelineIdText = "ui-timeline";
uiTimeline.SelectedRowValueText = "ignored";
Check(uiTimelineNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.NewTimelineIdText)) &&
      uiTimelineNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.SelectedRowValueText)),
    "Timeline registration and selected-row fields must be observable two-way binding sources.");
uiTimelineNotifications.Clear();
Check(uiTimeline.RegisterTimeline(uiTimeline.NewTimelineIdText) &&
      uiTimeline.TimelineIds.SequenceEqual(["ui-timeline"]) &&
      uiTimeline.StatusText.Contains("已註冊"),
    "The V1 seam must register a timeline from its observable ID field.");
Check(uiTimeline.Select(uiTimeline.NewTimelineIdText) && uiTimeline.HasSelection &&
      uiTimeline.SelectedTimelineId == "ui-timeline" && !uiTimeline.HasEditSession,
    "The V1 seam must select the registered timeline fail-closed.");
Check(uiTimeline.BeginEdit() && uiTimeline.NewParameterIdText.Length == 0 &&
      uiTimeline.UpsertFromFields() == false,
    "The V1 seam must reject an empty parsed event without changing state.");
uiTimeline.NewParameterIdText = "11";
uiTimeline.NewPositionText = "220";
uiTimeline.NewValueText = "0.45";
uiTimelineNotifications.Clear();
Check(uiTimeline.UpsertFromFields() && uiTimeline.Rows.Count == 1 &&
      uiTimeline.Rows[0] == new Vst3TimelineEditorViewModelV1.TimelineEventRow(0, 11U, 220UL, 0.45) &&
      uiTimelineNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.Rows)),
    "The V1 seam must add parsed events through bound fields.");
uiTimeline.SelectedRowIndex = 0;
uiTimeline.SelectedRowValueText = "0.65";
Check(uiTimeline.SetSelectedRowValue(uiTimeline.SelectedRowValueText) &&
      uiTimeline.Rows[0] == new Vst3TimelineEditorViewModelV1.TimelineEventRow(0, 11U, 220UL, 0.65),
    "The V1 seam must update the selected row through its bound value field.");
uiTimelineNotifications.Clear();
Check(uiTimeline.Commit() && !uiTimeline.HasEditSession && uiTimeline.IsDirty &&
      uiTimeline.CanUndo && uiTimeline.UndoDepth == 1 &&
      uiTimelineNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.IsDirty)),
    "The V1 seam must expose bounded publish/history projections.");
Check(uiTimeline.SaveSelected() && !uiTimeline.IsDirty &&
      uiTimeline.StatusText.Contains("已保存"),
    "The V1 seam must expose save re-baselining.");
var saveBoundaryViewModel = new Vst3TimelineEditorViewModelV1();
var saveBoundaryNotifications = new List<string>();
saveBoundaryViewModel.PropertyChanged += (_, args) =>
    saveBoundaryNotifications.Add(args.PropertyName ?? string.Empty);
Check(!saveBoundaryViewModel.SaveSelected() &&
      saveBoundaryViewModel.StatusText.Contains("保存失敗"),
    "Editor ViewModel baseline save must refuse without a selected timeline.");
Check(saveBoundaryViewModel.RegisterTimeline("baseline-boundary") &&
      saveBoundaryViewModel.Select("baseline-boundary") &&
      saveBoundaryViewModel.BeginEdit(),
    "Editor ViewModel baseline-save fixture failed to prepare.");
saveBoundaryNotifications.Clear();
Check(!saveBoundaryViewModel.SaveSelected() &&
      saveBoundaryViewModel.HasEditSession &&
      saveBoundaryViewModel.StatusText.Contains("保存失敗"),
    "Editor ViewModel baseline save must refuse while a draft is open.");
Check(saveBoundaryViewModel.Discard(),
    "Editor ViewModel baseline-save draft discard failed.");
saveBoundaryViewModel.NewParameterIdText = "7";
saveBoundaryViewModel.NewPositionText = "90";
saveBoundaryViewModel.NewValueText = "0.25";
Check(saveBoundaryViewModel.BeginEdit() &&
      saveBoundaryViewModel.UpsertFromFields() &&
      saveBoundaryViewModel.Commit() &&
      saveBoundaryViewModel.IsDirty,
    "Editor ViewModel baseline-save dirty fixture failed.");
saveBoundaryNotifications.Clear();
Check(saveBoundaryViewModel.SaveSelected() &&
      !saveBoundaryViewModel.IsDirty &&
      saveBoundaryViewModel.StatusText.Contains("已保存") &&
      saveBoundaryNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.IsDirty)),
    "Editor ViewModel baseline-save success must re-baseline and notify dirty state.");
Check(uiTimeline.RemoveSelectedTimeline() && !uiTimeline.HasSelection &&
      uiTimeline.TimelineIds.Count == 0 && uiTimeline.Rows.Count == 0 &&
      uiTimeline.SelectedRowIndex == -1 &&
      uiTimelineNotifications.Contains(nameof(Vst3TimelineEditorViewModelV1.TimelineIds)),
    "The V1 seam must remove the selected timeline fail-closed.");
var writableSelectionViewModel = new Vst3TimelineEditorViewModelV1();
Check(writableSelectionViewModel.RegisterTimeline("writable-selection"),
    "The writable-selection fixture must register successfully.");
writableSelectionViewModel.SelectedTimelineId = "writable-selection";
Check(writableSelectionViewModel.SelectedTimelineId == "writable-selection" &&
      writableSelectionViewModel.HasSelection,
    "The safe SelectedTimelineId setter must support two-way selection bindings.");
writableSelectionViewModel.BeginEdit();
writableSelectionViewModel.SelectedTimelineId = "missing";
Check(writableSelectionViewModel.SelectedTimelineId == "writable-selection" &&
      writableSelectionViewModel.HasSelection &&
      writableSelectionViewModel.StatusText.Contains("選取失敗") &&
      writableSelectionViewModel.HasEditSession,
    "A rejected selection change must keep the prior selection and open draft.");

Console.WriteLine("Control model checks passed.");
