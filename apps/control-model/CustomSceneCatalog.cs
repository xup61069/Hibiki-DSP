// SPDX-License-Identifier: GPL-3.0-only

namespace Hibiki.ControlModel;

// UI/control-plane mirror for custom Scene cards. The engine remains the
// authoritative owner of the full SceneDefinition (graph/loudness); this
// bounded catalog only keeps the user-facing identity available for selection.
public sealed class CustomSceneCatalogV1
{
    public const int MaxScenes = 32;
    private readonly List<SceneCard> _scenes = new(MaxScenes);

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

    private static bool IsValid(SceneCard scene)
    {
        if (string.IsNullOrWhiteSpace(scene.Id) || scene.Id.Length > 31 ||
            string.IsNullOrWhiteSpace(scene.Name) || scene.Name.Length > 120 ||
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
}
