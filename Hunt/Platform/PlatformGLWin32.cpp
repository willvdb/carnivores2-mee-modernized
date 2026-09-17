// Legacy WGL reference backend. SDL builds do not compile this file.
#include "Platform.h"
#include "PlatformWin32.h"
#include "../Debug/Log.h"

#define WGL_CONTEXT_MAJOR_VERSION_ARB     0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB     0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB      0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB  0x00000001

using PFNWGLCREATECONTEXTATTRIBSARBPROC = HGLRC (WINAPI *)(HDC, HGLRC, const int*);




namespace {
HWND window = nullptr;
HDC device = nullptr;
HGLRC context = nullptr;
HMODULE libGL = nullptr;
}

namespace Platform {
bool CreateGLContext()
{
    window = Platform::Win32::GameWindow();
    if (!window) {
        LOG_ERROR("hwndMain is null");
        return false;
    }

    device = GetDC(window);
    if (!device) {
        LOG_ERROR("GetDC failed");
        return false;
    }

    PIXELFORMATDESCRIPTOR pfd = {
        sizeof(PIXELFORMATDESCRIPTOR),
        1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        PFD_TYPE_RGBA,
        32,
        0, 0, 0, 0, 0, 0,
        0,
        0,
        0,
        0, 0, 0, 0,
        24,
        8,
        0,
        PFD_MAIN_PLANE,
        0,
        0, 0, 0
    };

    int pixelFormat = ChoosePixelFormat(device, &pfd);
    if (!pixelFormat) {
        LOG_ERROR("Failed to choose pixel format");
        return false;
    }

    if (!SetPixelFormat(device, pixelFormat, &pfd)) {
        LOG_ERROR("Failed to set pixel format");
        return false;
    }

    HGLRC tempContext = wglCreateContext(device);
    if (!tempContext) {
        LOG_ERROR("Failed to create temporary context");
        return false;
    }

    if (!wglMakeCurrent(device, tempContext)) {
        LOG_ERROR("Failed to make temporary context current");
        wglDeleteContext(tempContext);
        return false;
    }

    PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB =
        (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");

    if (wglCreateContextAttribsARB) {
        int attribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
            WGL_CONTEXT_MINOR_VERSION_ARB, 3,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            0
        };

        context = wglCreateContextAttribsARB(device, 0, attribs);
        if (context) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(tempContext);
            wglMakeCurrent(device, context);
            LOG_INFO("OpenGL 3.3 Core Profile context created");
        } else {
            LOG_WARN("Failed to create 3.3 context; falling back to legacy");
            context = tempContext;
        }
    } else {
        LOG_WARN("wglCreateContextAttribsARB not found; using legacy context");
        context = tempContext;
    }

    if (!context) {
        LOG_ERROR("Failed to create any OpenGL context");
        return false;
    }

    libGL = LoadLibraryA("opengl32.dll");
    if (!libGL) {
        LOG_ERROR("Failed to load opengl32.dll");
        return false;
    }

    return true;
}

void DestroyGLContext()
{
    if (context) {
        if (wglGetCurrentContext() == context) {
            wglMakeCurrent(nullptr, nullptr);
        }
        wglDeleteContext(context);
        context = nullptr;
    }

    if (device && window) {
        ReleaseDC(window, device);
        device = nullptr;
    }

    if (libGL) {
        FreeLibrary(libGL);
        libGL = nullptr;
    }
}

void* GLProcAddress(const char* name)
{
    void* p = (void*)wglGetProcAddress(name);
    if (p == 0 || p == (void*)0x1 || p == (void*)0x2 || p == (void*)0x3 || p == (void*)-1) {
        p = (void*)GetProcAddress(libGL, name);
    }
    return p;
}

void SwapGLBuffers()
{
    if (device && window) SwapBuffers(device);
}
} // namespace Platform
