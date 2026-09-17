cmake_minimum_required(VERSION 3.15)
project(Carnivores1)

# Enable modern MSVC runtime selection
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Renderer selection: SOFT or GL.
# Leave empty (unset) to build only the standalone menu executable.
set(RENDERER "" CACHE STRING "Select the renderer: SOFT or GL. Leave empty to build the menu only.")
set_property(CACHE RENDERER PROPERTY STRINGS SOFT GL "")

# Named presets select an ABI and validate the compiler environment. Ninja's
# architecture metadata alone does not switch an already active MSVC toolchain.
set(CARNIVORES_ARCH "" CACHE STRING "Expected Windows MSVC target: x86 or x64")
set_property(CACHE CARNIVORES_ARCH PROPERTY STRINGS "" x86 x64)
if(CARNIVORES_ARCH OR RENDERER STREQUAL "SOFT")
    if(NOT WIN32 OR NOT MSVC)
        message(FATAL_ERROR "Architecture presets and the software renderer require Windows MSVC.")
    endif()
    include(CheckCXXSourceCompiles)
    if(CARNIVORES_ARCH STREQUAL "x86" OR RENDERER STREQUAL "SOFT")
        check_cxx_source_compiles("#ifndef _M_IX86
#error Expected MSVC x86
#endif
static_assert(sizeof(void*) == 4, \"Expected x86 pointers\");
int main() { return 0; }" CARNIVORES_COMPILER_IS_X86)
        if(NOT CARNIVORES_COMPILER_IS_X86)
            message(FATAL_ERROR "This configuration requires Windows x86 MSVC. SOFT is Windows x86-only; use an x64 OpenGL preset for x64.")
        endif()
    endif()
    if(CARNIVORES_ARCH STREQUAL "x64")
        check_cxx_source_compiles("#ifndef _M_X64
#error Expected MSVC x64
#endif
static_assert(sizeof(void*) == 8, \"Expected x64 pointers\");
int main() { return 0; }" CARNIVORES_COMPILER_IS_X64)
        if(NOT CARNIVORES_COMPILER_IS_X64)
            message(FATAL_ERROR "This configuration requires Windows x64 MSVC. Activate an x64 developer environment.")
        endif()
    elseif(CARNIVORES_ARCH AND NOT CARNIVORES_ARCH STREQUAL "x86")
        message(FATAL_ERROR "Unknown CARNIVORES_ARCH='${CARNIVORES_ARCH}'; expected x86 or x64.")
    endif()
endif()

# Enable LTO for Release builds (cross-module inlining, ~5-15% CPU gain)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

# Phase 0.1 in-engine GL perf harness. Defaults OFF. The ogl-release
# preset turns this ON because perf measurements must run on the
# optimized shipping build (debug builds are dominated by _DEBUG and
# MEM_DEBUG overhead, so the numbers are useless). To produce a
# shippable build without the harness, reconfigure with
# -DGL_PERF_HOOKS=OFF or build a different preset. Has no effect on
# SOFT builds.
option(GL_PERF_HOOKS "Enable in-engine GL performance harness (Phase 0.1)" OFF)
option(MEM_DEBUG "Enable game allocation tracking and leak reporting" OFF)

# Renderer and game-only definitions are applied to the game target below;
# keeping them target-scoped prevents the standalone menu from inheriting
# _soft/_gl, GL_PERF_HOOKS, or MEM_DEBUG.
set(RENDER_DEFINES "")
set(GAME_DEBUG_DEFINES "")

# Windows compiler settings shared by x86 and x64.
if(MSVC)

    # Relax compiler for legacy code
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-D_CRT_NONSTDC_NO_DEPRECATE)
    add_definitions(-D_WINSOCK_DEPRECATED_NO_WARNINGS)
    add_definitions(-DWIN32)
    add_definitions(-DNOMINMAX)

    # Fix for some legacy headers and scoping
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP /permissive")

    # Enable memory debugging for Debug builds by default, or explicitly for
    # any configuration through -DMEM_DEBUG=ON. Keep it game-target scoped;
    # the standalone menu intentionally remains outside MEM_DEBUG.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR MEM_DEBUG)
        list(APPEND GAME_DEBUG_DEFINES MEM_DEBUG)
    endif()
endif()

# Renderer-specific source and library configuration. Skipped entirely when
# RENDERER is empty (menu-only builds).
set(RENDER_SRC "")

if(RENDERER)
    if(RENDERER STREQUAL "SOFT")
        list(APPEND RENDER_DEFINES _soft)
        set(RENDER_SRC
            Hunt/Renderer/renderasm.cpp
            Hunt/Renderer/RenderSoft.cpp
            Hunt/Renderer/SoftInternal.h
            Hunt/Renderer/SoftHUD.cpp
            Hunt/Renderer/SoftModel.cpp
            Hunt/Renderer/SoftTerrain.cpp
            Hunt/Renderer/SoftWater.cpp
            Hunt/Renderer/SoftEntities.cpp
            Hunt/Renderer/SoftVideo.cpp
            Hunt/Renderer/SoftSky.cpp
            Hunt/Renderer/SoftMisc.cpp
            Hunt/Renderer/SoftRenderer.cpp
            Hunt/Renderer/SoftRenderer.h
        )
        message(STATUS "Building with Software Renderer")
    elseif(RENDERER STREQUAL "GL")
        list(APPEND RENDER_DEFINES _gl)
        if(GL_PERF_HOOKS)
            list(APPEND RENDER_DEFINES GL_PERF_HOOKS)
        endif()
        set(RENDER_SRC
            Hunt/Renderer/IRenderer.h
            Hunt/Renderer/GLRenderer.h
            Hunt/Renderer/GLRenderer.cpp
            Hunt/Renderer/GLUtils.cpp
            Hunt/Renderer/GLFog.cpp
            Hunt/Renderer/GLNight.cpp
            Hunt/Renderer/GLShadow.cpp
            Hunt/Renderer/GLSky.cpp
            Hunt/Renderer/GLHUD.cpp
            Hunt/Renderer/GLModel.cpp
            Hunt/Renderer/GLTerrain.cpp
            Hunt/Renderer/GLWater.cpp
            Hunt/Renderer/GLShader.h
            Hunt/Renderer/GLShader.cpp
            Hunt/Renderer/GLPerf.h
            Hunt/Renderer/GLPerf.cpp
            Hunt/Renderer/GLUI.cpp
            deps/glad/glad.c
        )
        if(GL_PERF_HOOKS)
            message(STATUS "Building with OpenGL Renderer (GL_PERF_HOOKS=ON)")
        else()
            message(STATUS "Building with OpenGL Renderer")
        endif()
    else()
        message(FATAL_ERROR "Unknown RENDERER='${RENDERER}'. Expected SOFT, GL, or empty for menu-only builds.")
    endif()
else()
    message(STATUS "RENDERER is empty - building menu only (no game executable)")
endif()

# Set C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Core source files (excluding renderers)
set(CORE_FILES
    Hunt/Audio/Audio_DLL.cpp
    Hunt/Audio/OpenAL_Loader.cpp
    Hunt/Game/Characters.cpp
    Hunt/Game/CharacterCollision.cpp
    Hunt/Game/CharacterTargeting.cpp
    Hunt/Game/CharacterMovement.cpp
    Hunt/Game/CharacterMorph.cpp
    Hunt/Game/CharacterSpawn.cpp
    Hunt/Game/CharacterAI.cpp
    Hunt/Game/CharacterTracking.cpp
    Hunt/Game/CharacterAnimation.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Hunt/Game/CharacterAnimation/AnimateHuntDead.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Hunt/Game/CharacterAnimation/AnimateDeadCommon.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Hunt/Game/CharacterAnimation/AnimateTitan.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Hunt/Game/CharacterAnimation/AnimatePoacher.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Hunt/Game/CharacterAnimation/AnimateHuntable.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Hunt/Game/CharacterAnimation/AnimateMicro.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Hunt/Game/CharacterAnimation/huntDogSearch.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Hunt/Game/CharacterAnimation/AnimateHuntdog.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Hunt/Game/CharacterAnimation/AnimateTRex.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Hunt/Game/CharacterAnimation/AnimateMClientCharacter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Hunt/Game/CharacterAnimation/AnimateClassicAmbient.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Hunt/Game/CharacterAnimation/AnimateFish.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Hunt/Game/CharacterAnimation/AnimateIcth.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Hunt/Game/CharacterAnimation/AnimateDeadFish.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Hunt/Game/CharacterAnimation/AnimateIcthDead.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Hunt/Game/CharacterAnimation/AnimateBrahi.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Hunt/Game/CharacterAnimation/AnimateBrahiOld.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Hunt/Game/CharacterAnimation/AnimateDimor.cpp
    Hunt/Game/Controls.cpp
    Hunt/Game/PlayerMovement.cpp
    Hunt/Game/Effects.cpp
    Hunt/Game/Network.cpp
    Hunt/Network/NetworkManager.cpp
    Hunt/Debug/Log.cpp
    Hunt/Game/TerrainQueries.cpp
    Hunt/Game/Ship.cpp
    Hunt/Game/CommandLine.cpp
    Hunt/Game/Config.cpp
    Hunt/Game/EngineInit.cpp
    Hunt/Game/Projectiles.cpp
    Hunt/Game/Ships.cpp
    Hunt/Game/Trophy.cpp
    Hunt/Game/Hunt.cpp
    Hunt/Game/Interface.cpp
    Hunt/Math/Math.cpp
    Hunt/Math/Vector.cpp
    Hunt/Loaders/Resources.cpp
    Hunt/Loaders/ScriptParser.cpp
    Hunt/Loaders/ModelLoader.cpp
    Hunt/Loaders/MapLoader.cpp
    Hunt/Loaders/SoundLoader.cpp
    Hunt/Loaders/PictureLoader.cpp
    Hunt/Loaders/CharacterLoader.cpp
    Hunt/Renderer/UIText.cpp
    Hunt/Renderer/UIText.h
    Hunt/AppRes.rc
    Hunt/Carnivores1.manifest
    # Headers (optional, for IDE)
    Hunt/Core/StateDefs.cpp
    Hunt/Hunt.h
    Hunt/Core/Constants.h
    Hunt/Core/MathTypes.h
    Hunt/Core/AudioTypes.h
    Hunt/Core/RenderTypes.h
    Hunt/Core/ModelTypes.h
    Hunt/Core/GameTypes.h
    Hunt/Core/GameState.h
    Hunt/Core/GameMode.h
    Hunt/Core/ConfigText.h
    Hunt/Core/Version.h
    Hunt/Renderer/RenderContext.h
    Hunt/Renderer/RenderContext.cpp
    Hunt/AppRes.h
)


# ============================================================
# Performance: Optimization flags for Release builds
# ============================================================
if(MSVC AND CMAKE_BUILD_TYPE STREQUAL "Release")
    # /GL + /LTCG = Link-Time Optimization
    # /O2 = Maximize Speed, /Oi = Intrinsics, /Ot = Favor Fast Code, /Ob2 = Aggressive Inlining
    # /Gw = Optimize Globals, /GS- = No Buffer Checks, /fp:fast = Fast Float, /arch:SSE2 = SSE2
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /GL /O2 /Oi /Ot /Ob2 /Gw /GS- /fp:fast /Zc:inline")
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        string(APPEND CMAKE_CXX_FLAGS_RELEASE " /arch:SSE2")
    endif()
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
endif()

# Menu subdirectory (always built - renderer-agnostic)
add_subdirectory(Menu)

# Game executable (only when a renderer is selected)
if(RENDERER)
    add_executable(Carnivores1 WIN32 ${CORE_FILES} ${RENDER_SRC})

    target_compile_definitions(Carnivores1 PRIVATE
        ${RENDER_DEFINES}
        ${GAME_DEBUG_DEFINES}
    )

    # Include directories
    target_include_directories(Carnivores1 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Hunt
        ${CMAKE_CURRENT_SOURCE_DIR}/deps
    )

    # Link libraries
    target_link_libraries(Carnivores1 PRIVATE
        kernel32
        user32
        gdi32
        winmm
        ddraw
        opengl32
    )

    # Output directory for the executable
    set_target_properties(Carnivores1 PROPERTIES
        OUTPUT_NAME "Carnivores1_${RENDERER}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    # Copy shader files to the output directory (for the GL renderer)
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/shaders DESTINATION ${CMAKE_BINARY_DIR}/bin)
endif()

# ============================================================
# Testing
# ============================================================
if(MSVC)
    enable_testing()
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/release-1.12.1.zip
    )
    set(FETCHCONTENT_QUIET OFF)
    FetchContent_MakeAvailable(googletest)

    add_executable(Carnivores2Tests
        tests/test_billboard_math.cpp
        tests/test_math.cpp
        tests/test_trophy_mode.cpp
        tests/test_key_bindings.cpp
        tests/test_load_validate.cpp
        tests/test_map_scaling.cpp
        tests/test_memory_arena.cpp
        tests/test_menu_slider.cpp
        tests/test_serialized_layout.cpp
        tests/test_legacy_profile.cpp
        tests/test_projectile_math.cpp
        tests/test_terrain_fog.cpp
        tests/test_water_object_visibility.cpp
    )
    target_include_directories(Carnivores2Tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Hunt
        ${CMAKE_CURRENT_SOURCE_DIR}/Hunt/Core
        ${CMAKE_CURRENT_SOURCE_DIR}/Menu
    )
    target_link_libraries(Carnivores2Tests PRIVATE gtest_main)
    add_test(NAME Carnivores2Tests COMMAND Carnivores2Tests)

    # Compile the same allocator-boundary probes with and without debug forwarding.
    foreach(memory_mode IN ITEMS Plain Debug)
        set(memory_test_target Carnivores2Memory${memory_mode}Tests)
        add_executable(${memory_test_target} tests/test_memory_factories.cpp)
        target_include_directories(${memory_test_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Hunt)
        target_link_libraries(${memory_test_target} PRIVATE gtest_main)
        if(memory_mode STREQUAL "Debug")
            target_compile_definitions(${memory_test_target} PRIVATE MEM_DEBUG)
        endif()
        add_test(NAME Carnivores2Tests.Memory${memory_mode} COMMAND ${memory_test_target})
    endforeach()

    # Menu and engine headers define different global types. Keep the menu
    # record checks in a separate executable and compare against the same bytes.
    add_executable(Carnivores2MenuLayoutTests tests/test_menu_serialized_layout.cpp)
    target_link_libraries(Carnivores2MenuLayoutTests PRIVATE gtest_main)
    add_test(NAME Carnivores2Tests.MenuLayout COMMAND Carnivores2MenuLayoutTests)

    # Exercise the production world-model sampler without a GL context.
    # Engine globals / CalcFogLevel are controlled test doubles in this target.
    add_executable(Carnivores2FogTests
        tests/test_fog_sampling.cpp
        Hunt/Renderer/GLFog.cpp
    )
    target_include_directories(Carnivores2FogTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Hunt)
    target_link_libraries(Carnivores2FogTests PRIVATE gtest_main)
    add_test(NAME Carnivores2Tests.FogSampling COMMAND Carnivores2FogTests)

    # Exercise the production box-text layout without a renderer. Only WinH and
    # UIScale are needed as test doubles for Hunt/Renderer/UIText.cpp.
    add_executable(Carnivores2UITests
        tests/test_ui_text.cpp
        Hunt/Renderer/UIText.cpp
    )
    target_include_directories(Carnivores2UITests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Hunt)
    target_link_libraries(Carnivores2UITests PRIVATE gtest_main gdi32)
    add_test(NAME Carnivores2Tests.UIText COMMAND Carnivores2UITests)

    # SmokeTest only runs when a game executable is built (not menu-only)
    if(TARGET Carnivores1)
        add_test(NAME SmokeTest
            COMMAND powershell -ExecutionPolicy Bypass -File
                ${CMAKE_SOURCE_DIR}/tests/smoke_test.ps1
                -Renderer ${RENDERER}
                -Duration 5
        )
    endif()
endif()
