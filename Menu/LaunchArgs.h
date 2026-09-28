#pragma once

#include <sstream>
#include <string>

// The menu passes these values as separate command-line tokens to the hunt
// executable. Keep construction independent from the Win32 menu state so the
// launch contract can be tested without creating a menu window.
struct HuntLaunchRequest
{
    std::string projectName;
    std::string mapFile;
    int registration = 0;
    int dinoFlags = 0;
    int weaponFlags = 0;
    int timeOfDay = 0;
};

inline bool IsValidLaunchToken(const std::string& value)
{
    return !value.empty() &&
           value.find_first_of(" \t\r\n\"") == std::string::npos;
}

inline bool BuildHuntLaunchArguments(const HuntLaunchRequest& request,
                                     std::string& output)
{
    const std::string& launchName = request.mapFile.empty()
        ? request.projectName
        : request.mapFile;
    if (!IsValidLaunchToken(launchName) ||
        request.dinoFlags < 0 || request.dinoFlags > 1023 ||
        request.weaponFlags < 0 || request.weaponFlags > 1023 ||
        request.timeOfDay < 0 || request.timeOfDay > 2)
        return false;

    std::ostringstream params;
    params << " reg=" << request.registration;
    params << " prj=huntdat/areas/" << launchName;
    params << " din=" << request.dinoFlags;
    params << " wep=" << request.weaponFlags;
    params << " dtm=" << request.timeOfDay;
    output = params.str();
    return true;
}

// The menu builds the full command line by appending accessories, score mods,
// and the display-mode flag to the base arguments. Those appends must go into
// an empty stream that takes the base arguments as its first write: a
// stringstream constructed from an existing string leaves the put pointer at
// position 0, so later appends overwrite the "reg=/prj=/din=/wep=" prefix
// while .str() keeps the old length. The surviving tail then silently replaces
// the project argument and the game halts with "Error opening resource file
// .rsc". This factory owns that contract for both launch paths.
inline std::stringstream MakeLaunchParamStream(const std::string& launchArguments)
{
    std::stringstream params;
    params << launchArguments;
    return params;
}
