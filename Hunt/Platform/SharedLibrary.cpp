#include "SharedLibrary.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace Platform {
SharedLibrary OpenSharedLibrary(const char* path) {
#ifdef _WIN32
    return LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}
void* SharedLibrarySymbol(SharedLibrary library, const char* name) {
    if (!library) return nullptr;
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
#else
    return dlsym(library, name);
#endif
}
void CloseSharedLibrary(SharedLibrary library) {
    if (!library) return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(library));
#else
    dlclose(library);
#endif
}
}
