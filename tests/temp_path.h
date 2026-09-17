#pragma once
#include <chrono>
#include <filesystem>
#include <atomic>
#include <string>
inline std::string TestTempPath(const char* prefix)
{
    static std::atomic<unsigned> sequence{0};
    return (std::filesystem::temp_directory_path() / (std::string(prefix) + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(sequence++))).string();
}
