// SPDX-License-Identifier: GPL-3.0-only

using System;
using Hibiki.WinUI;

namespace Hibiki.WindowPlacement.Check;

internal static class Program
{
    private static int _failedTests = 0;

    public static int Main()
    {
        Console.WriteLine("Running WindowPlacement.Normalize boundary check suite...");

        try
        {
            TestNormalPlacement();
            TestOversizedPlacement();
            TestOffScreenPlacement();
            TestNegativeMonitorPlacement();
            TestInvalidWorkArea();

            if (_failedTests > 0)
            {
                Console.WriteLine($"Boundary checks completed with {_failedTests} failure(s).");
                return 1;
            }

            Console.WriteLine("All 5 boundary check categories passed successfully.");
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Fatal test harness error: {ex.Message}");
            return 2;
        }
    }

    private static void AssertEqual(WindowPlacementV1 expected, WindowPlacementV1 actual, string testName)
    {
        if (expected == actual)
        {
            Console.WriteLine($"[PASS] {testName}");
        }
        else
        {
            Console.Error.WriteLine($"[FAIL] {testName}");
            Console.Error.WriteLine($"  Expected: Width={expected.Width}, Height={expected.Height}, X={expected.X}, Y={expected.Y}");
            Console.Error.WriteLine($"  Actual:   Width={actual.Width}, Height={actual.Height}, X={actual.X}, Y={actual.Y}");
            _failedTests++;
        }
    }

    private static void TestNormalPlacement()
    {
        // Normal: Placement fits inside work area. Should be preserved.
        var placement = new WindowPlacementV1(800, 600, 100, 100);
        var normalized = Hibiki.WinUI.WindowPlacement.Normalize(placement, 0, 0, 1920, 1080);
        AssertEqual(new WindowPlacementV1(800, 600, 100, 100), normalized, "TestNormalPlacement_FullyInside");

        // Normal: Partly outside but intersects. Should be preserved.
        var placementPartlyOut = new WindowPlacementV1(800, 600, 1800, 1000);
        var normalizedPartlyOut = Hibiki.WinUI.WindowPlacement.Normalize(placementPartlyOut, 0, 0, 1920, 1080);
        AssertEqual(new WindowPlacementV1(800, 600, 1800, 1000), normalizedPartlyOut, "TestNormalPlacement_PartlyOutsideIntersects");
    }

    private static void TestOversizedPlacement()
    {
        // Oversized: Size is larger than work area (1920x1080).
        // It should clamp window size to work area size, respecting minimum boundaries (720x520).
        var placement = new WindowPlacementV1(2000, 1200, 0, 0);
        var normalized = Hibiki.WinUI.WindowPlacement.Normalize(placement, 0, 0, 1920, 1080);
        AssertEqual(new WindowPlacementV1(1920, 1080, 0, 0), normalized, "TestOversizedPlacement_ClampedToWorkArea");

        // Oversized: Size is larger than work area, but work area is still larger than min.
        var placementSmallScreen = new WindowPlacementV1(1000, 800, 0, 0);
        var normalizedSmallScreen = Hibiki.WinUI.WindowPlacement.Normalize(placementSmallScreen, 0, 0, 800, 600);
        AssertEqual(new WindowPlacementV1(800, 600, 0, 0), normalizedSmallScreen, "TestOversizedPlacement_SmallScreenClamped");
    }

    private static void TestOffScreenPlacement()
    {
        // Off-screen: Completely outside (no intersection). Should relocate to work area top-left.
        var placement = new WindowPlacementV1(800, 600, 2000, 1200);
        var normalized = Hibiki.WinUI.WindowPlacement.Normalize(placement, 0, 0, 1920, 1080);
        AssertEqual(new WindowPlacementV1(800, 600, 0, 0), normalized, "TestOffScreenPlacement_RelocatedToTopLeft");

        // Off-screen: Negative coordinates, completely outside.
        var placementNegative = new WindowPlacementV1(800, 600, -1000, -800);
        var normalizedNegative = Hibiki.WinUI.WindowPlacement.Normalize(placementNegative, 0, 0, 1920, 1080);
        AssertEqual(new WindowPlacementV1(800, 600, 0, 0), normalizedNegative, "TestOffScreenPlacement_NegativeRelocatedToTopLeft");
    }

    private static void TestNegativeMonitorPlacement()
    {
        // Negative monitor: Work area is on a secondary monitor to the left (e.g., X=-1920, Y=0, Width=1920, Height=1080).
        // Placement is inside this negative monitor.
        var placement = new WindowPlacementV1(800, 600, -1000, 100);
        var normalized = Hibiki.WinUI.WindowPlacement.Normalize(placement, -1920, 0, 1920, 1080);
        AssertEqual(new WindowPlacementV1(800, 600, -1000, 100), normalized, "TestNegativeMonitorPlacement_Inside");

        // Negative monitor: Placement is completely off-screen relative to the negative monitor.
        // Should relocate to the secondary monitor's top-left (-1920, 0).
        var placementOff = new WindowPlacementV1(800, 600, 100, 100); // this is on the primary monitor, but the supplied work area is the negative one. No intersection.
        var normalizedOff = Hibiki.WinUI.WindowPlacement.Normalize(placementOff, -1920, 0, 1000, 1000);
        AssertEqual(new WindowPlacementV1(800, 600, -1920, 0), normalizedOff, "TestNegativeMonitorPlacement_OffScreenRelocatedToTopLeft");
    }

    private static void TestInvalidWorkArea()
    {
        // Invalid work area: Work area size (e.g. 500x400) is smaller than MinWidth (720) and MinHeight (520).
        // Should clamp window size to MinWidth/MinHeight, and place it at workAreaX, workAreaY.
        var placement = new WindowPlacementV1(800, 600, 100, 100);
        var normalized = Hibiki.WinUI.WindowPlacement.Normalize(placement, 10, 10, 500, 400);
        // Note: because the resulting size is 720x520, a placement at (100, 100) on a 500x400 screen
        // will still intersect! (X=100 < 10 + 500 = 510, and X+W = 820 > 10).
        // So the position is preserved. Let's verify that.
        AssertEqual(new WindowPlacementV1(720, 520, 100, 100), normalized, "TestInvalidWorkArea_IntersectPreserved");

        // Invalid work area: No intersection.
        var placementNoIntersect = new WindowPlacementV1(800, 600, 1000, 1000);
        var normalizedNoIntersect = Hibiki.WinUI.WindowPlacement.Normalize(placementNoIntersect, 10, 10, 500, 400);
        AssertEqual(new WindowPlacementV1(720, 520, 10, 10), normalizedNoIntersect, "TestInvalidWorkArea_NoIntersectRelocated");
    }
}
