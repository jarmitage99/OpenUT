#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
    std::vector<uint8_t> readFile(const std::filesystem::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
}

TEST_CASE("batchexport sound wav writes the stored wave file", "[data]")
{
    const char* xdg = std::getenv("XDG_DATA_HOME");
    REQUIRE(xdg != nullptr);
    std::filesystem::path home = std::filesystem::path(xdg) / "ucc-audio-export";
    std::filesystem::remove_all(home);
    std::filesystem::path directory = home / "wav";
    std::string command = "XDG_DATA_HOME=\"" + home.string() + "\" \"" UCC_PATH "\" batchexport AmbCity.uax sound wav \"" + directory.string() + "\" > /dev/null 2>&1";
    REQUIRE(std::system(command.c_str()) == 0);
    std::vector<uint8_t> package = readFile(std::filesystem::path(OPENUT_GAME_DATA) / "Sounds" / "AmbCity.uax");
    REQUIRE_FALSE(package.empty());
    size_t files = 0;
    for (const auto& item : std::filesystem::directory_iterator(directory)) {
        if (item.path().extension() != ".wav") {
            continue;
        }
        files++;
        std::vector<uint8_t> wave = readFile(item.path());
        INFO(item.path().filename().string());
        REQUIRE(wave.size() > 44);
        CHECK(std::memcmp(wave.data(), "RIFF", 4) == 0);
        CHECK(std::memcmp(wave.data() + 8, "WAVEfmt ", 8) == 0);
        CHECK(std::search(package.begin(), package.end(), wave.begin(), wave.end()) != package.end());
    }
    CHECK(files == 8);
    std::filesystem::remove_all(home);
}
