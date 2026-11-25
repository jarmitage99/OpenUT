#include "core/objects.h"
#include "engine/package.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <vector>

TEST_CASE("compact index decodes single and multi byte values")
{
    std::vector<uint8_t> bytes = { 0x05, 0x85, 0x40, 0x01, 0x7f, 0x7f, 0x40, 0x80, 0x02 };
    ut::Reader reader(bytes, 0);
    CHECK(reader.index() == 5);
    CHECK(reader.index() == -5);
    CHECK(reader.index() == 64);
    CHECK(reader.index() == 8191);
    CHECK(reader.index() == 16384);
    CHECK(reader.tell() == bytes.size());
}

TEST_CASE("reader throws when reading past the end")
{
    std::vector<uint8_t> bytes = { 0x01, 0x02 };
    ut::Reader reader(bytes, 0);
    CHECK_THROWS_AS(reader.u32(), std::runtime_error);
}

namespace {
    std::string importPackage(const ut::Package& file, int32_t ref)
    {
        int32_t outer = file.imports()[static_cast<size_t>(-ref - 1)].outer;
        while (outer < 0 && file.imports()[static_cast<size_t>(-outer - 1)].outer != 0) {
            outer = file.imports()[static_cast<size_t>(-outer - 1)].outer;
        }
        return outer < 0 ? file.objectName(outer) : std::string();
    }
}

TEST_CASE("package header and tables of Engine.u", "[data]")
{
    ut::Package package(OPENUT_GAME_DATA "/System/Engine.u");
    CHECK(package.version() == 69);
    CHECK(package.licensee() == 0);
    CHECK(package.names().size() == 3348);
    CHECK(package.exports().size() == 5370);
    CHECK(package.imports().size() == 60);
}

TEST_CASE("imports resolve to exports of the packages that the import table names", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::Linker* map = ut::objects::loadPackage(ut::Name("DM-Deck16]["));
    REQUIRE(map != nullptr);
    const ut::Package& file = map->file();
    size_t textures = 0;
    for (size_t i = 0; i < file.imports().size() && textures < 20; i++) {
        int32_t ref = -static_cast<int32_t>(i + 1);
        if (file.classOf(ref) != "Texture") {
            continue;
        }
        ut::UObject* resolved = map->object(ref);
        INFO(file.objectName(ref));
        REQUIRE(resolved != nullptr);
        CHECK(resolved->name == ut::Name(file.objectName(ref)));
        CHECK(resolved->cls->name == ut::Name("Texture"));
        CHECK(ut::iequals(resolved->outermost()->name.str(), importPackage(file, ref)));
        textures++;
    }
    CHECK(textures == 20);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
}

TEST_CASE("imports from version 61 packages resolve", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    bool found = false;
    for (const char* mapName : { "DM-Deck16][", "DM-Morpheus", "DM-Agony", "CTF-Face", "DM-Fractal" }) {
        ut::Linker* map = ut::objects::loadPackage(ut::Name(mapName));
        REQUIRE(map != nullptr);
        const ut::Package& file = map->file();
        for (size_t i = 0; i < file.imports().size() && !found; i++) {
            int32_t ref = -static_cast<int32_t>(i + 1);
            if (file.classOf(ref) != "Texture") {
                continue;
            }
            ut::Linker* source = ut::objects::loadPackage(ut::Name(importPackage(file, ref)));
            if (!source || source->file().version() != 61) {
                continue;
            }
            ut::UObject* resolved = map->object(ref);
            INFO(mapName << " " << file.objectName(ref));
            REQUIRE(resolved != nullptr);
            CHECK(resolved->name == ut::Name(file.objectName(ref)));
            ut::objects::ensureLoaded(resolved);
            ut::UTexture* texture = ut::cast<ut::UTexture>(resolved);
            REQUIRE(texture != nullptr);
            CHECK(texture->decodable());
            found = true;
        }
        ut::objects::unloadPackage(ut::Name(mapName));
    }
    CHECK(found);
}
