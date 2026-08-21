// SPDX-License-Identifier: GPL-3.0-only

#if HIBIKI_COMPATIBILITY_PREVIEW
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

namespace Hibiki.WinUI;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        WinRT.ComWrappersSupport.InitializeComWrappers();
        Application.Start(_ =>
        {
            SynchronizationContext.SetSynchronizationContext(
                new DispatcherQueueSynchronizationContext(DispatcherQueue.GetForCurrentThread()));
            var app = new App();
        });
    }
}
#endif
