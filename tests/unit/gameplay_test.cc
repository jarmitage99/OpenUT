#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/props_engine.h"
#include "engine/world.h"

#include "test_world.h"

#include <catch_amalgamated.hpp>

#include <vector>

TEST_CASE("traces do not report actors behind level geometry", "[data]")
{
    testworld::Deck deck;
    ut::World& world = deck.world;
    auto [start, direction] = testworld::clearLine(world, 200.0f);
    ut::CheckResult wall;
    REQUIRE(world.bspLineCheck(wall, start, start + direction * 20000.0f, {}));
    float distance = (wall.location - start).size();
    ut::UObject* box = testworld::spawnBox(world, start + direction * (distance + 150.0f));
    ut::Vector end = start + direction * (distance + 300.0f);

    std::vector<ut::CheckResult> hits = world.multiLineCheck(start, end, {}, nullptr, ut::TRACE_AllColliding);
    REQUIRE_FALSE(hits.empty());
    CHECK(hits.front().actor == world.levelInfo);
    bool boxHit = false;
    for (const ut::CheckResult& hit : hits) {
        boxHit = boxHit || hit.actor == box;
    }
    CHECK_FALSE(boxHit);

    std::vector<ut::CheckResult> actorsOnly = world.multiLineCheck(start, end, {}, nullptr, ut::TRACE_AllColliding & ~(ut::TRACE_Level | ut::TRACE_LevelGeometry));
    bool boxWithoutLevel = false;
    for (const ut::CheckResult& hit : actorsOnly) {
        boxWithoutLevel = boxWithoutLevel || hit.actor == box;
    }
    CHECK(boxWithoutLevel);
}

TEST_CASE("player swims in a water zone", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Turbine"));
    ut::World world;
    REQUIRE(world.loadMap("DM-Turbine?game=Botpack.DeathMatchPlus"));
    ut::UObject* player = world.player();
    REQUIRE(player != nullptr);
    world.tick(1.0f / 30.0f);
    ut::vm::Call(player, ut::Name("Fire")).f(0.0f).run();
    for (int i = 0; i < 90 && ut::actor::Physics(player) != 1; i++) {
        world.tick(1.0f / 30.0f);
    }
    REQUIRE(ut::actor::Physics(player) == 1);
    ut::UObject* water = testworld::firstOfClass(world, "WaterZone");
    REQUIRE(water != nullptr);
    REQUIRE(world.farMoveActor(player, ut::actor::Location(water), false, true));
    for (int i = 0; i < 20; i++) {
        world.tick(1.0f / 30.0f);
    }
    CHECK(ut::region(player).zone == water);
    CHECK(ut::actor::Physics(player) == 3);
    ut::objects::unloadPackage(ut::Name("DM-Turbine"));
}
