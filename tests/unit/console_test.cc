#include "core/config.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/console.h"
#include "engine/props_engine.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
    std::string sectionValue(const std::filesystem::path& path, const std::string& section, const std::string& key)
    {
        std::ifstream file(path);
        std::string line;
        bool inSection = false;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!line.empty() && line.front() == '[') {
                inSection = line == "[" + section + "]";
                continue;
            }
            if (inSection && line.rfind(key + "=", 0) == 0) {
                return line.substr(key.size() + 1);
            }
        }
        return {};
    }
}

TEST_CASE("SET InputExt rebinds a key and saves it to the user ini", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    std::string original = ut::console::binding(ut::console::keyFromName("N"));
    ut::console::execute(nullptr, "SET InputExt N ShowGoalsWindow");
    CHECK(ut::console::binding(ut::console::keyFromName("N")) == "ShowGoalsWindow");
    CHECK(ut::console::execute(nullptr, "KEYBINDING N") == "ShowGoalsWindow");
    CHECK(sectionValue(ut::config::user().path, "Engine.Input", "N") == "ShowGoalsWindow");
    ut::console::execute(nullptr, "SET InputExt N " + original);
    CHECK(sectionValue(ut::config::user().path, "Engine.Input", "N") == original);
}

TEST_CASE("set ini: writes the audio device section and get reads it back", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    std::string original = ut::console::execute(nullptr, "get ini:Engine.Engine.AudioDevice MusicVolume");
    REQUIRE(original == "160");
    ut::console::execute(nullptr, "set ini:Engine.Engine.AudioDevice MusicVolume 100");
    CHECK(ut::console::execute(nullptr, "get ini:Engine.Engine.AudioDevice MusicVolume") == "100");
    CHECK(sectionValue(ut::config::system().path, "Galaxy.GalaxyAudioSubsystem", "MusicVolume") == "100");
    ut::console::execute(nullptr, "set ini:Engine.Engine.AudioDevice MusicVolume " + original);
    CHECK(sectionValue(ut::config::system().path, "Galaxy.GalaxyAudioSubsystem", "MusicVolume") == "160");
}

TEST_CASE("set of a config property updates the class default and persists it", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    std::string original = ut::console::execute(nullptr, "get PlayerPawn bMessageBeep");
    REQUIRE(original == "True");
    ut::console::execute(nullptr, "set PlayerPawn bMessageBeep False");
    CHECK(ut::console::execute(nullptr, "get PlayerPawn bMessageBeep") == "False");
    CHECK(sectionValue(ut::config::user().path, "Engine.PlayerPawn", "bMessageBeep") == "False");
    ut::console::execute(nullptr, "set PlayerPawn bMessageBeep " + original);
    CHECK(sectionValue(ut::config::user().path, "Engine.PlayerPawn", "bMessageBeep") == "True");
}

TEST_CASE("open start and servertravel pass partial absolute and relative travel types", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    std::vector<std::pair<std::string, int32_t>> requests;
    auto previous = ut::console::openMapHook;
    ut::console::openMapHook = [&](const std::string& url, int32_t travelType) {
        requests.emplace_back(url, travelType);
    };
    ut::console::execute(nullptr, "open DM-Agony?Difficulty=2");
    ut::console::execute(nullptr, "start DM-Deck16][");
    ut::console::execute(nullptr, "servertravel DM-Morpheus");
    ut::console::openMapHook = previous;
    REQUIRE(requests.size() == 3);
    CHECK(requests[0].first == "DM-Agony?Difficulty=2");
    CHECK(requests[0].second == 1);
    CHECK(requests[1].second == 0);
    CHECK(requests[2].second == 2);
}

TEST_CASE("a Talk binding opens the script console with a Say prompt", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    {
        ut::World world;
        REQUIRE(world.loadMap("DM-Deck16]["));
        ut::UObject* scriptConsole = ut::player::Console(world.viewport);
        REQUIRE(scriptConsole != nullptr);
        CHECK(ut::console::bTyping(scriptConsole) == 0);
        ut::console::execute(world.player(), "Talk");
        CHECK(ut::console::bTyping(scriptConsole) != 0);
        CHECK(ut::console::TypedStr(scriptConsole) == "Say ");
    }
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
}

TEST_CASE("Console.ConsoleCommand prints the command output to the console", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    {
        ut::World world;
        REQUIRE(world.loadMap("DM-Deck16]["));
        ut::UObject* scriptConsole = ut::player::Console(world.viewport);
        REQUIRE(scriptConsole != nullptr);
        ut::vm::Call call(scriptConsole, ut::Name("ConsoleCommand"));
        call.s("get PlayerPawn bMessageBeep").run();
        CHECK(ut::console::MsgText(scriptConsole, ut::console::TopLine(scriptConsole)) == "True");
    }
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
}
