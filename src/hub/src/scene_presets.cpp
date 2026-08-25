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
    defaults.loudness.standard = "equal-loudness-derived";
    // Built-in Relative-mode scenes ship with live phon recompute enabled:
    // accepted VolumeNotifications then drive the bounded debounced recompute
    // through the normal control plane. Studio overrides this below; Strict
    // Direct keeps the fail-closed default.
    defaults.loudness.live_update_enabled = true;

    switch (kind) {
        case EasySceneKind::Game:
            defaults.scene.latency_mode = LatencyMode::Game;
            defaults.scene.ir_phase = IrPhasePolicyV1{1, IrPhaseMode::MinimumPhase, 0.0};
            defaults.loudness.strength = 0.30;
            defaults.loudness.max_boost_db = 6.0;
            break;
        case EasySceneKind::Movie:
            defaults.scene.latency_mode = LatencyMode::MovieLinearPhase;
            defaults.scene.ir_phase = IrPhasePolicyV1{1, IrPhaseMode::LinearPhase, 1.0};
            defaults.loudness.strength = 0.70;
            defaults.loudness.max_boost_db = 6.0;
            defaults.program_aware.enabled = true;
            defaults.program_aware.target_dbfs = -23.0;
            defaults.program_aware.max_cut_db = 12.0;
            defaults.program_aware.bass_correction_enabled = true;
            defaults.program_aware.bass_max_cut_db = 4.0;
            defaults.program_aware.night_compression_enabled = true;
            defaults.program_aware.night_compression_max_reduction_db = 9.0;
            defaults.program_aware.night_compression_knee_db = 12.0;
            break;
        case EasySceneKind::Voice:
            defaults.scene.latency_mode = LatencyMode::Balanced;
            defaults.scene.ir_phase = IrPhasePolicyV1{1, IrPhaseMode::MixedPhase, 0.5};
            defaults.loudness.strength = 0.15;
            defaults.loudness.max_boost_db = 3.0;
            break;
        case EasySceneKind::Studio:
            defaults.scene.latency_mode = LatencyMode::StrictDirect;
            defaults.scene.ir_phase = IrPhasePolicyV1{1, IrPhaseMode::Bypass, 0.0};
            defaults.scene.auto_attenuate = false;
            defaults.graph.strict_direct = true;
            defaults.loudness.live_update_enabled = false;
            defaults.loudness.strength = 0.0;
            defaults.loudness.max_boost_db = 0.0;
            break;
    }
    return defaults;
}

}  // namespace hibiki
