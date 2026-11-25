#include "compiler/exporter.h"
#include "core/objects.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <string>

TEST_CASE("exported string defaults escape backslashes so they import unchanged", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::UClass* setup = ut::objects::findClass("UBrowser.UBrowserIRCSetupClient");
    REQUIRE(setup != nullptr);
    ut::UClass* parent = setup->superClass();
    std::string text = ut::compiler::exportProperties(setup, setup->defaultObject->data, parent, parent->defaultObject->data, 4);
    CHECK(text.find("     WarningText=\"The Chat facility will connect you to the Internet Relay Chat (IRC) network.\\\\n\\\\nEpic Games") != std::string::npos);
}

TEST_CASE("finding an object by path skips same named objects of another class", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    REQUIRE(ut::objects::loadPackage(ut::Name("GreatFire")) != nullptr);
    ut::UClass* texture = ut::objects::findClass("Engine.Texture");
    ut::UClass* palette = ut::objects::findClass("Engine.Palette");
    REQUIRE(texture != nullptr);
    REQUIRE(palette != nullptr);
    ut::UObject* found = ut::objects::findObject("GreatFire.ancflame1", texture);
    REQUIRE(found != nullptr);
    CHECK(found->isA(texture));
    CHECK(found->pathName() == "GreatFire.ancflame1");
    ut::UObject* other = ut::objects::findObject("GreatFire.ancflame1", palette);
    REQUIRE(other != nullptr);
    CHECK(other->isA(palette));
    CHECK(other != found);
}
