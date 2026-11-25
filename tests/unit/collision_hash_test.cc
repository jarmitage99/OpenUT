#include "core/objects.h"
#include "engine/actor.h"
#include "engine/console.h"
#include "engine/props_engine.h"
#include "engine/world.h"
#include "test_world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <memory>
#include <random>
#include <vector>

using namespace testworld;

namespace {
    std::vector<ut::UObject*> overlappingCylinders(ut::World& world, const ut::Vector& boxMin, const ut::Vector& boxMax)
    {
        std::vector<ut::UObject*> expected;
        for (ut::UObject* a : world.actors) {
            if (!a || a->destroyed || !ut::actor::bCollideActors(a) || ut::isBrush(a)) {
                continue;
            }
            ut::Vector location = ut::actor::Location(a);
            float radius = ut::actor::CollisionRadius(a);
            float height = ut::actor::CollisionHeight(a);
            if (location.x + radius < boxMin.x || location.x - radius > boxMax.x || location.y + radius < boxMin.y || location.y - radius > boxMax.y || location.z + height < boxMin.z || location.z - height > boxMax.z) {
                continue;
            }
            expected.push_back(a);
        }
        return expected;
    }

    std::vector<ut::UObject*> withoutBrushes(const std::vector<ut::UObject*>& candidates)
    {
        std::vector<ut::UObject*> result;
        for (ut::UObject* a : candidates) {
            if (!ut::isBrush(a)) {
                result.push_back(a);
            }
        }
        return result;
    }
}

TEST_CASE("collision hash returns exactly the overlapping cylinders in actor order", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    ut::Vector low { 1e9f, 1e9f, 1e9f };
    ut::Vector high { -1e9f, -1e9f, -1e9f };
    for (ut::UObject* a : world.actors) {
        if (a && !a->destroyed && ut::actor::bCollideActors(a) && !ut::isBrush(a)) {
            ut::Vector l = ut::actor::Location(a);
            low = { std::min(low.x, l.x), std::min(low.y, l.y), std::min(low.z, l.z) };
            high = { std::max(high.x, l.x), std::max(high.y, l.y), std::max(high.z, l.z) };
        }
    }
    std::mt19937 random(12345);
    std::uniform_real_distribution<float> coordinateX(low.x, high.x);
    std::uniform_real_distribution<float> coordinateY(low.y, high.y);
    std::uniform_real_distribution<float> height(low.z, high.z);
    std::uniform_real_distribution<float> size(1.0f, 2000.0f);
    size_t nonEmpty = 0;
    for (int i = 0; i < 2000; i++) {
        ut::Vector center { coordinateX(random), coordinateY(random), height(random) };
        ut::Vector half { size(random) * 0.5f, size(random) * 0.5f, size(random) * 0.25f };
        std::vector<ut::UObject*> expected = overlappingCylinders(world, center - half, center + half);
        std::vector<ut::UObject*> actual = withoutBrushes(world.collisionHash.query(center - half, center + half));
        INFO("query " << i);
        REQUIRE(actual == expected);
        nonEmpty += expected.empty() ? 0 : 1;
    }
    CHECK(nonEmpty > 200);

    ut::UObject* player = world.player();
    REQUIRE(player != nullptr);
    ut::actor::bCollideActors(player) = 1;
    world.collisionHash.update(player);
    ut::Vector original = ut::actor::Location(player);
    ut::Vector destination = openSpot(world, 200.0f) + ut::Vector { 0, 0, ut::actor::CollisionHeight(player) + 2 };
    REQUIRE((destination - original).size() > 100.0f);
    ut::console::execute(player, "teleport " + std::to_string(destination.x) + " " + std::to_string(destination.y) + " " + std::to_string(destination.z) + " 0");
    ut::Vector moved = ut::actor::Location(player);
    CHECK((moved - destination).size() < 8.0f);
    std::vector<ut::UObject*> near = world.collisionHash.query(moved - ut::Vector { 4, 4, 4 }, moved + ut::Vector { 4, 4, 4 });
    CHECK(std::find(near.begin(), near.end(), player) != near.end());
    std::vector<ut::UObject*> start = world.collisionHash.query(original - ut::Vector { 4, 4, 4 }, original + ut::Vector { 4, 4, 4 });
    CHECK(std::find(start.begin(), start.end(), player) == start.end());

    for (int i = 0; i < 300; i++) {
        world.tick(1.0f / 30.0f);
    }
    for (int i = 0; i < 500; i++) {
        ut::Vector center { coordinateX(random), coordinateY(random), height(random) };
        ut::Vector half { size(random) * 0.5f, size(random) * 0.5f, size(random) * 0.25f };
        std::vector<ut::UObject*> expected = overlappingCylinders(world, center - half, center + half);
        std::vector<ut::UObject*> actual = withoutBrushes(world.collisionHash.query(center - half, center + half));
        INFO("query after ticking " << i);
        REQUIRE(actual == expected);
    }
    ut::console::resetInput(player);
}

TEST_CASE("collision hash finds movers where traces hit them", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    const ut::Vector directions[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    size_t hits = 0;
    for (ut::UObject* a : world.actors) {
        if (!a || a->destroyed || !ut::isMover(a) || !ut::actor::bCollideActors(a)) {
            continue;
        }
        ut::Vector target = ut::actor::Location(a);
        for (const ut::Vector& direction : directions) {
            ut::CheckResult hit;
            if (!world.lineCheck(hit, target + direction * 400.0f, target, {}, nullptr, ut::TRACE_Movers) || hit.actor != a) {
                continue;
            }
            hits++;
            std::vector<ut::UObject*> found = world.collisionHash.query(hit.location - ut::Vector { 1, 1, 1 }, hit.location + ut::Vector { 1, 1, 1 });
            INFO(a->name.c_str());
            CHECK(std::find(found.begin(), found.end(), a) != found.end());
        }
    }
    CHECK(hits > 0);
}

TEST_CASE("live actor snapshot drops destroyed actors and picks up spawned ones", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    std::shared_ptr<const std::vector<ut::UObject*>> before = world.liveActors();
    ut::UObject* victim = firstOfClass(world, "Inventory");
    REQUIRE(victim != nullptr);
    REQUIRE(world.destroy(victim));
    std::shared_ptr<const std::vector<ut::UObject*>> after = world.liveActors();
    CHECK(after->size() < before->size());
    CHECK(std::find(after->begin(), after->end(), victim) == after->end());
    CHECK(std::find(before->begin(), before->end(), victim) != before->end());
    ut::UObject* spawned = spawnBox(world, openSpot(world, 200.0f) + ut::Vector { 0, 0, 30 });
    std::shared_ptr<const std::vector<ut::UObject*>> later = world.liveActors();
    CHECK(later->size() == after->size() + 1);
    CHECK(later->back() == spawned);
}
