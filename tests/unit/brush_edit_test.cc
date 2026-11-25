#include "core/objects.h"
#include "engine/actor.h"
#include "engine/brush_edit.h"
#include "engine/bsp.h"
#include "engine/csg.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
    template <typename T>
    T* make(const char* className, ut::UObject* outer)
    {
        ut::UObject* object = ut::objects::newObject(ut::csg::nativeClass(className), outer, ut::Name(), 0);
        object->loaded = true;
        return ut::cast<T>(object);
    }

    ut::UObject* makeBrush(const char* packageName, float width, float depth, float height)
    {
        ut::engine::initialize(OPENUT_GAME_DATA);
        ut::UObject* package = ut::objects::packageObject(ut::Name(packageName));
        ut::UObject* brush = make<ut::UObject>("Brush", package);
        ut::UModel* model = make<ut::UModel>("Model", package);
        model->polys = make<ut::UPolys>("Polys", package);
        ut::actor::Brush(brush) = model;
        ut::csg::buildCube(*model->polys, width, depth, height, nullptr);
        ut::csg::updateBounds(*model);
        return brush;
    }

    ut::UModel& modelOf(ut::UObject* brush)
    {
        return *ut::cast<ut::UModel>(ut::actor::Brush(brush));
    }

    ut::Vector windingNormal(const ut::Poly& poly)
    {
        ut::Vector sum;
        for (size_t i = 0; i < poly.vertices.size(); i++) {
            const ut::Vector& a = poly.vertices[i];
            const ut::Vector& b = poly.vertices[(i + 1) % poly.vertices.size()];
            sum += ut::Vector { (a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y) };
        }
        return sum.normal();
    }

    ut::Vector centerOf(const std::vector<ut::Vector>& points)
    {
        ut::Vector sum;
        for (const ut::Vector& point : points) {
            sum += point;
        }
        return sum / static_cast<float>(points.size());
    }

    void requireOutward(const std::vector<ut::Poly>& polys, const ut::Vector& inside)
    {
        for (const ut::Poly& poly : polys) {
            ut::Vector winding = windingNormal(poly);
            CHECK((centerOf(poly.vertices) - inside).dot(winding) > 0.0f);
            CHECK(winding.dot(poly.normal) == Catch::Approx(1.0f).margin(0.001));
        }
    }

    void requireBounds(const std::vector<ut::Poly>& polys, const ut::Vector& min, const ut::Vector& max)
    {
        ut::Vector low { 1e9f, 1e9f, 1e9f };
        ut::Vector high { -1e9f, -1e9f, -1e9f };
        for (const ut::Poly& poly : polys) {
            for (const ut::Vector& v : poly.vertices) {
                low = { std::min(low.x, v.x), std::min(low.y, v.y), std::min(low.z, v.z) };
                high = { std::max(high.x, v.x), std::max(high.y, v.y), std::max(high.z, v.z) };
            }
        }
        CHECK(low.x == Catch::Approx(min.x).margin(0.01));
        CHECK(low.y == Catch::Approx(min.y).margin(0.01));
        CHECK(low.z == Catch::Approx(min.z).margin(0.01));
        CHECK(high.x == Catch::Approx(max.x).margin(0.01));
        CHECK(high.y == Catch::Approx(max.y).margin(0.01));
        CHECK(high.z == Catch::Approx(max.z).margin(0.01));
    }

    float volumeOf(const std::vector<ut::Poly>& polys)
    {
        ut::Vector reference { 17.0f, 29.0f, -53.0f };
        float sum = 0.0f;
        for (const ut::Poly& poly : polys) {
            for (size_t i = 1; i + 1 < poly.vertices.size(); i++) {
                sum += (poly.vertices[0] - reference).dot((poly.vertices[i] - reference).cross(poly.vertices[i + 1] - reference));
            }
        }
        return std::fabs(sum) / 6.0f;
    }

    std::vector<ut::Vector> topFace()
    {
        return { { -128.0f, -128.0f, 128.0f }, { 128.0f, -128.0f, 128.0f }, { 128.0f, 128.0f, 128.0f }, { -128.0f, 128.0f, 128.0f } };
    }

    void shift(ut::UObject* brush, const ut::Vector& offset)
    {
        for (ut::Poly& poly : modelOf(brush).polys->polys) {
            for (ut::Vector& v : poly.vertices) {
                v += offset;
            }
            poly.base += offset;
        }
    }
}

TEST_CASE("mirroring a brush and transforming it permanently flips vertices and keeps outward winding", "[data]")
{
    ut::UObject* brush = makeBrush("BrushEditMirrorTest", 256.0f, 128.0f, 64.0f);
    shift(brush, { 100.0f, 0.0f, 0.0f });
    ut::brushedit::mirror(brush, 0);
    CHECK(ut::brushedit::mainScale(brush).x == -1.0f);
    std::vector<ut::Poly> world = ut::brushedit::worldPolys(brush);
    requireBounds(world, { -228.0f, -64.0f, -32.0f }, { 28.0f, 64.0f, 32.0f });
    requireOutward(world, { -100.0f, 0.0f, 0.0f });

    ut::brushedit::applyTransform(brush);
    CHECK(ut::brushedit::mainScale(brush).x == 1.0f);
    const std::vector<ut::Poly>& local = modelOf(brush).polys->polys;
    REQUIRE(local.size() == 6);
    requireBounds(local, { -228.0f, -64.0f, -32.0f }, { 28.0f, 64.0f, 32.0f });
    requireOutward(local, { -100.0f, 0.0f, 0.0f });
    CHECK(modelOf(brush).bounds.min.x == Catch::Approx(-228.0f).margin(0.01));
}

TEST_CASE("mirroring twice on two axes keeps the winding of every polygon outward", "[data]")
{
    ut::UObject* brush = makeBrush("BrushEditMirrorTwiceTest", 128.0f, 128.0f, 128.0f);
    shift(brush, { 0.0f, 50.0f, 20.0f });
    ut::brushedit::mirror(brush, 1);
    ut::brushedit::mirror(brush, 2);
    ut::brushedit::applyTransform(brush);
    const std::vector<ut::Poly>& local = modelOf(brush).polys->polys;
    requireBounds(local, { -64.0f, -114.0f, -84.0f }, { 64.0f, 14.0f, 44.0f });
    requireOutward(local, { 0.0f, -50.0f, -20.0f });
}

TEST_CASE("brush scale factors and rotation are baked into vertices and then reset", "[data]")
{
    ut::UObject* brush = makeBrush("BrushEditScaleTest", 256.0f, 128.0f, 64.0f);
    ut::brushedit::setMainScale(brush, { 2.0f, 1.0f, 0.5f });
    requireBounds(ut::brushedit::worldPolys(brush), { -256.0f, -64.0f, -16.0f }, { 256.0f, 64.0f, 16.0f });
    ut::actor::Rotation(brush) = { 0, 16384, 0 };
    ut::actor::Location(brush) = { 1000.0f, 0.0f, 0.0f };
    requireBounds(ut::brushedit::worldPolys(brush), { 936.0f, -256.0f, -16.0f }, { 1064.0f, 256.0f, 16.0f });

    ut::brushedit::applyTransform(brush);
    CHECK(ut::actor::Rotation(brush).yaw == 0);
    CHECK(ut::brushedit::mainScale(brush).x == 1.0f);
    CHECK(ut::brushedit::mainScale(brush).z == 1.0f);
    requireBounds(modelOf(brush).polys->polys, { -64.0f, -256.0f, -16.0f }, { 64.0f, 256.0f, 16.0f });
    requireBounds(ut::brushedit::worldPolys(brush), { 936.0f, -256.0f, -16.0f }, { 1064.0f, 256.0f, 16.0f });
    requireOutward(modelOf(brush).polys->polys, {});

    ut::brushedit::setMainScale(brush, { 3.0f, 3.0f, 3.0f });
    ut::brushedit::resetAll(brush);
    CHECK(ut::brushedit::mainScale(brush).y == 1.0f);
    CHECK(ut::actor::Location(brush).x == 0.0f);
}

TEST_CASE("setting and resetting the brush pivot keeps the brush in place", "[data]")
{
    ut::UObject* brush = makeBrush("BrushEditPivotTest", 256.0f, 128.0f, 64.0f);
    ut::actor::Location(brush) = { 100.0f, 0.0f, 0.0f };
    ut::brushedit::setPivot(brush, { 228.0f, 64.0f, 32.0f });
    CHECK(ut::actor::Location(brush).x == Catch::Approx(228.0f));
    CHECK(ut::actor::PrePivot(brush).x == Catch::Approx(128.0f));
    CHECK(ut::actor::PrePivot(brush).y == Catch::Approx(64.0f));
    CHECK(ut::actor::PrePivot(brush).z == Catch::Approx(32.0f));
    requireBounds(ut::brushedit::worldPolys(brush), { -28.0f, -64.0f, -32.0f }, { 228.0f, 64.0f, 32.0f });

    ut::actor::Rotation(brush) = { 0, 32768, 0 };
    requireBounds(ut::brushedit::worldPolys(brush), { 228.0f, 64.0f, -32.0f }, { 484.0f, 192.0f, 32.0f });

    ut::brushedit::resetPivot(brush);
    CHECK(ut::actor::PrePivot(brush).x == 0.0f);
    CHECK(ut::actor::Location(brush).x == Catch::Approx(356.0f).margin(0.01));
    CHECK(ut::actor::Location(brush).y == Catch::Approx(128.0f).margin(0.01));
    requireBounds(ut::brushedit::worldPolys(brush), { 228.0f, 64.0f, -32.0f }, { 484.0f, 192.0f, 32.0f });
}

TEST_CASE("snapping brush vertices moves them to the world grid", "[data]")
{
    ut::UObject* brush = makeBrush("BrushEditSnapTest", 250.0f, 250.0f, 100.0f);
    ut::actor::Location(brush) = { 4.0f, 0.0f, 0.0f };
    ut::brushedit::snapVertices(brush, { 16.0f, 16.0f, 16.0f });
    requireBounds(ut::brushedit::worldPolys(brush), { -128.0f, -128.0f, -48.0f }, { 128.0f, 128.0f, 48.0f });
    requireBounds(modelOf(brush).polys->polys, { -132.0f, -128.0f, -48.0f }, { 124.0f, 128.0f, 48.0f });
    CHECK(modelOf(brush).polys->polys.size() == 6);
}

TEST_CASE("dragging a brush vertex rebuilds non planar polygons as triangles", "[data]")
{
    ut::UObject* brush = makeBrush("BrushEditVertexTest", 256.0f, 256.0f, 256.0f);
    ut::UModel& model = modelOf(brush);
    CHECK(ut::brushedit::localVertices(model).size() == 8);
    CHECK(ut::brushedit::moveVertices(model, { { 128.0f, 128.0f, 128.0f } }, { 0.0f, 0.0f, 64.0f }) == 3);
    ut::brushedit::validate(model);
    const std::vector<ut::Poly>& polys = model.polys->polys;
    CHECK(polys.size() == 7);
    for (const ut::Poly& poly : polys) {
        for (const ut::Vector& v : poly.vertices) {
            CHECK(std::fabs((v - poly.vertices[0]).dot(poly.normal)) < 0.01f);
        }
    }
    requireOutward(polys, {});
    CHECK(model.bounds.max.z == Catch::Approx(192.0f));
    CHECK(ut::brushedit::localVertices(model).size() == 8);

    ut::UModel& edge = modelOf(makeBrush("BrushEditVertexEdgeTest", 256.0f, 256.0f, 256.0f));
    CHECK(ut::brushedit::moveVertices(edge, { { -128.0f, -128.0f, -128.0f }, { -128.0f, -128.0f, 128.0f } }, { 0.0f, 0.0f, 256.0f }) == 6);
    ut::brushedit::validate(edge);
    CHECK(edge.bounds.min.z == Catch::Approx(-128.0f));
    CHECK(edge.bounds.max.z == Catch::Approx(384.0f));
    requireOutward(edge.polys->polys, { 0.0f, 0.0f, 64.0f });
}

TEST_CASE("clipping a brush keeps the side behind the clip plane and caps it", "[data]")
{
    ut::UObject* brush = makeBrush("BrushEditClipTest", 256.0f, 256.0f, 256.0f);
    ut::UModel& model = modelOf(brush);
    REQUIRE(ut::brushedit::clipBrush(model, { 64.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }));
    CHECK(model.polys->polys.size() == 6);
    requireBounds(model.polys->polys, { -128.0f, -128.0f, -128.0f }, { 64.0f, 128.0f, 128.0f });
    requireOutward(model.polys->polys, { -32.0f, 0.0f, 0.0f });

    ut::UObject* other = makeBrush("BrushEditClipDiagonalTest", 256.0f, 256.0f, 256.0f);
    std::vector<ut::Poly> original = modelOf(other).polys->polys;
    ut::Vector diagonal = ut::Vector { 1.0f, 1.0f, 0.0f }.normal();
    std::vector<ut::Poly> back = ut::brushedit::clipPolys(original, {}, diagonal);
    std::vector<ut::Poly> front = ut::brushedit::clipPolys(original, {}, diagonal * -1.0f);
    CHECK(back.size() == 5);
    CHECK(front.size() == 5);
    requireOutward(back, { -64.0f, -64.0f, 0.0f });
    requireOutward(front, { 64.0f, 64.0f, 0.0f });
    requireBounds(back, { -128.0f, -128.0f, -128.0f }, { 128.0f, 128.0f, 128.0f });
    CHECK_FALSE(ut::brushedit::clipBrush(modelOf(other), { 200.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }));
    CHECK(modelOf(other).polys->polys.size() == 6);
}

TEST_CASE("clipping a concave brush only caps the solid part of the cut", "[data]")
{
    ut::UObject* brush = makeBrush("BrushEditClipConcaveTest", 256.0f, 256.0f, 256.0f);
    ut::UModel& model = modelOf(brush);
    std::vector<ut::Vector> profile = { { 0, 0, 0 }, { 256, 0, 0 }, { 256, 64, 0 }, { 64, 64, 0 }, { 64, 256, 0 }, { 0, 256, 0 } };
    std::vector<ut::Poly> polys;
    ut::Poly bottom;
    ut::Poly top;
    for (size_t i = 0; i < profile.size(); i++) {
        bottom.vertices.push_back(profile[profile.size() - 1 - i]);
        top.vertices.push_back(profile[i] + ut::Vector { 0, 0, 64 });
        ut::Poly side;
        const ut::Vector& a = profile[i];
        const ut::Vector& b = profile[(i + 1) % profile.size()];
        side.vertices = { a, b, b + ut::Vector { 0, 0, 64 }, a + ut::Vector { 0, 0, 64 } };
        polys.push_back(side);
    }
    polys.push_back(bottom);
    polys.push_back(top);
    for (ut::Poly& poly : polys) {
        ut::bsp::calcNormal(poly);
        poly.base = poly.vertices[0];
    }
    model.polys->polys = polys;
    ut::Vector diagonal = ut::Vector { 1.0f, 1.0f, 0.0f }.normal();
    REQUIRE(ut::brushedit::clipBrush(model, { 100.0f, 100.0f, 0.0f }, diagonal));
    requireBounds(model.polys->polys, { 0.0f, 0.0f, 0.0f }, { 200.0f, 200.0f, 64.0f });
    float capArea = 0.0f;
    for (const ut::Poly& poly : model.polys->polys) {
        if (poly.normal.dot(diagonal) > 0.99f) {
            for (size_t i = 1; i + 1 < poly.vertices.size(); i++) {
                capArea += (poly.vertices[i] - poly.vertices[0]).cross(poly.vertices[i + 1] - poly.vertices[0]).size() * 0.5f;
            }
        }
    }
    CHECK(capArea == Catch::Approx(2.0f * 64.0f * std::sqrt(2.0f) * 64.0f).margin(1.0));
}

TEST_CASE("two clip markers in an orthographic view define the clip plane in brush space", "[data]")
{
    ut::UObject* brush = makeBrush("BrushEditClipPlaneTest", 256.0f, 256.0f, 256.0f);
    ut::Vector base;
    ut::Vector normal;
    std::vector<ut::Vector> markers = { { 0.0f, -100.0f, 0.0f }, { 0.0f, 100.0f, 0.0f } };
    REQUIRE(ut::brushedit::clipPlane(brush, markers, 2, base, normal));
    CHECK(normal.x == Catch::Approx(-1.0f));
    std::vector<ut::Vector> flipped = { markers[1], markers[0] };
    REQUIRE(ut::brushedit::clipPlane(brush, flipped, 2, base, normal));
    CHECK(normal.x == Catch::Approx(1.0f));

    ut::actor::Location(brush) = { 32.0f, 0.0f, 0.0f };
    ut::brushedit::mirror(brush, 0);
    REQUIRE(ut::brushedit::clipPlane(brush, markers, 2, base, normal));
    CHECK(normal.x == Catch::Approx(1.0f));
    CHECK(base.x == Catch::Approx(32.0f));
    CHECK_FALSE(ut::brushedit::clipPlane(brush, { markers[0] }, 2, base, normal));
}

TEST_CASE("mover keyframes store positions relative to the base and show each key", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::UClass* moverClass = ut::objects::findClass("Engine.Mover");
    REQUIRE(moverClass);
    ut::UObject* mover = ut::objects::newObject(moverClass, ut::objects::packageObject(ut::Name("BrushEditMoverTest")), ut::Name(), 0);
    mover->loaded = true;
    ut::actor::Location(mover) = { 100.0f, 0.0f, 0.0f };
    ut::actor::Rotation(mover) = { 0, 1024, 0 };
    ut::brushedit::moverMoved(mover);
    CHECK(ut::mover::BasePos(mover).x == 100.0f);
    CHECK(ut::mover::BaseRot(mover).yaw == 1024);

    ut::brushedit::showKey(mover, 1);
    CHECK(ut::mover::KeyNum(mover) == 1);
    CHECK(ut::actor::Location(mover).x == 100.0f);
    ut::actor::Location(mover) = { 100.0f, 32.0f, 64.0f };
    ut::actor::Rotation(mover) = { 0, 1024 + 16384, 0 };
    ut::brushedit::moverMoved(mover);
    CHECK(ut::mover::KeyPos(mover, 1).y == 32.0f);
    CHECK(ut::mover::KeyPos(mover, 1).z == 64.0f);
    CHECK(ut::mover::KeyRot(mover, 1).yaw == 16384);
    CHECK(ut::mover::BasePos(mover).x == 100.0f);

    ut::brushedit::showKey(mover, 0);
    CHECK(ut::actor::Location(mover).y == 0.0f);
    CHECK(ut::actor::Location(mover).z == 0.0f);
    CHECK(ut::actor::Rotation(mover).yaw == 1024);
    CHECK(ut::mover::BasePos(mover).x == 100.0f);

    ut::actor::Location(mover) = { 200.0f, 0.0f, 0.0f };
    ut::brushedit::moverMoved(mover);
    ut::brushedit::showKey(mover, 1);
    CHECK(ut::actor::Location(mover).x == 200.0f);
    CHECK(ut::actor::Location(mover).y == 32.0f);
    CHECK(ut::actor::Rotation(mover).yaw == 1024 + 16384);

    ut::brushedit::showKey(mover, 12);
    CHECK(ut::mover::KeyNum(mover) == 7);
}

TEST_CASE("brush edges are listed once each and faces are found by their vertices in any order", "[data]")
{
    ut::UModel& model = modelOf(makeBrush("BrushEditEdgesTest", 256.0f, 256.0f, 256.0f));
    CHECK(ut::brushedit::localEdges(model).size() == 12);
    int32_t top = ut::brushedit::findPoly(model, topFace());
    REQUIRE(top >= 0);
    CHECK(model.polys->polys[static_cast<size_t>(top)].normal.z == Catch::Approx(1.0f));
    std::vector<ut::Vector> reordered = { topFace()[2], topFace()[0], topFace()[3], topFace()[1] };
    CHECK(ut::brushedit::findPoly(model, reordered) == top);
    CHECK(ut::brushedit::findPoly(model, { topFace()[0], topFace()[1], topFace()[2] }) == -1);
}

TEST_CASE("extruding a face outward adds side faces and grows the brush by the swept volume", "[data]")
{
    ut::UModel& model = modelOf(makeBrush("BrushEditExtrudeTest", 256.0f, 256.0f, 256.0f));
    size_t top = static_cast<size_t>(ut::brushedit::findPoly(model, topFace()));
    REQUIRE(ut::brushedit::extrudeFace(model, top, { 0.0f, 0.0f, 64.0f }));
    requireBounds(model.polys->polys, { -128.0f, -128.0f, -128.0f }, { 128.0f, 128.0f, 192.0f });
    requireOutward(model.polys->polys, {});
    CHECK(volumeOf(model.polys->polys) == Catch::Approx(256.0f * 256.0f * 320.0f).margin(1.0));
    CHECK(model.polys->polys.size() == 6);
    CHECK(ut::brushedit::localVertices(model).size() == 8);
    CHECK(model.bounds.max.z == Catch::Approx(192.0f));

    ut::UModel& sheared = modelOf(makeBrush("BrushEditExtrudeShearTest", 256.0f, 256.0f, 256.0f));
    size_t face = static_cast<size_t>(ut::brushedit::findPoly(sheared, topFace()));
    REQUIRE(ut::brushedit::extrudeFace(sheared, face, { 64.0f, 0.0f, 64.0f }));
    requireBounds(sheared.polys->polys, { -128.0f, -128.0f, -128.0f }, { 192.0f, 128.0f, 192.0f });
    for (const ut::Poly& poly : sheared.polys->polys) {
        CHECK(windingNormal(poly).dot(poly.normal) == Catch::Approx(1.0f).margin(0.001));
    }
    CHECK(volumeOf(sheared.polys->polys) == Catch::Approx(256.0f * 256.0f * 320.0f).margin(1.0));
    std::vector<ut::Vector> shearedVertices = ut::brushedit::localVertices(sheared);
    CHECK(std::any_of(shearedVertices.begin(), shearedVertices.end(), [](const ut::Vector& v) {
        return (v - ut::Vector { 128.0f, -128.0f, 128.0f }).sizeSquared() < 0.01f;
    }));
    CHECK(ut::brushedit::findPoly(sheared, { { -64.0f, -128.0f, 192.0f }, { 192.0f, -128.0f, 192.0f }, { 192.0f, 128.0f, 192.0f }, { -64.0f, 128.0f, 192.0f } }) >= 0);
}

TEST_CASE("extruding a face inward moves it and shrinks the brush", "[data]")
{
    ut::UModel& model = modelOf(makeBrush("BrushEditExtrudeInwardTest", 256.0f, 256.0f, 256.0f));
    size_t top = static_cast<size_t>(ut::brushedit::findPoly(model, topFace()));
    REQUIRE(ut::brushedit::extrudeFace(model, top, { 0.0f, 0.0f, -64.0f }));
    CHECK(model.polys->polys.size() == 6);
    requireBounds(model.polys->polys, { -128.0f, -128.0f, -128.0f }, { 128.0f, 128.0f, 64.0f });
    requireOutward(model.polys->polys, {});
    CHECK(volumeOf(model.polys->polys) == Catch::Approx(256.0f * 256.0f * 192.0f).margin(1.0));
    CHECK_FALSE(ut::brushedit::extrudeFace(model, top, {}));
    CHECK_FALSE(ut::brushedit::extrudeFace(model, 99, { 0.0f, 0.0f, 64.0f }));
}

TEST_CASE("transforming selected vertices scales a face around its center into a frustum", "[data]")
{
    ut::UModel& model = modelOf(makeBrush("BrushEditTransformTest", 256.0f, 256.0f, 256.0f));
    ut::Vector center { 0.0f, 0.0f, 128.0f };
    size_t moved = ut::brushedit::transformVertices(model, topFace(), [&](const ut::Vector& vertex) {
        return center + (vertex - center) * 0.5f;
    });
    CHECK(moved == 12);
    ut::brushedit::validate(model);
    requireOutward(model.polys->polys, {});
    std::vector<ut::Vector> vertices = ut::brushedit::localVertices(model);
    CHECK(vertices.size() == 8);
    CHECK(std::any_of(vertices.begin(), vertices.end(), [](const ut::Vector& v) {
        return (v - ut::Vector { 64.0f, 64.0f, 128.0f }).sizeSquared() < 0.01f;
    }));
    CHECK(volumeOf(model.polys->polys) == Catch::Approx(256.0f * (256.0f * 256.0f + 256.0f * 128.0f + 128.0f * 128.0f) / 3.0f).margin(1.0));
}

TEST_CASE("a ray picks the nearest brush polygon from outside and from inside", "[data]")
{
    ut::UModel& model = modelOf(makeBrush("BrushEditRayTest", 256.0f, 256.0f, 256.0f));
    const std::vector<ut::Poly>& polys = model.polys->polys;
    float distance = 0.0f;
    int32_t hit = ut::brushedit::rayPoly(polys, { 10.0f, 20.0f, 1000.0f }, { 0.0f, 0.0f, -1.0f }, distance);
    REQUIRE(hit >= 0);
    CHECK(polys[static_cast<size_t>(hit)].normal.z == Catch::Approx(1.0f));
    CHECK(distance == Catch::Approx(872.0f));

    hit = ut::brushedit::rayPoly(polys, {}, { 1.0f, 0.0f, 0.0f }, distance);
    REQUIRE(hit >= 0);
    CHECK(polys[static_cast<size_t>(hit)].normal.x == Catch::Approx(1.0f));
    CHECK(distance == Catch::Approx(128.0f));

    CHECK(ut::brushedit::rayPoly(polys, { 300.0f, 0.0f, 1000.0f }, { 0.0f, 0.0f, -1.0f }, distance) == -1);
}

TEST_CASE("extruding a face only merges the new side faces and leaves other coplanar faces split", "[data]")
{
    ut::UModel& model = modelOf(makeBrush("BrushEditExtrudeSplitTest", 256.0f, 256.0f, 256.0f));
    std::vector<ut::Poly>& polys = model.polys->polys;
    auto bottom = std::find_if(polys.begin(), polys.end(), [](const ut::Poly& poly) {
        return poly.normal.z < -0.99f;
    });
    REQUIRE(bottom != polys.end());
    ut::Poly half = *bottom;
    half.vertices = { bottom->vertices[0], bottom->vertices[2], bottom->vertices[3] };
    bottom->vertices = { bottom->vertices[0], bottom->vertices[1], bottom->vertices[2] };
    polys.push_back(half);
    REQUIRE(polys.size() == 7);
    size_t top = static_cast<size_t>(ut::brushedit::findPoly(model, topFace()));
    REQUIRE(ut::brushedit::extrudeFace(model, top, { 0.0f, 0.0f, 64.0f }));
    size_t bottoms = static_cast<size_t>(std::count_if(model.polys->polys.begin(), model.polys->polys.end(), [](const ut::Poly& poly) {
        return poly.normal.z < -0.99f;
    }));
    CHECK(bottoms == 2);
    CHECK(model.polys->polys.size() == 7);
    CHECK(volumeOf(model.polys->polys) == Catch::Approx(256.0f * 256.0f * 320.0f).margin(1.0));
    requireOutward(model.polys->polys, {});
}
