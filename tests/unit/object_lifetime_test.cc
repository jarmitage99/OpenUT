#include "audio/audio.h"
#include "compiler/script_compiler.h"
#include "core/config.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/resources.h"
#include "engine/props_engine.h"
#include "engine/world.h"
#include "render/scene_renderer.h"

#include "offscreen_context.h"
#include "test_world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_set>
#include <vector>

using namespace testworld;

namespace {
    ut::UObject** listHolding(ut::UObject* target)
    {
        ut::UClass* listClass = ut::objects::findClass("UWindow.UWindowList");
        REQUIRE(listClass != nullptr);
        ut::UObject* list = ut::objects::newObject(listClass, ut::objects::packageObject(ut::Name("Transient")), ut::Name(), 0);
        ut::UObject** next = reinterpret_cast<ut::UObject**>(list->propertyAddress(ut::Name("Next")));
        REQUIRE(next != nullptr);
        *next = target;
        return next;
    }

    void drawFrom(ut::SceneRenderer& renderer, ut::World& world, const ut::Vector& eye)
    {
        renderer.setTime(1.0);
        renderer.beginFrame(640, 480);
        renderer.drawScene(world.level, world.actors, eye, ut::Rotator { -16384, 0, 0 }, 90.0f, world.player());
        renderer.endFrame();
    }
}

TEST_CASE("a destroyed actor gives up its own property data after the end-of-tick cleanup", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    world.tick(1.0f / 30.0f);
    ut::UObject* box = spawnBox(world, openSpot(world, 200.0f) + ut::Vector { 0, 0, 30 });
    REQUIRE(box != nullptr);
    float standard = ut::actor::DrawScale(box->cls->defaultObject);
    REQUIRE(standard != 7.0f);
    ut::actor::DrawScale(box) = 7.0f;
    world.destroy(box);
    CHECK(ut::actor::DrawScale(box) == 7.0f);
    CHECK_FALSE(box->released);
    world.tick(1.0f / 30.0f);
    CHECK(box->released);
    CHECK(box->destroyed);
    CHECK(ut::actor::DrawScale(box) == standard);
}

TEST_CASE("the renderer forgets the caches of actors that were destroyed", "[data]")
{
    testgl::OffscreenContext gl;
    REQUIRE(gl.context != nullptr);
    Deck deck;
    ut::World& world = deck.world;
    world.tick(1.0f / 30.0f);
    ut::Vector floor = openSpot(world, 300.0f);
    ut::Vector eye = floor + ut::Vector { 0, 0, 260 };
    ut::SceneRenderer renderer;
    drawFrom(renderer, world, eye);
    drawFrom(renderer, world, eye);
    size_t without = renderer.actorCacheEntries();
    ut::UObject* box = spawnBox(world, floor + ut::Vector { 0, 0, 64 });
    REQUIRE(box != nullptr);
    drawFrom(renderer, world, eye);
    size_t with = renderer.actorCacheEntries();
    CHECK(with > without);
    world.destroy(box);
    world.tick(1.0f / 30.0f);
    drawFrom(renderer, world, eye);
    CHECK(renderer.actorCacheEntries() == without);
    CHECK(glGetError() == GL_NO_ERROR);
}

TEST_CASE("collecting an unloaded map deletes its objects and clears references to them", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::Name map("DM-Codex");
    ut::objects::unloadPackage(map);
    ut::objects::collectPackage(map);
    std::unordered_set<ut::UObject*> levelObjects;
    ut::UObject** heldPlayer = nullptr;
    ut::UObject** heldDead = nullptr;
    std::filesystem::path logFile = ut::config::userDirectory() / "Logs" / "collect.ngLog.tmp";
    std::filesystem::remove(logFile);
    REQUIRE(ut::audio::initializeOffline(44100));
    {
        ut::World world;
        REQUIRE(world.loadMap("DM-Codex?game=Botpack.DeathMatchPlus"));
        world.tick(1.0f / 30.0f);
        ut::UObject* box = spawnBox(world, openSpot(world, 80.0f) + ut::Vector { 0, 0, 30 });
        REQUIRE(box != nullptr);
        heldPlayer = listHolding(world.player());
        heldDead = listHolding(box);
        world.destroy(box);
        world.tick(1.0f / 30.0f);
        CHECK(*heldDead == nullptr);
        *heldDead = box;
        ut::UObject* package = world.level->outermost();
        ut::USound* ambience = ut::cast<ut::USound>(ut::objects::loadObject("AmbCity.Ambh01a", nullptr));
        REQUIRE(ambience != nullptr);
        ut::objects::ensureLoaded(ambience);
        ut::USound* levelSound = ut::cast<ut::USound>(ut::objects::newObject(ut::objects::findClass("Engine.Sound"), package, ut::Name("CollectedSound"), 0));
        REQUIRE(levelSound != nullptr);
        levelSound->bytes = ambience->bytes;
        REQUIRE(ut::audio::playSound(ambience, world.player(), ut::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, false) != 0);
        REQUIRE(ut::audio::playSound(levelSound, nullptr, ut::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, false) != 0);
        ut::audio::update({}, {}, 0.0f);
        ut::UObject* log = world.spawn(ut::objects::findClass("Engine.StatLogFile"), nullptr, ut::Name(), {}, {}, nullptr, true);
        REQUIRE(log != nullptr);
        *reinterpret_cast<std::string*>(log->propertyAddress(ut::Name("StatLogFile"))) = "../Logs/collect.ngLog.tmp";
        ut::vm::Call(log, ut::Name("OpenLog")).run();
        ut::vm::Call(log, ut::Name("FileLog")).s("kept").run();
        for (ut::UObject* object : ut::objects::all()) {
            if (object && object->outermost() == package) {
                levelObjects.insert(object);
            }
        }
        REQUIRE(levelObjects.count(world.player()) == 1);
        REQUIRE(levelObjects.count(box) == 1);
        REQUIRE(levelObjects.count(world.level) == 1);
        REQUIRE(levelObjects.count(levelSound) == 1);
        REQUIRE(levelObjects.count(log) == 1);
        ut::objects::unloadPackage(map);
    }
    CHECK(ut::objects::collectPackage(map) == levelObjects.size());
    CHECK(*heldPlayer == nullptr);
    CHECK(*heldDead == nullptr);
    size_t remaining = 0;
    for (ut::UObject* object : ut::objects::all()) {
        remaining += levelObjects.count(object);
    }
    CHECK(remaining == 0);
    std::vector<ut::audio::VoiceStatus> voices = ut::audio::voices();
    REQUIRE(voices.size() == 2);
    for (const ut::audio::VoiceStatus& voice : voices) {
        CHECK(levelObjects.count(const_cast<ut::UObject*>(voice.actor)) == 0);
        CHECK(levelObjects.count(const_cast<ut::USound*>(voice.sound)) == 0);
    }
    ut::audio::shutdown();
    std::ifstream in(logFile, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text == "kept\r\n");
    CHECK(ut::objects::collectPackage(map) == 0);

    ut::World again;
    REQUIRE(again.loadMap("DM-Codex?game=Botpack.DeathMatchPlus"));
    for (int i = 0; i < 60; i++) {
        again.tick(1.0f / 30.0f);
    }
    CHECK(again.player() != nullptr);
    CHECK_FALSE(again.player()->destroyed);
    ut::objects::unloadPackage(map);
}

namespace {
    ut::UClass* compileInto(ut::UObject* package, const std::string& className, const std::string& text)
    {
        std::string log;
        ut::compiler::PackageCompiler compiler(package, [&](const std::string& line) {
            log += line + "\n";
        });
        ut::UClass* cls = compiler.importClass(className + ".uc", text);
        REQUIRE(cls != nullptr);
        bool compiled = compiler.compile();
        INFO(log);
        REQUIRE(compiled);
        REQUIRE((cls->classFlags & ut::CLASS_Compiled) != 0);
        return cls;
    }

    int32_t tally(ut::UObject* probe)
    {
        ut::vm::Call call(probe, ut::Name("Tally"));
        REQUIRE(call.valid());
        call.run();
        return *reinterpret_cast<int32_t*>(call.returnValue());
    }

    const std::string probeSource = "class CollectProbe extends Actor;\r\n"
                                    "\r\n"
                                    "enum EProbeMode\r\n{\r\n    PM_Idle,\r\n    PM_Busy\r\n};\r\n"
                                    "\r\n"
                                    "struct ProbeEntry\r\n{\r\n    var int Count;\r\n    var string Label;\r\n};\r\n"
                                    "\r\n"
                                    "var EProbeMode Mode;\r\n"
                                    "var ProbeEntry Entries[2];\r\n"
                                    "var string Note;\r\n"
                                    "var array<string> Lines;\r\n"
                                    "\r\n"
                                    "function int Tally()\r\n{\r\n    Entries[0].Count++;\r\n    Entries[0].Label = Note;\r\n    return Entries[0].Count;\r\n}\r\n"
                                    "\r\n"
                                    "state Waiting\r\n{\r\n    function int Tally()\r\n    {\r\n        return -1;\r\n    }\r\n}\r\n"
                                    "\r\n"
                                    "defaultproperties\r\n{\r\n    Note=\"probe\"\r\n}\r\n";

    const std::string probeListSource = "class CollectProbeList extends UWindowList;\r\n"
                                        "var string Caption;\r\n";
}

TEST_CASE("collecting an unloaded map also deletes the script classes it defined and everything made from them", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::Name map("DM-Codex");
    ut::objects::unloadPackage(map);
    ut::objects::collectPackage(map);
    ut::UClass* controlClass = ut::objects::findClass("UWindow.UWindowListControl");
    REQUIRE(controlClass != nullptr);
    ut::UObject* transient = ut::objects::packageObject(ut::Name("Transient"));
    ut::UObject* control = ut::objects::newObject(controlClass, transient, ut::Name(), 0);
    ut::UObject** listClass = reinterpret_cast<ut::UObject**>(control->propertyAddress(ut::Name("ListClass")));
    REQUIRE(listClass != nullptr);
    std::unordered_set<ut::UObject*> doomed;
    ut::UObject** heldClass = nullptr;
    ut::UObject* stray = nullptr;
    {
        ut::World world;
        REQUIRE(world.loadMap("DM-Codex?game=Botpack.DeathMatchPlus"));
        world.tick(1.0f / 30.0f);
        ut::UObject* package = world.level->outermost();
        ut::UClass* probeClass = compileInto(package, "CollectProbe", probeSource);
        ut::UClass* probeListClass = compileInto(package, "CollectProbeList", probeListSource);
        ut::UObject* probe = world.spawn(probeClass, nullptr, ut::Name(), openSpot(world, 80.0f) + ut::Vector { 0, 0, 30 }, {}, nullptr, true);
        REQUIRE(probe != nullptr);
        CHECK(tally(probe) == 1);
        CHECK(tally(probe) == 2);
        stray = ut::objects::newObject(probeListClass, transient, ut::Name(), 0);
        *listClass = probeListClass;
        heldClass = listHolding(probeClass);
        world.tick(1.0f / 30.0f);
        for (ut::UObject* object : ut::objects::all()) {
            if (object && object->outermost() == package) {
                doomed.insert(object);
            }
        }
        REQUIRE(doomed.count(probeClass) == 1);
        REQUIRE(doomed.count(probeClass->defaultObject) == 1);
        REQUIRE(doomed.count(probeClass->findFunction(ut::Name("Tally"))) == 1);
        REQUIRE(doomed.count(probeClass->findState(ut::Name("Waiting"))) == 1);
        REQUIRE(doomed.count(probe) == 1);
        REQUIRE(doomed.count(stray) == 0);
        doomed.insert(stray);
        ut::objects::unloadPackage(map);
    }
    CHECK(ut::objects::collectPackage(map) == doomed.size());
    CHECK(*listClass == nullptr);
    CHECK(*heldClass == nullptr);
    size_t remaining = 0;
    for (ut::UObject* object : ut::objects::all()) {
        remaining += doomed.count(object);
    }
    CHECK(remaining == 0);
    CHECK(ut::objects::findClass("MyLevel.CollectProbe") == nullptr);
    CHECK(ut::objects::findClass("DM-Codex.CollectProbe") == nullptr);

    ut::World again;
    REQUIRE(again.loadMap("DM-Codex?game=Botpack.DeathMatchPlus"));
    ut::UClass* probeClass = compileInto(again.level->outermost(), "CollectProbe", probeSource);
    ut::UObject* probe = again.spawn(probeClass, nullptr, ut::Name(), openSpot(again, 80.0f) + ut::Vector { 0, 0, 30 }, {}, nullptr, true);
    REQUIRE(probe != nullptr);
    CHECK(tally(probe) == 1);
    for (int i = 0; i < 30; i++) {
        again.tick(1.0f / 30.0f);
    }
    CHECK_FALSE(again.player()->destroyed);
    ut::objects::unloadPackage(map);
    ut::objects::collectPackage(map);
    control->destroyed = true;
}

namespace {
    const std::string churnSource = "class ChurnProbe extends Actor;\r\n"
                                    "\r\n"
                                    "var Object Kept;\r\n"
                                    "var Object Inner;\r\n"
                                    "\r\n"
                                    "function Churn(int Count)\r\n"
                                    "{\r\n"
                                    "    local int i;\r\n"
                                    "    local UWindowList L;\r\n"
                                    "    for (i = 0; i < Count; i++)\r\n"
                                    "        L = new class'UWindowList';\r\n"
                                    "    Kept = new class'UWindowList';\r\n"
                                    "    UWindowList(Kept).Next = new class'UWindowList';\r\n"
                                    "    L = new class'UWindowList';\r\n"
                                    "    Inner = new(L) class'UWindowList';\r\n"
                                    "}\r\n";
}

TEST_CASE("objects scripts create and stop referencing are freed once enough of them pile up", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    world.tick(1.0f / 30.0f);
    ut::UClass* probeClass = compileInto(world.level->outermost(), "ChurnProbe", churnSource);
    ut::UObject* probe = world.spawn(probeClass, nullptr, ut::Name(), openSpot(world, 80.0f) + ut::Vector { 0, 0, 30 }, {}, nullptr, true);
    REQUIRE(probe != nullptr);
    ut::objects::collectScriptObjects();
    int32_t churn = static_cast<int32_t>(std::max<size_t>(5000, ut::objects::trackedScriptObjects() + 100));
    std::unordered_set<ut::UObject*> before(ut::objects::all().begin(), ut::objects::all().end());
    ut::vm::Call(probe, ut::Name("Churn")).i(churn).run();
    std::unordered_set<ut::UObject*> created;
    for (ut::UObject* object : ut::objects::all()) {
        if (object && !before.count(object)) {
            created.insert(object);
        }
    }
    REQUIRE(created.size() == static_cast<size_t>(churn) + 4);
    ut::UObject* kept = *reinterpret_cast<ut::UObject**>(probe->propertyAddress(ut::Name("Kept")));
    ut::UObject* inner = *reinterpret_cast<ut::UObject**>(probe->propertyAddress(ut::Name("Inner")));
    REQUIRE(kept != nullptr);
    REQUIRE(inner != nullptr);
    ut::UObject* next = *reinterpret_cast<ut::UObject**>(kept->propertyAddress(ut::Name("Next")));
    ut::UObject* holder = inner->outer;
    REQUIRE(next != nullptr);
    REQUIRE(created.count(holder) == 1);
    world.tick(1.0f / 30.0f);
    std::unordered_set<ut::UObject*> survivors;
    for (ut::UObject* object : ut::objects::all()) {
        if (object && created.count(object)) {
            survivors.insert(object);
        }
    }
    CHECK(survivors == std::unordered_set<ut::UObject*> { kept, next, holder, inner });
    CHECK(*reinterpret_cast<ut::UObject**>(kept->propertyAddress(ut::Name("Next"))) == next);
    CHECK(inner->outer == holder);
    CHECK(ut::objects::collectScriptObjects() == 0);
}

TEST_CASE("a world forgets the script objects that were freed while it was running", "[data]")
{
    Deck deck;
    ut::World& world = deck.world;
    world.tick(1.0f / 30.0f);
    ut::UClass* probeClass = compileInto(world.level->outermost(), "ChurnProbe", churnSource);
    ut::UObject* probe = world.spawn(probeClass, nullptr, ut::Name(), openSpot(world, 80.0f) + ut::Vector { 0, 0, 30 }, {}, nullptr, true);
    REQUIRE(probe != nullptr);
    ut::objects::collectScriptObjects();
    std::unordered_set<ut::UObject*> before(ut::objects::all().begin(), ut::objects::all().end());
    ut::vm::Call(probe, ut::Name("Churn")).i(500).run();
    std::unordered_set<ut::UObject*> created;
    for (ut::UObject* object : ut::objects::all()) {
        if (object && !before.count(object)) {
            created.insert(object);
        }
    }
    ut::UObject* box = spawnBox(world, openSpot(world, 200.0f) + ut::Vector { 0, 0, 30 });
    REQUIRE(box != nullptr);
    world.destroy(box);
    world.tick(1.0f / 30.0f);
    size_t cached = 0;
    for (ut::UObject* object : world.runtimeObjects) {
        cached += created.count(object);
    }
    REQUIRE(cached == created.size());
    ut::UObject* kept = *reinterpret_cast<ut::UObject**>(probe->propertyAddress(ut::Name("Kept")));
    ut::UObject* inner = *reinterpret_cast<ut::UObject**>(probe->propertyAddress(ut::Name("Inner")));
    ut::UObject* next = *reinterpret_cast<ut::UObject**>(kept->propertyAddress(ut::Name("Next")));
    std::unordered_set<ut::UObject*> survivors { kept, next, inner, inner->outer };
    CHECK(ut::objects::collectScriptObjects() == created.size() - survivors.size());
    size_t stale = 0;
    for (ut::UObject* object : world.runtimeObjects) {
        stale += created.count(object) && !survivors.count(object);
    }
    CHECK(stale == 0);
    ut::UObject* another = spawnBox(world, openSpot(world, 300.0f) + ut::Vector { 0, 0, 30 });
    REQUIRE(another != nullptr);
    world.destroy(another);
    world.tick(1.0f / 30.0f);
    CHECK(another->released);
}
