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
        ViewModel.LoadRouteRules(out _);
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

    private void OnSessionSelectClick(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: ulong handle })
            ViewModel.SelectSession(handle);
    }

    private async void OnApplySessionRouteClick(object sender, RoutedEventArgs e)
    {
        await ViewModel.ApplySelectedSessionRouteAsync();
    }

    private async void OnApplySessionVolumeClick(object sender, RoutedEventArgs e)
    {
        await ViewModel.ApplySelectedSessionVolumeAsync();
    }

    private async void OnApplyRouteRuleClick(object sender, RoutedEventArgs e)
    {
        await ViewModel.ApplyRouteRuleAsync();
    }

    private async void OnRemoveRouteRuleClick(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: string ruleId })
            await ViewModel.ApplyRemoveRouteRuleAsync(ruleId);
    }

    private async void OnClearRouteRulesClick(object sender, RoutedEventArgs e)
    {
        await ViewModel.ApplyClearRouteRulesAsync();
    }

    private async void OnClosed(object sender, WindowEventArgs e)
    {
        await ViewModel.DisconnectAsync();
    }
}
