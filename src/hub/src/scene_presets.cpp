#include "hibiki/scene_presets.hpp"

namespace hibiki {

EasySceneDefaultsV1 make_easy_scene(const EasySceneKind kind,
                                    const std::string& output_group) {
    EasySceneDefaultsV1 defaults;
    defaults.scene.id = output_group + ".scene";
    defaults.scene.name = "Hibiki Scene";
    defaults.scene.output_group = output_group;
    defaults.scene.limiter_dbtp = -1.0;
    defaults.scene.auto_attenuate = true;
    defaults.graph.output_channels = 2;
    defaults.graph.lanes.push_back(LaneConfigV1{"main", output_group, 2, 0.0, true});
    defaults.loudness.reference_phon = 80.0;
    defaults.loudness.standard = "iso-226-2023-derived";

    switch (kind) {
        case EasySceneKind::Game:
            defaults.scene.latency_mode = LatencyMode::Game;
            defaults.loudness.strength = 0.30;
            defaults.loudness.max_boost_db = 6.0;
            break;
        case EasySceneKind::Movie:
            defaults.scene.latency_mode = LatencyMode::MovieLinearPhase;
            defaults.loudness.strength = 0.70;
            defaults.loudness.max_boost_db = 6.0;
            break;
        case EasySceneKind::Voice:
            defaults.scene.latency_mode = LatencyMode::Balanced;
            defaults.loudness.strength = 0.15;
            defaults.loudness.max_boost_db = 3.0;
            break;
        case EasySceneKind::Studio:
            defaults.scene.latency_mode = LatencyMode::StrictDirect;
            defaults.scene.auto_attenuate = false;
            defaults.graph.strict_direct = true;
            defaults.loudness.strength = 0.0;
            defaults.loudness.max_boost_db = 0.0;
            break;
    }
    return defaults;
}

}  // namespace hibiki
