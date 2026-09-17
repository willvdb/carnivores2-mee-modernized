// ==========================================================================
// UIText.h - scale-aware GDI text layout for the boxed HUD panels
//
// The score / trophy / survival box pictures are drawn scaled by
// uiscale = WinH/600 * UIScale, but the text inside them used to be drawn at
// fixed pixel offsets with a fixed 16px font. At higher resolutions the text
// then clustered into the top-left corner of a much larger box and collided
// with its frame.
//
// This unit owns the sizing rule for that text: one font per box, chosen so
// the widest row fits the panel horizontally and all rows fit it vertically.
//
// A plain multiply by uiscale is NOT sufficient, for two measured reasons:
//
//  1. GDI text width is not linear in font height - the default UI face gets
//     substituted for a relatively wider one as the height grows, so the same
//     string is 81px at 1:1 but 232px at 2.4x (2.86x, not 2.4x).
//  2. Strings vary a lot in length. "Name: Pachycephalosaurus" overflows the
//     210px-wide trophy art even at 1:1 with the stock font, and mod names run
//     to 22 characters.
//
// So the font size has to be measured against the actual strings and clamped
// to both axes. Callers build the rows once and hand them over; the helper
// does the fitting and the drawing.
// ==========================================================================
#pragma once

#include "CPUText.h"

namespace uitxt {

// HUD scale factor - the same one Hunt.cpp uses to draw the box pictures
// (score.tga, trophy.tga, collect.tga, exit_s.tga).
float Scale();

// Scales an unscaled art pixel value by Scale(), rounded to nearest.
int Px(int artPixels);

// One coloured run of text.
struct Seg {
    const char* text;
    std::uint32_t    color;
};

// One line, drawn left to right with a gap between consecutive segments.
struct Row {
    const Seg* seg;
    int        count;
};

// Draws `rows` inside the box whose top-left corner is (x, y) on screen.
//
// padX / padY  inset from the box corner to the text origin  (art pixels)
// step         line-to-line distance                          (art pixels)
// maxW / maxH  usable text area inside the panel              (art pixels)
//
// Every row shares one font size, starting from the resolution-scaled 16px and
// shrinking until the widest row fits maxW and all rows fit maxH. Returns the
// pixel height actually used, or 0 if nothing could be drawn.
int DrawBox(CPUText::Canvas* canvas, int x, int y,
            int padX, int padY, int step,
            int maxW, int maxH,
            const Row* rows, int rowCount);

} // namespace uitxt
