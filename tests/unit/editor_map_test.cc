#include "core/objects.h"
#include "engine/actor.h"
#include "engine/map_writer.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {
    ut::ULevel* loadEditorLevel(ut::Linker* linker)
    {
        int32_t ref = linker->findExport(ut::Name("MyLevel"), ut::Name("Level"), "*");
        ut::ULevel* level = ut::cast<ut::ULevel>(linker->object(ref));
        ut::objects::ensureLoaded(level);
        ut::objects::flushLoads();
        for (ut::UObject* a : level->actors) {
            ut::objects::ensureLoaded(a);
        }
        ut::objects::flushLoads();
        ut::objects::ensureLoaded(level->model);
        ut::objects::flushLoads();
        return level;
    }

    ut::UObject* findActor(ut::ULevel* level, ut::Name name)
    {
        for (ut::UObject* a : level->actors) {
            if (a && a->name == name) {
                return a;
            }
        }
        return nullptr;
    }

    uint32_t exportFlags(ut::Linker* linker, ut::Name name)
    {
        int32_t ref = linker->findExport(name, ut::Name(), "*");
        return ref > 0 ? linker->file().exportAt(ref).flags : 0;
    }

    struct LevelCounts {
        size_t actors = 0;
        size_t nodes = 0;
        size_t surfs = 0;
        size_t points = 0;
        size_t verts = 0;
        size_t lightBits = 0;
        size_t leaves = 0;
        size_t leafHulls = 0;
        size_t reachSpecs = 0;
    };

    size_t liveActors(ut::ULevel* level)
    {
        return static_cast<size_t>(std::count_if(level->actors.begin(), level->actors.end(), [](ut::UObject* a) {
            return a != nullptr;
        }));
    }

    LevelCounts countsOf(ut::ULevel* level)
    {
        return { liveActors(level), level->model->nodes.size(), level->model->surfs.size(), level->model->points.size(), level->model->verts.size(), level->model->lightBits.size(), level->model->leaves.size(), level->model->leafHulls.size(), level->reachSpecs.size() };
    }
}

TEST_CASE("saved map reloads with the same level and a moved actor", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    ut::objects::setEditorMode(true);
    std::filesystem::path file = std::filesystem::path(std::getenv("XDG_DATA_HOME")) / "openut-editor-map-test" / "EditedDeck.unr";
    std::filesystem::remove_all(file.parent_path());
    LevelCounts original;
    ut::Name movedName;
    ut::Name builderName;
    {
        ut::Linker* linker = ut::objects::loadPackage(ut::Name("DM-Deck16]["));
        REQUIRE(linker != nullptr);
        ut::ULevel* level = loadEditorLevel(linker);
        original = countsOf(level);
        ut::UObject* moved = nullptr;
        for (ut::UObject* a : level->actors) {
            if (!moved && a && a->isA(ut::Name("PathNode"))) {
                moved = a;
            }
        }
        REQUIRE(moved != nullptr);
        movedName = moved->name;
        builderName = level->actors[1]->name;
        ut::actor::Location(moved) = { -2266.0f, 8294.0f, -256.0f };
        std::string error;
        REQUIRE(ut::mapwriter::saveMap(level, file, &error));
        CHECK(error.empty());
    }
    size_t mismatchesBefore = ut::objects::loadMismatches().size();
    ut::Linker* saved = ut::objects::loadPackageFile(ut::Name("EditedDeck"), file, false);
    REQUIRE(saved != nullptr);
    ut::ULevel* level = loadEditorLevel(saved);
    REQUIRE(level != nullptr);
    REQUIRE(level->model != nullptr);
    LevelCounts reloaded = countsOf(level);
    CHECK(reloaded.actors == original.actors);
    CHECK(level->actors.size() == original.actors);
    CHECK(reloaded.nodes == original.nodes);
    CHECK(reloaded.surfs == original.surfs);
    CHECK(reloaded.points == original.points);
    CHECK(reloaded.verts == original.verts);
    CHECK(reloaded.lightBits == original.lightBits);
    CHECK(reloaded.leaves == original.leaves);
    CHECK(reloaded.leafHulls == original.leafHulls);
    CHECK(reloaded.reachSpecs == original.reachSpecs);
    ut::UObject* moved = findActor(level, movedName);
    REQUIRE(moved != nullptr);
    CHECK(moved->outermost()->name == ut::Name("EditedDeck"));
    CHECK(ut::actor::Location(moved).x == -2266.0f);
    CHECK(ut::actor::Location(moved).y == 8294.0f);
    CHECK(ut::actor::Location(moved).z == -256.0f);
    CHECK(level->actors[0]->cls->name == ut::Name("LevelInfo"));
    CHECK((exportFlags(saved, builderName) & (ut::RF_NotForClient | ut::RF_NotForServer)) == (ut::RF_NotForClient | ut::RF_NotForServer));
    CHECK((exportFlags(saved, movedName) & (ut::RF_NotForClient | ut::RF_NotForServer)) == 0);
    for (ut::UObject* a : level->actors) {
        ut::objects::ensureLoaded(ut::actor::Brush(a));
    }
    ut::objects::flushLoads();
    const std::vector<std::string>& mismatches = ut::objects::loadMismatches();
    INFO("first mismatch: " << (mismatches.size() > mismatchesBefore ? mismatches[mismatchesBefore] : std::string()));
    CHECK(mismatches.size() == mismatchesBefore);
    ut::objects::unloadPackage(ut::Name("EditedDeck"));
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    ut::objects::setEditorMode(false);
}
