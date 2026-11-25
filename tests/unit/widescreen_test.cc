#include "compiler/script_compiler.h"
#include "core/config.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/canvas.h"
#include "engine/canvas_hooks.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/video_options.h"
#include "engine/world.h"
#include "render/texture_cache.h"
#include "test_world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
    struct Tile {
        float x = 0;
        float y = 0;
        float w = 0;
        float h = 0;
    };

    struct RecordingCanvas : ut::Canvas2D {
        float width() const override
        {
            return 2560;
        }

        float height() const override
        {
            return 1080;
        }

        void setClip(float, float, float, float) override
        {
        }

        void clearClip() override
        {
        }

        void drawTile(ut::UTexture*, float x, float y, float w, float h, float, float, float, float, ut::LinearColor, ut::BlendStyle, bool) override
        {
            tiles.push_back({ x, y, w, h });
        }

        void drawRect(float x, float y, float w, float h, ut::LinearColor, ut::BlendStyle) override
        {
            tiles.push_back({ x, y, w, h });
        }

        void drawLine(float, float, float, float, ut::LinearColor) override
        {
        }

        std::vector<Tile> tiles;
    };

    ut::UObject*& objectAt(ut::UObject* object, const char* name)
    {
        return *reinterpret_cast<ut::UObject**>(object->propertyAddress(ut::Name(name)));
    }

    float& floatAt(ut::UObject* object, const char* name)
    {
        return *reinterpret_cast<float*>(object->propertyAddress(ut::Name(name)));
    }

    const std::string& stringAt(ut::UObject* object, const char* name)
    {
        return *reinterpret_cast<std::string*>(object->propertyAddress(ut::Name(name)));
    }

    struct WideScreen {
        ut::World world;
        RecordingCanvas recorder;
        ut::Canvas2D* previousTarget = nullptr;
        ut::UObject* player = nullptr;
        ut::UObject* hud = nullptr;
        ut::UObject* canvas = nullptr;

        explicit WideScreen(const std::string& game = "Botpack.DeathMatchPlus")
        {
            ut::engine::initialize(OPENUT_GAME_DATA);
            ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
            REQUIRE(world.loadMap("DM-Deck16][?game=" + game));
            player = world.player();
            REQUIRE(player != nullptr);
            canvas = ut::objects::newObject(ut::engineClasses().canvas, ut::objects::packageObject(ut::Name("Transient")), ut::Name("WideCanvas"), 0);
            ut::canvas::Viewport(canvas) = world.viewport;
            ut::vm::event(canvas, ut::Name("Reset"));
            ut::canvas::SizeX(canvas) = 2560;
            ut::canvas::SizeY(canvas) = 1080;
            ut::canvas::ClipX(canvas) = 2560;
            ut::canvas::ClipY(canvas) = 1080;
            previousTarget = ut::canvasHooks().target;
            ut::canvasHooks().target = &recorder;
            for (int frame = 0; frame < 2; frame++) {
                ut::vm::Call(player, ut::Name("PostRender")).obj(canvas).run();
            }
            hud = ut::playerpawn::myHUD(player);
            REQUIRE(hud != nullptr);
            REQUIRE(hud->isA(ut::Name("ChallengeHUD")));
        }

        ~WideScreen()
        {
            ut::canvasHooks().target = previousTarget;
            ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
        }
    };

    ut::UObject* controlWithText(ut::UObject* window, const std::string& text)
    {
        for (ut::UObject* child = objectAt(window, "FirstChildWindow"); child; child = objectAt(child, "NextSiblingWindow")) {
            if (child->isA(ut::Name("UWindowDialogControl")) && stringAt(child, "Text") == text) {
                return child;
            }
        }
        return nullptr;
    }

    std::string comboValue(ut::UObject* combo)
    {
        ut::vm::Call value(combo, ut::Name("GetValue"));
        value.run();
        return *reinterpret_cast<std::string*>(value.returnValue());
    }

    std::string videoSetting(const char* key)
    {
        return ut::config::system().get("WinDrv.WindowsClient", key).value_or("");
    }
}

TEST_CASE("the HUD scales with the screen height, so a 21:9 screen gets the same HUD as 4:3", "[data]")
{
    WideScreen screen;
    float hudScale = floatAt(screen.hud, "HUDScale");
    ut::vm::Call(screen.hud, ut::Name("HUDSetup")).obj(screen.canvas).run();
    CHECK(floatAt(screen.hud, "Scale") == Catch::Approx(hudScale * 1.125f));
    CHECK(ut::canvas::ClipX(screen.canvas) == 2560);

    ut::canvas::ClipX(screen.canvas) = 1440;
    ut::vm::Call(screen.hud, ut::Name("HUDSetup")).obj(screen.canvas).run();
    CHECK(floatAt(screen.hud, "Scale") == Catch::Approx(hudScale * 1.125f));
}

TEST_CASE("the crosshair keeps its 4:3 size and stays in the middle of a 21:9 screen", "[data]")
{
    WideScreen screen;
    ut::vm::Call(screen.hud, ut::Name("HUDSetup")).obj(screen.canvas).run();
    *reinterpret_cast<int32_t*>(screen.hud->propertyAddress(ut::Name("Crosshair"))) = 0;
    floatAt(screen.hud, "PickupTime") = -100;
    ut::playerpawn::Handedness(screen.player) = 0;
    screen.recorder.tiles.clear();
    ut::vm::Call(screen.hud, ut::Name("DrawCrossHair")).obj(screen.canvas).i(0).i(0).run();
    REQUIRE(screen.recorder.tiles.size() == 1);
    const Tile& tile = screen.recorder.tiles[0];
    CHECK(tile.w == 128);
    CHECK(tile.x + tile.w / 2 == 1280);
    CHECK(tile.y + tile.h / 2 == 540);
    CHECK(ut::canvas::OrgX(screen.canvas) == 0);
    CHECK(ut::canvas::ClipX(screen.canvas) == 2560);
}

TEST_CASE("the sniper scope keeps its 4:3 size and stays in the middle of a 21:9 screen", "[data]")
{
    WideScreen screen;
    ut::UObject* rifle = screen.world.spawn(ut::objects::findClass("Botpack.SniperRifle"), screen.player, ut::Name(), ut::actor::Location(screen.player), ut::Rotator(), nullptr, true);
    REQUIRE(rifle != nullptr);
    ut::playerpawn::DesiredFOV(screen.player) = 30;
    ut::playerpawn::DefaultFOV(screen.player) = 90;
    screen.recorder.tiles.clear();
    ut::vm::Call(rifle, ut::Name("PostRender")).obj(screen.canvas).run();
    REQUIRE(!screen.recorder.tiles.empty());
    const Tile& reticle = screen.recorder.tiles[0];
    CHECK(reticle.w == 576);
    CHECK(reticle.x + reticle.w / 2 == 1280);
    CHECK(reticle.y + reticle.h / 2 == 540);
    CHECK(ut::canvas::OrgX(screen.canvas) == 0);
    CHECK(ut::canvas::ClipX(screen.canvas) == 2560);
}

TEST_CASE("the muzzle flash keeps its 4:3 size on a 21:9 screen", "[data]")
{
    WideScreen screen;
    ut::UObject* rifle = screen.world.spawn(ut::objects::findClass("Botpack.SniperRifle"), screen.player, ut::Name(), ut::actor::Location(screen.player), ut::Rotator(), nullptr, true);
    REQUIRE(rifle != nullptr);
    ut::pawn::Weapon(screen.player) = rifle;
    ut::levelinfo::bHighDetailMode(ut::actor::Level(screen.player)) = 1;
    ut::playerpawn::Handedness(screen.player) = 0;
    ut::playerpawn::DesiredFOV(screen.player) = ut::playerpawn::DefaultFOV(screen.player);
    *rifle->propertyAddress(ut::Name("bMuzzleFlash")) = 1;
    ut::UObject* flash = objectAt(rifle, "MFTexture");
    REQUIRE(flash != nullptr);
    screen.recorder.tiles.clear();
    ut::vm::Call(screen.player, ut::Name("RenderOverlays")).obj(screen.canvas).run();
    REQUIRE(screen.recorder.tiles.size() == 1);
    CHECK(screen.recorder.tiles[0].w == Catch::Approx(ut::bitmap::USize(flash) * 2.25f));
    CHECK(ut::canvas::OrgX(screen.canvas) == 0);
    CHECK(ut::canvas::ClipX(screen.canvas) == 2560);
}

TEST_CASE("the guided redeemer view keeps its 4:3 layout, and its static still covers the whole 21:9 screen", "[data]")
{
    WideScreen screen;
    ut::UObject* redeemer = screen.world.spawn(ut::objects::findClass("Botpack.WarheadLauncher"), screen.player, ut::Name(), ut::actor::Location(screen.player), ut::Rotator(), nullptr, true);
    REQUIRE(redeemer != nullptr);
    *reinterpret_cast<uint32_t*>(redeemer->propertyAddress(ut::Name("bGuiding"))) = 1;
    ut::UObject* reticleTexture = ut::objects::loadObject("Botpack.GuidedX", nullptr);
    REQUIRE(reticleTexture != nullptr);
    screen.recorder.tiles.clear();
    ut::vm::Call(redeemer, ut::Name("PostRender")).obj(screen.canvas).run();
    REQUIRE(screen.recorder.tiles.size() > 2);
    const Tile& reticle = screen.recorder.tiles[0];
    CHECK(reticle.w == ut::bitmap::USize(reticleTexture) * 2);
    CHECK(reticle.x + reticle.w / 2 == 1280);
    for (size_t i = 1; i < screen.recorder.tiles.size(); i++) {
        CHECK(screen.recorder.tiles[i].x == 561);
    }
    CHECK(ut::canvas::OrgX(screen.canvas) == 0);
    CHECK(ut::canvas::ClipX(screen.canvas) == 2560);

    *reinterpret_cast<uint32_t*>(redeemer->propertyAddress(ut::Name("bGuiding"))) = 0;
    *reinterpret_cast<uint32_t*>(redeemer->propertyAddress(ut::Name("bShowStatic"))) = 1;
    screen.recorder.tiles.clear();
    ut::vm::Call(redeemer, ut::Name("PostRender")).obj(screen.canvas).run();
    REQUIRE(screen.recorder.tiles.size() == 1);
    CHECK(screen.recorder.tiles[0].x == 0);
    CHECK(screen.recorder.tiles[0].w >= 2560);
}

TEST_CASE("the scoreboard is drawn inside the centered 4:3 area of a 21:9 screen", "[data]")
{
    auto [game, scoreboardClass] = GENERATE(table<std::string, std::string>({
        { "Botpack.DeathMatchPlus", "TournamentScoreBoard" },
        { "Botpack.TeamGamePlus", "TeamScoreBoard" },
        { "Botpack.LastManStanding", "LMSScoreboard" },
    }));
    INFO(game);
    WideScreen screen(game);
    for (int tick = 0; tick < 60; tick++) {
        screen.world.tick(0.05f);
    }
    ut::playerpawn::bShowScores(screen.player) = 1;
    ut::vm::Call(screen.player, ut::Name("PostRender")).obj(screen.canvas).run();
    ut::UObject* scoreboard = ut::playerpawn::Scoring(screen.player);
    REQUIRE(scoreboard != nullptr);
    REQUIRE(scoreboard->cls->name.str() == scoreboardClass);
    screen.recorder.tiles.clear();
    ut::vm::Call(scoreboard, ut::Name("ShowScores")).obj(screen.canvas).run();
    REQUIRE(screen.recorder.tiles.size() > 10);
    for (const Tile& tile : screen.recorder.tiles) {
        INFO("tile at " << tile.x << " width " << tile.w);
        CHECK(tile.x >= 560 - 0.5f);
        CHECK(tile.x + tile.w <= 2000 + 0.5f);
    }
    CHECK(ut::canvas::ClipX(screen.canvas) == 2560);
}

TEST_CASE("the scoreboard header puts the game name and the frag limit on separate lines", "[data]")
{
    WideScreen screen;
    for (int tick = 0; tick < 60; tick++) {
        screen.world.tick(0.05f);
    }
    ut::playerpawn::bShowScores(screen.player) = 1;
    ut::vm::Call(screen.player, ut::Name("PostRender")).obj(screen.canvas).run();
    ut::UObject* scoreboard = ut::playerpawn::Scoring(screen.player);
    REQUIRE(scoreboard != nullptr);
    ut::canvas::Font(screen.canvas) = ut::objects::loadObject("Engine.SmallFont", nullptr);
    ut::canvas::Style(screen.canvas) = 1;
    ut::canvas::bCenter(screen.canvas) = 1;
    ut::vm::Call measure(screen.canvas, ut::Name("StrLen"));
    measure.s("Test").f(0).f(0).run();
    float lineHeight = *reinterpret_cast<float*>(measure.parameter(2));
    REQUIRE(lineHeight > 0);
    ut::canvas::CurX(screen.canvas) = 0;
    ut::canvas::CurY(screen.canvas) = 100;
    screen.recorder.tiles.clear();
    ut::vm::Call(scoreboard, ut::Name("DrawVictoryConditions")).obj(screen.canvas).run();
    REQUIRE(screen.recorder.tiles.size() > 10);
    std::vector<float> rows;
    for (const Tile& tile : screen.recorder.tiles) {
        if (std::find(rows.begin(), rows.end(), tile.y) == rows.end()) {
            rows.push_back(tile.y);
        }
    }
    REQUIRE(rows.size() == 2);
    CHECK(rows[0] == 100);
    CHECK(rows[1] == 100 + lineHeight);
}

TEST_CASE("a chat message draws the player name and the text on the same line", "[data]")
{
    WideScreen screen;
    ut::UObject* playerInfo = objectAt(screen.player, "PlayerReplicationInfo");
    REQUIRE(playerInfo != nullptr);
    REQUIRE(!stringAt(playerInfo, "PlayerName").empty());
    ut::canvas::Font(screen.canvas) = ut::objects::loadObject("Engine.SmallFont", nullptr);
    ut::canvas::Style(screen.canvas) = 1;
    ut::canvas::bCenter(screen.canvas) = 0;
    ut::vm::Call measure(screen.canvas, ut::Name("StrLen"));
    measure.s("TEST").f(0).f(0).run();
    float lineHeight = *reinterpret_cast<float*>(measure.parameter(2));
    REQUIRE(lineHeight > 0);
    ut::canvas::CurX(screen.canvas) = 6;
    ut::canvas::CurY(screen.canvas) = 50;
    screen.recorder.tiles.clear();
    ut::UClass* sayMessage = ut::objects::findClass("Botpack.SayMessagePlus");
    REQUIRE(sayMessage != nullptr);
    ut::vm::Call(sayMessage->defaultObject, ut::Name("RenderComplexMessage")).obj(screen.canvas).f(0).f(lineHeight).s("hello there").i(0).obj(playerInfo).obj(nullptr).obj(nullptr).run();
    REQUIRE(screen.recorder.tiles.size() > stringAt(playerInfo, "PlayerName").size() + 5);
    for (const Tile& tile : screen.recorder.tiles) {
        CHECK(tile.y == 50);
    }
}

TEST_CASE("a UI scale draws everything larger and reports a smaller canvas")
{
    RecordingCanvas recorder;
    ut::ScaledCanvas scaled(recorder, 2);
    CHECK(scaled.width() == 1280);
    CHECK(scaled.height() == 540);
    scaled.drawTile(nullptr, 10, 20, 30, 40, 0, 0, 8, 8, {}, ut::BlendStyle::Normal, false);
    scaled.drawRect(1, 2, 3, 4, {}, ut::BlendStyle::Normal);
    REQUIRE(recorder.tiles.size() == 2);
    CHECK(recorder.tiles[0].x == 20);
    CHECK(recorder.tiles[0].y == 40);
    CHECK(recorder.tiles[0].w == 60);
    CHECK(recorder.tiles[0].h == 80);
    CHECK(recorder.tiles[1].x == 2);
    CHECK(recorder.tiles[1].h == 8);
}

TEST_CASE("the video options offer anti-aliasing, anisotropy, UI scale, raw mouse, S3TC, soft particles and high resolution fonts, off or automatic by default", "[data]")
{
    WideScreen screen;
    ut::UObject* console = ut::player::Console(screen.world.viewport);
    REQUIRE(console != nullptr);
    ut::vm::Call(console, ut::Name("CreateRootWindow")).obj(nullptr).run();
    ut::UObject* root = objectAt(console, "Root");
    REQUIRE(root != nullptr);
    ut::vm::Call create(root, ut::Name("CreateWindow"));
    create.obj(ut::objects::findClass("UMenu.UMenuVideoClientWindow")).f(0).f(0).f(400).f(600);
    create.run();
    ut::UObject* window = *reinterpret_cast<ut::UObject**>(create.returnValue());
    REQUIRE(window != nullptr);

    ut::UObject* antiAliasing = controlWithText(window, "Anti-Aliasing");
    ut::UObject* anisotropy = controlWithText(window, "Anisotropic Filtering");
    ut::UObject* uiScale = controlWithText(window, "UI Scale");
    ut::UObject* rawMouse = controlWithText(window, "Raw Mouse Input");
    ut::UObject* highDetail = controlWithText(window, "High Detail Textures");
    ut::UObject* softParticles = controlWithText(window, "Soft Particles");
    ut::UObject* fonts = controlWithText(window, "High Resolution Fonts");
    REQUIRE(antiAliasing != nullptr);
    REQUIRE(anisotropy != nullptr);
    REQUIRE(uiScale != nullptr);
    REQUIRE(rawMouse != nullptr);
    REQUIRE(highDetail != nullptr);
    REQUIRE(softParticles != nullptr);
    REQUIRE(fonts != nullptr);
    CHECK(comboValue(antiAliasing) == "Off");
    CHECK(comboValue(anisotropy) == "Off");
    CHECK(comboValue(uiScale) == "Auto");
    CHECK(*reinterpret_cast<uint32_t*>(rawMouse->propertyAddress(ut::Name("bChecked"))) == 0);
    CHECK(*reinterpret_cast<uint32_t*>(highDetail->propertyAddress(ut::Name("bChecked"))) == 0);
    CHECK(*reinterpret_cast<uint32_t*>(softParticles->propertyAddress(ut::Name("bChecked"))) == 0);
    CHECK(*reinterpret_cast<uint32_t*>(fonts->propertyAddress(ut::Name("bChecked"))) == 0);
    ut::UObject* driver = objectAt(window, "DriverDesc");
    REQUIRE(driver != nullptr);
    CHECK(stringAt(driver, "Text") == "OpenGL Support");
    ut::UObject* colorDepth = objectAt(window, "ColorDepthCombo");
    REQUIRE(colorDepth != nullptr);
    CHECK(comboValue(colorDepth) == "32 bit");
    ut::UObject* lastStock = objectAt(window, "DynamicLightsCheck");
    REQUIRE(lastStock != nullptr);
    CHECK(floatAt(antiAliasing, "WinTop") > floatAt(lastStock, "WinTop"));
    CHECK(floatAt(softParticles, "WinTop") > floatAt(highDetail, "WinTop"));
    CHECK(floatAt(fonts, "WinTop") > floatAt(softParticles, "WinTop"));
    CHECK(floatAt(window, "DesiredHeight") > floatAt(fonts, "WinTop"));

    ut::vm::Call(antiAliasing, ut::Name("SetSelectedIndex")).i(2).run();
    ut::vm::Call(anisotropy, ut::Name("SetSelectedIndex")).i(4).run();
    ut::vm::Call(uiScale, ut::Name("SetSelectedIndex")).i(2).run();
    ut::vm::Call(rawMouse, ut::Name("LMouseUp")).f(1).f(1).run();
    ut::vm::Call(highDetail, ut::Name("LMouseUp")).f(1).f(1).run();
    ut::vm::Call(softParticles, ut::Name("LMouseUp")).f(1).f(1).run();
    ut::vm::Call(fonts, ut::Name("LMouseUp")).f(1).f(1).run();
    CHECK(videoSetting("Multisampling") == "2");
    CHECK(videoSetting("Anisotropy") == "4");
    CHECK(videoSetting("UIScale") == "2");
    CHECK(videoSetting("RawMouseInput") == "True");
    CHECK(videoSetting("UseS3TC") == "True");
    CHECK(videoSetting("SoftParticles") == "True");
    CHECK(videoSetting("TrueTypeFonts") == "True");

    ut::config::system().set("WinDrv.WindowsClient", "Multisampling", "0");
    ut::config::system().set("WinDrv.WindowsClient", "Anisotropy", "0");
    ut::config::system().set("WinDrv.WindowsClient", "UIScale", "0");
    ut::config::system().set("WinDrv.WindowsClient", "RawMouseInput", "False");
    ut::config::system().set("WinDrv.WindowsClient", "UseS3TC", "False");
    ut::config::system().set("WinDrv.WindowsClient", "SoftParticles", "False");
    ut::config::system().set("WinDrv.WindowsClient", "TrueTypeFonts", "False");
    ut::config::system().save();
}

TEST_CASE("a video setting that is not a number or does not fit in an int falls back to its default", "[data]")
{
    WideScreen screen;
    REQUIRE(ut::videoSection() == "WinDrv.WindowsClient");
    auto [stored, expected] = GENERATE(table<std::string, int32_t>({
        { "3", 3 },
        { " 3", 3 },
        { "+2", 2 },
        { "-1", -1 },
        { "TRUE", 1 },
        { "false", 0 },
        { "2147483647", 2147483647 },
        { "-2147483648", -2147483647 - 1 },
        { "2147483648", 7 },
        { "-2147483649", 7 },
        { "99999999999999999999", 7 },
        { "abc", 7 },
        { "", 7 },
    }));
    INFO("stored '" << stored << "'");
    ut::config::system().set("WinDrv.WindowsClient", "OpenUTProbe", stored);
    CHECK(ut::videoSetting("OpenUTProbe", 7) == expected);
    CHECK(ut::videoSetting("OpenUTMissing", 7) == 7);
}

TEST_CASE("high detail textures upload the S3TC mips only when enabled and present", "[data]")
{
    std::vector<ut::UTexture*> fractal = testworld::packageTextures("FractalFX");
    ut::UTexture* compressed = nullptr;
    for (ut::UTexture* texture : fractal) {
        if (texture->hasHighDetail()) {
            compressed = texture;
            break;
        }
    }
    REQUIRE(compressed != nullptr);
    CHECK(&ut::TextureCache::uploadMips(compressed, true) == &compressed->compressedMips);
    CHECK(&ut::TextureCache::uploadMips(compressed, false) == &compressed->mips);
    const ut::TextureMip& top = compressed->compressedMips[0];
    CHECK(compressed->highDetailRgba(0).size() == static_cast<size_t>(top.width) * top.height * 4);

    std::vector<ut::UTexture*> plain = testworld::packageTextures("Ancient");
    REQUIRE_FALSE(plain.empty());
    CHECK_FALSE(plain[0]->hasHighDetail());
    CHECK(&ut::TextureCache::uploadMips(plain[0], true) == &plain[0]->mips);
}

namespace {
    struct ComboTable {
        const char* caption;
        const char* key;
        std::vector<std::pair<std::string, std::string>> values;
    };

    const std::vector<ComboTable>& comboTables()
    {
        static const std::vector<ComboTable> tables = {
            { "Anti-Aliasing", "Multisampling", { { "0", "Off" }, { "1", "2x" }, { "2", "4x" }, { "3", "8x" } } },
            { "Anisotropic Filtering", "Anisotropy", { { "0", "Off" }, { "1", "2x" }, { "2", "4x" }, { "3", "8x" }, { "4", "16x" } } },
            { "UI Scale", "UIScale", { { "0", "Auto" }, { "1", "Off" }, { "2", "2x" }, { "3", "3x" }, { "4", "4x" } } },
        };
        return tables;
    }

    const std::vector<std::pair<const char*, const char*>>& checkTables()
    {
        static const std::vector<std::pair<const char*, const char*>> tables = {
            { "Raw Mouse Input", "RawMouseInput" },
            { "High Detail Textures", "UseS3TC" },
            { "Soft Particles", "SoftParticles" },
            { "High Resolution Fonts", "TrueTypeFonts" },
        };
        return tables;
    }

    ut::UObject* createVideoWindow(WideScreen& screen, float width, float height)
    {
        ut::UObject* console = ut::player::Console(screen.world.viewport);
        REQUIRE(console != nullptr);
        if (!objectAt(console, "Root")) {
            ut::vm::Call(console, ut::Name("CreateRootWindow")).obj(nullptr).run();
        }
        ut::UObject* root = objectAt(console, "Root");
        REQUIRE(root != nullptr);
        ut::vm::Call create(root, ut::Name("CreateWindow"));
        create.obj(ut::objects::findClass("UMenu.UMenuVideoClientWindow")).f(0).f(0).f(width).f(height);
        create.run();
        ut::UObject* window = *reinterpret_cast<ut::UObject**>(create.returnValue());
        REQUIRE(window != nullptr);
        return window;
    }

    uint32_t checked(ut::UObject* control)
    {
        return *reinterpret_cast<uint32_t*>(control->propertyAddress(ut::Name("bChecked")));
    }

    void resetVideoSettings()
    {
        ut::config::system().set("WinDrv.WindowsClient", "Multisampling", "0");
        ut::config::system().set("WinDrv.WindowsClient", "Anisotropy", "0");
        ut::config::system().set("WinDrv.WindowsClient", "UIScale", "0");
        ut::config::system().set("WinDrv.WindowsClient", "RawMouseInput", "False");
        ut::config::system().set("WinDrv.WindowsClient", "UseS3TC", "False");
        ut::config::system().set("WinDrv.WindowsClient", "SoftParticles", "False");
        ut::config::system().set("WinDrv.WindowsClient", "TrueTypeFonts", "False");
        ut::config::system().save();
    }

    bool finite(const Tile& tile)
    {
        return std::isfinite(tile.x) && std::isfinite(tile.y) && std::isfinite(tile.w) && std::isfinite(tile.h);
    }
}

TEST_CASE("an automatic UI scale picks the largest size that keeps 540 rows, and a chosen scale always wins", "[data]")
{
    WideScreen screen;
    ut::config::system().set("WinDrv.WindowsClient", "UIScale", "");
    CHECK(ut::uiScale(480) == 1);
    CHECK(ut::uiScale(1079) == 1);
    CHECK(ut::uiScale(1080) == 2);
    CHECK(ut::uiScale(1440) == 2);
    CHECK(ut::uiScale(1620) == 3);
    CHECK(ut::uiScale(2160) == 4);
    CHECK(ut::uiScale(4320) == 4);
    ut::config::system().set("WinDrv.WindowsClient", "UIScale", "0");
    CHECK(ut::uiScale(1440) == 2);
    ut::config::system().set("WinDrv.WindowsClient", "UIScale", "1");
    CHECK(ut::uiScale(2160) == 1);
    ut::config::system().set("WinDrv.WindowsClient", "UIScale", "3");
    CHECK(ut::uiScale(480) == 3);
    ut::config::system().set("WinDrv.WindowsClient", "UIScale", "9");
    CHECK(ut::uiScale(480) == 4);
    resetVideoSettings();
}

TEST_CASE("the video options show the default for a setting that does not fit in an int", "[data]")
{
    WideScreen screen;
    ut::config::system().set("WinDrv.WindowsClient", "Multisampling", "99999999999");
    ut::config::system().set("WinDrv.WindowsClient", "UIScale", "99999999999");
    ut::config::system().set("WinDrv.WindowsClient", "RawMouseInput", "99999999999");
    ut::UObject* window = createVideoWindow(screen, 400, 600);
    ut::UObject* antiAliasing = controlWithText(window, "Anti-Aliasing");
    ut::UObject* uiScale = controlWithText(window, "UI Scale");
    ut::UObject* rawMouse = controlWithText(window, "Raw Mouse Input");
    REQUIRE(antiAliasing != nullptr);
    REQUIRE(uiScale != nullptr);
    REQUIRE(rawMouse != nullptr);
    CHECK(comboValue(antiAliasing) == "Off");
    CHECK(comboValue(uiScale) == "Auto");
    CHECK(checked(rawMouse) == 0);
    resetVideoSettings();
}

TEST_CASE("fuzzed ini values, clicks and resizes never break the video options", "[data][fuzz]")
{
    WideScreen screen;
    std::mt19937 rng(Catch::getSeed());
    const std::vector<std::string> garbage = { "", "-1", "0", "1", "2", "3", "4", "5", "99999999999", "-99999999999", "True", "False", "true", "TRUE", "abc", "2.7", " 3", "0x10", "1e9" };
    for (int round = 0; round < 25; round++) {
        INFO("round " << round);
        std::vector<std::string> written;
        for (const ComboTable& table : comboTables()) {
            std::string value = rng() % 2 ? garbage[rng() % garbage.size()] : std::to_string(static_cast<int>(rng() % 21) - 10);
            ut::config::system().set("WinDrv.WindowsClient", table.key, value);
            written.push_back(value);
        }
        for (const auto& [caption, key] : checkTables()) {
            std::string value = garbage[rng() % garbage.size()];
            ut::config::system().set("WinDrv.WindowsClient", key, value);
            written.push_back(value);
        }
        ut::UObject* window = createVideoWindow(screen, static_cast<float>(rng() % 1600), static_cast<float>(rng() % 1200));
        for (size_t i = 0; i < comboTables().size(); i++) {
            const ComboTable& table = comboTables()[i];
            ut::UObject* combo = controlWithText(window, table.caption);
            REQUIRE(combo != nullptr);
            std::string shown = comboValue(combo);
            INFO(table.key << "=" << written[i] << " shows " << shown);
            bool known = false;
            for (const auto& [stored, display] : table.values) {
                known = known || display == shown;
                if (stored == written[i]) {
                    CHECK(shown == display);
                }
            }
            CHECK(known);
        }
        for (size_t i = 0; i < checkTables().size(); i++) {
            ut::UObject* check = controlWithText(window, checkTables()[i].first);
            REQUIRE(check != nullptr);
            const std::string& value = written[comboTables().size() + i];
            CHECK(checked(check) <= 1);
            if (value == "True" || value == "1") {
                CHECK(checked(check) == 1);
            }
            if (value == "False" || value == "0" || value.empty()) {
                CHECK(checked(check) == 0);
            }
        }
        ut::UObject* stockCombo = objectAt(window, "ScaleCombo");
        ut::UObject* stockCheck = objectAt(window, "DynamicLightsCheck");
        REQUIRE(stockCombo != nullptr);
        REQUIRE(stockCheck != nullptr);
        for (int action = 0; action < 40; action++) {
            int kind = static_cast<int>(rng() % 3);
            if (kind == 0) {
                const ComboTable& table = comboTables()[rng() % comboTables().size()];
                ut::UObject* combo = controlWithText(window, table.caption);
                size_t index = rng() % table.values.size();
                ut::vm::Call(combo, ut::Name("SetSelectedIndex")).i(static_cast<int32_t>(index)).run();
                CHECK(comboValue(combo) == table.values[index].second);
                CHECK(videoSetting(table.key) == table.values[index].first);
            } else if (kind == 1) {
                const auto& [caption, key] = checkTables()[rng() % checkTables().size()];
                ut::UObject* check = controlWithText(window, caption);
                uint32_t before = checked(check);
                ut::vm::Call(check, ut::Name("LMouseUp")).f(1).f(1).run();
                CHECK(checked(check) == (before ? 0u : 1u));
                CHECK(videoSetting(key) == (checked(check) ? "True" : "False"));
            } else {
                float width = static_cast<float>(rng() % 2000);
                ut::vm::Call(window, ut::Name("SetSize")).f(width).f(static_cast<float>(rng() % 1500)).run();
                ut::vm::Call(window, ut::Name("BeforePaint")).obj(screen.canvas).f(0).f(0).run();
                INFO("width " << width);
                for (const ComboTable& table : comboTables()) {
                    ut::UObject* combo = controlWithText(window, table.caption);
                    CHECK(floatAt(combo, "WinLeft") == floatAt(stockCombo, "WinLeft"));
                    CHECK(floatAt(combo, "WinWidth") == floatAt(stockCombo, "WinWidth"));
                    CHECK(floatAt(combo, "EditBoxWidth") == floatAt(stockCombo, "EditBoxWidth"));
                }
                for (const auto& [caption, key] : checkTables()) {
                    ut::UObject* check = controlWithText(window, caption);
                    CHECK(floatAt(check, "WinLeft") == floatAt(stockCheck, "WinLeft"));
                    CHECK(floatAt(check, "WinWidth") == floatAt(stockCheck, "WinWidth"));
                }
            }
        }
    }
    resetVideoSettings();
}

TEST_CASE("the HUD survives random canvas sizes and always gives the canvas back unchanged", "[data][fuzz]")
{
    WideScreen screen;
    for (int tick = 0; tick < 60; tick++) {
        screen.world.tick(0.05f);
    }
    std::mt19937 rng(Catch::getSeed());
    const std::vector<int> sizes = { 1, 2, 63, 240, 320, 480, 640, 768, 1024, 1080, 1280, 1440, 1920, 2160, 2560, 3440, 5120, 7680 };
    *reinterpret_cast<int32_t*>(screen.hud->propertyAddress(ut::Name("Crosshair"))) = 0;
    ut::playerpawn::Handedness(screen.player) = 0;
    for (int round = 0; round < 200; round++) {
        float width = static_cast<float>(rng() % 2 ? sizes[rng() % sizes.size()] : 1 + static_cast<int>(rng() % 8000));
        float height = static_cast<float>(rng() % 2 ? sizes[rng() % sizes.size()] : 1 + static_cast<int>(rng() % 5000));
        INFO(width << "x" << height);
        ut::canvas::SizeX(screen.canvas) = static_cast<int32_t>(width);
        ut::canvas::SizeY(screen.canvas) = static_cast<int32_t>(height);
        ut::canvas::ClipX(screen.canvas) = width;
        ut::canvas::ClipY(screen.canvas) = height;
        ut::canvas::OrgX(screen.canvas) = 0;
        ut::canvas::OrgY(screen.canvas) = 0;
        ut::playerpawn::bShowScores(screen.player) = rng() % 2;
        screen.recorder.tiles.clear();
        ut::vm::Call(screen.player, ut::Name("PostRender")).obj(screen.canvas).run();
        CHECK(ut::canvas::OrgX(screen.canvas) == 0);
        CHECK(ut::canvas::ClipX(screen.canvas) == width);
        CHECK(ut::canvas::ClipY(screen.canvas) == height);
        for (const Tile& tile : screen.recorder.tiles) {
            CHECK(finite(tile));
        }
        floatAt(screen.hud, "PickupTime") = -100;
        screen.recorder.tiles.clear();
        ut::vm::Call(screen.hud, ut::Name("DrawCrossHair")).obj(screen.canvas).i(0).i(0).run();
        REQUIRE(screen.recorder.tiles.size() == 1);
        const Tile& crosshair = screen.recorder.tiles[0];
        CHECK(crosshair.x + crosshair.w / 2 == Catch::Approx(width / 2).margin(1));
        CHECK(crosshair.y + crosshair.h / 2 == Catch::Approx(height / 2).margin(1));
        CHECK(ut::canvas::ClipX(screen.canvas) == width);
    }
}

TEST_CASE("DrawText always moves down by the height StrLen reports, for any text and clip width", "[data][fuzz]")
{
    WideScreen screen;
    std::mt19937 rng(Catch::getSeed());
    ut::canvas::Font(screen.canvas) = ut::objects::loadObject("Engine.SmallFont", nullptr);
    ut::canvas::Style(screen.canvas) = 1;
    const std::string alphabet = "abcXYZ019 .,:  \n\t-_&\xe9\xff";
    const std::vector<float> clips = { 0, 1, 5, 20, 100, 640, 2560 };
    for (int round = 0; round < 2000; round++) {
        std::string text;
        size_t length = rng() % 4 == 0 ? rng() % 400 : rng() % 40;
        for (size_t i = 0; i < length; i++) {
            text += alphabet[rng() % alphabet.size()];
        }
        float clip = rng() % 2 ? clips[rng() % clips.size()] : static_cast<float>(rng() % 3000);
        ut::canvas::ClipX(screen.canvas) = clip;
        ut::canvas::bCenter(screen.canvas) = rng() % 2;
        ut::canvas::CurX(screen.canvas) = static_cast<float>(rng() % (static_cast<uint32_t>(clip) + 1));
        ut::canvas::CurY(screen.canvas) = static_cast<float>(rng() % 1000);
        ut::canvas::CurYL(screen.canvas) = 0;
        INFO("clip " << clip << " text [" << text << "]");
        float before = ut::canvas::CurY(screen.canvas);
        ut::vm::Call measure(screen.canvas, ut::Name("StrLen"));
        measure.s(text).f(0).f(0).run();
        float height = *reinterpret_cast<float*>(measure.parameter(2));
        CHECK(height >= 0);
        screen.recorder.tiles.clear();
        ut::vm::Call(screen.canvas, ut::Name("DrawText")).s(text).b(false).run();
        CHECK(ut::canvas::CurY(screen.canvas) == before + height);
        for (const Tile& tile : screen.recorder.tiles) {
            CHECK(finite(tile));
        }
    }
    ut::canvas::ClipX(screen.canvas) = 2560;
}

TEST_CASE("corrupted S3TC mips are rejected or decoded within bounds", "[data][fuzz]")
{
    std::vector<ut::UTexture*> fractal = testworld::packageTextures("FractalFX");
    ut::UTexture* texture = nullptr;
    for (ut::UTexture* candidate : fractal) {
        if (candidate->hasHighDetail()) {
            texture = candidate;
            break;
        }
    }
    REQUIRE(texture != nullptr);
    const std::vector<ut::TextureMip> original = texture->compressedMips;
    std::mt19937 rng(Catch::getSeed());
    for (int round = 0; round < 500; round++) {
        std::vector<ut::TextureMip> mips = original;
        int edits = 1 + static_cast<int>(rng() % 4);
        for (int edit = 0; edit < edits && !mips.empty(); edit++) {
            ut::TextureMip& mip = mips[rng() % mips.size()];
            switch (rng() % 6) {
            case 0:
                mip.data.resize(rng() % (mip.data.size() + 1));
                break;
            case 1:
                mip.width = rng() % 1025;
                break;
            case 2:
                mip.height = rng() % 1025;
                break;
            case 3:
                mips.resize(rng() % (mips.size() + 1));
                break;
            case 4:
                mip.data.resize(mip.data.size() + rng() % 64, static_cast<uint8_t>(rng()));
                break;
            default:
                if (!mip.data.empty()) {
                    mip.data[rng() % mip.data.size()] = static_cast<uint8_t>(rng());
                }
                break;
            }
        }
        texture->compressedMips = mips;
        INFO("round " << round);
        if (texture->hasHighDetail()) {
            const ut::TextureMip& top = texture->compressedMips[0];
            std::vector<uint8_t> pixels;
            REQUIRE_NOTHROW(pixels = texture->highDetailRgba(0));
            CHECK(pixels.size() == static_cast<size_t>(top.width) * top.height * 4);
            CHECK(&ut::TextureCache::uploadMips(texture, true) == &texture->compressedMips);
        } else {
            CHECK(&ut::TextureCache::uploadMips(texture, true) == &texture->mips);
        }
        for (size_t i = 0; i < texture->compressedMips.size(); i++) {
            const ut::TextureMip& mip = texture->compressedMips[i];
            try {
                std::vector<uint8_t> pixels = texture->highDetailRgba(i);
                CHECK(pixels.size() == static_cast<size_t>(mip.width) * mip.height * 4);
            } catch (const std::runtime_error&) {
                CHECK(mip.data.size() < static_cast<size_t>((mip.width + 3) / 4) * ((mip.height + 3) / 4) * 8);
            }
        }
    }
    texture->compressedMips = original;
}

TEST_CASE("a HUD that is not a ChallengeHUD is drawn inside the centered 4:3 area of a 21:9 screen", "[data]")
{
    WideScreen screen;
    ut::UClass* unrealHud = ut::objects::findClass("UnrealShare.UnrealHUD");
    REQUIRE(unrealHud != nullptr);
    ut::UObject* hud = screen.world.spawn(unrealHud, screen.player, ut::Name(), ut::actor::Location(screen.player), ut::Rotator(), nullptr, true);
    REQUIRE(hud != nullptr);
    ut::playerpawn::myHUD(screen.player) = hud;
    for (int tick = 0; tick < 10; tick++) {
        screen.world.tick(0.05f);
    }
    screen.recorder.tiles.clear();
    ut::vm::Call(hud, ut::Name("PostRender")).obj(screen.canvas).run();
    REQUIRE(screen.recorder.tiles.size() > 3);
    float left = 2560;
    float right = 0;
    for (const Tile& tile : screen.recorder.tiles) {
        INFO("tile at " << tile.x << " width " << tile.w);
        CHECK(tile.x >= 560 - 0.5f);
        CHECK(tile.x + tile.w <= 2000 + 0.5f);
        left = std::min(left, tile.x);
        right = std::max(right, tile.x + tile.w);
    }
    CHECK(right - left > 1000);
    CHECK(ut::canvas::ClipX(screen.canvas) == 2560);
    CHECK(ut::canvas::OrgX(screen.canvas) == 0);
}

TEST_CASE("a ChallengeHUD still spreads its corners across the whole 21:9 screen", "[data]")
{
    WideScreen screen;
    ut::vm::Call(screen.world.gameInfo, ut::Name("StartMatch")).run();
    for (int tick = 0; tick < 10; tick++) {
        screen.world.tick(0.05f);
    }
    screen.recorder.tiles.clear();
    ut::vm::Call(screen.hud, ut::Name("PostRender")).obj(screen.canvas).run();
    REQUIRE(screen.recorder.tiles.size() > 3);
    float left = 2560;
    float right = 0;
    for (const Tile& tile : screen.recorder.tiles) {
        left = std::min(left, tile.x);
        right = std::max(right, tile.x + tile.w);
    }
    CHECK(left < 560 - 0.5f);
    CHECK(right > 2000 + 0.5f);
}

TEST_CASE("a mod HUD that resets the canvas and moves its origin stays inside the centered 4:3 area", "[data]")
{
    WideScreen screen;
    std::string source = "class OriginProbeHUD extends HUD;\r\n"
                         "\r\n"
                         "simulated function PostRender(canvas Canvas)\r\n"
                         "{\r\n"
                         "    Canvas.Reset();\r\n"
                         "    Canvas.SetPos(0, 0);\r\n"
                         "    Canvas.DrawRect(Texture'Engine.DefaultTexture', 4, 4);\r\n"
                         "    Canvas.SetOrigin(100, 0);\r\n"
                         "    Canvas.SetPos(0, 0);\r\n"
                         "    Canvas.DrawRect(Texture'Engine.DefaultTexture', 4, 4);\r\n"
                         "    Canvas.SetPos(Canvas.ClipX - 4, 0);\r\n"
                         "    Canvas.DrawRect(Texture'Engine.DefaultTexture', 4, 4);\r\n"
                         "}\r\n";
    std::string log;
    ut::compiler::PackageCompiler compiler(screen.world.level->outermost(), [&](const std::string& line) {
        log += line + "\n";
    });
    ut::UClass* probeClass = compiler.importClass("OriginProbeHUD.uc", source);
    REQUIRE(probeClass != nullptr);
    bool compiled = compiler.compile();
    INFO(log);
    REQUIRE(compiled);
    ut::UObject* hud = screen.world.spawn(probeClass, screen.player, ut::Name(), ut::actor::Location(screen.player), ut::Rotator(), nullptr, true);
    REQUIRE(hud != nullptr);
    screen.recorder.tiles.clear();
    ut::vm::Call(hud, ut::Name("PostRender")).obj(screen.canvas).run();
    REQUIRE(screen.recorder.tiles.size() == 3);
    CHECK(screen.recorder.tiles[0].x == 560);
    CHECK(screen.recorder.tiles[1].x == 660);
    CHECK(screen.recorder.tiles[2].x == 660 + 1440 - 4);
    CHECK(ut::canvas::OrgX(screen.canvas) == 0);
    CHECK(ut::canvas::ClipX(screen.canvas) == 2560);
}
