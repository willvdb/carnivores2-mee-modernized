#include "Hunt.h"
#include <vector>
// Observe existing I/O calls, including failure cleanup, without production seams.
std::vector<HANDLE> MediaHandles;
DWORD MediaEndPosition=0;
static HANDLE MediaOpen(LPCSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES sa,
                        DWORD disposition,DWORD flags,HANDLE templ)
{
    HANDLE h=CreateFileA(path,access,share,sa,disposition,flags,templ);
    if(h!=INVALID_HANDLE_VALUE) MediaHandles.push_back(h);
    return h;
}
static BOOL MediaClose(HANDLE h)
{
    MediaEndPosition=SetFilePointer(h,0,nullptr,FILE_CURRENT);
    for(auto& tracked:MediaHandles) if(tracked==h) tracked=INVALID_HANDLE_VALUE;
    return CloseHandle(h);
}
#undef CreateFile
#define CreateFile MediaOpen
#define CloseHandle MediaClose
#include "../Hunt/Loaders/PictureLoader.cpp"
#include "../Hunt/Loaders/SoundLoader.cpp"
