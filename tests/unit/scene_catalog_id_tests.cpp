// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/scene_catalog.hpp"

#include "hibiki/scene_presets.hpp"

#include <cstdint>
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
using namespace hibiki;

SceneDefinitionV1 make_definition(const std::string& scene_id) {
    auto defaults = make_easy_scene(EasySceneKind::Movie, "custom-output");
    SceneDefinitionV1 definition;
    definition.scene = defaults.scene;
    definition.graph = defaults.graph;
    definition.loudness = defaults.loudness;
    definition.scene.id = scene_id;
    definition.scene.name = "Catalog Test";
    return definition;
}
}  // namespace

int main() {
    // ---- valid_scene_id: length bounds --------------------------------------
    {
        auto at_limit = make_definition(std::string(31U, 'a'));
        CHECK(validate_scene_definition_v1(at_limit));
        auto over_limit = make_definition(std::string(32U, 'a'));
        CHECK(!validate_scene_definition_v1(over_limit));
        CHECK(SceneCatalogV1{}.upsert(over_limit) == SceneCatalogResultV1::Invalid);
    }

    // ---- valid_scene_id: character rules ------------------------------------
    {
        auto digits_only = make_definition("123");
        CHECK(validate_scene_definition_v1(digits_only));
        auto leading_separator = make_definition(".lead");
        CHECK(!validate_scene_definition_v1(leading_separator));
        auto uppercase = make_definition("BadId");
        CHECK(!validate_scene_definition_v1(uppercase));
        auto embedded_nul = make_definition(std::string("bad\0id", 6U));
        CHECK(!validate_scene_definition_v1(embedded_nul));
        auto empty_id = make_definition("");
        CHECK(!validate_scene_definition_v1(empty_id));
        auto all_separators = make_definition("a-b_c.d-e");
        CHECK(validate_scene_definition_v1(all_separators));
    }

    // ---- name bound ----------------------------------------------------------
    {
        auto definition = make_definition("name-limit");
        definition.scene.name.assign(120U, 'x');
        CHECK(validate_scene_definition_v1(definition));
        definition.scene.name.assign(121U, 'x');
        CHECK(!validate_scene_definition_v1(definition));
    }

    // ---- upsert: insert then replace keeps size ------------------------------
    {
        SceneCatalogV1 catalog;
        const auto first = make_definition("dup-target");
        CHECK(catalog.upsert(first) == SceneCatalogResultV1::Applied &&
              catalog.size() == 1U);
        auto second = make_definition("dup-target");
        second.scene.name = "Replaced Name";
        CHECK(catalog.upsert(second) == SceneCatalogResultV1::Applied &&
              catalog.size() == 1U);
        const auto* found = catalog.find("dup-target");
        CHECK(found != nullptr && found->scene.name == "Replaced Name");
    }

    // ---- capacity: replacement bypasses the exhausted check ------------------
    {
        SceneCatalogV1 catalog;
        for (std::size_t index = 0U; index < kMaxCustomScenesV1; ++index) {
            const auto definition =
                make_definition("s" + std::to_string(index));
            CHECK(catalog.upsert(definition) == SceneCatalogResultV1::Applied);
        }
        CHECK(catalog.size() == kMaxCustomScenesV1);

        auto fresh = make_definition("fresh-id");
        CHECK(catalog.upsert(fresh) == SceneCatalogResultV1::CapacityExhausted &&
              catalog.size() == kMaxCustomScenesV1 &&
              catalog.find("fresh-id") == nullptr);

        auto replacement = make_definition("s7");
        replacement.scene.name = "In-Place Replacement";
        CHECK(catalog.upsert(replacement) == SceneCatalogResultV1::Applied &&
              catalog.size() == kMaxCustomScenesV1);
        const auto* replaced = catalog.find("s7");
        CHECK(replaced != nullptr &&
              replaced->scene.name == "In-Place Replacement");
    }

    // ---- remove / find / clear boundaries ------------------------------------
    {
        SceneCatalogV1 catalog;
        CHECK(catalog.remove("missing") == SceneCatalogResultV1::NotFound &&
              catalog.size() == 0U);
        CHECK(catalog.remove("Bad ID") == SceneCatalogResultV1::NotFound);
        CHECK(catalog.find("") == nullptr && catalog.find("OK!") == nullptr);

        const auto kept = make_definition("kept");
        CHECK(catalog.upsert(kept) == SceneCatalogResultV1::Applied);
        catalog.clear();
        CHECK(catalog.size() == 0U && catalog.find("kept") == nullptr);
        CHECK(catalog.remove("kept") == SceneCatalogResultV1::NotFound);
    }

    std::fputs("scene_catalog_id_tests passed\n", stdout);
    return 0;
}
