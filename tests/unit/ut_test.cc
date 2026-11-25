#include "core/config.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/canvas.h"
#include "engine/canvas_hooks.h"
#include "engine/icon.h"
#include "engine/mesh.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/scripted_texture.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {
    struct DeckWorld {
        ut::World world;

        DeckWorld()
        {
            ut::engine::initialize(OPENUT_GAME_DATA);
            ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
            REQUIRE(world.loadMap("DM-Deck16][?game=Botpack.DeathMatchPlus"));
        }

        ~DeckWorld()
        {
            ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
        }
    };

    struct Tile {
        float x = 0;
        float u = 0;
        float v = 0;
    };

    class RecordingCanvas : public ut::Canvas2D {
    public:
        float width() const override
        {
            return 640;
        }

        float height() const override
        {
            return 480;
        }

        void setClip(float, float, float, float) override
        {
        }

        void clearClip() override
        {
        }

        void drawTile(ut::UTexture*, float x, float, float, float, float u, float v, float, float, ut::LinearColor, ut::BlendStyle, bool) override
        {
            tiles.push_back({ x, u, v });
        }

        void drawRect(float, float, float, float, ut::LinearColor, ut::BlendStyle) override
        {
        }

        void drawLine(float, float, float, float, ut::LinearColor) override
        {
        }

        std::vector<Tile> tiles;
    };

    std::pair<std::string, std::string> nextIntDesc(ut::UObject* actor, const char* metaClass, int32_t index)
    {
        ut::vm::Call call(actor, ut::Name("GetNextIntDesc"));
        call.s(metaClass).i(index).s("").s("").run();
        return { *reinterpret_cast<std::string*>(call.parameter(2)), *reinterpret_cast<std::string*>(call.parameter(3)) };
    }
}

TEST_CASE("UT meshes decode packed 11 11 10 bit vertices that fill the stored bounds", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::UMesh* mesh = ut::cast<ut::UMesh>(ut::objects::loadObject("Botpack.Eightm", nullptr));
    ut::objects::flushLoads();
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->verts.size() == static_cast<size_t>(mesh->frameVerts) * static_cast<size_t>(mesh->animFrames));
    ut::Vector low = mesh->verts.front();
    ut::Vector high = mesh->verts.front();
    for (const ut::Vector& vertex : mesh->verts) {
        low = { std::min(low.x, vertex.x), std::min(low.y, vertex.y), std::min(low.z, vertex.z) };
        high = { std::max(high.x, vertex.x), std::max(high.y, vertex.y), std::max(high.z, vertex.z) };
    }
    CHECK(low.x == mesh->meshBounds.min.x);
    CHECK(low.y == mesh->meshBounds.min.y);
    CHECK(low.z == mesh->meshBounds.min.z);
    CHECK(high.x == mesh->meshBounds.max.x);
    CHECK(high.y == mesh->meshBounds.max.y);
    CHECK(high.z == mesh->meshBounds.max.z);
}

TEST_CASE("the game keeps its replication info because actors spawned before play get no events", "[data]")
{
    DeckWorld deck;
    ut::UObject* game = ut::levelinfo::Game(deck.world.levelInfo);
    REQUIRE(game != nullptr);
    CHECK(game->isA(ut::Name("DeathMatchPlus")));
    ut::UObject* replication = ut::gameinfo::GameReplicationInfo(game);
    REQUIRE(replication != nullptr);
    CHECK_FALSE(replication->destroyed);
    CHECK(ut::gameinfo::BaseMutator(game) != nullptr);
}

TEST_CASE("GetNextIntDesc lists the classes of a meta class from every .int file in order", "[data]")
{
    DeckWorld deck;
    ut::UObject* level = deck.world.levelInfo;
    const char* games[] = { "BotPack.DeathMatchPlus", "BotPack.TeamGamePlus", "BotPack.Assault", "BotPack.Domination", "BotPack.CTFGame", "BotPack.LastManStanding" };
    for (int32_t i = 0; i < 6; i++) {
        CHECK(nextIntDesc(level, "Botpack.TournamentGameInfo", i).first == games[i]);
    }
    CHECK(nextIntDesc(level, "Botpack.TournamentGameInfo", 6).first.empty());
    CHECK(nextIntDesc(level, "TournamentGameInfo", 4).first == "BotPack.CTFGame");
    CHECK(nextIntDesc(level, "TournamentGameInfo", 6).first.empty());
    CHECK(nextIntDesc(level, "Engine.TournamentGameInfo", 0).first.empty());
    std::pair<std::string, std::string> instagib = nextIntDesc(level, "Engine.Mutator", 0);
    CHECK(instagib.first == "BotPack.InstaGibDM");
    CHECK(instagib.second == "InstaGib,Combat with super high powered shock rifles.");
    ut::vm::Call call(level, ut::Name("GetNextInt"));
    call.s("Botpack.TournamentGameInfo").i(2).run();
    CHECK(*reinterpret_cast<std::string*>(call.returnValue()) == "BotPack.Assault");
}

TEST_CASE("CheckValidSkinPackage accepts packages whose textures are declared in .int files", "[data]")
{
    DeckWorld deck;
    ut::UObject* player = deck.world.player();
    REQUIRE(player != nullptr);
    auto valid = [&](const char* package) {
        ut::vm::Call call(player, ut::Name("CheckValidSkinPackage"));
        call.s(package).s("Soldier").run();
        return *reinterpret_cast<uint32_t*>(call.returnValue()) != 0;
    };
    CHECK(valid("SoldierSkins"));
    CHECK(valid("soldierskins"));
    CHECK_FALSE(valid("Soldier"));
    CHECK_FALSE(valid("Engine"));
}

TEST_CASE("RandRange stays between its bounds", "[data]")
{
    DeckWorld deck;
    for (int i = 0; i < 200; i++) {
        ut::vm::Call call(deck.world.levelInfo, ut::Name("RandRange"));
        call.f(2.0f).f(3.0f).run();
        float value = *reinterpret_cast<float*>(call.returnValue());
        REQUIRE(value >= 2.0f);
        REQUIRE(value <= 3.0f);
    }
}

TEST_CASE("DrawTextClipped hides the hotkey ampersand and underlines the next letter", "[data]")
{
    DeckWorld deck;
    ut::UObject* font = ut::objects::loadObject("Engine.SmallFont", nullptr);
    ut::objects::flushLoads();
    ut::UFont* smallFont = ut::cast<ut::UFont>(font);
    REQUIRE(smallFont != nullptr);
    ut::UObject* canvas = ut::objects::newObject(ut::objects::findClass("Engine.Canvas"), ut::objects::packageObject(ut::Name("Transient")), ut::Name(), 0);
    ut::canvas::Font(canvas) = font;
    ut::canvas::ClipX(canvas) = 640;
    ut::canvas::ClipY(canvas) = 480;
    ut::canvas::Style(canvas) = 1;
    ut::canvas::CurX(canvas) = 20;
    RecordingCanvas recorder;
    ut::Canvas2D* previous = ut::canvasHooks().target;
    ut::canvasHooks().target = &recorder;
    ut::vm::Call(canvas, ut::Name("DrawTextClipped")).s("A&B&&C").b(true).run();
    ut::canvasHooks().target = previous;
    auto glyph = [&](char c) {
        const ut::FontCharacter* character = smallFont->glyph(static_cast<uint8_t>(c), nullptr);
        REQUIRE(character != nullptr);
        return std::make_pair(static_cast<float>(character->u), static_cast<float>(character->v));
    };
    std::vector<std::pair<float, float>> drawn;
    for (const Tile& tile : recorder.tiles) {
        drawn.emplace_back(tile.u, tile.v);
    }
    std::vector<std::pair<float, float>> expected { glyph('A'), glyph('B'), glyph('&'), glyph('C'), glyph('_') };
    REQUIRE(drawn == expected);
    CHECK(recorder.tiles[4].x == recorder.tiles[1].x);
}

TEST_CASE("StatLogFile writes logged lines and renames the temporary log when closed", "[data]")
{
    DeckWorld deck;
    ut::UObject* log = deck.world.spawn(ut::objects::findClass("Engine.StatLogFile"), nullptr, ut::Name(), {}, {}, nullptr, true);
    REQUIRE(log != nullptr);
    *reinterpret_cast<std::string*>(log->propertyAddress(ut::Name("StatLogFile"))) = "../Logs/unit.ngLog.tmp";
    *reinterpret_cast<std::string*>(log->propertyAddress(ut::Name("StatLogFinal"))) = "../Logs/unit.ngLog.log";
    std::filesystem::path logs = ut::config::userDirectory() / "Logs";
    std::filesystem::remove(logs / "unit.ngLog.log");
    ut::vm::Call(log, ut::Name("OpenLog")).run();
    ut::vm::Call(log, ut::Name("FileLog")).s("first").run();
    ut::vm::Call(log, ut::Name("FileLog")).s("second").run();
    ut::vm::Call(log, ut::Name("FileFlush")).run();
    CHECK(std::filesystem::exists(logs / "unit.ngLog.tmp"));
    ut::vm::Call(log, ut::Name("CloseLog")).run();
    CHECK_FALSE(std::filesystem::exists(logs / "unit.ngLog.tmp"));
    std::ifstream in(logs / "unit.ngLog.log", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text == "first\r\nsecond\r\n");
}

TEST_CASE("every shipped texture, sound and music package loads without native serialization mismatches", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    size_t before = ut::objects::loadMismatches().size();
    size_t packages = 0;
    for (const char* directory : { "Textures", "Sounds", "Music" }) {
        for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(OPENUT_GAME_DATA) / directory)) {
            ut::Linker* linker = ut::objects::loadPackage(ut::Name(entry.path().stem().string()));
            INFO(entry.path().filename().string());
            REQUIRE(linker != nullptr);
            for (size_t i = 0; i < linker->file().exports().size(); i++) {
                ut::objects::ensureLoaded(linker->object(static_cast<int32_t>(i + 1)));
            }
            ut::objects::flushLoads();
            packages++;
        }
    }
    CHECK(packages > 0);
    const std::vector<std::string>& mismatches = ut::objects::loadMismatches();
    INFO("first mismatch: " << (mismatches.size() > before ? mismatches[before] : std::string()));
    CHECK(mismatches.size() == before);
}

TEST_CASE("ScriptedTexture DrawColoredText writes the palette entry of the colour under every glyph pixel", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::UTexture* texture = ut::cast<ut::UTexture>(ut::objects::loadObject("Scripted.MessageScroll2", nullptr));
    ut::UFont* font = ut::cast<ut::UFont>(ut::objects::loadObject("Engine.SmallFont", nullptr));
    ut::objects::flushLoads();
    REQUIRE(texture != nullptr);
    REQUIRE(font != nullptr);
    ut::scriptedtexture::NotifyActor(texture) = nullptr;
    CHECK_FALSE(ut::scriptedtexture::refresh(texture));
    ut::TextureMip& mip = texture->mips[0];
    REQUIRE(mip.data.size() == static_cast<size_t>(mip.width) * mip.height);
    std::fill(mip.data.begin(), mip.data.end(), uint8_t { 0 });
    const std::vector<uint32_t>& colors = texture->palette()->colors;
    size_t unique = 0;
    for (size_t i = 1; i < colors.size() && unique == 0; i++) {
        if (std::count(colors.begin(), colors.end(), colors[i]) == 1) {
            unique = i;
        }
    }
    REQUIRE(unique != 0);
    ut::Color color { static_cast<uint8_t>(colors[unique] & 0xff), static_cast<uint8_t>((colors[unique] >> 8) & 0xff), static_cast<uint8_t>((colors[unique] >> 16) & 0xff), 255 };
    ut::vm::Call call(texture, ut::Name("DrawColoredText"));
    call.f(10).f(5).s("A").obj(font).skip();
    *reinterpret_cast<ut::Color*>(call.parameter(4)) = color;
    call.run();
    ut::UTexture* page = nullptr;
    const ut::FontCharacter* glyph = font->glyph('A', &page);
    REQUIRE(glyph != nullptr);
    REQUIRE(page != nullptr);
    const ut::TextureMip& pageMip = page->mips[0];
    size_t lit = 0;
    for (int32_t row = 0; row < glyph->height; row++) {
        for (int32_t column = 0; column < glyph->width; column++) {
            uint8_t source = pageMip.data[static_cast<size_t>(glyph->v + row) * pageMip.width + static_cast<size_t>(glyph->u + column)];
            uint8_t drawn = mip.data[static_cast<size_t>(5 + row) * mip.width + static_cast<size_t>(10 + column)];
            CHECK(drawn == (source != 0 ? unique : 0));
            lit += source != 0 ? 1 : 0;
        }
    }
    CHECK(lit > 0);
    CHECK(std::count(mip.data.begin(), mip.data.end(), static_cast<uint8_t>(unique)) == static_cast<std::ptrdiff_t>(lit));
}

TEST_CASE("a ScriptedTexture redraws through its NotifyActor once per level time", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Morpheus"));
    ut::World world;
    REQUIRE(world.loadMap("DM-Morpheus?game=Botpack.DeathMatchPlus"));
    ut::UTexture* texture = ut::cast<ut::UTexture>(ut::objects::findObject("Scripted.MessageScroll2", nullptr));
    REQUIRE(texture != nullptr);
    ut::UObject* notify = ut::scriptedtexture::NotifyActor(texture);
    REQUIRE(notify != nullptr);
    CHECK(notify->isA(ut::Name("ScrollingMessageTexture")));
    for (int i = 0; i < 20; i++) {
        world.tick(0.05f);
    }
    CHECK(ut::scriptedtexture::refresh(texture));
    CHECK(ut::scriptedtexture::LocalTime(texture) == ut::levelinfo::TimeSeconds(world.levelInfo));
    CHECK_FALSE(ut::scriptedtexture::refresh(texture));
    int32_t position = *reinterpret_cast<int32_t*>(notify->propertyAddress(ut::Name("Position")));
    CHECK(position == static_cast<int32_t>(texture->width()));
    world.tick(0.5f);
    CHECK(ut::scriptedtexture::refresh(texture));
    CHECK(*reinterpret_cast<int32_t*>(notify->propertyAddress(ut::Name("Position"))) < position);
    ut::objects::unloadPackage(ut::Name("DM-Morpheus"));
}

TEST_CASE("icons decode the palette, the AND mask and the bottom up rows of a 4 bit image")
{
    std::vector<uint8_t> bytes(6 + 16, 0);
    bytes[2] = 1;
    bytes[4] = 1;
    std::vector<uint8_t> image(40, 0);
    image[0] = 40;
    image[4] = 2;
    image[8] = 4;
    image[12] = 1;
    image[14] = 4;
    std::vector<uint8_t> palette(16 * 4, 0);
    palette[1 * 4 + 2] = 255;
    palette[2 * 4 + 1] = 255;
    palette[3 * 4 + 0] = 255;
    image.insert(image.end(), palette.begin(), palette.end());
    std::vector<uint8_t> pixels { 0x30, 0, 0, 0, 0x12, 0, 0, 0 };
    image.insert(image.end(), pixels.begin(), pixels.end());
    std::vector<uint8_t> mask { 0x00, 0, 0, 0, 0x40, 0, 0, 0 };
    image.insert(image.end(), mask.begin(), mask.end());
    bytes[6 + 8] = static_cast<uint8_t>(image.size());
    bytes[6 + 12] = 22;
    bytes.insert(bytes.end(), image.begin(), image.end());
    ut::icon::Image decoded = ut::icon::decode(bytes);
    REQUIRE(decoded.width == 2);
    REQUIRE(decoded.height == 2);
    std::vector<uint8_t> expected {
        255,
        0,
        0,
        255,
        0,
        255,
        0,
        0,
        0,
        0,
        255,
        255,
        0,
        0,
        0,
        255,
    };
    CHECK(decoded.rgba == expected);
}

TEST_CASE("GetEntryLevel returns the Entry level's LevelInfo from any map", "[data]")
{
    {
        DeckWorld deck;
        ut::vm::Call call(deck.world.player(), ut::Name("GetEntryLevel"));
        call.run();
        ut::UObject* entry = *reinterpret_cast<ut::UObject**>(call.returnValue());
        REQUIRE(entry != nullptr);
        CHECK(entry != deck.world.levelInfo);
        CHECK(entry->isA(ut::Name("LevelInfo")));
        CHECK(entry->outermost()->name == ut::Name("Entry"));
    }
    ut::objects::unloadPackage(ut::Name("Entry"));
    ut::World entryWorld;
    REQUIRE(entryWorld.loadMap("Entry"));
    ut::vm::Call call(entryWorld.player(), ut::Name("GetEntryLevel"));
    call.run();
    CHECK(*reinterpret_cast<ut::UObject**>(call.returnValue()) == entryWorld.levelInfo);
    ut::objects::unloadPackage(ut::Name("Entry"));
}

TEST_CASE("LogMutator writes the mutator class as a game event", "[data]")
{
    DeckWorld deck;
    ut::UObject* game = ut::levelinfo::Game(deck.world.levelInfo);
    REQUIRE(game != nullptr);
    ut::UObject* mutator = ut::gameinfo::BaseMutator(game);
    REQUIRE(mutator != nullptr);
    ut::UObject* log = deck.world.spawn(ut::objects::findClass("Engine.StatLogFile"), nullptr, ut::Name(), {}, {}, nullptr, true);
    REQUIRE(log != nullptr);
    *reinterpret_cast<std::string*>(log->propertyAddress(ut::Name("StatLogFile"))) = "../Logs/mutator.ngLog.tmp";
    *reinterpret_cast<std::string*>(log->propertyAddress(ut::Name("StatLogFinal"))) = "../Logs/mutator.ngLog.log";
    ut::vm::Call(log, ut::Name("OpenLog")).run();
    ut::vm::Call(log, ut::Name("LogMutator")).obj(mutator).run();
    ut::vm::Call(log, ut::Name("CloseLog")).run();
    std::ifstream in(ut::config::userDirectory() / "Logs" / "mutator.ngLog.log", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string expected = "\tgame\tGameMutator\tClass Botpack.DMMutator\r\n";
    REQUIRE(text.size() > expected.size());
    CHECK(text.substr(text.size() - expected.size()) == expected);
    CHECK(mutator->cls->name == ut::Name("DMMutator"));
}
