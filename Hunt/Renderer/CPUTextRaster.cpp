#include "CPUTextRaster.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>

namespace CPUText {
bool Buffer::Resize(int w, int h)
{
    if (w <= 0 || h <= 0 || std::size_t(w) > std::numeric_limits<std::size_t>::max() / sizeof(std::uint16_t) / std::size_t(h)) return false;
    try { pixels.assign(std::size_t(w) * h, 0); }
    catch (const std::bad_alloc&) { return false; }
    catch (const std::length_error&) { return false; }
    width = w; height = h;
    return true;
}
namespace {
struct FontConfig {
    FcConfig* config = FcInitLoadConfigAndFonts();
    ~FontConfig() { if (config) FcConfigDestroy(config); FcFini(); }
};
unsigned Codepoint(unsigned char c)
{
#ifdef __rus
    if (c >= 0xc0) return 0x410 + c - 0xc0;
    if (c == 0xa8) return 0x401;
    if (c == 0xb8) return 0x451;
#else
    constexpr unsigned cp1252[] = {0x20ac,0x81,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,0x2c6,0x2030,0x160,0x2039,0x152,0x8d,0x17d,0x8f,
        0x90,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0x9d,0x17e,0x178};
    if (c >= 0x80 && c < 0xa0) return cp1252[c - 0x80];
#endif
    return c;
}
}
struct RasterCanvas::Impl {
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    std::uint16_t* pixels = nullptr;
    int width = 0, height = 0, pitch = 0;
    Font font{0,0,0};
    int baseline = 0;
    Impl() {
        if (FT_Init_FreeType(&library)) return;
        static FontConfig fonts;
        FcConfig* config = fonts.config;
        if (!config) return;
        FcPattern* pattern = FcNameParse(reinterpret_cast<const FcChar8*>("Liberation Sans"));
        FcConfigSubstitute(config, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);
        FcResult result;
        FcPattern* match = FcFontMatch(config, pattern, &result);
        FcChar8* path = nullptr;
        if (match && FcPatternGetString(match, FC_FILE, 0, &path) == FcResultMatch)
            FT_New_Face(library, reinterpret_cast<const char*>(path), 0, &face);
        if (match) FcPatternDestroy(match);
        FcPatternDestroy(pattern);
    }
    ~Impl() { if (face) FT_Done_Face(face); if (library) FT_Done_FreeType(library); }
    bool Select(Font requested) {
        if (!face || requested.height <= 0) return false;
        if (requested.height == font.height && requested.width == font.width && requested.weight == font.weight) return true;
        font = requested;
        const int emHeight = std::max(1, requested.height * face->units_per_EM / face->height);
        FT_Set_Transform(face, nullptr, nullptr);
        if (FT_Set_Pixel_Sizes(face, 0, emHeight)) return false;
        // GDI's width argument is an average character width. Match that cell
        // intent, while keeping proportional glyph advances and the old layout.
        long total = 0;
        const char* sample = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        for (const char* c = sample; *c; ++c)
            if (!FT_Load_Char(face, *c, FT_LOAD_DEFAULT)) total += face->glyph->advance.x;
        const double average = total / (64.0 * std::strlen(sample));
        FT_Matrix transform{static_cast<FT_Fixed>(65536 * (requested.width > 0 && average > 0 ? requested.width / average : 1)), 0, 0, 65536};
        FT_Set_Transform(face, &transform, nullptr);
        baseline = static_cast<int>(face->size->metrics.ascender >> 6);
        return true;
    }
};
RasterCanvas::RasterCanvas() : impl(std::make_unique<Impl>()) {}
RasterCanvas::~RasterCanvas() = default;
void RasterCanvas::SetBuffer(std::uint16_t* pixels, int width, int height, int pitch)
{
    impl->pixels = pixels; impl->width = width; impl->height = height; impl->pitch = pitch;
}
bool RasterCanvas::Ready() const { return impl->face != nullptr; }
int RasterCanvas::Width(const char* text, Font font)
{
    if (!text || !impl->Select(font)) return 0;
    int width = 0;
    for (const auto* c = reinterpret_cast<const unsigned char*>(text); *c; ++c)
        if (!FT_Load_Char(impl->face, Codepoint(*c), FT_LOAD_DEFAULT)) width += (impl->face->glyph->advance.x + 32) >> 6;
    return width;
}
void RasterCanvas::Draw(int x, int y, const char* text, std::uint32_t color, Font font)
{
    if (!text || !impl->pixels || impl->pitch < impl->width || !impl->Select(font)) return;
    const auto packed = static_cast<std::uint16_t>(((color & 255) >> 3) << 10 | (((color >> 8) & 255) >> 3) << 5 | ((color >> 16) & 255) >> 3);
    for (const auto* c = reinterpret_cast<const unsigned char*>(text); *c; ++c) {
        if (FT_Load_Char(impl->face, Codepoint(*c), FT_LOAD_DEFAULT | FT_LOAD_TARGET_MONO) || FT_Render_Glyph(impl->face->glyph, FT_RENDER_MODE_MONO)) continue;
        const auto* glyph = impl->face->glyph;
        for (unsigned row = 0; row < glyph->bitmap.rows; ++row) {
            const int py = y + impl->baseline - glyph->bitmap_top + static_cast<int>(row);
            if (py < 0 || py >= impl->height) continue;
            for (unsigned col = 0; col < glyph->bitmap.width; ++col) {
                const int px = x + glyph->bitmap_left + static_cast<int>(col);
                if (px < 0 || px >= impl->width) continue;
                if (glyph->bitmap.buffer[row * glyph->bitmap.pitch + col / 8] & (0x80 >> (col % 8)))
                    impl->pixels[std::size_t(py) * impl->pitch + px] = packed;
            }
        }
        x += (glyph->advance.x + 32) >> 6;
    }
}
}
