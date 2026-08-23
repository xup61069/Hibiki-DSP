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
        ViewModel.LoadCustomScenes(out _);
        ViewModel.LoadRouteRules(out _);
#if HIBIKI_COMPATIBILITY_PREVIEW
        Content = BuildCompatibilityPreview();
#else
        InitializeComponent();
        RootGrid.DataContext = ViewModel;
        ConfigureTitleBar();
#endif
        Closed += OnClosed;
    }

#if !HIBIKI_COMPATIBILITY_PREVIEW
    private void ConfigureTitleBar()
    {
        ExtendsContentIntoTitleBar = true;
        if (RootGrid.FindName("TitleBarDragRegion") is UIElement dragRegion)
        {
            SetTitleBar(dragRegion);
        }
    }
#endif

    private async void OnConnectClick(object sender, RoutedEventArgs e)
    {
        await ViewModel.ConnectAsync(TimeSpan.FromSeconds(3));
    }

    private async void OnEnhanceClick(object sender, RoutedEventArgs e)
    {
        await ViewModel.OneTapEnhanceAsync();
    }

    private async void OnPrepareIrClick(object sender, RoutedEventArgs e)
    {
        var picker = new Windows.Storage.Pickers.FileOpenPicker();

        var handle = WinRT.Interop.WindowNative.GetWindowHandle(this);
        WinRT.Interop.InitializeWithWindow.Initialize(picker, handle);

        picker.FileTypeFilter.Add(".wav");
        var file = await picker.PickSingleFileAsync();

        if (file is not null)
            await ViewModel.PrepareIrAsync(file.Path);
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

    private void OnVst3TimelineSelect(object sender, SelectionChangedEventArgs e)
    {
        if (sender is ComboBox { SelectedItem: string id })
            ViewModel.Vst3TimelineEditor.Select(id);
    }

    private void OnVst3BeginEditClick(object sender, RoutedEventArgs e)
    {
        ViewModel.Vst3TimelineEditor.BeginEdit();
    }

    private void OnVst3CommitClick(object sender, RoutedEventArgs e)
    {
        ViewModel.Vst3TimelineEditor.Commit();
    }

    private void OnVst3DiscardClick(object sender, RoutedEventArgs e)
    {
        ViewModel.Vst3TimelineEditor.Discard();
    }

    private void OnVst3UndoClick(object sender, RoutedEventArgs e)
    {
        ViewModel.Vst3TimelineEditor.Undo();
    }

    private void OnVst3RedoClick(object sender, RoutedEventArgs e)
    {
        ViewModel.Vst3TimelineEditor.Redo();
    }

    private void OnVst3RegisterClick(object sender, RoutedEventArgs e)
    {
        ViewModel.Vst3TimelineEditor.RegisterTimeline(ViewModel.Vst3TimelineEditor.NewTimelineIdText);
        ViewModel.Vst3TimelineEditor.NewTimelineIdText = string.Empty;
    }

    private void OnVst3UpsertClick(object sender, RoutedEventArgs e)
    {
        ViewModel.Vst3TimelineEditor.UpsertFromFields();
    }

    private void OnVst3RowSelectClick(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: int index })
            ViewModel.Vst3TimelineEditor.SelectedRowIndex = index;
    }

    private void OnVst3SetRowValueClick(object sender, RoutedEventArgs e)
    {
            ViewModel.Vst3TimelineEditor.SetSelectedRowValue(ViewModel.Vst3TimelineEditor.SelectedRowValueText);
        ViewModel.Vst3TimelineEditor.SelectedRowValueText = string.Empty;
    }

    private void OnVst3RemoveRowClick(object sender, RoutedEventArgs e)
    {
        ViewModel.Vst3TimelineEditor.RemoveSelectedRow();
    }

    private void OnVst3RemoveTimelineClick(object sender, RoutedEventArgs e)
    {
        ViewModel.Vst3TimelineEditor.RemoveSelectedTimeline();
    }

    private async void OnClosed(object sender, WindowEventArgs e)
    {
        await ViewModel.DisconnectAsync();
    }
}
