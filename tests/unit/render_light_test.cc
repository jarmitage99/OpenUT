#include "core/objects.h"
#include "engine/props_engine.h"
#include "engine/world.h"
#include "render/lighting.h"

#include <catch_amalgamated.hpp>

namespace {
    ut::UObject* makeSpotlight()
    {
        ut::engine::initialize(OPENUT_GAME_DATA);
        ut::UClass* lightClass = ut::objects::findClass("Engine.Light");
        REQUIRE(lightClass != nullptr);
        ut::UObject* light = ut::objects::newObject(lightClass, ut::objects::packageObject(ut::Name("Transient")), ut::Name(), 0);
        ut::actor::LightRadius(light) = 3;
        ut::actor::LightEffect(light) = 12;
        ut::actor::LightCone(light) = 128;
        ut::actor::Rotation(light) = { -16384, 0, 0 };
        return light;
    }
}

TEST_CASE("prepared spotlight illumination matches hand computed values", "[data]")
{
    ut::UObject* light = makeSpotlight();
    ut::Vector location { 0, 0, 100 };
    ut::Vector up { 0, 0, 1 };
    ut::PreparedLight prepared = ut::prepareLight(light, location);
    CHECK(prepared.radius == 100.0f);
    CHECK(prepared.spotDirection.z == Catch::Approx(1.0f).margin(0.0001));
    CHECK(prepared.outer == Catch::Approx(0.5f).margin(0.00001));
    CHECK(ut::lightIllumination(prepared, { 0, 0, 50 }, up, 0) == Catch::Approx(0.5f).margin(0.00005));
    CHECK(ut::lightIllumination(prepared, { 30, 0, 50 }, up, 0) == Catch::Approx(0.16499f).margin(0.00005));
    CHECK(ut::lightIllumination(prepared, { 60, 0, 90 }, up, 0) == 0.0f);
    CHECK(ut::lightIllumination(light, location, { 30, 0, 50 }, up, 0) == Catch::Approx(0.16499f).margin(0.00005));
}
