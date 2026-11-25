#include "engine/mesh.h"

#include <catch_amalgamated.hpp>

namespace {
    void setupMesh(ut::UMesh& mesh)
    {
        mesh.frameVerts = 1;
        mesh.animFrames = 4;
        mesh.scale = { 1, 1, 1 };
        mesh.verts = { { 0, 0, 0 }, { 10, 0, 0 }, { 100, 0, 0 }, { 200, 0, 0 } };
        ut::AnimSequence first;
        first.name = ut::Name("TweenTestA");
        first.startFrame = 0;
        first.numFrames = 2;
        first.rate = 10;
        ut::AnimSequence second;
        second.name = ut::Name("TweenTestB");
        second.startFrame = 2;
        second.numFrames = 2;
        second.rate = 10;
        mesh.sequences = { first, second };
    }
}

TEST_CASE("tweening starts from the last drawn pose and moves toward the new sequence")
{
    ut::UMesh mesh;
    setupMesh(mesh);
    ut::MeshPoseCache cache;
    mesh.tweenFrame(ut::Name("TweenTestA"), 0.25f, cache, 1);
    REQUIRE(cache.verts.size() == 1);
    CHECK(cache.verts[0].x == 5.0f);
    mesh.tweenFrame(ut::Name("TweenTestB"), -0.5f, cache, 1);
    CHECK(cache.verts[0].x == 5.0f);
    mesh.tweenFrame(ut::Name("TweenTestB"), -0.25f, cache, 1);
    CHECK(cache.verts[0].x == 52.5f);
    mesh.tweenFrame(ut::Name("TweenTestB"), -0.125f, cache, 1);
    CHECK(cache.verts[0].x == 76.25f);
    mesh.tweenFrame(ut::Name("TweenTestB"), 0.0f, cache, 1);
    CHECK(cache.verts[0].x == 100.0f);
}

TEST_CASE("LOD mesh tweening stops once the tween indicator passes 0.97")
{
    ut::ULodMesh mesh;
    setupMesh(mesh);
    ut::MeshPoseCache cache;
    mesh.tweenFrame(ut::Name("TweenTestA"), 0.25f, cache, 1);
    mesh.tweenFrame(ut::Name("TweenTestB"), -0.5f, cache, 1);
    const float frames[] = { -0.25f, -0.125f, -0.0625f, -0.03125f, -0.015625f };
    for (float frame : frames) {
        mesh.tweenFrame(ut::Name("TweenTestB"), frame, cache, 1);
    }
    CHECK(cache.verts[0].x == 97.03125f);
    mesh.tweenFrame(ut::Name("TweenTestB"), -0.0078125f, cache, 1);
    CHECK(cache.verts[0].x == 97.03125f);
    CHECK(cache.tweenIndicator > 0.97f);
}

TEST_CASE("LOD mesh tweening cannot return more vertices than the cached pose had")
{
    ut::ULodMesh mesh;
    setupMesh(mesh);
    ut::MeshPoseCache cache;
    CHECK(mesh.tweenFrame(ut::Name("TweenTestA"), 0.25f, cache, 0) == 0);
    CHECK(mesh.tweenFrame(ut::Name("TweenTestB"), -0.5f, cache, 1) == 0);
    CHECK(mesh.tweenFrame(ut::Name("TweenTestA"), 0.5f, cache, 1) == 1);
}
