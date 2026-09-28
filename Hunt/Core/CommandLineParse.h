#pragma once

#include "Strings.h"
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

// Pure command-line rules shared by the engine and its unit tests. The game
// receives menu arguments as separate argv entries, so an option must begin at
// offset zero; substring matches could copy from the wrong offset or accept an
// unrelated argument.
enum class CommandLineCopyResult {
    NotPresent,
    Copied,
    Invalid,
};

inline bool CommandLineOptionValue(const char* argument, const char* option,
                                   const char** value)
{
    if (!argument || !option || !value || *option == '\0')
        return false;

    const size_t optionLength = strlen(option);
    if (LegacyText::Compare(argument, option, optionLength) != 0)
        return false;

    *value = argument + optionLength;
    return true;
}

inline CommandLineCopyResult CopyCommandLineOption(char* destination,
                                                    size_t destinationCapacity,
                                                    const char* argument,
                                                    const char* option)
{
    const char* value = nullptr;
    if (!CommandLineOptionValue(argument, option, &value))
        return CommandLineCopyResult::NotPresent;
    if (!destination || destinationCapacity == 0 || !value || *value == '\0')
        return CommandLineCopyResult::Invalid;

    const size_t length = strlen(value);
    if (length >= destinationCapacity)
        return CommandLineCopyResult::Invalid;

    memcpy(destination, value, length + 1);
    return CommandLineCopyResult::Copied;
}

inline bool ParseCommandLineInt(const char* text, int& out)
{
    if (!text || *text == '\0')
        return false;

    char* end = nullptr;
    errno = 0;
    const long value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE)
        return false;
    if (value < static_cast<long>((std::numeric_limits<int>::min)()) ||
        value > static_cast<long>((std::numeric_limits<int>::max)()))
        return false;

    out = static_cast<int>(value);
    return true;
}

inline bool ParseCommandLineFloat(const char* text, float& out)
{
    if (!text || *text == '\0')
        return false;

    char* end = nullptr;
    errno = 0;
    const float value = strtof(text, &end);
    if (end == text || *end != '\0' || errno == ERANGE || !std::isfinite(value))
        return false;

    out = value;
    return true;
}

inline bool ParseCommandLineResolution(const char* text, int& width, int& height)
{
    if (!text || *text == '\0')
        return false;

    char* end = nullptr;
    errno = 0;
    const long parsedWidth = strtol(text, &end, 10);
    if (end == text || errno == ERANGE || (*end != 'x' && *end != 'X'))
        return false;

    const char* heightText = end + 1;
    errno = 0;
    const long parsedHeight = strtol(heightText, &end, 10);
    if (end == heightText || *end != '\0' || errno == ERANGE)
        return false;

    if (parsedWidth <= 0 || parsedHeight <= 0 ||
        parsedWidth > static_cast<long>((std::numeric_limits<int>::max)()) ||
        parsedHeight > static_cast<long>((std::numeric_limits<int>::max)()))
        return false;

    width = static_cast<int>(parsedWidth);
    height = static_cast<int>(parsedHeight);
    return true;
}
