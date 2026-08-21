// SPDX-License-Identifier: GPL-3.0-only

using Hibiki.ControlModel;

var viewModel = new EasyControlViewModel();
if (!await viewModel.ConnectAsync(TimeSpan.FromSeconds(2)))
    throw new InvalidOperationException("Control model could not connect to Engine Preview.");

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

    Console.WriteLine($"Control model Engine Preview smoke passed (effective={viewModel.EffectiveVolumeDb:0.00} dB, generation={viewModel.VolumeGeneration}, status_sequence={viewModel.StatusSequence}, scene={viewModel.SelectedScene?.Id}).");
}
finally
{
    await viewModel.DisconnectAsync();
}
