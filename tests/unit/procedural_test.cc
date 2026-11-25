#include "core/objects.h"
#include "engine/props_engine.h"
#include "engine/props_fire.h"
#include "engine/world.h"
#include "render/procedural.h"
#include "render/texture_cache.h"

#include "offscreen_context.h"
#include "test_world.h"

#include <catch_amalgamated.hpp>

#include <vector>

namespace {
    ut::UTexture* makeTexture(const char* className, uint32_t size, const std::vector<uint8_t>& pixels)
    {
        ut::engine::initialize(OPENUT_GAME_DATA);
        ut::UClass* cls = ut::objects::findClass(className);
        REQUIRE(cls != nullptr);
        ut::UObject* object = ut::objects::newObject(cls, ut::objects::packageObject(ut::Name("Transient")), ut::Name(), 0);
        ut::UTexture* texture = ut::cast<ut::UTexture>(object);
        REQUIRE(texture != nullptr);
        ut::TextureMip mip;
        mip.width = size;
        mip.height = size;
        mip.ubits = static_cast<uint8_t>(size == 8 ? 3 : 2);
        mip.vbits = mip.ubits;
        mip.data = pixels;
        texture->mips = { mip };
        return texture;
    }

    std::vector<uint8_t> rampSource(uint32_t size)
    {
        std::vector<uint8_t> pixels;
        for (uint32_t v = 0; v < size; v++) {
            for (uint32_t u = 0; u < size; u++) {
                pixels.push_back(static_cast<uint8_t>(10 + size * v + u));
            }
        }
        return pixels;
    }

    ut::UTexture* makeIce(ut::UTexture* source, ut::UTexture* glass, uint32_t moveIce)
    {
        ut::UTexture* ice = makeTexture("Fire.IceTexture", 8, std::vector<uint8_t>(64, 0));
        ut::icetexture::SourceTexture(ice) = source;
        ut::icetexture::GlassTexture(ice) = glass;
        ut::icetexture::PanningStyle(ice) = 0;
        ut::icetexture::TimeMethod(ice) = 1;
        ut::icetexture::HorizPanSpeed(ice) = 132;
        ut::icetexture::VertPanSpeed(ice) = 129;
        ut::icetexture::MoveIce(ice) = moveIce;
        return ice;
    }

    std::vector<uint8_t> columnGlass()
    {
        std::vector<uint8_t> pixels;
        for (uint32_t v = 0; v < 8; v++) {
            for (uint32_t u = 0; u < 8; u++) {
                pixels.push_back(static_cast<uint8_t>(u));
            }
        }
        return pixels;
    }

    void checkIce(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height, const int (&rows)[8], const int (&columns)[8], int rowStride)
    {
        REQUIRE(width == 8);
        REQUIRE(height == 8);
        REQUIRE(rgba.size() == 8 * 8 * 4);
        for (int v = 0; v < 8; v++) {
            for (int u = 0; u < 8; u++) {
                INFO("pixel " << u << "," << v);
                CHECK(rgba[static_cast<size_t>((v * 8 + u) * 4)] == 10 + rowStride * rows[v] + columns[u]);
            }
        }
    }
}

TEST_CASE("ice texture pans the glass layer over the source", "[data]")
{
    ut::UTexture* source = makeTexture("Engine.Texture", 8, rampSource(8));
    ut::UTexture* glass = makeTexture("Engine.Texture", 8, columnGlass());
    ut::UTexture* ice = makeIce(source, glass, 1);
    ut::ProceduralTextures procedural;
    std::vector<uint8_t> rgba;
    uint32_t width = 0;
    uint32_t height = 0;
    REQUIRE(procedural.update(ice, 1.0, rgba, width, height));
    checkIce(rgba, width, height, { 0, 1, 2, 3, 4, 5, 6, 7 }, { 0, 2, 4, 6, 0, 2, 4, 6 }, 8);
    REQUIRE(procedural.update(ice, 1.5, rgba, width, height));
    checkIce(rgba, width, height, { 0, 1, 2, 3, 4, 5, 6, 7 }, { 4, 6, 0, 2, 4, 6, 0, 2 }, 8);
}

TEST_CASE("the GPU copy of an ice texture holds each new frame in the same texture object", "[data]")
{
    testgl::OffscreenContext gl;
    REQUIRE(gl.context != nullptr);
    ut::UTexture* source = makeTexture("Engine.Texture", 8, rampSource(8));
    ut::UTexture* glass = makeTexture("Engine.Texture", 8, columnGlass());
    ut::UTexture* ice = makeIce(source, glass, 1);
    ut::TextureCache cache;
    auto uploaded = [&](double now, uint32_t& id) {
        cache.beginFrame(now);
        const ut::GLTextureInfo* info = cache.get(ice);
        REQUIRE(info != nullptr);
        REQUIRE(info->width == 8);
        REQUIRE(info->height == 8);
        id = info->id;
        std::vector<uint8_t> rgba(8 * 8 * 4);
        glBindTexture(GL_TEXTURE_2D, info->id);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        CHECK(glGetError() == GL_NO_ERROR);
        return rgba;
    };
    uint32_t firstId = 0;
    uint32_t secondId = 0;
    checkIce(uploaded(1.0, firstId), 8, 8, { 0, 1, 2, 3, 4, 5, 6, 7 }, { 0, 2, 4, 6, 0, 2, 4, 6 }, 8);
    checkIce(uploaded(1.5, secondId), 8, 8, { 0, 1, 2, 3, 4, 5, 6, 7 }, { 4, 6, 0, 2, 4, 6, 0, 2 }, 8);
    CHECK(secondId == firstId);
}

TEST_CASE("ice texture pans the source under a fixed glass layer", "[data]")
{
    ut::UTexture* source = makeTexture("Engine.Texture", 8, rampSource(8));
    ut::UTexture* glass = makeTexture("Engine.Texture", 8, columnGlass());
    ut::UTexture* ice = makeIce(source, glass, 0);
    ut::ProceduralTextures procedural;
    std::vector<uint8_t> rgba;
    uint32_t width = 0;
    uint32_t height = 0;
    REQUIRE(procedural.update(ice, 1.0, rgba, width, height));
    REQUIRE(procedural.update(ice, 1.5, rgba, width, height));
    checkIce(rgba, width, height, { 1, 2, 3, 4, 5, 6, 7, 0 }, { 4, 6, 0, 2, 4, 6, 0, 2 }, 8);
}

TEST_CASE("ice texture without glass shows the upsampled source", "[data]")
{
    ut::UTexture* source = makeTexture("Engine.Texture", 4, rampSource(4));
    ut::UTexture* ice = makeIce(source, nullptr, 1);
    ut::ProceduralTextures procedural;
    std::vector<uint8_t> rgba;
    uint32_t width = 0;
    uint32_t height = 0;
    REQUIRE(procedural.update(ice, 1.0, rgba, width, height));
    checkIce(rgba, width, height, { 0, 0, 1, 1, 2, 2, 3, 3 }, { 0, 0, 1, 1, 2, 2, 3, 3 }, 4);
}

TEST_CASE("undisturbed wave texture shades every pixel with the flat surface light", "[data]")
{
    std::vector<ut::UTexture*> waves = testworld::packageTextures("HubEffects", "WaveTexture");
    REQUIRE_FALSE(waves.empty());
    ut::UTexture* wave = waves[0];
    int32_t numDrops = ut::watertexture::NumDrops(wave);
    uint8_t light = ut::wavetexture::BumpMapLight(wave);
    uint8_t range = ut::wavetexture::PhongRange(wave);
    uint8_t prime = ut::texture::PrimeCount(wave);
    float maxRate = ut::texture::MaxFrameRate(wave);
    ut::watertexture::NumDrops(wave) = 0;
    ut::wavetexture::BumpMapLight(wave) = 85;
    ut::wavetexture::PhongRange(wave) = 0;
    ut::texture::PrimeCount(wave) = 0;
    ut::texture::MaxFrameRate(wave) = 0;

    ut::ProceduralTextures procedural;
    std::vector<uint8_t> rgba;
    uint32_t width = 0;
    uint32_t height = 0;
    REQUIRE(procedural.update(wave, 1.0, rgba, width, height));
    REQUIRE(procedural.update(wave, 1.1, rgba, width, height));
    REQUIRE(procedural.update(wave, 1.2, rgba, width, height));

    ut::watertexture::NumDrops(wave) = numDrops;
    ut::wavetexture::BumpMapLight(wave) = light;
    ut::wavetexture::PhongRange(wave) = range;
    ut::texture::PrimeCount(wave) = prime;
    ut::texture::MaxFrameRate(wave) = maxRate;

    REQUIRE(width == wave->mips[0].width);
    REQUIRE(height == wave->mips[0].height);
    uint32_t color = wave->palette()->colors[221];
    size_t mismatches = 0;
    for (size_t i = 0; i < static_cast<size_t>(width) * height; i++) {
        bool same = rgba[i * 4 + 0] == (color & 0xff) && rgba[i * 4 + 1] == ((color >> 8) & 0xff) && rgba[i * 4 + 2] == ((color >> 16) & 0xff) && rgba[i * 4 + 3] == 255;
        mismatches += same ? 0 : 1;
    }
    CHECK(mismatches == 0);
}

TEST_CASE("a wet texture refracts the live frame of a procedural source", "[data]")
{
    std::vector<ut::UTexture*> fires = testworld::packageTextures("HubEffects", "FireTexture");
    REQUIRE_FALSE(fires.empty());
    ut::UTexture* fire = fires[0];
    REQUIRE_FALSE(fire->mips.empty());
    ut::UClass* cls = ut::objects::findClass("Fire.WetTexture");
    REQUIRE(cls != nullptr);
    ut::UTexture* wet = ut::cast<ut::UTexture>(ut::objects::newObject(cls, ut::objects::packageObject(ut::Name("Transient")), ut::Name(), 0));
    REQUIRE(wet != nullptr);
    ut::TextureMip mip = fire->mips[0];
    mip.data.assign(mip.data.size(), 0);
    wet->mips = { mip };
    ut::wettexture::SourceTexture(wet) = fire;
    ut::watertexture::WaveAmp(wet) = 0;
    ut::watertexture::NumDrops(wet) = 0;
    ut::ProceduralTextures procedural;
    std::vector<uint8_t> fireRgba;
    std::vector<uint8_t> wetRgba;
    uint32_t width = 0;
    uint32_t height = 0;
    int differentFrames = 0;
    std::vector<uint8_t> first;
    for (int step = 1; step <= 4; step++) {
        double time = 0.1 * step;
        REQUIRE(procedural.update(fire, time, fireRgba, width, height));
        REQUIRE(procedural.update(wet, time, wetRgba, width, height));
        INFO("step " << step);
        CHECK(wetRgba == fireRgba);
        if (step == 1) {
            first = wetRgba;
        } else if (wetRgba != first) {
            differentFrames++;
        }
    }
    CHECK(differentFrames > 0);
}

TEST_CASE("procedural textures with no frame rate limit can be capped at 60 steps per second", "[data]")
{
    std::vector<ut::UTexture*> fires = testworld::packageTextures("HubEffects", "FireTexture");
    REQUIRE_FALSE(fires.empty());
    ut::UTexture* fire = fires[0];
    uint8_t prime = ut::texture::PrimeCount(fire);
    float maxRate = ut::texture::MaxFrameRate(fire);
    ut::texture::PrimeCount(fire) = 0;
    ut::texture::MaxFrameRate(fire) = 0;
    std::vector<uint8_t> rgba;
    uint32_t width = 0;
    uint32_t height = 0;

    ut::ProceduralTextures perFrame;
    REQUIRE(perFrame.update(fire, 1.0, rgba, width, height));
    int perFrameSteps = 0;
    for (int i = 1; i <= 480; i++) {
        perFrameSteps += perFrame.update(fire, 1.0 + i / 480.0, rgba, width, height) ? 1 : 0;
    }
    CHECK(perFrameSteps == 480);

    ut::ProceduralTextures capped;
    capped.setConstantRate(60.0f);
    REQUIRE(capped.update(fire, 1.0, rgba, width, height));
    int cappedSteps = 0;
    for (int i = 1; i <= 480; i++) {
        cappedSteps += capped.update(fire, 1.0 + i / 480.0, rgba, width, height) ? 1 : 0;
    }
    ut::texture::PrimeCount(fire) = prime;
    ut::texture::MaxFrameRate(fire) = maxRate;
    CHECK(cappedSteps == 60);
}
