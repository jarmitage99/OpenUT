#pragma once

#include "core/math.h"
#include "core/object.h"
#include "engine/canvas.h"

#include <functional>
#include <string>
#include <vector>

namespace ut {
    struct CanvasHooks {
        Canvas2D* target = nullptr;
        std::function<void(UObject* actor, bool clearZ)> drawActor;
        std::function<void(UObject* actor, float x, float y, float width, float height)> drawClippedActor;
        std::function<void(int x, int y, int width, int height, UObject* camera, const Vector& location, const Rotator& rotation, float fov, bool clearZ)> drawPortal;
        std::function<void(const std::string&)> clipboardCopy;
        std::function<std::string()> clipboardPaste;
    };

    CanvasHooks& canvasHooks();
    LinearColor canvasColor(UObject* canvas);
}
