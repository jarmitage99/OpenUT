#include "compiler/asset_package.h"
#include "compiler/script_package.h"
#include "core/config.h"
#include "core/objects.h"
#include "engine/fractal_paint.h"
#include "engine/mesh.h"
#include "engine/props_fire.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace {
    ut::UObject* newTexture(const std::string& packageName, const std::string& name, const std::string& className)
    {
        ut::engine::initialize(OPENUT_GAME_DATA);
        ut::UObject* package = ut::objects::packageObject(ut::Name(packageName));
        std::vector<std::string> log;
        REQUIRE(ut::compiler::runAssetCommand(package, {}, "TEXTURE NEW NAME=" + name + " CLASS=" + className + " PACKAGE=" + packageName + " USIZE=64 VSIZE=64", log));
        ut::UObject* texture = ut::objects::findObject(packageName + "." + name, nullptr);
        REQUIRE(texture != nullptr);
        return texture;
    }

    std::vector<int> sparkBytes(const ut::FireSpark& spark)
    {
        return { spark.type, spark.heat, spark.x, spark.y, spark.a, spark.b, spark.c, spark.d };
    }

    std::vector<int> dropBytes(ut::UObject* texture, int index)
    {
        const uint8_t* drop = ut::watertexture::Drops(texture) + index * 8;
        return std::vector<int>(drop, drop + 8);
    }
}

TEST_CASE("fire painting adds, lines, erases and closes sparks like UnrealEd", "[data]")
{
    ut::UObject* object = newTexture("PaintFireTest", "Torch", "FireTexture");
    ut::UFireTexture* fire = ut::cast<ut::UFireTexture>(object);
    REQUIRE(fire != nullptr);
    REQUIRE(ut::fractalpaint::paintable(fire));
    ut::fractalpaint::PaintState state;
    ut::firetexture::SparkType(fire) = 4;
    ut::fractalpaint::paint(fire, state, 10, 20, true, false);
    ut::fractalpaint::paint(fire, state, 11, 20, true, false);
    ut::fractalpaint::paint(fire, state, 11, 20, false, false);
    REQUIRE(ut::firetexture::NumSparks(fire) == 2);
    CHECK(sparkBytes(fire->sparks[0]) == std::vector<int>({ 4, 255, 10, 20, 0, 0, 0, 0 }));
    CHECK(sparkBytes(fire->sparks[1]) == std::vector<int>({ 4, 255, 11, 20, 0, 0, 0, 0 }));

    ut::firetexture::SparkType(fire) = 0;
    ut::fractalpaint::paint(fire, state, 0, 0, true, false);
    ut::fractalpaint::paint(fire, state, 5, 0, true, false);
    ut::fractalpaint::paint(fire, state, 5, 0, false, false);
    REQUIRE(ut::firetexture::NumSparks(fire) == 8);
    std::vector<int> xs;
    for (size_t i = 2; i < 8; i++) {
        CHECK(fire->sparks[i].y == 0);
        xs.push_back(fire->sparks[i].x);
    }
    CHECK(xs == std::vector<int>({ 0, 0, 1, 2, 3, 4 }));

    ut::fractalpaint::paint(fire, state, 10, 20, false, true);
    ut::fractalpaint::paint(fire, state, 10, 20, false, false);
    CHECK(ut::firetexture::NumSparks(fire) == 6);
    CHECK(fire->sparks.size() == 6);
    for (const ut::FireSpark& spark : fire->sparks) {
        CHECK(spark.y == 0);
    }

    ut::firetexture::SparkType(fire) = 13;
    ut::fractalpaint::addSpark(fire, state, 30, 40);
    CHECK(sparkBytes(fire->sparks[6]) == std::vector<int>({ 13, 255, 30, 40, 2, 241, 0, 2 }));

    ut::firetexture::SparkType(fire) = 23;
    ut::fractalpaint::paint(fire, state, 5, 5, true, false);
    REQUIRE(ut::firetexture::NumSparks(fire) == 8);
    CHECK(sparkBytes(fire->sparks[7]) == std::vector<int>({ 23, 0, 5, 5, 0, 0, 0, 0 }));
    ut::fractalpaint::paint(fire, state, 15, 9, true, false);
    REQUIRE(ut::firetexture::NumSparks(fire) == 8);
    CHECK(sparkBytes(fire->sparks[7]) == std::vector<int>({ 23, 255, 5, 5, 10, 4, 0, 0 }));
    ut::fractalpaint::paint(fire, state, 15, 9, false, false);
    CHECK(fire->sparks[7].d == 16);

    ut::fractalpaint::addSpark(fire, state, 64, 10);
    ut::fractalpaint::addSpark(fire, state, -1, 10);
    CHECK(ut::firetexture::NumSparks(fire) == 8);

    std::filesystem::path file = ut::config::userDirectory() / "paint-test" / "PaintFireTest.utx";
    std::string error;
    REQUIRE(ut::compiler::saveAssetPackage(ut::objects::packageObject(ut::Name("PaintFireTest")), file, &error));
    for (ut::UObject* item : ut::compiler::packageContents(ut::objects::packageObject(ut::Name("PaintFireTest")))) {
        item->destroyed = true;
    }
    ut::Linker* linker = ut::objects::loadPackageFile(ut::Name("PaintFireTest"), file, false);
    ut::UFireTexture* loaded = ut::cast<ut::UFireTexture>(linker->object(linker->findExport(ut::Name("Torch"), ut::Name("FireTexture"), "*")));
    REQUIRE(loaded != nullptr);
    ut::objects::ensureLoaded(loaded);
    ut::objects::flushLoads();
    CHECK(ut::firetexture::NumSparks(loaded) == 8);
    REQUIRE(loaded->sparks.size() == 8);
    CHECK(sparkBytes(loaded->sparks[6]) == std::vector<int>({ 13, 255, 30, 40, 2, 241, 0, 2 }));
    CHECK(sparkBytes(loaded->sparks[7]) == std::vector<int>({ 23, 255, 5, 5, 10, 4, 0, 16 }));
}

TEST_CASE("water painting adds drops with their phase and erases them", "[data]")
{
    ut::UObject* water = newTexture("PaintWaterTest", "Pool", "WaveTexture");
    REQUIRE(ut::fractalpaint::paintable(water));
    ut::fractalpaint::PaintState state;
    ut::watertexture::DropType(water) = 1;
    ut::fractalpaint::paint(water, state, 20, 31, true, false);
    REQUIRE(ut::watertexture::NumDrops(water) == 1);
    CHECK(dropBytes(water, 0) == std::vector<int>({ 1, 8, 10, 15, 0, 0, 0, 8 }));
    ut::fractalpaint::paint(water, state, 22, 31, true, false);
    CHECK(ut::watertexture::NumDrops(water) == 2);
    ut::fractalpaint::paint(water, state, 22, 31, false, false);

    ut::watertexture::DropType(water) = 8;
    ut::fractalpaint::paint(water, state, 50, 50, true, false);
    ut::fractalpaint::paint(water, state, 54, 50, true, false);
    ut::fractalpaint::paint(water, state, 54, 50, false, false);
    REQUIRE(ut::watertexture::NumDrops(water) == 3);
    CHECK(dropBytes(water, 2) == std::vector<int>({ 8, 255, 25, 25, 0, 0, 0, 30 }));

    ut::fractalpaint::paint(water, state, 21, 30, false, true);
    CHECK(ut::watertexture::NumDrops(water) == 1);
    CHECK(dropBytes(water, 0)[0] == 8);
}
