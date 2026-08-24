// SPDX-License-Identifier: GPL-3.0-only

using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Hibiki.ControlModel;

// UI/control-plane mirror for custom Scene cards. The engine remains the
// authoritative owner of the full SceneDefinition (graph/loudness); this
// bounded catalog only keeps the user-facing identity available for selection.
public sealed class CustomSceneCatalogV1
{
    public const int MaxScenes = 32;
    public const int SchemaVersion = 1;
    private const long MaxFileBytes = 1024 * 1024;
    private readonly List<SceneCard> _scenes = new(MaxScenes);
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = null
    };

    public IReadOnlyList<SceneCard> Scenes => _scenes;
    public int Count => _scenes.Count;

    public bool Upsert(SceneCard? scene)
    {
        if (scene is null || !IsValid(scene)) return false;
        var index = _scenes.FindIndex(item => item.Id == scene.Id);
        if (index >= 0)
        {
            _scenes[index] = scene;
            return true;
        }
        if (_scenes.Count >= MaxScenes) return false;
        _scenes.Add(scene);
        return true;
    }

    public bool Remove(string? sceneId)
    {
        if (string.IsNullOrWhiteSpace(sceneId)) return false;
        var index = _scenes.FindIndex(item => item.Id == sceneId.Trim());
        if (index < 0) return false;
        _scenes.RemoveAt(index);
        return true;
    }

    public void Clear() => _scenes.Clear();

    // Persistence is control/UI-plane only. The file contains display cards,
    // not graph, calibration, plugin state or device identities. Save uses a
    // same-directory temporary file followed by replacement so a crash cannot
    // leave a half-written catalog.
    public bool TrySave(string filePath, out string error)
    {
        error = string.Empty;
        if (string.IsNullOrWhiteSpace(filePath))
        {
            error = "Preset 路徑不可為空";
            return false;
        }

        var temporaryPath = string.Empty;
        try
        {
            var fullPath = Path.GetFullPath(filePath);
            var directory = Path.GetDirectoryName(fullPath);
            if (string.IsNullOrWhiteSpace(directory))
            {
                error = "Preset 路徑缺少資料夾";
                return false;
            }
            Directory.CreateDirectory(directory);
            var document = new CatalogDocument
            {
                SchemaVersion = SchemaVersion,
                Scenes = _scenes.Select(scene => (CatalogScene?)ToDocument(scene)).ToList()
            };
            var json = JsonSerializer.Serialize(document, JsonOptions);
            if (Encoding.UTF8.GetByteCount(json) > MaxFileBytes)
            {
                error = "Preset 檔案超過大小上限";
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
            error = "Preset 儲存失敗";
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

    // Parse and validate the complete candidate before mutating the current
    // catalog. A malformed or mixed-version file therefore preserves state.
    public bool TryLoad(string filePath, out string error)
    {
        error = string.Empty;
        if (string.IsNullOrWhiteSpace(filePath))
        {
            error = "Preset 路徑不可為空";
            return false;
        }
        try
        {
            var fullPath = Path.GetFullPath(filePath);
            var info = new FileInfo(fullPath);
            if (!info.Exists || info.Length < 1 || info.Length > MaxFileBytes)
            {
                error = "Preset 檔案不存在或超過大小上限";
                return false;
            }
            var document = JsonSerializer.Deserialize<CatalogDocument>(
                File.ReadAllText(fullPath, Encoding.UTF8));
            if (document is null || document.SchemaVersion != SchemaVersion ||
                document.Scenes is null || document.Scenes.Count > MaxScenes)
            {
                error = "Preset schema 版本或容量無效";
                return false;
            }

            var candidate = new List<SceneCard>(MaxScenes);
            foreach (var item in document.Scenes)
            {
                if (item is null) { error = "Preset 含有空場景"; return false; }
                var scene = new SceneCard(item.Id ?? string.Empty, item.Name ?? string.Empty,
                                          item.Description ?? string.Empty,
                                          item.LatencyLabel ?? string.Empty, item.SafetyEnabled);
                if (!IsValid(scene) || candidate.Any(existing => existing.Id == scene.Id))
                {
                    error = "Preset 含有無效或重複場景";
                    return false;
                }
                candidate.Add(scene);
            }
            _scenes.Clear();
            _scenes.AddRange(candidate);
            return true;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or
                                          JsonException or NotSupportedException or ArgumentException)
        {
            error = "Preset 載入失敗";
            return false;
        }
    }

    private static CatalogScene ToDocument(SceneCard scene) => new()
    {
        Id = scene.Id,
        Name = scene.Name,
        Description = scene.Description,
        LatencyLabel = scene.LatencyLabel,
        SafetyEnabled = scene.SafetyEnabled
    };

    private static bool IsValid(SceneCard scene)
    {
        if (string.IsNullOrWhiteSpace(scene.Id) || scene.Id.Length > 31 ||
            string.IsNullOrWhiteSpace(scene.Name) ||
            Encoding.UTF8.GetByteCount(scene.Name) > 120 ||
            scene.Name.Any(char.IsControl) ||
            scene.Description.Length > 240 || scene.LatencyLabel.Length > 64 ||
            ScenePresetCatalog.EasyDefaults.Any(item => item.Id == scene.Id))
            return false;

        for (var index = 0; index < scene.Id.Length; index++)
        {
            var value = scene.Id[index];
            var lower = value is >= 'a' and <= 'z';
            var digit = value is >= '0' and <= '9';
            var separator = value is '.' or '_' or '-';
            if ((!lower && !digit && !separator) ||
                (index == 0 && !lower && !digit)) return false;
        }
        return true;
    }

    private sealed class CatalogDocument
    {
        [JsonPropertyName("schema_version")]
        public int SchemaVersion { get; set; }

        [JsonPropertyName("scenes")]
        public List<CatalogScene?>? Scenes { get; set; }
    }

    private sealed class CatalogScene
    {
        [JsonPropertyName("id")]
        public string? Id { get; set; }

        [JsonPropertyName("name")]
        public string? Name { get; set; }

        [JsonPropertyName("description")]
        public string? Description { get; set; }

        [JsonPropertyName("latency_label")]
        public string? LatencyLabel { get; set; }

        [JsonPropertyName("safety_enabled")]
        public bool SafetyEnabled { get; set; }
    }
}
