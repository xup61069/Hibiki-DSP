// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/scene_presets.hpp"

#include <cstdio>
#include <string>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::EasySceneKind;
using hibiki::IrPhaseMode;
using hibiki::LatencyMode;
using hibiki::make_easy_scene;

}  // namespace

int main()
{
    // ---- shared defaults across every preset --------------------------------
    for (const auto kind : {EasySceneKind::Game, EasySceneKind::Movie,
                            EasySceneKind::Voice, EasySceneKind::Studio}) {
        const auto scene = make_easy_scene(kind, "main");
        CHECK(scene.scene.id == "main.scene");
        CHECK(scene.scene.name == "Hibiki Scene");
        CHECK(scene.scene.output_group == "main");
        CHECK(scene.scene.limiter_dbtp == -1.0);
        CHECK(scene.graph.output_channels == 2U);
        CHECK(scene.graph.lanes.size() == 1U);
        if (scene.graph.lanes.size() == 1U) {
            CHECK(scene.graph.lanes[0].id == "main");
            CHECK(scene.graph.lanes[0].output_group == "main");
            CHECK(scene.graph.lanes[0].channel_count == 2U);
            CHECK(scene.graph.lanes[0].enabled);
        }
    }

    // The output group flows through id, output_group and the lane binding.
    {
        const auto scene = make_easy_scene(EasySceneKind::Game, "low-latency");
        CHECK(scene.scene.id == "low-latency.scene");
        CHECK(scene.scene.output_group == "low-latency");
        CHECK(!scene.graph.lanes.empty() &&
              scene.graph.lanes[0].output_group == "low-latency");
    }

    // ---- Game: low latency, minimum phase, gentle loudness -------------------
    {
        const auto scene = make_easy_scene(EasySceneKind::Game, "main");
        CHECK(scene.scene.latency_mode == LatencyMode::Game);
        CHECK(scene.scene.ir_phase.mode == IrPhaseMode::MinimumPhase);
        CHECK(scene.scene.ir_phase.strength == 0.0);
        CHECK(scene.scene.auto_attenuate);
        CHECK(!scene.graph.strict_direct);
        CHECK(scene.loudness.strength == 0.30);
        CHECK(scene.loudness.max_boost_db == 6.0);
        CHECK(scene.loudness.live_update_enabled);
        CHECK(!scene.program_aware.enabled);
    }

    // ---- Movie: linear phase plus program-aware night mode -------------------
    {
        const auto scene = make_easy_scene(EasySceneKind::Movie, "main");
        CHECK(scene.scene.latency_mode == LatencyMode::MovieLinearPhase);
        CHECK(scene.scene.ir_phase.mode == IrPhaseMode::LinearPhase);
        CHECK(scene.scene.ir_phase.strength == 1.0);
        CHECK(scene.scene.auto_attenuate);
        CHECK(!scene.graph.strict_direct);
        CHECK(scene.loudness.strength == 0.70);
        CHECK(scene.loudness.max_boost_db == 6.0);
        CHECK(scene.program_aware.enabled);
        CHECK(scene.program_aware.target_dbfs == -23.0);
        CHECK(scene.program_aware.max_cut_db == 12.0);
        CHECK(scene.program_aware.bass_correction_enabled);
        CHECK(scene.program_aware.bass_max_cut_db == 4.0);
        CHECK(scene.program_aware.night_compression_enabled);
        CHECK(scene.program_aware.night_compression_max_reduction_db == 9.0);
        CHECK(scene.program_aware.night_compression_knee_db == 12.0);
    }

    // ---- Voice: balanced latency, mixed phase, quiet loudness ----------------
    {
        const auto scene = make_easy_scene(EasySceneKind::Voice, "main");
        CHECK(scene.scene.latency_mode == LatencyMode::Balanced);
        CHECK(scene.scene.ir_phase.mode == IrPhaseMode::MixedPhase);
        CHECK(scene.scene.ir_phase.strength == 0.5);
        CHECK(scene.scene.auto_attenuate);
        CHECK(!scene.graph.strict_direct);
        CHECK(scene.loudness.strength == 0.15);
        CHECK(scene.loudness.max_boost_db == 3.0);
        CHECK(!scene.program_aware.enabled);
    }

    // ---- Studio: strict direct path with all processing off ------------------
    {
        const auto scene = make_easy_scene(EasySceneKind::Studio, "main");
        CHECK(scene.scene.latency_mode == LatencyMode::StrictDirect);
        CHECK(scene.scene.ir_phase.mode == IrPhaseMode::Bypass);
        CHECK(scene.scene.ir_phase.strength == 0.0);
        CHECK(!scene.scene.auto_attenuate);
        CHECK(scene.graph.strict_direct);
        CHECK(!scene.loudness.live_update_enabled);
        CHECK(scene.loudness.strength == 0.0);
        CHECK(scene.loudness.max_boost_db == 0.0);
        CHECK(!scene.program_aware.enabled);
    }

    // ---- presets stay consistent between calls -------------------------------
    {
        const auto first = make_easy_scene(EasySceneKind::Movie, "main");
        const auto second = make_easy_scene(EasySceneKind::Movie, "main");
        CHECK(first.scene.latency_mode == second.scene.latency_mode);
        CHECK(first.loudness.strength == second.loudness.strength);
        CHECK(first.program_aware.enabled == second.program_aware.enabled);
    }

    return 0;
}
