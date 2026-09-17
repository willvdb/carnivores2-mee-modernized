// Compile the actual production loader, observing its existing close boundary.
// The spy records positions while handles are valid; it always calls Win32 close.
#include "Hunt.h"
BOOL ResourceTestCloseHandle(HANDLE file);
#define CloseHandle ResourceTestCloseHandle
#include "../Hunt/Loaders/Resources.cpp"
#undef CloseHandle
