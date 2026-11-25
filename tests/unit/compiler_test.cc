#include "compiler/assets.h"
#include "compiler/exporter.h"
#include "compiler/package_dump.h"
#include "core/objects.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <set>
#include <string>
#include <vector>

namespace {
    std::string lowered(std::string text)
    {
        for (char& c : text) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return text;
    }

    std::string normalized(const ut::compiler::DumpedExport& entry)
    {
        static const std::regex dependencyCrc(R"((\{[^}]*\}:[01]):[0-9]+)");
        static const std::regex importTime(R"(InternalTime\[[01]\]:2=[0-9a-f]+ )");
        static const std::regex leadingText(R"(text=\s+)");
        std::string body = std::regex_replace(entry.body, dependencyCrc, "$1");
        body = std::regex_replace(body, importTime, "");
        if (entry.className == "TextBuffer") {
            body = std::regex_replace(body, leadingText, "text=", std::regex_constants::format_first_only);
            while (!body.empty() && (body.back() == '\r' || body.back() == '\n' || body.back() == ' ')) {
                body.pop_back();
            }
        }
        return lowered(body);
    }

    std::map<std::string, ut::compiler::DumpedExport> dumpByPath(const std::filesystem::path& file)
    {
        std::map<std::string, ut::compiler::DumpedExport> result;
        for (auto& [path, entry] : ut::compiler::dumpPackage(file, std::filesystem::path(OPENUT_GAME_DATA) / "System")) {
            result[lowered(path)] = entry;
        }
        return result;
    }

    std::vector<std::string> editPackages()
    {
        std::ifstream in(std::filesystem::path(OPENUT_GAME_DATA) / "System" / "UnrealTournament.ini");
        std::vector<std::string> packages;
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("EditPackages=", 0) != 0) {
                continue;
            }
            std::string name = line.substr(13);
            while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) {
                name.pop_back();
            }
            packages.push_back(name);
        }
        return packages;
    }

    std::string renamed(const std::string& body, const std::regex& pattern, const std::map<std::string, std::string>& names)
    {
        std::string result;
        auto last = body.cbegin();
        for (std::sregex_iterator it(body.begin(), body.end(), pattern), end; it != end; ++it) {
            auto found = names.find(lowered((*it)[1].str()));
            if (found == names.end()) {
                continue;
            }
            result.append(last, (*it)[1].first);
            result += found->second;
            last = (*it)[1].second;
        }
        result.append(last, body.cend());
        return result;
    }

    std::string sortedTags(const std::string& body)
    {
        size_t open = body.find("props[");
        size_t close = open == std::string::npos ? std::string::npos : body.find(" ]", open);
        if (close == std::string::npos) {
            return body;
        }
        open += 6;
        std::string inner = body.substr(open, close - open);
        static const std::regex tagStart(R"((^|\s)(?=[a-z_0-9]+\[\d+\]:\d+=))");
        std::vector<size_t> cuts;
        for (std::sregex_iterator it(inner.begin(), inner.end(), tagStart), end; it != end; ++it) {
            cuts.push_back(static_cast<size_t>(it->position() + it->length()));
        }
        std::vector<std::string> tags;
        for (size_t i = 0; i < cuts.size(); i++) {
            std::string tag = inner.substr(cuts[i], (i + 1 < cuts.size() ? cuts[i + 1] : inner.size()) - cuts[i]);
            while (!tag.empty() && tag.back() == ' ') {
                tag.pop_back();
            }
            tags.push_back(tag);
        }
        std::sort(tags.begin(), tags.end());
        std::string joined;
        for (const std::string& tag : tags) {
            joined += tag + " ";
        }
        return body.substr(0, open) + joined + body.substr(close + 1);
    }

    std::map<std::string, std::string> comparable(const std::map<std::string, ut::compiler::DumpedExport>& dump)
    {
        static const std::regex paletteRef(R"(Palette\[0\]:5=\{([^}]*)\})");
        static const std::regex pageRef(R"(\{([^}]*\.texture\d+)\})", std::regex::icase);
        static const std::regex position(R"(pos=(\d+))");
        std::map<std::string, std::string> palettes;
        std::map<std::string, std::string> pages;
        std::map<std::string, size_t> shift;
        for (const auto& [path, entry] : dump) {
            for (std::sregex_iterator it(entry.body.begin(), entry.body.end(), paletteRef), end; it != end; ++it) {
                palettes.emplace(lowered((*it)[1].str()), "palette#" + std::to_string(palettes.size()));
            }
            if (entry.className == "Font") {
                static const std::regex fontPage(R"(page=\{([^}]*)\})");
                for (std::sregex_iterator it(entry.body.begin(), entry.body.end(), fontPage), end; it != end; ++it) {
                    pages.emplace(lowered((*it)[1].str()), path + ".page#" + std::to_string(pages.size()));
                }
            }
            size_t text = entry.className == "TextBuffer" ? entry.body.find("text=") : std::string::npos;
            size_t leading = 0;
            while (text != std::string::npos && text + 5 + leading < entry.body.size() && std::isspace(static_cast<unsigned char>(entry.body[text + 5 + leading]))) {
                leading++;
            }
            if (leading > 0 && path.size() > 11 && path.substr(path.size() - 11) == ".scripttext") {
                shift[path.substr(0, path.size() - 11)] = leading;
            }
        }
        std::map<std::string, std::string> keys;
        for (const auto& [ref, name] : palettes) {
            keys[ref.substr(ref.find('.') + 1)] = name;
        }
        for (const auto& [ref, name] : pages) {
            keys[ref.substr(ref.find('.') + 1)] = name;
        }
        std::map<std::string, std::string> result;
        for (const auto& [path, entry] : dump) {
            if (entry.className == "Class" && entry.body.empty() && path.rfind("none", 0) == 0) {
                continue;
            }
            std::string body = renamed(entry.body, paletteRef, palettes);
            body = renamed(body, pageRef, pages);
            auto moved = shift.find(path.substr(0, path.find('.')));
            if (moved != shift.end() && (entry.className == "Function" || entry.className == "State")) {
                std::string adjusted;
                auto last = body.cbegin();
                for (std::sregex_iterator it(body.begin(), body.end(), position), end; it != end; ++it) {
                    adjusted.append(last, (*it)[0].first);
                    adjusted += "pos=" + std::to_string(std::stoul((*it)[1].str()) - moved->second);
                    last = (*it)[0].second;
                }
                adjusted.append(last, body.cend());
                body = adjusted;
            }
            ut::compiler::DumpedExport copy = entry;
            copy.body = body;
            auto key = keys.find(path);
            result[key != keys.end() ? key->second : path] = entry.className + "|" + lowered(entry.super) + "|" + std::to_string(entry.flags) + "|" + sortedTags(normalized(copy));
        }
        return result;
    }

    int run(const std::string& command)
    {
        return std::system(command.c_str());
    }
}

TEST_CASE("exported default properties use the UnrealEd format", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::UClass* bot = ut::objects::findClass("Botpack.Bot");
    REQUIRE(bot != nullptr);
    REQUIRE(*reinterpret_cast<float*>(bot->defaultObject->propertyAddress(ut::Name("TimeBetweenAttacks"))) == 0.6f);
    REQUIRE(*reinterpret_cast<ut::UObject**>(bot->defaultObject->propertyAddress(ut::Name("CarcassType"))) == ut::objects::findClass("Engine.Carcass"));
    REQUIRE(*bot->defaultObject->propertyAddress(ut::Name("Intelligence")) == 3);
    ut::UClass* parent = bot->superClass();
    std::string text = ut::compiler::exportProperties(bot, bot->defaultObject->data, parent, parent->defaultObject->data, 4);
    CHECK(text.find("     TimeBetweenAttacks=0.600000\r\n") != std::string::npos);
    CHECK(text.find("     CarcassType=Class'Engine.Carcass'\r\n") != std::string::npos);
    CHECK(text.find("     Intelligence=BRAINS_HUMAN\r\n") != std::string::npos);
}

TEST_CASE("ucc make rebuilds Core on its own", "[data]")
{
    const char* xdg = std::getenv("XDG_DATA_HOME");
    REQUIRE(xdg != nullptr);
    std::filesystem::path home = std::filesystem::path(xdg) / "ucc-core";
    std::filesystem::remove_all(home);
    std::filesystem::path user = home / "openut";
    std::filesystem::create_directories(user);
    {
        std::ifstream in(std::filesystem::path(OPENUT_GAME_DATA) / "System" / "UnrealTournament.ini", std::ios::binary);
        std::ofstream out(user / "UnrealTournament.ini", std::ios::binary);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("EditPackages=", 0) != 0 || line.rfind("EditPackages=Core", 0) == 0) {
                out << line << "\n";
            }
        }
    }
    std::string environment = "XDG_DATA_HOME=\"" + home.string() + "\" ";
    REQUIRE(run(environment + "\"" UCC_PATH "\" exportsources Core.u \"" + user.string() + "\" > /dev/null 2>&1") == 0);
    std::filesystem::path log = home / "make.log";
    REQUIRE(run(environment + "\"" UCC_PATH "\" make > \"" + log.string() + "\" 2>&1") == 0);
    std::ifstream in(log);
    std::stringstream output;
    output << in.rdbuf();
    CHECK(output.str().find("Building Core from ") != std::string::npos);
    CHECK(output.str().find("--------------------Engine") == std::string::npos);
    auto expected = dumpByPath(std::filesystem::path(OPENUT_GAME_DATA) / "System" / "Core.u");
    auto actual = dumpByPath(user / "System" / "Core.u");
    REQUIRE(actual.size() == expected.size());
    int mismatches = 0;
    for (auto& [path, entry] : expected) {
        auto it = actual.find(path);
        if (it == actual.end() || it->second.className != entry.className || it->second.flags != entry.flags || normalized(it->second) != normalized(entry)) {
            mismatches++;
        }
    }
    CHECK(mismatches == 0);
    CHECK(actual["object"].className == "Class");
    std::filesystem::remove_all(home);
}

TEST_CASE("ucc make reports script errors with file and line and writes no package", "[data]")
{
    struct BrokenScript {
        const char* source;
        const char* error;
    };

    const BrokenScript cases[] = {
        { "class Broken extends Actor;\r\n\r\nvar int Count\r\nvar int Other;\r\n", "Broken.uc(4) : Error, Missing ';' before 'var'" },
        { "class Broken extends Actor;\r\n\r\nfunction Touch(Actor Other)\r\n{\r\n\tUndefinedThing = 1;\r\n}\r\n", "Broken.uc(5) : Error, 'UndefinedThing': Bad command or expression" },
        { "class Broken extends Actor;\r\n\r\nfunction PostBeginPlay()\r\n{\r\n\tlocal int Value;\r\n\r\n\tValue = Self;\r\n}\r\n", "Broken.uc(7) : Error, Type mismatch in '='" },
    };
    const char* xdg = std::getenv("XDG_DATA_HOME");
    REQUIRE(xdg != nullptr);
    std::filesystem::path home = std::filesystem::path(xdg) / "ucc-errors";
    std::filesystem::path user = home / "openut";
    for (const BrokenScript& broken : cases) {
        INFO(broken.error);
        std::filesystem::remove_all(home);
        std::filesystem::create_directories(user / "BrokenMod" / "Classes");
        {
            std::ifstream in(std::filesystem::path(OPENUT_GAME_DATA) / "System" / "UnrealTournament.ini", std::ios::binary);
            std::stringstream ini;
            ini << in.rdbuf();
            std::string text = ini.str();
            size_t last = text.rfind("EditPackages=");
            REQUIRE(last != std::string::npos);
            text.insert(text.find('\n', last) + 1, "EditPackages=BrokenMod\r\n");
            std::ofstream out(user / "UnrealTournament.ini", std::ios::binary);
            out << text;
        }
        {
            std::ofstream out(user / "BrokenMod" / "Classes" / "Broken.uc", std::ios::binary);
            out << broken.source;
        }
        std::filesystem::path log = home / "make.log";
        CHECK(run("XDG_DATA_HOME=\"" + home.string() + "\" \"" UCC_PATH "\" make > \"" + log.string() + "\" 2>&1") != 0);
        std::ifstream in(log);
        std::stringstream output;
        output << in.rdbuf();
        CHECK(output.str().find(broken.error) != std::string::npos);
        CHECK_FALSE(std::filesystem::exists(user / "System" / "BrokenMod.u"));
    }
    std::filesystem::remove_all(home);
}

TEST_CASE("ucc make rebuilds every EditPackage from exported sources", "[data]")
{
    const char* xdg = std::getenv("XDG_DATA_HOME");
    REQUIRE(xdg != nullptr);
    std::filesystem::path home = std::filesystem::path(xdg) / "ucc-make";
    std::filesystem::remove_all(home);
    std::filesystem::path user = home / "openut";
    std::vector<std::string> packages = editPackages();
    REQUIRE(packages.size() == 16);
    CHECK(packages.front() == "Core");
    std::string environment = "XDG_DATA_HOME=\"" + home.string() + "\" ";
    for (const std::string& package : packages) {
        INFO(package);
        REQUIRE(run(environment + "\"" UCC_PATH "\" exportsources " + package + ".u \"" + user.string() + "\" > /dev/null 2>&1") == 0);
    }
    REQUIRE(run(environment + "\"" UCC_PATH "\" make > /dev/null 2>&1") == 0);
    std::set<std::string> mismatches;
    std::set<std::string> extras;
    for (const std::string& package : packages) {
        INFO(package);
        std::filesystem::path shipped = ut::compiler::resolveFile(std::filesystem::path(OPENUT_GAME_DATA) / "System", package + ".u");
        std::filesystem::path built = user / "System" / (package + ".u");
        REQUIRE(std::filesystem::exists(built));
        std::map<std::string, std::string> expected = comparable(dumpByPath(shipped));
        std::map<std::string, std::string> actual = comparable(dumpByPath(built));
        for (const auto& [path, entry] : expected) {
            auto it = actual.find(path);
            if (it == actual.end() || it->second != entry) {
                mismatches.insert(lowered(package) + "/" + path);
            }
        }
        for (const auto& [path, entry] : actual) {
            if (!expected.count(path)) {
                extras.insert(lowered(package) + "/" + path);
            }
        }
    }
    const std::set<std::string> knownMismatches = {
        "unrealshare/s_bubble1",
        "unrealshare/s_bubble2",
        "unrealshare/s_bubble3",
        "unrealshare/skaarjw",
        "unrealshare/unrealweaponmenu.drawmenu",
        "unrealshare/unrealweaponmenu.processleft",
        "unrealshare/unrealweaponmenu.processright",
        "botpack/boss [lodmesh]",
        "botpack/commando",
        "botpack/fcommando",
        "botpack/sgirl",
        "botpack/soldier",
        "botpack/trophyboss",
        "botpack/trophyfemale1",
        "botpack/trophyfemale2",
        "botpack/trophymale1",
        "botpack/trophymale2",
    };
    CHECK(mismatches == knownMismatches);
    CHECK(extras.empty());
    std::filesystem::remove_all(home);
}
