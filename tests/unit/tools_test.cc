#include "core/objects.h"
#include "editor/log_capture.h"
#include "engine/actor.h"
#include "engine/actor_search.h"
#include "engine/csg.h"
#include "engine/map_check.h"
#include "engine/property_edit.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {
    struct CheckRoom {
        ut::UObject* package = nullptr;
        ut::ULevel* level = nullptr;
        ut::UObject* builder = nullptr;
    };

    ut::UObject* spawn(CheckRoom& room, const char* className, const ut::Vector& location)
    {
        ut::UObject* actor = ut::objects::newObject(ut::objects::findClass(className), room.package, ut::Name(), 0);
        actor->loaded = true;
        ut::actor::Location(actor) = location;
        room.level->actors.push_back(actor);
        return actor;
    }

    CheckRoom makeCheckRoom(const char* packageName)
    {
        ut::engine::initialize(OPENUT_GAME_DATA);
        CheckRoom room;
        room.package = ut::objects::packageObject(ut::Name(packageName));
        room.level = ut::cast<ut::ULevel>(ut::objects::newObject(ut::csg::nativeClass("Level"), room.package, ut::Name(), 0));
        room.level->model = ut::cast<ut::UModel>(ut::objects::newObject(ut::csg::nativeClass("Model"), room.package, ut::Name(), 0));
        spawn(room, "Engine.LevelInfo", {});
        room.builder = spawn(room, "Engine.Brush", {});
        ut::UModel* builderModel = ut::cast<ut::UModel>(ut::objects::newObject(ut::csg::nativeClass("Model"), room.package, ut::Name(), 0));
        builderModel->polys = ut::cast<ut::UPolys>(ut::objects::newObject(ut::csg::nativeClass("Polys"), room.package, ut::Name(), 0));
        ut::actor::Brush(room.builder) = builderModel;
        ut::brush::CsgOper(room.builder) = ut::csg::CsgActive;
        ut::csg::buildCube(*builderModel->polys, 512.0f, 512.0f, 256.0f, nullptr);
        ut::csg::addBrush(room.level, room.builder, ut::csg::CsgSubtract);
        return room;
    }

    std::vector<ut::mapcheck::Issue> issuesFor(const std::vector<ut::mapcheck::Issue>& issues, ut::UObject* actor)
    {
        std::vector<ut::mapcheck::Issue> result;
        for (const ut::mapcheck::Issue& issue : issues) {
            if (issue.actor == actor) {
                result.push_back(issue);
            }
        }
        return result;
    }

    bool mentions(const std::vector<ut::mapcheck::Issue>& issues, ut::UObject* actor, const std::string& text)
    {
        for (const ut::mapcheck::Issue& issue : issuesFor(issues, actor)) {
            if (issue.message.find(text) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
}

TEST_CASE("map check reports a level without a PlayerStart as an error", "[data]")
{
    CheckRoom room = makeCheckRoom("MapCheckStartTest");
    ut::csg::rebuild(room.level);
    std::vector<ut::mapcheck::Issue> issues = ut::mapcheck::check(room.level);
    REQUIRE(mentions(issues, nullptr, "No PlayerStart"));
    CHECK(issuesFor(issues, nullptr).front().severity == ut::mapcheck::Severity::Error);
    spawn(room, "Engine.PlayerStart", { 0.0f, 0.0f, 0.0f });
    CHECK(ut::mapcheck::check(room.level).empty());
}

TEST_CASE("map check reports an Event that matches no Tag until an actor carries that Tag", "[data]")
{
    CheckRoom room = makeCheckRoom("MapCheckEventTest");
    spawn(room, "Engine.PlayerStart", {});
    ut::UObject* trigger = spawn(room, "Engine.Trigger", { 64.0f, 0.0f, 0.0f });
    ut::actor::Event(trigger) = ut::Name("OpenVault");
    ut::csg::rebuild(room.level);
    std::vector<ut::mapcheck::Issue> issues = ut::mapcheck::check(room.level);
    REQUIRE(issuesFor(issues, trigger).size() == 1);
    CHECK(issuesFor(issues, trigger)[0].message == "Event OpenVault matches the Tag of no actor");
    CHECK(issuesFor(issues, trigger)[0].severity == ut::mapcheck::Severity::Warning);
    ut::UObject* target = spawn(room, "Engine.Light", { -64.0f, 0.0f, 0.0f });
    ut::actor::Tag(target) = ut::Name("OpenVault");
    CHECK(issuesFor(ut::mapcheck::check(room.level), trigger).empty());
}

TEST_CASE("map check finds actors in solid space, empty brushes, dead lights, zone infos sharing a zone and untouchable triggers", "[data]")
{
    CheckRoom room = makeCheckRoom("MapCheckActorsTest");
    spawn(room, "Engine.PlayerStart", {});
    ut::UObject* inside = spawn(room, "Engine.Light", { 0.0f, 0.0f, 32.0f });
    ut::actor::LightType(inside) = 1;
    ut::actor::LightBrightness(inside) = 64;
    ut::actor::LightRadius(inside) = 16;
    ut::UObject* outside = spawn(room, "Engine.PlayerStart", { 4000.0f, 0.0f, 0.0f });
    ut::UObject* dark = spawn(room, "Engine.Light", { 32.0f, 0.0f, 0.0f });
    ut::actor::LightType(dark) = 1;
    ut::actor::LightBrightness(dark) = 0;
    ut::actor::LightRadius(dark) = 16;
    ut::UObject* zone = spawn(room, "Engine.ZoneInfo", { 0.0f, 32.0f, 0.0f });
    ut::UObject* sameZone = spawn(room, "Engine.ZoneInfo", { 0.0f, 64.0f, 0.0f });
    ut::UObject* trigger = spawn(room, "Engine.Trigger", { 0.0f, -32.0f, 0.0f });
    ut::actor::bCollideActors(trigger) = 0;
    ut::actor::Tag(trigger) = ut::Name("Relay");
    ut::csg::rebuild(room.level);
    ut::UObject* empty = spawn(room, "Engine.Brush", {});
    ut::UModel* emptyModel = ut::cast<ut::UModel>(ut::objects::newObject(ut::csg::nativeClass("Model"), room.package, ut::Name(), 0));
    emptyModel->polys = ut::cast<ut::UPolys>(ut::objects::newObject(ut::csg::nativeClass("Polys"), room.package, ut::Name(), 0));
    ut::actor::Brush(empty) = emptyModel;
    ut::brush::CsgOper(empty) = ut::csg::CsgAdd;
    std::vector<ut::mapcheck::Issue> issues = ut::mapcheck::check(room.level);
    CHECK(issuesFor(issues, inside).empty());
    CHECK(mentions(issues, outside, "outside the world"));
    CHECK(mentions(issues, dark, "LightBrightness is 0"));
    CHECK(issuesFor(issues, zone).empty());
    CHECK(mentions(issues, sameZone, "ZoneInfo has no effect"));
    CHECK(mentions(issues, trigger, "can never fire"));
    CHECK(mentions(issues, empty, "Brush has no polygons"));
    CHECK(issuesFor(issues, empty)[0].severity == ut::mapcheck::Severity::Error);
    CHECK(issuesFor(issues, room.builder).empty());
    CHECK(issuesFor(issues, room.level->actors[0]).empty());
    ut::UObject* relay = spawn(room, "Engine.Light", { 0.0f, 0.0f, -32.0f });
    ut::actor::Event(relay) = ut::Name("Relay");
    CHECK(issuesFor(ut::mapcheck::check(room.level), trigger).empty());
}

TEST_CASE("map check reports duplicate actor names and abstract actor classes", "[data]")
{
    CheckRoom room = makeCheckRoom("MapCheckNamesTest");
    spawn(room, "Engine.PlayerStart", {});
    ut::UObject* first = spawn(room, "Engine.Light", {});
    ut::UObject* second = spawn(room, "Engine.Light", { 16.0f, 0.0f, 0.0f });
    second->name = first->name;
    ut::UClass* info = ut::objects::findClass("Engine.Info");
    REQUIRE((info->classFlags & ut::CLASS_Abstract) != 0);
    ut::UObject* abstract = spawn(room, "Engine.Info", { 0.0f, 16.0f, 0.0f });
    ut::csg::rebuild(room.level);
    std::vector<ut::mapcheck::Issue> issues = ut::mapcheck::check(room.level);
    CHECK(issuesFor(issues, first).empty());
    CHECK(mentions(issues, second, "is used by more than one actor"));
    CHECK(mentions(issues, abstract, "Info is abstract"));
}

TEST_CASE("actor search matches name, class, tag and event and find next cycles through matches", "[data]")
{
    CheckRoom room = makeCheckRoom("ActorSearchTest");
    ut::UObject* lamp = spawn(room, "Engine.Light", {});
    ut::UObject* door = spawn(room, "Engine.Trigger", {});
    ut::UObject* bell = spawn(room, "Engine.Light", {});
    ut::actor::Tag(lamp) = ut::Name("HallLamp");
    ut::actor::Event(door) = ut::Name("OpenHall");
    ut::actor::Tag(bell) = ut::Name("Bell");
    std::vector<ut::UObject*> actors = { lamp, door, bell };
    CHECK(ut::actorsearch::find(actors, { "", "light", "", "" }) == std::vector<ut::UObject*> { lamp, bell });
    CHECK(ut::actorsearch::find(actors, { "", "", "hall", "" }) == std::vector<ut::UObject*> { lamp });
    CHECK(ut::actorsearch::find(actors, { "", "", "", "HALL" }) == std::vector<ut::UObject*> { door });
    CHECK(ut::actorsearch::find(actors, { door->name.str(), "", "", "" }) == std::vector<ut::UObject*> { door });
    CHECK(ut::actorsearch::find(actors, { "", "Light", "Bell", "" }) == std::vector<ut::UObject*> { bell });
    CHECK(ut::actorsearch::find(actors, { "", "Trigger", "Bell", "" }).empty());
    ut::actorsearch::Query lights { "", "Light", "", "" };
    CHECK(ut::actorsearch::findNext(actors, lights, nullptr) == lamp);
    CHECK(ut::actorsearch::findNext(actors, lights, lamp) == bell);
    CHECK(ut::actorsearch::findNext(actors, lights, bell) == lamp);
    CHECK(ut::actorsearch::findNext(actors, lights, door) == bell);
    CHECK(ut::actorsearch::findNext(actors, { "", "Mover", "", "" }, lamp) == nullptr);
}

TEST_CASE("the log capture records lines written to stderr until it stops")
{
    ut::editor::LogCapture capture;
    REQUIRE(capture.start());
    std::fprintf(stderr, "log capture marker one\npartial");
    std::fprintf(stderr, " line\n");
    std::vector<std::string> lines;
    for (int attempt = 0; attempt < 200; attempt++) {
        lines = capture.lines();
        if (lines.size() >= 2) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(lines == std::vector<std::string> { "log capture marker one", "partial line" });
    capture.stop();
    std::fprintf(stderr, "after the capture\n");
    capture.add("> command");
    CHECK(capture.lines() == std::vector<std::string> { "log capture marker one", "partial line", "> command" });
    capture.clear();
    CHECK(capture.lines().empty());
}
