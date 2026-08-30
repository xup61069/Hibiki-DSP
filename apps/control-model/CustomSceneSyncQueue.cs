// SPDX-License-Identifier: GPL-3.0-only

using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Hibiki.ControlModel;

// One bounded, replayable offline scene catalog operation.
public sealed record SceneCatalogQueueCard(
    bool IsUpsert, string SceneId, string Name, string OutputGroup,
    string IrReference = "",
    bool LoudnessLiveUpdate = false);

// Persisted companion to the custom scene card mirror. It keeps only the
// bounded replay operations needed to resume engine synchronization after a
// restart; malformed or oversized files fail closed and never touch cards.
public sealed class CustomSceneSyncQueueV1
{
    public const int SchemaVersion = 1;
    private const long MaxFileBytes = 1024 * 1024;
    private readonly List<SceneCatalogQueueCard> _operations = new(
        EasyControlViewModel.MaxPendingSceneCatalogOps);
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = null,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow
    };

    public IReadOnlyList<SceneCatalogQueueCard> Operations => _operations;

    public bool Enqueue(SceneCatalogQueueCard? operation)
    {
        if (operation is null || !IsValid(operation)) return false;
        while (_operations.Count >= EasyControlViewModel.MaxPendingSceneCatalogOps)
        {
            _operations.RemoveAt(0);
        }
        _operations.Add(operation);
        return true;
    }

    public void Clear() => _operations.Clear();

    // Save uses a same-directory temporary file followed by replacement,
    // matching the existing card mirror crash-safety contract.
    public bool TrySave(string filePath, int droppedCount, out string error)
    {
        error = string.Empty;
        if (string.IsNullOrWhiteSpace(filePath))
        {
            error = "場景同步佇列路徑不可為空";
            return false;
        }
        if (droppedCount < 0 || _operations.Count > EasyControlViewModel.MaxPendingSceneCatalogOps)
        {
            error = "場景同步佇列容量無效";
            return false;
        }

        var temporaryPath = string.Empty;
        try
        {
            var fullPath = Path.GetFullPath(filePath);
            var directory = Path.GetDirectoryName(fullPath);
            if (string.IsNullOrWhiteSpace(directory))
            {
                error = "場景同步佇列路徑缺少資料夾";
                return false;
            }
            Directory.CreateDirectory(directory);
            var document = new QueueDocument
            {
                SchemaVersion = SchemaVersion,
                DroppedOperations = droppedCount,
                Operations = _operations.Select(operation => (QueueOperation?)ToDocument(operation)).ToList()
            };
            var json = JsonSerializer.Serialize(document, JsonOptions);
            if (Encoding.UTF8.GetByteCount(json) > MaxFileBytes)
            {
                error = "場景同步佇列檔案超過大小上限";
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
            error = "場景同步佇列儲存失敗";
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

    // Parse and validate the complete candidate before mutating the queue.
    // A missing file is an empty queue; malformed or oversized data fails closed.
    public bool TryLoad(string filePath, out int droppedCount, out string error)
    {
        error = string.Empty;
        droppedCount = 0;
        if (string.IsNullOrWhiteSpace(filePath))
        {
            error = "場景同步佇列路徑不可為空";
            return false;
        }

        try
        {
            var fullPath = Path.GetFullPath(filePath);
            using var stream = new FileStream(
                fullPath, FileMode.Open, FileAccess.Read, FileShare.Read);
            if (stream.Length < 1 || stream.Length > MaxFileBytes)
            {
                error = "場景同步佇列檔案不存在或超過大小上限";
                return false;
            }

            using var reader = new StreamReader(stream, Encoding.UTF8, detectEncodingFromByteOrderMarks: true);
            var document = JsonSerializer.Deserialize<QueueDocument>(
                reader.ReadToEnd(), JsonOptions);
            if (document is null || document.SchemaVersion != SchemaVersion ||
                document.DroppedOperations < 0 ||
                document.Operations is null ||
                document.Operations.Count > EasyControlViewModel.MaxPendingSceneCatalogOps)
            {
                error = "場景同步佇列 schema 版本或容量無效";
                return false;
            }

            var candidate = new List<SceneCatalogQueueCard>(
                EasyControlViewModel.MaxPendingSceneCatalogOps);
            foreach (var item in document.Operations)
            {
                if (item is null)
                {
                    error = "場景同步佇列含有空操作";
                    return false;
                }
                var operation = new SceneCatalogQueueCard(
                    item.IsUpsert, item.SceneId ?? string.Empty, item.Name ?? string.Empty,
                    item.OutputGroup ?? string.Empty,
                    item.IrReference ?? string.Empty,
                    item.LoudnessLiveUpdate);
                if (!IsValid(operation))
                {
                    error = "場景同步佇列含有無效操作";
                    return false;
                }
                candidate.Add(operation);
            }

            Clear();
            _operations.AddRange(candidate);
            droppedCount = document.DroppedOperations;
            return true;
        }
        catch (FileNotFoundException)
        {
            Clear();
            return true;
        }
        catch (DirectoryNotFoundException)
        {
            Clear();
            return true;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or
                                          JsonException or NotSupportedException or ArgumentException)
        {
            error = "場景同步佇列載入失敗";
            return false;
        }
    }

    private static QueueOperation ToDocument(SceneCatalogQueueCard operation) => new()
    {
        IsUpsert = operation.IsUpsert,
        SceneId = operation.SceneId,
        Name = operation.Name,
        OutputGroup = operation.OutputGroup,
        IrReference = operation.IrReference,
        LoudnessLiveUpdate = operation.LoudnessLiveUpdate
    };

    private static bool IsValid(SceneCatalogQueueCard operation)
    {
        if (string.IsNullOrWhiteSpace(operation.SceneId) || operation.SceneId.Length > 31)
            return false;
        for (var index = 0; index < operation.SceneId.Length; index++)
        {
            var value = operation.SceneId[index];
            var lower = value is >= 'a' and <= 'z';
            var digit = value is >= '0' and <= '9';
            var separator = value is '.' or '_' or '-';
            if ((!lower && !digit && !separator) ||
                (index == 0 && !lower && !digit)) return false;
        }

        if (operation.IsUpsert)
        {
            return !string.IsNullOrWhiteSpace(operation.Name) &&
                   Encoding.UTF8.GetByteCount(operation.Name) <= 120 &&
                   !operation.Name.Any(char.IsControl) &&
                   !string.IsNullOrWhiteSpace(operation.OutputGroup) &&
                   Encoding.UTF8.GetByteCount(operation.OutputGroup) <= 64 &&
                   !operation.OutputGroup.Any(char.IsControl) &&
                   Encoding.UTF8.GetByteCount(operation.IrReference) <= 64 &&
                   (operation.IrReference.Length == 0 ||
                    Encoding.UTF8.GetByteCount(operation.IrReference) >= 8) &&
                   !operation.IrReference.Any(char.IsControl);
        }

        return string.IsNullOrEmpty(operation.Name) &&
               string.IsNullOrEmpty(operation.OutputGroup) &&
               string.IsNullOrEmpty(operation.IrReference);
    }

    private sealed class QueueDocument
    {
        [JsonPropertyName("schema_version")]
        public int SchemaVersion { get; set; }

        [JsonPropertyName("dropped_operations")]
        public int DroppedOperations { get; set; }

        [JsonPropertyName("operations")]
        public List<QueueOperation?>? Operations { get; set; }
    }

    private sealed class QueueOperation
    {
        [JsonPropertyName("is_upsert")]
        public bool IsUpsert { get; set; }

        [JsonPropertyName("scene_id")]
        public string? SceneId { get; set; }

        [JsonPropertyName("name")]
        public string? Name { get; set; }

        [JsonPropertyName("output_group")]
        public string? OutputGroup { get; set; }

        [JsonPropertyName("ir_reference")]
        public string? IrReference { get; set; }

        [JsonPropertyName("loudness_live_update")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
        public bool LoudnessLiveUpdate { get; set; }
    }
}
