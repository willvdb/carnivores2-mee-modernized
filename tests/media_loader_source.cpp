#include "Hunt.h"
#include "temp_path.h"
#include <vector>
// Observe existing I/O calls, including failure cleanup, without production seams.
std::vector<Platform::FileHandle> MediaHandles;
std::uint32_t MediaEndPosition=0;
namespace Platform {
static FileHandle MediaOpen(const char* path, FileMode mode, bool share = true)
{
    FileHandle h=OpenFile(path,mode,share);
    if(h!=InvalidFile) MediaHandles.push_back(h);
    return h;
}
static bool MediaClose(Platform::FileHandle h)
{
    MediaEndPosition=Platform::SeekFile(h, 0, Platform::SeekOrigin::Current);
    for(auto& tracked:MediaHandles) if(tracked==h) tracked=Platform::InvalidFile;
    return Platform::CloseFile(h);
}
}
#define OpenFile MediaOpen
#define CloseFile MediaClose
#include "../Hunt/Loaders/PictureLoader.cpp"
#include "../Hunt/Loaders/SoundLoader.cpp"
