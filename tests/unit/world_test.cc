#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/ai.h"
#include "engine/console.h"
#include "engine/props_engine.h"
#include "engine/world.h"
#include "test_world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <vector>

using namespace testworld;

namespace {
    size_t indexOf(ut::World& world, ut::UObject* actor)
    {
        return static_cast<size_t>(std::find(world.actors.begin(), world.actors.end(), actor) - world.actors.begin());
    }

    ut::World* hookWorld = nullptr;
    ut::UObject* hookActor = nullptr;
    ut::NativeFunction hookOriginal = nullptr;
    int hookCalls = 0;
    bool hookSawActorInHash = false;

    void collisionHashProbe(ut::UObject* self, ut::Frame& stack, void* result)
    {
        if (self == hookActor) {
            hookCalls++;
            ut::Vector location = ut::actor::Location(hookActor);
            ut::Vector extent = ut::cylinderExtent(hookActor);
            const std::vector<ut::UObject*>& found = hookWorld->collisionHash.query(location - extent, location + extent);
            hookSawActorInHash = std::find(found.begin(), found.end(), hookActor) != found.end();
        }
        hookOriginal(self, stack, result);
    }
}

TEST_CASE("bots acquire enemies and fight once the match starts", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    ut::World world;
    REQUIRE(world.loadMap("DM-Deck16][?game=Botpack.DeathMatchPlus"));
    ut::UObject* player = world.player();
    REQUIRE(player != nullptr);
    world.tick(1.0f / 30.0f);
    ut::vm::Call(player, ut::Name("Fire")).f(0.0f).run();
    bool acquired = false;
    bool fought = false;
    for (int i = 0; i < 30 * 180 && !(acquired && fought); i++) {
        world.tick(1.0f / 30.0f);
        for (ut::UObject* a : world.actors) {
            if (a && !a->destroyed && a->isA(ut::Name("Bot"))) {
                acquired = acquired || ut::pawn::Enemy(a) != nullptr;
                fought = fought || ut::pawn::Health(a) < ut::pawn::Health(a->cls->defaultObject) || ut::playerreplicationinfo::Score(ut::pawn::PlayerReplicationInfo(a)) > 0;
            }
        }
    }
    CHECK(firstOfClass(world, "Bot") != nullptr);
    CHECK(acquired);
    CHECK(fought);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
}

TEST_CASE("frames shorter than five milliseconds advance actors in real time", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    ut::Vector origin = openSpot(world, 300.0f);
    auto travelled = [&](float height, float step, int steps) {
        ut::UObject* box = spawnBox(world, origin + ut::Vector { -100, 0, height });
        ut::actor::bCollideWorld(box) = 0;
        ut::actor::bCollideActors(box) = 0;
        ut::actor::Physics(box) = ut::PHYS_Projectile;
        ut::actor::Velocity(box) = { 100, 0, 0 };
        ut::Vector start = ut::actor::Location(box);
        float before = world.timeSeconds;
        for (int i = 0; i < steps; i++) {
            world.tick(step);
        }
        CHECK(world.timeSeconds - before == Catch::Approx(dilation(world)).epsilon(0.01));
        return (ut::actor::Location(box) - start).size();
    };
    CHECK(travelled(100.0f, 0.01f, 100) == Catch::Approx(100.0f * dilation(world)).epsilon(0.02));
    CHECK(travelled(200.0f, 0.001f, 1000) == Catch::Approx(100.0f * dilation(world)).epsilon(0.02));
}

TEST_CASE("box traces against level geometry stop short by a tenth of their length", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    ut::Vector spot = openSpot(world, 100.0f);
    ut::CheckResult hit;
    ut::Vector probe = spot + ut::Vector { 0, 0, 100 };
    REQUIRE(world.bspLineCheck(hit, probe, probe - ut::Vector { 0, 0, 200 }, {}));
    float floor = hit.location.z;
    ut::Vector start = { spot.x, spot.y, floor + 47.25f };
    REQUIRE(world.bspLineCheck(hit, start, start - ut::Vector { 0, 0, 27 }, { 20, 20, 43 }));
    CHECK(hit.time == Catch::Approx(4.75f / 27.0f - 0.1f).margin(0.0005f));
}

TEST_CASE("a non-player pawn sees a near pawn and loses it beyond 4000 units unless distance is ignored", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("CTF-Face"));
    ut::World world;
    REQUIRE(world.loadMap("CTF-Face"));
    ut::UObject* player = world.player();
    REQUIRE(player != nullptr);
    auto [start, direction] = clearLine(world, 4600.0f);
    ut::UObject* nali = spawnNali(world, start);
    REQUIRE(ut::pawn::bIsPlayer(nali) == 0);
    ut::pawn::Enemy(nali) = nullptr;
    ut::pawn::Visibility(player) = 128;
    ut::actor::Location(nali) = start;
    ut::actor::Location(player) = start + direction * 900.0f;
    CHECK(ut::lineOfSightTo(nali, player, false, false));
    ut::actor::Location(player) = start + direction * 4500.0f;
    CHECK_FALSE(ut::lineOfSightTo(nali, player, false, false));
    CHECK(ut::lineOfSightTo(nali, player, false, true));
    ut::objects::unloadPackage(ut::Name("CTF-Face"));
}

TEST_CASE("switching to walking physics onto a floor actor bases the actor on it", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    ut::Vector spot = openSpot(world, 200.0f);
    ut::UObject* floor = spawnBox(world, spot + ut::Vector { 0, 0, 30 });
    ut::UObject* box = spawnBox(world, spot + ut::Vector { 0, 0, 120 });
    ut::actor::Physics(box) = ut::PHYS_Falling;
    ut::setPhysics(box, ut::PHYS_Walking, floor);
    CHECK(ut::actor::Physics(box) == ut::PHYS_Walking);
    CHECK(ut::actor::Base(box) == floor);
    ut::setPhysics(box, ut::PHYS_Falling, nullptr);
    CHECK(ut::actor::Base(box) == nullptr);
}

TEST_CASE("switching to rotating physics clears velocity and acceleration", "[data]")
{
    Deck deck;
    ut::UObject* box = spawnBox(deck.world, openSpot(deck.world, 200.0f) + ut::Vector { 0, 0, 150 });
    ut::actor::Physics(box) = ut::PHYS_Falling;
    ut::actor::Velocity(box) = { 10, 20, -30 };
    ut::actor::Acceleration(box) = { 1, 2, 3 };
    ut::setPhysics(box, ut::PHYS_Rotating, nullptr);
    CHECK(ut::actor::Velocity(box).zero());
    CHECK(ut::actor::Acceleration(box).zero());
}

TEST_CASE("a full touch list drops a non-player pawn to make room for a player", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    ut::UObject* player = world.player();
    ut::UObject* node = firstOfClass(world, "PathNode");
    REQUIRE(node != nullptr);
    ut::Vector spot = openSpot(world, 300.0f);
    std::vector<ut::UObject*> pawns;
    for (int i = 0; i < 4; i++) {
        pawns.push_back(spawnNali(world, spot + ut::Vector { -150.0f + 100.0f * static_cast<float>(i), 0, 60 }));
        ut::actor::Physics(pawns.back()) = ut::PHYS_Walking;
        ut::actor::Touching(node, i) = pawns.back();
    }
    ut::beginTouch(node, player);
    CHECK(ut::actor::Touching(node, 0) == player);
    CHECK(ut::actor::Touching(node, 1) == pawns[1]);
    CHECK(ut::actor::Touching(node, 2) == pawns[2]);
    CHECK(ut::actor::Touching(node, 3) == pawns[3]);
}

TEST_CASE("a destroyed actor leaves the collision hash before its Destroyed event runs", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    world.tick(1.0f / 30.0f);
    ut::UObject* box = spawnBox(world, openSpot(world, 200.0f) + ut::Vector { 0, 0, 30 });
    REQUIRE(ut::actor::bCollideActors(box));
    *reinterpret_cast<ut::UObject**>(box->propertyAddress(ut::Name("Contents"))) = ut::objects::findClass("UnrealShare.WoodenBox");
    ut::UFunction* spawn = ut::vm::findFunction(box, ut::Name("Spawn"));
    REQUIRE(spawn != nullptr);
    REQUIRE(spawn->nativeIndex == 278);
    hookWorld = &world;
    hookActor = box;
    hookOriginal = ut::vm::bindNative(spawn);
    REQUIRE(hookOriginal != nullptr);
    hookCalls = 0;
    hookSawActorInHash = true;
    ut::vm::registerNative(278, collisionHashProbe);
    world.destroy(box);
    ut::vm::registerNative(278, hookOriginal);
    CHECK(hookCalls == 1);
    CHECK_FALSE(hookSawActorInHash);
}

TEST_CASE("an actor whose owner ticks later waits until the owner has ticked", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    ut::UObject* player = world.player();
    ut::UObject* pickup = firstOfClass(world, "Inventory");
    REQUIRE(pickup != nullptr);
    world.tick(1.0f / 30.0f);
    ut::UObject* box = spawnBox(world, openSpot(world, 200.0f) + ut::Vector { 0, 0, 30 });
    REQUIRE(indexOf(world, pickup) < indexOf(world, player));
    REQUIRE(indexOf(world, player) < indexOf(world, box));
    ut::actor::Owner(pickup) = box;
    bool sampled = false;
    bool ownerTickedBeforePlayer = true;
    bool childTickedBeforePlayer = true;
    world.inputHook = [&](ut::UObject*, float delta) {
        if (delta > 0 && !sampled) {
            sampled = true;
            ownerTickedBeforePlayer = ut::actor::bTicked(box) == world.tickParity;
            childTickedBeforePlayer = ut::actor::bTicked(pickup) == world.tickParity;
        }
    };
    world.tick(1.0f / 30.0f);
    world.inputHook = nullptr;
    REQUIRE(sampled);
    CHECK_FALSE(ownerTickedBeforePlayer);
    CHECK_FALSE(childTickedBeforePlayer);
    CHECK(ut::actor::bTicked(pickup) == ut::actor::bTicked(player));
    CHECK(ut::actor::bTicked(box) == ut::actor::bTicked(player));
    ut::actor::Owner(pickup) = nullptr;
}

TEST_CASE("actors spawned between frames are ticked on the next frame", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    ut::Vector origin = openSpot(world, 250.0f);
    for (int frame = 0; frame < 2; frame++) {
        ut::UObject* box = spawnBox(world, origin + ut::Vector { -100.0f + 200.0f * static_cast<float>(frame), 0, 30 });
        ut::actor::LifeSpan(box) = 10.0f;
        world.tick(0.25f);
        INFO(frame);
        CHECK(ut::actor::LifeSpan(box) == Catch::Approx(10.0f - 0.25f * dilation(world)));
    }
}

TEST_CASE("actors without physics do not rotate", "[data]")
{
    Deck deck;
    ut::UObject* box = spawnBox(deck.world, openSpot(deck.world, 200.0f) + ut::Vector { 0, 0, 30 });
    ut::actor::Physics(box) = ut::PHYS_None;
    ut::actor::Rotation(box) = {};
    ut::actor::bFixedRotationDir(box) = 1;
    ut::actor::bRotateToDesired(box) = 0;
    ut::actor::RotationRate(box) = { 0, 6000, 0 };
    deck.world.tick(0.1f);
    CHECK(ut::actor::Rotation(box).yaw == 0);
}

TEST_CASE("players-only mode stops non-player actors after their animation update", "[data]")
{
    Deck deck;
    ut::UObject* box = spawnBox(deck.world, openSpot(deck.world, 200.0f) + ut::Vector { 0, 0, 30 });
    ut::actor::LifeSpan(box) = 10.0f;
    ut::levelinfo::bPlayersOnly(deck.world.levelInfo) = 1;
    deck.world.tick(0.25f);
    CHECK(ut::actor::LifeSpan(box) == 10.0f);
    ut::levelinfo::bPlayersOnly(deck.world.levelInfo) = 0;
    deck.world.tick(0.25f);
    CHECK(ut::actor::LifeSpan(box) == Catch::Approx(10.0f - 0.25f * dilation(deck.world)));
}

TEST_CASE("level startup moves static actors ahead of dynamic ones and never ticks them", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    REQUIRE(world.firstDynamicActor > 2);
    REQUIRE(world.firstDynamicActor < world.actors.size());
    for (size_t i = 2; i < world.actors.size(); i++) {
        INFO(i);
        if (!world.actors[i]) {
            continue;
        }
        CHECK((ut::actor::bStatic(world.actors[i]) != 0) == (i < world.firstDynamicActor));
    }
    ut::UObject* staticActor = world.actors[2];
    ut::actor::LifeSpan(staticActor) = 10.0f;
    world.tick(0.25f);
    CHECK(ut::actor::LifeSpan(staticActor) == 10.0f);
}

TEST_CASE("a rolling actor on flat ground loses speed to ground and fluid friction", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    ut::UObject* box = spawnBox(world, openSpot(world, 300.0f) + ut::Vector { -100, 0, 30 });
    for (int i = 0; i < 60 && ut::actor::Physics(box) != ut::PHYS_None; i++) {
        world.tick(1.0f / 30.0f);
    }
    REQUIRE(ut::actor::Physics(box) == ut::PHYS_None);
    ut::UObject* zone = ut::region(box).zone;
    REQUIRE(zone != nullptr);
    REQUIRE(ut::zoneinfo::ZoneGroundFriction(zone) == 8.0f);
    REQUIRE(ut::zoneinfo::ZoneFluidFriction(zone) == 1.2f);
    REQUIRE(ut::zoneinfo::ZoneVelocity(zone).zero());
    ut::setPhysics(box, ut::PHYS_Rolling, nullptr);
    ut::actor::Velocity(box) = { 100, 0, 0 };
    ut::actor::Acceleration(box) = {};
    float startX = ut::actor::Location(box).x;
    ut::performPhysics(box, 0.05f);
    CHECK(ut::actor::Physics(box) == ut::PHYS_Rolling);
    CHECK(ut::actor::Location(box).x - startX == Catch::Approx(2.82f).margin(0.001f));
    CHECK(ut::actor::Velocity(box).x == Catch::Approx(56.4f).margin(0.02f));
}

TEST_CASE("script spawns give the new actor the spawner's instigator", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    ut::Vector spot = openSpot(world, 250.0f);
    ut::UObject* spawner = spawnNali(world, spot + ut::Vector { -100, 0, 60 });
    ut::UObject* instigator = spawnNali(world, spot + ut::Vector { 100, 0, 60 });
    ut::actor::Instigator(spawner) = instigator;
    ut::UClass* cls = ut::objects::findClass("UnrealShare.WoodenBox");
    ut::vm::Call spawn(spawner, ut::Name("Spawn"));
    spawn.obj(cls).obj(spawner);
    spawn.run();
    ut::UObject* box = *reinterpret_cast<ut::UObject**>(spawn.returnValue());
    REQUIRE(box != nullptr);
    CHECK(ut::actor::Owner(box) == spawner);
    CHECK(ut::actor::Instigator(box) == instigator);
}

TEST_CASE("traces ignore every actor up the source's owner chain but not its children", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    ut::Vector center = openSpot(world, 400.0f) + ut::Vector { 0, 0, 60 };
    ut::Vector origin = center + ut::Vector { -200, 0, 0 };
    ut::UObject* source = spawnBox(world, origin);
    ut::UObject* middle = spawnBox(world, center + ut::Vector { 0, 250, 0 });
    ut::UObject* top = spawnBox(world, center);
    ut::actor::Owner(source) = middle;
    ut::actor::Owner(middle) = top;
    ut::CheckResult hit;
    world.lineCheck(hit, origin, origin + ut::Vector { 400, 0, 0 }, {}, source, ut::TRACE_Others);
    CHECK(hit.actor == nullptr);
    world.lineCheck(hit, ut::actor::Location(top), ut::actor::Location(middle), {}, top, ut::TRACE_Others);
    CHECK(hit.actor == middle);
}

TEST_CASE("an actor's owner still blocks its moves", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    ut::Vector origin = openSpot(world, 300.0f) + ut::Vector { -100, 0, 60 };
    ut::UObject* mover = spawnBox(world, origin);
    ut::UObject* owner = spawnBox(world, origin + ut::Vector { 100, 0, 0 });
    REQUIRE(ut::isBlockedBy(mover, owner));
    ut::actor::Owner(mover) = owner;
    ut::CheckResult hit;
    world.moveActor(mover, { 100, 0, 0 }, ut::actor::Rotation(mover), hit, false, false, false, false);
    CHECK(hit.actor == owner);
    CHECK(ut::actor::Location(mover).x < origin.x + 100.0f - ut::actor::CollisionRadius(owner));
}

TEST_CASE("test moves update the actor's zone without zone events", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    ut::Vector back = openSpot(world, 200.0f) + ut::Vector { 0, 0, 60 };
    ut::UObject* box = spawnBox(world, back);
    ut::UObject* startZone = ut::region(box).zone;
    ut::UObject* otherZone = nullptr;
    ut::Vector otherLocation;
    for (ut::UObject* a : world.actors) {
        if (a && a->isA(ut::engineClasses().zoneInfo) && a != startZone && a != world.levelInfo && world.zoneAt(ut::actor::Location(a), nullptr, nullptr) == a) {
            otherZone = a;
            otherLocation = ut::actor::Location(a);
            break;
        }
    }
    REQUIRE(otherZone != nullptr);
    REQUIRE(world.farMoveActor(box, otherLocation, true, true));
    CHECK(ut::region(box).zone == otherZone);
    REQUIRE(world.farMoveActor(box, back, false, true));
    REQUIRE(ut::region(box).zone == startZone);
    ut::CheckResult hit;
    world.moveActor(box, otherLocation - back, ut::actor::Rotation(box), hit, true, false, false, true);
    CHECK(ut::region(box).zone == world.zoneAt(ut::actor::Location(box), nullptr, nullptr));
    CHECK(ut::region(box).zone != startZone);
}

TEST_CASE("paused frames zero the player's input axes and tick always-tick actors", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    ut::UObject* player = world.player();
    ut::Vector spot = openSpot(world, 250.0f);
    ut::UObject* box = spawnBox(world, spot + ut::Vector { -100, 0, 30 });
    ut::actor::bAlwaysTick(box) = 1;
    ut::actor::LifeSpan(box) = 10.0f;
    ut::UObject* other = spawnBox(world, spot + ut::Vector { 100, 0, 30 });
    ut::actor::LifeSpan(other) = 10.0f;
    ut::levelinfo::Pauser(world.levelInfo) = "test";
    ut::playerpawn::aBaseY(player) = 5.0f;
    world.tick(0.25f);
    CHECK(world.paused);
    CHECK(ut::playerpawn::aBaseY(player) == 0.0f);
    CHECK(ut::actor::LifeSpan(box) == Catch::Approx(10.0f - 0.25f * dilation(world)));
    CHECK(ut::actor::LifeSpan(other) == 10.0f);
}

TEST_CASE("a spider pawn with no surface around it falls", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    ut::UObject* nali = spawnNali(world, openSpot(world, 300.0f) + ut::Vector { 0, 0, 200 });
    ut::actor::Physics(nali) = ut::PHYS_Spider;
    ut::pawn::Floor(nali) = {};
    ut::actor::Velocity(nali) = { 10, 0, 0 };
    ut::performPhysics(nali, 0.05f);
    CHECK(ut::actor::Physics(nali) == ut::PHYS_Falling);
}

TEST_CASE("physics hands the pending touch actor PostTouch and pops it off the chain", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    ut::Vector origin = openSpot(world, 300.0f) + ut::Vector { -100, 0, 60 };
    ut::UObject* a = spawnBox(world, origin);
    ut::UObject* b = spawnBox(world, origin + ut::Vector { 100, 0, 0 });
    ut::UObject* c = spawnBox(world, origin + ut::Vector { 200, 0, 0 });
    ut::actor::Physics(a) = ut::PHYS_None;
    ut::actor::PendingTouch(a) = b;
    ut::actor::PendingTouch(b) = c;
    ut::performPhysics(a, 0.05f);
    CHECK(ut::actor::PendingTouch(a) == c);
    CHECK(ut::actor::PendingTouch(b) == nullptr);
}

TEST_CASE("pawns level their pitch while walking, turn it at a fixed rate otherwise, and never roll", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    ut::UObject* nali = spawnNali(world, openSpot(world, 200.0f) + ut::Vector { 0, 0, 60 });
    ut::actor::RotationRate(nali).pitch = 32768;
    ut::actor::Physics(nali) = ut::PHYS_Walking;
    ut::actor::Velocity(nali) = {};
    ut::actor::Acceleration(nali) = {};
    ut::Rotator start = ut::actor::Rotation(nali);
    start.pitch = 0;
    start.roll = 1000;
    ut::actor::Rotation(nali) = start;
    ut::actor::DesiredRotation(nali) = { 4000, start.yaw, 0 };
    ut::performPhysics(nali, 0.05f);
    CHECK(ut::actor::Rotation(nali).pitch == 0);
    CHECK(ut::actor::Rotation(nali).roll == 0);
    ut::actor::Physics(nali) = ut::PHYS_Flying;
    ut::performPhysics(nali, 0.05f);
    CHECK(ut::actor::Rotation(nali).pitch == 1638);
}

TEST_CASE("the player's input is read again with -1 after PlayerTick, which zeroes the axes", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    ut::UObject* player = world.player();
    std::vector<float> deltas;
    world.inputHook = [&](ut::UObject*, float delta) {
        deltas.push_back(delta);
    };
    world.tick(0.25f);
    world.inputHook = nullptr;
    REQUIRE(deltas.size() == 2);
    CHECK(deltas[0] == Catch::Approx(0.25f * dilation(world)));
    CHECK(deltas[1] == -1.0f);
    ut::playerpawn::aBaseY(player) = 5.0f;
    ut::console::readInput(player, -1.0f);
    CHECK(ut::playerpawn::aBaseY(player) == 0.0f);
}

TEST_CASE("a player pawn without a player object is blocked like any other actor", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    ut::UObject* player = world.player();
    ut::UObject* box = spawnBox(world, openSpot(world, 200.0f) + ut::Vector { 0, 0, 30 });
    ut::actor::bBlockActors(box) = 1;
    ut::actor::bBlockPlayers(box) = 0;
    ut::actor::bBlockActors(player) = 1;
    ut::actor::bBlockPlayers(player) = 1;
    ut::UObject* possessing = ut::playerpawn::Player(player);
    ut::playerpawn::Player(player) = nullptr;
    CHECK(ut::isBlockedBy(player, box));
    ut::playerpawn::Player(player) = possessing ? possessing : box;
    CHECK_FALSE(ut::isBlockedBy(player, box));
    ut::playerpawn::Player(player) = possessing;
}

TEST_CASE("objects created after a cleanup lose their references to actors destroyed later", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    world.tick(1.0f / 30.0f);
    ut::UClass* listClass = ut::objects::findClass("UWindow.UWindowList");
    REQUIRE(listClass != nullptr);
    auto listHolding = [&](ut::UObject* target) {
        ut::UObject* list = ut::objects::newObject(listClass, ut::objects::packageObject(ut::Name("Transient")), ut::Name(), 0);
        ut::UObject** next = reinterpret_cast<ut::UObject**>(list->propertyAddress(ut::Name("Next")));
        REQUIRE(next != nullptr);
        *next = target;
        return next;
    };
    ut::UObject* first = spawnBox(world, openSpot(world, 200.0f) + ut::Vector { 0, 0, 30 });
    ut::UObject* second = spawnBox(world, openSpot(world, 300.0f) + ut::Vector { 0, 0, 30 });
    ut::UObject** early = listHolding(first);
    world.destroy(first);
    world.tick(1.0f / 30.0f);
    CHECK(*early == nullptr);
    ut::UObject** late = listHolding(second);
    world.tick(1.0f / 30.0f);
    CHECK(*late == second);
    world.destroy(second);
    world.tick(1.0f / 30.0f);
    CHECK(*late == nullptr);
}

TEST_CASE("a spawned actor enters the auto state its nearest ancestor declares", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    ut::UObject* medbox = spawnClass(world, "Botpack.MedBox", openSpot(world, 200.0f) + ut::Vector { 0, 0, 30 });
    REQUIRE(medbox != nullptr);
    ut::UState* state = ut::vm::stateFrame(medbox).state;
    REQUIRE(state != nullptr);
    CHECK(state->name == ut::Name("Pickup"));
    CHECK(state->outer == ut::objects::findClass("Botpack.TournamentHealth"));
}
