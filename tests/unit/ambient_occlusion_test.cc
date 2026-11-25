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
#include <tuple>
#include <vector>

namespace {
    constexpr int frameWidth = 640;
    constexpr int frameHeight = 480;

    struct Box {
        int x0 = frameWidth;
        int y0 = frameHeight;
        int x1 = -1;
        int y1 = -1;

        bool contains(int x, int y, int margin) const
        {
            return x >= x0 - margin && x <= x1 + margin && y >= y0 - margin && y <= y1 + margin;
        }
    };

    std::vector<uint8_t> render(ut::SceneRenderer& renderer, ut::World& world, ut::OcclusionQuality occlusion, const ut::Vector& eye, const ut::Rotator& rotation = ut::Rotator { -16384, 0, 0 }, int multisample = 0, int width = frameWidth, int height = frameHeight)
    {
        ut::RenderOptions options;
        options.ambientOcclusion = occlusion;
        options.multisample = multisample;
        renderer.setOptions(options);
        renderer.setTime(1.0);
        renderer.beginFrame(width, height);
        renderer.drawScene(world.level, world.actors, eye, rotation, 90.0f, world.player());
        renderer.endFrame();
        renderer.applyBrightness(0.5f);
        std::vector<uint8_t> rgba;
        int readWidth = 0;
        int readHeight = 0;
        renderer.readPixels(rgba, readWidth, readHeight);
        CHECK(glGetError() == GL_NO_ERROR);
        return rgba;
    }

    ut::UObject* lowCrate(ut::World& world)
    {
        ut::Vector floor = testworld::openSpot(world, 300.0f);
        ut::UObject* crate = testworld::spawnBox(world, floor + ut::Vector { 0, 0, 64 });
        ut::actor::DrawScale(crate) *= 0.5f;
        ut::actor::CollisionRadius(crate) *= 0.5f;
        ut::actor::CollisionHeight(crate) *= 0.5f;
        ut::actor::Location(crate) = floor + ut::Vector { 0, 0, ut::actor::CollisionHeight(crate) };
        return crate;
    }

    int brightness(const std::vector<uint8_t>& rgba, int x, int y, int width = frameWidth)
    {
        size_t at = static_cast<size_t>(y * width + x) * 4;
        return rgba[at] + rgba[at + 1] + rgba[at + 2];
    }
}

TEST_CASE("every ambient occlusion level darkens the floor around a crate and leaves open floor unchanged", "[data]")
{
    testgl::OffscreenContext gl(frameWidth, frameHeight);
    REQUIRE(gl.context != nullptr);
    testworld::Deck deck;
    ut::World& world = deck.world;
    ut::UObject* crate = lowCrate(world);
    ut::Vector eye = ut::actor::Location(crate) + ut::Vector { 0, 0, 220 };

    ut::SceneRenderer renderer;
    render(renderer, world, ut::OcclusionQuality::Off, eye);
    std::vector<uint8_t> plain = render(renderer, world, ut::OcclusionQuality::Off, eye);
    std::vector<uint8_t> low = render(renderer, world, ut::OcclusionQuality::Low, eye);
    std::vector<uint8_t> medium = render(renderer, world, ut::OcclusionQuality::Medium, eye);
    std::vector<uint8_t> high = render(renderer, world, ut::OcclusionQuality::High, eye);
    std::vector<uint8_t> plainAgain = render(renderer, world, ut::OcclusionQuality::Off, eye);
    std::vector<uint8_t> lowAfterOff = render(renderer, world, ut::OcclusionQuality::Low, eye);
    std::vector<uint8_t> plainMultisampled = render(renderer, world, ut::OcclusionQuality::Off, eye, ut::Rotator { -16384, 0, 0 }, 4);
    std::vector<uint8_t> lowMultisampled = render(renderer, world, ut::OcclusionQuality::Low, eye, ut::Rotator { -16384, 0, 0 }, 4);
    std::vector<uint8_t> plainMultisampledAgain = render(renderer, world, ut::OcclusionQuality::Off, eye, ut::Rotator { -16384, 0, 0 }, 4);
    std::vector<uint8_t> lowAfterMultisample = render(renderer, world, ut::OcclusionQuality::Low, eye);
    renderer.beginFrame(frameWidth - 3, frameHeight - 3);
    renderer.drawScene(world.level, world.actors, eye, ut::Rotator { -16384, 0, 0 }, 90.0f, world.player());
    renderer.endFrame();
    renderer.applyBrightness(0.5f);
    CHECK(glGetError() == GL_NO_ERROR);
    std::vector<uint8_t> lowAfterResize = render(renderer, world, ut::OcclusionQuality::Low, eye);
    ut::actor::bHidden(crate) = 1;
    std::vector<uint8_t> empty = render(renderer, world, ut::OcclusionQuality::Off, eye);
    CHECK(plainAgain == plain);
    CHECK(lowAfterOff == low);
    CHECK(plainMultisampled != plain);
    CHECK(plainMultisampledAgain == plainMultisampled);
    CHECK(lowAfterMultisample == low);
    CHECK(lowAfterResize == low);
    CHECK(low != medium);
    CHECK(medium != high);

    Box crateBox;
    for (int y = 0; y < frameHeight; y++) {
        for (int x = 0; x < frameWidth; x++) {
            if (brightness(plain, x, y) != brightness(empty, x, y)) {
                crateBox.x0 = std::min(crateBox.x0, x);
                crateBox.y0 = std::min(crateBox.y0, y);
                crateBox.x1 = std::max(crateBox.x1, x);
                crateBox.y1 = std::max(crateBox.y1, y);
            }
        }
    }
    REQUIRE(crateBox.x1 > crateBox.x0);
    REQUIRE(crateBox.contains(frameWidth / 2, frameHeight / 2, 0));

    const std::tuple<const char*, const std::vector<uint8_t>*, const std::vector<uint8_t>*> levels[4] = { { "low", &low, &plain }, { "medium", &medium, &plain }, { "high", &high, &plain }, { "low multisampled", &lowMultisampled, &plainMultisampled } };
    for (const auto& [name, occluded, baseline] : levels) {
        INFO("level " << name);
        int brightened = 0;
        for (int y = 0; y < frameHeight; y++) {
            for (int x = 0; x < frameWidth; x++) {
                brightened = std::max(brightened, brightness(*occluded, x, y) - brightness(*baseline, x, y));
            }
        }
        long ringBefore = 0;
        long ringAfter = 0;
        long openBefore = 0;
        long openAfter = 0;
        int openWorst = 0;
        for (int y = 20; y < 300; y++) {
            for (int x = 215; x < 515; x++) {
                int before = brightness(*baseline, x, y);
                int after = brightness(*occluded, x, y);
                if (crateBox.contains(x, y, 0)) {
                    continue;
                }
                if (crateBox.contains(x, y, 10)) {
                    ringBefore += before;
                    ringAfter += after;
                } else if (!crateBox.contains(x, y, 60)) {
                    openBefore += before;
                    openAfter += after;
                    openWorst = std::max(openWorst, before - after);
                }
            }
        }
        CHECK(brightened == 0);
        CHECK(ringAfter * 100 < ringBefore * 99);
        CHECK((openBefore - openAfter) * 500 < openBefore);
        CHECK(openWorst <= 9);
    }
}

TEST_CASE("ambient occlusion at the foot of a crate is no darker than a wall would make it", "[data]")
{
    testgl::OffscreenContext gl(frameWidth, frameHeight);
    REQUIRE(gl.context != nullptr);
    testworld::Deck deck;
    ut::World& world = deck.world;
    ut::UObject* crate = lowCrate(world);
    ut::Vector eye = ut::actor::Location(crate) + ut::Vector { -150, 0, 40 };
    ut::Rotator rotation { -2000, 0, 0 };

    ut::SceneRenderer renderer;
    render(renderer, world, ut::OcclusionQuality::Off, eye, rotation);
    std::vector<uint8_t> plain = render(renderer, world, ut::OcclusionQuality::Off, eye, rotation);
    ut::actor::bHidden(crate) = 1;
    std::vector<uint8_t> empty = render(renderer, world, ut::OcclusionQuality::Off, eye, rotation);
    ut::actor::bHidden(crate) = 0;

    Box crateBox;
    for (int y = 0; y < frameHeight; y++) {
        for (int x = 0; x < frameWidth; x++) {
            if (brightness(plain, x, y) != brightness(empty, x, y)) {
                crateBox.x0 = std::min(crateBox.x0, x);
                crateBox.y0 = std::min(crateBox.y0, y);
                crateBox.x1 = std::max(crateBox.x1, x);
                crateBox.y1 = std::max(crateBox.y1, y);
            }
        }
    }
    REQUIRE(crateBox.x1 > crateBox.x0);
    REQUIRE(crateBox.y1 < frameHeight - 10);

    const std::pair<const char*, ut::OcclusionQuality> levels[3] = { { "low", ut::OcclusionQuality::Low }, { "medium", ut::OcclusionQuality::Medium }, { "high", ut::OcclusionQuality::High } };
    for (const auto& [name, level] : levels) {
        INFO("level " << name);
        std::vector<uint8_t> occluded = render(renderer, world, level, eye, rotation);
        int measured = 0;
        double darkest = 1.0;
        for (int y = crateBox.y1 + 1; y <= crateBox.y1 + 10; y++) {
            for (int x = crateBox.x0; x <= crateBox.x1; x++) {
                int before = brightness(plain, x, y);
                if (before > 60) {
                    darkest = std::min(darkest, static_cast<double>(brightness(occluded, x, y)) / before);
                    measured++;
                }
            }
        }
        REQUIRE(measured > 100);
        CHECK(darkest < 0.9);
        CHECK(darkest > 0.45);
    }
}

TEST_CASE("ambient occlusion leaves an unlit sprite beside a crate untouched", "[data]")
{
    testgl::OffscreenContext gl(frameWidth, frameHeight);
    REQUIRE(gl.context != nullptr);
    testworld::Deck deck;
    ut::World& world = deck.world;
    ut::UClass* crateClass = ut::objects::findClass("UnrealShare.WoodenBox");
    ut::objects::flushLoads();
    REQUIRE(crateClass != nullptr);
    std::vector<ut::UTexture*> ancient = testworld::packageTextures("Ancient");
    REQUIRE_FALSE(ancient.empty());
    ut::UObject* brick = ancient[0];
    ut::UObject* crate = lowCrate(world);
    float halfWidth = 32.0f;
    ut::Vector beside = ut::actor::Location(crate);
    beside.x += ut::actor::CollisionRadius(crate) + halfWidth + 2.0f;
    beside.z -= ut::actor::CollisionHeight(crate) - 1.0f;
    ut::UObject* sprite = world.spawn(crateClass, nullptr, ut::Name(), beside, {}, nullptr, true);
    REQUIRE(sprite != nullptr);
    ut::actor::DrawType(sprite) = 1;
    ut::actor::Style(sprite) = 1;
    ut::actor::Texture(sprite) = brick;
    ut::actor::DrawScale(sprite) = halfWidth * 2.0f / static_cast<float>(ut::cast<ut::UTexture>(brick)->width());
    ut::Vector eye = ut::actor::Location(crate) + ut::Vector { 0, 0, 220 };

    ut::SceneRenderer renderer;
    render(renderer, world, ut::OcclusionQuality::Off, eye);
    std::vector<uint8_t> plain = render(renderer, world, ut::OcclusionQuality::Off, eye);
    ut::actor::bHidden(sprite) = 1;
    std::vector<uint8_t> withoutSprite = render(renderer, world, ut::OcclusionQuality::Off, eye);
    std::vector<uint8_t> occludedWithoutSprite = render(renderer, world, ut::OcclusionQuality::Low, eye);
    ut::actor::bHidden(sprite) = 0;

    int spritePixels = 0;
    int shadedUnderSprite = 0;
    for (int y = 0; y < frameHeight; y++) {
        for (int x = 0; x < frameWidth; x++) {
            if (brightness(plain, x, y) != brightness(withoutSprite, x, y)) {
                spritePixels++;
                if (brightness(occludedWithoutSprite, x, y) < brightness(withoutSprite, x, y)) {
                    shadedUnderSprite++;
                }
            }
        }
    }
    REQUIRE(spritePixels > 2000);
    REQUIRE(shadedUnderSprite > 20);

    const std::pair<const char*, ut::OcclusionQuality> levels[3] = { { "low", ut::OcclusionQuality::Low }, { "medium", ut::OcclusionQuality::Medium }, { "high", ut::OcclusionQuality::High } };
    for (const auto& [name, level] : levels) {
        INFO("level " << name);
        std::vector<uint8_t> occluded = render(renderer, world, level, eye);
        int changed = 0;
        for (int y = 0; y < frameHeight; y++) {
            for (int x = 0; x < frameWidth; x++) {
                if (brightness(plain, x, y) != brightness(withoutSprite, x, y) && brightness(occluded, x, y) != brightness(plain, x, y)) {
                    changed++;
                }
            }
        }
        CHECK(changed == 0);
    }
}

TEST_CASE("a view of the sky renders without GL errors with and without ambient occlusion", "[data]")
{
    testgl::OffscreenContext gl(frameWidth, frameHeight);
    REQUIRE(gl.context != nullptr);
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("CTF-Face"));
    ut::World world;
    REQUIRE(world.loadMap("CTF-Face"));
    ut::UObject* player = world.player();
    REQUIRE(player != nullptr);
    ut::Vector eye = ut::actor::Location(player);
    ut::Rotator rotation { 3000, ut::actor::Rotation(player).yaw, 0 };

    ut::SceneRenderer renderer;
    std::vector<uint8_t> plain = render(renderer, world, ut::OcclusionQuality::Off, eye, rotation);
    std::vector<uint8_t> occluded = render(renderer, world, ut::OcclusionQuality::Low, eye, rotation);
    CHECK(occluded != plain);
    ut::objects::unloadPackage(ut::Name("CTF-Face"));
}

TEST_CASE("ambient occlusion at an odd resolution darkens the floor around a crate and leaves open floor unchanged", "[data]")
{
    testgl::OffscreenContext gl(frameWidth, frameHeight);
    REQUIRE(gl.context != nullptr);
    testworld::Deck deck;
    ut::World& world = deck.world;
    ut::UObject* crate = lowCrate(world);
    ut::Vector eye = ut::actor::Location(crate) + ut::Vector { 0, 0, 220 };
    ut::Rotator down { -16384, 0, 0 };
    int width = frameWidth - 3;
    int height = frameHeight - 3;

    ut::SceneRenderer renderer;
    render(renderer, world, ut::OcclusionQuality::Off, eye, down, 0, width, height);
    std::vector<uint8_t> plain = render(renderer, world, ut::OcclusionQuality::Off, eye, down, 0, width, height);
    std::vector<uint8_t> low = render(renderer, world, ut::OcclusionQuality::Low, eye, down, 0, width, height);
    ut::actor::bHidden(crate) = 1;
    std::vector<uint8_t> empty = render(renderer, world, ut::OcclusionQuality::Off, eye, down, 0, width, height);
    REQUIRE(plain.size() == static_cast<size_t>(width * height * 4));

    Box crateBox;
    int brightened = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (brightness(plain, x, y, width) != brightness(empty, x, y, width)) {
                crateBox.x0 = std::min(crateBox.x0, x);
                crateBox.y0 = std::min(crateBox.y0, y);
                crateBox.x1 = std::max(crateBox.x1, x);
                crateBox.y1 = std::max(crateBox.y1, y);
            }
            brightened = std::max(brightened, brightness(low, x, y, width) - brightness(plain, x, y, width));
        }
    }
    REQUIRE(crateBox.x1 > crateBox.x0);
    REQUIRE(crateBox.contains(width / 2, height / 2, 0));

    long ringBefore = 0;
    long ringAfter = 0;
    long openBefore = 0;
    long openAfter = 0;
    int openWorst = 0;
    for (int y = 20; y < 300; y++) {
        for (int x = 215; x < 515; x++) {
            int before = brightness(plain, x, y, width);
            int after = brightness(low, x, y, width);
            if (crateBox.contains(x, y, 0)) {
                continue;
            }
            if (crateBox.contains(x, y, 10)) {
                ringBefore += before;
                ringAfter += after;
            } else if (!crateBox.contains(x, y, 60)) {
                openBefore += before;
                openAfter += after;
                openWorst = std::max(openWorst, before - after);
            }
        }
    }
    CHECK(brightened == 0);
    CHECK(ringAfter * 100 < ringBefore * 99);
    CHECK((openBefore - openAfter) * 500 < openBefore);
    CHECK(openWorst <= 9);
}
