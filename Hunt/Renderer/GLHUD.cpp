// ==========================================================================
// GLHUD.cpp � HUD overlay and UI element rendering
// ==========================================================================

#include "Hunt.h"
#include "GLRenderer.h"
#include "Renderer/GLUtils.h"
#include "Renderer/UIText.h"
#include "Platform/Platform.h"

#ifdef _gl

#include "glad/glad.h"

void GLRenderer::Render_LifeInfo(int index)
{
    // Draw dino info when looking through binoculars
    auto* canvas = CPUText::GameCanvas();
    if (!canvas) return;
    if (index < 0 || index >= ChCount) return;


    int ctype = Characters[index].CType;
    float scale = Characters[index].scale;
    char t[32];

    int x = VideoCX + WinW / 64;
    int y = VideoCY + static_cast<int>((WinH / 6.8));

    auto textOut = [&](int px, int py, const char* str, int color) {
        canvas->Draw(px + 1, py + 1, str, 0x00000000, CPUText::SmallFont());
        canvas->Draw(px, py, str, color, CPUText::SmallFont());
    };

    textOut(x, y, DinoInfo[ctype].Name, 0x0000b000);

    if (OptSys) sprintf(t, "Weight: %3.2ft ", DinoInfo[ctype].Mass * scale * scale / 0.907f);
    else        sprintf(t, "Weight: %3.2fT ", DinoInfo[ctype].Mass * scale * scale);
    textOut(x, y + 16, t, 0x0000b000);

    int R = static_cast<int>((VectorLength(SubVectors(Characters[index].pos, PlayerPos)) * 3 / 64.0f));
    if (OptSys) sprintf(t, "Distance: %dft ", R);
    else        sprintf(t, "Distance: %dm  ", R / 3);
    textOut(x, y + 32, t, 0x0000b000);

    // Mark dirty: 3 lines × 16px step + shadow + font height
    MarkDirtyRect(x - 1, y - 1, 140, 50);

}

void GLRenderer::Render_Cross(int x, int y)
{
    if (!m_modelShader.IsValid() || !m_modelVAO || !m_modelVBO || !m_whiteTexture ||
        WinW <= 0 || WinH <= 0) {
        return;
    }

    float radius = static_cast<float>(WinW) / 12.0f * UIScale;
    radius = (std::max)(1.0f, radius);
    const float halfThicknessX = 1.5f / static_cast<float>(WinW);
    const float halfThicknessY = 1.5f / static_cast<float>(WinH);
    const float centerX = static_cast<float>(x) / static_cast<float>(WinW) * 2.0f - 1.0f;
    const float centerY = 1.0f - static_cast<float>(y) / static_cast<float>(WinH) * 2.0f;
    const float radiusX = radius / static_cast<float>(WinW) * 2.0f;
    const float radiusY = radius / static_cast<float>(WinH) * 2.0f;

    auto makeVertex = [](float px, float py) -> ModelVertex {
        return {
            px, py, 0.0f, 0.0f, 0.0f,
            255, 255, 128, 0, // light, fog-to-black, alpha, cutout
            0, 0, 0,          // fog colour
            {0, 0, 0, 0, 0}
        };
    };

    std::vector<ModelVertex> vertices;
    vertices.reserve(12);
    auto appendQuad = [&](float left, float top, float right, float bottom) {
        vertices.push_back(makeVertex(left,  top));
        vertices.push_back(makeVertex(right, top));
        vertices.push_back(makeVertex(right, bottom));
        vertices.push_back(makeVertex(left,  top));
        vertices.push_back(makeVertex(right, bottom));
        vertices.push_back(makeVertex(left,  bottom));
    };
    appendQuad(centerX - radiusX, centerY + halfThicknessY,
               centerX + radiusX, centerY - halfThicknessY);
    appendQuad(centerX - halfThicknessX, centerY + radiusY,
               centerX + halfThicknessX, centerY - radiusY);

    const std::array<float, 16> identity = {
        1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f,
        0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f
    };
    UpdatePerFrameUBO(identity);
    m_modelShader.Use();

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_whiteTexture);
    glBindVertexArray(m_modelVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_modelVBO);
    const GLsizeiptr bytes = static_cast<GLsizeiptr>(vertices.size() * sizeof(ModelVertex));
    glBufferData(GL_ARRAY_BUFFER, bytes, nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, vertices.data());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
#ifdef GL_PERF_HOOKS
    GL_PERF_DRAW(4);
#endif
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);

    // NDC drawing temporarily replaces the shared projection UBO. Restore the
    // active world/optic projection for any model effects drawn afterward.
    UpdatePerFrameUBO(BuildLegacyProjection());
}

void GLRenderer::RenderHealthBar()
{
    // Interface compliance only. The GL path draws the health bar into
    // lpVideoBuf via the free function RenderHealthBar() in GLStubs.cpp
    // (called from ShowControlElements), and DrawHUDOverlay uploads it
    // along with the rest of the HUD. This matches how the other 2D
    // elements (DrawPicture, DrawTrophyText, etc.) are handled.
}

void GLRenderer::DrawTrophyText(int x, int y)
{
    // Trophy text is rendered via GDI onto the lpVideoBuf.
    // D3D/3DFX call ddTextOut/FXTextOut which write to the backbuffer.
    // For GL, we draw text onto lpVideoBuf using GDI, then DrawHUDOverlay uploads it.
    // We need to use the hdcCMain + hbmpVideoBuf to draw onto lpVideoBuf.

    auto* canvas = CPUText::GameCanvas();
    if (!canvas) return;

    const int   dtype = TrophyDisplayBody.ctype;
    const int   time  = TrophyDisplayBody.time;
    const int   date  = TrophyDisplayBody.date;
    const int   wep   = TrophyDisplayBody.weapon;
    const int   score = TrophyDisplayBody.score;
    const float scale = TrophyDisplayBody.scale;
    const float range = TrophyDisplayBody.range;

    char tWeight[32], tLength[32], tWeapon[64], tScore[32], tRange[32], tDate[32], tTime[32];

    if (OptSys) sprintf_s(tWeight, sizeof(tWeight), "%3.2ft", DinoInfo[dtype].Mass * scale * scale / 0.907f);
    else        sprintf_s(tWeight, sizeof(tWeight), "%3.2fT", DinoInfo[dtype].Mass * scale * scale);

    if (OptSys) sprintf_s(tLength, sizeof(tLength), "%3.2fft", DinoInfo[dtype].Length * scale / 0.3f);
    else        sprintf_s(tLength, sizeof(tLength), "%3.2fm", DinoInfo[dtype].Length * scale);

    sprintf_s(tWeapon, sizeof(tWeapon), "%s", WeapInfo[wep].Name);
    sprintf_s(tScore,  sizeof(tScore),  "%d", score);

    if (OptSys) sprintf_s(tRange, sizeof(tRange), "%3.1fft", range / 0.3f);
    else        sprintf_s(tRange, sizeof(tRange), "%3.1fm", range);

    if (OptSys) sprintf_s(tDate, sizeof(tDate), "%d.%d.%d", ((date >> 10) & 255), (date & 255), date >> 20);
    else        sprintf_s(tDate, sizeof(tDate), "%d.%d.%d", (date & 255), ((date >> 10) & 255), date >> 20);

    sprintf_s(tTime, sizeof(tTime), "%d:%02d", ((time >> 10) & 255), (time & 255));

    // Five paired rows rather than eight single-stat ones: the recessed panel
    // in trophy.tga/collect.tga is only ~76 art pixels tall, so eight rows at a
    // resolution-scaled font cannot fit it. This is the layout C2's software
    // renderer and both C1 renderers already use - all eight stats are kept.
    const std::uint32_t kLabel = 0x00BFBFBF;
    const std::uint32_t kValue = 0x0000BFBF;

    const uitxt::Seg rowName[]   = { { "Name: ",        kLabel }, { DinoInfo[dtype].Name, kValue } };
    const uitxt::Seg rowSize[]   = { { "Weight: ",      kLabel }, { tWeight,  kValue },
                                     { "Length: ",      kLabel }, { tLength,  kValue } };
    const uitxt::Seg rowGear[]   = { { "Weapon: ",      kLabel }, { tWeapon,  kValue },
                                     { "Score: ",       kLabel }, { tScore,   kValue } };
    const uitxt::Seg rowRange[]  = { { "Range of kill: ", kLabel }, { tRange, kValue } };
    const uitxt::Seg rowWhen[]   = { { "Date: ",        kLabel }, { tDate,    kValue },
                                     { "Time: ",        kLabel }, { tTime,    kValue } };

    const uitxt::Row rows[] = {
        { rowName,  2 },
        { rowSize,  4 },
        { rowGear,  4 },
        { rowRange, 2 },
        { rowWhen,  4 },
    };


    // trophy.tga / collect.tga are 210x124; the recessed panel spans rows
    // 22..98, so the text area starts 18px down and is 80px tall.
    uitxt::DrawBox(canvas, x, y,
                   /*padX*/ 16, /*padY*/ 18, /*step*/ 16,
                   /*maxW*/ 190, /*maxH*/ 80,
                   rows, 5);

    // Cover the whole panel rather than the text extent: the chosen font can
    // shrink, and a too-small rect would leave ghost pixels on the overlay.
    MarkDirtyRect(x + uitxt::Px(16) - 2, y + uitxt::Px(18) - 2,
                  uitxt::Px(190) + 4, uitxt::Px(80) + 6);

}

// Present the existing CPU loading picture through the same UI shader/quad.
// No native window or separate GL context is involved.
void GLRenderer::PresentLoading(const std::uint16_t* pixels, int width, int height, int pitch)
{
    if (!pixels || !m_uiShader.IsValid() || width <= 0 || height <= 0) return;
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5, width, height, 0,
                 GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    m_uiShader.Use();
    glBindVertexArray(m_uiVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glDeleteTextures(1, &texture);
    glEnable(GL_DEPTH_TEST);
    Platform::SwapGLBuffers();
}

void GLRenderer::DrawHUDOverlay()
{
#ifdef GL_PERF_HOOKS
    GL_PERF_SCOPE("DrawHUDOverlay");
#endif

    if (!m_uiShader.IsValid() || !lpVideoBuf || WinW <= 0 || WinH <= 0) return;

    EnsureUITexture();

    // CPU RGB555 rows use the explicit pixel pitch, including odd widths.
    // The default four-byte unpack alignment would silently pad those rows.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glBindTexture(GL_TEXTURE_2D, m_uiTexture);
#ifdef GL_PERF_HOOKS
    GL_PERF_TEXTURE_BIND(m_uiTexture);
#endif

    // Upload dirty regions (or full buffer if needed).
    // The texture is top-down (EnsureUITexture allocates with nullptr data),
    // but lpVideoBuf is top-down too (CreateVideoDIB with negative height).
    // So no flip is needed at upload — the vertex shader handles the v-flip.
    //
    // We upload BOTH the previous frame's dirty rects (now cleared to zero
    // by ClearStaleHUDRegions) AND the current frame's dirty rects (just
    // drawn by HUD elements).  This ensures stale pixels that disappeared
    // get zeroed on the GPU, while new pixels appear.
    if (m_hudNeedsFullUpload) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, VideoPitch);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WinW, WinH,
                        GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, lpVideoBuf);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        m_hudNeedsFullUpload = false;
    } else {
        int totalRects = m_prevDirtyRectCount + m_dirtyRectCount;
        if (totalRects > kMaxDirtyRects) {
            // Too many rects to upload individually — request a full
            // clear+upload for next frame instead of uploading the (possibly
            // stale) buffer now. This frame we still upload the tracked
            // prev/current rects below (fresh content); any older stale pixels
            // are skipped here and wiped by next frame's full clear.
            m_hudNeedsFullClear = true;
        }
        // Upload previous frame's rects that are NOT fully covered by
        // a current rect (those covered rects will be uploaded anyway
        // by the current-rect pass below with fresh content).
            for (int i = 0; i < m_prevDirtyRectCount; i++) {
                const DirtyRect& r = m_prevDirtyRects[i];
                bool covered = false;
                for (int j = 0; j < m_dirtyRectCount; j++) {
                    const DirtyRect& c = m_dirtyRects[j];
                    if (r.x >= c.x && r.y >= c.y &&
                        r.x + r.w <= c.x + c.w && r.y + r.h <= c.y + c.h) {
                        covered = true;
                        break;
                    }
                }
                if (covered) continue;  // will be uploaded with current content below

                const WORD* src = static_cast<const WORD*>(lpVideoBuf)
                                  + static_cast<size_t>(r.y) * VideoPitch + r.x;
                glPixelStorei(GL_UNPACK_ROW_LENGTH, VideoPitch);
                glTexSubImage2D(GL_TEXTURE_2D, 0, r.x, r.y, r.w, r.h,
                                GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, src);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        }
        // Then upload current frame's rects (newly drawn content)
            for (int i = 0; i < m_dirtyRectCount; i++) {
                const DirtyRect& r = m_dirtyRects[i];
                const WORD* src = static_cast<const WORD*>(lpVideoBuf)
                                  + static_cast<size_t>(r.y) * VideoPitch + r.x;
                glPixelStorei(GL_UNPACK_ROW_LENGTH, VideoPitch);
                glTexSubImage2D(GL_TEXTURE_2D, 0, r.x, r.y, r.w, r.h,
                                GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, src);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            }
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_uiShader.Use();
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(m_uiVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
#ifdef GL_PERF_HOOKS
    GL_PERF_DRAW(2);
#endif
    glBindVertexArray(0);

    glDisable(GL_BLEND);
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif
    glDepthMask(GL_TRUE);
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif
    glEnable(GL_DEPTH_TEST);
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif

    // Swap: current becomes previous for next frame's clear+upload
    for (int i = 0; i < m_dirtyRectCount && i < kMaxDirtyRects; i++)
        m_prevDirtyRects[i] = m_dirtyRects[i];
    m_prevDirtyRectCount = m_dirtyRectCount;
    m_dirtyRectCount = 0;
}

void GLRenderer::DrawScaledPicture(int x, int y, int w, int h, TPicture& pic)
{
    if (!pic.lpImage || pic.W <= 0 || pic.H <= 0 || !lpVideoBuf) return;

    WORD* dst = static_cast<WORD*>(lpVideoBuf);
    for (int yy = 0; yy < h; yy++) {
        int dstY = yy + y;
        if (dstY < 0 || dstY >= WinH) continue;
        int sy = yy * pic.H / h;
        for (int xx = 0; xx < w; xx++) {
            int dstX = xx + x;
            if (dstX < 0 || dstX >= WinW) continue;
            int sx = xx * pic.W / w;
            WORD c = pic.lpImage[sy * pic.W + sx];
            if (c != 0) dst[dstY * VideoPitch + dstX] = Conv565to555(c);
        }
    }

    MarkDirtyRect(x, y, w, h);
}

void GLRenderer::DrawPicture(int x, int y, TPicture& pic)
{
    if (!pic.lpImage || pic.W <= 0 || pic.H <= 0 || !lpVideoBuf) return;

    WORD* dst = static_cast<WORD*>(lpVideoBuf);
    for (int yy = 0; yy < pic.H; yy++) {
        int dstY = yy + y;
        if (dstY < 0 || dstY >= WinH) continue;
        int copyW = pic.W;
        int srcX = 0;
        int dstX = x;
        if (dstX < 0) { srcX = -dstX; copyW += dstX; dstX = 0; }
        if (dstX + copyW > WinW) copyW = WinW - dstX;
        if (copyW <= 0) continue;
        const WORD* src = pic.lpImage.get() + yy * pic.W + srcX;
        WORD* d = dst + dstY * VideoPitch + dstX;
        for (int i = 0; i < copyW; i++) {
            d[i] = Conv565to555(src[i]);
        }
    }

    MarkDirtyRect(x, y, pic.W, pic.H);
}

void GLRenderer::RegisterPicture(TPicture* pptr)
{
    // No-op: we copy to lpVideoBuf in DrawPicture instead
    (void)pptr;
}

void GLRenderer::MarkDirtyRect(int x, int y, int w, int h)
{
    // Clamp to screen bounds
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > WinW) w = WinW - x;
    if (y + h > WinH) h = WinH - y;
    if (w <= 0 || h <= 0) return;

    // If we already need a full upload (overflow, texture recreated, etc.),
    // don't bother accumulating rects.
    if (m_hudNeedsFullUpload) return;

    // Check if this rect is already covered by an existing rect
    for (int i = 0; i < m_dirtyRectCount; i++) {
        const DirtyRect& r = m_dirtyRects[i];
        if (x >= r.x && y >= r.y && x + w <= r.x + r.w && y + h <= r.y + r.h)
            return;  // fully contained
    }

    // Overflow guard: too many rects to track individually. Request a full
    // buffer clear+upload for the NEXT frame instead of uploading the
    // (possibly stale) buffer now. Simply dropping rects here would leave
    // their pixels in lpVideoBuf unmarked — and therefore un-erased — which
    // is exactly what causes HUD text/ghosting to persist. The next frame's
    // ClearStaleHUDRegions memsets the whole buffer, wiping those pixels.
    if (m_dirtyRectCount >= kMaxDirtyRects) {
        m_hudNeedsFullClear = true;
        m_dirtyRectCount = 0;
        return;
    }

    m_dirtyRects[m_dirtyRectCount++] = {x, y, w, h};
}

void GLRenderer::InvalidateHUDOverlay()
{
    // Called after CopyHARDToDIB writes the full 3D scene into lpVideoBuf.
    // The next frame must do a full-buffer clear + full upload to erase
    // the non-HUD scene pixels from the overlay texture.
    m_hudNeedsFullClear = true;
    m_dirtyRectCount = 0;
    m_prevDirtyRectCount = 0;
}

void GLRenderer::ClearStaleHUDRegions()
{
    // Clear only the regions from the previous frame (not the whole buffer).
    // This zeros out HUD elements that may have moved or disappeared,
    // while leaving the rest of lpVideoBuf untouched (it will retain its
    // previous content which is still valid on the GPU texture).
    if (!lpVideoBuf || VideoPitch <= 0) return;

    if (m_hudNeedsFullClear) {
        // A full-DIB write (CopyHARDToDIB) happened — the entire buffer
        // has non-zero scene data.  Clear it all and force full upload.
        memset(lpVideoBuf, 0, static_cast<size_t>(VideoPitch) * WinH * sizeof(WORD));
        m_hudNeedsFullClear = false;
        m_hudNeedsFullUpload = true;
        return;
    }

    for (int i = 0; i < m_prevDirtyRectCount; i++) {
        const DirtyRect& r = m_prevDirtyRects[i];
        // Clamp to be safe (rects from previous frame should already be clamped)
        int cx = r.x, cy = r.y, cw = r.w, ch = r.h;
        if (cx < 0) { cw += cx; cx = 0; }
        if (cy < 0) { ch += cy; cy = 0; }
        if (cx + cw > WinW) cw = WinW - cx;
        if (cy + ch > WinH) ch = WinH - cy;
        if (cw <= 0 || ch <= 0) continue;

        WORD* row = static_cast<WORD*>(lpVideoBuf) + static_cast<size_t>(cy) * VideoPitch + cx;
        for (int yy = 0; yy < ch; yy++) {
            memset(row, 0, static_cast<size_t>(cw) * sizeof(WORD));
            row += VideoPitch;
        }
    }
}

void GLRenderer::UpdateUIPixels()
{
    // Phase 2.20: this function is now a no-op.  lpVideoBuf is uploaded
    // directly to the GPU as a GL_RGB5 texture in DrawHUDOverlay — no
    // CPU-side 555→RGBA8 conversion needed.  Saved ~1ms CPU per frame.
    (void)0;
}

void GLRenderer::EnsureUITexture()
{
    // Phase 2.20: allocate as GL_RGB5 (16-bit) to match lpVideoBuf's
    // X1R5G5B5 format.  No CPU-side RGBA8 buffer needed — we upload
    // lpVideoBuf directly via glTexSubImage2D.
    if (WinW <= 0 || WinH <= 0) return;
    if (m_uiTexture && m_uiTextureWidth == WinW && m_uiTextureHeight == WinH) return;

    if (!m_uiTexture) glGenTextures(1, &m_uiTexture);
    glBindTexture(GL_TEXTURE_2D, m_uiTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5, WinW, WinH, 0,
                 GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, nullptr);
    m_uiTextureWidth = WinW;
    m_uiTextureHeight = WinH;
    m_hudNeedsFullUpload = true;  // texture recreated — must full-upload next frame
    m_dirtyRectCount = 0;
    m_prevDirtyRectCount = 0;
}

void GLRenderer::ShutdownHudPipeline()
{
    if (m_uiTexture && m_hasContext) { glDeleteTextures(1, &m_uiTexture); m_uiTexture = 0; }
    if (m_uiVBO && m_hasContext) { glDeleteBuffers(1, &m_uiVBO); m_uiVBO = 0; }
    if (m_uiVAO && m_hasContext) { glDeleteVertexArrays(1, &m_uiVAO); m_uiVAO = 0; }
    // m_uiShader destroyed by GLShader destructor
    m_uiTextureWidth = 0;
    m_uiTextureHeight = 0;
}

void GLRenderer::InitializeHudPipeline()
{
    // Phase 2.20: skip the GDI round-trip.  lpVideoBuf is 16-bit 555
    // (X1R5G5B5), top-down.  We upload it directly as a GL_RGB5 texture
    // and let the fragment shader convert to RGBA8 with transparency.
    // This eliminates the UpdateUIPixels CPU loop (480K pixel conversions)
    // and the m_uiPixels RGBA8 buffer (1.92 MB).
    if (!m_uiShader.LoadFromFile("shaders/ui.vert", "shaders/ui.frag")) {
        LOG_ERROR("UI shader compilation failed");
        return;
    }
    LOG_INFO("UI shader compilation succeeded");

    // Static fullscreen quad in NDC: (x, y, u, v) per vertex
    // Texture is flipped vertically in UpdateUIPixels, so:
    //   tex (0,0) = bottom-left of image, tex (0,1) = top-left of image
    //   Screen top-left (-1,1) should show tex (0,1) = top of image
    constexpr float kQuadVertices[] = {
        -1.0f,  1.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
    };

    glGenVertexArrays(1, &m_uiVAO);
    glGenBuffers(1, &m_uiVBO);

    glBindVertexArray(m_uiVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_uiVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_uiShader.Use();
    glUniform1i(glGetUniformLocation(m_uiShader.GetProgramID(), "uTexture"), 0);
}

void GLRenderer::DrawHMap()
{
}

void GLRenderer::DrawTPlaneClip(bool clip)
{
    (void)clip;
}

void GLRenderer::DrawTPlane(bool clip)
{
    (void)clip;
}

void GLRenderer::DrawPostObjects()
{
}

#endif // _gl
