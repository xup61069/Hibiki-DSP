// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/scene_catalog.hpp"
#include "hibiki/scene_presets.hpp"

#include <cstdio>
#include <string>

#define CHECK(expr) do { if (!(expr)) { std::fputs("FAILED: " #expr "\n", stderr); return 1; } } while (false)

namespace {

using hibiki::EasySceneKind;
using hibiki::LatencyMode;
using hibiki::make_easy_scene;
using hibiki::SceneCatalogResultV1;
using hibiki::SceneCatalogV1;
using hibiki::SceneDefinitionV1;

SceneDefinitionV1 valid_scene(const std::string& scene_id)
{
    auto defaults = make_easy_scene(EasySceneKind::Game, "main");
    SceneDefinitionV1 definition;
    definition.scene = std::move(defaults.scene);
    definition.graph = std::move(defaults.graph);
    definition.loudness = std::move(defaults.loudness);
    definition.scene.id = scene_id;
    return definition;
}

SceneDefinitionV1 strict_direct_scene(const std::string& scene_id)
{
    auto defaults = make_easy_scene(EasySceneKind::Studio, "main");
    SceneDefinitionV1 definition;
    definition.scene = std::move(defaults.scene);
    definition.graph = std::move(defaults.graph);
    definition.loudness = std::move(defaults.loudness);
    definition.scene.id = scene_id;
    return definition;
}

}  // namespace

int main()
{
    // ---- validate: defaults from presets are accepted ------------------------
    {
        const auto definition = valid_scene("catalog.sample");
        CHECK(hibiki::validate_scene_definition_v1(definition));
    }

    // ---- validate: schema version must be exactly 1 --------------------------
    {
        auto definition = valid_scene("catalog.sample");
        definition.schema_version = 2U;
        CHECK(!hibiki::validate_scene_definition_v1(definition));
    }

    // ---- validate: id character contract -------------------------------------
    {
        auto definition = valid_scene("");
        CHECK(!hibiki::validate_scene_definition_v1(definition));
    }
    {
        auto definition = valid_scene(std::string(32U, 'a'));
        CHECK(!hibiki::validate_scene_definition_v1(definition));
    }
    {
        auto definition = valid_scene("Catalog");
        CHECK(!hibiki::validate_scene_definition_v1(definition));
    }
    {
        auto definition = valid_scene("_leading");
        CHECK(!hibiki::validate_scene_definition_v1(definition));
    }
    {
        auto definition = valid_scene("has space");
        CHECK(!hibiki::validate_scene_definition_v1(definition));
    }
    {
        std::string with_nul{"ok"};
        with_nul.push_back('\0');
        with_nul.append("id");
        auto definition = valid_scene(with_nul);
        CHECK(!hibiki::validate_scene_definition_v1(definition));
    }
    {
        auto definition = valid_scene("9start.digit");
        CHECK(hibiki::validate_scene_definition_v1(definition));
    }

    // ---- validate: name length -----------------------------------------------
    {
        auto definition = valid_scene("catalog.sample");
        definition.scene.name.clear();
        CHECK(!hibiki::validate_scene_definition_v1(definition));
    }
    {
        auto definition = valid_scene("catalog.sample");
        definition.scene.name = std::string(121U, 'n');
        CHECK(!hibiki::validate_scene_definition_v1(definition));
    }

    // ---- validate: scene text is printable UTF-8 ----------------------------
    {
        auto definition = valid_scene("catalog.unicode");
        definition.scene.name = "\xE5\xAE\xA2\xE5\xBB\xB3";
        definition.scene.output_group = "\xE4\xB8\xbb";
        CHECK(hibiki::validate_scene_definition_v1(definition));
    }
    {
        auto definition = valid_scene("catalog.name-control");
        definition.scene.name = "Name\n";
        CHECK(!hibiki::validate_scene_definition_v1(definition));
    }
    {
        auto definition = valid_scene("catalog.name-malformed");
        definition.scene.name = "Name\xC3\x28";
        CHECK(!hibiki::validate_scene_definition_v1(definition));
    }
    {
        auto definition = valid_scene("catalog.group-control");
        definition.scene.output_group = "main\t";
        CHECK(!hibiki::validate_scene_definition_v1(definition));
    }
    {
        auto definition = valid_scene("catalog.group-malformed");
        definition.scene.output_group = "\xE2\x82";
        CHECK(!hibiki::validate_scene_definition_v1(definition));
    }

    // ---- validate: latency mode and strict_direct must agree -----------------
    {
        auto definition = valid_scene("catalog.sample");
        definition.graph.strict_direct = true;
        CHECK(!hibiki::validate_scene_definition_v1(definition));
    }
    {
        auto definition = strict_direct_scene("catalog.strict");
        CHECK(hibiki::validate_scene_definition_v1(definition));
        definition.scene.latency_mode = LatencyMode::Game;
        CHECK(!hibiki::validate_scene_definition_v1(definition));
    }

    // ---- catalog: upsert, replace, find ---------------------------------------
    {
        SceneCatalogV1 catalog;
        CHECK(catalog.size() == 0U);
        CHECK(catalog.find("missing") == nullptr);

        auto first = valid_scene("catalog.alpha");
        first.scene.name = "Alpha";
        CHECK(catalog.upsert(first) == SceneCatalogResultV1::Applied);
        CHECK(catalog.size() == 1U);

        const auto* found = catalog.find("catalog.alpha");
        CHECK(found != nullptr);
        CHECK(found->scene.name == "Alpha");
        CHECK(found->scene.output_group == "main");

        auto replacement = valid_scene("catalog.alpha");
        replacement.scene.name = "Alpha v2";
        CHECK(catalog.upsert(replacement) == SceneCatalogResultV1::Applied);
        CHECK(catalog.size() == 1U);
        const auto* updated = catalog.find("catalog.alpha");
        CHECK(updated != nullptr && updated->scene.name == "Alpha v2");

        CHECK(catalog.upsert(valid_scene("")) == SceneCatalogResultV1::Invalid);
        CHECK(catalog.size() == 1U);

        auto invalid_group = valid_scene("catalog.invalid-group");
        invalid_group.scene.output_group = "main\x7F";
        CHECK(catalog.upsert(invalid_group) == SceneCatalogResultV1::Invalid);
        CHECK(catalog.size() == 1U);

        CHECK(catalog.remove("catalog.missing") ==
              SceneCatalogResultV1::NotFound);
        CHECK(catalog.remove("catalog.alpha") == SceneCatalogResultV1::Applied);
        CHECK(catalog.size() == 0U);
        CHECK(catalog.find("catalog.alpha") == nullptr);
    }

    // ---- catalog: clear --------------------------------------------------------
    {
        SceneCatalogV1 catalog;
        for (int index = 0; index < 4; ++index) {
            const auto definition =
                valid_scene("catalog.s" + std::to_string(index));
            CHECK(catalog.upsert(definition) == SceneCatalogResultV1::Applied);
        }
        catalog.clear();
        CHECK(catalog.size() == 0U);
        CHECK(catalog.find("catalog.s0") == nullptr);
        const auto reuse = valid_scene("catalog.s0");
        CHECK(catalog.upsert(reuse) == SceneCatalogResultV1::Applied);
    }

    // ---- catalog: capacity is fail-closed at 32 --------------------------------
    {
        SceneCatalogV1 catalog;
        for (std::size_t index = 0; index < hibiki::kMaxCustomScenesV1; ++index) {
            const auto definition = valid_scene("catalog.s" + std::to_string(index));
            CHECK(catalog.upsert(definition) == SceneCatalogResultV1::Applied);
        }
        CHECK(catalog.size() == hibiki::kMaxCustomScenesV1);

        const auto overflow = valid_scene("catalog.overflow");
        CHECK(catalog.upsert(overflow) == SceneCatalogResultV1::CapacityExhausted);
        CHECK(catalog.size() == hibiki::kMaxCustomScenesV1);

        CHECK(catalog.remove("catalog.s7") == SceneCatalogResultV1::Applied);
        CHECK(catalog.upsert(valid_scene("catalog.refill")) ==
              SceneCatalogResultV1::Applied);
        CHECK(catalog.size() == hibiki::kMaxCustomScenesV1);
    }

    return 0;
}
