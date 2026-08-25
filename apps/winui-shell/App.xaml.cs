// SPDX-License-Identifier: GPL-3.0-only

using System.IO;
using Hibiki.ControlModel;
using Microsoft.UI.Xaml;
#if HIBIKI_COMPATIBILITY_PREVIEW
using Microsoft.UI.Xaml.Controls;
#endif

namespace Hibiki.WinUI;

public partial class App : Application
{
    public static Window? MainWindow { get; private set; }
    private static WindowPlacementStoreV1? _placementStore;

    public App()
    {
#if !HIBIKI_COMPATIBILITY_PREVIEW
        InitializeComponent();
#endif
    }

#if !HIBIKI_COMPATIBILITY_PREVIEW
    private static string PlacementFilePath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "Hibiki DSP", "window-placement-v1.json");

    private static void RestoreWindowPlacement(Window window)
    {
        try
        {
            var store = new WindowPlacementStoreV1();
            if (!store.TryLoad(PlacementFilePath, out _) || !store.HasPlacement)
            {
                return;
            }

            var appWindow = window.AppWindow;
            if (store.X is int x && store.Y is int y)
            {
                appWindow.Move(new Windows.Graphics.PointInt32(x, y));
            }
            var width = (int)Math.Round(store.Width!.Value);
            var height = (int)Math.Round(store.Height!.Value);
            appWindow.Resize(new Windows.Graphics.SizeInt32(width, height));

            if (store.IsMaximized && appWindow.Presenter is Microsoft.UI.Windowing.OverlappedPresenter presenter)
            {
                presenter.Maximize();
            }
        }
        catch (Exception exception) when (exception is IOException or
                                          UnauthorizedAccessException or System.Runtime.InteropServices.COMException)
        {
            // Placement restoration is best-effort; the shell keeps the default bounds.
        }
    }

    private static void PersistWindowPlacement(Window window)
    {
        try
        {
            var appWindow = window.AppWindow;
            var isMaximized = false;
            int? x = null;
            int? y = null;
            double? width = null;
            double? height = null;

            if (appWindow.Presenter is Microsoft.UI.Windowing.OverlappedPresenter presenter)
            {
                isMaximized = presenter.State == Microsoft.UI.Windowing.OverlappedPresenterState.Maximized;
            }
            if (!isMaximized)
            {
                var position = appWindow.Position;
                var size = appWindow.Size;
                x = position.X;
                y = position.Y;
                width = size.Width;
                height = size.Height;
            }

            var store = _placementStore ??= new WindowPlacementStoreV1();
            store.SetPlacement(width, height, x, y, isMaximized);
            store.TrySave(PlacementFilePath, out _);
        }
        catch (Exception exception) when (exception is IOException or
                                          UnauthorizedAccessException or System.Runtime.InteropServices.COMException)
        {
            // Persisting placement must never block or crash shell shutdown.
        }
    }
#endif

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        MainWindow ??= new MainWindow();
#if !HIBIKI_COMPATIBILITY_PREVIEW
        RestoreWindowPlacement(MainWindow);
        MainWindow.Closed += OnMainWindowClosed;
#endif
        MainWindow.Activate();
    }

#if !HIBIKI_COMPATIBILITY_PREVIEW
    private static void OnMainWindowClosed(object sender, WindowEventArgs args)
    {
        if (sender is Window window)
        {
            PersistWindowPlacement(window);
        }
    }
#endif
}
