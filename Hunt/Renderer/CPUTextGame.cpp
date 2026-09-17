#include "Hunt.h"
#include "CPUText.h"
#ifdef _WIN32
#include "CPUTextWin32.h"
#else
#include "CPUTextRaster.h"
#endif
namespace CPUText {
Font SmallFont() { return {static_cast<int>(16 * UIScale), static_cast<int>(7 * UIScale), 100}; }
Font MiddleFont() { auto font = SmallFont(); font.weight = 550; return font; }
Canvas* GameCanvas()
{
    if (!lpVideoBuf) return nullptr;
#ifdef _WIN32
    static GDICanvas canvas;
    canvas.dc = hdcCMain;
    canvas.bitmap = hbmpVideoBuf;
    return canvas.dc && canvas.bitmap ? &canvas : nullptr;
#else
    static RasterCanvas canvas;
    canvas.SetBuffer(static_cast<std::uint16_t*>(lpVideoBuf), WinW, WinH, VideoPitch);
    return &canvas;
#endif
}
}
