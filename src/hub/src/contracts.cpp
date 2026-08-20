#include "hibiki/contracts.hpp"

namespace hibiki {

bool validate_scene(const SceneProfileV1& scene) noexcept {
    if (scene.schema_version != 1 || scene.id.empty() || scene.name.empty() ||
        scene.output_group.empty() || scene.limiter_dbtp > -1.0 || scene.limiter_dbtp < -20.0 ||
        scene.automation_timeline_ids.size() > 16U || !validate_ir_phase_policy(scene.ir_phase)) {
        return false;
    }
    for (const auto& timeline_id : scene.automation_timeline_ids) {
        if (timeline_id.empty() || timeline_id.size() > 64U ||
            timeline_id.find('\0') != std::string::npos) {
            return false;
        }
    }
    return true;
}

}  // namespace hibiki
