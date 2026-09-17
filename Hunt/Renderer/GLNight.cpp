// ==========================================================================
// GLNight.cpp � Night vision and desaturation rendering
// ==========================================================================

#include "Hunt.h"
#include "GLRenderer.h"
#include "Renderer/GLUtils.h"

#ifdef _gl

#include "glad/glad.h"
#include <cmath>

#ifdef GL_PERF_HOOKS
#include "Renderer/GLPerf.h"
#endif

void GLRenderer::RenderNightDarkness()
{
    // Just the dark overlay — applied AFTER the HUD is composited
    RenderFSRect(0x80000000, false);
}

void GLRenderer::RenderSceneDesaturated()
{
    if (!m_nightDesatProgram.IsValid()) return;

    EnsureNightSceneTex(m_nightSceneTex, m_nightTexWidth, m_nightTexHeight, WinW, WinH);

    // 1. Copy the current framebuffer (3D scene) to the texture
    glBindTexture(GL_TEXTURE_2D, m_nightSceneTex);
#ifdef GL_PERF_HOOKS
    GL_PERF_TEXTURE_BIND(m_nightSceneTex);
#endif
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, WinW, WinH);

    // 2. Render opaque fullscreen quad with desaturation shader
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    GLint blendSaved;
    glGetIntegerv(GL_BLEND, &blendSaved);
    glDisable(GL_BLEND);

    m_nightDesatProgram.Use();
    glUniform1i(m_locNightDesatTexture, 0);
    glBindTexture(GL_TEXTURE_2D, m_nightSceneTex);
#ifdef GL_PERF_HOOKS
    GL_PERF_TEXTURE_BIND(m_nightSceneTex);
#endif
    glBindVertexArray(m_uiVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
#ifdef GL_PERF_HOOKS
    GL_PERF_DRAW(2);
#endif
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    if (blendSaved) glEnable(GL_BLEND);
}

void GLRenderer::ShutdownNightDesaturation()
{
    if (m_nightSceneTex && m_hasContext) { glDeleteTextures(1, &m_nightSceneTex); m_nightSceneTex = 0; }
    // m_nightDesatProgram destroyed by GLShader destructor
    m_nightTexWidth = 0;
    m_nightTexHeight = 0;
    m_locNightDesatTexture = -1;
    m_locNightDesatStrength = -1;
}

void GLRenderer::InitializeNightDesaturation()
{
    if (!m_nightDesatProgram.LoadFromFile("shaders/night_desat.vert", "shaders/night_desat.frag")) {
        LOG_ERROR("Night desaturation shader compilation failed");
        return;
    }
    LOG_INFO("Night desaturation shader compilation succeeded");

    m_nightDesatProgram.Use();
    m_locNightDesatTexture = glGetUniformLocation(m_nightDesatProgram.GetProgramID(), "uSceneTexture");
    m_locNightDesatStrength = glGetUniformLocation(m_nightDesatProgram.GetProgramID(), "uDesaturateStrength");
    glUniform1i(m_locNightDesatTexture, 0);
    glUniform1f(m_locNightDesatStrength, 0.6f);  // 60% desaturation
}

#endif // _gl
