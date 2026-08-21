// SPDX-License-Identifier: GPL-3.0-only

using Hibiki.ControlModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;

namespace Hibiki.WinUI;

public sealed partial class MainWindow : Window
{
    public EasyControlViewModel ViewModel { get; } = new();

    public MainWindow()
    {
        InitializeComponent();
        ViewModel.LoadCustomScenes(out _);
        RootGrid.DataContext = ViewModel;
        Closed += OnClosed;
    }

    private async void OnConnectClick(object sender, RoutedEventArgs e)
    {
        await ViewModel.ConnectAsync(TimeSpan.FromSeconds(3));
    }

    private async void OnEnhanceClick(object sender, RoutedEventArgs e)
    {
        await ViewModel.OneTapEnhanceAsync();
    }

    private async void OnSwitchDeviceClick(object sender, RoutedEventArgs e)
    {
        if (ViewModel.SelectedPhysicalDeviceId is { Length: > 0 } endpointId)
            await ViewModel.SwitchPhysicalDeviceAsync(endpointId);
    }

    private async void OnSceneClick(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: string sceneId })
            await ViewModel.SelectSceneAsync(sceneId);
    }

    private void OnAddCustomSceneClick(object sender, RoutedEventArgs e)
    {
        ViewModel.AddCustomScene();
    }

    private async void OnVolumeChanged(object sender, RangeBaseValueChangedEventArgs e)
    {
        if (ViewModel.IsConnected)
            await ViewModel.QueueVolumeAsync();
    }

    private async void OnClosed(object sender, WindowEventArgs e)
    {
        await ViewModel.DisconnectAsync();
    }
}
