#pragma once
#include <vector>
#include <cstdint>
#include <gtest/gtest.h>
#include <windows.h>
struct MediaFile {
    char path[MAX_PATH]{};
    explicit MediaFile(const std::vector<std::uint8_t>& b) {
        char dir[MAX_PATH];GetTempPathA(MAX_PATH,dir);GetTempFileNameA(dir,"med",0,path);
        HANDLE f=CreateFileA(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
        DWORD n=0;EXPECT_TRUE(WriteFile(f,b.data(),static_cast<DWORD>(b.size()),&n,nullptr));
        EXPECT_EQ(n,b.size());CloseHandle(f);
    }
    ~MediaFile() { DeleteFileA(path); }
};
