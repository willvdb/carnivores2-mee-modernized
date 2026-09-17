# Pin source and checksum so the default dependency is reproducible. A packager
# can supply an installed SDL3 instead, without changing the target linkage.
option(CARNIVORES_SYSTEM_SDL3 "Use an installed SDL3 package" OFF)
if(CARNIVORES_SYSTEM_SDL3)
    find_package(SDL3 3.2.28 CONFIG REQUIRED)
else()
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        URL https://www.libsdl.org/release/SDL3-3.2.28.tar.gz
        URL_HASH SHA256=1330671214d146f8aeb1ed399fc3e081873cdb38b5189d1f8bb6ab15bbc04211
    )
    # SDL_mslibc.c deliberately disables /GL. Inheriting the game's Release
    # IPO would build SDL's PCH with /GL and produce MSVC C4652 on that file.
    # Compile the dependency without IPO; retain the game's existing LTO.
    set(carnivores_saved_release_ipo "${CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE}")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF)
    FetchContent_MakeAvailable(SDL3)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE "${carnivores_saved_release_ipo}")
    unset(carnivores_saved_release_ipo)
endif()
