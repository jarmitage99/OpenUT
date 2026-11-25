#include "core/objects.h"
#include "engine/actor.h"
#include "engine/csg.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/surface_select.h"
#include "engine/world.h"

#include "test_world.h"

#include <catch_amalgamated.hpp>

#include <cctype>
#include <set>
#include <string>
#include <vector>

namespace {
    template <typename T>
    T* make(const char* className, ut::UObject* outer)
    {
        ut::UObject* object = ut::objects::newObject(ut::csg::nativeClass(className), outer, ut::Name(), 0);
        object->loaded = true;
        return ut::cast<T>(object);
    }

    struct Rooms {
        ut::ULevel* level = nullptr;
        ut::UObject* builder = nullptr;
        ut::UObject* roomA = nullptr;
        ut::UObject* roomB = nullptr;
        ut::UObject* roomC = nullptr;
        ut::UObject* block = nullptr;
    };

    ut::UObject* addCube(Rooms& rooms, float width, float depth, float height, const ut::Vector& location, const ut::Rotator& rotation, uint8_t csgOper)
    {
        ut::UPolys& polys = *ut::cast<ut::UModel>(ut::actor::Brush(rooms.builder))->polys;
        ut::csg::buildCube(polys, width, depth, height, nullptr);
        for (size_t i = 0; i < polys.polys.size(); i++) {
            polys.polys[i].itemName = ut::Name(i < 4 ? "Side" : "Cap");
        }
        ut::actor::Location(rooms.builder) = location;
        ut::actor::Rotation(rooms.builder) = rotation;
        return ut::csg::addBrush(rooms.level, rooms.builder, csgOper);
    }

    Rooms makeRooms(const char* packageName)
    {
        ut::engine::initialize(OPENUT_GAME_DATA);
        ut::UObject* package = ut::objects::packageObject(ut::Name(packageName));
        Rooms rooms;
        rooms.level = make<ut::ULevel>("Level", package);
        rooms.level->model = make<ut::UModel>("Model", package);
        rooms.builder = make<ut::UObject>("Brush", package);
        ut::UModel* builderModel = make<ut::UModel>("Model", package);
        builderModel->polys = make<ut::UPolys>("Polys", package);
        ut::actor::Brush(rooms.builder) = builderModel;
        rooms.level->actors.push_back(nullptr);
        rooms.level->actors.push_back(rooms.builder);
        rooms.roomA = addCube(rooms, 512.0f, 512.0f, 256.0f, { 0, 0, 0 }, { 0, 0, 0 }, ut::csg::CsgSubtract);
        rooms.roomB = addCube(rooms, 256.0f, 512.0f, 256.0f, { 384, 0, 0 }, { 0, 0, 0 }, ut::csg::CsgSubtract);
        rooms.roomC = addCube(rooms, 512.0f, 512.0f, 256.0f, { 1536, 0, 0 }, { 0, 0, 0 }, ut::csg::CsgSubtract);
        rooms.block = addCube(rooms, 64.0f, 64.0f, 64.0f, { 0, 0, 0 }, { 8192, 0, 0 }, ut::csg::CsgAdd);
        ut::actor::Group(rooms.roomA) = ut::Name("East");
        ut::actor::Group(rooms.roomB) = ut::Name("West");
        ut::actor::Group(rooms.roomC) = ut::Name("East");
        ut::actor::Group(rooms.block) = ut::Name("Props");
        ut::csg::rebuild(rooms.level);
        return rooms;
    }

    int32_t surfOf(const ut::UModel& model, const ut::UObject* owner, const ut::Vector& normal)
    {
        for (size_t i = 0; i < model.surfs.size(); i++) {
            if (model.surfs[i].actor == owner && model.vectors[static_cast<size_t>(model.surfs[i].vNormal)].dot(normal) > 0.99f) {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }

    std::set<const ut::UObject*> ownersOf(const ut::UModel& model, const std::set<int32_t>& selection)
    {
        std::set<const ut::UObject*> owners;
        for (int32_t index : selection) {
            owners.insert(model.surfs[static_cast<size_t>(index)].actor);
        }
        return owners;
    }

    size_t surfCount(const ut::UModel& model, const ut::UObject* owner)
    {
        size_t count = 0;
        for (const ut::BspSurf& surf : model.surfs) {
            if (surf.actor == owner) {
                count++;
            }
        }
        return count;
    }
}

TEST_CASE("adjacent wall floor and all selections follow shared vertices of the room", "[data]")
{
    Rooms rooms = makeRooms("SurfSelectAdjacentTest");
    const ut::UModel& model = *rooms.level->model;
    int32_t floorA = surfOf(model, rooms.roomA, { 0, 0, 1 });
    int32_t ceilingA = surfOf(model, rooms.roomA, { 0, 0, -1 });
    int32_t ceilingB = surfOf(model, rooms.roomB, { 0, 0, -1 });
    int32_t floorB = surfOf(model, rooms.roomB, { 0, 0, 1 });
    int32_t floorC = surfOf(model, rooms.roomC, { 0, 0, 1 });
    int32_t wallA = surfOf(model, rooms.roomA, { 1, 0, 0 });
    REQUIRE(floorA >= 0);
    REQUIRE(ceilingA >= 0);
    REQUIRE(floorB >= 0);
    REQUIRE(floorC >= 0);
    REQUIRE(wallA >= 0);

    std::set<int32_t> walls { floorA };
    ut::editorops::selectAdjacent(model, walls, ut::editorops::Adjacent::Walls);
    CHECK(walls.count(floorA) == 1);
    CHECK(walls.count(ceilingA) == 0);
    CHECK(walls.count(floorB) == 0);
    CHECK(walls.count(surfOf(model, rooms.roomA, { 0, 1, 0 })) == 1);
    CHECK(walls.count(surfOf(model, rooms.roomA, { 0, -1, 0 })) == 1);
    CHECK(walls.count(wallA) == 1);
    CHECK(surfOf(model, rooms.roomA, { -1, 0, 0 }) == -1);
    CHECK(walls.count(surfOf(model, rooms.roomB, { -1, 0, 0 })) == 1);
    CHECK(walls.count(surfOf(model, rooms.roomC, { 1, 0, 0 })) == 0);
    CHECK(ownersOf(model, walls) == std::set<const ut::UObject*> { rooms.roomA, rooms.roomB });

    std::set<int32_t> floors { floorA };
    ut::editorops::selectAdjacent(model, floors, ut::editorops::Adjacent::Floors);
    CHECK(floors == std::set<int32_t> { floorA, floorB });

    std::set<int32_t> floorsAndCeilings { floorA, surfOf(model, rooms.roomA, { 0, 1, 0 }) };
    ut::editorops::selectAdjacent(model, floorsAndCeilings, ut::editorops::Adjacent::Floors);
    CHECK(floorsAndCeilings.count(ceilingA) == 1);
    CHECK(floorsAndCeilings.count(ceilingB) == 1);
    CHECK(floorsAndCeilings.count(floorC) == 0);

    std::set<int32_t> all { floorA };
    ut::editorops::selectAdjacent(model, all, ut::editorops::Adjacent::All);
    CHECK(all.size() == surfCount(model, rooms.roomA) + surfCount(model, rooms.roomB));
    CHECK(ownersOf(model, all) == std::set<const ut::UObject*> { rooms.roomA, rooms.roomB });
}

TEST_CASE("adjacent slants and coplanars select only the matching connected surfaces", "[data]")
{
    Rooms rooms = makeRooms("SurfSelectSlantTest");
    const ut::UModel& model = *rooms.level->model;
    REQUIRE(surfCount(model, rooms.block) == 6);
    int32_t side = surfOf(model, rooms.block, { 0, 1, 0 });
    int32_t otherSide = surfOf(model, rooms.block, { 0, -1, 0 });
    REQUIRE(side >= 0);
    REQUIRE(otherSide >= 0);
    std::set<int32_t> slantStart;
    for (size_t i = 0; i < model.surfs.size(); i++) {
        float z = model.vectors[static_cast<size_t>(model.surfs[i].vNormal)].z;
        if (model.surfs[i].actor == rooms.block && z > 0.7f && z < 0.72f) {
            slantStart.insert(static_cast<int32_t>(i));
            break;
        }
    }
    REQUIRE(slantStart.size() == 1);

    std::set<int32_t> slants = slantStart;
    ut::editorops::selectAdjacent(model, slants, ut::editorops::Adjacent::Slants);
    CHECK(slants.size() == 4);
    CHECK(slants.count(side) == 0);
    CHECK(slants.count(otherSide) == 0);
    CHECK(ownersOf(model, slants) == std::set<const ut::UObject*> { rooms.block });

    std::set<int32_t> sideWalls { side };
    ut::editorops::selectAdjacent(model, sideWalls, ut::editorops::Adjacent::Walls);
    CHECK(sideWalls == std::set<int32_t> { side });

    std::set<int32_t> blockAll { side };
    ut::editorops::selectAdjacent(model, blockAll, ut::editorops::Adjacent::All);
    CHECK(blockAll.size() == 6);
    CHECK(ownersOf(model, blockAll) == std::set<const ut::UObject*> { rooms.block });

    int32_t floorA = surfOf(model, rooms.roomA, { 0, 0, 1 });
    int32_t floorB = surfOf(model, rooms.roomB, { 0, 0, 1 });
    int32_t floorC = surfOf(model, rooms.roomC, { 0, 0, 1 });
    std::set<int32_t> coplanars { floorA };
    ut::editorops::selectAdjacent(model, coplanars, ut::editorops::Adjacent::Coplanars);
    CHECK(coplanars == std::set<int32_t> { floorA, floorB });
    CHECK(coplanars.count(floorC) == 0);
}

TEST_CASE("select all reverse and memory combinations", "[data]")
{
    Rooms rooms = makeRooms("SurfSelectMemoryTest");
    const ut::UModel& model = *rooms.level->model;
    REQUIRE(model.surfs.size() > 6);
    std::set<int32_t> all;
    ut::editorops::selectAllSurfs(model, all);
    CHECK(all.size() == model.surfs.size());
    std::set<int32_t> reversed { 0, 2 };
    ut::editorops::selectReverse(model, reversed);
    CHECK(reversed.size() == model.surfs.size() - 2);
    CHECK(reversed.count(0) == 0);
    CHECK(reversed.count(1) == 1);
    CHECK(reversed.count(2) == 0);
    std::set<int32_t> memory { 1, 2, 3 };
    std::set<int32_t> orSet { 3, 4 };
    ut::editorops::combineWithMemory(orSet, memory, ut::editorops::MemoryOp::Or);
    CHECK(orSet == std::set<int32_t> { 1, 2, 3, 4 });
    std::set<int32_t> andSet { 3, 4 };
    ut::editorops::combineWithMemory(andSet, memory, ut::editorops::MemoryOp::And);
    CHECK(andSet == std::set<int32_t> { 3 });
    std::set<int32_t> xorSet { 3, 4 };
    ut::editorops::combineWithMemory(xorSet, memory, ut::editorops::MemoryOp::Xor);
    CHECK(xorSet == std::set<int32_t> { 1, 2, 4 });
}

TEST_CASE("matching group brush item and texture selections", "[data]")
{
    Rooms rooms = makeRooms("SurfSelectMatchingTest");
    ut::UModel& model = *rooms.level->model;
    int32_t floorA = surfOf(model, rooms.roomA, { 0, 0, 1 });
    int32_t ceilingA = surfOf(model, rooms.roomA, { 0, 0, -1 });
    int32_t floorB = surfOf(model, rooms.roomB, { 0, 0, 1 });
    int32_t ceilingB = surfOf(model, rooms.roomB, { 0, 0, -1 });
    REQUIRE(floorA >= 0);
    REQUIRE(ceilingB >= 0);

    std::set<int32_t> groups { floorA };
    ut::editorops::selectMatchingGroups(model, groups);
    CHECK(groups.size() == surfCount(model, rooms.roomA) + surfCount(model, rooms.roomC));
    CHECK(ownersOf(model, groups) == std::set<const ut::UObject*> { rooms.roomA, rooms.roomC });

    std::set<int32_t> brush { floorA };
    ut::editorops::selectMatchingBrush(model, brush);
    CHECK(brush.size() == surfCount(model, rooms.roomA));
    CHECK(ownersOf(model, brush) == std::set<const ut::UObject*> { rooms.roomA });

    std::set<int32_t> items { floorA };
    ut::editorops::selectMatchingItems(model, items);
    CHECK(items == std::set<int32_t> { floorA, ceilingA });

    std::set<int32_t> twoItems { floorA, floorB };
    ut::editorops::selectMatchingItems(model, twoItems);
    CHECK(twoItems == std::set<int32_t> { floorA, ceilingA, floorB, ceilingB });

    std::vector<ut::UTexture*> ancient = testworld::packageTextures("Ancient");
    REQUIRE_FALSE(ancient.empty());
    ut::UObject* brick = ancient[0];
    model.surfs[static_cast<size_t>(floorA)].texture = brick;
    model.surfs[static_cast<size_t>(ceilingB)].texture = brick;
    std::set<int32_t> textures { floorA };
    ut::editorops::selectMatchingTexture(model, textures);
    CHECK(textures == std::set<int32_t> { floorA, ceilingB });
    CHECK(ut::editorops::surfsUsingTexture(model, brick) == std::set<int32_t> { floorA, ceilingB });
    std::set<int32_t> blank { ceilingA };
    ut::editorops::selectMatchingTexture(model, blank);
    CHECK(blank.size() == model.surfs.size() - 2);
    CHECK(blank.count(floorA) == 0);
}

TEST_CASE("textures report their group inside the package and their format", "[data]")
{
    std::vector<ut::UTexture*> textures = testworld::packageTextures("Ancient");
    REQUIRE(textures.size() > 1);
    ut::Linker* linker = ut::objects::loadPackage(ut::Name("Ancient"));
    REQUIRE(linker != nullptr);
    std::set<std::string> expected;
    for (size_t i = 0; i < linker->file().exports().size(); i++) {
        int32_t ref = static_cast<int32_t>(i + 1);
        if (linker->file().classOf(ref) == "Texture") {
            expected.insert(linker->file().groupOf(ref));
        }
    }
    ut::UTexture* first = textures[0];
    CHECK(ut::editorops::textureGroup(first) == linker->file().groupOf(linker->findExport(first->name, ut::Name("Texture"), "*")));
    CHECK(ut::editorops::textureFormatName(first->format()) == "P8");
    CHECK(ut::editorops::textureFormatName(3) == "DXT1");
    std::vector<ut::UObject*> objects(textures.begin(), textures.end());
    std::set<std::string> groups;
    for (std::string group : ut::editorops::textureGroups(objects)) {
        for (char& c : group) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        groups.insert(group);
    }
    std::set<std::string> expectedLower;
    for (std::string group : expected) {
        for (char& c : group) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        expectedLower.insert(group);
    }
    CHECK(groups == expectedLower);
    CHECK(ut::editorops::textureGroups(objects).size() == expected.size());
}
