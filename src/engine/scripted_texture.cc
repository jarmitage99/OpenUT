#include "engine/scripted_texture.h"

#include "core/objects.h"
#include "core/vm.h"
#include "engine/props_engine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

namespace ut {
    namespace {
        const Name nameScriptedTexture("ScriptedTexture");

        TextureMip* targetMip(UObject* self)
        {
            UTexture* texture = cast<UTexture>(self);
            if (!texture || texture->mips.empty() || texture->format() != 0) {
                return nullptr;
            }
            TextureMip& mip = texture->mips[0];
            if (mip.data.size() < static_cast<size_t>(mip.width) * mip.height) {
                mip.data.resize(static_cast<size_t>(mip.width) * mip.height);
            }
            return &mip;
        }

        uint8_t bestMatch(UPalette* palette, uint8_t r, uint8_t g, uint8_t b)
        {
            if (!palette || palette->colors.size() < 256) {
                return 0;
            }
            uint8_t best = 1;
            int32_t bestDistance = std::numeric_limits<int32_t>::max();
            for (int32_t i = 1; i < 256; i++) {
                uint32_t color = palette->colors[static_cast<size_t>(i)];
                int32_t dr = static_cast<int32_t>(color & 0xff) - r;
                int32_t dg = static_cast<int32_t>((color >> 8) & 0xff) - g;
                int32_t db = static_cast<int32_t>((color >> 16) & 0xff) - b;
                int32_t distance = dr * dr + dg * dg + db * db;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = static_cast<uint8_t>(i);
                }
            }
            return best;
        }

        std::array<uint8_t, 256> remapTable(UTexture* source, UTexture* target)
        {
            std::array<uint8_t, 256> table {};
            UPalette* from = source->palette();
            UPalette* to = target->palette();
            for (size_t i = 0; i < table.size(); i++) {
                if (from == to || !from || from->colors.size() < 256) {
                    table[i] = static_cast<uint8_t>(i);
                    continue;
                }
                uint32_t color = from->colors[i];
                table[i] = i == 0 ? 0 : bestMatch(to, static_cast<uint8_t>(color & 0xff), static_cast<uint8_t>((color >> 8) & 0xff), static_cast<uint8_t>((color >> 16) & 0xff));
            }
            return table;
        }

        const TextureMip* sourceMip(UObject* object)
        {
            objects::ensureLoaded(object);
            UTexture* texture = cast<UTexture>(object);
            if (!texture || texture->mips.empty() || texture->format() != 0) {
                return nullptr;
            }
            const TextureMip& mip = texture->mips[0];
            if (mip.width == 0 || mip.height == 0 || mip.data.size() < static_cast<size_t>(mip.width) * mip.height) {
                return nullptr;
            }
            return &mip;
        }

        void drawText(UObject* self, float x, float y, const std::string& text, UObject* fontObject, const uint8_t* colorIndex)
        {
            TextureMip* target = targetMip(self);
            objects::ensureLoaded(fontObject);
            UFont* font = cast<UFont>(fontObject);
            if (!target || !font) {
                return;
            }
            int32_t penX = static_cast<int32_t>(std::floor(x));
            int32_t penY = static_cast<int32_t>(std::floor(y));
            for (char c : text) {
                UTexture* page = nullptr;
                const FontCharacter* glyph = font->glyph(static_cast<uint8_t>(c), &page);
                if (!glyph) {
                    continue;
                }
                const TextureMip* source = sourceMip(page);
                if (source) {
                    std::array<uint8_t, 256> table = remapTable(page, cast<UTexture>(self));
                    for (int32_t row = 0; row < glyph->height; row++) {
                        for (int32_t column = 0; column < glyph->width; column++) {
                            int32_t destX = penX + column;
                            int32_t destY = penY + row;
                            int32_t srcX = glyph->u + column;
                            int32_t srcY = glyph->v + row;
                            if (destX < 0 || destY < 0 || destX >= static_cast<int32_t>(target->width) || destY >= static_cast<int32_t>(target->height)) {
                                continue;
                            }
                            if (srcX < 0 || srcY < 0 || srcX >= static_cast<int32_t>(source->width) || srcY >= static_cast<int32_t>(source->height)) {
                                continue;
                            }
                            uint8_t index = source->data[static_cast<size_t>(srcY) * source->width + static_cast<size_t>(srcX)];
                            if (index == 0) {
                                continue;
                            }
                            target->data[static_cast<size_t>(destY) * target->width + static_cast<size_t>(destX)] = colorIndex ? *colorIndex : table[index];
                        }
                    }
                }
                penX += glyph->width;
            }
        }

        void nativeDrawText(UObject* self, Frame& stack, void*)
        {
            float x = vm::get<float>(stack);
            float y = vm::get<float>(stack);
            std::string text = vm::get<std::string>(stack);
            UObject* font = vm::get<UObject*>(stack);
            vm::finish(stack);
            drawText(self, x, y, text, font, nullptr);
        }

        void nativeDrawColoredText(UObject* self, Frame& stack, void*)
        {
            float x = vm::get<float>(stack);
            float y = vm::get<float>(stack);
            std::string text = vm::get<std::string>(stack);
            UObject* font = vm::get<UObject*>(stack);
            Color color = vm::get<Color>(stack);
            vm::finish(stack);
            UTexture* texture = cast<UTexture>(self);
            uint8_t index = bestMatch(texture ? texture->palette() : nullptr, color.r, color.g, color.b);
            drawText(self, x, y, text, font, &index);
        }

        void nativeDrawTile(UObject* self, Frame& stack, void*)
        {
            float x = vm::get<float>(stack);
            float y = vm::get<float>(stack);
            float xl = vm::get<float>(stack);
            float yl = vm::get<float>(stack);
            float u = vm::get<float>(stack);
            float v = vm::get<float>(stack);
            float ul = vm::get<float>(stack);
            float vl = vm::get<float>(stack);
            UObject* tex = vm::get<UObject*>(stack);
            bool masked = vm::getBool(stack);
            vm::finish(stack);
            TextureMip* target = targetMip(self);
            const TextureMip* source = sourceMip(tex);
            if (!target || !source || xl <= 0 || yl <= 0) {
                return;
            }
            std::array<uint8_t, 256> table = remapTable(cast<UTexture>(tex), cast<UTexture>(self));
            int32_t left = std::max(0, static_cast<int32_t>(std::floor(x)));
            int32_t top = std::max(0, static_cast<int32_t>(std::floor(y)));
            int32_t right = std::min(static_cast<int32_t>(target->width), static_cast<int32_t>(std::floor(x + xl)));
            int32_t bottom = std::min(static_cast<int32_t>(target->height), static_cast<int32_t>(std::floor(y + yl)));
            for (int32_t destY = top; destY < bottom; destY++) {
                float sampleV = v + (static_cast<float>(destY) - y) * vl / yl;
                int32_t srcY = static_cast<int32_t>(std::floor(sampleV)) & static_cast<int32_t>(source->height - 1);
                for (int32_t destX = left; destX < right; destX++) {
                    float sampleU = u + (static_cast<float>(destX) - x) * ul / xl;
                    int32_t srcX = static_cast<int32_t>(std::floor(sampleU)) & static_cast<int32_t>(source->width - 1);
                    uint8_t index = source->data[static_cast<size_t>(srcY) * source->width + static_cast<size_t>(srcX)];
                    if (masked && index == 0) {
                        continue;
                    }
                    target->data[static_cast<size_t>(destY) * target->width + static_cast<size_t>(destX)] = table[index];
                }
            }
        }

        void nativeReplaceTexture(UObject* self, Frame& stack, void*)
        {
            UObject* tex = vm::get<UObject*>(stack);
            vm::finish(stack);
            TextureMip* target = targetMip(self);
            const TextureMip* source = sourceMip(tex);
            if (!target || !source || source->width != target->width || source->height != target->height) {
                return;
            }
            std::array<uint8_t, 256> table = remapTable(cast<UTexture>(tex), cast<UTexture>(self));
            for (size_t i = 0; i < target->data.size() && i < source->data.size(); i++) {
                target->data[i] = table[source->data[i]];
            }
        }

        void nativeTextSize(UObject*, Frame& stack, void*)
        {
            std::string text = vm::get<std::string>(stack);
            float* xl = vm::getRef<float>(stack);
            float* yl = vm::getRef<float>(stack);
            UObject* fontObject = vm::get<UObject*>(stack);
            vm::finish(stack);
            objects::ensureLoaded(fontObject);
            UFont* font = cast<UFont>(fontObject);
            int32_t width = 0;
            int32_t height = 0;
            for (char c : text) {
                const FontCharacter* glyph = font ? font->glyph(static_cast<uint8_t>(c), nullptr) : nullptr;
                if (glyph) {
                    width += glyph->width;
                    height = std::max(height, glyph->height);
                }
            }
            *xl = static_cast<float>(width);
            *yl = static_cast<float>(height);
        }
    }

    namespace scriptedtexture {
        bool isScripted(UTexture* texture)
        {
            return texture && texture->cls && texture->isA(nameScriptedTexture);
        }

        bool refresh(UTexture* texture)
        {
            TextureMip* target = targetMip(texture);
            UObject* notify = scriptedtexture::NotifyActor(texture);
            if (!notify || notify->destroyed || !target) {
                return false;
            }
            UObject* level = actor::Level(notify);
            float now = level ? levelinfo::TimeSeconds(level) : 0.0f;
            float& localTime = scriptedtexture::LocalTime(texture);
            if (localTime == now) {
                return false;
            }
            localTime = now;
            const TextureMip* source = sourceMip(scriptedtexture::SourceTexture(texture));
            if (source && source->width == target->width && source->height == target->height) {
                std::array<uint8_t, 256> table = remapTable(cast<UTexture>(scriptedtexture::SourceTexture(texture)), texture);
                for (size_t i = 0; i < target->data.size(); i++) {
                    target->data[i] = table[source->data[i]];
                }
            }
            vm::Call(notify, Name("RenderTexture")).obj(texture).run();
            return true;
        }
    }

    void registerScriptedTextureNatives()
    {
        vm::registerNative(472, nativeDrawText);
        vm::registerNative(473, nativeDrawTile);
        vm::registerNative(474, nativeDrawColoredText);
        vm::registerNative(475, nativeReplaceTexture);
        vm::registerNative(476, nativeTextSize);
    }
}
