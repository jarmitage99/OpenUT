#include "engine/resources.h"

#include "test_world.h"

#include <catch_amalgamated.hpp>

#include <vector>

TEST_CASE("P8 texture loads mips and palette", "[data]")
{
    std::vector<ut::UTexture*> textures = testworld::packageTextures("Ancient");
    REQUIRE_FALSE(textures.empty());
    ut::UTexture* texture = textures[0];
    CHECK(texture->format() == 0);
    REQUIRE(texture->mips.size() > 1);
    for (size_t i = 0; i < texture->mips.size(); i++) {
        const ut::TextureMip& mip = texture->mips[i];
        INFO("mip " << i);
        CHECK(mip.data.size() == static_cast<size_t>(mip.width) * mip.height);
        if (i > 0) {
            CHECK(mip.width == std::max<uint32_t>(texture->mips[i - 1].width / 2, 1));
            CHECK(mip.height == std::max<uint32_t>(texture->mips[i - 1].height / 2, 1));
        }
    }
    CHECK(texture->mips.back().width == 1);
    REQUIRE(texture->palette() != nullptr);
    REQUIRE(texture->palette()->colors.size() == 256);

    const ut::TextureMip& top = texture->mips[0];
    std::vector<uint8_t> rgba = texture->rgba(0);
    REQUIRE(rgba.size() == top.data.size() * 4);
    size_t mismatches = 0;
    for (size_t i = 0; i < top.data.size(); i++) {
        uint32_t color = texture->palette()->colors[top.data[i]];
        bool same = rgba[i * 4] == (color & 0xff) && rgba[i * 4 + 1] == ((color >> 8) & 0xff) && rgba[i * 4 + 2] == ((color >> 16) & 0xff);
        mismatches += same ? 0 : 1;
    }
    CHECK(mismatches == 0);
    CHECK(rgba[3] == 0xff);
}

TEST_CASE("procedural textures have no stored pixels", "[data]")
{
    std::vector<ut::UTexture*> textures = testworld::packageTextures("Liquids", "WetTexture");
    REQUIRE_FALSE(textures.empty());
    CHECK(textures[0]->cls->name == ut::Name("WetTexture"));
    CHECK_FALSE(textures[0]->decodable());
}
