#include "core/objects.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/world.h"
#include "render/texture_cache.h"

#include <catch_amalgamated.hpp>

namespace {
    ut::UTexture* makeTexture(ut::UClass* cls)
    {
        ut::UObject* object = ut::objects::newObject(cls, ut::objects::packageObject(ut::Name("Transient")), ut::Name(), 0);
        return ut::cast<ut::UTexture>(object);
    }
}

TEST_CASE("animated textures advance like UTexture::Update and Tick", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::UClass* cls = ut::objects::findClass("Engine.Texture");
    REQUIRE(cls != nullptr);
    ut::UTexture* a = makeTexture(cls);
    ut::UTexture* b = makeTexture(cls);
    ut::UTexture* c = makeTexture(cls);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    ut::texture::AnimNext(a) = b;
    ut::texture::AnimNext(b) = c;
    ut::texture::AnimNext(c) = a;

    ut::texture::MaxFrameRate(a) = 0.0f;
    ut::TextureAnimation perFrame;
    CHECK(ut::TextureCache::advance(a, perFrame, 0.01) == b);
    CHECK(ut::TextureCache::advance(a, perFrame, 0.01) == b);
    CHECK(ut::TextureCache::advance(a, perFrame, 0.02) == c);
    CHECK(ut::TextureCache::advance(a, perFrame, 0.03) == a);

    ut::texture::MaxFrameRate(a) = 20.0f;
    ut::texture::MinFrameRate(a) = 10.0f;
    ut::TextureAnimation limited;
    limited.lastUpdate = 1.0;
    CHECK(ut::TextureCache::advance(a, limited, 1.03) == a);
    CHECK(ut::TextureCache::advance(a, limited, 1.06) == b);
    CHECK(limited.accumulator == 0.0f);
    CHECK(ut::TextureCache::advance(a, limited, 1.31) == c);
    CHECK(limited.accumulator == Catch::Approx(0.1f).margin(1e-5));

    ut::texture::MaxFrameRate(a) = 0.0f;
    ut::texture::PrimeCount(a) = 2;
    ut::TextureAnimation primed;
    CHECK(ut::TextureCache::advance(a, primed, 0.5) == a);
}

TEST_CASE("animated textures with no frame rate limit can be capped at 60 frames per second", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::UClass* cls = ut::objects::findClass("Engine.Texture");
    REQUIRE(cls != nullptr);
    ut::UTexture* a = makeTexture(cls);
    ut::UTexture* b = makeTexture(cls);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    ut::texture::AnimNext(a) = b;
    ut::texture::AnimNext(b) = a;
    ut::texture::MaxFrameRate(a) = 0.0f;

    ut::TextureAnimation capped;
    capped.lastUpdate = 1.0;
    ut::UTexture* shown = a;
    int changes = 0;
    for (int i = 1; i <= 480; i++) {
        ut::UTexture* next = ut::TextureCache::advance(a, capped, 1.0 + i / 480.0, 60.0f);
        if (next != shown) {
            changes++;
        }
        shown = next;
    }
    CHECK(changes == 60);

    ut::TextureAnimation perFrame;
    perFrame.lastUpdate = 1.0;
    shown = a;
    changes = 0;
    for (int i = 1; i <= 480; i++) {
        ut::UTexture* next = ut::TextureCache::advance(a, perFrame, 1.0 + i / 480.0);
        if (next != shown) {
            changes++;
        }
        shown = next;
    }
    CHECK(changes == 480);
}
