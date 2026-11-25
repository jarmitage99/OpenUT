#define SDL_MAIN_HANDLED

#include "core/objects.h"
#include "engine/props_engine.h"
#include "engine/world.h"
#include "render/scene_renderer.h"

#include "offscreen_context.h"
#include "test_world.h"

#include <catch_amalgamated.hpp>
#include <glad/gl.h>

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace {
    constexpr int frameWidth = 320;
    constexpr int frameHeight = 240;

    std::vector<uint8_t> render(ut::SceneRenderer& renderer, ut::World& world, const ut::Vector& eye, bool softParticles, int multisample = 0)
    {
        ut::RenderOptions options;
        options.softParticles = softParticles;
        options.multisample = multisample;
        renderer.setOptions(options);
        renderer.setTime(1.0);
        renderer.beginFrame(frameWidth, frameHeight);
        renderer.drawScene(world.level, world.actors, eye, ut::Rotator { -16384, 0, 0 }, 90.0f, world.player());
        renderer.endFrame();
        std::vector<uint8_t> rgba;
        int width = 0;
        int height = 0;
        renderer.readPixels(rgba, width, height);
        CHECK(glGetError() == GL_NO_ERROR);
        return rgba;
    }

    long added(const std::vector<uint8_t>& with, const std::vector<uint8_t>& without)
    {
        long total = 0;
        for (size_t i = 0; i < with.size(); i++) {
            if (i % 4 != 3) {
                total += static_cast<long>(with[i]) - static_cast<long>(without[i]);
            }
        }
        return total;
    }

    long changed(const std::vector<uint8_t>& with, const std::vector<uint8_t>& without)
    {
        long total = 0;
        for (size_t i = 0; i < with.size(); i++) {
            if (i % 4 != 3) {
                total += std::abs(static_cast<long>(with[i]) - static_cast<long>(without[i]));
            }
        }
        return total;
    }

    int largestDifference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
    {
        int largest = 0;
        for (size_t i = 0; i < a.size(); i++) {
            largest = std::max(largest, std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])));
        }
        return largest;
    }
}

TEST_CASE("soft particles fade translucent and modulated sprites that touch the floor and leave them unchanged away from geometry", "[data]")
{
    testgl::OffscreenContext gl(frameWidth, frameHeight);
    REQUIRE(gl.context != nullptr);
    testworld::Deck deck;
    ut::World& world = deck.world;
    ut::Vector floor = testworld::openSpot(world, 300.0f);
    ut::Vector eye = floor + ut::Vector { 0, 0, 220 };

    ut::UObject* sprite = testworld::spawnBox(world, floor + ut::Vector { 0, 0, 64 });
    ut::UObject* texture = ut::objects::loadObject("Engine.DefaultTexture", nullptr);
    REQUIRE(texture != nullptr);
    ut::actor::DrawType(sprite) = 1;
    ut::actor::Style(sprite) = 3;
    ut::actor::Texture(sprite) = texture;
    ut::actor::DrawScale(sprite) = 1.0f;

    ut::SceneRenderer renderer;
    ut::actor::bHidden(sprite) = 1;
    std::vector<uint8_t> empty = render(renderer, world, eye, false);
    std::vector<uint8_t> emptySoft = render(renderer, world, eye, true);
    std::vector<uint8_t> emptySoftMultisampled = render(renderer, world, eye, true, 4);
    std::vector<uint8_t> emptyMultisampled = render(renderer, world, eye, false, 4);
    ut::actor::bHidden(sprite) = 0;

    ut::actor::Location(sprite) = floor + ut::Vector { 0, 0, 120 };
    std::vector<uint8_t> highHard = render(renderer, world, eye, false);
    std::vector<uint8_t> highSoft = render(renderer, world, eye, true);

    ut::actor::Location(sprite) = floor + ut::Vector { 0, 0, 2 };
    std::vector<uint8_t> lowHard = render(renderer, world, eye, false);
    std::vector<uint8_t> lowSoft = render(renderer, world, eye, true);
    std::vector<uint8_t> lowSoftMultisampled = render(renderer, world, eye, true, 4);
    std::vector<uint8_t> lowHardAgain = render(renderer, world, eye, false);

    CHECK(largestDifference(emptySoft, empty) <= 1);
    CHECK(largestDifference(emptySoftMultisampled, emptyMultisampled) <= 1);

    long highHardAdded = added(highHard, empty);
    long lowHardAdded = added(lowHard, empty);
    REQUIRE(highHardAdded > 20000);
    REQUIRE(lowHardAdded > 5000);
    CHECK(largestDifference(highSoft, highHard) <= 1);

    long lowSoftAdded = added(lowSoft, empty);
    CHECK(lowSoftAdded >= 0);
    CHECK(lowSoftAdded * 5 < lowHardAdded);
    CHECK(added(lowSoftMultisampled, emptyMultisampled) * 5 < added(lowHard, empty));
    CHECK(largestDifference(lowHardAgain, lowHard) == 0);

    ut::actor::Style(sprite) = 4;
    std::vector<uint8_t> modulatedHard = render(renderer, world, eye, false);
    std::vector<uint8_t> modulatedSoft = render(renderer, world, eye, true);
    long modulatedHardChange = changed(modulatedHard, empty);
    REQUIRE(modulatedHardChange > 5000);
    CHECK(changed(modulatedSoft, empty) * 5 < modulatedHardChange);
}
