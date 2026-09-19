include("${CMAKE_CURRENT_LIST_DIR}/SDL3PatchIO.cmake")
# Local SDL 3.2.28 correction; see docs/SDL_X11_DISABLED_OUTPUTS.md.
function(carnivores_patch_sdl3_x11_disabled_outputs source_dir)
    set(path "${source_dir}/src/video/x11/SDL_x11modes.c")
    set(original_sha 77dd5c6354cd73c3f9f72f43248f399466d97d9189f0d771efe692879865edac)
    set(patched_sha 288ca05b537603ecdaf991939b1203cf839cbb28bd2d14c9ba4fd0cc4c579609)
    carnivores_read_sdl3_patch_source("${path}" source current_sha)
    if(current_sha STREQUAL patched_sha)
        return()
    endif()
    if(NOT current_sha STREQUAL original_sha)
        message(FATAL_ERROR "SDL X11 disabled-output patch: unexpected source ${path}")
    endif()
    set(before [=[                    // This display is active, remove it from the list
                    displays[i] = 0;
                    break;]=])
    set(after [=[                    // A connected connector may have no active CRTC. Query
                    // current state, not the queued event: our own mode switch
                    // temporarily disables a CRTC before enabling it again.
                    XRROutputInfo *info = X11_XRRGetOutputInfo(dpy, res, res->outputs[output]);
                    if (info && info->connection != RR_Disconnected && info->crtc) {
                        displays[i] = 0;
                    }
                    if (info) {
                        X11_XRRFreeOutputInfo(info);
                    }
                    break;]=])
    string(REPLACE "${before}" "${after}" source "${source}")
    string(SHA256 result_sha "${source}")
    if(NOT result_sha STREQUAL patched_sha)
        message(FATAL_ERROR "SDL X11 disabled-output patch produced unexpected source")
    endif()
    file(WRITE "${path}" "${source}")
endfunction()
