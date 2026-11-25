#define SDL_MAIN_HANDLED

#include "core/objects.h"
#include "core/vm.h"
#include "engine/canvas.h"
#include "engine/canvas_hooks.h"
#include "engine/props_engine.h"
#include "engine/world.h"
#include "engine/resources.h"
#include "engine/truetype_fonts.h"
#include "render/scene_renderer.h"

#include "offscreen_context.h"
#include "test_world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {
    constexpr int frameWidth = 200;
    constexpr int frameHeight = 60;

    std::vector<uint8_t> drawString(ut::SceneRenderer& renderer, ut::UFont* font, float x, float y)
    {
        renderer.beginFrame(frameWidth, frameHeight);
        ut::drawText(renderer.canvas(), font, "Video Driver GUI Mouse Speed", x, y, { 1, 1, 1, 1 }, ut::BlendStyle::Normal, 0);
        renderer.endFrame();
        std::vector<uint8_t> rgba;
        int width = 0;
        int height = 0;
        renderer.readPixels(rgba, width, height);
        return rgba;
    }

    bool shiftedCopy(const std::vector<uint8_t>& moved, const std::vector<uint8_t>& original, int dx, int dy)
    {
        for (int y = 0; y < frameHeight; y++) {
            for (int x = 0; x < frameWidth; x++) {
                int sx = x - dx;
                int sy = y - dy;
                uint8_t expected = sx >= 0 && sy >= 0 && sx < frameWidth && sy < frameHeight ? original[(sy * frameWidth + sx) * 4] : 0;
                if (moved[(y * frameWidth + x) * 4] != expected) {
                    return false;
                }
            }
        }
        return true;
    }
}

TEST_CASE("text drawn at a fractional position snaps to the nearest whole pixel with every glyph on the same rows", "[data]")
{
    testgl::OffscreenContext gl(frameWidth, frameHeight);
    REQUIRE(gl.context != nullptr);
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::UFont* font = ut::cast<ut::UFont>(ut::objects::loadObject("UWindowFonts.Tahoma10", nullptr));
    REQUIRE(font != nullptr);
    ut::SceneRenderer renderer;
    renderer.setTime(1.0);
    std::vector<uint8_t> aligned = drawString(renderer, font, 20, 20);
    long lit = 0;
    for (size_t i = 0; i < aligned.size(); i += 4) {
        lit += aligned[i] > 0 ? 1 : 0;
    }
    REQUIRE(lit > 100);
    const float offsets[] = { 0.25f, 0.499f, 0.5f, 0.501f, 0.75f };
    const int shifts[] = { 0, 0, 1, 1, 1 };
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            std::vector<uint8_t> moved = drawString(renderer, font, 20 + offsets[i], 20 + offsets[j]);
            INFO("offset " << offsets[i] << " " << offsets[j]);
            CHECK(shiftedCopy(moved, aligned, shifts[i], shifts[j]));
        }
    }
}

TEST_CASE("the stretched menu logo tiles meet without a line at the seam", "[data]")
{
    constexpr int tile = 480;
    testgl::OffscreenContext gl(2 * tile, tile);
    REQUIRE(gl.context != nullptr);
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::UTexture* left = ut::cast<ut::UTexture>(ut::objects::loadObject("UMenu.Bg21", nullptr));
    ut::UTexture* right = ut::cast<ut::UTexture>(ut::objects::loadObject("UMenu.Bg31", nullptr));
    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);
    ut::SceneRenderer renderer;
    renderer.setTime(1.0);
    renderer.beginFrame(2 * tile, tile);
    renderer.canvas().drawTile(right, tile - 1, 0, tile, tile, 0, 0, 256, 256, { 1, 1, 1, 1 }, ut::BlendStyle::Normal, true);
    renderer.canvas().drawTile(left, 0, 0, tile, tile, 0, 0, 256, 256, { 1, 1, 1, 1 }, ut::BlendStyle::Normal, true);
    renderer.endFrame();
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
    renderer.readPixels(rgba, width, height);
    REQUIRE(width == 2 * tile);
    auto columnStep = [&](int column) {
        long total = 0;
        for (int y = 0; y < tile; y++) {
            for (int c = 0; c < 3; c++) {
                total += std::abs(static_cast<int>(rgba[(y * width + column) * 4 + c]) - static_cast<int>(rgba[(y * width + column + 1) * 4 + c]));
            }
        }
        return total;
    };
    long typical = 0;
    for (int column = tile - 40; column < tile - 10; column++) {
        typical = std::max(typical, columnStep(column));
    }
    REQUIRE(typical > 0);
    CHECK(columnStep(tile - 2) <= typical);
    CHECK(columnStep(tile - 1) <= typical);
}

TEST_CASE("DrawClippedActor draws the actor from the origin into the clip region only", "[data]")
{
    constexpr int width = 320;
    constexpr int height = 240;
    testgl::OffscreenContext gl(width, height);
    REQUIRE(gl.context != nullptr);
    testworld::Deck deck;
    ut::UObject* box = testworld::spawnBox(deck.world, testworld::openSpot(deck.world, 100.0f));
    REQUIRE(box != nullptr);
    ut::actor::Location(box) = ut::Vector { 60, 0, 0 };
    ut::actor::bUnlit(box) = 1;
    ut::actor::DrawScale(box) = 0.1f;
    ut::UObject* canvas = ut::objects::newObject(ut::objects::findClass("Engine.Canvas"), ut::objects::packageObject(ut::Name("Transient")), ut::Name(), 0);

    ut::SceneRenderer renderer;
    renderer.setTime(1.0);
    ut::CanvasHooks& hooks = ut::canvasHooks();
    ut::CanvasHooks saved = hooks;
    hooks.target = &renderer.canvas();
    hooks.drawClippedActor = [&](ut::UObject* actor, float x, float y, float w, float h) {
        renderer.drawClippedActor(actor, 30.0f, static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h));
    };
    renderer.beginFrame(width, height);
    ut::vm::Call(canvas, ut::Name("DrawClippedActor")).obj(box).b(false).i(100).i(80).i(180).i(40).b(true).run();
    renderer.endFrame();
    hooks = saved;
    std::vector<uint8_t> rgba;
    int readWidth = 0;
    int readHeight = 0;
    renderer.readPixels(rgba, readWidth, readHeight);
    REQUIRE(readWidth == width);

    long inside = 0;
    long outside = 0;
    double sumX = 0;
    double sumY = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const uint8_t* pixel = &rgba[(y * width + x) * 4];
            if (pixel[0] + pixel[1] + pixel[2] == 0) {
                continue;
            }
            if (x >= 180 && x < 280 && y >= 40 && y < 120) {
                inside++;
                sumX += x;
                sumY += y;
            } else {
                outside++;
            }
        }
    }
    INFO("inside " << inside);
    REQUIRE(inside > 200);
    REQUIRE(inside < 4000);
    CHECK(outside == 0);
    CHECK(std::abs(sumX / static_cast<double>(inside) - 230.0) < 8.0);
    CHECK(std::abs(sumY / static_cast<double>(inside) - 80.0) < 8.0);
}

TEST_CASE("high resolution font glyphs blend over the background instead of drawing a box", "[data]")
{
    constexpr int width = 64;
    constexpr int height = 48;
    testgl::OffscreenContext gl(width, height);
    REQUIRE(gl.context != nullptr);
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::UFont* font = ut::cast<ut::UFont>(ut::objects::loadObject("UWindowFonts.Tahoma30", nullptr));
    REQUIRE(font != nullptr);
    ut::SceneRenderer renderer;
    renderer.setTime(1.0);
    ut::truetype::setEnabled(true);
    renderer.beginFrame(width, height);
    renderer.canvas().drawRect(0, 0, width, height, { 0.8f, 0.8f, 0.8f, 1 }, ut::BlendStyle::Normal);
    ut::drawText(renderer.canvas(), font, "o", 10, 4, { 0, 0, 0, 1 }, ut::BlendStyle::Normal, 0);
    renderer.endFrame();
    ut::truetype::setEnabled(false);
    std::vector<uint8_t> rgba;
    int readWidth = 0;
    int readHeight = 0;
    renderer.readPixels(rgba, readWidth, readHeight);
    REQUIRE(readWidth == width);
    int left = width;
    int right = -1;
    int top = height;
    int bottom = -1;
    long edges = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int value = rgba[(y * width + x) * 4];
            CHECK(value <= 205);
            if (value < 150) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
            if (value > 40 && value < 170) {
                edges++;
            }
        }
    }
    REQUIRE(right - left > 6);
    REQUIRE(bottom - top > 6);
    CHECK(edges > 10);
    CHECK(rgba[(((top + bottom) / 2) * width + (left + right) / 2) * 4] >= 195);
}
