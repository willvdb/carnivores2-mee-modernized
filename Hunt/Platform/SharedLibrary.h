#pragma once
namespace Platform {
using SharedLibrary = void*;
SharedLibrary OpenSharedLibrary(const char* path);
void* SharedLibrarySymbol(SharedLibrary library, const char* name);
void CloseSharedLibrary(SharedLibrary library);
}
