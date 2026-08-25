// SPDX-License-Identifier: GPL-3.0-only

using System.Globalization;
using System.IO;
using System.Text.Json;

namespace Hibiki.WinUI;

/// <summary>Bounded window placement persisted across sessions.</summary>
public sealed record WindowPlacementV1(int Width, int Height, int X, int Y);

public static class WindowPlacement
{
    private const int MinWidth = 720;
    private const int MinHeight = 520;
    private const int MaxPosition = 16000;
    private const string FileName = "window-placement-v1.json";

    private static readonly JsonSerializerOptions SerializerOptions = new() { WriteIndented = true };

    public static string DefaultPath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "Hibiki DSP", FileName);

    /// <summary>Loads placement; returns false when missing or invalid (caller uses defaults).</summary>
    public static bool TryLoad(string path, out WindowPlacementV1 placement)
    {
        placement = new(MinWidth, MinHeight, 0, 0);
        try
        {
            if (!File.Exists(path)) return false;
            var json = File.ReadAllText(path);
            var loaded = JsonSerializer.Deserialize<WindowPlacementV1>(json);
            if (loaded is null) return false;
            var width = Math.Max(loaded.Width, MinWidth);
            var height = Math.Max(loaded.Height, MinHeight);
            var x = Math.Clamp(loaded.X, -MaxPosition, MaxPosition);
            var y = Math.Clamp(loaded.Y, -MaxPosition, MaxPosition);
            placement = new(width, height, x, y);
            return true;
        }
        catch (IOException) { return false; }
        catch (JsonException) { return false; }
        catch (System.Security.SecurityException) { return false; }
        catch (UnauthorizedAccessException) { return false; }
    }

    /// <summary>Saves placement atomically via temp file rename.</summary>
    public static bool TrySave(string path, WindowPlacementV1 placement)
    {
        try
        {
            var directory = Path.GetDirectoryName(path);
            if (string.IsNullOrEmpty(directory)) return false;
            Directory.CreateDirectory(directory);
            var tempPath = path + ".tmp";
            File.WriteAllText(tempPath, JsonSerializer.Serialize(placement, SerializerOptions));
            File.Move(tempPath, path, overwrite: true);
            return true;
        }
        catch (IOException) { return false; }
        catch (JsonException) { return false; }
        catch (System.Security.SecurityException) { return false; }
        catch (UnauthorizedAccessException) { return false; }
    }
}
