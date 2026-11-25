#pragma once

#include "core/math.h"

#include <cstdint>
#include <string_view>

namespace ut {
    class UTexture;
    class UFont;

    enum class BlendStyle : uint8_t {
        None = 0,
        Normal = 1,
        Masked = 2,
        Translucent = 3,
        Modulated = 4,
        Premultiplied = 5,
    };

    struct LinearColor {
        float r = 1;
        float g = 1;
        float b = 1;
        float a = 1;
    };

    class Canvas2D {
    public:
        virtual ~Canvas2D() = default;
        virtual float width() const = 0;
        virtual float height() const = 0;
        virtual void setClip(float x, float y, float w, float h) = 0;
        virtual void clearClip() = 0;
        virtual void drawTile(UTexture* texture, float x, float y, float w, float h, float u, float v, float ul, float vl, LinearColor color, BlendStyle style, bool smooth) = 0;
        virtual void drawRect(float x, float y, float w, float h, LinearColor color, BlendStyle style) = 0;
        virtual void drawLine(float x1, float y1, float x2, float y2, LinearColor color) = 0;

        virtual float pixelScale() const
        {
            return 1;
        }
    };

    class ScaledCanvas : public Canvas2D {
    public:
        ScaledCanvas(Canvas2D& destination, float factor)
            : target(destination)
            , scale(factor)
        {
        }

        float width() const override
        {
            return target.width() / scale;
        }

        float height() const override
        {
            return target.height() / scale;
        }

        void setClip(float x, float y, float w, float h) override
        {
            target.setClip(x * scale, y * scale, w * scale, h * scale);
        }

        void clearClip() override
        {
            target.clearClip();
        }

        void drawTile(UTexture* texture, float x, float y, float w, float h, float u, float v, float ul, float vl, LinearColor color, BlendStyle style, bool smooth) override
        {
            target.drawTile(texture, x * scale, y * scale, w * scale, h * scale, u, v, ul, vl, color, style, smooth);
        }

        void drawRect(float x, float y, float w, float h, LinearColor color, BlendStyle style) override
        {
            target.drawRect(x * scale, y * scale, w * scale, h * scale, color, style);
        }

        void drawLine(float x1, float y1, float x2, float y2, LinearColor color) override
        {
            target.drawLine(x1 * scale, y1 * scale, x2 * scale, y2 * scale, color);
        }

        float pixelScale() const override
        {
            return scale * target.pixelScale();
        }

    private:
        Canvas2D& target;
        float scale;
    };

    float textWidth(UFont* font, std::string_view text, float spacing);
    float textHeight(UFont* font);
    void drawText(Canvas2D& canvas, UFont* font, std::string_view text, float x, float y, LinearColor color, BlendStyle style, float spacing);
}
