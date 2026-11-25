#include "engine/truetype_fonts.h"

#include "core/objects.h"
#include "engine/package.h"
#include "engine/resources.h"

#include <stb_truetype.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

extern "C" {
extern const unsigned char liberation_sans_regular[];
extern const unsigned char liberation_sans_bold[];
}

namespace ut::truetype {
    namespace {
        constexpr float MinimumCapPixels = 10;
        constexpr int FirstCharacter = 32;
        constexpr int CharacterCount = 256;
        constexpr int Padding = 1;
        constexpr uint8_t FormatRGBA8 = 5;

        constexpr uint16_t windows1252[32] = {
            0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
            0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178
        };

        struct Face {
            stbtt_fontinfo info {};
            float capUnits = 0;
            bool valid = false;
        };

        struct Metrics {
            bool valid = false;
            bool bold = false;
            int capHeight = 0;
            int baseline = 0;
            float fittedCap[2] = {};
        };

        struct Glyph {
            bool present = false;
            int u = 0;
            int v = 0;
            int width = 0;
            int height = 0;
            int left = 0;
            int top = 0;
            float advance = 0;
        };

        struct Atlas {
            UTexture* texture = nullptr;
            Glyph glyphs[CharacterCount];
        };

        struct State {
            bool enabled = false;
            bool loaded = false;
            Face faces[2];
            std::map<UFont*, Metrics> metrics;
            std::map<std::pair<int, long>, std::unique_ptr<Atlas>> atlases;
        };

        State& state()
        {
            static State instance;
            return instance;
        }

        int codepoint(int character)
        {
            if (character >= 128 && character < 160) {
                return windows1252[character - 128];
            }
            return character;
        }

        void load(Face& face, const unsigned char* data)
        {
            if (!stbtt_InitFont(&face.info, data, stbtt_GetFontOffsetForIndex(data, 0))) {
                return;
            }
            int x0 = 0;
            int y0 = 0;
            int x1 = 0;
            int y1 = 0;
            if (!stbtt_GetCodepointBox(&face.info, 'H', &x0, &y0, &x1, &y1) || y1 <= 0) {
                return;
            }
            face.capUnits = static_cast<float>(y1);
            face.valid = true;
        }

        Metrics measure(UFont* font)
        {
            Metrics result;
            UObject* package = font->outermost();
            if (!package || (!iequals(package->name.str(), "UWindowFonts") && !iequals(package->name.str(), "LadderFonts"))) {
                return result;
            }
            UTexture* page = nullptr;
            const FontCharacter* glyph = font->glyph(static_cast<uint8_t>('H'), &page);
            if (!glyph || !page || glyph->width <= 0 || glyph->height <= 0 || !page->decodable()) {
                return result;
            }
            std::vector<uint8_t> rgba = page->rgba(0);
            int pageWidth = static_cast<int>(page->width());
            int pageHeight = static_cast<int>(page->height());
            int top = -1;
            int bottom = -1;
            for (int row = 0; row < glyph->height && glyph->v + row < pageHeight; row++) {
                for (int column = 0; column < glyph->width && glyph->u + column < pageWidth; column++) {
                    const uint8_t* pixel = &rgba[(static_cast<size_t>(glyph->v + row) * static_cast<size_t>(pageWidth) + static_cast<size_t>(glyph->u + column)) * 4];
                    if (pixel[3] > 0 && std::max({ pixel[0], pixel[1], pixel[2] }) > 127) {
                        top = top < 0 ? row : top;
                        bottom = row;
                    }
                }
            }
            if (top < 0) {
                return result;
            }
            std::string name = font->name.str();
            result.capHeight = bottom - top + 1;
            result.baseline = bottom + 1;
            result.bold = name.size() > 6 && iequals(name.substr(0, 7), "TahomaB");
            result.valid = true;
            return result;
        }

        Metrics& metricsOf(UFont* font)
        {
            auto it = state().metrics.find(font);
            if (it == state().metrics.end()) {
                it = state().metrics.emplace(font, measure(font)).first;
            }
            return it->second;
        }

        float fittedCap(UFont* font, Metrics& metrics, int faceIndex)
        {
            float& cap = metrics.fittedCap[faceIndex];
            if (cap > 0) {
                return cap;
            }
            const Face& face = state().faces[faceIndex];
            float cells = 0;
            float advances = 0;
            for (char c : std::string_view("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")) {
                const FontCharacter* cell = font->glyph(static_cast<uint8_t>(c), nullptr);
                int index = stbtt_FindGlyphIndex(&face.info, c);
                if (!cell || index == 0) {
                    continue;
                }
                int advance = 0;
                int bearing = 0;
                stbtt_GetGlyphHMetrics(&face.info, index, &advance, &bearing);
                cells += static_cast<float>(cell->width);
                advances += static_cast<float>(advance) * static_cast<float>(metrics.capHeight) / face.capUnits;
            }
            float ratio = advances > 0 ? cells / advances : 1;
            cap = static_cast<float>(metrics.capHeight) * std::clamp(ratio, 0.88f, 1.12f);
            return cap;
        }

        UTexture* createTexture(int width, int height, std::vector<uint8_t> pixels)
        {
            std::string name = "TrueTypeAtlas" + std::to_string(state().atlases.size());
            UTexture* texture = cast<UTexture>(objects::newObject(objects::findClass("Engine.Texture"), objects::packageObject(Name("Transient")), Name(name), RF_Transient));
            if (!texture) {
                return nullptr;
            }
            texture->loaded = true;
            *texture->propertyAddress(Name("Format")) = FormatRGBA8;
            TextureMip mip;
            mip.width = static_cast<uint32_t>(width);
            mip.height = static_cast<uint32_t>(height);
            mip.ubits = static_cast<uint8_t>(std::countr_zero(mip.width));
            mip.vbits = static_cast<uint8_t>(std::countr_zero(mip.height));
            mip.data = std::move(pixels);
            texture->mips.push_back(std::move(mip));
            return texture;
        }

        const Atlas& atlasFor(int faceIndex, float capPixels)
        {
            std::unique_ptr<Atlas>& slot = state().atlases[{ faceIndex, std::lround(capPixels * 64.0f) }];
            if (slot) {
                return *slot;
            }
            slot = std::make_unique<Atlas>();
            Atlas& atlas = *slot;
            const stbtt_fontinfo& info = state().faces[faceIndex].info;
            float scale = capPixels / state().faces[faceIndex].capUnits;
            int atlasWidth = 256;
            while (static_cast<float>(atlasWidth) < capPixels * 24.0f) {
                atlasWidth *= 2;
            }
            std::vector<std::vector<uint8_t>> coverage(CharacterCount);
            int x = Padding;
            int y = Padding;
            int rowHeight = 0;
            for (int character = FirstCharacter; character < CharacterCount; character++) {
                int point = codepoint(character);
                int index = point ? stbtt_FindGlyphIndex(&info, point) : 0;
                if (index == 0) {
                    continue;
                }
                Glyph& glyph = atlas.glyphs[character];
                int advance = 0;
                int bearing = 0;
                stbtt_GetGlyphHMetrics(&info, index, &advance, &bearing);
                int x0 = 0;
                int y0 = 0;
                int x1 = 0;
                int y1 = 0;
                stbtt_GetGlyphBitmapBox(&info, index, scale, scale, &x0, &y0, &x1, &y1);
                glyph.present = true;
                glyph.advance = static_cast<float>(advance) * scale;
                if (x1 <= x0 || y1 <= y0) {
                    continue;
                }
                glyph.left = x0;
                glyph.top = y0;
                glyph.width = x1 - x0;
                glyph.height = y1 - y0;
                if (x + glyph.width + Padding > atlasWidth) {
                    x = Padding;
                    y += rowHeight + Padding;
                    rowHeight = 0;
                }
                glyph.u = x;
                glyph.v = y;
                x += glyph.width + Padding;
                rowHeight = std::max(rowHeight, glyph.height);
                coverage[static_cast<size_t>(character)].resize(static_cast<size_t>(glyph.width * glyph.height));
                stbtt_MakeGlyphBitmap(&info, coverage[static_cast<size_t>(character)].data(), glyph.width, glyph.height, glyph.width, scale, scale, index);
            }
            int atlasHeight = 1;
            while (atlasHeight < y + rowHeight + Padding) {
                atlasHeight *= 2;
            }
            std::vector<uint8_t> pixels(static_cast<size_t>(atlasWidth) * static_cast<size_t>(atlasHeight) * 4, 0);
            for (int character = FirstCharacter; character < CharacterCount; character++) {
                const Glyph& glyph = atlas.glyphs[character];
                const std::vector<uint8_t>& source = coverage[static_cast<size_t>(character)];
                if (source.empty()) {
                    continue;
                }
                for (int row = 0; row < glyph.height; row++) {
                    for (int column = 0; column < glyph.width; column++) {
                        uint8_t value = source[static_cast<size_t>(row * glyph.width + column)];
                        size_t offset = (static_cast<size_t>(glyph.v + row) * static_cast<size_t>(atlasWidth) + static_cast<size_t>(glyph.u + column)) * 4;
                        std::fill_n(pixels.begin() + static_cast<std::ptrdiff_t>(offset), 4, value);
                    }
                }
            }
            atlas.texture = createTexture(atlasWidth, atlasHeight, std::move(pixels));
            return atlas;
        }
    }

    void setEnabled(bool enabled)
    {
        state().enabled = enabled;
    }

    bool draw(Canvas2D& canvas, UFont* font, std::string_view text, float x, float y, LinearColor color, BlendStyle style, float spacing)
    {
        State& current = state();
        if (!current.enabled || !font || (style != BlendStyle::Normal && style != BlendStyle::Masked && style != BlendStyle::Translucent)) {
            return false;
        }
        if (!current.loaded) {
            load(current.faces[0], liberation_sans_regular);
            load(current.faces[1], liberation_sans_bold);
            current.loaded = true;
        }
        Metrics& metrics = metricsOf(font);
        if (!metrics.valid) {
            return false;
        }
        int faceIndex = metrics.bold && current.faces[1].valid ? 1 : 0;
        if (!current.faces[faceIndex].valid) {
            return false;
        }
        float pixelScale = canvas.pixelScale();
        float capPixels = fittedCap(font, metrics, faceIndex) * pixelScale;
        if (capPixels < MinimumCapPixels) {
            return false;
        }
        const Atlas& atlas = atlasFor(faceIndex, capPixels);
        if (!atlas.texture) {
            return false;
        }
        BlendStyle drawStyle = style == BlendStyle::Translucent ? BlendStyle::Translucent : BlendStyle::Premultiplied;
        LinearColor tint = color;
        if (drawStyle == BlendStyle::Premultiplied) {
            tint = { color.r * color.a, color.g * color.a, color.b * color.a, color.a };
        }
        float baseline = std::round((y + static_cast<float>(metrics.baseline)) * pixelScale);
        auto slotOf = [&](uint8_t character, const FontCharacter& cell) {
            const Glyph& glyph = atlas.glyphs[character];
            return (glyph.present ? glyph.advance : static_cast<float>(cell.width) * pixelScale) + spacing * pixelScale;
        };
        float cursor = x;
        size_t start = 0;
        while (start < text.size()) {
            bool blank = text[start] == ' ';
            size_t end = start + 1;
            while (end < text.size() && (text[end] == ' ') == blank) {
                end++;
            }
            float cells = 0;
            float natural = 0;
            for (size_t i = start; i < end; i++) {
                uint8_t character = static_cast<uint8_t>(text[i]);
                const FontCharacter* cell = font->glyph(character, nullptr);
                if (cell) {
                    cells += static_cast<float>(cell->width) + spacing;
                    natural += slotOf(character, *cell);
                }
            }
            float fit = natural > 0 ? std::clamp(cells * pixelScale / natural, 0.9f, 1.1f) : 1;
            float pen = cursor * pixelScale + (cells * pixelScale - natural * fit) / 2;
            for (size_t i = start; i < end; i++) {
                uint8_t character = static_cast<uint8_t>(text[i]);
                const FontCharacter* cell = font->glyph(character, nullptr);
                if (!cell) {
                    continue;
                }
                const Glyph& glyph = atlas.glyphs[character];
                float slot = slotOf(character, *cell);
                if (glyph.present && glyph.width > 0) {
                    float left = std::round(pen + (slot * fit - slot) / 2) + static_cast<float>(glyph.left);
                    float top = baseline + static_cast<float>(glyph.top);
                    canvas.drawTile(atlas.texture, left / pixelScale, top / pixelScale, static_cast<float>(glyph.width) / pixelScale, static_cast<float>(glyph.height) / pixelScale, static_cast<float>(glyph.u), static_cast<float>(glyph.v), static_cast<float>(glyph.width), static_cast<float>(glyph.height), tint, drawStyle, false);
                }
                pen += slot * fit;
            }
            cursor += cells;
            start = end;
        }
        return true;
    }
}
