// SPDX-License-Identifier: GPL-3.0-only

using System.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Data;
using Microsoft.UI.Xaml.Media;
using Hibiki.ControlModel;
using System.Globalization;

namespace Hibiki.WinUI;

public sealed partial class MainWindow
{
#if HIBIKI_COMPATIBILITY_PREVIEW
    private readonly StackPanel _compatibilityCustomSceneList = new() { Spacing = 8 };
    private readonly StackPanel _compatibilityRouteRuleList = new() { Spacing = 8 };

    private static T ResolveThemeResource<T>(string key) where T : class
    {
        return Application.Current.Resources.TryGetValue(key, out var value) && value is T typed ? typed : null!;
    }

    private Grid BuildCompatibilityPreview()
    {
        var root = new Grid
        {
            Padding = new Thickness(28, 24, 28, 20),
            DataContext = ViewModel,
        };
        SystemBackdrop = new MicaBackdrop { Kind = Microsoft.UI.Composition.SystemBackdrops.MicaKind.Base };

        var content = new StackPanel
        {
            Spacing = 14,
            MaxWidth = 700,
        };
        root.Children.Add(new ScrollViewer { Content = content });

        content.Children.Add(new TextBlock
        {
            Text = "Hibiki DSP",
            Style = ResolveThemeResource<Style>("TitleTextBlockStyle"),
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
        });
        content.Children.Add(new TextBlock
        {
            Text = "Compatibility Preview — 本機控制模型展示；不含虛擬 driver、系統攔截或正式品質驗證。",
            TextWrapping = TextWrapping.Wrap,
            Foreground = ResolveThemeResource<Brush>("TextFillColorSecondaryBrush"),
        });

        var connection = new Button { Content = "連接預覽引擎" };
        AutomationProperties.SetName(connection, "連接 Hibiki 預覽引擎");
        connection.Click += OnConnectClick;
        content.Children.Add(connection);
        content.Children.Add(BoundText("ConnectionStatusText"));

        content.Children.Add(new TextBlock
        {
            Text = "輸出群組",
            Style = ResolveThemeResource<Style>("SubtitleTextBlockStyle"),
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
        });
        var outputGroup = new ComboBox
        {
            DisplayMemberPath = "Name",
            SelectedValuePath = "Id",
        };
        AutomationProperties.SetName(outputGroup, "輸出群組");
        outputGroup.SetBinding(ItemsControl.ItemsSourceProperty, BindingFor("OutputGroups"));
        outputGroup.SetBinding(Selector.SelectedValueProperty, TwoWayBindingFor("SelectedOutputGroup"));
        content.Children.Add(outputGroup);

        content.Children.Add(new TextBlock
        {
            Text = "場景",
            Style = ResolveThemeResource<Style>("SubtitleTextBlockStyle"),
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
        });
        var sceneSelector = new ComboBox
        {
            DisplayMemberPath = "Name",
            SelectedValuePath = "Id",
            MinWidth = 280,
        };
        AutomationProperties.SetName(sceneSelector, "選取情境設定檔");
        sceneSelector.SetBinding(ItemsControl.ItemsSourceProperty, BindingFor("Scenes"));
        sceneSelector.SetBinding(ComboBox.SelectedItemProperty, BindingFor("SelectedScene"));
        sceneSelector.SelectionChanged += OnCompatibilitySceneSelect;
        content.Children.Add(sceneSelector);

        AutomationProperties.SetName(_compatibilityCustomSceneList, "自訂場景列表");
        content.Children.Add(_compatibilityCustomSceneList);
        var customSceneIdBox = new TextBox { Header = "Scene ID（英文小寫）", PlaceholderText = "例如 game-bgm" };
        AutomationProperties.SetName(customSceneIdBox, "自訂場景 ID");
        customSceneIdBox.SetBinding(TextBox.TextProperty, TwoWayBindingFor("CustomSceneId"));
        content.Children.Add(customSceneIdBox);
        var customSceneNameBox = new TextBox { Header = "名稱" };
        AutomationProperties.SetName(customSceneNameBox, "自訂場景名稱");
        customSceneNameBox.SetBinding(TextBox.TextProperty, TwoWayBindingFor("CustomSceneName"));
        content.Children.Add(customSceneNameBox);
        var customSceneDescriptionBox = new TextBox { Header = "說明" };
        AutomationProperties.SetName(customSceneDescriptionBox, "自訂場景說明");
        customSceneDescriptionBox.SetBinding(TextBox.TextProperty, TwoWayBindingFor("CustomSceneDescription"));
        content.Children.Add(customSceneDescriptionBox);
        var addCustomSceneButton = new Button { Content = "加入自訂場景" };
        AutomationProperties.SetName(addCustomSceneButton, "加入自訂場景");
        addCustomSceneButton.Click += OnCompatibilityAddCustomSceneClick;
        content.Children.Add(addCustomSceneButton);
        content.Children.Add(BoundText("StatusText"));
        ViewModel.PropertyChanged += OnCompatibilityViewModelPropertyChanged;
        Closed += OnCompatibilityPreviewClosed;
        SyncCompatibilityCustomScenes();

        var enhance = new Button { Content = "一鍵改善" };
        AutomationProperties.SetName(enhance, "一鍵改善聲音");
        enhance.Click += OnEnhanceClick;
        content.Children.Add(enhance);
        content.Children.Add(BoundText("StatusText"));

        content.Children.Add(new TextBlock
        {
            Text = "音量保護",
            Style = ResolveThemeResource<Style>("SubtitleTextBlockStyle"),
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
        });
        var prepareIrButton = new Button { Content = "選擇並準備 IR WAV 檔" };
        AutomationProperties.SetName(prepareIrButton, "準備 IR WAV 檔案");
        prepareIrButton.Click += OnPrepareIrClick;
        content.Children.Add(prepareIrButton);
        content.Children.Add(BoundText("IrPrepareStatus"));
        content.Children.Add(new TextBlock
        {
            Text = "路由狀態",
            Style = ResolveThemeResource<Style>("SubtitleTextBlockStyle"),
        });
        var routeSummary = new TextBlock { TextWrapping = TextWrapping.Wrap };
        AutomationProperties.SetName(routeSummary, "路由健康狀態摘要");
        routeSummary.SetBinding(TextBlock.TextProperty, BindingFor("Expert.RouteHealthAccessibleSummary"));
        content.Children.Add(routeSummary);

        content.Children.Add(new TextBlock
        {
            Text = "App 工作階段（需以 -EnableSessionRouting 啟動引擎）",
            Style = ResolveThemeResource<Style>("SubtitleTextBlockStyle"),
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
        });
        content.Children.Add(new TextBlock
        {
            Text = "只顯示 bounded metadata；套用 App 音量會寫入 Windows session。實體 per-App 重新送出已由 process-loopback Float32 source 的 E2E 驗證覆蓋（PR #1542；user-space 控制證據）。",
            TextWrapping = TextWrapping.Wrap,
            Foreground = ResolveThemeResource<Brush>("TextFillColorSecondaryBrush"),
        });
        var sessionSelector = new ComboBox { Header = "選取 Expert App", MinWidth = 280 };
        AutomationProperties.SetName(sessionSelector, "選取 Expert App");
        sessionSelector.DisplayMemberPath = nameof(SessionCatalogEntryV1.DisplayName);
        sessionSelector.SelectedValuePath = nameof(SessionCatalogEntryV1.Handle);
        sessionSelector.SetBinding(ItemsControl.ItemsSourceProperty, BindingFor("SessionCatalog"));
        sessionSelector.SetBinding(Selector.SelectedValueProperty, TwoWayBindingFor("SelectedSessionHandle"));
        sessionSelector.SelectionChanged += OnCompatibilitySessionSelect;
        content.Children.Add(sessionSelector);
        var refreshSessionsButton = new Button { Content = "刷新 App 清單" };
        AutomationProperties.SetName(refreshSessionsButton, "刷新 App 工作階段清單");
        refreshSessionsButton.Click += OnCompatibilityRefreshSessionsClick;
        content.Children.Add(refreshSessionsButton);
        content.Children.Add(BoundText("SelectedSessionDisplayText"));
        content.Children.Add(BoundText("SessionCatalogSequenceDisplayText"));
        content.Children.Add(BoundText("SelectedRouteRuleSummary"));
        content.Children.Add(BoundText("SessionVolumeDisplayText"));

        var sessionVolumeSlider = new Slider
        {
            Header = "選取 App 音量 dB",
            Minimum = -60,
            Maximum = 12,
            StepFrequency = 0.5,
        };
        AutomationProperties.SetName(sessionVolumeSlider, "選取 App 音量分貝");
        sessionVolumeSlider.SetBinding(RangeBase.ValueProperty, TwoWayBindingFor("SessionVolumeDb"));
        content.Children.Add(sessionVolumeSlider);
        var sessionMuteCheck = new CheckBox { Content = "靜音選取 App" };
        AutomationProperties.SetName(sessionMuteCheck, "靜音選取的 App 工作階段");
        sessionMuteCheck.SetBinding(CheckBox.IsCheckedProperty, TwoWayBindingFor("SessionMuted"));
        content.Children.Add(sessionMuteCheck);
        var applySessionVolumeButton = new Button { Content = "套用選取 App 音量" };
        AutomationProperties.SetName(applySessionVolumeButton, "套用選取 App 的音量與靜音設定");
        applySessionVolumeButton.Click += OnCompatibilityApplySessionVolumeClick;
        content.Children.Add(applySessionVolumeButton);

        var sessionLaneBox = new TextBox { Header = "App 路由 Lane ID" };
        AutomationProperties.SetName(sessionLaneBox, "選取 App 路由 Lane ID");
        sessionLaneBox.SetBinding(TextBox.TextProperty, TwoWayBindingFor("SessionRouteLaneId"));
        content.Children.Add(sessionLaneBox);
        var sessionOutputGroupBox = new TextBox { Header = "App 路由 Output Group（main／low-latency／surround）" };
        AutomationProperties.SetName(sessionOutputGroupBox, "選取 App 路由 Output Group");
        sessionOutputGroupBox.SetBinding(TextBox.TextProperty, TwoWayBindingFor("SessionRouteOutputGroup"));
        content.Children.Add(sessionOutputGroupBox);
        var applySessionRouteButton = new Button { Content = "套用選取 App 路由" };
        AutomationProperties.SetName(applySessionRouteButton, "套用選取 App 的路由設定");
        applySessionRouteButton.Click += OnCompatibilityApplySessionRouteClick;
        content.Children.Add(applySessionRouteButton);

        content.Children.Add(new TextBlock
        {
            Text = "App 路由預設（Expert）",
            Style = ResolveThemeResource<Style>("SubtitleTextBlockStyle"),
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
        });
        content.Children.Add(new TextBlock
        {
            Text = "建立後會保存到本機；只有 App 清單已同步且引擎回覆 Ack，才會顯示為已套用。App ID 或顯示名稱至少填一項。",
            TextWrapping = TextWrapping.Wrap,
            Foreground = ResolveThemeResource<Brush>("TextFillColorSecondaryBrush"),
        });
        AutomationProperties.SetName(_compatibilityRouteRuleList, "App 路由預設列表");
        content.Children.Add(_compatibilityRouteRuleList);
        var routeRuleIdBox = new TextBox { Header = "預設 ID（小寫英文／數字／- _ .）" };
        AutomationProperties.SetName(routeRuleIdBox, "App 路由預設 ID");
        routeRuleIdBox.SetBinding(TextBox.TextProperty, TwoWayBindingFor("RouteRuleId"));
        content.Children.Add(routeRuleIdBox);
        var routeRuleAppIdBox = new TextBox { Header = "App ID（例如 game.exe，可留空）" };
        AutomationProperties.SetName(routeRuleAppIdBox, "App 路由預設 App ID");
        routeRuleAppIdBox.SetBinding(TextBox.TextProperty, TwoWayBindingFor("RouteRuleAppId"));
        content.Children.Add(routeRuleAppIdBox);
        var routeRuleDisplayNameBox = new TextBox { Header = "顯示名稱（可留空）" };
        AutomationProperties.SetName(routeRuleDisplayNameBox, "App 路由預設顯示名稱");
        routeRuleDisplayNameBox.SetBinding(TextBox.TextProperty, TwoWayBindingFor("RouteRuleDisplayName"));
        content.Children.Add(routeRuleDisplayNameBox);
        var routeRuleLaneBox = new TextBox { Header = "Lane ID" };
        AutomationProperties.SetName(routeRuleLaneBox, "App 路由預設 Lane ID");
        routeRuleLaneBox.SetBinding(TextBox.TextProperty, TwoWayBindingFor("RouteRuleLaneId"));
        content.Children.Add(routeRuleLaneBox);
        var routeRuleOutputBox = new TextBox { Header = "Output Group（main／low-latency／surround）" };
        AutomationProperties.SetName(routeRuleOutputBox, "App 路由預設 Output Group");
        routeRuleOutputBox.SetBinding(TextBox.TextProperty, TwoWayBindingFor("RouteRuleOutputGroup"));
        content.Children.Add(routeRuleOutputBox);
        var routeRulePriorityBox = new NumberBox
        {
            Header = "優先級",
            Minimum = -1_000_000,
            Maximum = 1_000_000,
            SpinButtonPlacementMode = NumberBoxSpinButtonPlacementMode.Compact,
        };
        AutomationProperties.SetName(routeRulePriorityBox, "App 路由預設優先級");
        routeRulePriorityBox.SetBinding(NumberBox.ValueProperty, TwoWayBindingFor("RouteRulePriority"));
        content.Children.Add(routeRulePriorityBox);
        var routeRuleGainBox = new NumberBox
        {
            Header = "補償增益 dB",
            Minimum = -144,
            Maximum = 12,
            SmallChange = 0.5,
            SpinButtonPlacementMode = NumberBoxSpinButtonPlacementMode.Compact,
        };
        AutomationProperties.SetName(routeRuleGainBox, "App 路由預設補償增益分貝");
        routeRuleGainBox.SetBinding(NumberBox.ValueProperty, TwoWayBindingFor("RouteRuleMakeupGainDb"));
        content.Children.Add(routeRuleGainBox);
        var routeRuleEnabledCheck = new CheckBox { Content = "啟用預設" };
        AutomationProperties.SetName(routeRuleEnabledCheck, "啟用 App 路由預設");
        routeRuleEnabledCheck.SetBinding(CheckBox.IsCheckedProperty, TwoWayBindingFor("RouteRuleEnabled"));
        content.Children.Add(routeRuleEnabledCheck);
        var routeRuleGainOwnerSelector = new ComboBox { Header = "增益控制者" };
        AutomationProperties.SetName(routeRuleGainOwnerSelector, "App 路由預設增益控制者");
        routeRuleGainOwnerSelector.SetBinding(ItemsControl.ItemsSourceProperty, BindingFor("RouteRuleGainOwners"));
        routeRuleGainOwnerSelector.SetBinding(Selector.SelectedItemProperty, TwoWayBindingFor("RouteRuleGainOwner"));
        content.Children.Add(routeRuleGainOwnerSelector);
        var applyRouteRuleButton = new Button { Content = "新增／更新預設" };
        AutomationProperties.SetName(applyRouteRuleButton, "新增或更新 App 路由預設");
        applyRouteRuleButton.Click += OnCompatibilityApplyRouteRuleClick;
        content.Children.Add(applyRouteRuleButton);
        var clearRouteRulesButton = new Button { Content = "清除全部預設" };
        AutomationProperties.SetName(clearRouteRulesButton, "清除全部 App 路由預設");
        clearRouteRulesButton.Click += OnCompatibilityClearRouteRulesClick;
        content.Children.Add(clearRouteRulesButton);
        SyncCompatibilityRouteRules();

        var volume = new Slider
        {
            Minimum = -60,
            Maximum = 0,
        };
        AutomationProperties.SetName(volume, "系統音量");
        volume.SetBinding(RangeBase.ValueProperty, TwoWayBindingFor("RequestedVolumeDb"));
        volume.ValueChanged += OnVolumeChanged;
        content.Children.Add(volume);
        content.Children.Add(new TextBlock
        {
            Text = "實際有效音量（dB）",
            Foreground = ResolveThemeResource<Brush>("TextFillColorSecondaryBrush"),
        });
        content.Children.Add(BoundText("EffectiveVolumeDb"));
        content.Children.Add(BoundText("SafetyStatusText"));

        var eqStatus = new TextBlock { TextWrapping = TextWrapping.Wrap };
        AutomationProperties.SetName(eqStatus, "即時等化器狀態");
        eqStatus.SetBinding(TextBlock.TextProperty, BindingFor("EqSurface.StateText"));
        content.Children.Add(eqStatus);

        content.Children.Add(new TextBlock
        {
            Text = "正式預覽會在 Windows 11 24H2+、VS 2026 與鎖定 SDK/WDK 上重跑完整 XAML、無障礙與音訊驗收。",
            TextWrapping = TextWrapping.Wrap,
            Foreground = ResolveThemeResource<Brush>("TextFillColorSecondaryBrush"),
        });
        return root;
    }

    private void OnCompatibilitySceneSelect(object sender, SelectionChangedEventArgs e)
    {
        if (sender is ComboBox { SelectedItem: SceneCard scene })
            _ = SelectCompatibilitySceneAsync(scene.Id);
    }

    private async Task SelectCompatibilitySceneAsync(string sceneId)
    {
        await ViewModel.SelectSceneAsync(sceneId);
    }

    private void OnCompatibilityViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(EasyControlViewModel.CustomSceneCards))
            SyncCompatibilityCustomScenes();
        if (e.PropertyName == nameof(EasyControlViewModel.RouteRules))
            SyncCompatibilityRouteRules();
    }

    private void OnCompatibilitySessionSelect(object sender, SelectionChangedEventArgs e)
    {
        // SelectedSessionHandle is a TwoWay binding; the ViewModel owns
        // fail-closed validation and rule-preview refresh on selection change.
    }

    private async void OnCompatibilityRefreshSessionsClick(object sender, RoutedEventArgs e)
    {
        await ViewModel.RefreshSessionCatalogAsync();
    }

    private async void OnCompatibilityApplySessionVolumeClick(object sender, RoutedEventArgs e)
    {
        await ViewModel.ApplySelectedSessionVolumeAsync();
    }

    private async void OnCompatibilityApplySessionRouteClick(object sender, RoutedEventArgs e)
    {
        await ViewModel.ApplySelectedSessionRouteAsync();
    }

    private void OnCompatibilityPreviewClosed(object sender, WindowEventArgs e)
    {
        ViewModel.PropertyChanged -= OnCompatibilityViewModelPropertyChanged;
    }

    private void SyncCompatibilityCustomScenes()
    {
        _compatibilityCustomSceneList.Children.Clear();
        var secondaryBrush = ResolveThemeResource<Brush>("TextFillColorSecondaryBrush");
        foreach (var scene in ViewModel.CustomSceneCards)
        {
            var row = new Grid { ColumnSpacing = 8 };
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            var details = new StackPanel { Spacing = 2, VerticalAlignment = VerticalAlignment.Center };
            var name = new TextBlock
            {
                Text = scene.Name,
                FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
                TextWrapping = TextWrapping.Wrap,
            };
            var id = new TextBlock { Text = scene.Id, TextWrapping = TextWrapping.Wrap, Foreground = secondaryBrush };
            details.Children.Add(name);
            details.Children.Add(id);
            Grid.SetColumn(details, 0);
            var removeButton = new Button { Content = "移除", Tag = scene.Id };
            AutomationProperties.SetName(removeButton, "移除自訂場景");
            removeButton.Click += OnCompatibilityRemoveCustomSceneClick;
            Grid.SetColumn(removeButton, 1);
            row.Children.Add(details);
            row.Children.Add(removeButton);
            _compatibilityCustomSceneList.Children.Add(row);
        }
    }

    private async void OnCompatibilityAddCustomSceneClick(object sender, RoutedEventArgs e)
    {
        await ViewModel.AddCustomSceneAsync();
    }

    private async void OnCompatibilityRemoveCustomSceneClick(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: string sceneId })
            await ViewModel.RemoveCustomSceneAsync(sceneId);
    }

    private async void OnCompatibilityApplyRouteRuleClick(object sender, RoutedEventArgs e)
    {
        await ViewModel.ApplyRouteRuleAsync();
    }

    private async void OnCompatibilityRemoveRouteRuleClick(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: string ruleId })
            await ViewModel.ApplyRemoveRouteRuleAsync(ruleId);
    }

    private async void OnCompatibilityClearRouteRulesClick(object sender, RoutedEventArgs e)
    {
        await ViewModel.ApplyClearRouteRulesAsync();
    }

    private void SyncCompatibilityRouteRules()
    {
        _compatibilityRouteRuleList.Children.Clear();
        var secondaryBrush = ResolveThemeResource<Brush>("TextFillColorSecondaryBrush");
        foreach (var rule in ViewModel.RouteRules)
        {
            var row = new Grid { ColumnSpacing = 8 };
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            var summary = new TextBlock
            {
                Text = rule.Summary,
                TextWrapping = TextWrapping.Wrap,
                VerticalAlignment = VerticalAlignment.Center,
                Foreground = secondaryBrush,
            };
            AutomationProperties.SetName(summary, $"App 路由預設 {rule.RuleId}");
            Grid.SetColumn(summary, 0);
            var removeButton = new Button { Content = "移除", Tag = rule.RuleId };
            AutomationProperties.SetName(removeButton, $"移除 App 路由預設 {rule.RuleId}");
            removeButton.Click += OnCompatibilityRemoveRouteRuleClick;
            Grid.SetColumn(removeButton, 1);
            row.Children.Add(summary);
            row.Children.Add(removeButton);
            _compatibilityRouteRuleList.Children.Add(row);
        }
    }

    private static Binding BindingFor(string property) => new()
    {
        Path = new PropertyPath(property),
        Mode = BindingMode.OneWay,
    };

    private static Binding TwoWayBindingFor(string property) => new()
    {
        Path = new PropertyPath(property),
        Mode = BindingMode.TwoWay,
    };

    private static TextBlock BoundText(string property)
    {
        var text = new TextBlock
        {
            TextWrapping = TextWrapping.Wrap,
            Foreground = ResolveThemeResource<Brush>("TextFillColorSecondaryBrush"),
        };
        text.SetBinding(TextBlock.TextProperty, new Binding
        {
            Path = new PropertyPath(property),
            Mode = BindingMode.OneWay,
        });
        return text;
    }
#endif
}
