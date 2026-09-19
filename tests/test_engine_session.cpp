#include <gtest/gtest.h>
#include "Hunt.h"
#include "Session/Session.h"
#include "Platform/System.h"
#include "Platform/Screenshot.h"
#include "Renderer/GLPerf.h"
#include "../Shared/LegacyProfile.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>

#ifdef _WIN32
#include "Network/NetworkManager.h"
NetworkManager g_Network;
#endif

void SessionTestCreateConfig();
void SessionTestLoadConfig();
void LoadTrophy2(int);
// Only graphics/audio services are doubled, never filesystem, profile, config,
// command-line, logging, screenshot or performance policy.
[[noreturn]] void DoHalt(const char* text) { EngineSession::Fail(); throw std::runtime_error(text); }
[[noreturn]] void DoHalt2(const char* text) { DoHalt(text); }
void SyncLegacyDisplayState() {}
bool Audio_SetEnvParam(int, int, float) { return false; }
void CopyHARDToDIB() {}
std::int32_t _HeapFree(Platform::HeapHandle heap, std::uint32_t flags, void* memory) {
    return Platform::FreeHeap(heap, flags, memory);
}

namespace {
namespace fs = std::filesystem;
using Inventory = std::map<std::string, std::string>;
std::string Read(const fs::path& p) {
    std::ifstream file(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
void Put(const fs::path& p, const std::string& bytes) { std::ofstream(p, std::ios::binary) << bytes; }
template<class T> void Put(const fs::path& p, const T& bytes) {
    std::ofstream stream(p, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
Inventory Scan(const fs::path& path) {
    Inventory result;
    for (const auto& entry : fs::recursive_directory_iterator(path)) {
        const auto key = entry.path().lexically_relative(path).generic_string();
        result[key] = entry.is_directory() ? "<directory>" : Read(entry.path());
    }
    return result;
}
struct Session : testing::Test {
    fs::path cwd, base, content, source, baseline, work, module;
    Inventory originals;
    std::vector<std::string> args, legacy;
    std::string error;
    void SetUp() override {
        cwd = fs::current_path();
        base = fs::canonical(fs::temp_directory_path()) / ("c2 session " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        content = base/"content"; source = base/"source"; baseline = base/"baseline";
        work = base/"workspace"; module = base/"engine";
        for (const auto& p : {content/"HuNtDaT/Areas", source, baseline, work/"state", work/"config", work/"output", module}) fs::create_directories(p);
        Put(content/"HuNtDaT/Areas/ArEa1.MAP", std::string("content sentinel"));
        Put(content/"config.cfg", std::string("fov 39\nresolution 901x601\n"));
        Put(content/"trophy00.sav", std::string("installed personal profile sentinel"));
        Put(content/"render.log", std::string("old source log"));
        Put(module/"config.cfg", std::string("fov 42\n"));
        LegacyProfile::Save save;
        save.profile.header.registration = 0;
        save.profile.header.score = 100;
        save.profile.items[0].type = 1;
        save.options.keys[0] = 123;
        const auto sav = LegacyProfile::EncodeSave(save);
        const auto sab = LegacyProfile::EncodeRoom(LegacyProfile::Room{});
        for (const auto& p : {source, baseline, work/"state"}) { Put(p/"trophy00.sav", sav); Put(p/"trophy00.sab", sab); }
        args = {"engine", "--session-contract=1", "--session-root=" + work.string(),
            "--session-source=" + source.string(), "--session-baseline=" + baseline.string(), "--session-slot=0"};
        originals = Scan(content);
        fs::current_path(content);
        StartLegacy();
    }
    void TearDown() override {
        LogClose();
        if (hlog && hlog != Platform::InvalidFile) { Platform::CloseFile(hlog); hlog = Platform::InvalidFile; }
        StartLegacy();
        EXPECT_EQ(Scan(content), originals);
        EXPECT_EQ(Read(module/"config.cfg"), "fov 42\n");
        fs::current_path(cwd);
        std::error_code ec; fs::remove_all(base, ec);
    }
    void StartLegacy() { EngineSession::Initialize({"engine"}, content.string(), module.string(), legacy, error); }
    EngineSession::Startup Start() { return EngineSession::Initialize(args, content.string(), module.string(), legacy, error); }
    void Logs() { LogInit("carnivor.log"); CreateLog(); }
    void CloseLogs() { CloseLog(); hlog = Platform::InvalidFile; LogClose(); }
};
TEST_F(Session, ValidPathsSpacesAndLayout) {
    ASSERT_EQ(Start(), EngineSession::Startup::Ready) << error;
    EXPECT_EQ(legacy, (std::vector<std::string>{"engine", "reg=0"}));
    EXPECT_TRUE(fs::is_empty(work/"output"));
    EXPECT_EQ(Scan(source), Scan(baseline));
    EXPECT_EQ(Scan(source), Scan(work/"state"));
}
TEST_F(Session, StrictArgumentsFailBeforeAnyOutput) {
    const auto good = args;
    for (const auto& bad : {"--session", "--session_root=x", "--session-root", "--session-root=", "--session-slot=00", "--session-slot=8",
            "--session-contract=2", "--Session-slot=0", "-session-slot=0", "/session-slot=0",
            "--session-unknown=x", "--session-capabilities=1", "--session-slot=+0"}) {
        args=good; args.push_back(bad);
        EXPECT_EQ(Start(), EngineSession::Startup::Error) << bad;
        EXPECT_EQ(Platform::OpenFile("render.log", Platform::FileMode::Write), Platform::InvalidFile);
        EXPECT_EQ(EngineSession::ExitStatus(0), 3);
        EXPECT_TRUE(fs::is_empty(work/"output"));
    }
    for (std::size_t i=1; i<good.size(); ++i) {
        args=good; args.erase(args.begin()+i);
        EXPECT_EQ(Start(), EngineSession::Startup::Error) << i;
    }
}
TEST_F(Session, InvalidValuesAreTestedWithoutDuplicateOptions) {
    const auto good=args;
    for (const auto& bad : {"--session-slot=00", "--session-slot=8", "--session-slot=+0", "--session-slot=0x0",
                          "--session-contract=2", "--session-contract=01", "--session-root", "--session-root="}) {
        args=good; const auto key=std::string(bad).substr(0,std::string(bad).find('='));
        for (auto& arg:args) if(arg.rfind(key+"=",0)==0) arg=bad;
        EXPECT_EQ(Start(),EngineSession::Startup::Error) << bad;
    }
}
TEST_F(Session, NonzeroSlotProductionRoundTrip) {
    LegacyProfile::Save save; save.profile.header.registration=7; save.profile.header.score=77;
    save.profile.items[0].type=1;
    for(const auto& dir:{source,baseline,work/"state"}) {
        fs::remove(dir/"trophy00.sav"); fs::rename(dir/"trophy00.sab",dir/"trophy07.sab");
        Put(dir/"trophy07.sav",LegacyProfile::EncodeSave(save));
    }
    const auto original=Scan(source);
    args.back()="--session-slot=7"; ASSERT_EQ(Start(),EngineSession::Startup::Ready) << error;
    Logs(); Platform::SetArguments(legacy); ProcessCommandLine(); LoadTrophy();
    EXPECT_EQ(TrophyRoom.RegNumber,7); EXPECT_EQ(TrophyRoom.Score,77);
    TrophyRoom.Score=99; SaveTrophy(); LoadTrophy(); EXPECT_EQ(TrophyRoom.Score,99);
    EXPECT_EQ(Scan(source),original); EXPECT_EQ(Scan(baseline),original); CloseLogs();
    EXPECT_EQ(EngineSession::ExitStatus(0),0);
}
TEST_F(Session, QueryMustBeStandaloneAndHasVersionedEvidence) {
    args={"engine", "--session-capabilities"};
    EXPECT_EQ(Start(), EngineSession::Startup::Query);
    EXPECT_NE(std::string(EngineSession::Capability).find("\"version\":1"), std::string::npos);
    args.push_back("-list-displays"); EXPECT_EQ(Start(), EngineSession::Startup::Error);
}
TEST_F(Session, SessionValuesCannotReachLegacySubstringParser) {
    fs::path renamed = base/"x=12 reg=4 -observ";
    fs::rename(work, renamed); work = renamed; args[2]="--session-root="+work.string();
    args[0]="/engine prj=bad reg=7 -observ";
    args.push_back("-res=1024x768"); args.push_back("-refresh=60000/1001");
    ASSERT_EQ(Start(), EngineSession::Startup::Ready) << error;
    Platform::SetArguments(legacy); PlayerX=0; ObservMode=false; TrophyRoom.RegNumber=7;
    ProcessCommandLine();
    EXPECT_EQ(TrophyRoom.RegNumber, 0); EXPECT_FALSE(ObservMode); EXPECT_EQ(PlayerX,0);
    EXPECT_EQ(DisplayConfiguration.size.width,1024); EXPECT_EQ(DisplayConfiguration.refresh.numerator,60000u);
}
TEST_F(Session, RejectLegacySlotAndProjectAmbiguity) {
    const auto good=args;
    for (const auto& bad : {"reg=0", "a-reg=0", "REG=0", "prj=../escape", "prj=/tmp/escape", "prj=\\escape", "prj=C:\\escape", "xxxprj=HUNTDAT", "prj=HUNTDAT/../escape", "-multiplayer", "-host"}) {
        args=good; args.push_back(bad); EXPECT_EQ(Start(), EngineSession::Startup::Error) << bad;
    }
}
TEST_F(Session, RejectUnsafeAndOverlappingRoots) {
    const auto good=args;
    for (const auto& path : {fs::path("relative"), work/".."/"workspace", content, content/"nested", source, baseline, module, base}) {
        if (path.is_absolute() && path != work/".."/"workspace") fs::create_directories(path);
        args=good; args[2]="--session-root="+path.string(); EXPECT_EQ(Start(),EngineSession::Startup::Error) << path;
    }
    fs::remove_all(content/"nested");
}
TEST_F(Session, MissingExtraTruncatedAndMismatchedBaselineFailClosed) {
    const auto before=Scan(work);
    for (const auto& directory : {source, baseline, work/"state"}) {
        for (const auto* name : {"trophy00.sav", "trophy00.sab"}) {
            const auto path=directory/name; const auto original=Read(path);
            for (const auto& corrupt : {std::string(), original.substr(0,100), original+"x"}) {
                Put(path,corrupt); EXPECT_EQ(Start(),EngineSession::Startup::Error); EXPECT_EQ(Read(path),corrupt);
            }
            fs::remove(path); EXPECT_EQ(Start(),EngineSession::Startup::Error); EXPECT_FALSE(fs::exists(path));
            Put(path,original);
        }
    }
    auto invalid=Read(source/"trophy00.sav"); invalid[128]=4;
    for (const auto& directory : {source, baseline, work/"state"}) Put(directory/"trophy00.sav",invalid);
    EXPECT_EQ(Start(),EngineSession::Startup::Error);
    EXPECT_TRUE(fs::is_empty(work/"output"));
}
TEST_F(Session, RejectHardlinkedStateOrOutput) {
    fs::remove(work/"state/trophy00.sav"); fs::create_hard_link(source/"trophy00.sav", work/"state/trophy00.sav");
    EXPECT_EQ(Start(),EngineSession::Startup::Error);
    fs::remove(work/"state/trophy00.sav"); Put(work/"state/trophy00.sav",Read(source/"trophy00.sav"));
    ASSERT_EQ(Start(),EngineSession::Startup::Ready);
    fs::create_hard_link(content/"render.log",work/"output/render.log");
    EXPECT_EQ(Platform::OpenFile("render.log",Platform::FileMode::Write),Platform::InvalidFile);
    fs::remove(work/"output/render.log");
}
#ifndef _WIN32
TEST_F(Session, RejectSymlinkedRootsParentsLeavesAndDanglingTargets) {
    const auto good=args;
    fs::create_directory_symlink(work,base/"alias"); args[2]="--session-root="+(base/"alias").string();
    EXPECT_EQ(Start(),EngineSession::Startup::Error);
    args=good; fs::rename(work/"state",work/"saved"); fs::create_directory_symlink(work/"saved",work/"state");
    EXPECT_EQ(Start(),EngineSession::Startup::Error);
    fs::remove(work/"state"); fs::rename(work/"saved",work/"state");
    ASSERT_EQ(Start(),EngineSession::Startup::Ready);
    fs::create_symlink(content/"render.log",work/"output/render.log");
    EXPECT_EQ(Platform::OpenFile("render.log",Platform::FileMode::Write),Platform::InvalidFile);
    fs::create_symlink(base/"missing",work/"output/new.log");
    EXPECT_EQ(Platform::OpenFile("new.log",Platform::FileMode::Write),Platform::InvalidFile);
    fs::remove(work/"output/render.log"); fs::remove(work/"output/new.log");
}
#endif
TEST_F(Session, ContentKeepsCaseAndSeparatorCompatibility) {
    args.push_back("prj=huntdat\\areas\\area1");
    ASSERT_EQ(Start(),EngineSession::Startup::Ready);
    auto file=Platform::OpenFile("huntdat\\areas/AREA1.map",Platform::FileMode::Read);
    ASSERT_NE(file,Platform::InvalidFile); EXPECT_EQ(Platform::FileSize(file),16); EXPECT_TRUE(Platform::CloseFile(file));
}
TEST_F(Session, ProductionProfileReadsAndWritesOnlyWorkspace) {
    const auto original=Scan(source), frozen=Scan(baseline);
    ASSERT_EQ(Start(),EngineSession::Startup::Ready); Logs();
    TrophyRoom.RegNumber=0; LoadTrophy(); EXPECT_EQ(TrophyRoom.Score,100); EXPECT_EQ(KeyMap.fkForward,123);
    TrophyRoom.Score=678; SaveTrophy(); CloseLogs();
    LegacyProfile::Save saved; const auto bytes=Read(work/"state/trophy00.sav");
    ASSERT_TRUE(LegacyProfile::DecodeSave(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size(),saved));
    EXPECT_EQ(saved.profile.header.score,678); EXPECT_EQ(saved.options.keys[0],123);
    EXPECT_EQ(Scan(source),original); EXPECT_EQ(Scan(baseline),frozen); EXPECT_EQ(EngineSession::ExitStatus(0),0);
}
TEST_F(Session, ProductionLoadRejectsLaterMissingAndMalformedStateWithoutFallback) {
    ASSERT_EQ(Start(),EngineSession::Startup::Ready); Logs();
    fs::remove(work/"state/trophy00.sav"); TrophyRoom.RegNumber=0;
    EXPECT_THROW(LoadTrophy(),std::runtime_error); SaveTrophy();
    EXPECT_FALSE(fs::exists(work/"state/trophy00.sav")); EXPECT_NE(EngineSession::ExitStatus(0),0); CloseLogs();
}
TEST_F(Session, ProductionLoadRejectsWrongRegistrationAndOversizedRoom) {
    ASSERT_EQ(Start(),EngineSession::Startup::Ready); Logs();
    auto bytes=Read(work/"state/trophy00.sav"); bytes[128]=7; Put(work/"state/trophy00.sav",bytes);
    TrophyRoom.RegNumber=0; EXPECT_THROW(LoadTrophy(),std::runtime_error);
    Put(work/"state/trophy00.sab",Read(work/"state/trophy00.sab")+"x");
    EXPECT_THROW(LoadTrophy2(0),std::runtime_error); CloseLogs();
}
TEST_F(Session, ConfigIsLocalAndCliStillWins) {
    const auto exeConfig=fs::path(Platform::ModuleDirectory())/"config.cfg";
    const bool existed=fs::exists(exeConfig); const auto original=existed?Read(exeConfig):std::string();
    Put(work/"config/config.cfg",std::string("fov 65\nresolution 900x700\ndisplay_mode 0\n"));
    ASSERT_EQ(Start(),EngineSession::Startup::Ready); Logs();
    SessionTestCreateConfig(); SessionTestLoadConfig();
    EXPECT_EQ(OptFov,65); EXPECT_EQ(DisplayConfiguration.size.width,900);
    Platform::SetArguments(std::vector<std::string>{"engine","-res=800x600","-borderless"}); ProcessCommandLine();
    EXPECT_EQ(DisplayConfiguration.size.width,800); EXPECT_EQ(DisplayConfiguration.mode,Platform::WindowMode::Borderless);
    EXPECT_EQ(fs::exists(exeConfig),existed); if(existed) EXPECT_EQ(Read(exeConfig),original);
    CloseLogs();
}
TEST_F(Session, DefaultConfigCannotBeCreatedBesideExecutable) {
    const auto exeConfig=fs::path(Platform::ModuleDirectory())/"config.cfg";
    const bool existed=fs::exists(exeConfig); const auto original=existed?Read(exeConfig):std::string();
    ASSERT_EQ(Start(),EngineSession::Startup::Ready); Logs(); SessionTestCreateConfig(); SessionTestLoadConfig();
    EXPECT_TRUE(fs::is_regular_file(work/"config/config.cfg"));
    EXPECT_EQ(fs::exists(exeConfig),existed); if(existed) EXPECT_EQ(Read(exeConfig),original);
    CloseLogs();
}
TEST_F(Session, ProductionLogsScreenshotAndDebugExportAreSeparateFromState) {
    ASSERT_EQ(Start(),EngineSession::Startup::Ready); Logs(); PrintLog("debug dump sentinel\n");
    const auto original=Scan(work/"state");
    std::uint16_t pixels[4]={0,31,1024,32767}; WinW=2; WinH=2; VideoPitch=2; lpVideoBuf=pixels; _shotcounter=0;
#ifndef _WIN32
    SaveScreenShot(); // Real resource call site -> real BMP writer.
#else
    // The legacy Windows call site needs a GDI DIB; exercise its shared output API.
    EXPECT_TRUE(Platform::SaveBitmap555("HUNT0001.BMP",pixels,2,2,2));
#endif
    auto dump=Platform::OpenFile("debug-export.bmp",Platform::FileMode::Write);
    ASSERT_NE(dump,Platform::InvalidFile); std::uint32_t count=0;
    EXPECT_TRUE(Platform::WriteFile(dump,pixels,sizeof(pixels),&count)); EXPECT_TRUE(Platform::CloseFile(dump));
    CloseLogs(); lpVideoBuf=nullptr;
    EXPECT_TRUE(fs::exists(work/"output/HUNT0001.BMP"));
    EXPECT_NE(Read(work/"output/render.log").find("debug dump sentinel"),std::string::npos);
    EXPECT_NE(Read(work/"output/carnivor.log").find("Log started"),std::string::npos);
    EXPECT_EQ(Scan(work/"state"),original); EXPECT_EQ(EngineSession::ExitStatus(0),0);
}
TEST_F(Session, CompiledPerformanceHooksStayDisabledEvenWhenRequested) {
    ASSERT_EQ(Start(),EngineSession::Startup::Ready);
    g_glperfLoggingEnabled=true; glperf_set_logging(true); glperf_init(); glperf_trigger_capture();
    EXPECT_FALSE(glperf_is_active()); glperf_shutdown(); EXPECT_TRUE(fs::is_empty(work/"output"));
    g_glperfLoggingEnabled=false;
}
TEST_F(Session, DenyEscapesUnselectedProfilesAndUnsafeOutputs) {
    ASSERT_EQ(Start(),EngineSession::Startup::Ready);
    for (const auto& name : {"../escape", "output/../escape", "trophy01.sav", "C:\\escape", "config.cfg/../escape"})
        EXPECT_EQ(Platform::OpenFile(name,Platform::FileMode::Write),Platform::InvalidFile) << name;
    EXPECT_EQ(Platform::OpenFile((content/"trophy00.sav").string().c_str(),Platform::FileMode::Read),Platform::InvalidFile);
    EXPECT_EQ(Platform::OpenFile((module/"config.cfg").string().c_str(),Platform::FileMode::Write),Platform::InvalidFile);
    EXPECT_NE(EngineSession::ExitStatus(0),0);
}
TEST_F(Session, FailedSaveAndOutputCannotBecomeCleanExit) {
    ASSERT_EQ(Start(),EngineSession::Startup::Ready); Logs(); TrophyRoom.RegNumber=0; LoadTrophy();
    fs::remove(work/"state/trophy00.sab"); fs::create_directory(work/"state/trophy00.sab");
    SaveTrophy(); EXPECT_NE(EngineSession::ExitStatus(0),0); CloseLogs();
}
TEST_F(Session, FailedConfigCreationDoesNotFallBack) {
    ASSERT_EQ(Start(),EngineSession::Startup::Ready); Logs();
    fs::remove(work/"config"); EXPECT_THROW(SessionTestCreateConfig(),std::runtime_error); EXPECT_NE(EngineSession::ExitStatus(0),0);
    EXPECT_THROW(SessionTestLoadConfig(),std::runtime_error); CloseLogs();
}
#ifndef _WIN32
TEST_F(Session, BufferedAndHandleWriteErrorsAreLatched) {
    ASSERT_EQ(Start(),EngineSession::Startup::Ready);
    FILE* full=std::fopen("/dev/full","w"); ASSERT_NE(full,nullptr);
    std::fputs("buffered output",full); EXPECT_FALSE(Platform::CloseTextFile(full));
    EXPECT_NE(EngineSession::ExitStatus(0),0);
    full=std::fopen("/dev/full","w"); ASSERT_NE(full,nullptr); std::setvbuf(full,nullptr,_IONBF,0);
    std::uint32_t written=0; EXPECT_FALSE(Platform::WriteFile(full,"x",1,&written)); Platform::CloseFile(full);
}
#endif
TEST_F(Session, AbsentModePreservesLegacyRoutingAndArguments) {
    args={"engine","reg=4","prj=HUNTDAT/session-area","-windowed"};
    EXPECT_EQ(Start(),EngineSession::Startup::Legacy); EXPECT_EQ(legacy,args);
    std::string resolved; EXPECT_TRUE(EngineSession::Resolve("../legacy-output",true,resolved)); EXPECT_EQ(resolved,"../legacy-output");
    auto file=Platform::OpenFile((base/"legacy.log").string().c_str(),Platform::FileMode::Write);
    ASSERT_NE(file,Platform::InvalidFile); EXPECT_TRUE(Platform::CloseFile(file)); EXPECT_TRUE(fs::exists(base/"legacy.log"));
    EngineSession::Check(false); EXPECT_EQ(EngineSession::ExitStatus(0),0);
}
}
