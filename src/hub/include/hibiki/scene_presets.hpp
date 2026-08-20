#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/iso226.hpp"
#include "hibiki/contracts.hpp"
#include "hibiki/scene_graph.hpp"

#include <string>

namespace hibiki {

enum class EasySceneKind : std::uint8_t {
    Game,
    Movie,
    Voice,
    Studio,
};

struct EasySceneDefaultsV1 {
    SceneProfileV1 scene{};
    GraphConfigV1 graph{};
    EqualLoudnessPolicyV1 loudness{};
};

[[nodiscard]] EasySceneDefaultsV1 make_easy_scene(EasySceneKind kind,
                                                   const std::string& output_group);

}  // namespace hibiki
