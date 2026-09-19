#pragma once
#include "PlatformSDLInternal.h"
namespace Platform::LinuxIdentity {
void DiscoverX11(DisplayCatalog&, const SDL_DisplayID*, int);
void ShutdownWaylandDiscovery();
void DiscoverWayland(DisplayCatalog&, const SDL_DisplayID*, int);
}
