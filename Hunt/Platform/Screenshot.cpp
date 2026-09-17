#include "Screenshot.h"
#include "Files.h"
#include <array>
#include <vector>
#include <limits>
namespace Platform {
bool SaveBitmap555(const char* path, const std::uint16_t* pixels, int width, int height, int pitch)
{
    if (!pixels || width <= 0 || height <= 0 || pitch < width) return false;
    const std::uint64_t stride = (std::uint64_t(width)*3+3)&~std::uint64_t(3);
    const auto imageSize = stride*height;
    if (imageSize > UINT32_MAX-54) return false;
    std::array<std::uint8_t,54> header{};
    auto put = [&](int offset, std::uint32_t value, int size) {
        for (int i=0;i<size;++i) header[offset+i] = static_cast<std::uint8_t>(value >> (8*i));
    };
    put(0,0x4d42,2); put(2,54+imageSize,4); put(10,54,4); put(14,40,4);
    put(18,width,4); put(22,height,4); put(26,1,2); put(28,24,2); put(34,imageSize,4);
    auto file = OpenFile(path, FileMode::Write, false);
    if (file == InvalidFile) return false;
    std::uint32_t count = 0;
    bool ok = WriteFile(file,header.data(),header.size(),&count) && count == header.size();
    std::vector<std::uint8_t> row(stride);
    for (int y=height-1;ok && y>=0;--y) {
        for(int x=0;x<width;++x) {
            const auto color = pixels[std::size_t(y)*pitch+x];
            row[3*x] = (color&31)<<3;
            row[3*x+1] = ((color>>5)&31)<<3;
            row[3*x+2] = ((color>>10)&31)<<3;
        }
        ok = WriteFile(file,row.data(),row.size(),&count) && count == row.size();
    }
    return CloseFile(file) && ok;
}
}
