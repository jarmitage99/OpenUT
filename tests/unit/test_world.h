#pragma once

#include "core/objects.h"
#include "engine/actor.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <optional>
#include <vector>
#include <utility>

namespace testworld {
    struct Deck {
        ut::World world;

        Deck()
        {
            ut::engine::initialize(OPENUT_GAME_DATA);
            ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
            REQUIRE(world.loadMap("DM-Deck16]["));
        }

        ~Deck()
        {
            ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
        }
    };

    inline std::optional<float> floorBelow(ut::World& world, const ut::Vector& point, float depth)
    {
        ut::CheckResult hit;
        if (!world.bspLineCheck(hit, point, point - ut::Vector { 0, 0, depth }, {}) || hit.normal.z < 0.99f) {
            return std::nullopt;
        }
        return hit.location.z;
    }

    inline bool freeOfActors(ut::World& world, const ut::Vector& start, const ut::Vector& end)
    {
        ut::CheckResult hit;
        world.lineCheck(hit, start, end, { 40, 40, 40 }, nullptr, ut::TRACE_Others | ut::TRACE_Pawns);
        return hit.actor == nullptr;
    }

    inline ut::Vector openSpot(ut::World& world, float reach)
    {
        for (ut::UObject* a : world.actors) {
            if (!a || a->destroyed || !a->isA(ut::Name("PathNode"))) {
                continue;
            }
            ut::Vector point = ut::actor::Location(a) + ut::Vector { 0, 0, 64 };
            std::optional<float> floor = floorBelow(world, point, 200.0f);
            if (!floor) {
                continue;
            }
            bool clear = world.fastLineCheck(point + ut::Vector { 0, 0, reach }, point);
            for (const ut::Vector& side : { ut::Vector { reach, 0, 0 }, ut::Vector { -reach, 0, 0 }, ut::Vector { 0, reach, 0 }, ut::Vector { 0, -reach, 0 } }) {
                clear = clear && world.fastLineCheck(point + side, point) && floorBelow(world, point + side, 200.0f) == floor && freeOfActors(world, point, point + side);
            }
            if (clear) {
                return { point.x, point.y, *floor };
            }
        }
        FAIL("no open spot with flat floor in reach " << reach);
        return {};
    }

    inline std::pair<ut::Vector, ut::Vector> clearLine(ut::World& world, float length)
    {
        for (ut::UObject* a : world.actors) {
            if (!a || a->destroyed || !a->isA(ut::Name("PathNode"))) {
                continue;
            }
            ut::Vector start = ut::actor::Location(a) + ut::Vector { 0, 0, 40 };
            for (const ut::Vector& direction : { ut::Vector { 1, 0, 0 }, ut::Vector { -1, 0, 0 }, ut::Vector { 0, 1, 0 }, ut::Vector { 0, -1, 0 } }) {
                ut::Vector end = start + direction * length;
                if (world.fastLineCheck(end, start) && freeOfActors(world, start, end)) {
                    return { start, direction };
                }
            }
        }
        FAIL("no clear line of length " << length);
        return {};
    }

    inline ut::UObject* spawnClass(ut::World& world, const char* path, const ut::Vector& location)
    {
        ut::UClass* cls = ut::objects::findClass(path);
        ut::objects::flushLoads();
        REQUIRE(cls != nullptr);
        ut::UObject* spawned = world.spawn(cls, nullptr, ut::Name(), location, {}, nullptr, true);
        REQUIRE(spawned != nullptr);
        ut::actor::bStasis(spawned) = 0;
        return spawned;
    }

    inline ut::UObject* spawnBox(ut::World& world, const ut::Vector& location)
    {
        return spawnClass(world, "UnrealShare.WoodenBox", location);
    }

    inline ut::UObject* spawnNali(ut::World& world, const ut::Vector& location)
    {
        ut::UObject* game = ut::levelinfo::Game(world.levelInfo);
        REQUIRE(game != nullptr);
        ut::gameinfo::bNoMonsters(game) = 0;
        return spawnClass(world, "UnrealShare.Nali", location);
    }

    inline ut::UObject* firstOfClass(ut::World& world, const char* className)
    {
        for (ut::UObject* a : world.actors) {
            if (a && !a->destroyed && a->isA(ut::Name(className))) {
                return a;
            }
        }
        return nullptr;
    }

    inline std::vector<ut::UTexture*> packageTextures(const char* package, const char* className = "Texture")
    {
        ut::engine::initialize(OPENUT_GAME_DATA);
        ut::Linker* linker = ut::objects::loadPackage(ut::Name(package));
        REQUIRE(linker != nullptr);
        std::vector<ut::UTexture*> textures;
        for (size_t i = 0; i < linker->file().exports().size(); i++) {
            int32_t ref = static_cast<int32_t>(i + 1);
            if (linker->file().classOf(ref) != className) {
                continue;
            }
            ut::UTexture* texture = ut::cast<ut::UTexture>(linker->object(ref));
            ut::objects::ensureLoaded(texture);
            textures.push_back(texture);
        }
        ut::objects::flushLoads();
        return textures;
    }

    inline float dilation(ut::World& world)
    {
        return ut::levelinfo::TimeDilation(world.levelInfo);
    }
}
