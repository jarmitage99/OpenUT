#include "core/objects.h"
#include "engine/actor.h"
#include "engine/brush_builder.h"
#include "engine/csg.h"
#include "engine/editor_history.h"
#include "engine/editor_ops.h"
#include "engine/lightmap_builder.h"
#include "engine/map_writer.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"
#include "render/lightmap.h"

#include "test_world.h"

#include <catch_amalgamated.hpp>

#include <cstdlib>
#include <filesystem>
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

    struct Room {
        ut::ULevel* level = nullptr;
        ut::UObject* builder = nullptr;
        ut::UObject* light = nullptr;
    };

    Room makeRoom(const char* packageName, bool pillar)
    {
        ut::engine::initialize(OPENUT_GAME_DATA);
        ut::UObject* package = ut::objects::packageObject(ut::Name(packageName));
        Room room;
        room.level = make<ut::ULevel>("Level", package);
        room.level->model = make<ut::UModel>("Model", package);
        room.builder = make<ut::UObject>("Brush", package);
        ut::UModel* builderModel = make<ut::UModel>("Model", package);
        builderModel->polys = make<ut::UPolys>("Polys", package);
        ut::actor::Brush(room.builder) = builderModel;
        room.level->actors.push_back(nullptr);
        room.level->actors.push_back(room.builder);
        ut::csg::buildCube(*builderModel->polys, 512.0f, 512.0f, 256.0f, nullptr);
        ut::csg::addBrush(room.level, room.builder, ut::csg::CsgSubtract);
        if (pillar) {
            ut::csg::buildCube(*builderModel->polys, 64.0f, 64.0f, 256.0f, nullptr);
            ut::actor::Location(room.builder) = { 128.0f, 0.0f, 0.0f };
            ut::csg::addBrush(room.level, room.builder, ut::csg::CsgAdd);
        }
        room.light = ut::objects::newObject(ut::objects::findClass("Engine.Light"), package, ut::Name(), 0);
        room.light->loaded = true;
        ut::actor::Location(room.light) = { 0.0f, 0.0f, 0.0f };
        ut::actor::LightType(room.light) = 1;
        ut::actor::LightBrightness(room.light) = 64;
        ut::actor::LightRadius(room.light) = 32;
        ut::actor::bStatic(room.light) = 1;
        room.level->actors.push_back(room.light);
        ut::csg::rebuild(room.level);
        return room;
    }

    const ut::BspSurf* floorSurf(const ut::UModel& model)
    {
        for (const ut::BspSurf& surf : model.surfs) {
            if (model.vectors[static_cast<size_t>(surf.vNormal)].z > 0.99f) {
                return &surf;
            }
        }
        return nullptr;
    }

    bool texelLit(const ut::UModel& model, const ut::LightMapIndex& index, int32_t x, int32_t y)
    {
        int32_t pitch = (index.uClamp + 7) / 8;
        uint8_t byte = model.lightBits[static_cast<size_t>(index.dataOffset + y * pitch + (x >> 3))];
        return (byte & (1 << (x & 7))) != 0;
    }
}

TEST_CASE("undo and redo restore an actor move", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::UObject* package = ut::objects::packageObject(ut::Name("HistoryMoveTest"));
    ut::ULevel* level = make<ut::ULevel>("Level", package);
    ut::UObject* actor = ut::objects::newObject(ut::objects::findClass("Engine.Light"), package, ut::Name(), 0);
    actor->loaded = true;
    level->actors.push_back(actor);
    ut::actor::Location(actor) = { 10.0f, 20.0f, 30.0f };
    ut::EditorHistory history;
    history.begin(level, { actor }, "Move");
    ut::actor::Location(actor) = { 99.0f, 20.0f, 30.0f };
    history.commit();
    REQUIRE(history.undo());
    CHECK(ut::actor::Location(actor).x == 10.0f);
    CHECK(history.nextRedo() == "Move");
    REQUIRE(history.redo());
    CHECK(ut::actor::Location(actor).x == 99.0f);
    CHECK_FALSE(history.redo());
}

TEST_CASE("undo brings back a deleted actor and redo removes it again", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::UObject* package = ut::objects::packageObject(ut::Name("HistoryDeleteTest"));
    ut::ULevel* level = make<ut::ULevel>("Level", package);
    ut::UObject* first = ut::objects::newObject(ut::objects::findClass("Engine.Light"), package, ut::Name(), 0);
    ut::UObject* second = ut::objects::newObject(ut::objects::findClass("Engine.Light"), package, ut::Name(), 0);
    level->actors = { first, second };
    ut::EditorHistory history;
    history.begin(level, { second }, "Delete");
    level->actors.pop_back();
    second->destroyed = true;
    history.commit();
    REQUIRE(history.undo());
    CHECK(level->actors.size() == 2);
    CHECK(level->actors[1] == second);
    CHECK_FALSE(second->destroyed);
    REQUIRE(history.redo());
    CHECK(level->actors.size() == 1);
    CHECK(second->destroyed);
}

TEST_CASE("a light in an empty room lights the floor texels and not the edge texels inside the walls", "[data]")
{
    Room room = makeRoom("LightRoomTest", false);
    ut::lightbuild::Stats stats = ut::lightbuild::rebuild(room.level);
    const ut::UModel& model = *room.level->model;
    CHECK(stats.lights == 1);
    CHECK(stats.surfaces == 6);
    const ut::BspSurf* floor = floorSurf(model);
    REQUIRE(floor != nullptr);
    REQUIRE(floor->lightMap >= 0);
    const ut::LightMapIndex& index = model.lightMaps[static_cast<size_t>(floor->lightMap)];
    CHECK(index.uClamp == 16);
    CHECK(index.vClamp == 16);
    CHECK(index.uScale == Catch::Approx(34.15f).margin(0.0001));
    CHECK(index.pan.x == Catch::Approx(-0.125f).margin(0.0001));
    REQUIRE(index.lightActors >= 0);
    CHECK(model.lights[static_cast<size_t>(index.lightActors)] == room.light);
    CHECK(model.lights[static_cast<size_t>(index.lightActors) + 1] == nullptr);
    CHECK(model.lightBits[static_cast<size_t>(index.dataOffset)] == 0x00);
    CHECK(model.lightBits[static_cast<size_t>(index.dataOffset) + 1] == 0x00);
    CHECK(model.lightBits[static_cast<size_t>(index.dataOffset) + 2] == 0xfe);
    CHECK(model.lightBits[static_cast<size_t>(index.dataOffset) + 3] == 0x7f);
}

TEST_CASE("a pillar casts a shadow on the floor texels behind it", "[data]")
{
    Room room = makeRoom("LightPillarTest", true);
    ut::lightbuild::rebuild(room.level);
    ut::UModel& model = *room.level->model;
    const ut::BspSurf* floor = floorSurf(model);
    REQUIRE(floor != nullptr);
    REQUIRE(floor->lightMap >= 0);
    const ut::LightMapIndex& index = model.lightMaps[static_cast<size_t>(floor->lightMap)];
    REQUIRE(index.uClamp == 16);
    CHECK(texelLit(model, index, 2, 8));
    CHECK_FALSE(texelLit(model, index, 13, 8));
    CHECK_FALSE(texelLit(model, index, 14, 8));
    ut::Vector ambient { 0.1f, 0.1f, 0.1f };
    ut::LightmapTexels texels = ut::computeLightmap(&model, *floor, ambient);
    REQUIRE(texels.width == 16);
    const ut::Vector& shadowed = texels.colors[static_cast<size_t>(8 * 16 + 14)];
    const ut::Vector& lit = texels.colors[static_cast<size_t>(8 * 16 + 2)];
    CHECK(shadowed.x == Catch::Approx(0.0470588f).margin(0.000001));
    CHECK(shadowed.y == Catch::Approx(0.0470588f).margin(0.000001));
    CHECK(lit.x > 0.2f);
}

TEST_CASE("a non static light that cannot be deleted is raytraced like a static one", "[data]")
{
    Room room = makeRoom("LightNoDeleteTest", false);
    ut::actor::bStatic(room.light) = 0;
    ut::actor::bNoDelete(room.light) = 1;
    ut::lightbuild::Stats stats = ut::lightbuild::rebuild(room.level);
    const ut::UModel& model = *room.level->model;
    CHECK(stats.lights == 1);
    const ut::BspSurf* floor = floorSurf(model);
    REQUIRE(floor != nullptr);
    const ut::LightMapIndex& index = model.lightMaps[static_cast<size_t>(floor->lightMap)];
    REQUIRE(index.lightActors >= 0);
    CHECK(model.lights[static_cast<size_t>(index.lightActors)] == room.light);
    CHECK(texelLit(model, index, 8, 8));
}

TEST_CASE("an invisible brush between a light and the floor casts no shadow", "[data]")
{
    Room room = makeRoom("LightInvisibleTest", false);
    ut::UModel* builderModel = ut::cast<ut::UModel>(ut::actor::Brush(room.builder));
    ut::csg::buildCube(*builderModel->polys, 128.0f, 128.0f, 32.0f, nullptr);
    ut::actor::Location(room.builder) = { 0.0f, 0.0f, -64.0f };
    ut::csg::addBrush(room.level, room.builder, ut::csg::CsgAdd, ut::csg::PolyInvisible);
    ut::csg::rebuild(room.level);
    ut::lightbuild::rebuild(room.level);
    const ut::UModel& model = *room.level->model;
    const ut::BspSurf* floor = floorSurf(model);
    REQUIRE(floor != nullptr);
    REQUIRE(floor->lightMap >= 0);
    const ut::LightMapIndex& index = model.lightMaps[static_cast<size_t>(floor->lightMap)];
    REQUIRE(index.uClamp == 16);
    REQUIRE(index.lightActors >= 0);
    CHECK(texelLit(model, index, 7, 7));
    CHECK(texelLit(model, index, 8, 8));
}

TEST_CASE("geometry rebuild lists the lights permeating each leaf and lighting keeps the lists", "[data]")
{
    Room room = makeRoom("LightPermeateTest", false);
    ut::UModel& model = *room.level->model;
    REQUIRE(model.leaves.size() == 1);
    int32_t first = model.leaves[0].permeating;
    REQUIRE(first >= 0);
    CHECK(model.lights[static_cast<size_t>(first)] == room.light);
    CHECK(model.lights[static_cast<size_t>(first) + 1] == nullptr);
    ut::lightbuild::rebuild(room.level);
    int32_t kept = model.leaves[0].permeating;
    REQUIRE(kept >= 0);
    CHECK(model.lights[static_cast<size_t>(kept)] == room.light);
    CHECK(model.lights[static_cast<size_t>(kept) + 1] == nullptr);
    const ut::BspSurf* floor = floorSurf(model);
    REQUIRE(floor != nullptr);
    const ut::LightMapIndex& index = model.lightMaps[static_cast<size_t>(floor->lightMap)];
    CHECK(index.lightActors > kept);
}

TEST_CASE("a light behind a wall does not permeate the leaf on the other side", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::UObject* package = ut::objects::packageObject(ut::Name("LightTwoRoomTest"));
    ut::ULevel* level = make<ut::ULevel>("Level", package);
    level->model = make<ut::UModel>("Model", package);
    ut::UObject* builder = make<ut::UObject>("Brush", package);
    ut::UModel* builderModel = make<ut::UModel>("Model", package);
    builderModel->polys = make<ut::UPolys>("Polys", package);
    ut::actor::Brush(builder) = builderModel;
    level->actors = { nullptr, builder };
    ut::csg::buildCube(*builderModel->polys, 512.0f, 512.0f, 256.0f, nullptr);
    ut::csg::addBrush(level, builder, ut::csg::CsgSubtract);
    ut::actor::Location(builder) = { 0.0f, 640.0f, 0.0f };
    ut::csg::addBrush(level, builder, ut::csg::CsgSubtract);
    ut::UObject* light = ut::objects::newObject(ut::objects::findClass("Engine.Light"), package, ut::Name(), 0);
    light->loaded = true;
    ut::actor::LightType(light) = 1;
    ut::actor::LightRadius(light) = 64;
    ut::actor::bStatic(light) = 1;
    level->actors.push_back(light);
    ut::csg::rebuild(level);
    const ut::UModel& model = *level->model;
    REQUIRE(model.leaves.size() == 2);
    int lit = 0;
    for (const ut::ConvexLeaf& leaf : model.leaves) {
        if (leaf.permeating >= 0) {
            lit++;
            CHECK(model.lights[static_cast<size_t>(leaf.permeating)] == light);
        }
    }
    CHECK(lit == 1);
}

TEST_CASE("a volumetric light in a fog zone is listed in the leaves within its volume radius", "[data]")
{
    Room room = makeRoom("LightVolumeTest", false);
    ut::UObject* zone = ut::objects::newObject(ut::objects::findClass("Engine.ZoneInfo"), room.light->outer, ut::Name(), 0);
    zone->loaded = true;
    ut::actor::Location(zone) = { 100.0f, 100.0f, 0.0f };
    ut::zoneinfo::bFogZone(zone) = 1;
    room.level->actors.push_back(zone);
    ut::actor::VolumeRadius(room.light) = 8;
    ut::actor::VolumeBrightness(room.light) = 64;
    ut::csg::rebuild(room.level);
    const ut::UModel& model = *room.level->model;
    REQUIRE(model.leaves.size() == 1);
    REQUIRE(model.leaves[0].volumetric >= 0);
    CHECK(model.lights[static_cast<size_t>(model.leaves[0].volumetric)] == room.light);
    ut::zoneinfo::bFogZone(zone) = 0;
    ut::csg::rebuild(room.level);
    CHECK(room.level->model->leaves[0].volumetric == -1);
}

TEST_CASE("a surface the light cannot see gets no light list", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::UObject* package = ut::objects::packageObject(ut::Name("LightHiddenSurfTest"));
    ut::ULevel* level = make<ut::ULevel>("Level", package);
    level->model = make<ut::UModel>("Model", package);
    ut::UObject* builder = make<ut::UObject>("Brush", package);
    ut::UModel* builderModel = make<ut::UModel>("Model", package);
    builderModel->polys = make<ut::UPolys>("Polys", package);
    ut::actor::Brush(builder) = builderModel;
    level->actors = { nullptr, builder };
    ut::csg::buildCube(*builderModel->polys, 512.0f, 512.0f, 256.0f, nullptr);
    ut::csg::addBrush(level, builder, ut::csg::CsgSubtract);
    ut::actor::Location(builder) = { 1024.0f, 0.0f, 0.0f };
    ut::csg::addBrush(level, builder, ut::csg::CsgSubtract);
    ut::UObject* light = ut::objects::newObject(ut::objects::findClass("Engine.Light"), package, ut::Name(), 0);
    light->loaded = true;
    ut::actor::LightType(light) = 1;
    ut::actor::LightRadius(light) = 64;
    ut::actor::bStatic(light) = 1;
    level->actors.push_back(light);
    ut::csg::rebuild(level);
    ut::lightbuild::rebuild(level);
    const ut::UModel& model = *level->model;
    int litFloors = 0;
    int darkFloors = 0;
    for (const ut::BspNode& node : model.nodes) {
        const ut::BspSurf& surf = model.surfs[static_cast<size_t>(node.surf)];
        if (node.numVertices < 3 || node.plane.z < 0.99f || surf.lightMap < 0) {
            continue;
        }
        const ut::Vector& corner = model.points[static_cast<size_t>(model.verts[static_cast<size_t>(node.vertPool)].vertex)];
        const ut::LightMapIndex& index = model.lightMaps[static_cast<size_t>(surf.lightMap)];
        if (corner.x < 512.0f) {
            CHECK(index.lightActors >= 0);
            litFloors++;
        } else {
            CHECK(index.lightActors == -1);
            darkFloors++;
        }
    }
    CHECK(litFloors == 1);
    CHECK(darkFloors == 1);
}

TEST_CASE("a mover gets its own light maps and shadows the floor below it", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::UObject* package = ut::objects::packageObject(ut::Name("LightMoverTest"));
    ut::ULevel* level = make<ut::ULevel>("Level", package);
    level->model = make<ut::UModel>("Model", package);
    ut::UObject* builder = make<ut::UObject>("Brush", package);
    ut::UModel* builderModel = make<ut::UModel>("Model", package);
    builderModel->polys = make<ut::UPolys>("Polys", package);
    ut::actor::Brush(builder) = builderModel;
    level->actors = { nullptr, builder };
    ut::csg::buildCube(*builderModel->polys, 1024.0f, 1024.0f, 512.0f, nullptr);
    ut::csg::addBrush(level, builder, ut::csg::CsgSubtract);
    ut::csg::buildCube(*builderModel->polys, 128.0f, 128.0f, 128.0f, nullptr);
    ut::actor::Location(builder) = { 0.0f, 0.0f, -150.0f };
    ut::UObject* mover = ut::csg::addMover(level, builder, nullptr);
    REQUIRE(mover != nullptr);
    CHECK(ut::mover::BasePos(mover).z == -150.0f);
    ut::UObject* light = ut::objects::newObject(ut::objects::findClass("Engine.Light"), package, ut::Name(), 0);
    light->loaded = true;
    ut::actor::Location(light) = { 0.0f, 0.0f, 100.0f };
    ut::actor::LightType(light) = 1;
    ut::actor::LightRadius(light) = 64;
    ut::actor::bStatic(light) = 1;
    level->actors.push_back(light);
    ut::csg::rebuild(level);
    ut::lightbuild::rebuild(level);
    const ut::UModel& model = *level->model;
    const ut::BspSurf* floor = floorSurf(model);
    REQUIRE(floor != nullptr);
    REQUIRE(floor->lightMap >= 0);
    const ut::LightMapIndex& index = model.lightMaps[static_cast<size_t>(floor->lightMap)];
    REQUIRE(index.uClamp == 32);
    REQUIRE(index.lightActors >= 0);
    CHECK_FALSE(texelLit(model, index, 15, 15));
    CHECK(texelLit(model, index, 2, 15));
    ut::UModel& brush = *ut::cast<ut::UModel>(ut::actor::Brush(mover));
    REQUIRE(brush.lightMaps.size() == 6);
    int lit = 0;
    int unlit = 0;
    for (const ut::Poly& poly : brush.polys->polys) {
        REQUIRE(poly.brushPoly >= 0);
        const ut::LightMapIndex& moverIndex = brush.lightMaps[static_cast<size_t>(poly.brushPoly)];
        if (poly.normal.z > 0.99f) {
            REQUIRE(moverIndex.lightActors >= 0);
            CHECK(brush.lights[static_cast<size_t>(moverIndex.lightActors)] == light);
            CHECK(texelLit(brush, moverIndex, 1, 1));
            lit++;
        } else if (poly.normal.z < -0.99f) {
            CHECK(moverIndex.lightActors == -1);
            unlit++;
        }
    }
    CHECK(lit == 1);
    CHECK(unlit == 1);
    CHECK(ut::actor::Location(mover).z == -150.0f);
}

TEST_CASE("geometry rebuild attaches zone infos to their zones and sets actor regions", "[data]")
{
    Room room = makeRoom("ZoneInfoRegionTest", false);
    ut::UObject* zone = ut::objects::newObject(ut::objects::findClass("Engine.ZoneInfo"), room.light->outer, ut::Name(), 0);
    zone->loaded = true;
    ut::actor::Location(zone) = { 100.0f, 50.0f, 0.0f };
    room.level->actors.push_back(zone);
    ut::csg::rebuild(room.level);
    const ut::UModel& model = *room.level->model;
    CHECK(model.zones[1].zoneActor == zone);
    CHECK(ut::region(room.light).zone == zone);
    CHECK(ut::region(room.light).zoneNumber == 1);
    CHECK(ut::region(room.light).iLeaf == 0);
}

TEST_CASE("undo restores the level model after a brush is added", "[data]")
{
    Room room = makeRoom("HistoryModelTest", false);
    ut::UModel& model = *room.level->model;
    size_t nodes = model.nodes.size();
    size_t surfs = model.surfs.size();
    ut::EditorHistory history;
    ut::UModel* builderModel = ut::cast<ut::UModel>(ut::actor::Brush(room.builder));
    ut::csg::buildCube(*builderModel->polys, 64.0f, 64.0f, 64.0f, nullptr);
    history.begin(room.level, { &model }, "Add brush");
    ut::csg::addBrush(room.level, room.builder, ut::csg::CsgAdd);
    history.commit();
    REQUIRE(model.surfs.size() == surfs + 6);
    REQUIRE(history.undo());
    CHECK(model.nodes.size() == nodes);
    CHECK(model.surfs.size() == surfs);
    REQUIRE(history.redo());
    CHECK(model.surfs.size() == surfs + 6);
}

TEST_CASE("brush polygons export to and import from the UnrealEd t3d polylist format", "[data]")
{
    Room room = makeRoom("BrushTextTest", false);
    ut::UModel& brush = *ut::cast<ut::UModel>(ut::actor::Brush(room.builder));
    ut::Poly poly;
    poly.vertices = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 128.0f, 0.0f }, { 128.0f, 128.0f, 0.0f }, { 128.0f, 0.0f, 0.0f } };
    poly.base = { 0.0f, 0.0f, 0.0f };
    poly.normal = { 0.0f, 0.0f, -1.0f };
    poly.textureU = { 1.0f, 0.0f, 0.0f };
    poly.textureV = { 0.0f, -1.0f, 0.0f };
    poly.polyFlags = 0x20;
    poly.panU = 3;
    poly.link = 0;
    brush.polys->polys = { poly };
    std::string text = ut::editorops::exportPolys(*brush.polys);
    CHECK(text == "Begin PolyList\r\n"
                  "   Begin Polygon Flags=32 Link=0\r\n"
                  "      Origin   +00000.000000,+00000.000000,+00000.000000\r\n"
                  "      Normal   +00000.000000,+00000.000000,-00001.000000\r\n"
                  "      Pan      U=3 V=0\r\n"
                  "      TextureU +00001.000000,+00000.000000,+00000.000000\r\n"
                  "      TextureV +00000.000000,-00001.000000,+00000.000000\r\n"
                  "      Vertex   +00000.000000,+00000.000000,+00000.000000\r\n"
                  "      Vertex   +00000.000000,+00128.000000,+00000.000000\r\n"
                  "      Vertex   +00128.000000,+00128.000000,+00000.000000\r\n"
                  "      Vertex   +00128.000000,+00000.000000,+00000.000000\r\n"
                  "   End Polygon\r\n"
                  "End PolyList\r\n");
    brush.polys->polys.clear();
    ut::editorops::importPolys(*brush.polys, "Begin PolyList\n   Begin Polygon Flags=33554464 Link=0\n      Pan      U=3 V=7\n      Vertex   0,0,0\n      Vertex   0,128,0\n      Vertex   128,128,0\n      Vertex   128,0,0\n   End Polygon\n   Begin Polygon\n      Vertex   0,0,0\n      Vertex   1,0,0\n   End Polygon\nEnd PolyList\n");
    REQUIRE(brush.polys->polys.size() == 1);
    const ut::Poly& imported = brush.polys->polys[0];
    CHECK(imported.polyFlags == 0x20u);
    CHECK(imported.panU == 3);
    CHECK(imported.panV == 7);
    CHECK(imported.link == 0);
    CHECK(imported.vertices.size() == 4);
    CHECK(imported.base.x == 0.0f);
    CHECK(imported.normal.z == -1.0f);
}

TEST_CASE("surface pan scale align and flag edits update the surface and its brush polygon", "[data]")
{
    Room room = makeRoom("SurfaceOpsTest", false);
    ut::UModel& model = *room.level->model;
    const ut::BspSurf* floor = floorSurf(model);
    REQUIRE(floor != nullptr);
    int32_t index = static_cast<int32_t>(floor - model.surfs.data());
    std::set<int32_t> selection { index };
    ut::UObject* roomBrush = model.surfs[static_cast<size_t>(index)].actor;
    REQUIRE(roomBrush != nullptr);
    ut::Poly& master = ut::cast<ut::UModel>(ut::actor::Brush(roomBrush))->polys->polys[static_cast<size_t>(model.surfs[static_cast<size_t>(index)].brushPoly)];
    ut::editorops::texPan(model, selection, 16, -8, false);
    CHECK(model.surfs[static_cast<size_t>(index)].panU == 16);
    CHECK(model.surfs[static_cast<size_t>(index)].panV == -8);
    CHECK(master.panU == 16);
    ut::editorops::texPan(model, selection, 4, 4, false);
    CHECK(model.surfs[static_cast<size_t>(index)].panU == 20);
    ut::editorops::texPan(model, selection, 0, 0, true);
    CHECK(master.panU == 0);
    model.surfs[static_cast<size_t>(index)].lightMap = 3;
    ut::editorops::texScale(model, selection, 0.5f, 0.0f, 0.0f, 0.5f, true);
    const ut::BspSurf& scaled = model.surfs[static_cast<size_t>(index)];
    CHECK(model.vectors[static_cast<size_t>(scaled.vTextureU)].size() == Catch::Approx(0.5f).margin(0.0001));
    CHECK(model.vectors[static_cast<size_t>(scaled.vTextureV)].size() == Catch::Approx(0.5f).margin(0.0001));
    CHECK(scaled.lightMap == -1);
    CHECK(master.textureU.size() == Catch::Approx(0.5f).margin(0.0001));
    ut::editorops::setAndClearFlags(model, selection, 0x00800000, 0);
    CHECK((model.surfs[static_cast<size_t>(index)].polyFlags & 0x00800000) != 0);
    CHECK((master.polyFlags & 0x00800000) != 0);
    ut::editorops::setAndClearFlags(model, selection, 0, 0x00800000);
    CHECK((master.polyFlags & 0x00800000) == 0);
    std::vector<ut::UTexture*> ancient = testworld::packageTextures("Ancient");
    REQUIRE_FALSE(ancient.empty());
    ut::UObject* texture = ancient[0];
    ut::editorops::setTexture(model, selection, texture);
    CHECK(master.texture == texture);
    ut::editorops::texAlign(model, selection, ut::editorops::TexAlign::Floor);
    const ut::BspSurf& aligned = model.surfs[static_cast<size_t>(index)];
    CHECK(model.vectors[static_cast<size_t>(aligned.vTextureU)].x == 1.0f);
    CHECK(model.vectors[static_cast<size_t>(aligned.vTextureV)].y == 1.0f);
    CHECK(model.points[static_cast<size_t>(aligned.pBase)].z == -128.0f);
    CHECK(model.points[static_cast<size_t>(aligned.pBase)].x == 0.0f);
    CHECK(master.base.x == 0.0f);
    CHECK(master.base.z == -128.0f);
}

TEST_CASE("the original cube builder script builds the builder brush through the editor natives", "[data]")
{
    Room room = makeRoom("ScriptBuilderTest", false);
    ut::UClass* cube = nullptr;
    for (ut::UClass* cls : ut::brushbuilder::builderClasses()) {
        if (cls->name == ut::Name("CubeBuilder")) {
            cube = cls;
        }
    }
    REQUIRE(cube != nullptr);
    CHECK(ut::brushbuilder::builderClasses().size() == 10);
    ut::UObject* builder = ut::brushbuilder::builderFor(cube);
    REQUIRE(builder != nullptr);
    *reinterpret_cast<float*>(builder->propertyAddress(ut::Name("Height"))) = 300.0f;
    *reinterpret_cast<float*>(builder->propertyAddress(ut::Name("Width"))) = 200.0f;
    *reinterpret_cast<float*>(builder->propertyAddress(ut::Name("Breadth"))) = 100.0f;
    ut::actor::PrePivot(room.builder) = { 10.0f, 0.0f, 0.0f };
    ut::actor::Location(room.builder) = { 50.0f, 0.0f, 0.0f };
    ut::brushbuilder::setTarget(room.builder);
    REQUIRE(ut::brushbuilder::build(builder));
    ut::UModel& brush = *ut::cast<ut::UModel>(ut::actor::Brush(room.builder));
    REQUIRE(brush.polys->polys.size() == 6);
    const ut::Poly& first = brush.polys->polys[0];
    REQUIRE(first.vertices.size() == 4);
    CHECK(first.vertices[0].x == -50.0f);
    CHECK(first.vertices[0].y == -100.0f);
    CHECK(first.vertices[0].z == -150.0f);
    CHECK(first.normal.x == -1.0f);
    CHECK(brush.bounds.max.x == 50.0f);
    CHECK(ut::actor::Location(room.builder).x == 40.0f);
    CHECK(ut::actor::PrePivot(room.builder).x == 0.0f);
}

TEST_CASE("a map with a mover saves and reloads the mover brush with its nodes and light maps", "[data]")
{
    Room room = makeRoom("MoverSaveTest", false);
    ut::UObject* info = ut::objects::newObject(ut::objects::findClass("Engine.LevelInfo"), room.level->outermost(), ut::Name(), 0);
    info->loaded = true;
    room.level->actors[0] = info;
    ut::UModel* builderModel = ut::cast<ut::UModel>(ut::actor::Brush(room.builder));
    ut::csg::buildCube(*builderModel->polys, 64.0f, 64.0f, 32.0f, nullptr);
    ut::actor::Location(room.builder) = { 0.0f, 0.0f, -100.0f };
    ut::UObject* mover = ut::csg::addMover(room.level, room.builder, nullptr);
    REQUIRE(mover != nullptr);
    ut::lightbuild::rebuild(room.level);
    ut::UModel& brush = *ut::cast<ut::UModel>(ut::actor::Brush(mover));
    REQUIRE_FALSE(brush.nodes.empty());
    REQUIRE(brush.lightMaps.size() == 6);
    std::string brushName = brush.name.str();
    size_t nodes = brush.nodes.size();
    std::filesystem::path file = std::filesystem::path(std::getenv("XDG_DATA_HOME")) / "openut-mover-save-test" / "MoverSaved.unr";
    std::filesystem::remove_all(file.parent_path());
    std::string error;
    REQUIRE(ut::mapwriter::saveMap(room.level, file, &error));
    ut::Linker* saved = ut::objects::loadPackageFile(ut::Name("MoverSaved"), file, false);
    REQUIRE(saved != nullptr);
    int32_t ref = saved->findExport(ut::Name(brushName), ut::Name("Model"), "*");
    REQUIRE(ref > 0);
    ut::UModel* loaded = ut::cast<ut::UModel>(saved->object(ref));
    ut::objects::ensureLoaded(loaded);
    ut::objects::flushLoads();
    REQUIRE(loaded != nullptr);
    CHECK(loaded->nodes.size() == nodes);
    CHECK(loaded->lightMaps.size() == 6);
    CHECK(loaded->zones[0].zoneActor == nullptr);
    ut::objects::unloadPackage(ut::Name("MoverSaved"));
}

TEST_CASE("every brush builder in the Editor package builds a brush with its default parameters", "[data]")
{
    Room room = makeRoom("AllBuildersTest", false);
    std::vector<ut::UClass*> builders = ut::brushbuilder::builderClasses();
    REQUIRE(builders.size() == 10);
    for (ut::UClass* cls : builders) {
        INFO(cls->name.str());
        ut::UObject* builder = ut::brushbuilder::builderFor(cls);
        REQUIRE(builder != nullptr);
        ut::brushbuilder::setTarget(room.builder);
        CHECK(ut::brushbuilder::build(builder));
        ut::UModel* brush = ut::cast<ut::UModel>(ut::actor::Brush(room.builder));
        REQUIRE(brush != nullptr);
        REQUIRE(brush->polys != nullptr);
        CHECK_FALSE(brush->polys->polys.empty());
    }
}
