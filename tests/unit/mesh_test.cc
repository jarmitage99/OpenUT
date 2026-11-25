#include "core/objects.h"
#include "engine/mesh.h"
#include "engine/reader.h"
#include "engine/world.h"
#include "render/lighting.h"
#include "render/lightmap.h"

#include <catch_amalgamated.hpp>

TEST_CASE("Unreal hue saturation brightness conversion")
{
    ut::Vector white = ut::fGetHSV(0, 255, 255);
    CHECK(white.x == Catch::Approx(0.82128f).margin(0.0005));
    CHECK(white.y == Catch::Approx(0.82128f).margin(0.0005));
    CHECK(white.z == Catch::Approx(0.82128f).margin(0.0005));
    ut::Vector red = ut::fGetHSV(0, 0, 255);
    CHECK(red.x == Catch::Approx(0.82128f).margin(0.0005));
    CHECK(red.y == Catch::Approx(0.0f).margin(0.0005));
    CHECK(red.z == Catch::Approx(0.0f).margin(0.0005));
    ut::Vector black = ut::fGetHSV(40, 0, 0);
    CHECK(black.x == 0.0f);
}

TEST_CASE("light distance falloff")
{
    CHECK(ut::lightDistanceFalloff(0.25f) == Catch::Approx(0.5f).margin(0.0005));
    CHECK(ut::lightDistanceFalloff(0.81f) == Catch::Approx(0.0281f).margin(0.0005));
    CHECK(ut::lightDistanceFalloff(0.0001f) == Catch::Approx(0.99927f).margin(0.0001));
    CHECK(ut::lightDistanceFalloff(1.0f) == Catch::Approx(0.0f).margin(0.000001));
    CHECK(ut::lightDistanceFalloff(1.001f) == 0.0f);
}

TEST_CASE("every LodMesh in the game's System packages parses to its export end", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    size_t meshes = 0;
    for (const char* package : { "Botpack", "UnrealShare", "UnrealI", "Engine", "relics", "multimesh", "epiccustommodels" }) {
        ut::Linker* linker = ut::objects::loadPackage(ut::Name(package));
        REQUIRE(linker != nullptr);
        const ut::Package& file = linker->file();
        for (size_t i = 0; i < file.exports().size(); i++) {
            int32_t ref = static_cast<int32_t>(i + 1);
            if (file.classOf(ref) != "LodMesh") {
                continue;
            }
            const ut::ExportEntry& entry = file.exportAt(ref);
            ut::Reader reader(file.data(), static_cast<size_t>(entry.offset));
            ut::objects::readTaggedProperties(nullptr, nullptr, reader, *linker);
            ut::ULodMesh mesh;
            mesh.serialize(reader, *linker);
            INFO(package << "." << file.objectName(ref));
            CHECK(reader.tell() == static_cast<size_t>(entry.offset + entry.size));
            meshes++;
        }
    }
    CHECK(meshes > 300);
}

TEST_CASE("lightmap texels lie on their surface plane", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::Linker* linker = ut::objects::loadPackage(ut::Name("DM-Deck16]["));
    REQUIRE(linker != nullptr);
    ut::ULevel* level = ut::cast<ut::ULevel>(linker->object(linker->findExport(ut::Name("MyLevel"), ut::Name("Level"), "*")));
    REQUIRE(level != nullptr);
    ut::objects::ensureLoaded(level);
    ut::UModel* model = level->model;
    ut::objects::ensureLoaded(model);
    REQUIRE(model != nullptr);
    size_t checked = 0;
    for (const ut::BspSurf& surf : model->surfs) {
        if (surf.lightMap < 0 || checked >= 50) {
            continue;
        }
        const ut::LightMapIndex& index = model->lightMaps[static_cast<size_t>(surf.lightMap)];
        ut::Vector normal = model->vectors[static_cast<size_t>(surf.vNormal)];
        ut::Vector base = model->points[static_cast<size_t>(surf.pBase)];
        ut::Vector corner = ut::lightmapTexelPoint(model, surf, index.uClamp - 1, index.vClamp - 1);
        CHECK(normal.dot(corner - base) == Catch::Approx(0.0f).margin(0.05));
        checked++;
    }
    CHECK(checked == 50);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
}
