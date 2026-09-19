#include "MouseCapture.h"
#include "../Platform/Platform.h"

void ResetMousePos();

#ifndef _WIN32
namespace {
// Display recovery owns this transition. CaptureMouse owns enforcement so
// focus and menu paths cannot bypass it while rendering is suspended.
bool displayCaptureSuspended = false;
}
#endif

void CaptureMouse(std::int32_t capture)
{
#ifndef _WIN32
  if (capture && displayCaptureSuspended) return;
#endif
  Platform::SetMouseCapture(capture != false);
  if (capture) ResetMousePos();
}

#ifndef _WIN32
bool SuspendMouseCaptureForDisplay()
{
  if (displayCaptureSuspended) return false;
  displayCaptureSuspended = true;
  CaptureMouse(false);
  return true;
}

void ResumeMouseCaptureAfterDisplay(std::int32_t capture)
{
  if (!displayCaptureSuspended) return;
  displayCaptureSuspended = false;
  CaptureMouse(capture);
}
#endif
