#include "core/config.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/props_engine.h"
#include "engine/world.h"
#include "game_process.h"

#include <catch_amalgamated.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {
    ut::UObject*& objectAt(ut::UObject* object, const char* name, int32_t index = 0)
    {
        return *reinterpret_cast<ut::UObject**>(object->propertyAddress(ut::Name(name), index));
    }

    std::string& stringAt(ut::UObject* object, const char* name, int32_t index = 0)
    {
        return *reinterpret_cast<std::string*>(object->propertyAddress(ut::Name(name), index));
    }

    std::vector<std::string> captions(ut::UObject* list)
    {
        std::vector<std::string> result;
        for (ut::UObject* item = objectAt(list, "Next"); item; item = objectAt(item, "Next")) {
            result.push_back(stringAt(item, "Caption"));
        }
        return result;
    }

    ut::UObject* childOfClass(ut::UObject* window, const char* className)
    {
        for (ut::UObject* child = objectAt(window, "FirstChildWindow"); child; child = objectAt(child, "NextSiblingWindow")) {
            if (child->isA(ut::Name(className))) {
                return child;
            }
        }
        return nullptr;
    }

    struct BrowserWorld {
        ut::World world;
        ut::UObject* menu = nullptr;

        BrowserWorld()
        {
            ut::engine::initialize(OPENUT_GAME_DATA);
            ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
            REQUIRE(world.loadMap("DM-Deck16][?game=Botpack.DeathMatchPlus"));
            ut::UObject* console = ut::player::Console(world.viewport);
            REQUIRE(console != nullptr);
            ut::vm::Call(console, ut::Name("CreateRootWindow")).obj(nullptr).run();
            ut::UObject* root = objectAt(console, "Root");
            REQUIRE(root != nullptr);
            menu = objectAt(objectAt(root, "MenuBar"), "Multiplayer");
            REQUIRE(menu != nullptr);
        }

        ~BrowserWorld()
        {
            ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
        }

        ut::UObject* openBrowser()
        {
            ut::vm::Call(menu, ut::Name("LoadUBrowser")).run();
            ut::UObject* browser = objectAt(menu, "BrowserWindow");
            REQUIRE(browser != nullptr);
            ut::UObject* client = objectAt(browser, "ClientArea");
            REQUIRE(client != nullptr);
            return client;
        }

        ut::UObject* page(ut::UObject* client, const std::string& caption)
        {
            for (ut::UObject* tab = objectAt(objectAt(objectAt(client, "PageControl"), "Items"), "Next"); tab; tab = objectAt(tab, "Next")) {
                if (stringAt(tab, "Caption") == caption) {
                    return objectAt(tab, "Page");
                }
            }
            return nullptr;
        }
    };
}

TEST_CASE("the multiplayer menu offers the server browser in place of the internet only items", "[data]")
{
    BrowserWorld browser;
    std::vector<std::string> expected { "&Server Browser", "&Start New Multiplayer Game", "-", "&Disconnect from Server", "&Reconnect to Server" };
    CHECK(captions(objectAt(browser.menu, "Items")) == expected);
    CHECK(stringAt(browser.menu, "BrowserHelp") == "Find games on your local network, or connect to a server by its address.");
}

TEST_CASE("the server browser lists LAN servers, favorites and a direct connect page", "[data]")
{
    BrowserWorld browser;
    ut::UObject* client = browser.openBrowser();
    ut::UObject* pageControl = objectAt(client, "PageControl");
    std::vector<std::string> expected { "LAN Servers", "Favorites", "Direct Connect" };
    CHECK(captions(objectAt(pageControl, "Items")) == expected);
    CHECK(objectAt(pageControl, "SelectedTab") == objectAt(client, "LANPage"));
    ut::UObject* lan = browser.page(client, "LAN Servers");
    REQUIRE(lan != nullptr);
    CHECK(lan == objectAt(client, "FactoryWindows"));
    CHECK(stringAt(lan, "ListFactories").rfind("UBrowser.UBrowserLocalFact,", 0) == 0);
    ut::UObject* factory = objectAt(lan, "Factories");
    REQUIRE(factory != nullptr);
    CHECK(factory->isA(ut::Name("UBrowserLocalFact")));
    CHECK(objectAt(client, "Favorites") != nullptr);
    CHECK(objectAt(client, "InfoWindow") != nullptr);
}

TEST_CASE("a named window of a per object config class saves its settings to its own section", "[data]")
{
    BrowserWorld browser;
    ut::UObject* lan = browser.page(browser.openBrowser(), "LAN Servers");
    REQUIRE(lan != nullptr);
    CHECK(stringAt(lan, "URLAppend") == "?LAN");
    stringAt(lan, "URLAppend") = "?LAN?Saved";
    ut::vm::Call(lan, ut::Name("SaveConfig")).run();
    CHECK(ut::config::system().get("UBrowserLAN", "URLAppend").value_or("") == "?LAN?Saved");
    CHECK_FALSE(ut::config::system().get("UTBrowser.UTBrowserServerListWindow", "URLAppend").has_value());
    ut::config::system().set("UBrowserLAN", "URLAppend", "?LAN");
    ut::config::system().save();
}

TEST_CASE("direct connect travels to the typed address and keeps it in the history", "[data]")
{
    BrowserWorld browser;
    ut::UObject* page = browser.page(browser.openBrowser(), "Direct Connect");
    REQUIRE(page != nullptr);
    ut::UObject* combo = childOfClass(page, "UWindowComboControl");
    ut::UObject* button = childOfClass(page, "UWindowSmallButton");
    REQUIRE(combo != nullptr);
    REQUIRE(button != nullptr);
    CHECK(stringAt(button, "Text") == "Connect");

    ut::vm::Call(combo, ut::Name("SetValue")).s("10.0.0.5").run();
    ut::vm::Call(button, ut::Name("Notify")).byte(2).run();
    CHECK(browser.world.travelPending);
    CHECK(browser.world.travelUrl == "unreal://10.0.0.5");

    ut::UObject* again = browser.page(browser.openBrowser(), "Direct Connect");
    ut::UObject* field = childOfClass(again, "UWindowComboControl");
    ut::vm::Call(field, ut::Name("SetValue")).s("192.168.1.20:7777").run();
    ut::vm::Call(field, ut::Name("Notify")).byte(7).run();
    CHECK(browser.world.travelUrl == "unreal://192.168.1.20:7777");
    CHECK(ut::config::system().get("UBrowser.UBrowserOpenCW", "OpenHistory[0]").value_or("") == "192.168.1.20:7777");
    CHECK(ut::config::system().get("UBrowser.UBrowserOpenCW", "OpenHistory[1]").value_or("") == "10.0.0.5");
}

TEST_CASE("an empty address on the direct connect page does not travel", "[data]")
{
    BrowserWorld browser;
    ut::UObject* page = browser.page(browser.openBrowser(), "Direct Connect");
    REQUIRE(page != nullptr);
    ut::vm::Call(childOfClass(page, "UWindowSmallButton"), ut::Name("Notify")).byte(2).run();
    CHECK_FALSE(browser.world.travelPending);
}

#if defined(OPENUT_PATH) && !defined(_WIN32)
namespace {
    ut::UObject* pingedServer(ut::World& world, ut::UObject* listWindow, int32_t gamePort)
    {
        for (int step = 0; step < 400; step++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            world.tick(0.05f);
            ut::UObject* pinged = objectAt(listWindow, "PingedList");
            for (ut::UObject* entry = pinged ? objectAt(pinged, "Next") : nullptr; entry; entry = objectAt(entry, "Next")) {
                if (*reinterpret_cast<int32_t*>(entry->propertyAddress(ut::Name("GamePort"))) == gamePort) {
                    return entry;
                }
            }
        }
        return nullptr;
    }

    struct SavedFavorite {
        ut::UObject* defaults = ut::objects::findClass("UBrowser.UBrowserFavoritesFact")->defaultObject;

        explicit SavedFavorite(const std::string& entry)
        {
            *reinterpret_cast<int32_t*>(defaults->propertyAddress(ut::Name("FavoriteCount"))) = 1;
            stringAt(defaults, "Favorites") = entry;
        }

        ~SavedFavorite()
        {
            *reinterpret_cast<int32_t*>(defaults->propertyAddress(ut::Name("FavoriteCount"))) = 0;
            stringAt(defaults, "Favorites").clear();
        }

        SavedFavorite(const SavedFavorite&) = delete;
        SavedFavorite& operator=(const SavedFavorite&) = delete;
    };
}

TEST_CASE("the LAN and favorites pages find and ping a dedicated server on this machine", "[data][net]")
{
    testprocess::ScratchDirectory scratch;
    int32_t port = 20000 + getpid() % 20000;
    testprocess::GameProcess server("--server \"DM-Deck16][?game=Botpack.DeathMatchPlus\" --port " + std::to_string(port), scratch.path / "server.log", "");
    REQUIRE(testprocess::eventually([&]() {
        return server.output().find("server running") != std::string::npos;
    },
        120));
    BrowserWorld browser;
    SavedFavorite saved("Saved\\127.0.0.1\\" + std::to_string(port + 1) + "\\False");
    ut::UObject* client = browser.openBrowser();
    ut::UObject* lan = browser.page(client, "LAN Servers");
    REQUIRE(lan != nullptr);
    ut::UObject* found = pingedServer(browser.world, lan, port);
    REQUIRE(found != nullptr);
    CHECK(stringAt(found, "IP") == "127.0.0.1");
    CHECK(stringAt(found, "MapName") == "DM-Deck16][");

    ut::UObject* favoritesTab = objectAt(client, "Favorites");
    ut::vm::Call(objectAt(client, "PageControl"), ut::Name("GotoTab")).obj(favoritesTab).b(true).run();
    ut::UObject* favorite = pingedServer(browser.world, objectAt(favoritesTab, "Page"), port);
    REQUIRE(favorite != nullptr);
    CHECK(stringAt(favorite, "MapName") == "DM-Deck16][");
}
#endif
