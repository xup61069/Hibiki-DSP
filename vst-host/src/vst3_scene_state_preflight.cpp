// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_scene_state.hpp"

namespace hibiki {

bool preflight_scene_vst3_state_v1(const SceneProfileV1& scene,
                                   void* const context) noexcept {
    if (context == nullptr || scene.schema_version != 1U || scene.id.empty() ||
        scene.output_group.empty()) {
        return false;
    }
    const auto* coordinator = static_cast<const Vst3SceneStateCoordinatorV1*>(context);
    if (coordinator->binding_count() == 0U) return true;
    return coordinator->validate_scene(scene.id) == Vst3SceneStateResultV1::ok;
}

}  // namespace hibiki
