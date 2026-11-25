#include "core/objects.h"
#include "core/url.h"
#include "engine/actor.h"
#include "engine/props_engine.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <cmath>

TEST_CASE("travelling through a teleporter from a URL with options arrives at the destination portal", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    {
        ut::World world;
        std::string url = ut::travelUrl("DM-Agony?Difficulty=1", "DM-Deck16][#gotheredammit", ut::TRAVEL_Relative);
        REQUIRE(world.loadMap(url));
        ut::UObject* player = world.player();
        REQUIRE(player != nullptr);
        ut::Vector location = ut::actor::Location(player);
        CHECK(std::abs(location.x - 897.6f) < 1.0f);
        CHECK(std::abs(location.y - 1959.8f) < 1.0f);
        CHECK(world.urlOption("Difficulty") == "1");
    }
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
}
