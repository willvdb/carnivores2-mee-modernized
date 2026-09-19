include("${CMAKE_CURRENT_LIST_DIR}/SDL3PatchIO.cmake")
# Local SDL 3.2.28 correction; see docs/SDL_WAYLAND_DISCONNECT.md.
# Use CMake alone so Windows configuration needs no patch utility or shell.
function(carnivores_patch_sdl3_wayland_disconnect source_dir)
    set(path "${source_dir}/src/video/wayland/SDL_waylandwindow.c")
    set(original_sha 1632b3bc5ba112194032d14eebcd4950113f92b7f65b41930d81cbaa29a65255)
    set(patched_sha dd415a9d7b6db9f9de6f81a319151a9d99f0ad776cb7093e9ecc1813eea3601b)
    carnivores_read_sdl3_patch_source("${path}" source current_sha)
    if(current_sha STREQUAL patched_sha)
        return()
    endif()
    if(NOT current_sha STREQUAL original_sha)
        message(FATAL_ERROR "SDL Wayland disconnect patch: unexpected SDL_waylandwindow.c; review dependency changes")
    endif()
    set(before [=[        WAYLAND_wl_display_roundtrip(window->internal->waylandData->display);]=])
    set(after [=[        if (WAYLAND_wl_display_roundtrip(window->internal->waylandData->display) < 0) {
            SDL_SetError("Wayland connection lost while flushing window state");
            break;
        }]=])
    string(REPLACE "${before}" "${after}" source "${source}")
    set(before [=[    do {
        WAYLAND_wl_display_roundtrip(_this->internal->display);
    } while (wind->fullscreen_deadline_count || wind->maximized_restored_deadline_count);]=])
    set(after [=[    do {
        if (WAYLAND_wl_display_roundtrip(_this->internal->display) < 0) {
            return SDL_SetError("Wayland connection lost while synchronizing window state");
        }
    } while (wind->fullscreen_deadline_count || wind->maximized_restored_deadline_count);]=])
    string(REPLACE "${before}" "${after}" source "${source}")
    string(SHA256 result_sha "${source}")
    if(NOT result_sha STREQUAL patched_sha)
        message(FATAL_ERROR "SDL Wayland disconnect patch produced unexpected source")
    endif()
    file(WRITE "${path}" "${source}")
    message(STATUS "Applied SDL 3.2.28 Wayland dead-connection wait fix")
endfunction()
