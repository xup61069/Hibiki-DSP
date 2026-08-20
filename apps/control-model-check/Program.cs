using Hibiki.ControlModel;

static void Check(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

Check(ScenePresetCatalog.EasyDefaults.Count == 4, "Expected four Easy defaults.");
Check(ScenePresetCatalog.EasyDefaults[0].Id == "game", "Game preset missing.");
var snapshot = new ControlSnapshot(UiMode.Easy, AudioControlStatus.Controlled, "main", -8.5, -8.5, false, null, null);
Check(snapshot.DisplayVolume == "-8.5 dB", "dB display must use the effective value.");
var device = new DeviceSwitchModel();
Check(device.Prepare("endpoint-a") && device.Commit(), "Device A commit failed.");
Check(device.Prepare("endpoint-b"), "Device B prepare failed.");
device.Rollback();
Check(device.ActiveDevice == "endpoint-a", "Rollback replaced the active endpoint.");
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
Console.WriteLine("Control model checks passed.");
