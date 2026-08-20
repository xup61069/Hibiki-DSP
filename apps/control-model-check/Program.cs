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
Console.WriteLine("Control model checks passed.");
