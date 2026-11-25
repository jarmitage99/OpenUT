#pragma once

#include <cstdint>
#include <vector>

namespace ut {
    struct ScreenSpan {
        int32_t y = 0;
        int32_t x0 = 0;
        int32_t x1 = 0;
    };

    struct ScreenPoint {
        float x = 0;
        float y = 0;
    };

    class SpanBuffer {
    public:
        void reset(int32_t width, int32_t height);
        void reset(int32_t width, int32_t height, const std::vector<ScreenSpan>& open);
        bool full() const;
        bool rectVisible(int32_t x0, int32_t y0, int32_t x1, int32_t y1) const;
        bool polygonVisible(const ScreenPoint* points, int32_t count, std::vector<ScreenSpan>* visible) const;
        void occlude(const ScreenPoint* points, int32_t count);

    private:
        struct Interval {
            int32_t x0 = 0;
            int32_t x1 = 0;
        };

        struct Edge {
            float low = 0;
            float high = 0;
            float x = 0;
            float y = 0;
            float dx = 0;
            float dy = 0;
            float slope = 0;
            float minX = 0;
            float maxX = 0;
        };

        void cover(int32_t y, int32_t x0, int32_t x1);

        std::vector<std::vector<Interval>> rows;
        mutable std::vector<Edge> edges;
        int32_t columns = 0;
        int32_t openRows = 0;
    };
}
