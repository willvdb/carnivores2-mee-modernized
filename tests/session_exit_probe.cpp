// Execute production termination functions in an owned subprocess, without GL.
#include "Hunt.h"
#include "Session/Session.h"
#include "Platform/Platform.h"
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
std::int32_t Multiplayer = 0, Host = 0;
bool g_VerboseLogging = false;
Platform::FileHandle hlog = Platform::InvalidFile;
#ifdef _WIN32
HWND hwndMain = nullptr;
#endif
void AudioStop() {}
void Audio_Shutdown() {}
void ShutDown3DHardware() {}
void ShutDownServer() {}
void ShutDownClient() {}
namespace Platform {
void ShutdownApplication() {}
void ShowMessage(const char*, const char*) {}
}
#include "session_logs.inc"
#include "session_exit.inc"
int main(int argc, char** argv) {
    std::vector<std::string> args(argv, argv+argc), legacy;
    std::string error;
    if (EngineSession::Initialize(args, std::filesystem::current_path().string(),
            Platform::ModuleDirectory(), legacy, error) != EngineSession::Startup::Ready) return 2;
    LogInit("carnivor.log"); CreateLog();
    // std::exit would run unsafe global destruction during allocation failure.
    // A regression to that path is observable, without manufacturing a deadlock.
    std::atexit([] { std::_Exit(99); });
    const auto mode = args.back();
    if (mode == "io") EngineSession::Fail();
    if (mode == "early") DoHalt2("startup failure");
    DoHalt(mode == "fatal" ? "fatal failure" : "");
}
