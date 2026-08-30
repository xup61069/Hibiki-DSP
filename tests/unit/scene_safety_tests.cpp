// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/scene_safety.hpp"
#include "hibiki/contracts.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::OutputGroupVolumeStateV1;
using hibiki::SceneProfileV1;
using hibiki::SceneSafetyActionKind;
using hibiki::SceneSafetyController;
using hibiki::VolumeOrigin;

SceneProfileV1 make_scene() {
    SceneProfileV1 scene;
    scene.auto_attenuate = true;
    scene.limiter_dbtp = -2.0;
    return scene;
}

OutputGroupVolumeStateV1 make_state(const double requested_db,
                                    const double ceiling_db) {
    OutputGroupVolumeStateV1 state;
    state.requested_db = requested_db;
    state.safety_ceiling_db = ceiling_db;
    return state;
}

}  // namespace

int main() {
    // validate_scene: non-finite limiter targets fail at the shared boundary.
    {
        auto scene = make_scene();
        scene.id = "safety";
        scene.name = "Safety";
        scene.output_group = "main";
        CHECK(hibiki::validate_scene(scene));

        // The shared SceneProfile validator must use the same printable
        // UTF-8 boundary as the scene schema and catalog validator.
        scene.name = "\xE5\xAE\xA2\xE5\xBB\xB3";
        scene.output_group = "\xE4\xB8\xbb";
        CHECK(hibiki::validate_scene(scene));

        scene.name = std::string(120U, 'n');
        CHECK(hibiki::validate_scene(scene));
        scene.name = std::string(121U, 'n');
        CHECK(!hibiki::validate_scene(scene));

        scene.name = "Name\n";
        CHECK(!hibiki::validate_scene(scene));
        scene.name = "Name\x7F" "del";
        CHECK(!hibiki::validate_scene(scene));
        scene.name = "Name\xC2\x9F";
        CHECK(!hibiki::validate_scene(scene));
        scene.name = std::string("Name") + static_cast<char>(0x80);
        CHECK(!hibiki::validate_scene(scene));
        scene.name = "Name\xC3\x28";
        CHECK(!hibiki::validate_scene(scene));
        scene.name = std::string("Name") + std::string("\0hidden", 7U);
        CHECK(!hibiki::validate_scene(scene));

        scene.name = "Safe";
        scene.output_group = "Group\t";
        CHECK(!hibiki::validate_scene(scene));
        scene.output_group = "Group\xC2\x9F" "c1";
        CHECK(!hibiki::validate_scene(scene));
        scene.output_group = std::string("Group") + static_cast<char>(0x80);
        CHECK(!hibiki::validate_scene(scene));
        scene.output_group = "Group\xE2\x82";
        CHECK(!hibiki::validate_scene(scene));
        scene.output_group = std::string("Group") + std::string("\0hidden", 7U);
        CHECK(!hibiki::validate_scene(scene));

        scene.name = "Safe";
        scene.output_group = "main";
        CHECK(hibiki::validate_scene(scene));
        scene.limiter_dbtp = std::numeric_limits<double>::quiet_NaN();
        CHECK(!hibiki::validate_scene(scene));
        scene.limiter_dbtp = std::numeric_limits<double>::infinity();
        CHECK(!hibiki::validate_scene(scene));
        scene.limiter_dbtp = -std::numeric_limits<double>::infinity();
        CHECK(!hibiki::validate_scene(scene));
    }
    // begin: rejects wrong schema version.
    {
        SceneSafetyController controller;
        auto scene = make_scene();
        scene.schema_version = 2U;
        CHECK(!controller.begin(scene, make_state(-12.0, 0.0)));
        CHECK(!controller.active());
    }
    // begin: rejects non-finite or out-of-range limiter targets.
    {
        SceneSafetyController controller;
        auto hot = make_scene();
        hot.limiter_dbtp = std::numeric_limits<double>::quiet_NaN();
        CHECK(!controller.begin(hot, make_state(-12.0, 0.0)));
        auto above_zero = make_scene();
        above_zero.limiter_dbtp = 0.5;
        CHECK(!controller.begin(above_zero, make_state(-12.0, 0.0)));
        auto too_cold = make_scene();
        too_cold.limiter_dbtp = -24.5;
        CHECK(!controller.begin(too_cold, make_state(-12.0, 0.0)));
        CHECK(!controller.active());
    }
    // begin: clamps baseline below safety ceiling.
    {
        SceneSafetyController controller;
        CHECK(controller.begin(make_scene(), make_state(-30.0, -40.0)));
        CHECK(controller.active());
        CHECK(controller.baseline_db() == -40.0);
        const auto ended = controller.end(make_state(-30.0, -40.0));
        CHECK(ended.kind == SceneSafetyActionKind::None);
        CHECK(!controller.active());
    }
    // observe_peak: inactive controller never attenuates.
    {
        SceneSafetyController controller;
        const auto action =
            controller.observe_peak(6.0, 1000U, make_state(-12.0, 0.0));
        CHECK(action.kind == SceneSafetyActionKind::None);
        CHECK(action.origin == VolumeOrigin::Safety);
    }
    // observe_peak: peaks at or below the limiter plus hysteresis are ignored.
    {
        SceneSafetyController controller;
        CHECK(controller.begin(make_scene(), make_state(-12.0, 0.0)));
        const auto action =
            controller.observe_peak(-1.5, 5000U, make_state(-12.0, 0.0));
        CHECK(action.kind == SceneSafetyActionKind::None);
    }
    // observe_peak: first over-limit peak attenuates by the overage amount.
    {
        SceneSafetyController controller;
        CHECK(controller.begin(make_scene(), make_state(-12.0, 0.0)));
        const auto action =
            controller.observe_peak(1.0, 5000U, make_state(-12.0, 0.0));
        CHECK(action.kind == SceneSafetyActionKind::Attenuate);
        CHECK(action.requested_db == -15.0);
        CHECK(action.origin == VolumeOrigin::Safety);
    }
    // observe_peak: attenuation is capped per step.
    {
        SceneSafetyController controller;
        CHECK(controller.begin(make_scene(), make_state(-12.0, 0.0)));
        const auto action =
            controller.observe_peak(20.0, 5000U, make_state(-12.0, 0.0));
        CHECK(action.kind == SceneSafetyActionKind::Attenuate);
        CHECK(action.requested_db == -15.0);
    }
    // observe_peak: attenuation never goes below the digital silence floor.
    {
        SceneSafetyController controller;
        CHECK(controller.begin(make_scene(), make_state(-143.0, 0.0)));
        const auto action =
            controller.observe_peak(20.0, 5000U, make_state(-143.0, 0.0));
        CHECK(action.kind == SceneSafetyActionKind::Attenuate);
        CHECK(action.requested_db == -144.0);
    }
    // observe_peak: enforces minimum interval between actions.
    {
        SceneSafetyController controller;
        CHECK(controller.begin(make_scene(), make_state(-12.0, 0.0)));
        const auto first =
            controller.observe_peak(1.0, 10000U, make_state(-12.0, 0.0));
        CHECK(first.kind == SceneSafetyActionKind::Attenuate);
        const auto second =
            controller.observe_peak(1.0, 10099U, make_state(-15.0, 0.0));
        CHECK(second.kind == SceneSafetyActionKind::None);
        const auto third =
            controller.observe_peak(1.0, 10100U, make_state(-15.0, 0.0));
        CHECK(third.kind == SceneSafetyActionKind::Attenuate);
        CHECK(third.requested_db == -18.0);
    }
    // observe_peak: non-finite peak readings are ignored.
    {
        SceneSafetyController controller;
        CHECK(controller.begin(make_scene(), make_state(-12.0, 0.0)));
        const auto action = controller.observe_peak(
            std::numeric_limits<double>::quiet_NaN(), 5000U,
            make_state(-12.0, 0.0));
        CHECK(action.kind == SceneSafetyActionKind::None);
    }
    // observe_peak: external volume change disables auto attenuation and is
    // treated as a user override; end() then restores nothing.
    {
        SceneSafetyController controller;
        CHECK(controller.begin(make_scene(), make_state(-12.0, 0.0)));
        const auto action =
            controller.observe_peak(1.0, 5000U, make_state(-8.0, 0.0));
        CHECK(action.kind == SceneSafetyActionKind::None);
        CHECK(controller.user_override_detected());
        const auto restore = controller.end(make_state(-8.0, 0.0));
        CHECK(restore.kind == SceneSafetyActionKind::None);
    }
    // end: restores toward the remembered baseline after attenuation and
    // deactivates the session.
    {
        SceneSafetyController controller;
        CHECK(controller.begin(make_scene(), make_state(-12.0, -10.0)));
        const auto attenuate =
            controller.observe_peak(1.0, 5000U, make_state(-12.0, -10.0));
        CHECK(attenuate.kind == SceneSafetyActionKind::Attenuate);
        const auto restore = controller.end(make_state(-15.0, -10.0));
        CHECK(restore.kind == SceneSafetyActionKind::Restore);
        CHECK(restore.requested_db == -12.0);
        CHECK(restore.origin == VolumeOrigin::Scene);
        CHECK(!controller.active());
    }
    // end: without prior attenuation there is nothing to restore.
    {
        SceneSafetyController controller;
        CHECK(controller.begin(make_scene(), make_state(-12.0, -10.0)));
        const auto restore = controller.end(make_state(-12.0, -10.0));
        CHECK(restore.kind == SceneSafetyActionKind::None);
        CHECK(!controller.active());
    }
    // begin/end: auto_attenuate=false tracks override detection only.
    {
        SceneSafetyController controller;
        auto scene = make_scene();
        scene.auto_attenuate = false;
        CHECK(controller.begin(scene, make_state(-12.0, -10.0)));
        const auto action =
            controller.observe_peak(6.0, 5000U, make_state(-12.0, -10.0));
        CHECK(action.kind == SceneSafetyActionKind::None);
        const auto restore = controller.end(make_state(-12.0, -10.0));
        CHECK(restore.kind == SceneSafetyActionKind::None);
    }

    return 0;
}

