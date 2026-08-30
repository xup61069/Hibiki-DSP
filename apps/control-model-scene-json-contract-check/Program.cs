using Hibiki.ControlModel;

static void Check(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

var root = Path.Combine(Path.GetTempPath(), $"hibiki-scene-json-contract-{Guid.NewGuid():N}");
Directory.CreateDirectory(root);

try
{
    var validCatalogPath = Path.Combine(root, "valid-catalog.json");
    File.WriteAllText(validCatalogPath, """
        {
          "schema_version": 1,
          "scenes": [
            {
              "id": "quiet-room",
              "name": "Quiet room",
              "description": "Known fields remain loadable",
              "latency_label": "low",
              "safety_enabled": true
            }
          ]
        }
        """);
    var catalog = new CustomSceneCatalogV1();
    Check(catalog.TryLoad(validCatalogPath, out var catalogError) &&
          catalogError.Length == 0 && catalog.Count == 1 &&
          catalog.Scenes[0].Id == "quiet-room" &&
          catalog.Scenes[0].IrReference.Length == 0 &&
          !catalog.Scenes[0].LoudnessLiveUpdate,
        "A valid scene-card document with omitted optional fields must load.");

    var unknownCatalogTopLevelPath = Path.Combine(root, "unknown-catalog-top-level.json");
    File.WriteAllText(unknownCatalogTopLevelPath, """
        {
          "schema_version": 1,
          "scenes": [],
          "unexpected": true
        }
        """);
    var catalogBeforeTopLevelRejection = catalog.Scenes[0].Id;
    Check(!catalog.TryLoad(unknownCatalogTopLevelPath, out _) &&
          catalog.Count == 1 && catalog.Scenes[0].Id == catalogBeforeTopLevelRejection,
        "An unknown scene-card top-level property must reject without replacing state.");

    var unknownCatalogNestedPath = Path.Combine(root, "unknown-catalog-nested.json");
    File.WriteAllText(unknownCatalogNestedPath, """
        {
          "schema_version": 1,
          "scenes": [
            {
              "id": "quiet-room",
              "name": "Quiet room",
              "description": "Known fields remain loadable",
              "latency_label": "low",
              "safety_enabled": true,
              "unexpected": "must reject"
            }
          ]
        }
        """);
    Check(!catalog.TryLoad(unknownCatalogNestedPath, out _) &&
          catalog.Count == 1 && catalog.Scenes[0].Id == catalogBeforeTopLevelRejection,
        "An unknown scene-card nested property must reject without replacing state.");

    var validQueuePath = Path.Combine(root, "valid-queue.json");
    File.WriteAllText(validQueuePath, """
        {
          "schema_version": 1,
          "dropped_operations": 2,
          "operations": [
            {
              "is_upsert": false,
              "scene_id": "quiet-room",
              "name": "",
              "output_group": ""
            }
          ]
        }
        """);
    var queue = new CustomSceneSyncQueueV1();
    Check(queue.TryLoad(validQueuePath, out var dropped, out var queueError) &&
          queueError.Length == 0 && dropped == 2 && queue.Operations.Count == 1 &&
          queue.Operations[0].SceneId == "quiet-room" &&
          queue.Operations[0].IrReference.Length == 0 &&
          !queue.Operations[0].LoudnessLiveUpdate,
        "A valid scene-sync queue with omitted optional fields must load.");

    var unknownQueueTopLevelPath = Path.Combine(root, "unknown-queue-top-level.json");
    File.WriteAllText(unknownQueueTopLevelPath, """
        {
          "schema_version": 1,
          "dropped_operations": 0,
          "operations": [],
          "unexpected": true
        }
        """);
    var queueBeforeTopLevelRejection = queue.Operations[0].SceneId;
    Check(!queue.TryLoad(unknownQueueTopLevelPath, out _, out _) &&
          queue.Operations.Count == 1 &&
          queue.Operations[0].SceneId == queueBeforeTopLevelRejection,
        "An unknown scene-sync queue top-level property must reject without replacing state.");

    var unknownQueueNestedPath = Path.Combine(root, "unknown-queue-nested.json");
    File.WriteAllText(unknownQueueNestedPath, """
        {
          "schema_version": 1,
          "dropped_operations": 0,
          "operations": [
            {
              "is_upsert": false,
              "scene_id": "quiet-room",
              "name": "",
              "output_group": "",
              "unexpected": "must reject"
            }
          ]
        }
        """);
    Check(!queue.TryLoad(unknownQueueNestedPath, out _, out _) &&
          queue.Operations.Count == 1 &&
          queue.Operations[0].SceneId == queueBeforeTopLevelRejection,
        "An unknown scene-sync queue nested property must reject without replacing state.");

    Console.WriteLine("Control-model scene JSON strict-property checks passed.");
}
finally
{
    try { if (Directory.Exists(root)) Directory.Delete(root, recursive: true); }
    catch (IOException) { }
    catch (UnauthorizedAccessException) { }
}
