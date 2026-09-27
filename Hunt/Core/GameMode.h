// GameMode.h — Game mode state enum (replaces boolean flag sprawl)
// Phase 1.1: replaces 30+ independent BOOL flags with a single state machine.
#pragma once

#include <cstring>

enum class GameMode : unsigned int {
    Normal          = 0,
    Swimming        = 1,
    Underwater      = 2,
    Flying          = 3,    // reserved: takes over the FLY bool when it moves
    Paused          = 4,
    Binocular       = 5,
    OpticScope      = 6,
    MapMode         = 7,
    ExitCountdown   = 8,
    TrophyMode      = 9,
    Dead            = 10,   // reserved
    Falling         = 11,   // reserved
    Crouching       = 12,
    NightVision     = 13,
    DogMode         = 14,
    SurvivalMode    = 15,
    ScannerMode     = 16,
    SonarMode       = 17,
};

extern GameMode g_GameMode;
// Overlay stashed when the Escape menu or Pause takes over the single-enum
// mode slot, so dismissing the menu can return to the scope, binocular, or
// map view that was up (the original engine kept EXITMODE/PAUSE as
// independent flags and never lost it). Only meaningful while the menu is
// up; cleared on dismiss and wherever ExitCountdown is entered outside the
// menu (death flow). See DismissMenuRestore() in Hunt/Game/Hunt.cpp.
extern GameMode g_SavedOverlayMode;
// Non-menu entry into ExitCountdown (death demo): clears the stash first so
// a later dismiss restores Normal instead of resurrecting a stale overlay.
// Lives here so every translation unit enters ExitCountdown outside the menu
// through the same choke point (see EnterMenuMode() in Hunt/Game/Hunt.cpp).
inline void EnterExitCountdownNoStash() {
  g_SavedOverlayMode = GameMode::Normal;
  g_GameMode = GameMode::ExitCountdown;
}
// Full-screen aiming views that survive a menu round-trip via the stash above.
inline bool IsOverlayMode(GameMode m) {
  return m == GameMode::OpticScope || m == GameMode::Binocular || m == GameMode::MapMode;
}
// Pure state transitions used by the Escape/Pause menu paths. Keeping the
// stash rules separate from mouse/window side effects makes the restoration
// contract testable without a running Win32 game window.
inline GameMode EnterMenuState(GameMode current, GameMode& saved) {
  saved = current;
  return GameMode::ExitCountdown;
}
inline GameMode RestoreMenuState(GameMode saved) {
  return IsOverlayMode(saved) ? saved : GameMode::Normal;
}
// The only modes an underwater transition may silently overwrite. Every other
// mode is an overlay the player opened deliberately (Tab -> map, Escape ->
// exit prompt, Pause), and stomping on it every frame would make that overlay
// unreachable while the camera is submerged. The side effects of the
// transition (camera tweak, splash sound, water circle) still apply either
// way; see the swim branch in Hunt/Game/Controls.cpp.
//
// Crouching is kept in the set for symmetry with the original list, but it is
// vestigial: stance moved to the CrouchMode flag above, and nothing assigns
// GameMode::Crouching any more.
inline bool IsInWorldMovementMode(GameMode m) {
  return m == GameMode::Normal || m == GameMode::Swimming ||
         m == GameMode::Crouching;
}
// Rendering view: the exit menu (ExitCountdown) and Pause borrow the mode
// slot but must not change what the player SEES of an underlying magnifier.
// The original engine kept EXITMODE/PAUSE as independent flags, so the scope
// stayed zoomed under the menu; without this, ActiveWorldZoom() collapses to
// 1x the moment the menu opens (mask up, world wide). Logic gates that drive
// input/animation (breath ease, toggles) keep using the raw mode — only
// rendering predicates use these.
inline bool IsScopeView() {
  if (g_GameMode == GameMode::OpticScope) return true;
  if ((g_GameMode == GameMode::ExitCountdown || g_GameMode == GameMode::Paused) &&
      g_SavedOverlayMode == GameMode::OpticScope) return true;
  return false;
}
inline bool IsBinocularView() {
  if (g_GameMode == GameMode::Binocular) return true;
  if ((g_GameMode == GameMode::ExitCountdown || g_GameMode == GameMode::Paused) &&
      g_SavedOverlayMode == GameMode::Binocular) return true;
  return false;
}
extern int CrouchMode; // stance flag: independent of g_GameMode so crouch
                       // coexists with overlays (OpticScope, Binocular, ...)

// --- Accessor helpers (inline, defined after g_GameMode is visible) ---

// Caveat: the previous version of `IsUnderwater()` used
//   `g_GameMode == GameMode::Underwater || g_GameMode == GameMode::Swimming`.
// That definition collapses "the player is physically submerged" with
// "the logical game mode is Underwater/Swimming". Once the player opens
// an overlay (Tab -> MapMode, Escape -> ExitCountdown, Pause, ...),
// g_GameMode flips off Underwater even though the camera is still
// physically below the water surface. The old helper would then return
// false and the renderer would yank away the fog, render the water
// surface opaque, and clip underwater models.
//
// The call sites that use IsUnderwater() all want the *physical* semantic
// (NOT the game-mode semantic): fog density, sun glare in ShowVideo(),
// sky dimming, water-surface visibility, terrain water blending, drowning
// in Controls.cpp, weapon hide, animation slowdown, breath handling, etc.
// So we redirect the helper to the global flag that's always been the
// actual source of truth for the physical state.
extern int UNDERWATER;          // 1 iff camera is submerged this frame

inline bool IsUnderwater()  { return UNDERWATER != 0; }
inline bool IsScopeActive() { return g_GameMode == GameMode::OpticScope || g_GameMode == GameMode::Binocular; }
inline bool IsPaused()      { return g_GameMode == GameMode::Paused; }
// Crouch used to BE GameMode::Crouching, which made stance and overlay
// mutually exclusive: drawing a scoped weapon stood the player up and
// crouching killed an active scope. Stance now lives in CrouchMode.
inline bool IsCrouching()   { return CrouchMode != 0; }

// --- Per-state accessors (1:1 with g_GameMode == GameMode::X) ---
//
// Mirrors the helper set in Carnivores1/Hunt/Core/GameMode.h so the
// two projects read uniformly. Use these whenever you want to test
// for a specific game-mode value without writing the inline
// comparison — and so the call site stays readable if the underlying
// GameMode enum evolves.

inline bool IsNormal()        { return g_GameMode == GameMode::Normal; }
inline bool IsSwimming()      { return g_GameMode == GameMode::Swimming; }
inline bool IsUnderwaterMode() { return g_GameMode == GameMode::Underwater; }
inline bool IsBinocular()     { return g_GameMode == GameMode::Binocular; }
inline bool IsOpticScope()    { return g_GameMode == GameMode::OpticScope; }
inline bool IsMapMode()       { return g_GameMode == GameMode::MapMode; }
inline bool IsExitCountdown() { return g_GameMode == GameMode::ExitCountdown; }
inline bool IsTrophyMode()    { return g_GameMode == GameMode::TrophyMode; }

extern char ProjectName[128]; // set once from the prj= command line (GameState.h)
// Trophy-room test that does NOT depend on g_GameMode (a dozen writers can
// flip the mode mid-session, e.g. water/swim transitions). ProjectName is
// stable for the whole session. Same substring LoadResources() uses to set
// TrophyMode, so the two can never disagree about which map is the room.
inline bool InTrophyRoomMap() { return strstr(ProjectName, "trophy") != nullptr; }
// Session semantics must survive movement and overlay changes to the mode slot.
inline bool InTrophyRoom() { return IsTrophyMode() || InTrophyRoomMap(); }
// The trophy room has no usable hunting map. Base this on session identity,
// not only TrophyMode, because movement and overlays can replace that mode.
inline bool CanUseMap() { return !InTrophyRoom(); }
inline bool IsSurvivalMode()  { return g_GameMode == GameMode::SurvivalMode; }
