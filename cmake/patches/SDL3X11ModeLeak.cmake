include("${CMAKE_CURRENT_LIST_DIR}/SDL3PatchIO.cmake")
# Local modification to SDL 3.2.28; see docs/SDL_X11_MODE_LEAK.md.
# Use CMake alone so Windows builds need neither patch(1) nor a shell.
function(carnivores_patch_sdl3_x11_mode_leak source_dir)
    set(path "${source_dir}/src/video/x11/SDL_x11modes.c")
    set(original_sha a820dfef4e5abcea98dff946c00d66d29c29d0546ce9f5de32559f8dbd5f9691)
    set(patched_sha dbcf0804fb88f318dbb93993a6bdbfba5adfb7d48c89e6c24b77574f19648d0e)
    carnivores_read_sdl3_patch_source("${path}" source current_sha)
    if(current_sha STREQUAL 288ca05b537603ecdaf991939b1203cf839cbb28bd2d14c9ba4fd0cc4c579609 OR current_sha STREQUAL patched_sha OR current_sha STREQUAL 77dd5c6354cd73c3f9f72f43248f399466d97d9189f0d771efe692879865edac)
        return()
    endif()
    if(NOT current_sha STREQUAL original_sha)
        message(FATAL_ERROR
            "SDL X11 mode-leak patch: unexpected SDL_x11modes.c. "
            "Review the patch when changing SDL sources; do not silently omit it.")
    endif()

    set(before [=[    // update mode - this call takes ownership of display.desktop_mode.internal
    SDL_SetDesktopDisplayMode(existing_display, &display.desktop_mode);]=])
    set(after [=[    // Temporary fullscreen modes must not replace the saved desktop mode.
    if (existing_display->fullscreen_active) {
        SDL_free(display.desktop_mode.internal);
    } else {
        // This call takes ownership of display.desktop_mode.internal.
        SDL_SetDesktopDisplayMode(existing_display, &display.desktop_mode);
    }]=])
    string(REPLACE "${before}" "${after}" source "${source}")
    string(SHA256 result_sha "${source}")
    if(NOT result_sha STREQUAL patched_sha)
        message(FATAL_ERROR "SDL X11 mode-leak patch produced unexpected source")
    endif()
    file(WRITE "${path}" "${source}")
    message(STATUS "Applied SDL 3.2.28 X11 fullscreen mode ownership fix")
endfunction()
