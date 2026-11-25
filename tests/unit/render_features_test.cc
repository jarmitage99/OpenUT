#include "core/objects.h"
#include "core/vm.h"
#include "engine/decal.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"
#include "render/lighting.h"
#include "render/scene_renderer.h"

#include "test_world.h"

#include <catch_amalgamated.hpp>

#include <cmath>

TEST_CASE("volumetric fog integrates the light sphere along the view ray")
{
    ut::VolumetricLight light = ut::prepareVolumetric({ 0, 0, 100 }, 3, 1.0f, 8);
    CHECK(light.radius == 100.0f);
    CHECK(light.brightness == 0.125f);
    CHECK_FALSE(light.inside);
    CHECK(ut::volumetricFog(light, { 0, 0, 300 }) == Catch::Approx(0.5f).margin(0.002));
    CHECK(ut::volumetricFog(light, { 0, 0, 100 }) == Catch::Approx(0.25f).margin(0.002));
    CHECK(ut::volumetricFog(light, { 300, 0, 0 }) == 0.0f);
    CHECK(ut::volumetricFog(light, { 0, 0, -50 }) == 0.0f);
    ut::VolumetricLight bright = ut::prepareVolumetric({ 0, 0, 100 }, 3, 1.0f, 64);
    CHECK(ut::volumetricFog(bright, { 0, 0, 300 }) == 1.0f);
}

TEST_CASE("fog maps of several volumetric lights merge like a screen blend in 7 bits")
{
    CHECK(ut::fogMerge(100, 50) == 111);
    CHECK(ut::fogMerge(0, 64) == 64);
    CHECK(ut::fogMerge(127, 127) == 127);
}

TEST_CASE("environment map coordinates come from the world space reflection vector")
{
    float u = -1;
    float v = -1;
    ut::environmentCoords({ 10, 0, 0 }, { -1, 0, 0 }, u, v);
    CHECK(u == Catch::Approx(0.0f).margin(1e-5));
    CHECK(v == Catch::Approx(0.5f).margin(1e-5));
    ut::environmentCoords({ 0, 0, -5 }, { 0, 0, 1 }, u, v);
    CHECK(u == Catch::Approx(0.5f).margin(1e-5));
    CHECK(v == Catch::Approx(0.5f).margin(1e-5));
    ut::environmentCoords({ 3, 3, 0 }, { 0, -1, 0 }, u, v);
    CHECK(u == Catch::Approx(0.853553f).margin(1e-5));
    CHECK(v == Catch::Approx(0.146447f).margin(1e-5));
}

TEST_CASE("only DT_Sprite and DT_SpriteAnimOnce actors are drawn as sprites")
{
    CHECK(ut::drawnAsSprite(1));
    CHECK(ut::drawnAsSprite(7));
    CHECK_FALSE(ut::drawnAsSprite(0));
    CHECK_FALSE(ut::drawnAsSprite(2));
    CHECK_FALSE(ut::drawnAsSprite(3));
    CHECK_FALSE(ut::drawnAsSprite(4));
    CHECK_FALSE(ut::drawnAsSprite(5));
    CHECK_FALSE(ut::drawnAsSprite(6));
}

using namespace testworld;

TEST_CASE("version 61 texture packages load names and mip data", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::Linker* linker = ut::objects::loadPackage(ut::Name("Ancient"));
    REQUIRE(linker != nullptr);
    CHECK(linker->file().version() == 61);
    size_t textures = 0;
    for (size_t i = 0; i < linker->file().exports().size(); i++) {
        int32_t ref = static_cast<int32_t>(i + 1);
        if (linker->file().classOf(ref) != "Texture") {
            continue;
        }
        ut::UTexture* texture = ut::cast<ut::UTexture>(linker->object(ref));
        REQUIRE(texture != nullptr);
        ut::objects::ensureLoaded(texture);
        INFO(texture->name.str());
        REQUIRE_FALSE(texture->mips.empty());
        const ut::TextureMip& mip = texture->mips[0];
        CHECK(mip.width == 1u << mip.ubits);
        CHECK(mip.height == 1u << mip.vbits);
        CHECK(mip.data.size() == static_cast<size_t>(mip.width) * mip.height);
        CHECK(texture->rgba(0).size() == mip.data.size() * 4);
        textures++;
    }
    CHECK(textures > 0);
}

TEST_CASE("matrix mode textures come only from a viewport actor's sprite", "[data]")
{
    Deck deck;
    ut::UObject* player = deck.world.player();
    REQUIRE(player != nullptr);
    ut::UTexture* texture = ut::cast<ut::UTexture>(ut::objects::loadObject("Engine.S_Actor", nullptr));
    REQUIRE(texture != nullptr);
    CHECK(ut::matrixTexture(player) == nullptr);
    ut::actor::Sprite(player) = texture;
    CHECK(ut::matrixTexture(player) == texture);
    ut::actor::Sprite(player) = nullptr;
    ut::UObject* box = spawnBox(deck.world, openSpot(deck.world, 200.0f) + ut::Vector { 0, 0, 30 });
    ut::actor::Sprite(box) = texture;
    CHECK(ut::matrixTexture(box) == nullptr);
}

TEST_CASE("decals attach to the floor surface below them, clip to its node and detach", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    ut::Vector spot = openSpot(world, 100.0f);
    ut::CheckResult floor;
    REQUIRE(world.bspLineCheck(floor, spot + ut::Vector { 0, 0, 50 }, spot - ut::Vector { 0, 0, 50 }, {}));
    REQUIRE(floor.normal.z == Catch::Approx(1.0f));
    ut::UObject* pock = world.spawn(ut::objects::findClass("Botpack.Pock"), nullptr, ut::Name(), floor.location + ut::Vector { 0, 0, 8 }, { 16384, 0, 0 }, nullptr, true);
    REQUIRE(pock != nullptr);
    REQUIRE_FALSE(pock->destroyed);
    auto found = world.decals.pieces.find(pock);
    REQUIRE(found != world.decals.pieces.end());
    REQUIRE(found->second.size() == 1);
    const ut::DecalPiece& piece = found->second[0];
    const ut::BspSurf& surf = world.level->model->surfs[static_cast<size_t>(piece.surf)];
    CHECK(world.level->model->vectors[static_cast<size_t>(surf.vNormal)].z == Catch::Approx(1.0f));
    float floorZ = world.level->model->points[static_cast<size_t>(surf.pBase)].z;
    for (const ut::Vector& vertex : piece.vertices) {
        CHECK(vertex.z == Catch::Approx(floorZ).margin(0.01));
    }
    ut::Vector center = (piece.vertices[0] + piece.vertices[2]) * 0.5f;
    CHECK(center.x == Catch::Approx(floor.location.x).margin(0.05));
    CHECK(center.y == Catch::Approx(floor.location.y).margin(0.05));
    CHECK(ut::decalFindSurf(world.level->model, floor.item, floor.location) == piece.surf);
    std::vector<ut::Vector> outside { floor.location + ut::Vector { 100000, 0, 0 }, floor.location + ut::Vector { 100010, 0, 0 }, floor.location + ut::Vector { 100010, 10, 0 } };
    ut::clipDecalToNode(world.level->model, floor.item, outside);
    CHECK(outside.empty());
    ut::detachDecal(world, pock);
    CHECK(world.decals.pieces.count(pock) == 0);
}

TEST_CASE("player shadows attach under the owner's feet when the renderer updates them", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    ut::UObject* player = world.player();
    REQUIRE(player != nullptr);
    ut::UObject* shadow = ut::pawn::Shadow(player);
    REQUIRE(shadow != nullptr);
    CHECK(shadow->isA(ut::Name("PlayerShadow")));
    ut::Vector spot = openSpot(world, 100.0f);
    REQUIRE(world.farMoveActor(player, spot + ut::Vector { 0, 0, ut::actor::CollisionHeight(player) + 1 }, false, true));
    world.tick(1.0f / 30.0f);
    ut::vm::Call(shadow, ut::Name("Update")).obj(nullptr).run();
    auto found = world.decals.pieces.find(shadow);
    REQUIRE(found != world.decals.pieces.end());
    REQUIRE_FALSE(found->second.empty());
    const ut::BspSurf& surf = world.level->model->surfs[static_cast<size_t>(found->second[0].surf)];
    CHECK(world.level->model->vectors[static_cast<size_t>(surf.vNormal)].z == Catch::Approx(1.0f));
    float floorZ = world.level->model->points[static_cast<size_t>(surf.pBase)].z;
    CHECK(std::abs(floorZ - spot.z) < 1.0f);
    for (const ut::Vector& vertex : found->second[0].vertices) {
        CHECK(vertex.z == Catch::Approx(floorZ).margin(0.01));
    }
}
