#include "core/objects.h"
#include "engine/actor.h"
#include "engine/csg.h"
#include "engine/path_builder.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <vector>

namespace {
    template <typename T>
    T* make(const char* className, ut::UObject* outer)
    {
        ut::UObject* object = ut::objects::newObject(ut::csg::nativeClass(className), outer, ut::Name(), 0);
        object->loaded = true;
        return ut::cast<T>(object);
    }

    ut::UObject* place(ut::ULevel* level, const char* className, const ut::Vector& location)
    {
        ut::UObject* actor = ut::objects::newObject(ut::objects::findClass(className), level->outermost(), ut::Name(), 0);
        actor->loaded = true;
        ut::actor::Location(actor) = location;
        ut::actor::Level(actor) = level->actors[0];
        ut::actor::XLevel(actor) = level;
        level->actors.push_back(actor);
        return actor;
    }

    ut::ULevel* makeRoom(const char* packageName)
    {
        ut::engine::initialize(OPENUT_GAME_DATA);
        ut::UObject* package = ut::objects::packageObject(ut::Name(packageName));
        ut::ULevel* level = make<ut::ULevel>("Level", package);
        level->model = make<ut::UModel>("Model", package);
        ut::UObject* info = ut::objects::newObject(ut::objects::findClass("Engine.LevelInfo"), package, ut::Name(), 0);
        info->loaded = true;
        ut::UObject* builder = make<ut::UObject>("Brush", package);
        ut::UModel* builderModel = make<ut::UModel>("Model", package);
        builderModel->polys = make<ut::UPolys>("Polys", package);
        ut::actor::Brush(builder) = builderModel;
        level->actors = { info, builder };
        ut::actor::Level(info) = info;
        ut::csg::buildCube(*builderModel->polys, 2048.0f, 1024.0f, 512.0f, nullptr);
        ut::csg::addBrush(level, builder, ut::csg::CsgSubtract);
        ut::csg::rebuild(level);
        return level;
    }

    int pathCount(ut::UObject* node)
    {
        int count = 0;
        while (count < 16 && ut::navigationpoint::Paths(node, count) != -1) {
            count++;
        }
        return count;
    }
}

TEST_CASE("define paths links nearby path nodes with reach specs and prunes the long way round", "[data]")
{
    ut::ULevel* level = makeRoom("PathsDefineTest");
    ut::UObject* a = place(level, "Engine.PathNode", { -300.0f, 0.0f, -241.0f });
    ut::UObject* b = place(level, "Engine.PathNode", { 0.0f, 0.0f, -241.0f });
    ut::UObject* c = place(level, "Engine.PathNode", { 300.0f, 0.0f, -241.0f });
    ut::UObject* far = place(level, "Engine.PathNode", { 900.0f, 450.0f, -241.0f });
    ut::paths::undefinePaths(level);
    ut::paths::definePaths(level);
    CHECK(level->reachSpecs.size() == 8);
    REQUIRE(ut::levelinfo::NavigationPointList(level->actors[0]) == far);
    CHECK(ut::navigationpoint::nextNavigationPoint(far) == c);
    REQUIRE(pathCount(a) == 1);
    const ut::ReachSpec& ab = level->reachSpecs[static_cast<size_t>(ut::navigationpoint::Paths(a, 0))];
    CHECK(ab.start == a);
    CHECK(ab.end == b);
    CHECK(ab.distance == 300);
    CHECK(ab.reachFlags == 1);
    CHECK(ab.collisionRadius >= 52);
    REQUIRE(ut::navigationpoint::PrunedPaths(a, 0) != -1);
    const ut::ReachSpec& ac = level->reachSpecs[static_cast<size_t>(ut::navigationpoint::PrunedPaths(a, 0))];
    CHECK(ac.end == c);
    CHECK(ac.pruned == 1);
    CHECK(pathCount(b) == 2);
    CHECK(pathCount(c) == 2);
    CHECK(pathCount(far) == 1);
    for (ut::UObject* actor : level->actors) {
        CHECK_FALSE(actor->isA(ut::Name("Scout")));
    }
    ut::paths::undefinePaths(level);
    CHECK(level->reachSpecs.empty());
    CHECK(ut::navigationpoint::Paths(b, 0) == -1);
    CHECK(ut::levelinfo::NavigationPointList(level->actors[0]) == nullptr);
}

namespace {
    ut::ULevel* makeLRoom(const char* packageName)
    {
        ut::ULevel* level = makeRoom(packageName);
        ut::UObject* builder = level->actors[1];
        ut::actor::Location(builder) = { 512.0f, 1024.0f, 0.0f };
        ut::csg::buildCube(*ut::cast<ut::UModel>(ut::actor::Brush(builder))->polys, 1024.0f, 1024.0f, 512.0f, nullptr);
        ut::csg::addBrush(level, builder, ut::csg::CsgSubtract);
        ut::csg::rebuild(level);
        return level;
    }

    std::vector<ut::UObject*> actorsOf(ut::ULevel* level, const char* className)
    {
        std::vector<ut::UObject*> result;
        for (ut::UObject* actor : level->actors) {
            if (!actor->destroyed && actor->isA(ut::Name(className))) {
                result.push_back(actor);
            }
        }
        return result;
    }
}

TEST_CASE("build paths places no path node in a convex room", "[data]")
{
    ut::ULevel* level = makeRoom("PathsBuildConvexTest");
    place(level, "Engine.Inventory", { 400.0f, 100.0f, -200.0f });
    CHECK(ut::paths::buildPaths(level, 1) == 0);
    CHECK(actorsOf(level, "PathNode").empty());
    CHECK(actorsOf(level, "Scout").empty());
}

TEST_CASE("build paths places one path node at the inside corner of an L shaped room", "[data]")
{
    ut::ULevel* level = makeLRoom("PathsBuildCornerTest");
    place(level, "Engine.Inventory", { -600.0f, 0.0f, -200.0f });
    CHECK(ut::paths::buildPaths(level, 1) == 1);
    std::vector<ut::UObject*> nodes = actorsOf(level, "PathNode");
    REQUIRE(nodes.size() == 1);
    ut::Vector location = ut::actor::Location(nodes[0]);
    CHECK(location.x >= 115.0f);
    CHECK(location.x <= 131.0f);
    CHECK(location.y >= 381.0f);
    CHECK(location.y <= 397.0f);
    CHECK(location.z > -256.0f);
    CHECK(location.z < -192.0f);
    CHECK(ut::navigationpoint::Paths(nodes[0], 0) == -1);
    CHECK(ut::navigationpoint::upstreamPaths(nodes[0], 15) == -1);
    CHECK(actorsOf(level, "Scout").empty());
}

TEST_CASE("build paths merges the inside corner into an existing path node", "[data]")
{
    ut::ULevel* level = makeLRoom("PathsBuildMergeTest");
    place(level, "Engine.Inventory", { -600.0f, 0.0f, -200.0f });
    REQUIRE(ut::paths::buildPaths(level, 1) == 1);
    CHECK(ut::paths::removePaths(level) == 1);
    ut::UObject* existing = place(level, "Engine.PathNode", { 160.0f, 360.0f, -245.0f });
    CHECK(ut::paths::buildPaths(level, 1) == 0);
    std::vector<ut::UObject*> nodes = actorsOf(level, "PathNode");
    REQUIRE(nodes.size() == 1);
    CHECK(nodes[0] == existing);
    CHECK(ut::actor::Location(existing).x == 160.0f);
    CHECK(ut::actor::Location(existing).y == 360.0f);
}
