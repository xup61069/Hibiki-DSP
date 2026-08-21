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
var snapshotBytes = ControlPayloadsV1.EncodeDeviceCatalogSnapshot(
    [snapshotSpeaker, headphones], 30UL);
Check(ControlPayloadsV1.TryDecodeDeviceCatalogSnapshot(snapshotBytes,
          out var snapshotSequence, out var decodedDevices) && snapshotSequence == 30UL &&
      decodedDevices.Count == 2 && decodedDevices[1].IsDefault,
    "Physical device catalog snapshot did not round-trip.");
var snapshotFrame = new IpcEnvelopeV1(ControlMessageType.DeviceCatalogSnapshot, 0UL, snapshotBytes);
Check(viewModel.ApplyPhysicalDeviceSnapshot(snapshotFrame, out _) &&
      viewModel.PhysicalDevices.Count == 2 && viewModel.PhysicalDevices[1].IsDefault,
    "ViewModel did not atomically apply the device catalog snapshot.");
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
Console.WriteLine("Control model checks passed.");
