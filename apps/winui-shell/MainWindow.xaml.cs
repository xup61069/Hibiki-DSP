// SPDX-License-Identifier: GPL-3.0-only

using System.ComponentModel;
using System.Linq;
using System.Text.Json;
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
#if !HIBIKI_COMPATIBILITY_PREVIEW
    private static readonly string ThemePreferencePath = System.IO.Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "Hibiki DSP", "ui-theme-v1.json");
    private bool _isDarkTheme;
    private bool _ignoreThemeToggle;
#endif

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
        ApplyTheme(LoadDarkThemePreference(), persist: false);
        ConfigureTitleBar();
#endif
        Closed += OnClosed;
    }

#if !HIBIKI_COMPATIBILITY_PREVIEW
    private void OnThemeToggled(object sender, RoutedEventArgs e)
    {
        if (_ignoreThemeToggle || ThemeToggleSwitch is null) return;
        ApplyTheme(ThemeToggleSwitch.IsOn, persist: true);
    }

    private void ApplyTheme(bool isDark, bool persist)
    {
        _isDarkTheme = isDark;
        RootGrid.RequestedTheme = isDark ? ElementTheme.Dark : ElementTheme.Light;
        _ignoreThemeToggle = true;
        ThemeToggleSwitch.IsOn = isDark;
        _ignoreThemeToggle = false;
        if (persist) SaveDarkThemePreference();
    }

    private static bool LoadDarkThemePreference()
    {
        try
        {
            if (!File.Exists(ThemePreferencePath)) return false;
            using var document = JsonDocument.Parse(File.ReadAllText(ThemePreferencePath));
            return document.RootElement.TryGetProperty("dark", out var dark) &&
                   dark.ValueKind is JsonValueKind.True && dark.GetBoolean();
        }
        catch
        {
            return false;
        }
    }

    private void SaveDarkThemePreference()
    {
        try
        {
            var directory = System.IO.Path.GetDirectoryName(ThemePreferencePath);
            if (string.IsNullOrWhiteSpace(directory)) return;
            Directory.CreateDirectory(directory);
            var json = JsonSerializer.Serialize(new { schema_version = 1, dark = _isDarkTheme });
            File.WriteAllText(ThemePreferencePath, json);
        }
        catch
        {
            // Theme persistence is best effort; a failed write must not affect
            // the current visual state or any engine/control-plane operation.
        }
    }
#endif

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

        DrawZeroDbReferenceLine(width, height);
        DrawFrequencyTickLabels(width, height);

        var pointCollection = new Microsoft.UI.Xaml.Media.PointCollection();
        foreach (var point in points)
        {
            var gainDb = Math.Clamp(point.GainDb, -24.0, 24.0);
            var x = FrequencyToX(point.FrequencyHz, width);
            var y = Math.Clamp(height * 0.5 - gainDb * 2.2, height * 0.08, height * 0.92);
            pointCollection.Append(new Windows.Foundation.Point(x, y));
        }

        var curve = new Polyline
        {
            StrokeThickness = 3.0,
            StrokeLineJoin = PenLineJoin.Round,
            Points = pointCollection,
        };
        switch (frame.Source)
        {
            case EqVisualSourceV1.EqualLoudness:
                curve.Stroke = new SolidColorBrush(Color.FromArgb(255, 0, 120, 212));
                break;
            case EqVisualSourceV1.AdaptiveCorrection:
                // Dash pattern keeps the two sources distinguishable by shape,
                // not only by hue, for color-vision-deficient users.
                curve.Stroke = new SolidColorBrush(Color.FromArgb(255, 0, 153, 188));
                curve.StrokeDashArray = new Microsoft.UI.Xaml.Media.DoubleCollection { 4.0, 2.5 };
                break;
            default:
                curve.Stroke = new SolidColorBrush((Color)Application.Current.Resources["SystemAccentColor"]);
                break;
        }

        EqVisualCanvas.Children.Add(curve);
        UpdateEqCanvasAccessibilityDescription(frame, points);
    }

    private static double FrequencyToX(double frequencyHz, double width)
    {
        return width * (0.02 + (Math.Log10(frequencyHz / 20.0) / Math.Log10(1000.0)) * 0.96);
    }

    private void DrawZeroDbReferenceLine(double width, double height)
    {
        var zeroY = height * 0.5;
        var zeroLine = new Microsoft.UI.Xaml.Shapes.Line
        {
            X1 = 0,
            Y1 = zeroY,
            X2 = width,
            Y2 = zeroY,
            Stroke = new SolidColorBrush(Color.FromArgb(255, 160, 160, 160)),
            StrokeThickness = 1.0,
            IsHitTestVisible = false,
        };
        EqVisualCanvas!.Children.Add(zeroLine);
    }

    private void DrawFrequencyTickLabels(double width, double height)
    {
        var ticks = new (double FrequencyHz, string Label)[]
        {
            (31.0, "31"), (250.0, "250"), (1000.0, "1k"), (8000.0, "8k"),
        };
        foreach (var (frequencyHz, label) in ticks)
        {
            var x = FrequencyToX(frequencyHz, width);
            var tickLabel = new TextBlock
            {
                Text = label,
                FontSize = 10,
                Foreground = new SolidColorBrush(Color.FromArgb(255, 130, 130, 130)),
                IsHitTestVisible = false,
            };
            EqVisualCanvas!.Children.Add(tickLabel);
            Microsoft.UI.Xaml.Controls.Canvas.SetLeft(tickLabel, Math.Clamp(x - 8, 0, Math.Max(width - 20, 0)));
            Microsoft.UI.Xaml.Controls.Canvas.SetTop(tickLabel, height - 16);
        }
    }

    private void UpdateEqCanvasAccessibilityDescription(
        EqVisualSurfaceModelV1 frame,
        IReadOnlyList<EqVisualPointV1> renderedPoints)
    {
        if (EqVisualCanvas is null) return;
        var sourceName = frame.Source switch
        {
            EqVisualSourceV1.EqualLoudness => "等響度補償（實線）",
            EqVisualSourceV1.AdaptiveCorrection => "自適應低頻校正（虛線）",
            _ => "尚未確認來源",
        };
        double minDb = double.MaxValue;
        double maxDb = double.MinValue;
        foreach (var point in renderedPoints)
        {
            minDb = Math.Min(minDb, point.GainDb);
            maxDb = Math.Max(maxDb, point.GainDb);
        }
        var rangeText = minDb <= maxDb
            ? $"，增益範圍 {minDb:F1} 至 {maxDb:F1} dB"
            : string.Empty;
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(
            EqVisualCanvas, $"即時等化器曲線：{sourceName}{rangeText}");
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
        SetSectionVisibility("ShellCalibrateSection", tag == "calibrate");
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

    private void OnOpenExpertClick(object sender, RoutedEventArgs e)
    {
        ViewModel.IsExpert = true;
        if (ShellNav.MenuItems.OfType<NavigationViewItem>().FirstOrDefault(item => item.Tag as string == "volume") is { } volumeItem)
            ShellNav.SelectedItem = volumeItem;
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

    private async void OnImportWizardMeasurementClick(object sender, RoutedEventArgs e)
    {
        var picker = new Windows.Storage.Pickers.FileOpenPicker();

        var handle = WinRT.Interop.WindowNative.GetWindowHandle(this);
        WinRT.Interop.InitializeWithWindow.Initialize(picker, handle);

        picker.FileTypeFilter.Add(".csv");
        picker.FileTypeFilter.Add(".txt");
        var file = await picker.PickSingleFileAsync();

        if (file is not null)
            ViewModel.ImportWizardMeasurement(file.Path);
    }

    private async void OnImportPerChannelWizardMeasurementsClick(object sender, RoutedEventArgs e)
    {
        var picker = new Windows.Storage.Pickers.FileOpenPicker();

        var handle = WinRT.Interop.WindowNative.GetWindowHandle(this);
        WinRT.Interop.InitializeWithWindow.Initialize(picker, handle);

        picker.FileTypeFilter.Add(".csv");
        picker.FileTypeFilter.Add(".txt");
        var files = await picker.PickMultipleFilesAsync();

        if (files.Count > 0)
            ViewModel.ImportWizardPerChannelMeasurements(files.Select(f => f.Path).ToList());
    }

    private void OnCompileWizardClick(object sender, RoutedEventArgs e)
    {
        ViewModel.CompileWizardCorrection();
    }

    private async void OnExportWizardProfileClick(object sender, RoutedEventArgs e)
    {
        var picker = new Windows.Storage.Pickers.FileSavePicker();

        var handle = WinRT.Interop.WindowNative.GetWindowHandle(this);
        WinRT.Interop.InitializeWithWindow.Initialize(picker, handle);

        picker.SuggestedFileName = "hibiki-calibration";
        picker.FileTypeChoices.Add("JSON", [".json"]);
        var file = await picker.PickSaveFileAsync();

        if (file is not null)
            ViewModel.ExportWizardProfile(file.Path);
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
    private async void OnExportCustomScenesClick(object sender, RoutedEventArgs e)
    {
        var picker = new Windows.Storage.Pickers.FileSavePicker();

        var handle = WinRT.Interop.WindowNative.GetWindowHandle(this);
        WinRT.Interop.InitializeWithWindow.Initialize(picker, handle);

        picker.SuggestedFileName = "hibiki-custom-scenes";
        picker.FileTypeChoices.Add("JSON", [".json"]);
        var file = await picker.PickSaveFileAsync();

        if (file is not null)
        {
            ViewModel.ExportCustomScenes(file.Path);
        }
    }

    private async void OnImportCustomScenesClick(object sender, RoutedEventArgs e)
    {
        var picker = new Windows.Storage.Pickers.FileOpenPicker();

        var handle = WinRT.Interop.WindowNative.GetWindowHandle(this);
        WinRT.Interop.InitializeWithWindow.Initialize(picker, handle);

        picker.FileTypeFilter.Add(".json");
        var file = await picker.PickSingleFileAsync();

        if (file is not null)
        {
            ViewModel.ImportCustomScenes(file.Path);
        }
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

#if !HIBIKI_COMPATIBILITY_PREVIEW
    private static double[] ParseWizardNumbers(string? text, out bool parseFailed)
    {
        parseFailed = false;
        var values = new List<double>();
        if (string.IsNullOrWhiteSpace(text))
        {
            return values.ToArray();
        }

        foreach (var token in text.Split(new[] { ',', ';' }, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            if (!double.TryParse(token, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var value))
            {
                parseFailed = true;
                return values.ToArray();
            }
            values.Add(value);
        }
        return values.ToArray();
    }

    private void OnCalibrationWizardBuildClick(object sender, RoutedEventArgs e)
    {
        var frequencies = ParseWizardNumbers(CalibrationWizardFrequencyInput.Text, out var freqParseFailed);
        var levels = ParseWizardNumbers(CalibrationWizardLevelInput.Text, out var levelParseFailed);
        if (freqParseFailed || levelParseFailed || frequencies.Length != levels.Length)
        {
            CalibrationWizardStatusText.Text = "輸入錯誤：頻率與電平都要是有效數值，且數量相同。";
            return;
        }

        if (ViewModel.Expert.TryBuildWizardPeq(frequencies, levels, out var error))
        {
            CalibrationWizardStatusText.Text = ViewModel.Expert.WizardStatusText;
        }
        else
        {
            CalibrationWizardStatusText.Text = error;
        }
    }
#endif
}
