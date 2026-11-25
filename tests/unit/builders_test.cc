#include "core/objects.h"
#include "engine/actor.h"
#include "engine/bsp.h"
#include "engine/csg.h"
#include "engine/extra_builders.h"
#include "engine/floor_lofter.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/shape_editor.h"
#include "engine/special_brush.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {
    template <typename T>
    T* make(const char* className, ut::UObject* outer)
    {
        ut::UObject* object = ut::objects::newObject(ut::csg::nativeClass(className), outer, ut::Name(), 0);
        object->loaded = true;
        return ut::cast<T>(object);
    }

    ut::UObject* makeBuilderBrush(const char* packageName)
    {
        ut::engine::initialize(OPENUT_GAME_DATA);
        ut::UObject* package = ut::objects::packageObject(ut::Name(packageName));
        ut::UObject* brush = make<ut::UObject>("Brush", package);
        ut::UModel* model = make<ut::UModel>("Model", package);
        model->polys = make<ut::UPolys>("Polys", package);
        ut::actor::Brush(brush) = model;
        return brush;
    }

    std::vector<ut::Poly>& polysOf(ut::UObject* brush)
    {
        return ut::cast<ut::UModel>(ut::actor::Brush(brush))->polys->polys;
    }

    ut::brushbuilder::ExtraBuilder tube(std::initializer_list<std::pair<const char*, double>> overrides)
    {
        for (ut::brushbuilder::ExtraBuilder& builder : ut::brushbuilder::extraBuilders()) {
            if (builder.name != "Tube") {
                continue;
            }
            for (const std::pair<const char*, double>& item : overrides) {
                for (ut::brushbuilder::ExtraParameter& parameter : builder.parameters) {
                    if (parameter.name == item.first) {
                        parameter.value = item.second;
                    }
                }
            }
            return builder;
        }
        FAIL("missing Tube builder");
        return {};
    }

    void checkVertex(const ut::Poly& poly, size_t index, float x, float y, float z)
    {
        REQUIRE(poly.vertices.size() > index);
        CHECK(poly.vertices[index].x == Catch::Approx(x).margin(0.001));
        CHECK(poly.vertices[index].y == Catch::Approx(y).margin(0.001));
        CHECK(poly.vertices[index].z == Catch::Approx(z).margin(0.001));
    }

    size_t countItem(const std::vector<ut::Poly>& polys, const char* item)
    {
        size_t count = 0;
        for (const ut::Poly& poly : polys) {
            if (poly.itemName == ut::Name(item)) {
                count++;
            }
        }
        return count;
    }

    ut::Vector centroid(const ut::Poly& poly)
    {
        ut::Vector sum;
        for (const ut::Vector& v : poly.vertices) {
            sum += v;
        }
        return sum / static_cast<float>(poly.vertices.size());
    }

    ut::Vector faceNormal(const ut::Poly& poly)
    {
        ut::Vector normal;
        for (size_t i = 2; i < poly.vertices.size(); i++) {
            normal += (poly.vertices[i - 1] - poly.vertices[0]).cross(poly.vertices[i] - poly.vertices[0]);
        }
        return normal;
    }

    bool hasVertex(const ut::Poly& poly, float x, float y, float z)
    {
        for (const ut::Vector& v : poly.vertices) {
            if (std::abs(v.x - x) < 0.01f && std::abs(v.y - y) < 0.01f && std::abs(v.z - z) < 0.01f) {
                return true;
            }
        }
        return false;
    }

    void checkFacesPointAwayFrom(const std::vector<ut::Poly>& polys, const ut::Vector& inside)
    {
        for (const ut::Poly& poly : polys) {
            CHECK(faceNormal(poly).dot(centroid(poly) - inside) > 0);
        }
    }

    void writeBytes(const std::filesystem::path& file, const std::vector<uint8_t>& bytes)
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    void put16(std::vector<uint8_t>& bytes, uint32_t value)
    {
        bytes.push_back(static_cast<uint8_t>(value & 0xff));
        bytes.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    }

    void put32(std::vector<uint8_t>& bytes, uint32_t value)
    {
        put16(bytes, value & 0xffff);
        put16(bytes, value >> 16);
    }

    void putFloat(std::vector<uint8_t>& bytes, float value)
    {
        uint32_t raw = 0;
        std::memcpy(&raw, &value, 4);
        put32(bytes, raw);
    }

    std::filesystem::path scratch(const char* name)
    {
        std::filesystem::path directory = std::filesystem::path(std::getenv("XDG_DATA_HOME")) / "openut-builders-test";
        std::filesystem::create_directories(directory);
        return directory / name;
    }

    double polygonArea(const std::vector<ut::shapeeditor::Point>& points, const std::vector<std::array<size_t, 3>>& triangles)
    {
        double total = 0;
        for (const std::array<size_t, 3>& t : triangles) {
            const ut::shapeeditor::Point& a = points[t[0]];
            const ut::shapeeditor::Point& b = points[t[1]];
            const ut::shapeeditor::Point& c = points[t[2]];
            total += ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) / 2;
        }
        return total;
    }

    ut::shapeeditor::Document lShape()
    {
        ut::shapeeditor::Document document;
        document.shapes.push_back({ { { 0, 0 }, { 0, 128 }, { 64, 128 }, { 64, 64 }, { 128, 64 }, { 128, 0 } } });
        return document;
    }
}

TEST_CASE("tube builder is listed with the PsTube form defaults", "[builders]")
{
    ut::brushbuilder::ExtraBuilder builder = tube({});
    REQUIRE(builder.parameters.size() == 6);
    CHECK(builder.parameters[0].name == "OuterRadius");
    CHECK(builder.parameters[0].value == 512);
    CHECK(builder.parameters[1].value == 496);
    CHECK(builder.parameters[2].value == 256);
    CHECK(builder.parameters[3].value == 8);
    CHECK(builder.parameters[4].value == 0);
    CHECK(builder.parameters[5].value == 1);
}

TEST_CASE("solid tube default matches PsTube", "[builders][data]")
{
    ut::UObject* brush = makeBuilderBrush("TubeSolidDefault");
    std::string error;
    REQUIRE(ut::brushbuilder::buildExtra(tube({}), brush, &error));
    std::vector<ut::Poly>& polys = polysOf(brush);
    REQUIRE(polys.size() == 10);
    CHECK(ut::actor::Group(brush) == ut::Name("TUBE"));
    CHECK(countItem(polys, "Outer") == 8);
    CHECK(countItem(polys, "Base") == 1);
    CHECK(countItem(polys, "Top") == 1);
    checkVertex(polys[0], 0, 212.0773f, 512, -128);
    checkVertex(polys[0], 1, 212.0773f, 512, 128);
    checkVertex(polys[0], 2, 512, 212.0773f, 128);
    checkVertex(polys[0], 3, 512, 212.0773f, -128);
    REQUIRE(polys[8].vertices.size() == 8);
    checkVertex(polys[8], 0, 512, -212.0773f, -128);
    checkVertex(polys[8], 7, 512, 212.0773f, -128);
    checkVertex(polys[9], 0, 512, 212.0773f, 128);
    checkFacesPointAwayFrom(polys, {});
}

TEST_CASE("hollow tube has inner walls facing the axis and ring caps", "[builders][data]")
{
    ut::UObject* brush = makeBuilderBrush("TubeHollow");
    std::string error;
    REQUIRE(ut::brushbuilder::buildExtra(tube({ { "Hollow", 1 } }), brush, &error));
    std::vector<ut::Poly>& polys = polysOf(brush);
    REQUIRE(polys.size() == 32);
    CHECK(countItem(polys, "Outer") == 8);
    CHECK(countItem(polys, "Inner") == 8);
    CHECK(countItem(polys, "Bottom") == 8);
    CHECK(countItem(polys, "Top") == 8);
    REQUIRE(polys[16].itemName == ut::Name("Bottom"));
    checkVertex(polys[16], 0, 512, 212.0773f, -128);
    checkVertex(polys[16], 1, 496, 205.45f, -128);
    for (const ut::Poly& poly : polys) {
        ut::Vector c = centroid(poly);
        ut::Vector radial { c.x, c.y, 0 };
        ut::Vector n = faceNormal(poly);
        if (poly.itemName == ut::Name("Inner")) {
            CHECK(n.dot(radial) < 0);
        } else if (poly.itemName == ut::Name("Outer")) {
            CHECK(n.dot(radial) > 0);
        } else if (poly.itemName == ut::Name("Top")) {
            CHECK(n.z > 0);
        } else {
            CHECK(n.z < 0);
        }
    }
    CHECK_FALSE(ut::brushbuilder::buildExtra(tube({ { "Hollow", 1 }, { "InnerRadius", 600 } }), brush, &error));
    CHECK(error == "The Inner Radius must be smaller than the Outer Radius");
}

TEST_CASE("solid tube with many sides splits its caps into fans around the centre", "[builders][data]")
{
    ut::UObject* brush = makeBuilderBrush("TubeFans");
    std::string error;
    REQUIRE(ut::brushbuilder::buildExtra(tube({ { "Sides", 16 }, { "AlignSide", 0 } }), brush, &error));
    std::vector<ut::Poly>& polys = polysOf(brush);
    REQUIRE(polys.size() == 24);
    CHECK(countItem(polys, "Base") == 8);
    for (size_t i = 16; i < 24; i++) {
        CHECK(hasVertex(polys[i], 0, 0, i < 20 ? -128.0f : 128.0f));
    }
    CHECK(polys[16].vertices.size() == 7);
    CHECK(polys[19].vertices.size() == 3);
    checkFacesPointAwayFrom(polys, {});
}

TEST_CASE("floor lofter builds a closed slab from a flat grid", "[builders][data]")
{
    ut::UObject* brush = makeBuilderBrush("FloorFlat");
    ut::brushbuilder::FloorGrid grid;
    ut::brushbuilder::resizeFloor(grid);
    std::string error;
    REQUIRE(ut::brushbuilder::buildFloor(grid, brush, &error));
    std::vector<ut::Poly>& polys = polysOf(brush);
    REQUIRE(polys.size() == 33);
    CHECK(ut::actor::Group(brush) == ut::Name("Floor"));
    CHECK(countItem(polys, "FloorTOP") == 16);
    CHECK(countItem(polys, "FloorXSIDE1") == 4);
    CHECK(countItem(polys, "FloorYSIDE2") == 4);
    checkVertex(polys[0], 0, -128, -128, 0);
    checkVertex(polys[0], 1, -64, -128, 0);
    checkVertex(polys[0], 2, -64, -64, 0);
    checkVertex(polys[0], 3, -128, -64, 0);
    REQUIRE(polys[32].itemName == ut::Name("FloorBOTTOM"));
    checkVertex(polys[32], 0, -128, 128, -64);
    checkVertex(polys[32], 2, 128, -128, -64);
    checkFacesPointAwayFrom(polys, { 0, 0, -32 });
}

TEST_CASE("floor lofter splits raised cells along the chosen diagonal", "[builders][data]")
{
    ut::UObject* brush = makeBuilderBrush("FloorRaised");
    ut::brushbuilder::FloorGrid grid;
    grid.sizeX = 2;
    grid.sizeY = 1;
    ut::brushbuilder::resizeFloor(grid);
    ut::brushbuilder::floorHeight(grid, 1, 1) = 32;
    grid.reversed[1] = 1;
    std::string error;
    REQUIRE(ut::brushbuilder::buildFloor(grid, brush, &error));
    std::vector<ut::Poly>& polys = polysOf(brush);
    CHECK(countItem(polys, "FloorTOP") == 4);
    REQUIRE(polys.size() == 4 + 2 + 2 + 2 + 1);
    checkVertex(polys[0], 0, -64, -32, 0);
    checkVertex(polys[0], 1, 0, -32, 0);
    checkVertex(polys[0], 2, 0, 32, 32);
    checkVertex(polys[1], 2, -64, 32, 0);
    checkVertex(polys[2], 0, 64, -32, 0);
    checkVertex(polys[2], 1, 64, 32, 0);
    checkVertex(polys[2], 2, 0, 32, 32);
    checkVertex(polys[3], 2, 0, -32, 0);
    for (size_t i = 0; i < 4; i++) {
        CHECK(faceNormal(polys[i]).z > 0);
    }
    checkFacesPointAwayFrom(std::vector<ut::Poly>(polys.begin() + 4, polys.end()), { 0, 0, -32 });
    ut::brushbuilder::floorHeight(grid, 0, 0) = -64;
    CHECK_FALSE(ut::brushbuilder::buildFloor(grid, brush, &error));
}

TEST_CASE("height maps load from greyscale BMP and PCX files", "[builders]")
{
    std::vector<uint8_t> bmp { 'B', 'M' };
    put32(bmp, 54 + 1024 + 8);
    put32(bmp, 0);
    put32(bmp, 54 + 1024);
    put32(bmp, 40);
    put32(bmp, 3);
    put32(bmp, 2);
    put16(bmp, 1);
    put16(bmp, 8);
    put32(bmp, 0);
    put32(bmp, 8);
    put32(bmp, 0);
    put32(bmp, 0);
    put32(bmp, 256);
    put32(bmp, 256);
    for (uint32_t i = 0; i < 256; i++) {
        uint8_t grey = static_cast<uint8_t>(255 - i);
        bmp.insert(bmp.end(), { grey, grey, grey, 0 });
    }
    bmp.insert(bmp.end(), { 255, 254, 253, 0, 0, 1, 2, 0 });
    std::filesystem::path bmpFile = scratch("height.bmp");
    writeBytes(bmpFile, bmp);
    ut::brushbuilder::GreyImage image;
    std::string error;
    REQUIRE(ut::brushbuilder::loadGreyImage(bmpFile, image, &error));
    CHECK(image.width == 3);
    CHECK(image.height == 2);
    CHECK(image.pixels == std::vector<uint8_t> { 255, 254, 253, 0, 1, 2 });

    std::vector<uint8_t> pcx { 10, 5, 1, 8 };
    put16(pcx, 0);
    put16(pcx, 0);
    put16(pcx, 2);
    put16(pcx, 1);
    pcx.resize(65, 0);
    pcx.push_back(1);
    put16(pcx, 4);
    pcx.resize(128, 0);
    pcx.insert(pcx.end(), { 0xc3, 40, 0, 0xc4, 200 });
    pcx.push_back(12);
    for (int i = 0; i < 256; i++) {
        uint8_t grey = static_cast<uint8_t>(i);
        pcx.insert(pcx.end(), { grey, grey, grey });
    }
    std::filesystem::path pcxFile = scratch("height.pcx");
    writeBytes(pcxFile, pcx);
    REQUIRE(ut::brushbuilder::loadGreyImage(pcxFile, image, &error));
    CHECK(image.width == 3);
    CHECK(image.height == 2);
    CHECK(image.pixels == std::vector<uint8_t> { 40, 40, 40, 200, 200, 200 });
}

TEST_CASE("height map builder raises grid vertices by the scaled pixel values", "[builders][data]")
{
    ut::brushbuilder::GreyImage image;
    image.width = 3;
    image.height = 2;
    image.pixels = { 0, 10, 0, 0, 0, 5 };
    ut::brushbuilder::FloorGrid grid = ut::brushbuilder::heightMapGrid(image, 32, 2, 16);
    CHECK(grid.sizeX == 2);
    CHECK(grid.sizeY == 1);
    ut::UObject* brush = makeBuilderBrush("HeightMapBuild");
    std::string error;
    REQUIRE(ut::brushbuilder::buildFloor(grid, brush, &error));
    std::vector<ut::Poly>& polys = polysOf(brush);
    bool raised = false;
    bool lower = false;
    for (const ut::Poly& poly : polys) {
        raised = raised || hasVertex(poly, 0, -16, 20);
        lower = lower || hasVertex(poly, 32, 16, 10);
        CHECK_FALSE(hasVertex(poly, 0, -16, 0));
    }
    CHECK(raised);
    CHECK(lower);
    CHECK(hasVertex(polys.back(), -32, -16, -16));
    CHECK(countItem(polys, "FloorTOP") == 4);
}

TEST_CASE("shape triangulation covers concave shapes and rejects crossing sides", "[builders]")
{
    std::vector<std::array<size_t, 3>> triangles;
    std::string error;
    std::vector<ut::shapeeditor::Point> square = ut::shapeeditor::defaultShape().vertices;
    REQUIRE(ut::shapeeditor::triangulate(square, triangles, &error));
    CHECK(triangles.size() == 2);
    CHECK(std::abs(polygonArea(square, triangles)) == Catch::Approx(256.0 * 256.0));
    std::vector<ut::shapeeditor::Point> l = lShape().shapes[0].vertices;
    REQUIRE(ut::shapeeditor::triangulate(l, triangles, &error));
    CHECK(triangles.size() == 4);
    CHECK(std::abs(polygonArea(l, triangles)) == Catch::Approx(128.0 * 128.0 - 64.0 * 64.0));
    std::vector<ut::shapeeditor::Point> bowTie { { 0, 0 }, { 64, 64 }, { 64, 0 }, { 0, 64 } };
    CHECK_FALSE(ut::shapeeditor::triangulate(bowTie, triangles, &error));
    CHECK(error == "The sides of a shape cross each other");
}

TEST_CASE("shape vertices can be split, deleted and snapped", "[builders]")
{
    ut::shapeeditor::Shape shape = ut::shapeeditor::defaultShape();
    ut::shapeeditor::splitSide(shape, 0, 16);
    REQUIRE(shape.vertices.size() == 5);
    CHECK(shape.vertices[1].x == -128);
    CHECK(shape.vertices[1].y == 0);
    ut::shapeeditor::Point snapped = ut::shapeeditor::snap({ 21, -9 }, 16);
    CHECK(snapped.x == 16);
    CHECK(snapped.y == -16);
    CHECK(ut::shapeeditor::deleteVertex(shape, 1));
    CHECK(ut::shapeeditor::deleteVertex(shape, 0));
    CHECK(shape.vertices.size() == 3);
    CHECK_FALSE(ut::shapeeditor::deleteVertex(shape, 0));
}

TEST_CASE("extruding the default shape makes a closed box around the origin", "[builders][data]")
{
    ut::UObject* brush = makeBuilderBrush("ShapeExtrude");
    std::string error;
    REQUIRE(ut::shapeeditor::extrude(ut::shapeeditor::newDocument(), 64, brush, &error));
    std::vector<ut::Poly>& polys = polysOf(brush);
    REQUIRE(polys.size() == 6);
    CHECK(ut::actor::Group(brush) == ut::Name("2DLoft"));
    CHECK(countItem(polys, "2DLoftSIDE") == 4);
    checkFacesPointAwayFrom(polys, {});
    for (const ut::Poly& poly : polys) {
        for (const ut::Vector& v : poly.vertices) {
            CHECK(std::abs(v.x) == 128);
            CHECK(std::abs(v.y) == 128);
            CHECK(std::abs(v.z) == 32);
        }
    }
    ut::shapeeditor::Document moved = ut::shapeeditor::newDocument();
    moved.origin = { 128, 128 };
    REQUIRE(ut::shapeeditor::extrude(moved, 64, brush, &error));
    for (const ut::Poly& poly : polysOf(brush)) {
        for (const ut::Vector& v : poly.vertices) {
            CHECK(v.x <= 0);
            CHECK(v.x >= -256);
            CHECK(v.y >= 0);
            CHECK(v.y <= 256);
        }
    }
}

TEST_CASE("extruding a concave shape keeps every face convex and outward", "[builders][data]")
{
    ut::UObject* brush = makeBuilderBrush("ShapeExtrudeL");
    std::string error;
    ut::shapeeditor::Document document = lShape();
    REQUIRE(ut::shapeeditor::extrude(document, 32, brush, &error));
    std::vector<ut::Poly>& polys = polysOf(brush);
    CHECK(countItem(polys, "2DLoftSIDE") == 6);
    double topArea = 0;
    for (const ut::Poly& poly : polys) {
        ut::Vector n = faceNormal(poly);
        if (poly.itemName == ut::Name("2DLoftTOP")) {
            CHECK(n.z > 0);
            topArea += n.size() / 2;
        } else if (poly.itemName == ut::Name("2DLoftEND")) {
            CHECK(n.z < 0);
        }
        for (size_t i = 0; i < poly.vertices.size(); i++) {
            const ut::Vector& a = poly.vertices[i];
            const ut::Vector& b = poly.vertices[(i + 1) % poly.vertices.size()];
            const ut::Vector& c = poly.vertices[(i + 2) % poly.vertices.size()];
            CHECK((b - a).cross(c - b).dot(n) >= -0.001f);
        }
    }
    CHECK(topArea == Catch::Approx(128.0 * 128.0 - 64.0 * 64.0));
    for (const ut::Poly& poly : polys) {
        if (poly.itemName != ut::Name("2DLoftSIDE")) {
            continue;
        }
        ut::Vector n = faceNormal(poly);
        ut::Vector c = centroid(poly);
        bool outerX = std::abs(c.x) < 0.01f && n.x < 0;
        bool outerY = std::abs(c.y) < 0.01f && n.y > 0;
        bool farX = std::abs(c.x - 128) < 0.01f && n.x > 0;
        bool farY = std::abs(c.y + 128) < 0.01f && n.y < 0;
        bool notchX = std::abs(c.x - 64) < 0.01f && n.x > 0;
        bool notchY = std::abs(c.y + 64) < 0.01f && n.y < 0;
        CHECK((outerX || outerY || farX || farY || notchX || notchY));
    }
}

TEST_CASE("extrude to point and to bevel taper the shape upwards", "[builders][data]")
{
    ut::UObject* brush = makeBuilderBrush("ShapeTaper");
    std::string error;
    REQUIRE(ut::shapeeditor::extrudeToPoint(ut::shapeeditor::newDocument(), 256, brush, &error));
    std::vector<ut::Poly>& pyramid = polysOf(brush);
    REQUIRE(pyramid.size() == 5);
    CHECK(countItem(pyramid, "2DLoftSIDE") == 4);
    for (const ut::Poly& poly : pyramid) {
        if (poly.itemName == ut::Name("2DLoftSIDE")) {
            CHECK(poly.vertices.size() == 3);
            CHECK(hasVertex(poly, 0, 0, 256));
        }
    }
    checkFacesPointAwayFrom(pyramid, { 0, 0, 64 });
    REQUIRE(ut::shapeeditor::extrudeToBevel(ut::shapeeditor::newDocument(), 128, 32, brush, &error));
    std::vector<ut::Poly>& bevel = polysOf(brush);
    REQUIRE(bevel.size() == 6);
    bool top = false;
    for (const ut::Poly& poly : bevel) {
        if (poly.itemName == ut::Name("2DLoftTOP")) {
            top = true;
            CHECK(hasVertex(poly, 96, 96, 32));
            CHECK(hasVertex(poly, -96, -96, 32));
        }
    }
    CHECK(top);
    checkFacesPointAwayFrom(bevel, { 0, 0, 16 });
    CHECK_FALSE(ut::shapeeditor::extrudeToBevel(ut::shapeeditor::newDocument(), 128, 128, brush, &error));
}

TEST_CASE("revolving a shape sweeps its sides around the origin", "[builders][data]")
{
    ut::shapeeditor::Document document;
    document.shapes.push_back({ { { 64, -64 }, { 64, 64 }, { 128, 64 }, { 128, -64 } } });
    ut::UObject* brush = makeBuilderBrush("ShapeRevolve");
    std::string error;
    REQUIRE(ut::shapeeditor::revolve(document, 8, 8, brush, &error));
    std::vector<ut::Poly>& ring = polysOf(brush);
    REQUIRE(ring.size() == 32);
    for (const ut::Poly& poly : ring) {
        ut::Vector c = centroid(poly);
        double angle = std::atan2(c.z, c.x);
        ut::Vector core { static_cast<float>(96 * std::cos(angle)), 0, static_cast<float>(96 * std::sin(angle)) };
        CHECK(faceNormal(poly).dot(c - core) > 0);
        for (const ut::Vector& v : poly.vertices) {
            float radius = std::sqrt(v.x * v.x + v.z * v.z);
            bool onCircle = std::abs(radius - 64) < 0.01f || std::abs(radius - 128) < 0.01f;
            CHECK(onCircle);
        }
    }
    REQUIRE(ut::shapeeditor::revolve(document, 4, 1, brush, &error));
    std::vector<ut::Poly>& quarter = polysOf(brush);
    REQUIRE(quarter.size() == 6);
    CHECK(countItem(quarter, "2DLoftEND") == 1);
    CHECK(countItem(quarter, "2DLoftTOP") == 1);
    for (const ut::Poly& poly : quarter) {
        if (poly.itemName == ut::Name("2DLoftEND")) {
            CHECK(faceNormal(poly).z < 0);
        }
        if (poly.itemName == ut::Name("2DLoftTOP")) {
            CHECK(faceNormal(poly).x < 0);
            CHECK(hasVertex(poly, 0, 64, 128));
        }
    }
    ut::shapeeditor::flip(document, true);
    REQUIRE(ut::shapeeditor::revolve(document, 8, 8, brush, &error));
    for (const ut::Poly& poly : polysOf(brush)) {
        ut::Vector c = centroid(poly);
        double angle = std::atan2(c.z, c.x);
        ut::Vector core { static_cast<float>(96 * std::cos(angle)), 0, static_cast<float>(96 * std::sin(angle)) };
        CHECK(faceNormal(poly).dot(c - core) > 0);
    }
    document.origin = { -96, 0 };
    CHECK_FALSE(ut::shapeeditor::revolve(document, 8, 8, brush, &error));
    CHECK(error == "Origin must be completely to the left or right side of the shape.");
}

TEST_CASE("2D shapes save and load in the UnrealEd .2ds format", "[builders]")
{
    ut::shapeeditor::Document document = lShape();
    document.origin = { 16, 32 };
    std::filesystem::path file = scratch("shape.2ds");
    std::string error;
    REQUIRE(ut::shapeeditor::save(document, file, &error));
    CHECK(std::filesystem::file_size(file) == 4 + 4 + 4 + 4 + 6 * (12 * 4 + 8));
    ut::shapeeditor::Document loaded;
    REQUIRE(ut::shapeeditor::load(loaded, file, &error));
    CHECK(loaded.origin.x == 16);
    CHECK(loaded.origin.y == 32);
    REQUIRE(loaded.shapes.size() == 1);
    REQUIRE(loaded.shapes[0].vertices.size() == 6);
    for (size_t i = 0; i < 6; i++) {
        CHECK(loaded.shapes[0].vertices[i].x == document.shapes[0].vertices[i].x);
        CHECK(loaded.shapes[0].vertices[i].y == document.shapes[0].vertices[i].y);
    }

    std::vector<uint8_t> bytes;
    putFloat(bytes, 0);
    putFloat(bytes, 0);
    put32(bytes, 1);
    put32(bytes, 3);
    float segments[3][4] = { { 0, 0, 0, 64 }, { 64, 0, 0, 0 }, { 0, 64, 64, 0 } };
    for (const auto& segment : segments) {
        for (int k = 0; k < 2; k++) {
            putFloat(bytes, segment[0]);
            putFloat(bytes, segment[1]);
            putFloat(bytes, 0);
            putFloat(bytes, segment[2]);
            putFloat(bytes, segment[3]);
            putFloat(bytes, 0);
        }
        put32(bytes, 0);
        put32(bytes, 10);
    }
    std::filesystem::path unordered = scratch("unordered.2ds");
    writeBytes(unordered, bytes);
    REQUIRE(ut::shapeeditor::load(loaded, unordered, &error));
    REQUIRE(loaded.shapes[0].vertices.size() == 3);
    CHECK(loaded.shapes[0].vertices[0].x == 0);
    CHECK(loaded.shapes[0].vertices[0].y == 0);
    CHECK(loaded.shapes[0].vertices[1].x == 0);
    CHECK(loaded.shapes[0].vertices[1].y == 64);
    CHECK(loaded.shapes[0].vertices[2].x == 64);
    CHECK(loaded.shapes[0].vertices[2].y == 0);
}

TEST_CASE("special brush presets carry the Add Special dialog flags", "[builders]")
{
    std::vector<ut::specialbrush::Preset> presets = ut::specialbrush::presets();
    REQUIRE(presets.size() == 8);
    auto flagsOf = [&](const char* name) {
        for (const ut::specialbrush::Preset& preset : presets) {
            if (preset.name == name) {
                return preset.flags;
            }
        }
        FAIL("missing preset " << name);
        return 0u;
    };
    CHECK(flagsOf("Regular Brush") == 0u);
    CHECK(flagsOf("Invisible collision hull") == 0x00000021u);
    CHECK(flagsOf("Zone Portal") == 0x04000109u);
    CHECK(flagsOf("Semisolid Pillar") == 0x00000020u);
    CHECK(flagsOf("Transparent Window") == 0x0000000cu);
    CHECK(flagsOf("Masked Decoration") == 0x0000000au);
    CHECK(flagsOf("Masked Wall") == 0x00000022u);
    CHECK(flagsOf("Water") == 0x0400010cu);
    ut::specialbrush::Options custom;
    custom.masked = true;
    custom.twoSided = true;
    custom.solidity = ut::specialbrush::Solidity::NotSolid;
    CHECK(ut::specialbrush::flags(custom) == 0x0000010au);
    std::vector<ut::specialbrush::Preset> zones = ut::specialbrush::advancedZones();
    REQUIRE(zones.size() == 3);
    CHECK(zones[0].name == "Cutaway Zone");
    CHECK(zones[1].name == "No-Terrain Zone");
    CHECK(zones[2].name == "No-Cut Zone");
}

TEST_CASE("special brushes keep their flags and solidity through a CSG rebuild", "[builders][data]")
{
    std::vector<ut::specialbrush::Preset> all = ut::specialbrush::presets();
    for (const ut::specialbrush::Preset& zone : ut::specialbrush::advancedZones()) {
        all.push_back(zone);
    }
    for (size_t index = 0; index < all.size(); index++) {
        const ut::specialbrush::Preset& preset = all[index];
        INFO(preset.name);
        ut::engine::initialize(OPENUT_GAME_DATA);
        ut::UObject* package = ut::objects::packageObject(ut::Name(("SpecialPresetLevel" + std::to_string(index)).c_str()));
        ut::ULevel* level = make<ut::ULevel>("Level", package);
        level->model = make<ut::UModel>("Model", package);
        ut::UObject* builder = make<ut::UObject>("Brush", package);
        ut::UModel* builderModel = make<ut::UModel>("Model", package);
        builderModel->polys = make<ut::UPolys>("Polys", package);
        ut::actor::Brush(builder) = builderModel;
        ut::brush::CsgOper(builder) = ut::csg::CsgActive;
        level->actors.push_back(nullptr);
        level->actors.push_back(builder);
        ut::csg::buildCube(*builderModel->polys, 1024.0f, 1024.0f, 512.0f, nullptr);
        ut::csg::addBrush(level, builder, ut::csg::CsgSubtract);
        ut::csg::buildCube(*builderModel->polys, 128.0f, 128.0f, 128.0f, nullptr);
        ut::actor::Location(builder) = { 200.0f, 0.0f, -256.0f };
        ut::UObject* special = ut::csg::addBrush(level, builder, ut::csg::CsgAdd, preset.flags);
        REQUIRE(special != nullptr);
        ut::csg::rebuild(level);
        const ut::UModel& model = *level->model;
        uint32_t expected = preset.flags;
        if (expected & ut::bsp::PF_Portal) {
            expected = (expected & ~ut::bsp::PF_Semisolid) | ut::bsp::PF_NotSolid;
        }
        CHECK(static_cast<uint32_t>(ut::brush::PolyFlags(special)) == expected);
        int surfaces = 0;
        for (const ut::BspSurf& surf : model.surfs) {
            if (surf.actor == special) {
                surfaces++;
                CHECK((surf.polyFlags & expected) == expected);
                CHECK((surf.polyFlags & (ut::bsp::PF_Semisolid | ut::bsp::PF_NotSolid | ut::bsp::PF_Portal | ut::bsp::PF_Invisible | ut::bsp::PF_Masked | 0x4u | ut::bsp::PF_TwoSided)) == expected);
            }
        }
        CHECK(surfaces > 0);
        bool notSolid = (expected & ut::bsp::PF_NotSolid) != 0;
        CHECK(ut::csg::pointSolid(model, { 200.0f, 0.0f, -230.0f }) == !notSolid);
        CHECK_FALSE(ut::csg::pointSolid(model, { 0.0f, 0.0f, -230.0f }));
        bool cutsFloor = (expected & (ut::bsp::PF_NotSolid | ut::bsp::PF_Semisolid)) == 0;
        bool floorUnder = false;
        for (const ut::BspNode& node : model.nodes) {
            if (node.numVertices < 3 || node.plane.z < 0.99f || std::abs(node.plane.w + 256.0f) > 0.5f) {
                continue;
            }
            bool covers = true;
            for (int32_t i = 0; i < node.numVertices; i++) {
                const ut::Vector& a = model.points[static_cast<size_t>(model.verts[static_cast<size_t>(node.vertPool + i)].vertex)];
                const ut::Vector& b = model.points[static_cast<size_t>(model.verts[static_cast<size_t>(node.vertPool + (i + 1) % node.numVertices)].vertex)];
                covers = covers && (b.x - a.x) * (0.0f - a.y) - (b.y - a.y) * (200.0f - a.x) >= 0;
            }
            floorUnder = floorUnder || (covers && model.surfs[static_cast<size_t>(node.surf)].actor != special);
        }
        CHECK(floorUnder == !cutsFloor);
    }
}

TEST_CASE("an extruded concave shape subtracts a matching room from the world", "[builders][data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::UObject* package = ut::objects::packageObject(ut::Name("ShapeCsgLevel"));
    ut::ULevel* level = make<ut::ULevel>("Level", package);
    level->model = make<ut::UModel>("Model", package);
    ut::UObject* builder = make<ut::UObject>("Brush", package);
    ut::UModel* builderModel = make<ut::UModel>("Model", package);
    builderModel->polys = make<ut::UPolys>("Polys", package);
    ut::actor::Brush(builder) = builderModel;
    ut::brush::CsgOper(builder) = ut::csg::CsgActive;
    level->actors.push_back(nullptr);
    level->actors.push_back(builder);
    std::string error;
    REQUIRE(ut::shapeeditor::extrude(lShape(), 64, builder, &error));
    REQUIRE(ut::csg::addBrush(level, builder, ut::csg::CsgSubtract) != nullptr);
    ut::csg::rebuild(level);
    const ut::UModel& model = *level->model;
    CHECK_FALSE(ut::csg::pointSolid(model, { 32, -32, 0 }));
    CHECK_FALSE(ut::csg::pointSolid(model, { 32, -100, 0 }));
    CHECK_FALSE(ut::csg::pointSolid(model, { 100, -32, 0 }));
    CHECK(ut::csg::pointSolid(model, { 100, -100, 0 }));
    CHECK(ut::csg::pointSolid(model, { 32, -32, 40 }));
    CHECK(ut::csg::pointSolid(model, { -10, -32, 0 }));
}
