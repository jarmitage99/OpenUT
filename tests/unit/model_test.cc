#include "core/objects.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <filesystem>

namespace {
    ut::ULevel* loadLevel(const std::string& map)
    {
        ut::engine::initialize(OPENUT_GAME_DATA);
        ut::Linker* linker = ut::objects::loadPackage(ut::Name(map));
        if (!linker) {
            return nullptr;
        }
        int32_t ref = linker->findExport(ut::Name("MyLevel"), ut::Name("Level"), "");
        ut::ULevel* level = ut::cast<ut::ULevel>(linker->object(ref));
        ut::objects::ensureLoaded(level);
        return level;
    }
}

TEST_CASE("every map loads without native serialization mismatches", "[data]")
{
    size_t maps = 0;
    for (const auto& entry : std::filesystem::directory_iterator(OPENUT_GAME_DATA "/Maps")) {
        ut::ULevel* level = loadLevel(entry.path().stem().string());
        INFO(entry.path().filename().string());
        REQUIRE(level != nullptr);
        ut::objects::ensureLoaded(level->model);
        CHECK_FALSE(level->model->nodes.empty());
        ut::objects::unloadPackage(ut::Name(entry.path().stem().string()));
        maps++;
    }
    CHECK(maps == 83);
    const std::vector<std::string>& mismatches = ut::objects::loadMismatches();
    INFO("first mismatch: " << (mismatches.empty() ? std::string() : mismatches.front()) << " of " << mismatches.size());
    CHECK(mismatches.empty());
}
