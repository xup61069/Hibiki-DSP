// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/scene_presets.hpp"

#include "hibiki/scene_graph.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {
using hibiki::EasySceneDefaultsV1;
using hibiki::EasySceneKind;
using hibiki::EqualLoudnessMode;
using hibiki::IrPhaseMode;
using hibiki::LatencyMode;
using hibiki::make_easy_scene;
using hibiki::validate_graph;
}  // namespace

int main() {
    // Every built-in Easy scene must produce a valid scene profile and a
    // valid graph for its default output group.
    const std::string group = "main";
    const auto game = make_easy_scene(EasySceneKind::Game, group);
    const auto movie = make_easy_scene(EasySceneKind::Movie, group);
    const auto voice = make_easy_scene(EasySceneKind::Voice, group);
    const auto studio = make_easy_scene(EasySceneKind::Studio, group);

    CHECK(hibiki::validate_scene(game.scene));
    CHECK(validate_graph(game.graph));
    CHECK(hibiki::validate_scene(movie.scene));
    CHECK(validate_graph(movie.graph));
    CHECK(hibiki::validate_scene(voice.scene));
    CHECK(validate_graph(voice.graph));
    CHECK(hibiki::validate_scene(studio.scene));
    CHECK(validate_graph(studio.graph));

    // The scene id is derived from the output-group label so the same preset
    // can be instantiated for different groups without id collisions.
    CHECK(game.scene.id == "main.scene");
    CHECK(movie.scene.id == "main.scene");
    CHECK(voice.scene.id == "main.scene");
    CHECK(studio.scene.id == "main.scene");
    const auto other_group = make_easy_scene(EasySceneKind::Game, "tv");
    CHECK(other_group.scene.id == "tv.scene");
    CHECK(other_group.scene.output_group == "tv");
    CHECK(!other_group.graph.lanes.empty() &&
          other_group.graph.lanes[0].output_group == "tv");

    // Game: minimum phase (no added buffering), moderate loudness strength,
    // live recompute enabled.
    CHECK(game.scene.latency_mode == LatencyMode::Game);
    CHECK(game.scene.ir_phase.mode == IrPhaseMode::MinimumPhase &&
          game.scene.ir_phase.strength == 0.0);
    CHECK(std::abs(game.loudness.strength - 0.30) < 1e-9);
    CHECK(std::abs(game.loudness.max_boost_db - 6.0) < 1e-9);
    CHECK(game.loudness.live_update_enabled);
    CHECK(game.loudness.mode == EqualLoudnessMode::Relative);

    // Movie: linear phase with the policy budget, strong loudness, program
    // awareness including bass and night compression opt-ins.
    CHECK(movie.scene.latency_mode == LatencyMode::MovieLinearPhase);
    CHECK(movie.scene.ir_phase.mode == IrPhaseMode::LinearPhase &&
          std::abs(movie.scene.ir_phase.strength - 1.0) < 1e-9);
    CHECK(std::abs(movie.loudness.strength - 0.70) < 1e-9);
    CHECK(std::abs(movie.loudness.max_boost_db - 6.0) < 1e-9);
    CHECK(movie.program_aware.enabled);
    CHECK(std::abs(movie.program_aware.target_dbfs - (-23.0)) < 1e-9);
    CHECK(std::abs(movie.program_aware.max_cut_db - 12.0) < 1e-9);
    CHECK(movie.program_aware.bass_correction_enabled);
    CHECK(std::abs(movie.program_aware.bass_max_cut_db - 4.0) < 1e-9);
    CHECK(movie.program_aware.night_compression_enabled);
    CHECK(std::abs(movie.program_aware.night_compression_max_reduction_db - 9.0) < 1e-9);
    CHECK(std::abs(movie.program_aware.night_compression_knee_db - 12.0) < 1e-9);

    // Voice: mixed phase at half strength, gentle loudness.
    CHECK(voice.scene.latency_mode == LatencyMode::Balanced);
    CHECK(voice.scene.ir_phase.mode == IrPhaseMode::MixedPhase &&
          std::abs(voice.scene.ir_phase.strength - 0.5) < 1e-9);
    CHECK(std::abs(voice.loudness.strength - 0.15) < 1e-9);
    CHECK(std::abs(voice.loudness.max_boost_db - 3.0) < 1e-9);
    CHECK(voice.loudness.live_update_enabled);

    // Studio: strict direct bypass; loudness disabled entirely and the graph
    // must agree on strict_direct so validate passes.
    CHECK(studio.scene.latency_mode == LatencyMode::StrictDirect);
    CHECK(studio.scene.ir_phase.mode == IrPhaseMode::Bypass &&
          studio.scene.ir_phase.strength == 0.0);
    CHECK(studio.graph.strict_direct);
    CHECK(!studio.scene.auto_attenuate);
    CHECK(!studio.loudness.live_update_enabled);
    CHECK(studio.loudness.strength == 0.0 && studio.loudness.max_boost_db == 0.0);
    CHECK(!studio.program_aware.enabled);

    // Shared invariants: every scene carries the same fail-closed defaults for
    // limiter and reference phon regardless of kind.
    for (const auto* d : {&game, &movie, &voice, &studio}) {
        CHECK(d->scene.schema_version == 1U);
        CHECK(d->graph.schema_version == 1U);
        CHECK(d->loudness.schema_version == 1U);
        CHECK(d->program_aware.schema_version == 1U);
        CHECK(d->graph.output_channels == 2U);
        CHECK(d->graph.lanes.size() == 1U);
        CHECK(d->graph.lanes[0].channel_count == 2U);
        CHECK(d->graph.lanes[0].enabled);
        CHECK(std::abs(d->scene.limiter_dbtp - (-1.0)) < 1e-9);
        CHECK(d->loudness.standard == "equal-loudness-derived");
        CHECK(std::abs(d->loudness.reference_phon - 80.0) < 1e-9);
    }

    return 0;
}
