#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace ut {
    enum class DisplayMode : int32_t {
        Windowed = 0,
        Fullscreen = 1,
        Borderless = 2,
    };

    std::string videoSection();
    int32_t videoSetting(const char* key, int32_t fallback);
    int32_t uiScale(int32_t screenHeight);
    DisplayMode displayModeSetting();

    // Set by the game client; switches the window to the given display mode.
    extern std::function<void(DisplayMode)> setDisplayModeHook;
}
