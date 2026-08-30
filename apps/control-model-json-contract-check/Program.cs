// SPDX-License-Identifier: GPL-3.0-only

using Hibiki.ControlModel;

static void Check(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

var directory = Path.Combine(
    Path.GetTempPath(), $"hibiki-control-model-json-{Guid.NewGuid():N}");
Directory.CreateDirectory(directory);

try
{
    var validResponsePath = Path.Combine(directory, "valid-response.json");
    File.WriteAllText(validResponsePath, """
        {
          "schema_version": 1,
          "sample_rate": 48000,
          "channels": 2,
          "points": [
            { "frequency_hz": 100, "measured_db": -2, "target_db": 0 },
            { "frequency_hz": 1000, "measured_db": 0.25, "target_db": 0 }
          ]
        }
        """);
    Check(CalibrationCompilerV1.TryLoadResponse(
              validResponsePath, out var response, out _) &&
          response is not null && response.Points.Count == 2,
        "Known calibration response properties must remain loadable.");

    var unknownResponseTopLevelPath = Path.Combine(directory, "unknown-response-top.json");
    File.WriteAllText(unknownResponseTopLevelPath, """
        {
          "schema_version": 1,
          "sample_rate": 48000,
          "channels": 2,
          "unexpected": true,
          "points": [
            { "frequency_hz": 100, "measured_db": -2, "target_db": 0 }
          ]
        }
        """);
    Check(!CalibrationCompilerV1.TryLoadResponse(
              unknownResponseTopLevelPath, out _, out _),
        "Unknown top-level calibration response properties must be rejected.");

    var unknownResponsePointPath = Path.Combine(directory, "unknown-response-point.json");
    File.WriteAllText(unknownResponsePointPath, """
        {
          "schema_version": 1,
          "sample_rate": 48000,
          "channels": 2,
          "points": [
            { "frequency_hz": 100, "measured_db": -2, "target_db": 0, "unexpected": true }
          ]
        }
        """);
    Check(!CalibrationCompilerV1.TryLoadResponse(
              unknownResponsePointPath, out _, out _),
        "Unknown nested calibration point properties must be rejected.");

    var validPresetPath = Path.Combine(directory, "valid-preset.json");
    File.WriteAllText(validPresetPath, """
        {
          "schema_version": 1,
          "filters": [
            { "type": "peaking", "frequency_hz": 100, "gain_db": 1, "q": 1 }
          ]
        }
        """);
    Check(CalibrationCompilerV1.TryLoadPreset(
              validPresetPath, out var preset, out _) &&
          preset is not null && preset.Filters.Count == 1,
        "Known PEQ preset properties must remain loadable.");

    var unknownPresetTopLevelPath = Path.Combine(directory, "unknown-preset-top.json");
    File.WriteAllText(unknownPresetTopLevelPath, """
        {
          "schema_version": 1,
          "unexpected": true,
          "filters": [
            { "type": "peaking", "frequency_hz": 100, "gain_db": 1, "q": 1 }
          ]
        }
        """);
    Check(!CalibrationCompilerV1.TryLoadPreset(
              unknownPresetTopLevelPath, out _, out _),
        "Unknown top-level PEQ preset properties must be rejected.");

    var unknownPresetFilterPath = Path.Combine(directory, "unknown-preset-filter.json");
    File.WriteAllText(unknownPresetFilterPath, """
        {
          "schema_version": 1,
          "filters": [
            { "type": "peaking", "frequency_hz": 100, "gain_db": 1, "q": 1, "unexpected": true }
          ]
        }
        """);
    Check(!CalibrationCompilerV1.TryLoadPreset(
              unknownPresetFilterPath, out _, out _),
        "Unknown nested PEQ filter properties must be rejected.");

    var validRouteRulesPath = Path.Combine(directory, "valid-route-rules.json");
    File.WriteAllText(validRouteRulesPath, """
        {
          "schema_version": 1,
          "rules": [
            {
              "rule_id": "quiet-game",
              "priority": 20,
              "enabled": true,
              "gain_owner": 0,
              "makeup_gain_db": 3.5,
              "app_id": "game.exe",
              "display_name": "DJMAX",
              "lane_id": "game",
              "output_group": "surround"
            }
          ]
        }
        """);
    var routeRules = new SessionRouteRuleCatalogV1();
    Check(routeRules.TryLoad(validRouteRulesPath, out _) && routeRules.Count == 1 &&
          routeRules.Rules[0].RuleId == "quiet-game",
        "Known route-rule properties must remain loadable.");

    var unknownRouteRulesTopLevelPath = Path.Combine(directory, "unknown-route-rules-top.json");
    File.WriteAllText(unknownRouteRulesTopLevelPath, """
        {
          "schema_version": 1,
          "unexpected": true,
          "rules": [
            {
              "rule_id": "quiet-game",
              "priority": 20,
              "enabled": true,
              "gain_owner": 0,
              "makeup_gain_db": 3.5,
              "app_id": "game.exe",
              "display_name": "DJMAX",
              "lane_id": "game",
              "output_group": "surround"
            }
          ]
        }
        """);
    Check(!routeRules.TryLoad(unknownRouteRulesTopLevelPath, out _) &&
          routeRules.Count == 1 && routeRules.Rules[0].RuleId == "quiet-game",
        "Unknown top-level route-rule properties must fail closed and preserve the catalog.");

    var unknownRouteRulePath = Path.Combine(directory, "unknown-route-rule.json");
    File.WriteAllText(unknownRouteRulePath, """
        {
          "schema_version": 1,
          "rules": [
            {
              "rule_id": "quiet-game",
              "priority": 20,
              "enabled": true,
              "gain_owner": 0,
              "makeup_gain_db": 3.5,
              "app_id": "game.exe",
              "display_name": "DJMAX",
              "lane_id": "game",
              "output_group": "surround",
              "unexpected": true
            }
          ]
        }
        """);
    Check(!routeRules.TryLoad(unknownRouteRulePath, out _) &&
          routeRules.Count == 1 && routeRules.Rules[0].RuleId == "quiet-game",
        "Unknown nested route-rule properties must fail closed and preserve the catalog.");
}
finally
{
    if (Directory.Exists(directory)) Directory.Delete(directory, recursive: true);
}

Console.WriteLine("Control-model JSON strict-property checks passed.");
