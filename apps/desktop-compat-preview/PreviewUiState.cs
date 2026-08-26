// SPDX-License-Identifier: GPL-3.0-only

using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Hibiki.DesktopPreview;

internal sealed record PreviewUiStateV1
{
    [JsonPropertyName("schema_version")]
    public int SchemaVersion { get; init; } = 1;

    [JsonPropertyName("selected_physical_device_endpoint_id")]
    public string? SelectedPhysicalDeviceEndpointId { get; init; }

    [JsonPropertyName("selected_scene_id")]
    public string? SelectedSceneId { get; init; }

    [JsonPropertyName("selected_output_group_id")]
    public string? SelectedOutputGroupId { get; init; }

    [JsonPropertyName("window_x")]
    public int? WindowX { get; init; }

    [JsonPropertyName("window_y")]
    public int? WindowY { get; init; }

    [JsonPropertyName("window_width")]
    public int? WindowWidth { get; init; }

    [JsonPropertyName("window_height")]
    public int? WindowHeight { get; init; }

    [JsonPropertyName("updated_at")]
    public string? UpdatedAtUtc { get; init; }
}

internal static class PreviewUiState
{
    private const int SchemaVersion = 1;
    private const int MaxIdLength = 256;

    private static readonly string StatePath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "Hibiki DSP", "desktop-preview-ui-state-v1.json");

    public static PreviewUiStateV1 Load()
    {
        try
        {
            if (!File.Exists(StatePath)) return new PreviewUiStateV1();
            var json = File.ReadAllText(StatePath);
            if (json.Length > 8_192) return new PreviewUiStateV1();
            var state = JsonSerializer.Deserialize<PreviewUiStateV1>(json);
            return state is not null && state.SchemaVersion == SchemaVersion &&
                   IsValidId(state.SelectedPhysicalDeviceEndpointId) &&
                   IsValidId(state.SelectedSceneId) &&
                   IsValidId(state.SelectedOutputGroupId)
                ? state
                : new PreviewUiStateV1();
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
        {
            // Fail closed: a damaged state file must never block startup.
            return new PreviewUiStateV1();
        }
    }

    public static void Save(string? selectedPhysicalDeviceEndpointId, string? selectedSceneId,
        string? selectedOutputGroupId, Rectangle? windowBounds)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(StatePath)!);
            var state = new PreviewUiStateV1
            {
                SelectedPhysicalDeviceEndpointId =
                    NormalizeId(selectedPhysicalDeviceEndpointId),
                SelectedSceneId = NormalizeId(selectedSceneId),
                SelectedOutputGroupId = NormalizeId(selectedOutputGroupId),
                WindowX = windowBounds?.X,
                WindowY = windowBounds?.Y,
                WindowWidth = windowBounds?.Width,
                WindowHeight = windowBounds?.Height,
                UpdatedAtUtc = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture)
            };
            var json = JsonSerializer.Serialize(state,
                new JsonSerializerOptions { DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull });
            File.WriteAllText(StatePath, json);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            // Persistence is best-effort; losing it never breaks the preview.
        }
    }

    private static bool IsValidId(string? value) =>
        value is null || (value.Length <= MaxIdLength && !value.Any(char.IsControl));

    private static string? NormalizeId(string? value) =>
        string.IsNullOrWhiteSpace(value) ? null : value.Trim();
}
