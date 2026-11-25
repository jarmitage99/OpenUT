#pragma once

#include <cstdint>
#include <string>

namespace ut {
    std::string videoSection();
    int32_t videoSetting(const char* key, int32_t fallback);
    int32_t uiScale(int32_t screenHeight);
}
