using Hibiki.ControlModel;

static void Check(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

static void ExpectArgumentException(Action action, string message)
{
    try
    {
        action();
    }
    catch (ArgumentException)
    {
        return;
    }
    throw new InvalidOperationException(message);
}

static CalibrationResponseV1 ValidResponse(string? deviceId) =>
    new(1, 48000, 2, deviceId, [new CalibrationPointV1(100, -2, 0)]);

const string highSurrogate = "\uD800";
const string lowSurrogate = "\uDC00";
const string supplementary = "\U0001F3A7";
var root = Path.Combine(Path.GetTempPath(), $"hibiki-surrogate-contract-{Guid.NewGuid():N}");
Directory.CreateDirectory(root);

try
{
    Check(!ValidResponse(highSurrogate).IsValid,
        "Calibration response must reject an isolated high surrogate.");
    Check(!ValidResponse(lowSurrogate).IsValid,
        "Calibration response must reject an isolated low surrogate.");
    Check(ValidResponse(supplementary).IsValid,
        "Calibration response must accept a valid supplementary-plane scalar.");
    ExpectArgumentException(
        () => ControlPayloadsV1.EncodeSceneCatalogCommand(
            SessionRouteRuleOperationV1.Upsert, "concert", highSurrogate, "main"),
        "Scene command construction must reject an isolated surrogate.");
    ExpectArgumentException(
        () => ControlPayloadsV1.EncodeSessionRouteRuleCommand(new SessionRouteRuleCommandV1(
            1, 10, 0, SessionRouteRuleOperationV1.Upsert, true,
            SessionRouteRuleGainOwnerV1.HibikiInternal, 1, "rule-1", highSurrogate,
            "Display", "main", "main")),
        "Route command construction must reject an isolated surrogate.");

    var devices = new PhysicalDeviceCatalogV1();
    var validDevice = new PhysicalDeviceCard(
        "endpoint-1", supplementary, PhysicalDeviceFlowV1.Render,
        PhysicalDeviceAvailabilityV1.Active, 2, 48000, 256, true, 1);
    Check(devices.ReplaceSnapshot([validDevice], 1, out var deviceError) &&
          deviceError.Length == 0 && devices.Devices.Count == 1,
        "Physical-device catalog must accept a valid supplementary-plane display name.");
    var invalidDevice = validDevice with { EndpointId = "endpoint-" + highSurrogate };
    Check(!devices.ReplaceSnapshot([invalidDevice], 2, out _) && devices.Devices.Count == 1,
        "Physical-device catalog must reject an isolated surrogate without replacing state.");

    var catalog = new CustomSceneCatalogV1();
    var validScene = new SceneCard(
        "concert", supplementary, "Description", "low", true);
    Check(catalog.Upsert(validScene) && catalog.Count == 1,
        "Scene catalog must accept a valid supplementary-plane name.");
    Check(!catalog.Upsert(validScene with { Id = "invalid-high", Name = highSurrogate }) &&
          catalog.Count == 1,
        "Scene catalog must reject an isolated surrogate name.");
    var invalidCatalogPath = Path.Combine(root, "invalid-catalog.json");
    File.WriteAllText(invalidCatalogPath, """
        {
          "schema_version": 1,
          "scenes": [
            {
              "id": "invalid-high",
              "name": "\uD800",
              "description": "Description",
              "latency_label": "low",
              "safety_enabled": true
            }
          ]
        }
        """);
    Check(!catalog.TryLoad(invalidCatalogPath, out _) && catalog.Count == 1,
        "Scene catalog load must reject an isolated surrogate without replacing state.");

    var queue = new CustomSceneSyncQueueV1();
    var validOperation = new SceneCatalogQueueCard(
        true, "concert", supplementary, "main");
    Check(queue.Enqueue(validOperation) && queue.Operations.Count == 1,
        "Scene sync queue must accept a valid supplementary-plane name.");
    Check(!queue.Enqueue(validOperation with { SceneId = "invalid-high", Name = highSurrogate }) &&
          queue.Operations.Count == 1,
        "Scene sync queue must reject an isolated surrogate name.");
    var invalidQueuePath = Path.Combine(root, "invalid-queue.json");
    File.WriteAllText(invalidQueuePath, """
        {
          "schema_version": 1,
          "dropped_operations": 0,
          "operations": [
            {
              "is_upsert": true,
              "scene_id": "invalid-high",
              "name": "\uD800",
              "output_group": "main",
              "ir_reference": ""
            }
          ]
        }
        """);
    Check(!queue.TryLoad(invalidQueuePath, out _, out _) && queue.Operations.Count == 1,
        "Scene sync queue load must reject an isolated surrogate without replacing state.");

    var routes = new SessionRouteRuleCatalogV1();
    var validRule = new SessionRouteRuleCard(
        "rule-1", 10, true, SessionRouteRuleGainOwnerV1.HibikiInternal,
        0, supplementary, "Display", "main", "main");
    Check(routes.Upsert(validRule) && routes.Count == 1,
        "Route-rule catalog must accept a valid supplementary-plane app id.");
    Check(!routes.Upsert(validRule with { RuleId = "rule-2", AppId = highSurrogate }) &&
          routes.Count == 1,
        "Route-rule catalog must reject an isolated surrogate app id.");
    var invalidRoutesPath = Path.Combine(root, "invalid-routes.json");
    File.WriteAllText(invalidRoutesPath, """
        {
          "schema_version": 1,
          "rules": [
            {
              "rule_id": "rule-2",
              "priority": 10,
              "enabled": true,
              "gain_owner": "HibikiInternal",
              "makeup_gain_db": 0,
              "app_id": "\uD800",
              "display_name": "Display",
              "lane_id": "main",
              "output_group": "main"
            }
          ]
        }
        """);
    Check(!routes.TryLoad(invalidRoutesPath, out _) && routes.Count == 1,
        "Route-rule catalog load must reject an isolated surrogate without replacing state.");

    Console.WriteLine("Control-model isolated-surrogate contract checks passed.");
}
finally
{
    try { if (Directory.Exists(root)) Directory.Delete(root, recursive: true); }
    catch (IOException) { }
    catch (UnauthorizedAccessException) { }
}
