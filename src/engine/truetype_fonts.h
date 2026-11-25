#pragma once

#include "engine/canvas.h"

#include <string_view>

namespace ut::truetype {
    void setEnabled(bool enabled);
    bool draw(Canvas2D& canvas, UFont* font, std::string_view text, float x, float y, LinearColor color, BlendStyle style, float spacing);
}
