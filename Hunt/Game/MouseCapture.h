#pragma once

#include <cstdint>

void CaptureMouse(std::int32_t capture);

#ifndef _WIN32
bool SuspendMouseCaptureForDisplay();
void ResumeMouseCaptureAfterDisplay(std::int32_t capture);
#endif
