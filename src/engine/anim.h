#pragma once

#include "core/object.h"

#include <cstdint>
#include <vector>

namespace ut {
    struct AnimInfo {
        Name group;
        int32_t numFrames = 0;
        float rate = 0;
        std::vector<std::pair<float, Name>> notifys;
    };

    bool findAnim(UObject* mesh, Name sequence, AnimInfo& info);
}
