// ==========================================================================
// UIText.cpp - see UIText.h for why the box text needs measuring, not
// just multiplying by the HUD scale.
// ==========================================================================

extern int WinH;
extern float UIScale;
#include "Renderer/UIText.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace uitxt {

namespace {

// Below this the text stops being legible, so it is better to overflow the art
// slightly than to keep shrinking.
constexpr int kMinFontPx = 6;

// The stock font was created as CreateFont(16, 7, ...) - see EngineInit.cpp.
constexpr int kBaseFontPx  = 16;
constexpr int kBaseFontWidth = 7;

CPUText::Font FontFor(int px)
{
    return {px, static_cast<int>(std::lround(static_cast<double>(px) * kBaseFontWidth / kBaseFontPx)), 100};
}
int MulDiv(int a, int b, int c)
{
    return static_cast<int>(std::lround(static_cast<double>(a) * b / c));
}
int TextWidth(CPUText::Canvas* canvas, const char* s, int px)
{
    return s ? canvas->Width(s, FontFor(px)) : 0;
}

int RowWidth(CPUText::Canvas* canvas, const Row& row, int gap, int px)
{
    int total = 0;
    for (int i = 0; i < row.count; i++) {
        total += TextWidth(canvas, row.seg[i].text, px);
        if (i + 1 < row.count) total += gap;
    }
    return total;
}

int WidestRow(CPUText::Canvas* canvas, const Row* rows, int rowCount, int gap, int px)
{
    int widest = 0;
    for (int i = 0; i < rowCount; i++) {
        widest = (std::max)(widest, RowWidth(canvas, rows[i], gap, px));
    }
    return widest;
}

} // namespace

float Scale()
{
    return static_cast<float>(WinH) / 600.0f * UIScale;
}

int Px(int artPixels)
{
    return static_cast<int>(std::lround(static_cast<double>(artPixels) * Scale()));
}

int DrawBox(CPUText::Canvas* canvas, int x, int y,
            int padX, int padY, int step,
            int maxW, int maxH,
            const Row* rows, int rowCount)
{
    if (!canvas || !rows || rowCount <= 0) return 0;

    const float s = Scale();
    if (s <= 0.0f) return 0;

    const int padXpx = Px(padX);
    const int padYpx = Px(padY);
    const int stepPx = Px(step);
    const int availW = Px(maxW);
    const int availH = Px(maxH);
    if (availW <= 0 || availH <= 0) return 0;

    const int nominal = (std::max)(kMinFontPx, Px(kBaseFontPx));

    // Shrink from the resolution-scaled size until the widest row fits the
    // panel width and all rows fit its height. Width is re-measured each pass
    // because it does not scale linearly with the font height.
    int px = nominal;
    for (int iter = 0; iter < 6; iter++) {
        const int gap = (std::max)(1, px / 2);

        const int widest = WidestRow(canvas, rows, rowCount, gap, px);

        const int byWidth = (widest > 0) ? MulDiv(px, availW, widest) : px;

        // Block height ~= (rowCount-1) * step*(px/nominal) + px, so the tallest
        // font that still fits solves to availH * nominal / ((rows-1)*step + nominal).
        const int denom = (rowCount - 1) * stepPx + nominal;
        const int byHeight = (denom > 0) ? MulDiv(availH, nominal, denom) : px;

        int target = (std::min)(px, (std::min)(byWidth, byHeight));
        if (target < kMinFontPx) target = kMinFontPx;
        if (target >= px) break;

        px = target;
    }

    const int gap = (std::max)(1, px / 2);
    const int lineStep = (std::max)(px, MulDiv(stepPx, px, nominal));


    for (int r = 0; r < rowCount; r++) {
        const Row& row = rows[r];
        int cx = x + padXpx;
        const int cy = y + padYpx + r * lineStep;

        for (int i = 0; i < row.count; i++) {
            const Seg& seg = row.seg[i];
            if (seg.text && seg.text[0]) {
                const int len = static_cast<int>(strlen(seg.text));
                canvas->Draw(cx + 1, cy + 1, seg.text, 0x00101010, FontFor(px));
                canvas->Draw(cx, cy, seg.text, seg.color, FontFor(px));
                cx += TextWidth(canvas, seg.text, px);
            }
            if (i + 1 < row.count) cx += gap;
        }
    }


    return px;
}

} // namespace uitxt
