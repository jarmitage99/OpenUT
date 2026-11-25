#include "core/objects.h"
#include "engine/canvas.h"
#include "engine/resources.h"
#include "engine/truetype_fonts.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {
    struct DrawnTile {
        ut::UTexture* texture = nullptr;
        float x = 0;
        float y = 0;
        float w = 0;
        float h = 0;
        float u = 0;
        float v = 0;
        ut::BlendStyle style = ut::BlendStyle::None;
    };

    class TileCanvas : public ut::Canvas2D {
    public:
        float width() const override
        {
            return 4000;
        }

        float height() const override
        {
            return 4000;
        }

        void setClip(float, float, float, float) override
        {
        }

        void clearClip() override
        {
        }

        void drawTile(ut::UTexture* texture, float x, float y, float w, float h, float u, float v, float, float, ut::LinearColor, ut::BlendStyle style, bool) override
        {
            tiles.push_back({ texture, x, y, w, h, u, v, style });
        }

        void drawRect(float, float, float, float, ut::LinearColor, ut::BlendStyle) override
        {
        }

        void drawLine(float, float, float, float, ut::LinearColor) override
        {
        }

        std::vector<DrawnTile> tiles;
    };

    ut::UFont* font(const char* name)
    {
        ut::engine::initialize(OPENUT_GAME_DATA);
        ut::UFont* result = ut::cast<ut::UFont>(ut::objects::loadObject(name, nullptr));
        REQUIRE(result != nullptr);
        return result;
    }

    std::vector<DrawnTile> draw(ut::UFont* textFont, const std::string& text, float scale, float x, float y)
    {
        TileCanvas device;
        ut::ScaledCanvas canvas(device, scale);
        ut::drawText(canvas, textFont, text, x, y, { 0, 0, 0, 1 }, ut::BlendStyle::Normal, 0);
        for (DrawnTile& tile : device.tiles) {
            tile.x /= scale;
            tile.y /= scale;
            tile.w /= scale;
            tile.h /= scale;
        }
        return device.tiles;
    }

    std::vector<int> coverage(const DrawnTile& tile, float scale)
    {
        std::vector<uint8_t> rgba = tile.texture->rgba(0);
        int textureWidth = static_cast<int>(tile.texture->width());
        int width = static_cast<int>(tile.w * scale + 0.5f);
        int height = static_cast<int>(tile.h * scale + 0.5f);
        std::vector<int> values;
        for (int row = 0; row < height; row++) {
            for (int column = 0; column < width; column++) {
                values.push_back(rgba[(static_cast<size_t>(static_cast<int>(tile.v) + row) * static_cast<size_t>(textureWidth) + static_cast<size_t>(static_cast<int>(tile.u) + column)) * 4 + 3]);
            }
        }
        return values;
    }

    long totalCoverage(const std::vector<DrawnTile>& tiles, float scale)
    {
        long total = 0;
        for (const DrawnTile& tile : tiles) {
            for (int value : coverage(tile, scale)) {
                total += value;
            }
        }
        return total;
    }

    struct Enabled {
        Enabled()
        {
            ut::truetype::setEnabled(true);
        }

        ~Enabled()
        {
            ut::truetype::setEnabled(false);
        }
    };
}

TEST_CASE("with high resolution fonts on, menu text at 2x draws antialiased glyphs that stay inside the original text box", "[data]")
{
    ut::UFont* tahoma = font("UWindowFonts.Tahoma10");
    std::vector<DrawnTile> bitmap = draw(tahoma, "Mutators", 2, 10, 20);
    REQUIRE(bitmap.size() == 8);
    CHECK(bitmap[0].style == ut::BlendStyle::Masked);

    Enabled enabled;
    std::vector<DrawnTile> tiles = draw(tahoma, "Mutators", 2, 10, 20);
    REQUIRE(tiles.size() == 8);
    float right = 10 + ut::textWidth(tahoma, "Mutators", 0);
    float bottom = 20 + ut::textHeight(tahoma);
    for (const DrawnTile& tile : tiles) {
        CHECK(tile.style == ut::BlendStyle::Premultiplied);
        CHECK(tile.texture != bitmap[0].texture);
        CHECK(tile.x >= 9.5f);
        CHECK(tile.x + tile.w <= right + 0.5f);
        CHECK(tile.y >= 20.0f);
        CHECK(tile.y + tile.h <= bottom);
        std::vector<int> values = coverage(tile, 2);
        CHECK(std::any_of(values.begin(), values.end(), [](int value) {
            return value > 0 && value < 255;
        }));
        CHECK(std::any_of(values.begin(), values.end(), [](int value) {
            return value > 200;
        }));
    }
}

TEST_CASE("high resolution fonts keep the layout of the bitmap font, so each word fills the box the original gives it", "[data]")
{
    auto [name, tolerance] = GENERATE(table<const char*, float>({ { "UWindowFonts.Tahoma10", 0.75f }, { "UWindowFonts.Tahoma20", 2.0f } }));
    ut::UFont* tahoma = font(name);
    Enabled enabled;
    std::string text = "Tournament DeathMatch Auto Change Levels Min Desired Framerate";
    std::vector<DrawnTile> tiles = draw(tahoma, text, 2, 0, 0);
    REQUIRE(tiles.size() == 55);
    size_t tile = 0;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = std::min(text.find(' ', start), text.size());
        const DrawnTile& first = tiles[tile];
        const DrawnTile& last = tiles[tile + end - start - 1];
        INFO(name << " " << text.substr(start, end - start));
        CHECK(std::abs(first.x - ut::textWidth(tahoma, text.substr(0, start), 0)) <= tolerance);
        CHECK(std::abs(last.x + last.w - ut::textWidth(tahoma, text.substr(0, end), 0)) <= tolerance);
        tile += end - start;
        start = end + 1;
    }
}

TEST_CASE("high resolution fonts leave small text at 1x to the bitmap font and draw large text smooth", "[data]")
{
    Enabled enabled;
    std::vector<DrawnTile> small = draw(font("UWindowFonts.Tahoma10"), "Map List", 1, 0, 0);
    std::vector<DrawnTile> large = draw(font("UWindowFonts.Tahoma20"), "Map List", 1, 0, 0);
    REQUIRE_FALSE(small.empty());
    REQUIRE_FALSE(large.empty());
    CHECK(small[0].style == ut::BlendStyle::Masked);
    CHECK(large[0].style == ut::BlendStyle::Premultiplied);
}

TEST_CASE("high resolution fonts draw the bold menu fonts with the bold face", "[data]")
{
    Enabled enabled;
    std::vector<DrawnTile> regular = draw(font("UWindowFonts.Tahoma20"), "l", 2, 0, 0);
    std::vector<DrawnTile> bold = draw(font("UWindowFonts.TahomaB20"), "l", 2, 0, 0);
    REQUIRE(regular.size() == 1);
    REQUIRE(bold.size() == 1);
    double regularStem = static_cast<double>(totalCoverage(regular, 2)) / 255.0 / static_cast<double>(regular[0].h * 2);
    double boldStem = static_cast<double>(totalCoverage(bold, 2)) / 255.0 / static_cast<double>(bold[0].h * 2);
    CHECK(boldStem > 1.3 * regularStem);
}

TEST_CASE("high resolution fonts draw the Windows-1252 apostrophe as a small mark high on the line", "[data]")
{
    ut::UFont* tahoma = font("UWindowFonts.Tahoma20");
    Enabled enabled;
    std::vector<DrawnTile> tiles = draw(tahoma, "P\x92s", 2, 0, 0);
    REQUIRE(tiles.size() == 3);
    const DrawnTile& letter = tiles[0];
    const DrawnTile& apostrophe = tiles[1];
    CHECK(apostrophe.h < letter.h / 2);
    CHECK(apostrophe.y <= letter.y + 1.0f);
    CHECK(apostrophe.w < letter.w / 2);
}
