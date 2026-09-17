#include "Hunt.h"
#include <vector>
// Observe existing I/O calls, including failure cleanup, without production seams.
std::vector<HANDLE> MediaHandles;
DWORD MediaEndPosition=0;
namespace Platform {
static FileHandle MediaOpen(const char* path, FileMode mode, bool share = true)
{
    FileHandle h=OpenFile(path,mode,share);
    if(h!=InvalidFile) MediaHandles.push_back(h);
    return h;
}
static bool MediaClose(HANDLE h)
{
    MediaEndPosition=SetFilePointer(h,0,nullptr,FILE_CURRENT);
    for(auto& tracked:MediaHandles) if(tracked==h) tracked=INVALID_HANDLE_VALUE;
    return CloseHandle(h);
}
}
#define OpenFile MediaOpen
#define CloseFile MediaClose
#include "../Hunt/Loaders/PictureLoader.cpp"
#include "../Hunt/Loaders/SoundLoader.cpp"
