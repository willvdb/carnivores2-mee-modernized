#include "System.h"
#include <ctime>
namespace Platform {
namespace { std::vector<std::string> arguments; }
void SetArguments(int argc, char** argv) { if (argc > 0 && argv) arguments.assign(argv, argv + argc); else arguments.clear(); }
void SetArguments(const std::vector<std::string>& values) { arguments = values; }
const std::vector<std::string>& Arguments() { return arguments; }
CalendarTime LocalTime()
{
    const auto now = std::time(nullptr);
    std::tm value{};
#ifdef _WIN32
    localtime_s(&value, &now);
#else
    localtime_r(&now, &value);
#endif
    return {value.tm_year + 1900, value.tm_mon + 1, value.tm_mday, value.tm_hour, value.tm_min};
}
}
