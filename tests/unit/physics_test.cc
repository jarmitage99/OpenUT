#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/props_engine.h"
#include "engine/world.h"
#include "test_world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>

using namespace testworld;

namespace {
    struct Wall {
        ut::Vector start;
        ut::Vector direction;
        float distance = 0;
    };

    Wall wallAhead(ut::World& world, float minimum, float maximum)
    {
        for (ut::UObject* a : world.actors) {
            if (!a || a->destroyed || !a->isA(ut::Name("PathNode"))) {
                continue;
            }
            ut::Vector point = ut::actor::Location(a);
            std::optional<float> floor = floorBelow(world, point, 200.0f);
            if (!floor) {
                continue;
            }
            ut::Vector start { point.x, point.y, *floor + 40.0f };
            for (const ut::Vector& direction : { ut::Vector { 1, 0, 0 }, ut::Vector { -1, 0, 0 }, ut::Vector { 0, 1, 0 }, ut::Vector { 0, -1, 0 } }) {
                ut::CheckResult hit;
                if (!world.bspLineCheck(hit, start, start + direction * maximum, {})) {
                    continue;
                }
                float distance = (hit.location - start).size();
                if (distance < minimum || std::abs(hit.normal.z) > 0.01f || hit.normal.dot(direction) > -0.99f) {
                    continue;
                }
                bool flat = true;
                for (float step = 0; step < distance - 30.0f && flat; step += 20.0f) {
                    flat = floorBelow(world, start + direction * step, 200.0f) == floor;
                }
                ut::CheckResult swept;
                flat = flat && !world.bspLineCheck(swept, start + ut::Vector { 0, 0, 10 }, start + ut::Vector { 0, 0, 10 } + direction * (distance - 40.0f), { 30, 30, 40 });
                ut::CheckResult up;
                bool tall = !world.bspLineCheck(up, hit.location - direction * 30.0f, hit.location - direction * 30.0f + ut::Vector { 0, 0, 300 }, {});
                if (flat && tall && freeOfActors(world, start, hit.location)) {
                    return { start, direction, distance };
                }
            }
        }
        FAIL("no wall ahead between " << minimum << " and " << maximum);
        return {};
    }

    ut::UObject* spider(ut::World& world, const ut::Vector& location)
    {
        ut::UObject* nali = spawnNali(world, location);
        REQUIRE(world.farMoveActor(nali, { location.x, location.y, location.z - 40.0f + ut::actor::CollisionHeight(nali) + 1.0f }, false, true));
        ut::setPhysics(nali, ut::PHYS_Spider, nullptr);
        ut::pawn::Floor(nali) = { 0, 0, 1 };
        ut::pawn::DesiredSpeed(nali) = 1.0f;
        ut::actor::Velocity(nali) = {};
        return nali;
    }
}

TEST_CASE("a spider pawn walks along the floor it is stuck to", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    Wall wall = wallAhead(world, 400.0f, 2000.0f);
    ut::UObject* nali = spider(world, wall.start);
    float floorZ = ut::actor::Location(nali).z;
    ut::actor::Acceleration(nali) = wall.direction * ut::pawn::AccelRate(nali);
    for (int i = 0; i < 10; i++) {
        ut::performPhysics(nali, 0.05f);
    }
    CHECK(ut::actor::Physics(nali) == ut::PHYS_Spider);
    CHECK((ut::actor::Location(nali) - wall.start).dot(wall.direction) > 20.0f);
    CHECK(std::abs(ut::actor::Location(nali).z - floorZ) < 2.0f);
    CHECK(ut::pawn::Floor(nali).z == Catch::Approx(1.0f));
    CHECK(ut::actor::DesiredRotation(nali).pitch == 0);
}

TEST_CASE("a spider pawn that runs into a wall climbs it and drops off when it pushes away", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    Wall wall = wallAhead(world, 80.0f, 300.0f);
    ut::UObject* nali = spider(world, wall.start);
    float startZ = ut::actor::Location(nali).z;
    ut::actor::Acceleration(nali) = wall.direction * ut::pawn::AccelRate(nali);
    bool onWall = false;
    for (int i = 0; i < 60 && !onWall; i++) {
        ut::performPhysics(nali, 0.05f);
        onWall = std::abs(ut::pawn::Floor(nali).z) < 0.1f;
    }
    REQUIRE(onWall);
    CHECK(ut::pawn::Floor(nali).dot(wall.direction) == Catch::Approx(-1.0f).margin(0.01f));
    ut::actor::Acceleration(nali) = { 0, 0, ut::pawn::AccelRate(nali) };
    for (int i = 0; i < 10; i++) {
        ut::performPhysics(nali, 0.05f);
    }
    CHECK(ut::actor::Physics(nali) == ut::PHYS_Spider);
    CHECK(ut::actor::Location(nali).z > startZ + 20.0f);

    ut::actor::Acceleration(nali) = ut::pawn::Floor(nali) * ut::pawn::AccelRate(nali);
    ut::performPhysics(nali, 0.05f);
    CHECK(ut::actor::Physics(nali) == ut::PHYS_Falling);
    CHECK(ut::pawn::Floor(nali).zero());
}

TEST_CASE("a rolling actor that hits a wall stops at it instead of passing through", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    Wall wall = wallAhead(world, 150.0f, 400.0f);
    ut::UObject* box = spawnBox(world, wall.start);
    for (int i = 0; i < 60 && ut::actor::Physics(box) != ut::PHYS_None; i++) {
        world.tick(1.0f / 30.0f);
    }
    REQUIRE(ut::actor::Physics(box) == ut::PHYS_None);
    ut::setPhysics(box, ut::PHYS_Rolling, nullptr);
    ut::actor::bBounce(box) = 0;
    ut::actor::Velocity(box) = wall.direction * 10000.0f;
    ut::actor::Acceleration(box) = {};
    for (int i = 0; i < 10; i++) {
        ut::performPhysics(box, 0.05f);
    }
    float travelled = (ut::actor::Location(box) - wall.start).dot(wall.direction);
    CHECK(travelled > wall.distance - ut::actor::CollisionRadius(box) - 5.0f);
    CHECK(travelled <= wall.distance - ut::actor::CollisionRadius(box) + 0.5f);
}

namespace {
    ut::UObject* linked(ut::UObject* point, const char* field)
    {
        return point ? *reinterpret_cast<ut::UObject**>(point->propertyAddress(ut::Name(field))) : nullptr;
    }

    float& rateModifier(ut::UObject* point)
    {
        return *reinterpret_cast<float*>(point->propertyAddress(ut::Name("RateModifier")));
    }

    struct Intro {
        ut::World world;

        Intro()
        {
            ut::engine::initialize(OPENUT_GAME_DATA);
            ut::objects::unloadPackage(ut::Name("CityIntro"));
            REQUIRE(world.loadMap("CityIntro"));
        }

        ~Intro()
        {
            ut::objects::unloadPackage(ut::Name("CityIntro"));
        }
    };

    ut::UObject* follower(ut::World& world, ut::UObject* target, float rate)
    {
        ut::UObject* box = spawnClass(world, "UnrealShare.WoodenBox", ut::actor::Location(target));
        ut::setCollision(box, false, false, false);
        ut::actor::bCollideWorld(box) = 0;
        ut::setPhysics(box, ut::PHYS_Interpolating, nullptr);
        ut::actor::Target(box) = target;
        ut::actor::PhysRate(box) = rate;
        ut::actor::PhysAlpha(box) = 0;
        ut::actor::bInterpolating(box) = 1;
        return box;
    }
}

TEST_CASE("an actor interpolating towards the last point of a path moves in a straight line", "[data]")
{
    Intro intro;
    ut::UObject* dest = nullptr;
    for (ut::UObject* a : intro.world.actors) {
        if (a && a->isA(ut::Name("InterpolationPoint")) && linked(a, "Next")) {
            dest = a;
            break;
        }
    }
    REQUIRE(dest != nullptr);
    ut::UObject* next = linked(dest, "Next");
    *reinterpret_cast<ut::UObject**>(next->propertyAddress(ut::Name("Next"))) = nullptr;
    rateModifier(dest) = 1.0f;
    rateModifier(next) = 1.0f;
    ut::UObject* box = follower(intro.world, dest, 1.0f);
    ut::performPhysics(box, 0.25f);
    ut::Vector expected = ut::actor::Location(dest) + (ut::actor::Location(next) - ut::actor::Location(dest)) * 0.25f;
    CHECK((ut::actor::Location(box) - expected).size() < 0.01f);
    CHECK(ut::actor::PhysAlpha(box) == Catch::Approx(0.25f));
    CHECK(ut::actor::Target(box) == dest);
}

TEST_CASE("an actor following a path crosses points without jumping and can run it backwards", "[data]")
{
    Intro intro;
    ut::UObject* dest = nullptr;
    for (ut::UObject* a : intro.world.actors) {
        if (!a || !a->isA(ut::Name("InterpolationPoint"))) {
            continue;
        }
        ut::UObject* next = linked(a, "Next");
        if (linked(a, "Prev") && next && linked(next, "Next") && linked(linked(next, "Next"), "Next")) {
            dest = a;
            break;
        }
    }
    REQUIRE(dest != nullptr);
    ut::UObject* next = linked(dest, "Next");
    for (ut::UObject* point : { linked(dest, "Prev"), dest, next, linked(next, "Next"), linked(linked(next, "Next"), "Next") }) {
        rateModifier(point) = 1.0f;
    }
    ut::UObject* box = follower(intro.world, dest, 1.0f);
    ut::performPhysics(box, 0.999f);
    ut::Vector before = ut::actor::Location(box);
    CHECK(ut::actor::Target(box) == dest);
    ut::performPhysics(box, 0.002f);
    CHECK(ut::actor::Target(box) == next);
    CHECK(ut::actor::PhysAlpha(box) == Catch::Approx(0.001f).margin(0.0001f));
    float segment = (ut::actor::Location(next) - ut::actor::Location(dest)).size();
    CHECK((ut::actor::Location(box) - before).size() < segment * 0.01f);

    ut::actor::PhysRate(box) = -1.0f;
    ut::performPhysics(box, 0.5f);
    CHECK(ut::actor::Target(box) == dest);
    CHECK(ut::actor::PhysAlpha(box) == Catch::Approx(0.501f).margin(0.0001f));
}

TEST_CASE("a ripper blade never flies faster than its maximum speed and bounces off walls", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    Wall wall = wallAhead(world, 200.0f, 600.0f);
    ut::UObject* blade = spawnClass(world, "Botpack.Razor2", wall.start);
    REQUIRE(ut::actor::Physics(blade) == ut::PHYS_Projectile);
    float maxSpeed = ut::projectile::MaxSpeed(blade);
    REQUIRE(maxSpeed > 0);
    ut::actor::Velocity(blade) = wall.direction * (maxSpeed * 5.0f);
    ut::actor::Acceleration(blade) = {};
    ut::performPhysics(blade, 0.01f);
    CHECK(ut::actor::Velocity(blade).size() == Catch::Approx(maxSpeed).epsilon(0.001));
    CHECK((ut::actor::Location(blade) - wall.start).dot(wall.direction) == Catch::Approx(maxSpeed * 0.01f).epsilon(0.001));
    float deepest = 0;
    bool bounced = false;
    for (int i = 0; i < 200 && !bounced && !blade->destroyed; i++) {
        ut::performPhysics(blade, 0.02f);
        deepest = std::max(deepest, (ut::actor::Location(blade) - wall.start).dot(wall.direction));
        bounced = ut::actor::Velocity(blade).dot(wall.direction) < 0;
    }
    REQUIRE_FALSE(blade->destroyed);
    CHECK(bounced);
    CHECK(deepest < wall.distance);
}

TEST_CASE("a flying pawn stops at a wall it flies into level and slides up a wall it rises along", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    Wall wall = wallAhead(world, 80.0f, 300.0f);

    SECTION("level flight into a wall stops at the wall")
    {
        ut::UObject* nali = spawnNali(world, wall.start + ut::Vector { 0, 0, 20 });
        ut::setPhysics(nali, ut::PHYS_Flying, nullptr);
        ut::pawn::DesiredSpeed(nali) = 1.0f;
        float speed = ut::pawn::AirSpeed(nali);
        REQUIRE(speed > 0);
        for (int i = 0; i < 40; i++) {
            ut::actor::Acceleration(nali) = wall.direction * ut::pawn::AccelRate(nali);
            ut::performPhysics(nali, 0.05f);
        }
        float travelled = (ut::actor::Location(nali) - wall.start).dot(wall.direction);
        CHECK(travelled > wall.distance - ut::actor::CollisionRadius(nali) - 5.0f);
        CHECK(travelled <= wall.distance - ut::actor::CollisionRadius(nali) + 0.5f);
        CHECK(ut::actor::Physics(nali) == ut::PHYS_Flying);
    }

    SECTION("rising flight into a wall slides upwards along it")
    {
        ut::UObject* nali = spawnNali(world, wall.start + ut::Vector { 0, 0, 20 });
        ut::setPhysics(nali, ut::PHYS_Flying, nullptr);
        ut::pawn::DesiredSpeed(nali) = 1.0f;
        ut::Vector climb = (wall.direction + ut::Vector { 0, 0, 2 }).normal();
        float startZ = ut::actor::Location(nali).z;
        float lastZ = startZ;
        bool slid = false;
        for (int i = 0; i < 40; i++) {
            ut::actor::Acceleration(nali) = climb * ut::pawn::AccelRate(nali);
            ut::performPhysics(nali, 0.05f);
            float travelled = (ut::actor::Location(nali) - wall.start).dot(wall.direction);
            REQUIRE(travelled <= wall.distance - ut::actor::CollisionRadius(nali) + 0.5f);
            if (travelled > wall.distance - ut::actor::CollisionRadius(nali) - 2.0f && ut::actor::Location(nali).z > lastZ + 1.0f) {
                slid = true;
            }
            lastZ = ut::actor::Location(nali).z;
        }
        CHECK(slid);
        CHECK(lastZ > startZ + 40.0f);
    }
}
