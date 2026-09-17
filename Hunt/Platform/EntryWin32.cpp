#include "System.h"
#include <cstdlib>
#include "PlatformWin32.h"

int RunGame();
LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
int PASCAL WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
    Platform::Win32::Initialize(instance, MainWndProc);
    Platform::SetArguments(__argc, __argv);
    return RunGame();
}
