#include "CPUTextWin32.h"
#include <cstring>
namespace CPUText {
namespace {
HFONT FontFor(Font font)
{
    static HFONT cached = nullptr;
    static Font previous{0,0,0};
    if (!cached || font.height != previous.height || font.width != previous.width || font.weight != previous.weight) {
        if (cached) DeleteObject(cached);
        cached = CreateFont(font.height, font.width, 0, 0, font.weight, 0, 0, 0,
#ifdef __rus
            RUSSIAN_CHARSET,
#else
            ANSI_CHARSET,
#endif
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, nullptr);
        previous = font;
    }
    return cached;
}
}
int GDICanvas::Width(const char* text, Font font)
{
    if (!dc || !text) return 0;
    const auto old = SelectObject(dc, FontFor(font));
    SIZE size{};
    GetTextExtentPoint32(dc, text, static_cast<int>(std::strlen(text)), &size);
    SelectObject(dc, old);
    return size.cx;
}
void GDICanvas::Draw(int x, int y, const char* text, std::uint32_t color, Font font)
{
    if (!dc || !text) return;
    const auto oldBitmap = bitmap ? SelectObject(dc, bitmap) : nullptr;
    const auto oldFont = SelectObject(dc, FontFor(font));
    const auto oldMode = SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    TextOut(dc, x, y, text, static_cast<int>(std::strlen(text)));
    SetBkMode(dc, oldMode);
    SelectObject(dc, oldFont);
    if (oldBitmap) SelectObject(dc, oldBitmap);
}
}
