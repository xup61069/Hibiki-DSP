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

    private static void PolishCompatibilityControl(Control control)
    {
        // Keep layout polish to properties supported by the fallback host.
        // CornerRadius and custom XAML brushes are target-shell features and
        // can fail-fast when the compiled XAML resources are unavailable.
        control.Margin = new Thickness(0, 2, 0, 2);
        control.MinHeight = 40;
        if (control is Button button)
            button.Padding = new Thickness(16, 8, 16, 8);
        else if (control is TextBox textBox)
            textBox.Padding = new Thickness(12, 8, 12, 8);
        else if (control is ComboBox comboBox)
            comboBox.Padding = new Thickness(10, 0, 10, 0);
    }

    private Grid BuildCompatibilityPreview()
    {
        var root = new Grid
        {
            Padding = new Thickness(28, 24, 28, 20),
            DataContext = ViewModel,
            Background = new SolidColorBrush(Windows.UI.Color.FromArgb(255, 244, 247, 251)),
            // The fallback host does not load App.xaml resources. Keep its
            // controls on a deterministic light surface instead of inheriting
            // a dark system theme and rendering as a black page.
            RequestedTheme = ElementTheme.Light,
        };

        var content = new StackPanel
        {
            Spacing = 14,
            MaxWidth = 700,
        };
        root.Children.Add(new ScrollViewer { Content = content });

        content.Children.Add(new TextBlock
        {
            Text = "Hibiki DSP",
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
        });
        content.Children.Add(new TextBlock
        {
            Text = "Compatibility Preview — 本機控制模型展示；不含虛擬 driver、系統攔截或正式品質驗證。",
            TextWrapping = TextWrapping.Wrap,
        });
        content.Children.Add(new TextBlock
        {
            Text = "連線與工作區",
            FontSize = 16,
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
            Margin = new Thickness(0, 8, 0, 0),
        });

        void Add(UIElement element)
        {
            if (element is Control control) PolishCompatibilityControl(control);
            content.Children.Add(element);
        }

        var connection = new Button { Content = "連接預覽引擎" };
        AutomationProperties.SetName(connection, "連接 Hibiki 預覽引擎");
        connection.Click += OnConnectClick;
        Add(connection);
        Add(BoundText("ConnectionStatusText"));

        Add(new TextBlock
        {
            Text = "輸出群組",
            FontSize = 16,
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
            Margin = new Thickness(0, 8, 0, 0),
        });
        var outputGroup = new ComboBox
        {
            DisplayMemberPath = "Name",
            SelectedValuePath = "Id",
        };
        AutomationProperties.SetName(outputGroup, "輸出群組");
        outputGroup.SetBinding(ItemsControl.ItemsSourceProperty, BindingFor("OutputGroups"));
        outputGroup.SetBinding(Selector.SelectedValueProperty, TwoWayBindingFor("SelectedOutputGroup"));
        Add(outputGroup);

        Add(new TextBlock
        {
            Text = "場景",
            FontSize = 16,
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
            Margin = new Thickness(0, 8, 0, 0),
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
        Add(sceneSelector);

        Add(new TextBlock
        {
            Text = "自訂場景",
            FontSize = 16,
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
            Margin = new Thickness(0, 16, 0, 0),
        });
        AutomationProperties.SetName(_compatibilityCustomSceneList, "自訂場景列表");
        Add(_compatibilityCustomSceneList);
        var customSceneIdBox = new TextBox { Header = "Scene ID（英文小寫）", PlaceholderText = "例如 game-bgm" };
        AutomationProperties.SetName(customSceneIdBox, "自訂場景 ID");
        customSceneIdBox.SetBinding(TextBox.TextProperty, TwoWayBindingFor("CustomSceneId"));
        Add(customSceneIdBox);
        var customSceneNameBox = new TextBox { Header = "名稱" };
        AutomationProperties.SetName(customSceneNameBox, "自訂場景名稱");
        customSceneNameBox.SetBinding(TextBox.TextProperty, TwoWayBindingFor("CustomSceneName"));
        Add(customSceneNameBox);
        var customSceneDescriptionBox = new TextBox { Header = "說明" };
        AutomationProperties.SetName(customSceneDescriptionBox, "自訂場景說明");
        customSceneDescriptionBox.SetBinding(TextBox.TextProperty, TwoWayBindingFor("CustomSceneDescription"));
        Add(customSceneDescriptionBox);
        var addCustomSceneButton = new Button { Content = "加入自訂場景" };
        AutomationProperties.SetName(addCustomSceneButton, "加入自訂場景");
        addCustomSceneButton.Click += OnCompatibilityAddCustomSceneClick;
        Add(addCustomSceneButton);
        Add(BoundText("StatusText"));
        ViewModel.PropertyChanged += OnCompatibilityViewModelPropertyChanged;
        Closed += OnCompatibilityPreviewClosed;
        SyncCompatibilityCustomScenes();

        Add(new TextBlock
        {
            Text = "音量保護與快速改善",
            FontSize = 16,
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
            Margin = new Thickness(0, 16, 0, 0),
        });
        var enhance = new Button { Content = "一鍵改善" };
        AutomationProperties.SetName(enhance, "一鍵改善聲音");
        enhance.Click += OnEnhanceClick;
        Add(enhance);
        Add(BoundText("StatusText"));

        Add(new TextBlock
        {
            Text = "音量保護",
            FontSize = 16,
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
            Margin = new Thickness(0, 8, 0, 0),
        });
        var prepareIrButton = new Button { Content = "選擇並準備 IR WAV 檔" };
        AutomationProperties.SetName(prepareIrButton, "準備 IR WAV 檔案");
        prepareIrButton.Click += OnPrepareIrClick;
        Add(prepareIrButton);
        Add(BoundText("IrPrepareStatus"));
        Add(new TextBlock
        {
            Text = "路由狀態",
            FontSize = 16,
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
            Margin = new Thickness(0, 8, 0, 0),
        });
        var routeSummary = new TextBlock { TextWrapping = TextWrapping.Wrap };
        AutomationProperties.SetName(routeSummary, "路由健康狀態摘要");
        routeSummary.SetBinding(TextBlock.TextProperty, BindingFor("Expert.RouteHealthAccessibleSummary"));
        Add(routeSummary);

        Add(new TextBlock
        {
            Text = "App 工作階段（需以 -EnableSessionRouting 啟動引擎）",
            FontSize = 16,
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
            Margin = new Thickness(0, 16, 0, 0),
        });
        Add(new TextBlock
        {
            Text = "只顯示 bounded metadata；套用 App 音量會寫入 Windows session，實體 per-App 重新送出仍未驗證。",
            TextWrapping = TextWrapping.Wrap,
        });
        var sessionSelector = new ComboBox { Header = "選取 Expert App", MinWidth = 280 };
        AutomationProperties.SetName(sessionSelector, "選取 Expert App");
        sessionSelector.DisplayMemberPath = nameof(SessionCatalogEntryV1.DisplayName);
        sessionSelector.SelectedValuePath = nameof(SessionCatalogEntryV1.Handle);
        sessionSelector.SetBinding(ItemsControl.ItemsSourceProperty, BindingFor("SessionCatalog"));
        sessionSelector.SetBinding(Selector.SelectedValueProperty, TwoWayBindingFor("SelectedSessionHandle"));
        sessionSelector.SelectionChanged += OnCompatibilitySessionSelect;
        Add(sessionSelector);
        var refreshSessionsButton = new Button { Content = "刷新 App 清單" };
        AutomationProperties.SetName(refreshSessionsButton, "刷新 App 工作階段清單");
        refreshSessionsButton.Click += OnCompatibilityRefreshSessionsClick;
        Add(refreshSessionsButton);
        Add(BoundText("SelectedSessionDisplayText"));
        Add(BoundText("SessionCatalogSequenceDisplayText"));
        Add(BoundText("SelectedRouteRuleSummary"));
        Add(BoundText("SessionVolumeDisplayText"));

        var sessionVolumeSlider = new Slider
        {
            Header = "選取 App 音量 dB",
            Minimum = -60,
            Maximum = 0,
            StepFrequency = 0.5,
        };
        AutomationProperties.SetName(sessionVolumeSlider, "選取 App 音量分貝");
        sessionVolumeSlider.SetBinding(RangeBase.ValueProperty, TwoWayBindingFor("SessionVolumeDb"));
        Add(sessionVolumeSlider);
        var sessionMuteCheck = new CheckBox { Content = "靜音選取 App" };
        AutomationProperties.SetName(sessionMuteCheck, "靜音選取的 App 工作階段");
        sessionMuteCheck.SetBinding(CheckBox.IsCheckedProperty, TwoWayBindingFor("SessionMuted"));
        Add(sessionMuteCheck);
        var applySessionVolumeButton = new Button { Content = "套用選取 App 音量" };
        AutomationProperties.SetName(applySessionVolumeButton, "套用選取 App 的音量與靜音設定");
        applySessionVolumeButton.Click += OnCompatibilityApplySessionVolumeClick;
        Add(applySessionVolumeButton);

        var sessionLaneBox = new TextBox { Header = "App 路由 Lane ID" };
        AutomationProperties.SetName(sessionLaneBox, "選取 App 路由 Lane ID");
        sessionLaneBox.SetBinding(TextBox.TextProperty, TwoWayBindingFor("SessionRouteLaneId"));
        Add(sessionLaneBox);
        var sessionOutputGroupBox = new TextBox { Header = "App 路由 Output Group（main／low-latency／surround）" };
        AutomationProperties.SetName(sessionOutputGroupBox, "選取 App 路由 Output Group");
        sessionOutputGroupBox.SetBinding(TextBox.TextProperty, TwoWayBindingFor("SessionRouteOutputGroup"));
        Add(sessionOutputGroupBox);
        var applySessionRouteButton = new Button { Content = "套用選取 App 路由" };
        AutomationProperties.SetName(applySessionRouteButton, "套用選取 App 的路由設定");
        applySessionRouteButton.Click += OnCompatibilityApplySessionRouteClick;
        Add(applySessionRouteButton);

        Add(new TextBlock
        {
            Text = "App 路由預設（Expert）",
            FontSize = 16,
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
            Margin = new Thickness(0, 16, 0, 0),
        });
        Add(new TextBlock
        {
            Text = "建立後會保存到本機；只有 App 清單已同步且引擎回覆 Ack，才會顯示為已套用。App ID 或顯示名稱至少填一項。",
            TextWrapping = TextWrapping.Wrap,
        });
        AutomationProperties.SetName(_compatibilityRouteRuleList, "App 路由預設列表");
        Add(_compatibilityRouteRuleList);
        var routeRuleIdBox = new TextBox { Header = "預設 ID（小寫英文／數字／- _ .）" };
        AutomationProperties.SetName(routeRuleIdBox, "App 路由預設 ID");
        routeRuleIdBox.SetBinding(TextBox.TextProperty, TwoWayBindingFor("RouteRuleId"));
        Add(routeRuleIdBox);
        var routeRuleAppIdBox = new TextBox { Header = "App ID（例如 game.exe，可留空）" };
        AutomationProperties.SetName(routeRuleAppIdBox, "App 路由預設 App ID");
        routeRuleAppIdBox.SetBinding(TextBox.TextProperty, TwoWayBindingFor("RouteRuleAppId"));
        Add(routeRuleAppIdBox);
        var routeRuleDisplayNameBox = new TextBox { Header = "顯示名稱（可留空）" };
        AutomationProperties.SetName(routeRuleDisplayNameBox, "App 路由預設顯示名稱");
        routeRuleDisplayNameBox.SetBinding(TextBox.TextProperty, TwoWayBindingFor("RouteRuleDisplayName"));
        Add(routeRuleDisplayNameBox);
        var routeRuleLaneBox = new TextBox { Header = "Lane ID" };
        AutomationProperties.SetName(routeRuleLaneBox, "App 路由預設 Lane ID");
        routeRuleLaneBox.SetBinding(TextBox.TextProperty, TwoWayBindingFor("RouteRuleLaneId"));
        Add(routeRuleLaneBox);
        var routeRuleOutputBox = new TextBox { Header = "Output Group（main／low-latency／surround）" };
        AutomationProperties.SetName(routeRuleOutputBox, "App 路由預設 Output Group");
        routeRuleOutputBox.SetBinding(TextBox.TextProperty, TwoWayBindingFor("RouteRuleOutputGroup"));
        Add(routeRuleOutputBox);
        var routeRulePriorityBox = new NumberBox
        {
            Header = "優先級",
            Minimum = -1_000_000,
            Maximum = 1_000_000,
            SpinButtonPlacementMode = NumberBoxSpinButtonPlacementMode.Compact,
        };
        AutomationProperties.SetName(routeRulePriorityBox, "App 路由預設優先級");
        routeRulePriorityBox.SetBinding(NumberBox.ValueProperty, TwoWayBindingFor("RouteRulePriority"));
        Add(routeRulePriorityBox);
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
        Add(routeRuleGainBox);
        var routeRuleEnabledCheck = new CheckBox { Content = "啟用預設" };
        AutomationProperties.SetName(routeRuleEnabledCheck, "啟用 App 路由預設");
        routeRuleEnabledCheck.SetBinding(CheckBox.IsCheckedProperty, TwoWayBindingFor("RouteRuleEnabled"));
        Add(routeRuleEnabledCheck);
        var routeRuleGainOwnerSelector = new ComboBox { Header = "增益控制者" };
        AutomationProperties.SetName(routeRuleGainOwnerSelector, "App 路由預設增益控制者");
        routeRuleGainOwnerSelector.SetBinding(ItemsControl.ItemsSourceProperty, BindingFor("RouteRuleGainOwners"));
        routeRuleGainOwnerSelector.SetBinding(Selector.SelectedItemProperty, TwoWayBindingFor("RouteRuleGainOwner"));
        Add(routeRuleGainOwnerSelector);
        var applyRouteRuleButton = new Button { Content = "新增／更新預設" };
        AutomationProperties.SetName(applyRouteRuleButton, "新增或更新 App 路由預設");
        applyRouteRuleButton.Click += OnCompatibilityApplyRouteRuleClick;
        Add(applyRouteRuleButton);
        var clearRouteRulesButton = new Button { Content = "清除全部預設" };
        AutomationProperties.SetName(clearRouteRulesButton, "清除全部 App 路由預設");
        clearRouteRulesButton.Click += OnCompatibilityClearRouteRulesClick;
        Add(clearRouteRulesButton);
        SyncCompatibilityRouteRules();

        Add(new TextBlock
        {
            Text = "主音量與診斷",
            FontSize = 16,
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
            Margin = new Thickness(0, 16, 0, 0),
        });
        var volume = new Slider
        {
            Minimum = -60,
            Maximum = 0,
        };
        AutomationProperties.SetName(volume, "系統音量");
        volume.SetBinding(RangeBase.ValueProperty, TwoWayBindingFor("RequestedVolumeDb"));
        volume.ValueChanged += OnVolumeChanged;
        Add(volume);
        Add(new TextBlock
        {
            Text = "實際有效音量（dB）",
        });
        Add(BoundText("EffectiveVolumeDb"));
        Add(BoundText("SafetyStatusText"));

        var eqStatus = new TextBlock { TextWrapping = TextWrapping.Wrap };
        AutomationProperties.SetName(eqStatus, "即時等化器狀態");
        eqStatus.SetBinding(TextBlock.TextProperty, BindingFor("EqSurface.StateText"));
        Add(eqStatus);

        Add(new TextBlock
        {
            Text = "正式預覽會在 Windows 11 24H2+、VS 2026 與鎖定 SDK/WDK 上重跑完整 XAML、無障礙與音訊驗收。",
            TextWrapping = TextWrapping.Wrap,
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
            var id = new TextBlock { Text = scene.Id, TextWrapping = TextWrapping.Wrap };
            details.Children.Add(name);
            details.Children.Add(id);
            Grid.SetColumn(details, 0);
            var removeButton = new Button { Content = "移除", Tag = scene.Id };
            PolishCompatibilityControl(removeButton);
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
            };
            AutomationProperties.SetName(summary, $"App 路由預設 {rule.RuleId}");
            Grid.SetColumn(summary, 0);
            var removeButton = new Button { Content = "移除", Tag = rule.RuleId };
            PolishCompatibilityControl(removeButton);
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
