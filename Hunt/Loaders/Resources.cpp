#include "Hunt.h"
#include "Platform/Platform.h"
#include "LoadValidate.h"
#include "ResourceIO.h"
#include "MapIO.h"
#include "stdio.h"
#include "timeapi.h"

// Corrupt/modded-.RSC fail-fast. Shipped areas scan clean (audit); anything
// rejected here previously overflowed fixed arrays (MObjects[256],
// Ambient[256], WaterList[256], ...) or drove unbounded vector assigns.
static void RscLoadFail(const char* what, int value, int limit)
{
  char sz[256];
  sprintf_s(sz, sizeof(sz),
            "Resource loading error: %s (value=%d, limit=%d). File is corrupt or modded.",
            what, value, limit);
  DoHalt(sz);
}

static void RequireMapRead(bool success, const char* what)
{
  if (!success)
  {
    char sz[256];
    sprintf_s(sz, sizeof(sz), "Resource loading error: truncated %s.", what);
    DoHalt(sz);
  }
}

template<class T>
static void ReadRscValue(HANDLE file, T& out, const char* what)
{
  if (!EngineResource::Read(file, out))
  {
    char sz[256];
    sprintf_s(sz, sizeof(sz), "Resource loading error: truncated %s.", what);
    DoHalt(sz);
  }
}

static void MapLoadFail(const char* what, int x, int y, int value, int limit)
{
  char sz[256];
  sprintf_s(sz, sizeof(sz),
            "Map loading error: %s at (%d,%d) (value=%d, limit=%d).",
            what, x, y, value, limit);
  DoHalt(sz);
}

static void ValidateMapReferences(int textureCount, int modelCount, int waterCount)
{
  int landingCount = 0;
  constexpr int landingCapacity =
      static_cast<int>(sizeof(LandingList.list) / sizeof(LandingList.list[0]));

  for (int y = 0; y < ctMapSize; y++)
    for (int x = 0; x < ctMapSize; x++)
    {
      if (!IsValidMapTextureIndex(TMap1[y][x], textureCount))
        MapLoadFail("primary texture index out of range", x, y, TMap1[y][x], textureCount);
      if (!IsValidMapTextureIndex(TMap2[y][x], textureCount))
        MapLoadFail("secondary texture index out of range", x, y, TMap2[y][x], textureCount);
      if (!IsValidMapObjectIndex(OMap[y][x], modelCount))
        MapLoadFail("object index out of range", x, y, OMap[y][x], modelCount);

      if (OMap[y][x] == 254 && ++landingCount > landingCapacity)
        MapLoadFail("landing marker capacity exceeded", x, y, landingCount, landingCapacity);

      if ((FMap[y][x] & fmWaterA) &&
          !IsValidMapWaterIndex(WMap[y][x], waterCount))
        MapLoadFail("water index out of range", x, y, WMap[y][x], waterCount);
    }
}

// Forward declarations for functions in ModelLoader.cpp
void LoadBMPModel(TObject &obj);
void LoadAnimation(TVTL &vtl, int modelVertexCount);
void GenerateMapImage();

#ifdef MEM_DEBUG
#include <mutex>
#endif

#ifdef MEM_DEBUG
std::mutex g_AllocMutex;
std::map<void*, AllocationInfo>* g_Allocations = nullptr;
#endif

void GenerateModelMipMaps(TModel *mptr, MemoryTag tag);
void GenerateAlphaFlags(TModel *mptr);

// Phase 5A: the 3-arg _HeapAlloc is preserved as a one-line forwarder to
// the new 4-arg overload that takes a MemoryTag. Call sites can be migrated
// incrementally (per phase 5B-5E) by switching to the explicit 4-arg form
// with the appropriate tag.
//
// The default tag is MemoryTag::Global (heap allocation), NOT
// MemoryTag::Level. This is a safety fix from the Phase 5C.2 testing:
// with the arena wired in, any 3-arg call that lands in the arena gets
// its memory reclaimed by LevelArena->Reset() the next time a level
// loads. The visible symptoms were a garbled exit menu (ExitPic's
// pixel data was arena-owned and got freed between level loads and
// the Escape press) and a broken gun envmap in OpenGL (the envmap
// data is loaded once at startup and used every frame; if it's
// arena-owned, the first LevelArena->Reset() corrupts it). Changing
// the default to Global means unmigrated call sites go to the heap
// (safe, survive arena reset) and only the explicitly-tagged Level
// allocations land in the arena. The trade-off is that the arena is
// underutilized until the remaining 3-arg per-level call sites are
// audited in Phase 5E; that's a missed optimization, not a
// correctness issue.
// --------------------------------------------------------------------------
// AllocDispatch: shared allocation logic extracted from the 4-arg overload
// so that MEM_DEBUG builds can record every allocation without duplicating
// the arena-vs-heap dispatch.
// --------------------------------------------------------------------------
static LPVOID AllocDispatch(HANDLE hHeap,
                            DWORD dwFlags,
                            size_t bytes,
                            MemoryTag tag)
{
  LPVOID res = nullptr;

  if (tag == MemoryTag::Level && LevelArena != nullptr)
  {
    res = LevelArena->Allocate(bytes);
    if (res)
      memset(res, 0, bytes);
  }
  else
  {
    res = HeapAlloc(hHeap,
                    dwFlags | HEAP_ZERO_MEMORY,
                    bytes);
  }

  if (!res)
    DoHalt("Memory allocation error!");

  HeapAllocated += bytes;
  return res;
}


#ifdef MEM_DEBUG
// The Memory.h tracking macro reroutes _HeapAlloc call syntax to
// _HeapAllocImpl. It must not rewrite the function definitions below,
// so park it here and restore it afterwards; every other TU keeps
// automatic __FILE__/__LINE__ capture.
#pragma push_macro("_HeapAlloc")
#undef _HeapAlloc
#endif

// 3-arg _HeapAlloc: forwards to the 4-arg overload with MemoryTag::Global.
// This is the safe default — untagged allocations land on the persistent
// heap where LevelArena->Reset() cannot invalidate them.
LPVOID _HeapAlloc(HANDLE hHeap,
                  DWORD dwFlags,
                  size_t bytes)
{
  return _HeapAlloc(hHeap, dwFlags, bytes, MemoryTag::Global);
}


// 4-arg _HeapAlloc: dispatches between arena and heap based on `tag`.
//   tag == MemoryTag::Level AND LevelArena != nullptr → allocate from
//     the per-level arena. The arena does not zero-initialize, so the
//     returned block is explicitly memset to 0 (matches C1's behavior;
//     the heap path gets HEAP_ZERO_MEMORY for the same effect).
//   otherwise → HeapAlloc on the game heap with HEAP_ZERO_MEMORY.
//
// HeapAllocated is incremented for ALL allocations (arena + heap),
// matching C1. The name is a misnomer — it's really "total bytes
// allocated" — but the counter feeds carnivor.log lines that downstream
// tooling may parse, so the accounting is preserved verbatim.
LPVOID _HeapAlloc(HANDLE hHeap,
                  DWORD dwFlags,
                  size_t bytes,
                  MemoryTag tag)
{
#ifndef MEM_DEBUG
  return AllocDispatch(hHeap, dwFlags, bytes, tag);
#else
  // In MEM_DEBUG builds, every allocation is recorded so the leak report
  // has complete coverage — even call sites that don't use _AllocTrack.
  std::lock_guard<std::mutex> lock(g_AllocMutex);
  if (!g_Allocations) g_Allocations = new std::map<void*, AllocationInfo>();
  LPVOID res = AllocDispatch(hHeap, dwFlags, bytes, tag);
  (*g_Allocations)[res] = { bytes, tag,
                            "unknown", 0 };
  return res;
#endif
}

#ifdef MEM_DEBUG
// 5-arg _HeapAlloc (Phase 5F): additive overload that captures the
// call-site file/line for the leak report. Calls AllocDispatch for the
// actual allocation and then records the pointer in g_Allocations under
// g_AllocMutex.
//
// The map and mutex are allocated lazily and are themselves NOT
// tracked (recursive tracking would be unsafe; see the bootstrap note
// in Memory.h). The map entry is the source of truth for the leak
// report at shutdown.
LPVOID _HeapAlloc(HANDLE hHeap,
                  DWORD dwFlags,
                  size_t bytes,
                  MemoryTag tag,
                  const char* file,
                  int line)
{
  std::lock_guard<std::mutex> lock(g_AllocMutex);
  if (!g_Allocations) g_Allocations = new std::map<void*, AllocationInfo>();

  LPVOID res = AllocDispatch(hHeap, dwFlags, bytes, tag);

  (*g_Allocations)[res] = { bytes, tag,
                            file ? file : "unknown", line };
  return res;
}

// _HeapAllocImpl: the MEM_DEBUG workhorse behind the Memory.h selector
// macro. Records the call site plus the actual backing store and arena
// generation at alloc time, so the shutdown report attributes every block
// and ClearTagAllocations can no longer silently hide heap-fallback blocks.
LPVOID _HeapAllocImpl(HANDLE hHeap,
                      DWORD dwFlags,
                      size_t bytes,
                      MemoryTag tag,
                      const char* file,
                      int line)
{
  std::lock_guard<std::mutex> lock(g_AllocMutex);
  if (!g_Allocations) g_Allocations = new std::map<void*, AllocationInfo>();

  LPVOID res = AllocDispatch(hHeap, dwFlags, bytes, tag);

  AllocationInfo info{ bytes, tag,
                       file ? file : "unknown", line };
  if (tag == MemoryTag::Level && LevelArena != nullptr &&
      LevelArena->Contains(res)) {
    info.backend = AllocBackend::Arena;
    info.arenaGen = LevelArena->GetGeneration();
  }
  (*g_Allocations)[res] = std::move(info);
  return res;
}

#pragma pop_macro("_HeapAlloc")
#endif

BOOL _HeapFree(HANDLE hHeap,
               DWORD  dwFlags,
               LPVOID lpMem)
{
  if (!lpMem) return false;

  // Phase 5F: remove the entry from the leak map (if recording is on)
  // before the pointer is freed. For allocations inside the arena we
  // skip HeapFree (the arena owns them and will reclaim them in bulk
  // on Reset()), but we still erase the map entry so they don't show
  // up as leaks. For MemoryTag::Level allocations that fell back to
  // HeapAlloc (because LevelArena was null at allocation time), the
  // map entry is erased and then the pointer is HeapFree'd normally.
  // Using the arena Contains() check instead of the tag-only check
  // ensures debug and release builds behave identically.
#ifdef MEM_DEBUG
  {
    std::lock_guard<std::mutex> lock(g_AllocMutex);
    if (g_Allocations) {
      auto it = g_Allocations->find(lpMem);
      if (it != g_Allocations->end()) {
        // Only short-circuit HeapFree when the pointer is actually in
        // the arena. A Level-tagged allocation that fell back to the
        // heap must still be HeapFree'd.
        if (LevelArena != nullptr && LevelArena->Contains(lpMem)) {
          g_Allocations->erase(it);
          return TRUE;
        }
        g_Allocations->erase(it);
      }
    }
  }
#endif

  // Phase 5A: arena allocations are silently ignored. The arena owns
  // them and will reclaim them in bulk on Reset() (LevelArena is null
  // in Phase 5A, so this check is always false and behavior is identical
  // to pre-5A).
  if (LevelArena != nullptr && LevelArena->Contains(lpMem))
    return true;

  const SIZE_T bytes = HeapSize(hHeap, HEAP_NO_SERIALIZE, lpMem);

  BOOL res = HeapFree(hHeap,
                      dwFlags,
                      lpMem);
  if (!res)
    DoHalt("Heap free error!");

  // HeapSize reports failure as SIZE_T(-1), not a released byte count.
  if (bytes != static_cast<SIZE_T>(-1))
    HeapReleased += bytes;

  return res;
}

#ifdef MEM_DEBUG
// Phase 5F: human-readable tag name for the leak report. C1 has the
// same function at Carnivores1/Hunt/Resources.cpp:97-107. Kept as a
// free function (not a method on MemoryTag) so it can be called from
// PrintMemoryLeaks without dragging the enum into a public header.
const char* MemoryTagToString(MemoryTag tag) {
    switch (tag) {
        case MemoryTag::Global:   return "Global";
        case MemoryTag::Level:    return "Level";
        case MemoryTag::Graphics: return "Graphics";
        case MemoryTag::Audio:    return "Audio";
        case MemoryTag::AI:       return "AI";
        case MemoryTag::Physics:  return "Physics";
        default:                  return "Unknown";
    }
}

// Phase 5F: walk g_Allocations and print every remaining entry (these
// are the leaks). Prints a per-tag summary at the end, then deletes
// the map. Called from Game.cpp ShutDownEngine() before delete
// LevelArena so the pointers in the report are still valid.
//
// Output goes to PrintLog, which writes to carnivor.log. The log is
// flushed by CloseLog() after the call returns (the doc explicitly
// warns about ordering: PrintMemoryLeaks must run before the log is
// closed, which it does because ShutDownEngine is called before
// CloseLog in WinMain's cleanup path).
void PrintMemoryLeaks()
{
    if (!g_Allocations || g_Allocations->empty()) {
        PrintLog("No memory leaks detected.\n");
        if (g_Allocations) {
            delete g_Allocations;
            g_Allocations = nullptr;
        }
        return;
    }

    char buf[512];
    sprintf(buf, "Memory leaks detected: %zu blocks\n", g_Allocations->size());
    PrintLog(buf);

    std::map<MemoryTag, size_t> tagTotals;
    size_t total = 0;

    for (auto const& kv : *g_Allocations) {
        void* ptr = kv.first;
        const AllocationInfo& info = kv.second;
        if (info.backend == AllocBackend::Arena)
            sprintf(buf, "[%s/arena gen %u] Leak: %p, size: %zu, at %s:%d\n",
                    MemoryTagToString(info.tag), info.arenaGen, ptr,
                    info.size, info.file.c_str(), info.line);
        else
            sprintf(buf, "[%s/heap] Leak: %p, size: %zu, at %s:%d\n",
                    MemoryTagToString(info.tag), ptr,
                    info.size, info.file.c_str(), info.line);
        PrintLog(buf);
        tagTotals[info.tag] += info.size;
        total += info.size;
    }

    PrintLog("\nMemory leaks summary by category:\n");
    for (auto const& kv : tagTotals) {
        sprintf(buf, "  %-10s: %zu bytes\n",
                MemoryTagToString(kv.first), kv.second);
        PrintLog(buf);
    }

    sprintf(buf, "Total leaked memory: %zu bytes\n", total);
    PrintLog(buf);

    delete g_Allocations;
    g_Allocations = nullptr;
}

// Remove only arena-backed entries after a bulk Reset. A Level-tagged
// allocation can be heap-backed when the arena did not exist; such a block
// was not reclaimed by Reset and must remain visible in the leak report
// until its owner explicitly frees it.
void ClearTagAllocations(MemoryTag tag)
{
    if (!g_Allocations) return;
    std::lock_guard<std::mutex> lock(g_AllocMutex);
    for (auto it = g_Allocations->begin(); it != g_Allocations->end(); ) {
        if (IsReclaimedByArenaReset(it->second.tag, it->second.backend, tag)) {
            it = g_Allocations->erase(it);
        } else {
            ++it;
        }
    }
}
#endif // MEM_DEBUG

void AddMessage(LPSTR mt)
{
  MessageList.timeleft = Platform::Milliseconds() + 2 * 1000;
  lstrcpy(MessageList.mtext, mt);
}

void PlaceHunter()
{
  if (LockLanding) return;

  if (g_GameMode == GameMode::TrophyMode)
  {
    PlayerX = 76*256+128;
    PlayerZ = 70*256+128;
    PlayerY = GetLandQH(PlayerX, PlayerZ);
    return;
  }

  if (g_GameMode == GameMode::SurvivalMode) {
	  PlayerX = SurvivalSpawnX * 256 + 128;
	  PlayerZ = SurvivalSpawnZ * 256 + 128;
	  PlayerY = GetLandQH(PlayerX, PlayerZ);
	  return;
  }

  int p = (Platform::Milliseconds() % LandingList.PCount);
  PlayerX = static_cast<float>(LandingList.list[p].x) * 256+128;
  PlayerZ = static_cast<float>(LandingList.list[p].y) * 256+128;
  PlayerY = GetLandQH(PlayerX, PlayerZ);
}

void CreateWaterTab()
{
  for (int c=0; c<0x8000; c++)
  {
    int R = (c >> 10);
    int G = (c >>  5) & 31;
    int B = c & 31;
    R =  1+(R * 8 ) / 28;
    if (R>31) R=31;
    G =  2+(G * 18) / 28;
    if (G>31) G=31;
    B =  3+(B * 22) / 28;
    if (B>31) B=31;
    FadeTab[64][c] = HiColor(R, G, B);
  }
}

void CreateFadeTab()
{
#ifdef _soft
  for (int l=0; l<64; l++)
    for (int c=0; c<0x8000; c++)
    {
      int R = (c >> 10);
      int G = (c >>  5) & 31;
      int B = c & 31;

      R = static_cast<int>((static_cast<float>(R) * (l) / 60.f + static_cast<float>(rand()) *0.2f / RAND_MAX));
      if (R>31) R=31;
      G = static_cast<int>((static_cast<float>(G) * (l) / 60.f + static_cast<float>(rand()) *0.2f / RAND_MAX));
      if (G>31) G=31;
      B = static_cast<int>((static_cast<float>(B) * (l) / 60.f + static_cast<float>(rand()) *0.2f / RAND_MAX));
      if (B>31) B=31;
      FadeTab[l][c] = HiColor(R, G, B);
    }

  CreateWaterTab();
#endif
}

void CreateDivTable()
{
  DivTbl[0] = 0x7fffffff;
  DivTbl[1] = 0x7fffffff;
  DivTbl[2] = 0x7fffffff;
  for( int i = 3; i < 10240; i++ )
    DivTbl[i] = static_cast<int>((static_cast<float>(0x100000000) / i));

  for (int y=0; y<32; y++)
    for (int x=0; x<32; x++)
      RandomMap[y][x] = rand() * 1024 / RAND_MAX;
}

void CreateVideoDIB()
{
  CreateVideoDIB(WinW, WinH);
}

void CreateVideoDIB(int W, int H)
{
  if (hdcMain == nullptr) {
    hdcMain = GetDC(hwndMain);
    hdcCMain = CreateCompatibleDC(hdcMain);

    SelectObject(hdcMain,  fnt_Midd);
    SelectObject(hdcCMain, fnt_Midd);
  }

  if (hbmpVideoBuf) {
    DeleteObject(hbmpVideoBuf);
    hbmpVideoBuf = nullptr;
  }

  BITMAPINFOHEADER bmih;
  bmih.biSize = sizeof( BITMAPINFOHEADER );
  bmih.biWidth  = W;
  bmih.biHeight = -H;
  bmih.biPlanes = 1;
  bmih.biBitCount = 16;
  bmih.biCompression = BI_RGB;
  bmih.biSizeImage = 0;
  bmih.biXPelsPerMeter = 400;
  bmih.biYPelsPerMeter = 400;
  bmih.biClrUsed = 0;
  bmih.biClrImportant = 0;

  BITMAPINFO binfo;
  binfo.bmiHeader = bmih;
  hbmpVideoBuf =
    CreateDIBSection(hdcMain, &binfo, DIB_RGB_COLORS, &lpVideoBuf, nullptr, 0);
}

int GetObjectH(int x, int y, int R)
{
  x = (x<<8) + 128;
  y = (y<<8) + 128;
  float hr,h;
  hr =GetLandH(static_cast<float>(x),    static_cast<float>(y));
  h = GetLandH( static_cast<float>(x)+R, static_cast<float>(y));
  if (h < hr) hr = h;
  h = GetLandH( static_cast<float>(x)-R, static_cast<float>(y));
  if (h < hr) hr = h;
  h = GetLandH( static_cast<float>(x),   static_cast<float>(y)+R);
  if (h < hr) hr = h;
  h = GetLandH( static_cast<float>(x),   static_cast<float>(y)-R);
  if (h < hr) hr = h;
  hr += 15;
  return  static_cast<int>((hr / ctHScale));
}

int GetObjectHWater(int x, int y)
{
  if (FMap[y][x] & fmReverse)
    return static_cast<int>((HMap[y][x+1]+HMap[y+1][x])) / 2 + 48;
  else
    return static_cast<int>((HMap[y][x]+HMap[y+1][x+1])) / 2 + 48;
}



WORD conv_565(WORD c)
{
  return (c & 31) + ( (c & 0xFFE0) << 1 );
}







void ReleaseResources()
{
  HeapReleased=0;

  // Release per-level objects before rewinding the arena. _HeapFree() knows
  // how to ignore arena-owned pointers, and this also covers Phase 5A builds
  // where LevelArena is null and the allocations really are heap-backed.
  for (int t=0; t<1024; t++)
    if (Textures[t].get())
    {
      Textures[t].reset();
      Textures[t] = nullptr;
    }
    else break;

  for (int m = 0; m < 256; m++)
  {
    MObjects[m].bmpmodel.lpTexture.reset();
    MObjects[m].vtl.aniData.reset();
    MObjects[m].vtl.FramesCount = 0;
    MObjects[m].vtl.AniTime = 0;

    TModel *mptr = MObjects[m].model.get();
    if (mptr)
    {
      // Remove GL texture cache entry before freeing the TModel.
      ReleaseModelTexture(mptr);

      mptr->lpTexture.reset();
      mptr->lpTexture  = nullptr;
      mptr->lpTexture2.reset();
      mptr->lpTexture2 = nullptr;
      mptr->lpTexture3.reset();
      mptr->lpTexture3 = nullptr;

      // gFace and VLight[0] are raw heap pointers allocated by
      // AllocateMemoryForModel. They are not managed by unique_ptr
      // (~TModel() is default) and must be freed explicitly before
      // the model is destroyed. Shared helper — see EngineAPI.h.
      ReleaseModelBuffers(mptr);

      MObjects[m].model.reset();
    }
  }

  // Zero-length sounds are valid, so empty vectors cannot act as sentinels.
  // Visit every slot and swap with an empty vector to release capacity too.
  for (int a = 0; a < 256; a++)
  {
    std::vector<short int>().swap(Ambient[a].sfx.lpData);
    Ambient[a].sfx.length = 0;
    Ambient[a].RSFXCount = 0;
  }

  for (int r = 0; r < 256; r++)
  {
    std::vector<short int>().swap(RandSound[r].lpData);
    RandSound[r].length = 0;
  }

  // Per-level UI pictures and weapon scratch buffers loaded from the
  // current .rsc/map pass.
  MapPic.lpImage.reset();
  TrophyPic.lpImage.reset();
  TrophyNoCollectPic.lpImage.reset();
  ScorePic.lpImage.reset();
  for (int i=0; i<4; i++)
    Weapon.Flash[i].lpImage.reset();
  for (int i=0; i<10; i++)
  {
    Weapon.BulletPic[i].lpImage.reset();
    Weapon.ChambPic[i].lpImage.reset();
  }
  for (int i=0; i<16; i++)
    MenuDinoInfo[i].CallIcon.lpImage.reset();

  Weapon.normals.reset();

  // Raw per-level render scratch buffers. _HeapFree() ignores arena-owned
  // pointers and frees them when LevelArena is null (Phase 5A compatibility).
  if (rVertex)
  {
    (void)_HeapFree(Heap, 0, rVertex);
    rVertex = nullptr;
  }
  if (gScrp)
  {
    (void)_HeapFree(Heap, 0, gScrp);
    gScrp = nullptr;
  }
  if (PhongMapping)
  {
    (void)_HeapFree(Heap, 0, PhongMapping);
    PhongMapping = nullptr;
  }

  // Clear the GL renderer's per-level caches now that all per-level
  // objects have been released. This prevents unbounded VRAM growth
  // (model textures, BMP textures, static geometry VBO/IBO offsets)
  // and ensures the terrain texture upload cache is fresh for the next
  // level. C1 does the same in its ReleaseResources via
  // renderer->ClearLevelTextureCache() and ResetTerrainTextureCache().
  ClearRendererLevelCache();
  ClearRendererTerrainCache();

  // Phase 5F.2: reset the per-level arena after per-level owners have
  // dropped their pointers. Keep the MEM_DEBUG cleanup after the reset so
  // the arena still owns the addresses while the leak map is pruned.
  if (LevelArena != nullptr) {
    LevelArena->Reset();
  }

#ifdef MEM_DEBUG
  if (LevelArena != nullptr) {
    ClearTagAllocations(MemoryTag::Level);
  }
#endif
}

void ReleaseGlobalResources()
{
  // Phase 5C.1: Release the global (cross-level) resources that the menu
  // and base game load once at startup. Without this, every Quit leaks the
  // weapon character info, the Sun/Compass/Binocular models, the menu
  // pictures, and the per-character global allocations in ChInfo[].
  //
  // Mirrors C1's ReleaseGlobalResources in Carnivores1/Hunt/Resources.cpp
  // (lines 1220-1256), adapted for C2 ME's larger ChInfo[128] array and
  // the absence of the menu SFX globals (fxMenuGo/Mov/Amb -- C2 ME handles
  // menu audio differently and doesn't have them as globals).

  for (int c = 0; c < DINOINFO_MAX; c++)
  {
    ReleaseCharacterInfo(ChInfo[c]);
  }

  ReleaseCharacterInfo(ShipModel);
  ReleaseCharacterInfo(SShipModel);
  ReleaseCharacterInfo(WindModel);

  // Previously omitted persistent owners: each leaked its full allocation
  // set at every clean shutdown (and polluted the MEM_DEBUG report).
  // All release helpers are null-safe, so unconditional release is correct
  // even when an owner was never loaded (e.g. MPlayerInfo in singleplayer).
  // Releasing everything here — before PrintMemoryLeaks and before C++
  // static teardown — is also what keeps late global destructors from
  // calling _HeapFree after the leak tracker is gone.
  ReleaseCharacterInfo(WCircleModel);
  ReleaseCharacterInfo(BagModel);
  ReleaseCharacterInfo(MuzzModel);
  ReleaseCharacterInfo(HitBoxModel);

  for (int w = 0; w < 10; w++)
  {
    ReleaseCharacterInfo(Weapon.chinfo[w]);
    ReleaseCharacterInfo(Weapon.Bullet[w]);
  }

  for (int p = 0; p < 3; p++)
  {
    ReleaseCharacterInfo(MPlayerInfo[p]);
  }

  ReleaseModel(SunModel);
  ReleaseModel(CompasModel);
  ReleaseModel(Binocular);

  // Menu pictures -- unique_heap_ptr<WORD[]>, so .reset() is enough.
  PausePic.lpImage.reset();
  ExitPic.lpImage.reset();
  TrophyExit.lpImage.reset();
  TrophyPic.lpImage.reset();
  TrophyNoCollectPic.lpImage.reset();
  ScorePic.lpImage.reset();
  LandPic.lpImage.reset();
  DinoPic.lpImage.reset();
  DinoPicM.lpImage.reset();
  MapPic.lpImage.reset();
  WepPic.lpImage.reset();

  // OpenGL/D3D/3DFX effect lookup textures loaded once in WinMain().
  TFX_SPECULAR.lpImage.reset();
  TFX_ENVMAP.lpImage.reset();

  // The "null" texture (index 255) is the one InitEngine allocates
  // before any level loads; it's never reset by ReleaseResources()
  // because ReleaseResources iterates from 0 and breaks on the first
  // null pointer. Release it here.
  Textures[255].reset();

  // M-3: Snow particle data was allocated via _HeapAlloc (tagged Global)
  // during LoadResourcesScript (InitEngine). Session-lifetime allocation;
  // freed here because ReleaseResources runs on every level transition
  // but Snow is never re-allocated per-level.
  if (Snow)
  {
    (void)_HeapFree(Heap, 0, Snow);
    Snow = nullptr;
  }
}

void LoadResources()
{

  int  FadeRGB[3][3];
  int TransRGB[3][3];

  int tc,mc;
  char MapName[128],RscName[128];
  HeapAllocated=0;
  if (strstr(ProjectName, "trophy"))
  {
    g_GameMode = GameMode::TrophyMode;
    ctViewR = 60;
    charViewR = ctViewR;
  }
  {
    // Permanent breadcrumb: which map booted and whether the room was
    // recognised (weapon gating keys off this in HideWeapon/ProcessShoot).
    char msg[192];
    sprintf_s(msg, sizeof(msg), "Area: %s (trophy room: %s).\n",
              ProjectName, InTrophyRoomMap() ? "yes" : "no");
    PrintLog(msg);
  }
  sprintf_s(MapName, sizeof(MapName),"%s%s", ProjectName, ".map");
  sprintf_s(RscName, sizeof(RscName),"%s%s", ProjectName, ".rsc");

  ReleaseResources();

  hfile = CreateFile(RscName,
                     GENERIC_READ, FILE_SHARE_READ,
                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

  if (hfile==INVALID_HANDLE_VALUE)
  {
    char sz[512];
    sprintf_s(sz, sizeof(sz), "Error opening resource file\n%s.", RscName );
    DoHalt(sz);
    return;
  }

  if (!EngineResource::Read(hfile, tc) || !EngineResource::Read(hfile, mc))
    DoHalt("Resource loading error: truncated resource header.");
  l = 4;
  // tc indexes Textures[1024]; mc indexes MObjects[256].
  if (!IsValidCount(tc, 1024))
    RscLoadFail("texture count exceeds capacity", tc, 1024);
  if (!IsValidCount(mc, 256))
    RscLoadFail("model count exceeds capacity", mc, 256);

  ReadRscValue(hfile, FadeRGB, "fade color table");
  ReadRscValue(hfile, TransRGB, "transparency color table");

  SkyR  =  FadeRGB[OptDayNight][0];
  SkyG  =  FadeRGB[OptDayNight][1];
  SkyB  =  FadeRGB[OptDayNight][2];

  SkyTR = TransRGB[OptDayNight][0];
  SkyTG = TransRGB[OptDayNight][1];
  SkyTB = TransRGB[OptDayNight][2];

  if (OptDayNight==2)
  {
    SkyR = 0;
    SkyB = 0;
    SkyTR = 0;
    SkyTB = 0;
  }

  SkyTR = MIN(255,SkyTR * (OptBrightness + 128) / 256);
  SkyTG = MIN(255,SkyTG * (OptBrightness + 128) / 256);
  SkyTB = MIN(255,SkyTB * (OptBrightness + 128) / 256);

  SkyR = MIN(255,SkyR * (OptBrightness + 128) / 256);
  SkyG = MIN(255,SkyG * (OptBrightness + 128) / 256);
  SkyB = MIN(255,SkyB * (OptBrightness + 128) / 256);

  PrintLog("Loading textures:");
  for (int tt=0; tt<tc; tt++)
    LoadTexture(Textures[tt]);
  PrintLog(" Done.\n");

  PrintLog("Loading models:");
  PrintLoad("Loading models...");
  for (int mm=0; mm<mc; mm++)
  {
    ReadRscValue(hfile, MObjects[mm].info, "model info record");
    MObjects[mm].info.Radius*=2;
    MObjects[mm].info.YLo*=2;
    MObjects[mm].info.YHi*=2;
    MObjects[mm].info.linelenght = (MObjects[mm].info.linelenght / 128) * 128;
    LoadModel(MObjects[mm].model, MemoryTag::Level);
    LoadBMPModel(MObjects[mm]);

    if (MObjects[mm].info.flags & ofNOLIGHT)
    {
        FillMemory(MObjects[mm].model->VLight[0], 4 * MObjects[mm].model->VCount, 0);
        FillMemory(MObjects[mm].model->VLight[1], 4 * MObjects[mm].model->VCount, 0);
        FillMemory(MObjects[mm].model->VLight[2], 4 * MObjects[mm].model->VCount, 0);
        FillMemory(MObjects[mm].model->VLight[3], 4 * MObjects[mm].model->VCount, 0);
    }

    if (MObjects[mm].info.flags & ofANIMATED)
      LoadAnimation(MObjects[mm].vtl, MObjects[mm].model->VCount);

    MObjects[mm].info.BoundR = 0;
    for (int v=0; v<MObjects[mm].model->VCount; v++)
    {
      float r = static_cast<float>(sqrt(MObjects[mm].model->gVertex[v].x * MObjects[mm].model->gVertex[v].x +
                            MObjects[mm].model->gVertex[v].z * MObjects[mm].model->gVertex[v].z ));
      if (r>MObjects[mm].info.BoundR) MObjects[mm].info.BoundR=r;
    }

    if (MObjects[mm].info.flags & ofBOUND)
      CalcBoundBox(MObjects[mm].model.get(), MObjects[mm].bound);

    GenerateModelMipMaps(MObjects[mm].model.get(), MemoryTag::Level);
    GenerateAlphaFlags(MObjects[mm].model.get());
  }
  PrintLog(" Done.\n");

  PrintLoad("Finishing with .res...");
  PrintLog("Finishing with .res:");
  LoadSky();
  LoadSkyMap();

  int FgCount = 0;
  ReadRscValue(hfile, FgCount, "fog count");
  // Loop below touches FogsList[0..FgCount]; the read targets FogsList[1].
  size_t fogbytes = 0;
  if (!IsValidCount(FgCount, 255) ||
      !CheckedTransferBytes2((size_t)FgCount, LegacyResource::FogSize, fogbytes))
    RscLoadFail("fog count exceeds FogsList capacity", FgCount, 255);
  for (int f=1; f<=FgCount; f++)
    ReadRscValue(hfile, FogsList[f], "fog records");

  for (int f=0; f<=FgCount; f++)
  {
    int fb = (FogsList[f].fogRGB >> 00) & 0xFF;
    int fg = (FogsList[f].fogRGB >>  8) & 0xFF;
    int fr = (FogsList[f].fogRGB >> 16) & 0xFF;
#ifdef _d3d
    FogsList[f].fogRGB = (fr) + (fg<<8) + (fb<<16);
#endif
    // Night vision green fog tint removed — handled by per-frame overlay
  }

  int RdCount = 0, AmbCount = 0, WtrCount = 0;

  ReadRscValue(hfile, RdCount, "random-sound count");
  if (!IsValidCount(RdCount, 256))
    RscLoadFail("random-sound count exceeds capacity", RdCount, 256);
  for (int r=0; r<RdCount; r++)
  {
    ReadRscValue(hfile, RandSound[r].length, "random-sound length");
    // Phase 5B.1: lpData is now std::vector<short int>. assign() value-
    // initializes to zero (matches the previous HEAP_ZERO_MEMORY behavior).
    // Bound the length first: corrupt values drove huge assigns (and odd
    // lengths overflowed the floor(length/2) read by one byte).
    if (!IsValidWavLength(RandSound[r].length))
      RscLoadFail("random-sound length out of range", RandSound[r].length, 16 << 20);
    RandSound[r].lpData.assign(WavAllocSamples(RandSound[r].length), 0);
    if (!EngineResource::ReadPCM16(hfile, RandSound[r].lpData.data(),
                                  RandSound[r].lpData.size(), RandSound[r].length))
      DoHalt("Resource loading error: truncated random-sound data.");
  }

  ReadRscValue(hfile, AmbCount, "ambient count");
  if (!IsValidCount(AmbCount, 256))
    RscLoadFail("ambient count exceeds capacity", AmbCount, 256);
  for (int a=0; a<AmbCount; a++)
  {
    ReadRscValue(hfile, Ambient[a].sfx.length, "ambient-sound length");
    if (!IsValidWavLength(Ambient[a].sfx.length))
      RscLoadFail("ambient-sound length out of range", Ambient[a].sfx.length, 16 << 20);
    Ambient[a].sfx.lpData.assign(WavAllocSamples(Ambient[a].sfx.length), 0);
    if (!EngineResource::ReadPCM16(hfile, Ambient[a].sfx.lpData.data(),
                                  Ambient[a].sfx.lpData.size(), Ambient[a].sfx.length))
      DoHalt("Resource loading error: truncated ambient-sound data.");

    ReadRscValue(hfile, Ambient[a].rdata, "ambient random-effect records");
    ReadRscValue(hfile, Ambient[a].RSFXCount, "ambient random-effect count");
    ReadRscValue(hfile, Ambient[a].AVolume, "ambient volume");
    // RSFXCount indexes rdata[16] (and rdata[r+1]) in the filter below
    // and feeds rand() % RSFXCount in Controls.cpp.
    if (!IsValidCount(Ambient[a].RSFXCount, 16))
      RscLoadFail("ambient RSFX count exceeds rdata capacity", Ambient[a].RSFXCount, 16);

    if (Ambient[a].RSFXCount)
      Ambient[a].RndTime = (Ambient[a].rdata[0].RFreq / 2 + rRand(Ambient[a].rdata[0].RFreq)) * 1000;

    int F = Ambient[a].rdata[0].RFreq;
    int E = Ambient[a].rdata[0].REnvir;
/////////////////

    //sprintf_s(logt, sizeof(logt),"Env=%d  Flag=%d  Freq=%d\n", E, Ambient[a].rdata[0].Flags, F);
    //PrintLog(logt);

    if (OptDayNight==2)
      for (int r=0; r<Ambient[a].RSFXCount; r++)
        if (Ambient[a].rdata[r].Flags)
        {
          if (r!=15) memcpy(&Ambient[a].rdata[r], &Ambient[a].rdata[r+1], (15-r)*sizeof(TRD));
          Ambient[a].RSFXCount--;
          r--;
        }

    Ambient[a].rdata[0].RFreq = F;
    Ambient[a].rdata[0].REnvir = E;

  }

  ReadRscValue(hfile, WtrCount, "water count");
  size_t wtrbytes = 0;
  if (!IsValidCount(WtrCount, 256) ||
      !CheckedTransferBytes2((size_t)WtrCount, LegacyResource::WaterSize, wtrbytes))
    RscLoadFail("water count exceeds WaterList capacity", WtrCount, 256);
  for (int w=0; w<WtrCount; w++)
    ReadRscValue(hfile, WaterList[w], "water records");

  WaterList[255].wlevel = 0;
  for (int w=0; w<WtrCount; w++)
  {
    // tindex comes from the file; Textures[] has tc live entries.
    if (WaterList[w].tindex < 0 || WaterList[w].tindex >= tc)
      RscLoadFail("water texture index out of range", WaterList[w].tindex, tc);
#ifdef _3dfx
    WaterList[w].fogRGB = (Textures[WaterList[w].tindex]->mR) +
                          (Textures[WaterList[w].tindex]->mG<<8) +
                          (Textures[WaterList[w].tindex]->mB<<16);
#else
    WaterList[w].fogRGB = (Textures[WaterList[w].tindex]->mB) +
                          (Textures[WaterList[w].tindex]->mG<<8) +
                          (Textures[WaterList[w].tindex]->mR<<16);
#endif
  }
  CloseHandle(hfile);
  PrintLog(" Done.\n");

//================ Load MAPs file ==================//
  PrintLoad("Loading .map...");
  PrintLog("Loading .map:");
  hfile = CreateFile(MapName,
                     GENERIC_READ, FILE_SHARE_READ,
                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

  if (hfile==INVALID_HANDLE_VALUE)
    DoHalt("Error opening map file.");

  RequireMapRead(EngineMap::ReadBytePlane(hfile, HMap), "height map");
  RequireMapRead(EngineMap::ReadWordPlane(hfile, TMap1), "primary texture map");
  RequireMapRead(EngineMap::ReadWordPlane(hfile, TMap2), "secondary texture map");
  RequireMapRead(EngineMap::ReadBytePlane(hfile, OMap), "object map");
  RequireMapRead(EngineMap::ReadWordPlane(hfile, FMap), "flags map");
  RequireMapRead(EngineMap::ReadLightPlane(hfile, LMap, OptDayNight), "light-map table");
  RequireMapRead(EngineMap::ReadBytePlane(hfile, WMap), "water map");
  RequireMapRead(EngineMap::ReadBytePlane(hfile, HMapO), "object-height map");
  RequireMapRead(EngineMap::ReadBytePlane(hfile, FogsMap), "fog map");
  RequireMapRead(EngineMap::ReadBytePlane(hfile, AmbMap), "ambient map");

  ValidateMapReferences(tc, mc, WtrCount);

  if (FogsList[1].YBegin>1.f)
    for (int x=0; x<510; x++)
      for (int y=0; y<510; y++)
        if (!FogsMap[y][x])
          if (HMap[y*2+0][x*2+0]<FogsList[1].YBegin || HMap[y*2+0][x*2+1]<FogsList[1].YBegin || HMap[y*2+0][x*2+2] < FogsList[1].YBegin ||
              HMap[y*2+1][x*2+0]<FogsList[1].YBegin || HMap[y*2+1][x*2+1]<FogsList[1].YBegin || HMap[y*2+1][x*2+2] < FogsList[1].YBegin ||
              HMap[y*2+2][x*2+0]<FogsList[1].YBegin || HMap[y*2+2][x*2+1]<FogsList[1].YBegin || HMap[y*2+2][x*2+2] < FogsList[1].YBegin)
            FogsMap[y][x] = 1;

  CloseHandle(hfile);
  PrintLog(" Done.\n");

//======= Post load rendering ==============//
  PrintLoad("Prepearing maps...");
  CreateTMap();
  RenderLightMap();

  LoadPictureTGA(MapPic, "HUNTDAT\\MENU\\mapframe.tga", MemoryTag::Level);
  conv_pic(MapPic);

  GenerateMapImage();

  for (int i = 0; i < 4;i++) {
	  char buff[100];
	  sprintf(buff, "HUNTDAT\\WEAPONS\\flash%i.tga", i+1);
	  LoadPictureTGA(Weapon.Flash[i], buff, MemoryTag::Level);
	  conv_pic(Weapon.Flash[i]);
  }

  if (g_GameMode == GameMode::TrophyMode) LoadPictureTGA(TrophyPic, "HUNTDAT\\MENU\\trophy.tga", MemoryTag::Level);
  else {
	  LoadPictureTGA(TrophyPic, "HUNTDAT\\MENU\\collect.tga", MemoryTag::Level);
	  LoadPictureTGA(TrophyNoCollectPic, "HUNTDAT\\MENU\\trophy_g.tga", MemoryTag::Level);
	  conv_pic(TrophyNoCollectPic);
  }
  conv_pic(TrophyPic);
  LoadPictureTGA(ScorePic, "HUNTDAT\\MENU\\score.tga", MemoryTag::Level);
  conv_pic(ScorePic);

//    ReInitGame();
}






//================ light map ========================//

void FillVector(int x, int y, Vector3d& v)
{
  v.x = static_cast<float>(x)*256;
  v.z = static_cast<float>(y)*256;
  v.y = static_cast<float>((static_cast<int>(HMap[y][x])))*ctHScale;
}

BOOL TraceVector(Vector3d v, Vector3d lv)
{
  v.y+=4;
  NormVector(lv,64);
  for (int l=0; l<32; l++)
  {
    v.x-=lv.x;
    v.y-=lv.y/6;
    v.z-=lv.z;
    if (v.y>255 * ctHScale) return true;
    if (GetLandH(v.x, v.z) > v.y) return false;
  }
  return true;
}

void AddShadow(int x, int y, int d)
{
  if (x<0 || y<0 || x>1023 || y>1023) return;
  int l = LMap[y][x];
  l-=d;
  if (l<32) l=32;
  LMap[y][x]=l;
}

void RenderShadowCircle(int x, int y, int R, int D)
{
  int cx = x / 256;
  int cy = y / 256;
  int cr = 1 + R / 256;
  for (int yy=-cr; yy<=cr; yy++)
    for (int xx=-cr; xx<=cr; xx++)
    {
      int tx = (cx+xx)*256;
      int ty = (cy+yy)*256;
      int r = static_cast<int>(sqrt( static_cast<double>(((tx-x)*(tx-x) + (ty-y)*(ty-y))) ));
      if (r>R) continue;
      AddShadow(cx+xx, cy+yy, D * (R-r) / R);
    }
}

void RenderLightMap()
{

  Vector3d lv;
  int x,y;

  lv.x = - 412;
  lv.z = - 412;
  lv.y = - 1024;
  NormVector(lv, 1.0f);

  for (y=1; y<ctMapSize-1; y++)
    for (x=1; x<ctMapSize-1; x++)
    {
      int ob = OMap[y][x];
      if (ob == 255) continue;

      int l = MObjects[ob].info.linelenght / 128;
      int s = 1;
      if (OptDayNight==2) s=-1;
      if (OptDayNight!=1) l = MObjects[ob].info.linelenght / 70;
      if (l>0) RenderShadowCircle(x*256+128,y*256+128, 256, MObjects[ob].info.lintensity * 2);
      for (int i=1; i<l; i++)
        AddShadow(x+i*s, y+i*s, MObjects[ob].info.lintensity);

      l = MObjects[ob].info.linelenght * 2;
      RenderShadowCircle(x*256+128+l*s,y*256+128+l*s,
                         MObjects[ob].info.circlerad*2,
                         MObjects[ob].info.cintensity*4);
    }

}

void SaveScreenShot()
{

  HANDLE hf;                  /* file handle */
  BITMAPFILEHEADER hdr;       /* bitmap file-header */
  BITMAPINFOHEADER bmi;       /* bitmap info-header */
  DWORD dwTmp;

  if (WinW>1024) return;

  //MessageBeep(0xFFFFFFFF);
  CopyHARDToDIB();

  bmi.biSize = sizeof(BITMAPINFOHEADER);
  bmi.biWidth = WinW;
  bmi.biHeight = WinH;
  bmi.biPlanes = 1;
  bmi.biBitCount = 24;
  bmi.biCompression = BI_RGB;

  bmi.biSizeImage = WinW*WinH*3;
  bmi.biClrImportant = 0;
  bmi.biClrUsed = 0;

  hdr.bfType = 0x4d42;
  hdr.bfSize = static_cast<DWORD>((sizeof(BITMAPFILEHEADER) +
                        bmi.biSize + bmi.biSizeImage));
  hdr.bfReserved1 = 0;
  hdr.bfReserved2 = 0;
  hdr.bfOffBits = static_cast<DWORD>(sizeof(BITMAPFILEHEADER)) +
                  bmi.biSize;

  char t[12];
  sprintf_s(t, sizeof(t),"HUNT%004d.BMP",++_shotcounter);
  hf = CreateFile(t,
                  GENERIC_READ | GENERIC_WRITE,
                  static_cast<DWORD>(0),
                  (LPSECURITY_ATTRIBUTES) nullptr,
                  CREATE_ALWAYS,
                  FILE_ATTRIBUTE_NORMAL,
                  (HANDLE) nullptr);

  WriteFile(hf, static_cast<LPVOID>(&hdr), sizeof(BITMAPFILEHEADER), static_cast<LPDWORD>(&dwTmp), (LPOVERLAPPED) nullptr);

  WriteFile(hf, &bmi, sizeof(BITMAPINFOHEADER), static_cast<LPDWORD>(&dwTmp), (LPOVERLAPPED) nullptr);

  byte fRGB[1024][3];

  for (int y=0; y<WinH; y++)
  {
    for (int x=0; x<WinW; x++)
    {
      WORD C = *(static_cast<WORD*>(lpVideoBuf) + (WinEY-y)*VideoPitch+x);
      fRGB[x][0] = (C       & 31)<<3;
#if defined(_gl)
      fRGB[x][1] = ((C>> 5) & 31)<<3;
      fRGB[x][2] = ((C>>10) & 31)<<3;
#else
      if (HARD3D)
      {
        fRGB[x][1] = ((C>> 5) & 63)<<2;
        fRGB[x][2] = ((C>>11) & 31)<<3;
      }
      else
      {
        fRGB[x][1] = ((C>> 5) & 31)<<3;
        fRGB[x][2] = ((C>>10) & 31)<<3;
      }
#endif
    }
    WriteFile( hf, fRGB, 3*WinW, &dwTmp, nullptr );
  }

  CloseHandle(hf);
  //MessageBeep(0xFFFFFFFF);
}

//===============================================================================================
//===============================================================================================

void CreateLog()
{

  hlog = CreateFile("render.log",
                    GENERIC_WRITE,
                    FILE_SHARE_READ, nullptr,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

#ifdef _d3d
  PrintLog("CarnivoresII  D3D video driver.");
#endif

#ifdef _3dfx
  PrintLog("CarnivoresII 3DFX video driver.");
#endif

#ifdef _soft
  PrintLog("CarnivoresII Soft video driver.");
#endif
  PrintLog(" Build v2.04. Sep.24 1999.\n");
}

void PrintLog(LPSTR l)
{
  DWORD w;

  if (l[strlen(l)-1]==0x0A)
  {
    BYTE b = 0x0D;
    WriteFile(hlog, l, strlen(l)-1, &w, nullptr);
    WriteFile(hlog, &b, 1, &w, nullptr);
    b = 0x0A;
    WriteFile(hlog, &b, 1, &w, nullptr);
  }
  else
    WriteFile(hlog, l, strlen(l), &w, nullptr);

}

void PrintLogVerbose(LPSTR l)
{
  if (!g_VerboseLogging) return;
  PrintLog(l);
}

void CloseLog()
{
  CloseHandle(hlog);
}