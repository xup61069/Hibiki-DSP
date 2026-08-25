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
#if !HIBIKI_COMPATIBILITY_PREVIEW
        InitializeComponent();
#endif
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        MainWindow ??= new MainWindow();
#if !HIBIKI_COMPATIBILITY_PREVIEW
        RestoreWindowPlacement(MainWindow);
        MainWindow.Closed += (_, _) => PersistWindowPlacement(MainWindow);
#endif
        MainWindow.Activate();
    }
#if !HIBIKI_COMPATIBILITY_PREVIEW
    private static void RestoreWindowPlacement(Window window)
    {
        if (!WindowPlacement.TryLoad(WindowPlacement.DefaultPath, out var placement)) return;
        try
        {
            window.AppWindow.Resize(new Windows.Graphics.SizeInt32(placement.Width, placement.Height));
            window.AppWindow.Move(new Windows.Graphics.PointInt32(placement.X, placement.Y));
        }
        catch (InvalidOperationException)
        {
            // AppWindow not yet available or presenter rejected the request; keep default layout.
        }
    }

    private static void PersistWindowPlacement(Window window)
    {
        try
        {
            var appWindow = window.AppWindow;
            WindowPlacement.TrySave(WindowPlacement.DefaultPath,
                new(appWindow.Size.Width, appWindow.Size.Height, appWindow.Position.X, appWindow.Position.Y));
        }
        catch (InvalidOperationException)
        {
            // Window already torn down; nothing to persist.
        }
    }
#endif
}
