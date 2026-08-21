// SPDX-License-Identifier: GPL-3.0-only

using Microsoft.UI.Xaml;
#if HIBIKI_COMPATIBILITY_PREVIEW
using Microsoft.UI.Xaml.Controls;
#endif

namespace Hibiki.WinUI;

public partial class App : Application
{
    public static Window? MainWindow { get; private set; }

    public App()
    {
#if HIBIKI_COMPATIBILITY_PREVIEW
        Resources.MergedDictionaries.Add(new XamlControlsResources());
#else
        InitializeComponent();
#endif
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        MainWindow ??= new MainWindow();
        MainWindow.Activate();
    }
}
