// SPDX-License-Identifier: GPL-3.0-only

using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Hibiki.ControlModel;

// UI-plane persistence for the formal WinUI shell window bounds. The file is
// local convenience state only: it contains no device identity, calibration,
// audio graph or private path data. Save uses a same-directory temporary file
// followed by replacement so a crash cannot leave a half-written placement.
public sealed class WindowPlacementStoreV1
{
    public const int SchemaVersion = 1;
    public const double MinWidth = 720.0;
    public const double MinHeight = 520.0;
    public const double MaxWidth = 10000.0;
    public const double MaxHeight = 10000.0;
    public const int CoordinateLimit = 100000;
    private const long MaxFileBytes = 64 * 1024;
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = null
    };

    public double? Width { get; private set; }
    public double? Height { get; private set; }
    public int? X { get; private set; }
    public int? Y { get; private set; }
    public bool IsMaximized { get; private set; }
    public bool HasPlacement => Width is not null && Height is not null;

    // Parse and validate the complete candidate before mutating the store. A
    // malformed or mixed-version file therefore preserves current state.
    public bool TryLoad(string filePath, out string error)
    {
        error = string.Empty;
        if (string.IsNullOrWhiteSpace(filePath))
        {
            error = "視窗狀態路徑不可為空";
            return false;
        }
        try
        {
            var fullPath = Path.GetFullPath(filePath);
            var info = new FileInfo(fullPath);
            if (!info.Exists || info.Length < 1 || info.Length > MaxFileBytes)
            {
                error = "視窗狀態檔不存在或超過大小上限";
                return false;
            }
            var document = JsonSerializer.Deserialize<PlacementDocument>(
                File.ReadAllText(fullPath, Encoding.UTF8));
            if (document is null || document.SchemaVersion != SchemaVersion)
            {
                error = "視窗狀態 schema 版本無效";
                return false;
            }
            if (document.Placement is null)
            {
                error = "視窗狀態缺少 placement";
                return false;
            }

            var placement = document.Placement;
            if (placement.Width is not null && !IsValidDimension(placement.Width.Value, MinWidth, MaxWidth))
            {
                error = "視窗狀態寬度超出範圍";
                return false;
            }
            if (placement.Height is not null && !IsValidDimension(placement.Height.Value, MinHeight, MaxHeight))
            {
                error = "視窗狀態高度超出範圍";
                return false;
            }
            if ((placement.X is not null) != (placement.Y is not null))
            {
                error = "視窗狀態位置不完整";
                return false;
            }
            if (placement.X is not null && Math.Abs(placement.X.Value) > CoordinateLimit)
            {
                error = "視窗狀態 X 超出範圍";
                return false;
            }
            if (placement.Y is not null && Math.Abs(placement.Y.Value) > CoordinateLimit)
            {
                error = "視窗狀態 Y 超出範圍";
                return false;
            }

            Width = placement.Width;
            Height = placement.Height;
            X = placement.X;
            Y = placement.Y;
            IsMaximized = placement.IsMaximized;
            return true;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or
                                          JsonException or NotSupportedException or ArgumentException)
        {
            error = "視窗狀態載入失敗";
            return false;
        }
    }

    public bool TrySave(string filePath, out string error)
    {
        error = string.Empty;
        if (string.IsNullOrWhiteSpace(filePath))
        {
            error = "視窗狀態路徑不可為空";
            return false;
        }
        if (Width is not null && !IsValidDimension(Width.Value, MinWidth, MaxWidth))
        {
            error = "視窗狀態寬度超出範圍";
            return false;
        }
        if (Height is not null && !IsValidDimension(Height.Value, MinHeight, MaxHeight))
        {
            error = "視窗狀態高度超出範圍";
            return false;
        }
        if ((X is not null) != (Y is not null))
        {
            error = "視窗狀態位置不完整";
            return false;
        }

        var temporaryPath = string.Empty;
        try
        {
            var fullPath = Path.GetFullPath(filePath);
            var directory = Path.GetDirectoryName(fullPath);
            if (string.IsNullOrWhiteSpace(directory))
            {
                error = "視窗狀態路徑缺少資料夾";
                return false;
            }
            Directory.CreateDirectory(directory);
            var document = new PlacementDocument
            {
                SchemaVersion = SchemaVersion,
                Placement = new PlacementRecord
                {
                    Width = Width,
                    Height = Height,
                    X = X,
                    Y = Y,
                    IsMaximized = IsMaximized
                }
            };
            var json = JsonSerializer.Serialize(document, JsonOptions);
            if (Encoding.UTF8.GetByteCount(json) > MaxFileBytes)
            {
                error = "視窗狀態檔超過大小上限";
                return false;
            }
            temporaryPath = Path.Combine(directory, $".{Path.GetFileName(fullPath)}.{Guid.NewGuid():N}.tmp");
            File.WriteAllText(temporaryPath, json, new UTF8Encoding(false));
            File.Move(temporaryPath, fullPath, true);
            return true;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or
                                          ArgumentException or NotSupportedException)
        {
            error = "視窗狀態儲存失敗";
            return false;
        }
        finally
        {
            if (!string.IsNullOrEmpty(temporaryPath))
            {
                try { if (File.Exists(temporaryPath)) File.Delete(temporaryPath); }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
            }
        }
    }

    public void SetPlacement(double? width, double? height, int? x, int? y, bool isMaximized)
    {
        if (width is not null && !IsValidDimension(width.Value, MinWidth, MaxWidth))
            throw new ArgumentOutOfRangeException(nameof(width));
        if (height is not null && !IsValidDimension(height.Value, MinHeight, MaxHeight))
            throw new ArgumentOutOfRangeException(nameof(height));
        if ((x is not null) != (y is not null))
            throw new ArgumentException("X and Y must be set together.");
        if (x is not null && Math.Abs(x.Value) > CoordinateLimit)
            throw new ArgumentOutOfRangeException(nameof(x));
        if (y is not null && Math.Abs(y.Value) > CoordinateLimit)
            throw new ArgumentOutOfRangeException(nameof(y));
        Width = width;
        Height = height;
        X = x;
        Y = y;
        IsMaximized = isMaximized;
    }

    private static bool IsValidDimension(double value, double min, double max)
        => double.IsFinite(value) && value >= min && value <= max;

    private sealed class PlacementDocument
    {
        [JsonPropertyName("schema_version")]
        public int SchemaVersion { get; set; }

        [JsonPropertyName("placement")]
        public PlacementRecord? Placement { get; set; }
    }

    private sealed class PlacementRecord
    {
        [JsonPropertyName("width")]
        public double? Width { get; set; }

        [JsonPropertyName("height")]
        public double? Height { get; set; }

        [JsonPropertyName("x")]
        public int? X { get; set; }

        [JsonPropertyName("y")]
        public int? Y { get; set; }

        [JsonPropertyName("is_maximized")]
        public bool IsMaximized { get; set; }
    }
}
