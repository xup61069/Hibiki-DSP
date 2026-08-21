// SPDX-License-Identifier: GPL-3.0-only

using Hibiki.ControlModel;

static string CreateSmokeIrWav()
{
    var path = Path.Combine(Path.GetTempPath(), $"hibiki-ir-smoke-{Guid.NewGuid():N}.wav");
    using var stream = File.Create(path);
    using var writer = new BinaryWriter(stream, System.Text.Encoding.ASCII, leaveOpen: false);
    writer.Write(System.Text.Encoding.ASCII.GetBytes("RIFF"));
    writer.Write(44U);
    writer.Write(System.Text.Encoding.ASCII.GetBytes("WAVE"));
    writer.Write(System.Text.Encoding.ASCII.GetBytes("fmt "));
    writer.Write(16U);
    writer.Write((ushort)3); // IEEE Float32
    writer.Write((ushort)1); // mono
    writer.Write(48000U);
    writer.Write(192000U);
    writer.Write((ushort)4);
    writer.Write((ushort)32);
    writer.Write(System.Text.Encoding.ASCII.GetBytes("data"));
    writer.Write(8U);
    writer.Write(1.0F);
    writer.Write(0.0F);
    return path;
}

var viewModel = new EasyControlViewModel();
if (!await viewModel.ConnectAsync(TimeSpan.FromSeconds(2)))
    throw new InvalidOperationException("Control model could not connect to Engine Preview.");

try
{
    var smokeIrPath = CreateSmokeIrWav();
    try
    {
    viewModel.SelectedOutputGroup = "main";
    viewModel.RequestedVolumeDb = -18.0;
    if (!await viewModel.QueueVolumeAsync(TimeSpan.FromMilliseconds(1)))
        throw new InvalidOperationException("Control model volume command was not acknowledged.");
    if (Math.Abs(viewModel.EffectiveVolumeDb - (-18.0)) > 0.01 ||
        viewModel.VolumeGeneration != 1UL)
        throw new InvalidOperationException(
            $"Engine status did not reconcile volume: effective={viewModel.EffectiveVolumeDb:0.00}, generation={viewModel.VolumeGeneration}.");

    if (!await viewModel.OneTapEnhanceAsync())
        throw new InvalidOperationException("Control model One-Tap SceneApply was not acknowledged.");
    if (viewModel.StatusSequence != 1UL || viewModel.SelectedScene?.Id != "game")
        throw new InvalidOperationException(
            $"Engine status/scene did not reconcile: sequence={viewModel.StatusSequence}, scene={viewModel.SelectedScene?.Id ?? "none"}.");

    viewModel.IrPhaseMode = IrPhaseMode.LinearPhase;
    viewModel.IrPhaseStrength = 0.5;
    if (!await viewModel.PrepareIrAsync(smokeIrPath) || !viewModel.HasPreparedIr ||
        !viewModel.IrPrepareStatus.Contains("已在引擎", StringComparison.Ordinal))
        throw new InvalidOperationException("Control model IR prepare was not acknowledged.");

    if (!await viewModel.SelectSceneAsync("movie") || viewModel.HasPreparedIr ||
        !viewModel.IrPrepareStatus.Contains("IR 已清除", StringComparison.Ordinal))
        throw new InvalidOperationException("Scene switch did not clear the prepared IR state.");

    var physicalRefresh = await viewModel.RefreshPhysicalDevicesAsync();
    var physicalRefreshStatus = viewModel.StatusText;
    var mainOutputRoute = viewModel.Expert.RouteHealth.FirstOrDefault(route =>
        route.Id == "main-output");
    Console.WriteLine($"Control model Engine Preview smoke passed (effective={viewModel.EffectiveVolumeDb:0.00} dB, generation={viewModel.VolumeGeneration}, status_sequence={viewModel.StatusSequence}, scene={viewModel.SelectedScene?.Id}, devices={viewModel.PhysicalDevices.Count}, catalog_refresh={physicalRefresh}, catalog_status={physicalRefreshStatus}, main_output={mainOutputRoute?.StateLabel ?? "unknown"}, main_detail={mainOutputRoute?.Detail ?? "missing"}, ir=cleared-on-scene-switch).");
    }
    finally
    {
        for (var attempt = 0; attempt < 20 && File.Exists(smokeIrPath); attempt++)
        {
            try
            {
                File.Delete(smokeIrPath);
            }
            catch (IOException) when (attempt < 19)
            {
                Thread.Sleep(25);
            }
        }
        if (File.Exists(smokeIrPath))
            throw new IOException("Engine Preview still holds the temporary IR file.");
    }
}
finally
{
    await viewModel.DisconnectAsync();
}
