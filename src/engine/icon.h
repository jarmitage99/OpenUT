#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

struct SDL_Window;

namespace ut::icon {
    struct Image {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> rgba;
    };

    Image decode(const std::vector<uint8_t>& bytes);
    Image load(const std::filesystem::path& file);
    void applyToWindow(SDL_Window* window, const std::filesystem::path& file);
}
