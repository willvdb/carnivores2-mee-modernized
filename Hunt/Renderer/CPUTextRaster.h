#pragma once
#include "CPUText.h"
#include <memory>
#include <vector>
namespace CPUText {
// Top-down RGB555, tightly packed words. Padding is explicit when borrowing a buffer.
class Buffer {
public:
    bool Resize(int width, int height);
    std::uint16_t* Pixels() { return pixels.data(); }
    int Width() const { return width; }
    int Height() const { return height; }
    int Pitch() const { return width; }
private:
    int width = 0, height = 0;
    std::vector<std::uint16_t> pixels;
};
class RasterCanvas final : public Canvas {
public:
    RasterCanvas();
    ~RasterCanvas() override;
    void SetBuffer(std::uint16_t* pixels, int width, int height, int pitch);
    bool Ready() const;
    int Width(const char* text, Font font) override;
    void Draw(int x, int y, const char* text, std::uint32_t color, Font font) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
