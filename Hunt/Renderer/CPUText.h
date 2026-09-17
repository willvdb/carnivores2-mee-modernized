#pragma once
#include <cstdint>

namespace CPUText {
struct Font { int height = 16, width = 7, weight = 100; };
class Canvas {
public:
    virtual ~Canvas() = default;
    virtual int Width(const char* text, Font font) = 0;
    virtual void Draw(int x, int y, const char* text, std::uint32_t color, Font font) = 0;
};
// Game adapter; neither GDI nor the portable rasterizer owns the engine buffer.
Canvas* GameCanvas();
Font SmallFont();
Font MiddleFont();
}
