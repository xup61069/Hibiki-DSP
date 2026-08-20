#include "hibiki/contracts.hpp"

namespace hibiki {

bool validate_scene(const SceneProfileV1& scene) noexcept {
    return scene.schema_version == 1 && !scene.id.empty() && !scene.name.empty() &&
           !scene.output_group.empty() && scene.limiter_dbtp <= -1.0 &&
           scene.limiter_dbtp >= -20.0 && validate_ir_phase_policy(scene.ir_phase);
}

}  // namespace hibiki
