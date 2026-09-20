#pragma once
#include <string>
#include <vector>

// Opt-in engine-owned I/O policy, not an OS sandbox. Call before platform/log init.
namespace EngineSession {
enum class Startup { Legacy, Ready, Query, Error };
inline constexpr const char* Capability =
    "{\"contract\":\"c2-engine-session\",\"version\":1,\"state\":\"sav-sab-pair\","
    "\"layout\":\"state-config-output-v1\",\"performance_capture\":false}";
Startup Initialize(const std::vector<std::string>& arguments,
                   const std::string& contentDirectory, const std::string& moduleDirectory,
                   std::vector<std::string>& legacyArguments, std::string& error);
bool Active();
int Slot();
std::string ConfigPath();
// Resolve only engine-owned I/O. Content reads retain the legacy resolver.
bool Resolve(const std::string& path, bool write, std::string& resolved);
void Fail();
bool Check(bool success); // latch any session I/O failure, including close/flush
int ExitStatus(int status);
// Production trophy readers call this before mutating runtime state.
bool ValidateProfile(const void* bytes, std::size_t size, bool room);
}
