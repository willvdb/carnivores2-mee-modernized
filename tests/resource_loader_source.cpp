// Compile the actual production loader, observing its existing close boundary.
// The spy records positions while handles are valid; it always calls Win32 close.
#include "Hunt.h"
namespace Platform { bool ResourceTestCloseFile(FileHandle file); }
#define CloseFile ResourceTestCloseFile
#include "../Hunt/Loaders/Resources.cpp"
#undef CloseFile
