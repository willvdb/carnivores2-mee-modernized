#pragma once
#include "CPUText.h"
#include <windows.h>
namespace CPUText {
class GDICanvas final : public Canvas {
public:
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    int Width(const char* text, Font font) override;
    void Draw(int x, int y, const char* text, std::uint32_t color, Font font) override;
};
}
