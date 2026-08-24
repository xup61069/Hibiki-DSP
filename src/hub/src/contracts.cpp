#include "hibiki/contracts.hpp"

#include "hibiki/control_payloads.hpp"

namespace hibiki {

bool validate_scene(const SceneProfileV1& scene) noexcept {
    if (scene.schema_version != 1 || scene.id.empty() || scene.name.empty() ||
        scene.output_group.empty() || scene.output_group.size() > 64U ||
        scene.limiter_dbtp > -1.0 || scene.limiter_dbtp < -20.0 ||
        scene.lanes.size() > 32U ||
        scene.automation_timeline_ids.size() > 16U ||
        scene.ir_reference.size() > 64U ||
        (scene.ir_reference.size() > 0U && scene.ir_reference.size() < 8U) ||
        !validate_ir_phase_policy(scene.ir_phase)) {
        return false;
    }
    for (const auto& timeline_id : scene.automation_timeline_ids) {
        if (timeline_id.empty() || timeline_id.size() > 64U ||
            !is_printable_utf8_v1(timeline_id)) {
            return false;
        }
    }
    for (const auto& lane_id : scene.lanes) {
        if (lane_id.empty() || lane_id.size() > 64U ||
            !is_printable_utf8_v1(lane_id)) {
            return false;
        }
    }
    if (!scene.ir_reference.empty() && !is_printable_utf8_v1(scene.ir_reference)) {
        return false;
    }
    return true;
}

}  // namespace hibiki
