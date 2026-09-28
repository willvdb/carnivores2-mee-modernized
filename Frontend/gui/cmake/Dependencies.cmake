# Pinned, checksum-verified dependencies for the optional RmlUi GUI.
# Only reached when C2_FRONTEND_GUI=ON. The CLI configuration never includes
# this file, so it never needs FreeType, SDL3, RmlUi, a GL context or a display.
include(FetchContent)
if(POLICY CMP0135)
  # Re-extraction of an updated archive must rebuild dependent objects.
  cmake_policy(SET CMP0135 NEW)
endif()

# FreeType is RmlUi's default font engine. Linux and macOS take the system
# package, as the engine build already does. Windows runners have no system
# FreeType, so the default there is a pinned source build with every optional
# FreeType dependency disabled (no zlib/bzip2/png/harfbuzz/brotli).
if(WIN32)
  set(c2_gui_default_system_freetype OFF)
else()
  set(c2_gui_default_system_freetype ON)
endif()
option(C2_FRONTEND_GUI_SYSTEM_FREETYPE "Use an installed FreeType instead of the pinned source" ${c2_gui_default_system_freetype})
if(C2_FRONTEND_GUI_SYSTEM_FREETYPE)
  find_package(Freetype REQUIRED)
else()
  set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
  set(SKIP_INSTALL_ALL ON)
  # FreeType 2.13.3 declares cmake_minimum_required(3.0...3.5); CMake 4 needs
  # this floor to accept it. It does not alter FreeType's own behavior.
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  FetchContent_Declare(freetype
    URL https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.xz
    URL_HASH SHA256=0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289
  )
  FetchContent_MakeAvailable(freetype)
  unset(CMAKE_POLICY_VERSION_MINIMUM)
  if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
  endif()
  # RmlUi's soft find_package(Freetype) must not pick up a system copy; it
  # only needs the Freetype::Freetype target, which now is the pinned build.
  set(CMAKE_DISABLE_FIND_PACKAGE_Freetype TRUE)
  set(c2_gui_freetype_license "${freetype_SOURCE_DIR}/docs/FTL.TXT")
endif()

# SDL3: the same release and checksum the engine pins in cmake/SDL3.cmake, so
# one SDL source archive serves both. The engine's display-mode patches are
# deliberately not applied: they concern fullscreen mode-setting and output
# identity for the game window, and the GUI is a windowed desktop application.
option(C2_FRONTEND_GUI_SYSTEM_SDL3 "Use an installed SDL3 package for the GUI instead of the pinned source" OFF)
if(C2_FRONTEND_GUI_SYSTEM_SDL3)
  find_package(SDL3 3.2.0 CONFIG REQUIRED)
else()
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    URL https://www.libsdl.org/release/SDL3-3.2.28.tar.gz
    URL_HASH SHA256=1330671214d146f8aeb1ed399fc3e081873cdb38b5189d1f8bb6ab15bbc04211
  )
  FetchContent_MakeAvailable(SDL3)
endif()

# RmlUi 6.3: release tag 6.3, commit ba95ffe8bfb6370efb2cdcca927eaad4710c5413.
# The archive was verified to match that tag file-for-file. Built as static
# libraries; when included with add_subdirectory RmlUi skips its own install.
set(c2_gui_saved_build_shared_libs "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "" FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
set(RMLUI_TRACY_PROFILING OFF CACHE BOOL "" FORCE)
set(RMLUI_SVG_PLUGIN OFF CACHE BOOL "" FORCE)
set(RMLUI_LOTTIE_PLUGIN OFF CACHE BOOL "" FORCE)
# RmlUi's own install rules (headers, static libraries, package config) must
# not land in the frontend stage; only the executable and its assets do. The
# subtree is added EXCLUDE_FROM_ALL so those rules are skipped while the
# libraries still build as dependencies of c2-frontend-gui.
set(c2_gui_rmlui_url https://github.com/mikke89/RmlUi/archive/refs/tags/6.3.tar.gz)
set(c2_gui_rmlui_sha256 d977298bb6147610e5984d5db85ddf284020d655a8713913f6982074f1dbdede)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
  FetchContent_Declare(RmlUi URL "${c2_gui_rmlui_url}" URL_HASH "SHA256=${c2_gui_rmlui_sha256}" EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(RmlUi)
else()
  FetchContent_Declare(RmlUi URL "${c2_gui_rmlui_url}" URL_HASH "SHA256=${c2_gui_rmlui_sha256}")
  FetchContent_GetProperties(RmlUi)
  if(NOT rmlui_POPULATED)
    FetchContent_Populate(RmlUi)
    add_subdirectory("${rmlui_SOURCE_DIR}" "${rmlui_BINARY_DIR}" EXCLUDE_FROM_ALL)
  endif()
endif()
set(BUILD_SHARED_LIBS "${c2_gui_saved_build_shared_libs}")
unset(c2_gui_saved_build_shared_libs)

# RmlUi ships its SDL platform layer and OpenGL 3 renderer as sample backend
# sources, not as an installed library. Compile the two files from the pinned
# archive unmodified; application glue lives in this repository.
# The GL3 renderer carries its own glad loader (Backends/RmlUi_Include_GL3.h)
# and requires an OpenGL 3.3 core profile context.
add_library(c2_rmlui_backend_sdl_gl3 STATIC
  "${rmlui_SOURCE_DIR}/Backends/RmlUi_Platform_SDL.cpp"
  "${rmlui_SOURCE_DIR}/Backends/RmlUi_Renderer_GL3.cpp")
target_include_directories(c2_rmlui_backend_sdl_gl3 SYSTEM PUBLIC "${rmlui_SOURCE_DIR}/Backends")
target_compile_definitions(c2_rmlui_backend_sdl_gl3 PUBLIC RMLUI_SDL_VERSION_MAJOR=3)
target_compile_features(c2_rmlui_backend_sdl_gl3 PUBLIC cxx_std_17)
target_link_libraries(c2_rmlui_backend_sdl_gl3 PUBLIC RmlUi::Core SDL3::SDL3 ${CMAKE_DL_LIBS})
