#include "platform/render/test_pattern.h"

#include <cstddef>

namespace ac04::render {

std::vector<std::uint8_t> MakeTestPattern(int width, int height) {
    std::vector<std::uint8_t> px(static_cast<size_t>(width) * height * 4);
    static const std::uint8_t bars[8][3] = {{192, 192, 192}, {192, 192, 0}, {0, 192, 192},
                                            {0, 192, 0},     {192, 0, 192}, {192, 0, 0},
                                            {0, 0, 192},     {16, 16, 16}};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::uint8_t r, g, b;
            if (y < height * 2 / 3) {
                const auto& c = bars[x * 8 / width];
                r = c[0];
                g = c[1];
                b = c[2];
            } else if (x < width / 2) {
                // One-pixel checkerboard: shows the difference between the filters most clearly.
                const std::uint8_t v = ((x ^ y) & 1) ? 255 : 0;
                r = g = b = v;
            } else {
                // Horizontal grey ramp.
                const int v = (x - width / 2) * 255 / (width / 2 - 1);
                r = g = b = static_cast<std::uint8_t>(v);
            }
            // One-pixel grid every 32 pixels and a white border.
            if (x % 32 == 0 || y % 32 == 0) r = g = b = 255;
            if (x == 0 || y == 0 || x == width - 1 || y == height - 1) {
                r = 255;
                g = 64;
                b = 64;
            }
            std::uint8_t* p = &px[(static_cast<size_t>(y) * width + x) * 4];
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 255;
        }
    }
    return px;
}

}  // namespace ac04::render
