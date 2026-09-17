#pragma once
#include <vector>
#include "temp_path.h"
#include <cstring>
#include <cstdint>
#include <gtest/gtest.h>
#include "../Hunt/Platform/Files.h"
struct MediaFile {
    char path[4096]{};
    explicit MediaFile(const std::vector<std::uint8_t>& b) {
        std::strcpy(path, TestTempPath("med").c_str());
        Platform::FileHandle f=Platform::OpenFile(path, Platform::FileMode::Write);
        std::uint32_t n=0;EXPECT_TRUE(Platform::WriteFile(f, b.data(), static_cast<std::uint32_t>(b.size()), &n));
        EXPECT_EQ(n,b.size());Platform::CloseFile(f);
    }
    ~MediaFile() { std::remove(path); }
};
