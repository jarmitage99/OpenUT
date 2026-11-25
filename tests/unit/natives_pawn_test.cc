#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"
#include "test_world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {
    struct Deck {
        std::unique_ptr<ut::World> world;

        Deck()
        {
            ut::engine::initialize(OPENUT_GAME_DATA);
            ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
            world = std::make_unique<ut::World>();
            REQUIRE(world->loadMap("DM-Deck16]["));
        }

        ~Deck()
        {
            world.reset();
            ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
        }

        ut::UObject* actor(const char* name)
        {
            for (ut::UObject* a : world->actors) {
                if (a && !a->destroyed && a->name == ut::Name(name)) {
                    return a;
                }
            }
            return nullptr;
        }
    };

    ut::UObject* walker(Deck& deck)
    {
        ut::UObject* player = deck.world->player();
        REQUIRE(player != nullptr);
        ut::UObject* bot = deck.world->spawn(ut::objects::findClass("Botpack.TMale1Bot"), nullptr, ut::Name(), ut::actor::Location(player), {}, nullptr, true);
        REQUIRE(bot != nullptr);
        ut::actor::Physics(bot) = ut::PHYS_Walking;
        ut::actor::Velocity(bot) = {};
        ut::actor::Acceleration(bot) = {};
        ut::vm::stateFrame(bot).latentAction = 0;
        return bot;
    }

    void moveTo(ut::UObject* self, const ut::Vector& destination, float speed)
    {
        ut::vm::Call call(self, ut::Name("MoveTo"));
        call.v(destination).f(speed).run();
    }

    ut::UObject* objectResult(ut::vm::Call& call)
    {
        return *reinterpret_cast<ut::UObject**>(call.returnValue());
    }
}

TEST_CASE("MoveTo caps the speed at MaxDesiredSpeed and times the move by 1.3 times the distance over the ground speed", "[data]")
{
    Deck deck;
    ut::UObject* bot = walker(deck);
    ut::pawn::GroundSpeed(bot) = 300.0f;
    ut::pawn::MaxDesiredSpeed(bot) = 0.5f;
    ut::Vector start = ut::actor::Location(bot);
    moveTo(bot, start + ut::Vector { 390, 0, 0 }, 0.8f);
    CHECK(ut::pawn::DesiredSpeed(bot) == 0.5f);
    CHECK(ut::pawn::MoveTimer(bot) == Catch::Approx(1.0 + 1.3 * 390.0 / 150.0).margin(1e-4));
    moveTo(bot, start + ut::Vector { 390, 0, 0 }, 0.3f);
    CHECK(ut::pawn::DesiredSpeed(bot) == 0.3f);
    CHECK(ut::pawn::MoveTimer(bot) == Catch::Approx(1.0 + 1.3 * 390.0 / 90.0).margin(1e-4));
    moveTo(bot, start + ut::Vector { 390, 0, 0 }, 0.0f);
    CHECK(ut::pawn::MoveTimer(bot) == 0.5f);
    CHECK(ut::vm::stateFrame(bot).latentAction == 501);
}

TEST_CASE("MoveToward a pawn allows 1.2 seconds and walking toward a pawn keeps the pitch", "[data]")
{
    Deck deck;
    ut::UObject* bot = walker(deck);
    ut::UObject* player = deck.world->player();
    REQUIRE(player != nullptr);
    ut::actor::Location(player) = ut::actor::Location(bot) + ut::Vector { 300, 0, 300 };
    ut::vm::Call call(bot, ut::Name("MoveToward"));
    call.obj(player).f(1.0f).run();
    CHECK(ut::pawn::MoveTimer(bot) == 1.2f);
    CHECK(ut::pawn::MoveTarget(bot) == player);
    CHECK(std::abs(ut::actor::DesiredRotation(bot).pitch - 8192) <= 1);
}

TEST_CASE("a move ends within 16 units in 2D when the height difference is below 48", "[data]")
{
    Deck deck;
    ut::UObject* bot = walker(deck);
    REQUIRE(ut::actor::CollisionHeight(bot) < 48.0f);
    ut::actor::Physics(bot) = ut::PHYS_Flying;
    ut::Vector start = ut::actor::Location(bot);
    moveTo(bot, start + ut::Vector { 12, 0, 40 }, 1.0f);
    CHECK(ut::actor::Acceleration(bot).x == 0.0f);
    CHECK(ut::actor::Acceleration(bot).z == 0.0f);
    moveTo(bot, start + ut::Vector { 12, 0, 60 }, 1.0f);
    double length = std::sqrt(12.0 * 12.0 + 60.0 * 60.0);
    CHECK(ut::actor::Acceleration(bot).x == Catch::Approx(12.0 / length * ut::pawn::AccelRate(bot)).margin(1e-3));
    CHECK(ut::actor::Acceleration(bot).z == Catch::Approx(60.0 / length * ut::pawn::AccelRate(bot)).margin(1e-3));
}

TEST_CASE("TurnTo leaves the acceleration and TurnToward and StrafeFacing aim with FaceTarget", "[data]")
{
    Deck deck;
    ut::UObject* bot = walker(deck);
    ut::UObject* player = deck.world->player();
    ut::actor::Acceleration(bot) = { 100, 0, 0 };
    {
        ut::vm::Call call(bot, ut::Name("TurnTo"));
        call.v(ut::actor::Location(bot) + ut::Vector { 0, 100, 0 }).run();
    }
    CHECK(ut::actor::Acceleration(bot).x == 100.0f);
    CHECK(ut::vm::stateFrame(bot).latentAction == 509);
    ut::UObject* marker = deck.actor("PathNode0");
    REQUIRE(marker != nullptr);
    ut::pawn::MoveTarget(bot) = marker;
    {
        ut::vm::Call call(bot, ut::Name("TurnToward"));
        call.obj(player).run();
    }
    CHECK(ut::pawn::MoveTarget(bot) == marker);
    CHECK(ut::pawn::FaceTarget(bot) == player);
    ut::vm::stateFrame(bot).latentAction = 0;
    {
        ut::vm::Call call(bot, ut::Name("StrafeFacing"));
        call.v(ut::actor::Location(bot)).obj(nullptr).f(1.0f).run();
    }
    CHECK(ut::vm::stateFrame(bot).latentAction == 0);
}

TEST_CASE("WaitForLanding waits 2.5 seconds before LongFall", "[data]")
{
    Deck deck;
    ut::UObject* bot = walker(deck);
    ut::actor::Physics(bot) = ut::PHYS_Falling;
    ut::vm::Call call(bot, ut::Name("WaitForLanding"));
    call.run();
    CHECK(ut::actor::LatentFloat(bot) == 2.5f);
    CHECK(ut::vm::stateFrame(bot).latentAction == 528);
}

TEST_CASE("ClearPaths resets every navigation point and FindRandomDest returns None", "[data]")
{
    Deck deck;
    ut::UObject* bot = walker(deck);
    ut::UObject* node = deck.actor("PathNode0");
    REQUIRE(node != nullptr);
    ut::navigationpoint::visitedWeight(node) = 5;
    ut::navigationpoint::bEndPoint(node) = 1;
    ut::navigationpoint::cost(node) = 77;
    ut::navigationpoint::ExtraCost(node) = 12;
    {
        ut::vm::Call call(bot, ut::Name("ClearPaths"));
        call.run();
    }
    CHECK(ut::navigationpoint::visitedWeight(node) == 10000000);
    CHECK(ut::navigationpoint::bEndPoint(node) == 0);
    CHECK(ut::navigationpoint::cost(node) == 12);
    ut::navigationpoint::visitedWeight(node) = 5;
    ut::vm::Call call(bot, ut::Name("FindRandomDest"));
    call.b(true).run();
    CHECK(objectResult(call) == nullptr);
    CHECK(ut::navigationpoint::visitedWeight(node) == 10000000);
}

TEST_CASE("FindPathToward leaves RouteCache alone, clears SpecialPause and never returns the goal pawn", "[data]")
{
    Deck deck;
    ut::UObject* bot = walker(deck);
    ut::UObject* player = deck.world->player();
    ut::UObject* marker = deck.actor("PathNode0");
    ut::pawn::RouteCache(bot, 0) = marker;
    ut::pawn::SpecialPause(bot) = 3.0f;
    ut::actor::Location(player) = ut::actor::Location(bot) + ut::Vector { 120, 0, 0 };
    ut::vm::Call call(bot, ut::Name("FindPathToward"));
    call.obj(player).b(false).b(true).run();
    CHECK(objectResult(call) != player);
    CHECK(ut::pawn::RouteCache(bot, 0) == marker);
    CHECK(ut::pawn::SpecialPause(bot) == 0.0f);
}

TEST_CASE("EAdjustJump simulates the arc in 0.05 second steps from the current velocity", "[data]")
{
    Deck deck;
    ut::UObject* bot = walker(deck);
    ut::UObject* zone = ut::zoneOf(bot);
    REQUIRE(zone != nullptr);
    ut::zoneinfo::ZoneGravity(zone) = { 0, 0, -950 };
    ut::actor::Velocity(bot) = { 0, 0, 500 };
    ut::pawn::JumpZ(bot) = 400.0f;
    ut::pawn::GroundSpeed(bot) = 2000.0f;
    ut::pawn::Destination(bot) = ut::actor::Location(bot) + ut::Vector { 300, 0, 0 };
    ut::vm::Call call(bot, ut::Name("EAdjustJump"));
    call.run();
    ut::Vector velocity = *reinterpret_cast<ut::Vector*>(call.returnValue());
    CHECK(velocity.z == 500.0f);
    CHECK(velocity.x == Catch::Approx(300.0 / (1.05 - 23.625 / 497.5)).margin(0.05));
    CHECK(velocity.y == Catch::Approx(0.0).margin(1e-3));
}

TEST_CASE("FindStairRotation eases the view pitch toward level on flat ground", "[data]")
{
    Deck deck;
    ut::UObject* player = deck.world->player();
    ut::pawn::ViewRotation(player) = { 2000, 0, 0 };
    auto stair = [&](float delta) {
        ut::vm::Call call(player, ut::Name("FindStairRotation"));
        call.f(delta).run();
        return *reinterpret_cast<int32_t*>(call.returnValue());
    };
    CHECK(stair(0.1f) == 399);
    CHECK(stair(0.5f) == 2000);
    ut::pawn::ViewRotation(player) = { 60000, 0, 0 };
    stair(0.1f);
    CHECK(ut::pawn::ViewRotation(player).pitch == -5536);
}

namespace {
    bool pickWallAdjust(ut::UObject* self)
    {
        ut::vm::Call call(self, ut::Name("PickWallAdjust"));
        call.run();
        return *reinterpret_cast<uint32_t*>(call.returnValue()) != 0;
    }

    ut::UObject* standing(ut::World& world, ut::UObject* pawn, const ut::Vector& floor)
    {
        REQUIRE(world.farMoveActor(pawn, floor + ut::Vector { 0, 0, ut::actor::CollisionHeight(pawn) + 1.0f }, false, true));
        ut::setPhysics(pawn, ut::PHYS_Walking, nullptr);
        ut::actor::Velocity(pawn) = {};
        ut::actor::Acceleration(pawn) = {};
        return pawn;
    }
}

TEST_CASE("PickWallAdjust steps a walking monster aside by its radius plus 16 units", "[data]")
{
    Deck deck;
    ut::World& world = *deck.world;
    ut::Vector floor = testworld::openSpot(world, 300.0f);
    ut::UObject* nali = standing(world, testworld::spawnNali(world, floor + ut::Vector { 0, 0, 60 }), floor);
    REQUIRE_FALSE(ut::pawn::bIsPlayer(nali));
    ut::Vector location = ut::actor::Location(nali);
    ut::pawn::Destination(nali) = location + ut::Vector { 250, 0, 0 };
    CHECK(pickWallAdjust(nali));
    ut::Vector expected = location + ut::Vector { 0, -1, 0 } * (ut::actor::CollisionRadius(nali) + 16.0f);
    CHECK((ut::pawn::Destination(nali) - expected).size() < 0.01f);
}

TEST_CASE("PickWallAdjust makes a walking player that can jump hop toward its destination", "[data]")
{
    Deck deck;
    ut::World& world = *deck.world;
    ut::Vector floor = testworld::openSpot(world, 300.0f);
    ut::UObject* bot = standing(world, walker(deck), floor);
    REQUIRE(ut::pawn::bIsPlayer(bot));
    REQUIRE(ut::pawn::bCanJump(bot));
    ut::pawn::Destination(bot) = ut::actor::Location(bot) + ut::Vector { 250, 0, 0 };
    CHECK(pickWallAdjust(bot));
    CHECK(ut::actor::Physics(bot) == ut::PHYS_Falling);
    CHECK(ut::actor::Velocity(bot).x == Catch::Approx(ut::pawn::GroundSpeed(bot)));
    CHECK(ut::actor::Velocity(bot).y == Catch::Approx(0.0f).margin(1e-4));
    CHECK(ut::actor::Velocity(bot).z == ut::pawn::JumpZ(bot));
}

TEST_CASE("PickWallAdjust gives up while falling or when the destination is already within reach", "[data]")
{
    Deck deck;
    ut::World& world = *deck.world;
    ut::Vector floor = testworld::openSpot(world, 300.0f);
    ut::UObject* nali = standing(world, testworld::spawnNali(world, floor + ut::Vector { 0, 0, 60 }), floor);
    ut::Vector location = ut::actor::Location(nali);
    ut::Vector near = location + ut::Vector { ut::actor::CollisionRadius(nali) * 0.5f, 0, 0 };
    ut::pawn::Destination(nali) = near;
    CHECK_FALSE(pickWallAdjust(nali));
    CHECK(ut::pawn::Destination(nali) == near);
    ut::Vector far = location + ut::Vector { 250, 0, 0 };
    ut::pawn::Destination(nali) = far;
    ut::setPhysics(nali, ut::PHYS_Falling, nullptr);
    CHECK_FALSE(pickWallAdjust(nali));
    CHECK(ut::pawn::Destination(nali) == far);
}

TEST_CASE("PickWallAdjust moves a flying pawn hovering just above its destination up by its height", "[data]")
{
    Deck deck;
    ut::World& world = *deck.world;
    ut::Vector floor = testworld::openSpot(world, 300.0f);
    ut::UObject* nali = standing(world, testworld::spawnNali(world, floor + ut::Vector { 0, 0, 60 }), floor);
    REQUIRE(world.farMoveActor(nali, ut::actor::Location(nali) + ut::Vector { 0, 0, 40 }, false, true));
    ut::setPhysics(nali, ut::PHYS_Flying, nullptr);
    ut::Vector location = ut::actor::Location(nali);
    float height = ut::actor::CollisionHeight(nali);
    ut::pawn::Destination(nali) = location + ut::Vector { ut::actor::CollisionRadius(nali) + 10.0f, 0, height * -0.5f };
    CHECK(pickWallAdjust(nali));
    CHECK((ut::pawn::Destination(nali) - (location + ut::Vector { 0, 0, height })).size() < 0.01f);
}

TEST_CASE("StrafeTo walks toward the destination while turning to face the focus", "[data]")
{
    Deck deck;
    ut::World& world = *deck.world;
    ut::Vector floor = testworld::openSpot(world, 300.0f);
    ut::UObject* bot = standing(world, walker(deck), floor);
    ut::Vector start = ut::actor::Location(bot);
    ut::Vector destination = start + ut::Vector { 200, 0, 0 };
    ut::Vector focus = start + ut::Vector { 0, 500, 0 };
    ut::vm::Call call(bot, ut::Name("StrafeTo"));
    call.v(destination).v(focus).run();
    CHECK(ut::vm::stateFrame(bot).latentAction == 505);
    CHECK(ut::pawn::Destination(bot) == destination);
    CHECK(ut::pawn::Focus(bot) == focus);
    CHECK(ut::pawn::MoveTarget(bot) == nullptr);
    CHECK(ut::actor::Acceleration(bot).x > 0);
    CHECK(ut::actor::Acceleration(bot).y == Catch::Approx(0.0f).margin(1e-3));
    CHECK((ut::actor::DesiredRotation(bot).yaw & 65535) == 16384);
    for (int i = 0; i < 80 && ut::vm::stateFrame(bot).latentAction == 505; i++) {
        world.tick(0.05f);
    }
    CHECK(ut::vm::stateFrame(bot).latentAction != 505);
    CHECK((ut::actor::Location(bot) - destination).size2D() < 20.0f);
    CHECK((ut::actor::Rotation(bot).yaw & 65535) == Catch::Approx(16384).margin(1024));
}
