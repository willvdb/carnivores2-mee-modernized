#pragma once
#include <vector>
#include <string>
namespace Platform {
void SetArguments(int argc, char** argv);
void SetArguments(const std::vector<std::string>& values);
const std::vector<std::string>& Arguments();
struct CalendarTime { int year, month, day, hour, minute; };
CalendarTime LocalTime();
}
