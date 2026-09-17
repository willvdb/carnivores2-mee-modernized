#pragma once
#include <cstdint>
namespace Platform {
bool SaveBitmap555(const char* path, const std::uint16_t* pixels, int width, int height, int pitch);
}
