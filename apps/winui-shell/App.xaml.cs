// SPDX-License-Identifier: GPL-3.0-only

using Microsoft.UI.Xaml;

namespace Hibiki.WinUI;

public partial class App : Application
{
    public static Window? MainWindow { get; private set; }

    public App()
    {
        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        MainWindow ??= new MainWindow();
        MainWindow.Activate();
    }
}
