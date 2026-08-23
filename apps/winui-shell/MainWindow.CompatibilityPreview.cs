// SPDX-License-Identifier: GPL-3.0-only

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Data;
using Microsoft.UI.Xaml.Media;

namespace Hibiki.WinUI;

public sealed partial class MainWindow
{
#if HIBIKI_COMPATIBILITY_PREVIEW
    private static T ResolveThemeResource<T>(string key) where T : class
    {
        return Application.Current.Resources.TryGetValue(key, out var value) ? (T)value : null;
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

        content.Children.Add(new TextBlock
        {
            Text = "VST3 時間軸編輯器（本機草稿）",
            Style = ResolveThemeResource<Style>("SubtitleTextBlockStyle"),
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
        });
        var vst3TimelineSelector = new ComboBox { Header = "時間軸", MinWidth = 200 };
        AutomationProperties.SetName(vst3TimelineSelector, "選取 VST3 時間軸");
        vst3TimelineSelector.SetBinding(ItemsControl.ItemsSourceProperty, BindingFor("Vst3TimelineEditor.TimelineIds"));
        content.Children.Add(vst3TimelineSelector);
        var beginEditButton = new Button { Content = "開始草稿" };
        AutomationProperties.SetName(beginEditButton, "開始時間軸草稿");
        beginEditButton.Click += OnVst3BeginEditClick;
        content.Children.Add(beginEditButton);
        var commitButton = new Button { Content = "提交草稿" };
        AutomationProperties.SetName(commitButton, "提交時間軸草稿");
        commitButton.Click += OnVst3CommitClick;
        content.Children.Add(commitButton);
        var discardButton = new Button { Content = "捨棄草稿" };
        AutomationProperties.SetName(discardButton, "捨棄時間軸草稿");
        discardButton.Click += OnVst3DiscardClick;
        content.Children.Add(discardButton);
        var undoButton = new Button { Content = "復原" };
        AutomationProperties.SetName(undoButton, "復原時間軸操作");
        undoButton.Click += OnVst3UndoClick;
        content.Children.Add(undoButton);
        var redoButton = new Button { Content = "重做" };
        AutomationProperties.SetName(redoButton, "重做時間軸操作");
        redoButton.Click += OnVst3RedoClick;
        content.Children.Add(redoButton);
        content.Children.Add(BoundText("Vst3TimelineEditor.StatusText"));

        content.Children.Add(new TextBlock
        {
            Text = "正式預覽會在 Windows 11 24H2+、VS 2026 與鎖定 SDK/WDK 上重跑完整 XAML、無障礙與音訊驗收。",
            TextWrapping = TextWrapping.Wrap,
            Foreground = ResolveThemeResource<Brush>("TextFillColorSecondaryBrush"),
        });
        return root;
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
