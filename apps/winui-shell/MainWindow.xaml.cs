// SPDX-License-Identifier: GPL-3.0-only

using System.ComponentModel;
using Hibiki.ControlModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Windows.UI;
using Microsoft.UI.Xaml.Input;

namespace Hibiki.WinUI;

public sealed partial class MainWindow : Window
{
    public EasyControlViewModel ViewModel { get; } = new();

    private Microsoft.UI.Dispatching.DispatcherQueueTimer? _eqVisualTransitionTimer;

    public MainWindow()
    {
        ViewModel.LoadCustomScenes(out _);
        ViewModel.LoadRouteRules(out _);
#if HIBIKI_COMPATIBILITY_PREVIEW
        Content = BuildCompatibilityPreview();
#else
        InitializeComponent();
        RootGrid.DataContext = ViewModel;
        ViewModel.PropertyChanged += OnViewModelPropertyChanged;
        EqVisualCanvas.SizeChanged += OnEqVisualCanvasSizeChanged;
        ConfigureTitleBar();
#endif
        Closed += OnClosed;
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(EasyControlViewModel.EqSurface) or
            nameof(EasyControlViewModel.IsConnected))
        {
#if !HIBIKI_COMPATIBILITY_PREVIEW
            DispatcherQueue.TryEnqueue(() =>
            {
                RefreshEqVisualCanvas();
                StartEqVisualTransitionTimer();
            });
#endif
        }

#if !HIBIKI_COMPATIBILITY_PREVIEW
        if (e.PropertyName == nameof(EasyControlViewModel.IsConnected))
        {
            DispatcherQueue.TryEnqueue(() =>
            {
                ConnectButton.Content = ViewModel.IsConnected ? "重新連接" : "連接引擎";
                if (DisconnectedBanner is not null)
                    DisconnectedBanner.IsOpen = !ViewModel.IsConnected;
            });
        }
#endif
    }

#if !HIBIKI_COMPATIBILITY_PREVIEW
    private void OnEqVisualCanvasSizeChanged(object sender, SizeChangedEventArgs e)
    {
        RefreshEqVisualCanvas();
        StartEqVisualTransitionTimer();
    }

    private void RefreshEqVisualCanvas()
    {
        if (EqVisualCanvas is null) return;
        EqVisualCanvas.Children.Clear();

        var frame = ViewModel.EqSurface;
        var points = frame.TransitionProgress >= 1.0 ? frame.TargetPoints : InterpolatePoints(frame.Points, frame.TargetPoints, frame.TransitionProgress);
        var width = double.IsNaN(EqVisualCanvas.ActualWidth) || EqVisualCanvas.ActualWidth < 120.0 ? 480.0 : EqVisualCanvas.ActualWidth;
        var height = double.IsNaN(EqVisualCanvas.ActualHeight) || EqVisualCanvas.ActualHeight < 60.0 ? 140.0 : EqVisualCanvas.ActualHeight;
        var pointCollection = new Microsoft.UI.Xaml.Media.PointCollection();
        foreach (var point in points)
        {
            var gainDb = Math.Clamp(point.GainDb, -24.0, 24.0);
            var x = width * (0.02 + (Math.Log10(point.FrequencyHz / 20.0) / Math.Log10(1000.0)) * 0.96);
            var y = Math.Clamp(height * 0.5 - gainDb * 2.2, height * 0.08, height * 0.92);
            pointCollection.Append(new Windows.Foundation.Point(x, y));
        }

        var strokeColor = frame.Source switch
        {
            EqVisualSourceV1.EqualLoudness => Color.FromArgb(255, 0, 120, 212),
            EqVisualSourceV1.AdaptiveCorrection => Color.FromArgb(255, 0, 153, 188),
            _ => (Color)Application.Current.Resources["SystemAccentColor"],
        };

        EqVisualCanvas.Children.Add(new Polyline
        {
            Stroke = new SolidColorBrush(strokeColor),
            StrokeThickness = 3.0,
            StrokeLineJoin = PenLineJoin.Round,
            Points = pointCollection,
        });
    }

    private static IReadOnlyList<EqVisualPointV1> InterpolatePoints(
        IReadOnlyList<EqVisualPointV1> from,
        IReadOnlyList<EqVisualPointV1> to,
        double progress)
    {
        if (from.Count != to.Count || progress <= 0.0) return from;
        if (progress >= 1.0) return to;
        return from.Select((point, index) => new EqVisualPointV1(
            point.FrequencyHz,
            point.GainDb + ((to[index].GainDb - point.GainDb) * progress))).ToArray();
    }

    private void ConfigureTitleBar()
    {
        ExtendsContentIntoTitleBar = true;
        if (RootGrid.FindName("TitleBarDragRegion") is UIElement dragRegion)
        {
            SetTitleBar(dragRegion);
        }

        // Keep the adaptive hero layout usable below this size.
        var appWindow = AppWindow;
        appWindow.Resize(new Windows.Graphics.SizeInt32(1080, 720));
        if (appWindow.Presenter is Microsoft.UI.Windowing.OverlappedPresenter presenter)
        {
            presenter.PreferredMinimumWidth = 720;
            presenter.PreferredMinimumHeight = 520;
        }
    }

    private void ShowNavigationSection(string tag)
    {
        SetSectionVisibility("ShellHomeSection", tag == "home");
        SetSectionVisibility("ShellScenesSection", tag == "scenes");
        SetSectionVisibility("ShellPresetsSection", tag == "presets");
        SetSectionVisibility("ShellVolumeSection", tag == "volume");
        SetSectionVisibility("ShellRouteSection", tag == "route");
        SetSectionVisibility("ShellExpertSection", tag == "expert");
    }

    private void OnSceneCardPrepared(ItemsRepeater sender, ItemsRepeaterElementPreparedEventArgs args)
    {
        var element = args.Element;
        var delayMs = Math.Min(args.Index, 7) * 40;

        element.Opacity = 0d;

        var timer = DispatcherQueue.CreateTimer();
        timer.Interval = TimeSpan.FromMilliseconds(delayMs);
        timer.IsRepeating = false;
        timer.Tick += (_, _) =>
        {
            var fade = new Microsoft.UI.Xaml.Media.Animation.DoubleAnimation
            {
                From = 0d,
                To = 1d,
                Duration = new Duration(TimeSpan.FromMilliseconds(240)),
            };
            Microsoft.UI.Xaml.Media.Animation.Storyboard.SetTarget(fade, element);
            Microsoft.UI.Xaml.Media.Animation.Storyboard.SetTargetProperty(fade, "Opacity");
            var sb = new Microsoft.UI.Xaml.Media.Animation.Storyboard();
            sb.Children.Add(fade);
            sb.Begin();
        };
        timer.Start();
    }

    private void SetSectionVisibility(string sectionName, bool visible)
    {
        if (RootGrid.FindName(sectionName) is FrameworkElement section)
            section.Visibility = visible ? Visibility.Visible : Visibility.Collapsed;
    }

    private void OnShellNavSelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
    {
        if (args.SelectedItem is NavigationViewItem item && item.Tag is string tag)
            ShowNavigationSection(tag);
    }
#endif

    private void OnEqVisualTransitionTick(object? sender, object e)
    {
        var timer = sender as Microsoft.UI.Dispatching.DispatcherQueueTimer ?? _eqVisualTransitionTimer;
        if (ViewModel.EqSurface.TransitionProgress >= 1.0)
        {
            if (timer is not null)
            {
                timer.Stop();
                timer.Tick -= OnEqVisualTransitionTick;
            }
            _eqVisualTransitionTimer = null;
        }

#if !HIBIKI_COMPATIBILITY_PREVIEW
        RefreshEqVisualCanvas();
#endif
    }

    private void StartEqVisualTransitionTimer()
    {
        if (ViewModel.EqSurface.TransitionProgress < 1.0)
        {
            _eqVisualTransitionTimer ??= Microsoft.UI.Dispatching.DispatcherQueue.GetForCurrentThread().CreateTimer();
            if (_eqVisualTransitionTimer is not null)
            {
                _eqVisualTransitionTimer.Tick -= OnEqVisualTransitionTick;
                _eqVisualTransitionTimer.Interval = TimeSpan.FromMilliseconds(16);
                _eqVisualTransitionTimer.IsRepeating = true;
                _eqVisualTransitionTimer.Tick += OnEqVisualTransitionTick;
                _eqVisualTransitionTimer.Start();
            }
        }
    }

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

    private async void OnRefreshPhysicalDevicesClick(object sender, RoutedEventArgs e)
    {
        await ViewModel.RefreshPhysicalDevicePickerAsync();
    }

    private async void OnSceneClick(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: string sceneId })
            await ViewModel.SelectSceneAsync(sceneId);
    }

    private async void OnAddCustomSceneClick(object sender, RoutedEventArgs e)
    {
        await ViewModel.AddCustomSceneAsync();
    }

    private async void OnRemoveCustomSceneClick(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: string sceneId })
            await ViewModel.RemoveCustomSceneAsync(sceneId);
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

    private async void OnRefreshSessionCatalogClick(object sender, RoutedEventArgs e)
    {
        await ViewModel.RefreshSessionCatalogAsync();
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

    private void OnSectionCardPointerEntered(object sender, PointerRoutedEventArgs e)
    {
        if (sender is Border card)
        {
            card.BorderBrush = (Brush)Application.Current.Resources["AccentFillColorSecondaryBrush"];
        }
    }

    private void OnSectionCardPointerExited(object sender, PointerRoutedEventArgs e)
    {
        if (sender is Border card)
        {
            card.BorderBrush = (Brush)Application.Current.Resources["CardStrokeColorDefaultBrush"];
        }
    }
}
