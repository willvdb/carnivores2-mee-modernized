// Asset-free native process fixture, never a game/Genesis certification binary.
// Uses the engine's actual v1 path policy and file I/O, not its graphics/gameplay.
#include "../../Hunt/Session/Session.h"
#include "../../Hunt/Platform/Files.h"
#include "../../Shared/LegacyProfile.h"
#include <filesystem>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <cstdio>
#include <string>
#include <vector>
int main(int argc, char** argv) {
    std::vector<std::string> args(argv, argv+argc), legacy;
    std::string error;
    const auto result = EngineSession::Initialize(args, std::filesystem::current_path().string(),
        Platform::ModuleDirectory(), legacy, error);
    const auto* behavior = std::getenv("C2_NATIVE_FIXTURE_BEHAVIOR");
    const std::string scenario = behavior ? behavior : "";
    if (result == EngineSession::Startup::Query) {
        std::puts(scenario == "bad-contract" ? "{\"version\":99}" : EngineSession::Capability); return 0;
    }
    if (result != EngineSession::Startup::Ready) { std::fprintf(stderr,"%s\n",error.c_str()); return 2; }
    if (scenario == "hang") std::this_thread::sleep_for(std::chrono::seconds(60));
    if (scenario == "nonzero") return 7;
    const auto name = "trophy0" + std::to_string(EngineSession::Slot()) + ".sav";
    auto file = Platform::OpenFile(name.c_str(),Platform::FileMode::Read);
    if(file == Platform::InvalidFile) return 3;
    LegacyProfile::SaveBytes bytes{}; std::uint32_t count=0;
    const bool read=Platform::ReadFile(file,bytes.data(),bytes.size(),&count);
    Platform::CloseFile(file);
    if(!read || !EngineSession::ValidateProfile(bytes.data(),count,false)) return 3;
    file=Platform::OpenFile(name.c_str(),Platform::FileMode::Write);
    Platform::WriteFile(file,bytes.data(),bytes.size(),&count); Platform::CloseFile(file);
    file=Platform::OpenFile("render.log",Platform::FileMode::Write);
    const std::string text="asset-free native process fixture; not Genesis\n";
    Platform::WriteFile(file,text.data(),text.size(),&count); Platform::CloseFile(file);
    std::puts("Native fixture completed using production session I/O.");
    return EngineSession::ExitStatus(0);
}
