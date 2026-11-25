#include "core/objects.h"
#include "engine/actor.h"
#include "engine/editor_views.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"
#include "render/view_modes.h"

#include <catch_amalgamated.hpp>

#include <string>

TEST_CASE("render modes are found by their command names")
{
    ut::RenderMode mode = ut::RenderMode::Textured;
    CHECK(ut::renderModeNamed("bspcuts", mode));
    CHECK(mode == ut::RenderMode::BspCuts);
    CHECK(ut::renderModeNamed("zones", mode));
    CHECK(mode == ut::RenderMode::ZonePortal);
    CHECK(ut::renderModeNamed("lighting", mode));
    CHECK(mode == ut::RenderMode::LightingOnly);
    CHECK_FALSE(ut::renderModeNamed("sideways", mode));
    CHECK(mode == ut::RenderMode::LightingOnly);
    CHECK(std::string(ut::renderModeName(ut::RenderMode::Wireframe)) == "Wireframe");
    CHECK(std::string(ut::renderModeName(ut::RenderMode::Unlit)) == "Unlit");
}

TEST_CASE("hsv colors follow FGetHSV brightness and hue wheel")
{
    ut::Vector red = ut::hsvColor(0, 0, 255);
    CHECK(red.x == Catch::Approx(0.821309f).margin(1e-5));
    CHECK(red.y == 0.0f);
    CHECK(red.z == 0.0f);
    ut::Vector green = ut::hsvColor(85, 0, 255);
    CHECK(green.x == 0.0f);
    CHECK(green.y == Catch::Approx(0.821309f).margin(1e-5));
    ut::Vector white = ut::hsvColor(200, 255, 255);
    CHECK(white.x == Catch::Approx(0.821309f).margin(1e-5));
    CHECK(white.y == Catch::Approx(0.821309f).margin(1e-5));
    CHECK(white.z == Catch::Approx(0.821309f).margin(1e-5));
}

TEST_CASE("bsp cut colors give neighbouring nodes different hues")
{
    ut::Vector first = ut::bspCutColor(0);
    ut::Vector second = ut::bspCutColor(1);
    CHECK(first.x == Catch::Approx(0.821309f).margin(1e-5));
    CHECK(first.y == 0.0f);
    CHECK(second.x == Catch::Approx(0.173924f).margin(1e-5));
    CHECK(second.y == Catch::Approx(0.647385f).margin(1e-5));
    CHECK(second.z == 0.0f);
}

TEST_CASE("zone colors tint by zone number and shade by node")
{
    ut::Vector texture { 0.4f, 0.6f, 0.8f };
    ut::Vector outside = ut::zoneColor(0, 8, texture);
    CHECK(outside.x == Catch::Approx(0.2f));
    CHECK(outside.y == Catch::Approx(0.3f));
    CHECK(outside.z == Catch::Approx(0.4f));
    ut::Vector third = ut::zoneColor(3, 5, texture);
    CHECK(third.x == Catch::Approx(0.637939f).margin(1e-5));
    CHECK(third.y == Catch::Approx(0.053955f).margin(1e-5));
    CHECK(third.z == Catch::Approx(0.269775f).margin(1e-5));
}

TEST_CASE("pane rectangles split the view area in a 2x2 grid or maximize one pane")
{
    std::array<ut::editorviews::PaneRect, 4> grid = ut::editorviews::paneRects(100, 20, 800, 600, -1);
    CHECK(grid[0].x == 100.0f);
    CHECK(grid[0].y == 20.0f);
    CHECK(grid[1].x == 500.0f);
    CHECK(grid[2].y == 320.0f);
    CHECK(grid[3].x == 500.0f);
    CHECK(grid[3].y == 320.0f);
    CHECK(grid[3].width == 400.0f);
    CHECK(grid[3].height == 300.0f);
    std::array<ut::editorviews::PaneRect, 4> single = ut::editorviews::paneRects(100, 20, 800, 600, 2);
    CHECK(single[2].x == 100.0f);
    CHECK(single[2].y == 20.0f);
    CHECK(single[2].width == 800.0f);
    CHECK(single[2].height == 600.0f);
    CHECK(single[0].width == 0.0f);
    CHECK(single[1].width == 0.0f);
    CHECK(single[3].width == 0.0f);
}

TEST_CASE("actor icons scale with the view zoom but stay visible")
{
    CHECK(ut::editorviews::iconSize(32, 1, 0.5f) == 16.0f);
    CHECK(ut::editorviews::iconSize(32, 2, 0.5f) == 32.0f);
    CHECK(ut::editorviews::iconSize(32, 1, 0.08f) == 6.0f);
}

TEST_CASE("path links come from unpruned reach specs and mark monster paths", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::UClass* nodeClass = ut::objects::findClass("Engine.PathNode");
    ut::objects::flushLoads();
    REQUIRE(nodeClass != nullptr);
    ut::UObject* package = ut::objects::packageObject(ut::Name("ViewLinksTest"));
    auto node = [&](const ut::Vector& location) {
        ut::UObject* actor = ut::objects::newObject(nodeClass, package, ut::Name(), 0);
        actor->loaded = true;
        ut::actor::Location(actor) = location;
        return actor;
    };
    ut::UObject* a = node({ 0, 0, 0 });
    ut::UObject* b = node({ 100, 0, 0 });
    ut::UObject* c = node({ 0, 200, 50 });
    ut::ULevel level;
    ut::ReachSpec walk;
    walk.start = a;
    walk.end = b;
    walk.collisionRadius = 18;
    walk.collisionHeight = 39;
    walk.reachFlags = 1;
    ut::ReachSpec fly = walk;
    fly.end = c;
    fly.reachFlags = 2;
    ut::ReachSpec pruned = walk;
    pruned.start = b;
    pruned.end = c;
    pruned.pruned = 1;
    ut::ReachSpec dangling = walk;
    dangling.end = nullptr;
    ut::ReachSpec narrow = walk;
    narrow.start = c;
    narrow.end = a;
    narrow.collisionRadius = 17;
    level.reachSpecs = { walk, fly, pruned, dangling, narrow };
    std::vector<ut::editorviews::PathLink> links = ut::editorviews::pathLinks(level);
    REQUIRE(links.size() == 3);
    CHECK(links[0].end.x == 100.0f);
    CHECK(links[0].monster);
    CHECK(links[1].end.y == 200.0f);
    CHECK(links[1].end.z == 50.0f);
    CHECK_FALSE(links[1].monster);
    CHECK(links[2].start.y == 200.0f);
    CHECK(links[2].end.x == 0.0f);
    CHECK_FALSE(links[2].monster);
}

TEST_CASE("overhead icons use the sprite texture of hidden and sprite actors only", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::UClass* lightClass = ut::objects::findClass("Engine.Light");
    ut::UClass* boxClass = ut::objects::findClass("UnrealShare.WoodenBox");
    ut::objects::flushLoads();
    REQUIRE(lightClass != nullptr);
    REQUIRE(boxClass != nullptr);
    ut::UTexture* lightIcon = ut::editorviews::actorIcon(lightClass->defaultObject);
    REQUIRE(lightIcon != nullptr);
    CHECK(lightIcon->pathName() == "Engine.S_Light");
    CHECK(ut::editorviews::actorIcon(boxClass->defaultObject) == nullptr);
}
