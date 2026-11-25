#include "engine/icon.h"

#include <SDL.h>

#include <algorithm>
#include <fstream>
#include <iterator>

namespace ut::icon {
    namespace {
        uint16_t u16(const uint8_t* p)
        {
            return static_cast<uint16_t>(p[0] | (p[1] << 8));
        }

        uint32_t u32(const uint8_t* p)
        {
            return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
        }

        Image decodeEntry(const uint8_t* p, size_t size)
        {
            Image image;
            if (size < 40) {
                return image;
            }
            uint32_t headerSize = u32(p);
            int32_t width = static_cast<int32_t>(u32(p + 4));
            int32_t height = static_cast<int32_t>(u32(p + 8)) / 2;
            uint16_t bits = u16(p + 14);
            uint32_t colors = u32(p + 32);
            if (width <= 0 || height <= 0 || width > 256 || height > 256 || (bits != 4 && bits != 8 && bits != 24 && bits != 32)) {
                return image;
            }
            if (colors == 0 && bits <= 8) {
                colors = 1u << bits;
            }
            size_t palette = headerSize;
            size_t pixels = palette + static_cast<size_t>(bits <= 8 ? colors : 0) * 4;
            size_t stride = ((static_cast<size_t>(width) * bits + 31) / 32) * 4;
            size_t maskStride = ((static_cast<size_t>(width) + 31) / 32) * 4;
            size_t mask = pixels + stride * static_cast<size_t>(height);
            size_t needed = bits == 32 ? mask : mask + maskStride * static_cast<size_t>(height);
            if (needed > size) {
                return image;
            }
            image.width = static_cast<uint32_t>(width);
            image.height = static_cast<uint32_t>(height);
            image.rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
            for (int32_t y = 0; y < height; y++) {
                const uint8_t* row = p + pixels + static_cast<size_t>(height - 1 - y) * stride;
                const uint8_t* maskRow = p + mask + static_cast<size_t>(height - 1 - y) * maskStride;
                for (int32_t x = 0; x < width; x++) {
                    uint8_t* out = image.rgba.data() + (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
                    const uint8_t* color = nullptr;
                    uint8_t alpha = 255;
                    if (bits == 32) {
                        color = row + x * 4;
                        alpha = color[3];
                    } else if (bits == 24) {
                        color = row + x * 3;
                    } else {
                        size_t bit = static_cast<size_t>(x) * bits;
                        uint32_t index = (static_cast<uint32_t>(row[bit / 8]) >> (8 - bits - bit % 8)) & ((1u << bits) - 1);
                        color = p + palette + std::min(index, colors - 1) * 4;
                    }
                    if (bits != 32 && ((maskRow[x / 8] >> (7 - x % 8)) & 1)) {
                        alpha = 0;
                    }
                    out[0] = color[2];
                    out[1] = color[1];
                    out[2] = color[0];
                    out[3] = alpha;
                }
            }
            return image;
        }
    }

    Image decode(const std::vector<uint8_t>& bytes)
    {
        Image best;
        uint32_t bestScore = 0;
        if (bytes.size() < 6 || u16(bytes.data() + 2) != 1) {
            return best;
        }
        uint16_t count = u16(bytes.data() + 4);
        for (size_t i = 0; i < count && 6 + i * 16 + 16 <= bytes.size(); i++) {
            const uint8_t* entry = bytes.data() + 6 + i * 16;
            uint32_t size = u32(entry + 8);
            uint32_t offset = u32(entry + 12);
            if (offset >= bytes.size() || size > bytes.size() - offset) {
                continue;
            }
            Image image = decodeEntry(bytes.data() + offset, size);
            uint32_t score = image.width * image.height * 64 + u16(bytes.data() + offset + 14);
            if (!image.rgba.empty() && score > bestScore) {
                bestScore = score;
                best = std::move(image);
            }
        }
        return best;
    }

    Image load(const std::filesystem::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return decode(bytes);
    }

    void applyToWindow(SDL_Window* window, const std::filesystem::path& file)
    {
        Image image = load(file);
        if (image.rgba.empty()) {
            return;
        }
        int width = static_cast<int>(image.width);
        int height = static_cast<int>(image.height);
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(image.rgba.data(), width, height, 32, width * 4, SDL_PIXELFORMAT_ABGR8888);
        if (surface) {
            SDL_SetWindowIcon(window, surface);
            SDL_FreeSurface(surface);
        }
    }
}
