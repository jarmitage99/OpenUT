#include "core/objects.h"
#include "engine/csg.h"
#include "engine/lightmap_builder.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"
#include "render/lighting.h"
#include "render/lightmap.h"

#include <catch_amalgamated.hpp>

#include <cmath>

namespace {
    ut::UObject* makeLight(uint8_t type, uint8_t period, uint8_t phase)
    {
        ut::engine::initialize(OPENUT_GAME_DATA);
        ut::UObject* light = ut::objects::newObject(ut::objects::findClass("Engine.Light"), ut::objects::packageObject(ut::Name("LightAnimTest")), ut::Name(), 0);
        light->loaded = true;
        ut::actor::LightType(light) = type;
        ut::actor::LightBrightness(light) = 128;
        ut::actor::LightHue(light) = 0;
        ut::actor::LightSaturation(light) = 255;
        ut::actor::LightPeriod(light) = period;
        ut::actor::LightPhase(light) = phase;
        ut::actor::LightRadius(light) = 3;
        return light;
    }

    float brightnessRatio(ut::UObject* light, float time)
    {
        uint8_t type = ut::actor::LightType(light);
        ut::Vector animated = ut::lightColor(light, time);
        ut::actor::LightType(light) = 1;
        ut::Vector steady = ut::lightColor(light, time);
        ut::actor::LightType(light) = type;
        return animated.x / steady.x;
    }

    ut::UObject* makeEffectLight(uint8_t effect)
    {
        ut::UObject* light = makeLight(1, 0, 0);
        ut::actor::LightEffect(light) = effect;
        return light;
    }

    template <typename T>
    T* make(const char* className, ut::UObject* outer)
    {
        ut::UObject* object = ut::objects::newObject(ut::csg::nativeClass(className), outer, ut::Name(), 0);
        object->loaded = true;
        return ut::cast<T>(object);
    }

    struct Room {
        ut::ULevel* level = nullptr;
        ut::UObject* light = nullptr;
    };

    Room makeLitRoom(const char* packageName)
    {
        ut::engine::initialize(OPENUT_GAME_DATA);
        ut::UObject* package = ut::objects::packageObject(ut::Name(packageName));
        Room room;
        room.level = make<ut::ULevel>("Level", package);
        room.level->model = make<ut::UModel>("Model", package);
        ut::UObject* builder = make<ut::UObject>("Brush", package);
        ut::UModel* builderModel = make<ut::UModel>("Model", package);
        builderModel->polys = make<ut::UPolys>("Polys", package);
        ut::actor::Brush(builder) = builderModel;
        room.level->actors.push_back(nullptr);
        room.level->actors.push_back(builder);
        ut::csg::buildCube(*builderModel->polys, 512.0f, 512.0f, 256.0f, nullptr);
        ut::csg::addBrush(room.level, builder, ut::csg::CsgSubtract);
        room.light = ut::objects::newObject(ut::objects::findClass("Engine.Light"), package, ut::Name(), 0);
        room.light->loaded = true;
        ut::actor::Location(room.light) = { 0.0f, 0.0f, 0.0f };
        ut::actor::LightType(room.light) = 1;
        ut::actor::LightBrightness(room.light) = 64;
        ut::actor::LightRadius(room.light) = 32;
        ut::actor::LightPeriod(room.light) = 35;
        ut::actor::bStatic(room.light) = 1;
        room.level->actors.push_back(room.light);
        ut::csg::rebuild(room.level);
        ut::lightbuild::rebuild(room.level);
        return room;
    }

    const ut::BspSurf* floorSurf(const ut::UModel& model)
    {
        for (const ut::BspSurf& surf : model.surfs) {
            if (model.vectors[static_cast<size_t>(surf.vNormal)].z > 0.99f) {
                return &surf;
            }
        }
        return nullptr;
    }
}

TEST_CASE("pulse and subtle pulse follow the table sine of 35 T over period plus phase", "[data]")
{
    ut::UObject* pulse = makeLight(2, 35, 0);
    CHECK(brightnessRatio(pulse, 0.25f) == Catch::Approx(0.99f).margin(0.00001));
    CHECK(brightnessRatio(pulse, 0.75f) == Catch::Approx(0.21f).margin(0.00001));
    ut::actor::LightPhase(pulse) = 64;
    CHECK(brightnessRatio(pulse, 0.5f) == Catch::Approx(0.21f).margin(0.00001));
    ut::UObject* subtle = makeLight(7, 35, 0);
    CHECK(brightnessRatio(subtle, 0.25f) == Catch::Approx(0.99f).margin(0.00001));
    CHECK(brightnessRatio(subtle, 0.75f) == Catch::Approx(0.81f).margin(0.00001));
}

TEST_CASE("blink keeps the low bit of the phase counter at a large level time", "[data]")
{
    ut::UObject* blink = makeLight(3, 0, 0);
    float even = 200.0f;
    float odd = 200.0f + 1.0f / 65536.0f;
    REQUIRE(odd != even);
    CHECK(ut::lightColor(blink, even).x > 0.1f);
    CHECK(ut::lightColor(blink, odd).x == 0.0f);
    CHECK(ut::truncateToInt(3000000000.0) == -1294967296);
}

TEST_CASE("fast wave shifts the integer distance before applying the time phase", "[data]")
{
    ut::UObject* wave = makeEffectLight(6);
    ut::UObject* plain = makeEffectLight(0);
    ut::Vector location { 0, 0, 22 };
    ut::Vector point { 0, 0, 0 };
    ut::Vector up { 0, 0, 1 };
    CHECK(ut::sqrtApprox(484.0f) == 22.0f);
    float base = ut::lightIllumination(plain, location, point, up, 0);
    REQUIRE(base > 0.0f);
    CHECK(ut::lightIllumination(wave, location, point, up, 0) / base == Catch::Approx(0.9494409f).margin(0.00001));
    CHECK(ut::lightIllumination(wave, location, point, up, 1.0f) / base == Catch::Approx(0.8148050f).margin(0.00001));
}

TEST_CASE("auto panning advances 35 texels per second of zone speed and wraps at 1024 texels")
{
    CHECK(ut::autoPanTexels(2.0f, 1.5f) == 105.0f);
    CHECK(ut::autoPanTexels(100.0f, 2.0f) == 856.0f);
    CHECK(ut::autoPanTexels(1.0f, -1.0f) == 989.0f);
    CHECK(ut::smallWavyPanU(0.0f) == Catch::Approx(4.0f).margin(0.00001));
    CHECK(ut::smallWavyPanV(0.0f) == Catch::Approx(8.0f).margin(0.00001));
    CHECK(ut::smallWavyPanU(1.0f) == Catch::Approx(4.066664f).margin(0.00001));
    CHECK(ut::smallWavyPanV(1.0f) == Catch::Approx(7.305239f).margin(0.00001));
}

TEST_CASE("continuous randoms move at most a sixteenth per tick while bases are redrawn")
{
    ut::tickLightRandoms(10.0f);
    float bases[256];
    float randoms[256];
    for (int32_t i = 0; i < 256; i++) {
        bases[i] = ut::lightRandomBase(i);
        randoms[i] = ut::lightRandom(i);
    }
    ut::tickLightRandoms(10.0f + 1.0f / 35.0f + 0.001f);
    int32_t basesChanged = 0;
    int32_t randomsChanged = 0;
    float largestStep = 0;
    for (int32_t i = 0; i < 256; i++) {
        if (ut::lightRandomBase(i) != bases[i]) {
            basesChanged++;
        }
        if (ut::lightRandom(i) != randoms[i]) {
            randomsChanged++;
        }
        largestStep = std::max(largestStep, std::abs(ut::lightRandom(i) - randoms[i]));
    }
    CHECK(basesChanged > 250);
    CHECK(randomsChanged > 200);
    CHECK(largestStep <= 1.0f / 16.0f + 0.00001f);
}

TEST_CASE("a pulsing static light relights its surface over time while a steady one stays baked", "[data]")
{
    Room room = makeLitRoom("LightAnimRoomTest");
    ut::UModel& model = *room.level->model;
    const ut::BspSurf* floor = floorSurf(model);
    REQUIRE(floor != nullptr);
    REQUIRE(floor->lightMap >= 0);
    ut::Vector ambient {};
    size_t texel = 8 * 16 + 8;

    ut::SurfaceLighting steady = ut::prepareSurfaceLighting(&model, *floor, ambient);
    CHECK(steady.lights.empty());
    ut::LightmapTexels steadyEarly = ut::computeLightmap(&model, *floor, ambient, 0.25f);
    ut::LightmapTexels steadyLate = ut::computeLightmap(&model, *floor, ambient, 0.75f);
    REQUIRE(steadyEarly.colors.size() > texel);
    CHECK(steadyEarly.colors[texel].x > 0.05f);
    CHECK(steadyEarly.colors[texel].x == steadyLate.colors[texel].x);

    ut::actor::LightType(room.light) = 2;
    ut::SurfaceLighting pulsing = ut::prepareSurfaceLighting(&model, *floor, ambient);
    CHECK(pulsing.lights.size() == 1);
    ut::LightmapTexels bright = ut::computeLightmap(&model, *floor, ambient, 0.25f);
    ut::LightmapTexels dim = ut::computeLightmap(&model, *floor, ambient, 0.75f);
    CHECK(bright.colors[texel].x == Catch::Approx(steadyEarly.colors[texel].x * 0.99f).margin(2.0f / 127.5f));
    CHECK(dim.colors[texel].x == Catch::Approx(steadyEarly.colors[texel].x * 0.21f).margin(2.0f / 127.5f));
    CHECK(dim.colors[texel].x < bright.colors[texel].x - 0.05f);
}
