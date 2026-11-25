#include "audio/audio.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"
#include "test_world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
    struct DeckWorld {
        ut::World world;

        DeckWorld()
        {
            ut::engine::initialize(OPENUT_GAME_DATA);
            ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
            REQUIRE(world.loadMap("DM-Deck16]["));
        }

        ~DeckWorld()
        {
            ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
        }
    };

    struct Bytecode {
        std::vector<uint8_t> code;

        Bytecode& byte(uint8_t value)
        {
            code.push_back(value);
            return *this;
        }

        Bytecode& word(uint32_t value, int bytes)
        {
            for (int i = 0; i < bytes; i++) {
                code.push_back(static_cast<uint8_t>(value >> (8 * i)));
            }
            return *this;
        }

        Bytecode& native(int32_t index)
        {
            if (index < 256) {
                return byte(static_cast<uint8_t>(index));
            }
            return byte(static_cast<uint8_t>(0x60 + (index >> 8))).byte(static_cast<uint8_t>(index & 255));
        }

        Bytecode& object(const ut::UObject* value)
        {
            return byte(0x20).word(value ? value->handle : 0, 4);
        }

        Bytecode& f(float value)
        {
            uint32_t bits;
            std::memcpy(&bits, &value, 4);
            return byte(0x1E).word(bits, 4);
        }

        Bytecode& v(const ut::Vector& value)
        {
            byte(0x23);
            for (float component : { value.x, value.y, value.z }) {
                uint32_t bits;
                std::memcpy(&bits, &component, 4);
                word(bits, 4);
            }
            return *this;
        }

        Bytecode& member(const ut::UObject* self, const char* name)
        {
            ut::UProperty* property = self->cls->findProperty(ut::Name(name));
            REQUIRE(property != nullptr);
            return byte(0x01).word(property->handle, 4);
        }

        Bytecode& omit()
        {
            return byte(0x0B);
        }

        Bytecode& end()
        {
            return byte(0x16);
        }
    };

    template <typename T>
    T runNative(ut::UObject* self, Bytecode& call)
    {
        ut::UStruct node;
        node.script = call.code;
        node.script.push_back(0x0B);
        ut::Frame frame;
        frame.object = self;
        frame.node = &node;
        frame.code = node.script.data();
        T result {};
        frame.step(self, &result);
        return result;
    }

    std::vector<ut::UObject*> recorded;
    ut::UObject* recordSpawnClass = nullptr;

    void recordTarget(ut::UObject* self, ut::Frame& stack, void*)
    {
        ut::vm::finish(stack);
        recorded.push_back(ut::actor::Target(self));
        if (recordSpawnClass) {
            ut::UClass* cls = ut::cast<ut::UClass>(recordSpawnClass);
            recordSpawnClass = nullptr;
            ut::World::current->spawn(cls, nullptr, ut::Name(), ut::actor::Location(self), {}, nullptr, true);
        }
    }

    std::vector<ut::UObject*> runIterator(ut::UObject* self, Bytecode& call)
    {
        ut::vm::registerNative(4095, recordTarget);
        recorded.clear();
        Bytecode script;
        script.byte(0x2F);
        script.code.insert(script.code.end(), call.code.begin(), call.code.end());
        size_t endAt = script.code.size() + 2 + 2 + 1 + 1;
        script.word(static_cast<uint32_t>(endAt), 2);
        script.native(4095).end().byte(0x31).byte(0x30);
        runNative<uint32_t>(self, script);
        return recorded;
    }

    bool contains(const std::vector<ut::UObject*>& list, const ut::UObject* item)
    {
        return std::find(list.begin(), list.end(), item) != list.end();
    }

    std::vector<ut::audio::VoiceStatus> voicesOf(const ut::UObject* actor)
    {
        std::vector<ut::audio::VoiceStatus> result;
        for (const ut::audio::VoiceStatus& voice : ut::audio::voices()) {
            if (voice.actor == actor) {
                result.push_back(voice);
            }
        }
        return result;
    }

    ut::USound* trafficSound()
    {
        ut::Linker* linker = ut::objects::loadPackage(ut::Name("AmbCity"));
        REQUIRE(linker != nullptr);
        int32_t ref = linker->findExport(ut::Name("traffic2"), ut::Name("Sound"), "*");
        REQUIRE(ref != 0);
        ut::USound* sound = ut::cast<ut::USound>(linker->object(ref));
        ut::objects::ensureLoaded(sound);
        ut::objects::flushLoads();
        REQUIRE(sound != nullptr);
        return sound;
    }

    void playDefaultSound(ut::UObject* source, ut::USound* sound)
    {
        Bytecode play;
        play.native(264).object(sound).end();
        runNative<uint32_t>(source, play);
    }
}

TEST_CASE("colour operators wrap each channel to a byte and keep the alpha of the colour operand", "[data]")
{
    DeckWorld island;
    ut::UObject* player = island.world.player();
    REQUIRE(player != nullptr);
    auto colorOp = [&](const char* name, auto&& fill) {
        ut::vm::Call call(player, ut::Name(name));
        REQUIRE(call.valid());
        fill(call);
        call.run();
        return *reinterpret_cast<ut::Color*>(call.returnValue());
    };
    auto setColor = [](ut::vm::Call& call, size_t parameter, ut::Color value) {
        *reinterpret_cast<ut::Color*>(call.parameter(parameter)) = value;
    };
    ut::Color scaled = colorOp("Multiply_ColorFloat", [&](ut::vm::Call& call) {
        setColor(call, 0, { 200, 100, 50, 77 });
        *reinterpret_cast<float*>(call.parameter(1)) = 2.0f;
    });
    CHECK(scaled.r == 144);
    CHECK(scaled.g == 200);
    CHECK(scaled.b == 100);
    CHECK(scaled.a == 77);
    ut::Color negative = colorOp("Multiply_FloatColor", [&](ut::vm::Call& call) {
        *reinterpret_cast<float*>(call.parameter(0)) = -1.0f;
        setColor(call, 1, { 10, 10, 10, 5 });
    });
    CHECK(negative.r == 246);
    CHECK(negative.a == 5);
    ut::Color sum = colorOp("Add_ColorColor", [&](ut::vm::Call& call) {
        setColor(call, 0, { 200, 10, 0, 100 });
        setColor(call, 1, { 100, 10, 0, 200 });
    });
    CHECK(sum.r == 44);
    CHECK(sum.g == 20);
    CHECK(sum.a == 100);
    ut::Color difference = colorOp("Subtract_ColorColor", [&](ut::vm::Call& call) {
        setColor(call, 0, { 10, 20, 30, 40 });
        setColor(call, 1, { 20, 10, 0, 50 });
    });
    CHECK(difference.r == 246);
    CHECK(difference.g == 10);
    CHECK(difference.b == 30);
    CHECK(difference.a == 40);
}

TEST_CASE("FastTrace only tests world geometry and ignores movers", "[data]")
{
    DeckWorld island;
    ut::UObject* player = island.world.player();
    REQUIRE(player != nullptr);
    ut::Vector start;
    ut::Vector end;
    bool found = false;
    for (ut::UObject* a : island.world.actors) {
        if (found || !a || a->destroyed || !ut::isMover(a)) {
            continue;
        }
        ut::Vector center = ut::actor::Location(a);
        for (const ut::Vector& axis : { ut::Vector { 96, 0, 0 }, ut::Vector { 0, 96, 0 } }) {
            for (float lift : { 0.0f, 32.0f, 64.0f }) {
                ut::Vector from = center - axis + ut::Vector { 0, 0, lift };
                ut::Vector to = center + axis + ut::Vector { 0, 0, lift };
                ut::CheckResult hit;
                if (!found && island.world.fastLineCheck(to, from) && island.world.lineCheck(hit, from, to, {}, nullptr, ut::TRACE_World) && hit.actor == a) {
                    start = from;
                    end = to;
                    found = true;
                }
            }
        }
    }
    REQUIRE(found);
    ut::vm::Call call(player, ut::Name("FastTrace"));
    call.v(end).v(start).run();
    CHECK(*reinterpret_cast<uint32_t*>(call.returnValue()) != 0);
}

TEST_CASE("Canvas natives follow the original cursor, size and hiding rules", "[data]")
{
    DeckWorld island;
    ut::UClass* canvasClass = ut::objects::findClass("Engine.Canvas");
    ut::UObject* font = ut::objects::loadObject("Engine.SmallFont", nullptr);
    ut::objects::flushLoads();
    REQUIRE(canvasClass != nullptr);
    REQUIRE(ut::cast<ut::UFont>(font) != nullptr);
    ut::UObject* canvas = ut::objects::newObject(canvasClass, ut::objects::packageObject(ut::Name("Transient")), ut::Name(), 0);
    ut::canvas::Font(canvas) = font;
    ut::canvas::ClipX(canvas) = 640;
    ut::canvas::ClipY(canvas) = 480;
    ut::canvas::SpaceX(canvas) = 3;
    ut::UObject* texture = island.world.level->model->surfs[0].texture;
    REQUIRE(texture != nullptr);

    ut::canvas::Style(canvas) = 0;
    ut::canvas::CurX(canvas) = 5;
    ut::canvas::CurYL(canvas) = 0;
    ut::vm::Call(canvas, ut::Name("DrawText")).s("Hello").b(false).run();
    CHECK(ut::canvas::CurX(canvas) == 5.0f);
    CHECK(ut::canvas::CurYL(canvas) == 0.0f);

    ut::canvas::Style(canvas) = 1;
    ut::vm::Call(canvas, ut::Name("DrawTile")).obj(nullptr).f(32).f(16).f(0).f(0).f(32).f(16).run();
    CHECK(ut::canvas::CurX(canvas) == 5.0f);

    ut::canvas::CurX(canvas) = -10;
    ut::canvas::CurY(canvas) = 4;
    ut::vm::Call(canvas, ut::Name("DrawTileClipped")).obj(texture).f(50).f(20).f(0).f(0).f(50).f(20).run();
    CHECK(ut::canvas::CurX(canvas) == 43.0f);
    CHECK(ut::canvas::CurYL(canvas) == 20.0f);

    auto textSize = [&](const char* text) {
        ut::vm::Call call(canvas, ut::Name("TextSize"));
        call.s(text).f(-1).f(-1).run();
        return std::make_pair(*reinterpret_cast<float*>(call.parameter(1)), *reinterpret_cast<float*>(call.parameter(2)));
    };
    auto empty = textSize("");
    CHECK(empty.first == 0.0f);
    CHECK(empty.second == 0.0f);
    const ut::FontCharacter* a = ut::cast<ut::UFont>(font)->glyph('A', nullptr);
    const ut::FontCharacter* b = ut::cast<ut::UFont>(font)->glyph('B', nullptr);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    auto ab = textSize("AB");
    CHECK(ab.first == static_cast<float>(a->width + b->width));

    ut::canvas::CurX(canvas) = 10.7f;
    ut::canvas::CurY(canvas) = 3.9f;
    ut::vm::Call(canvas, ut::Name("StrLen")).s("Hi").f(0).f(0).run();
    CHECK(ut::canvas::CurX(canvas) == 10.0f);
    CHECK(ut::canvas::CurY(canvas) == 3.0f);

    ut::UObject* player = island.world.player();
    REQUIRE(player != nullptr);
    ut::actor::bHidden(player) = 0;
    ut::vm::Call(canvas, ut::Name("DrawActor")).obj(player).b(false).b(false).run();
    CHECK(ut::actor::bHidden(player) != 0);
}

TEST_CASE("Warp and UnWarp transform location, velocity and rotation by the warp coordinates", "[data]")
{
    DeckWorld island;
    ut::UClass* warpClass = ut::objects::findClass("Engine.WarpZoneInfo");
    ut::objects::flushLoads();
    REQUIRE(warpClass != nullptr);
    ut::UObject* warp = ut::objects::newObject(warpClass, ut::objects::packageObject(ut::Name("Transient")), ut::Name(), 0);
    ut::warpzoneinfo::WarpCoords(warp) = { { 100, 0, 0 }, { 0, 1, 0 }, { -1, 0, 0 }, { 0, 0, 1 } };
    ut::actor::Location(warp) = { 10, 0, 0 };
    ut::actor::Velocity(warp) = { 1, 0, 0 };
    ut::actor::Rotation(warp) = {};
    Bytecode warpCall;
    warpCall.native(314).member(warp, "Location").member(warp, "Velocity").member(warp, "Rotation").end();
    runNative<uint32_t>(warp, warpCall);
    CHECK(ut::actor::Location(warp).x == Catch::Approx(100.0f));
    CHECK(ut::actor::Location(warp).y == Catch::Approx(10.0f));
    CHECK(ut::actor::Velocity(warp).x == Catch::Approx(0.0f).margin(1e-5));
    CHECK(ut::actor::Velocity(warp).y == Catch::Approx(1.0f));
    CHECK(std::abs(ut::actor::Rotation(warp).yaw - 16384) <= 4);
    Bytecode unwarpCall;
    unwarpCall.native(315).member(warp, "Location").member(warp, "Velocity").member(warp, "Rotation").end();
    runNative<uint32_t>(warp, unwarpCall);
    CHECK(ut::actor::Location(warp).x == Catch::Approx(10.0f));
    CHECK(ut::actor::Location(warp).y == Catch::Approx(0.0f).margin(1e-4));
    CHECK(ut::actor::Velocity(warp).x == Catch::Approx(1.0f));
    CHECK(std::abs(ut::actor::Rotation(warp).yaw) <= 4);
}

using namespace testworld;

TEST_CASE("PlaySound defaults to SLOT_Misc and an unset sound radius of 800 is heard within 800 over root 1.3", "[data]")
{
    Deck deck;
    REQUIRE(ut::audio::initializeOffline(44100));
    auto [origin, direction] = clearLine(deck.world, 720.0f);
    ut::UObject* nearBox = spawnBox(deck.world, origin + direction * 690.0f);
    ut::UObject* farBox = spawnBox(deck.world, origin + direction * 712.0f);
    ut::actor::Location(deck.world.player()) = origin;
    ut::playerpawn::ViewTarget(deck.world.player()) = nullptr;
    REQUIRE(ut::actor::TransientSoundRadius(nearBox) == 0.0f);
    ut::audio::update(origin, {}, 0.0f);
    ut::USound* sound = trafficSound();
    playDefaultSound(nearBox, sound);
    playDefaultSound(farBox, sound);
    std::vector<ut::audio::VoiceStatus> heard = voicesOf(nearBox);
    REQUIRE(heard.size() == 1);
    CHECK(heard[0].slot == 1);
    CHECK(voicesOf(farBox).empty());
    ut::audio::shutdown();
}

TEST_CASE("an obstructed PlaySound is heard within 0.6 of the hearing range at 0.35 volume", "[data]")
{
    Deck deck;
    REQUIRE(ut::audio::initializeOffline(44100));
    ut::Vector origin = openSpot(deck.world, 100.0f) + ut::Vector { 0, 0, 50 };
    ut::Vector inside = origin + ut::Vector { 0, 0, -530 };
    ut::Vector outside = origin + ut::Vector { 0, 0, -560 };
    REQUIRE_FALSE(deck.world.fastLineCheck(inside, origin));
    REQUIRE_FALSE(deck.world.fastLineCheck(outside, origin));
    ut::UObject* insideBox = spawnBox(deck.world, inside);
    ut::UObject* outsideBox = spawnBox(deck.world, outside);
    ut::UObject* reference = spawnBox(deck.world, inside);
    ut::actor::Location(deck.world.player()) = origin;
    ut::playerpawn::ViewTarget(deck.world.player()) = nullptr;
    ut::actor::Location(insideBox) = inside;
    ut::actor::Location(outsideBox) = outside;
    ut::actor::Location(reference) = inside;
    ut::audio::update(origin, {}, 0.0f);
    ut::USound* sound = trafficSound();
    playDefaultSound(insideBox, sound);
    playDefaultSound(outsideBox, sound);
    CHECK(voicesOf(outsideBox).empty());
    std::vector<ut::audio::VoiceStatus> heard = voicesOf(insideBox);
    REQUIRE(heard.size() == 1);
    REQUIRE(ut::audio::playSound(sound, reference, 1, inside, 0.35f, 800.0f, 1.0f, true) != 0);
    std::vector<ut::audio::VoiceStatus> expected = voicesOf(reference);
    REQUIRE(expected.size() == 1);
    CHECK(heard[0].volume == expected[0].volume);
    CHECK(heard[0].volume > 0);
    ut::audio::shutdown();
}

TEST_CASE("RadiusActors adds the collision radius and ChildActors walks the whole owner chain including itself", "[data]")
{
    Deck deck;
    ut::Vector origin = openSpot(deck.world, 200.0f) + ut::Vector { 0, 0, 1000 };
    ut::UObject* center = spawnBox(deck.world, origin);
    ut::UObject* inside = spawnBox(deck.world, origin + ut::Vector { 125, 0, 0 });
    ut::UObject* outside = spawnBox(deck.world, origin + ut::Vector { 0, 135, 0 });
    for (ut::UObject* box : { center, inside, outside }) {
        ut::actor::Physics(box) = ut::PHYS_None;
    }
    ut::actor::Location(center) = origin;
    ut::actor::Location(inside) = origin + ut::Vector { 125, 0, 0 };
    ut::actor::Location(outside) = origin + ut::Vector { 0, 135, 0 };
    ut::actor::CollisionRadius(inside) = 30;
    ut::actor::CollisionRadius(outside) = 30;
    Bytecode radius;
    radius.native(310).object(center->cls).member(center, "Target").f(100.0f).end();
    std::vector<ut::UObject*> found = runIterator(center, radius);
    CHECK(contains(found, center));
    CHECK(contains(found, inside));
    CHECK_FALSE(contains(found, outside));

    ut::actor::Owner(inside) = center;
    ut::actor::Owner(outside) = inside;
    Bytecode children;
    children.native(305).object(center->cls).member(center, "Target").end();
    std::vector<ut::UObject*> owned = runIterator(center, children);
    REQUIRE(owned.size() == 3);
    CHECK(contains(owned, center));
    CHECK(contains(owned, inside));
    CHECK(contains(owned, outside));
}

TEST_CASE("VisibleActors includes the caller and VisibleCollidingActors limits an unset radius to 1000", "[data]")
{
    Deck deck;
    auto [origin, direction] = clearLine(deck.world, 1150.0f);
    ut::UObject* center = spawnBox(deck.world, origin);
    ut::UObject* nearBox = spawnBox(deck.world, origin + direction * 900.0f);
    ut::UObject* farBox = spawnBox(deck.world, origin + direction * 1100.0f);
    for (ut::UObject* a : { center, nearBox, farBox }) {
        REQUIRE(ut::actor::bCollideActors(a) != 0);
        deck.world.collisionHash.update(a);
    }
    Bytecode visible;
    visible.native(311).object(center->cls).member(center, "Target").f(100.0f).end();
    CHECK(contains(runIterator(center, visible), center));

    Bytecode colliding;
    colliding.native(312).object(center->cls).member(center, "Target").end();
    std::vector<ut::UObject*> found = runIterator(center, colliding);
    CHECK(contains(found, center));
    CHECK(contains(found, nearBox));
    CHECK_FALSE(contains(found, farBox));
}

TEST_CASE("AllActors visits actors spawned inside the loop", "[data]")
{
    Deck deck;
    ut::UObject* first = spawnBox(deck.world, openSpot(deck.world, 200.0f) + ut::Vector { 0, 0, 30 });
    size_t before = 0;
    for (ut::UObject* a : deck.world.actors) {
        if (a && !a->destroyed && a->cls == first->cls) {
            before++;
        }
    }
    recordSpawnClass = first->cls;
    Bytecode all;
    all.native(304).object(first->cls).member(first, "Target").end();
    CHECK(runIterator(first, all).size() == before + 1);
}

TEST_CASE("Trace defaults to tracing actors when the caller collides and a miss reports a zero hit location", "[data]")
{
    Deck deck;
    auto [origin, direction] = clearLine(deck.world, 450.0f);
    ut::Vector end = origin + direction * 400.0f;
    ut::UObject* tracer = spawnBox(deck.world, origin);
    ut::actor::Location(tracer) = origin;
    REQUIRE(ut::actor::bCollideActors(tracer) != 0);
    Bytecode miss;
    miss.native(277).member(tracer, "Velocity").member(tracer, "Acceleration").v(end).end();
    ut::actor::Velocity(tracer) = { 5, 5, 5 };
    CHECK(runNative<ut::UObject*>(tracer, miss) == nullptr);
    CHECK(ut::actor::Velocity(tracer).x == 0.0f);
    CHECK(ut::actor::Velocity(tracer).y == 0.0f);
    CHECK(ut::actor::Velocity(tracer).z == 0.0f);

    ut::UObject* target = spawnBox(deck.world, origin + direction * 200.0f);
    ut::actor::Location(target) = origin + direction * 200.0f;
    deck.world.collisionHash.update(target);
    Bytecode hit;
    hit.native(277).member(tracer, "Velocity").member(tracer, "Acceleration").v(end).end();
    CHECK(runNative<ut::UObject*>(tracer, hit) == target);
}

TEST_CASE("MoveSmooth clears bJustTeleported and Spawn without a tag leaves Tag None", "[data]")
{
    Deck deck;
    ut::UObject* player = deck.world.player();
    REQUIRE(player != nullptr);
    ut::actor::bJustTeleported(player) = 1;
    ut::vm::Call move(player, ut::Name("MoveSmooth"));
    move.v({ 0, 0, 0 }).run();
    CHECK(ut::actor::bJustTeleported(player) == 0);

    ut::UClass* boxClass = ut::objects::findClass("UnrealShare.WoodenBox");
    ut::objects::flushLoads();
    Bytecode spawn;
    spawn.native(278).object(boxClass).object(nullptr).omit().v(openSpot(deck.world, 200.0f) + ut::Vector { 0, 0, 30 }).end();
    ut::UObject* spawned = runNative<ut::UObject*>(player, spawn);
    REQUIRE(spawned != nullptr);
    CHECK(ut::actor::Tag(spawned).none());
}

TEST_CASE("MakeNoise needs an instigator, remembers the noise on it and sets the stimulus of pawns that hear it", "[data]")
{
    Deck deck;
    ut::UObject* player = deck.world.player();
    REQUIRE(player != nullptr);
    auto [origin, direction] = clearLine(deck.world, 350.0f);
    ut::UObject* listener = spawnNali(deck.world, origin + direction * 300.0f);
    REQUIRE(ut::vm::isProbing(listener, ut::Name("HearNoise")));
    ut::actor::Location(listener) = origin + direction * 300.0f;
    ut::pawn::Alertness(listener) = 0.4f;
    ut::pawn::HearingThreshold(listener) = 0.1f;
    ut::pawn::Stimulus(listener) = -1.0f;
    ut::UObject* silent = spawnBox(deck.world, origin);
    ut::actor::Location(silent) = origin;
    ut::actor::Instigator(silent) = nullptr;
    ut::vm::Call(silent, ut::Name("MakeNoise")).f(1.0f).run();
    CHECK(ut::pawn::Stimulus(listener) == -1.0f);

    ut::UObject* noisy = spawnBox(deck.world, origin);
    ut::actor::Location(noisy) = origin;
    ut::actor::Instigator(noisy) = player;
    ut::pawn::noise1time(player) = -10.0f;
    ut::pawn::noise2time(player) = -10.0f;
    ut::vm::Call(noisy, ut::Name("MakeNoise")).f(1.0f).run();
    CHECK(ut::pawn::Stimulus(listener) == Catch::Approx(2.2f));
    CHECK(ut::pawn::noise1time(player) == deck.world.timeSeconds);
    CHECK(ut::pawn::noise1loudness(player) == 1.0f);
    CHECK(ut::pawn::noise1spot(player).x == origin.x);

    ut::pawn::Stimulus(listener) = -1.0f;
    ut::vm::Call(noisy, ut::Name("MakeNoise")).f(1.0f).run();
    CHECK(ut::pawn::Stimulus(listener) == -1.0f);
}

TEST_CASE("PlayerCanSeeMe needs the actor within range and inside the player's view cone", "[data]")
{
    Deck deck;
    ut::UObject* player = deck.world.player();
    REQUIRE(player != nullptr);
    auto [origin, direction] = clearLine(deck.world, 1300.0f);
    ut::actor::Location(player) = origin;
    ut::pawn::ViewRotation(player) = ut::vectorRotation(direction);
    ut::pawn::bBehindView(player) = 0;
    ut::Vector side { -direction.y, direction.x, 0 };
    auto seen = [&](const ut::Vector& offset) {
        ut::UObject* target = spawnBox(deck.world, origin + offset);
        ut::actor::Location(target) = origin + offset;
        ut::actor::CollisionRadius(target) = 10;
        ut::vm::Call call(target, ut::Name("PlayerCanSeeMe"));
        call.run();
        return *reinterpret_cast<uint32_t*>(call.returnValue()) != 0;
    };
    CHECK(seen(direction * 300.0f));
    CHECK_FALSE(seen(direction * 1200.0f));
    CHECK_FALSE(seen(side * 200.0f));
}

TEST_CASE("ZoneActors includes actors in the level pseudo zone", "[data]")
{
    Deck deck;
    ut::UObject* zone = nullptr;
    for (ut::UObject* a : deck.world.actors) {
        if (!zone && a && !a->destroyed && a != deck.world.levelInfo && a->isA(ut::Name("ZoneInfo"))) {
            zone = a;
        }
    }
    REQUIRE(zone != nullptr);
    ut::UObject* loose = spawnBox(deck.world, openSpot(deck.world, 200.0f) + ut::Vector { 0, 0, 30 });
    ut::region(loose).zone = deck.world.levelInfo;
    Bytecode zoneActors;
    zoneActors.native(308).object(loose->cls).member(zone, "Target").end();
    CHECK(contains(runIterator(zone, zoneActors), loose));
}

TEST_CASE("UpdateURL edits the current URL, GetAddressURL reports host and port and ClientTravel needs a Player", "[data]")
{
    Deck deck;
    ut::UObject* player = deck.world.player();
    REQUIRE(player != nullptr);
    ut::vm::Call(player, ut::Name("UpdateURL")).s("Probe").s("One").b(false).run();
    ut::vm::Call(player, ut::Name("UpdateURL")).s("Probe").s("Two").b(false).run();
    const std::string& url = deck.world.url;
    CHECK(url.find("?Probe=Two") != std::string::npos);
    CHECK(url.find("Probe=One") == std::string::npos);
    CHECK(url.find("Probe=") == url.rfind("Probe="));

    ut::vm::Call address(deck.world.levelInfo, ut::Name("GetAddressURL"));
    address.run();
    CHECK(*reinterpret_cast<std::string*>(address.returnValue()) == ":7777");

    ut::UObject* viewport = ut::playerpawn::Player(player);
    ut::playerpawn::Player(player) = nullptr;
    ut::vm::Call(player, ut::Name("ClientTravel")).s("DM-Agony").byte(0).b(true).run();
    CHECK_FALSE(deck.world.travelPending);
    ut::playerpawn::Player(player) = viewport;
}

TEST_CASE("GetNextSkin cycles through the skins in the .int files that start with the mesh name", "[data]")
{
    DeckWorld island;
    ut::UObject* player = island.world.player();
    REQUIRE(player != nullptr);
    auto nextSkin = [&](const std::string& prefix, const std::string& current, int32_t direction) {
        ut::vm::Call call(player, ut::Name("GetNextSkin"));
        call.s(prefix).s(current).i(direction).s("").s("").run();
        return std::make_pair(*reinterpret_cast<std::string*>(call.parameter(3)), *reinterpret_cast<std::string*>(call.parameter(4)));
    };

    CHECK(nextSkin("Soldier", "None", 1) == std::make_pair(std::string("SoldierSkins.blkt1"), std::string("Marine")));
    CHECK(nextSkin("Soldier", "SoldierSkins.blkt1", 1) == std::make_pair(std::string("SoldierSkins.blkt2"), std::string()));
    CHECK(nextSkin("Soldier", "soldierskins.BLKT2", -1).first == "SoldierSkins.blkt1");
    CHECK(nextSkin("Soldier", "SoldierSkins.blkt1", -1).first == "SoldierSkins.hkil2t_3");
    CHECK(nextSkin("Soldier", "SoldierSkins.hkil2t_3", 1).first == "SoldierSkins.blkt1");
    CHECK(nextSkin("NoSuchMesh", "None", 1) == std::make_pair(std::string(), std::string()));

    std::string skin = "None";
    std::string first;
    int count = 0;
    bool malcom = false;
    while (count < 1000) {
        auto [name, description] = nextSkin("Soldier", skin, 1);
        if (name == first) {
            break;
        }
        if (first.empty()) {
            first = name;
        }
        CHECK(name.rfind("SoldierSkins.", 0) == 0);
        malcom = malcom || (name == "SoldierSkins.blkt4Malcom" && description == "Malcom");
        skin = name;
        count++;
    }
    CHECK(count == 98);
    CHECK(malcom);
}
