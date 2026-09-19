# Local mapping-property extension/backport for pinned SDL 3.2.28.
# No private ABI is consumed by the application. See docs/LINUX_DISPLAY_IDENTITY.md.
function(carnivores_patch_sdl3_display_mapping source_dir)
    set(path "${source_dir}/src/video/x11/SDL_x11modes.c")
    set(original_sha dbcf0804fb88f318dbb93993a6bdbfba5adfb7d48c89e6c24b77574f19648d0e)
    set(patched_sha 77dd5c6354cd73c3f9f72f43248f399466d97d9189f0d771efe692879865edac)
    file(SHA256 "${path}" current_sha)
    if(NOT current_sha STREQUAL patched_sha)
        if(NOT current_sha STREQUAL original_sha)
            message(FATAL_ERROR "SDL display mapping patch: unexpected source ${path}")
        endif()
        file(READ "${path}" source)
        set(before [=[
    if (SDL_AddVideoDisplay(&display, send_event) == 0) {
        return false;
    }

]=])
        set(after [=[
    const SDL_DisplayID id = SDL_AddVideoDisplay(&display, send_event);
    if (id == 0) {
        return false;
    }

    // Carnivores extension: borrowed mapping only, never a persistent identity.
    const SDL_PropertiesID props = SDL_GetDisplayProperties(id);
    SDL_SetPointerProperty(props, "Carnivores.display.x11.connection", dpy);
    SDL_SetNumberProperty(props, "Carnivores.display.x11.output", outputid);
    SDL_SetNumberProperty(props, "Carnivores.display.x11.root", RootWindow(dpy, screen));

]=])
        string(REPLACE "${before}" "${after}" source "${source}")
        string(SHA256 result_sha "${source}")
        if(NOT result_sha STREQUAL patched_sha)
            message(FATAL_ERROR "SDL display mapping patch produced unexpected source ${path}")
        endif()
        file(WRITE "${path}" "${source}")
    endif()
    set(path "${source_dir}/src/video/wayland/SDL_waylandvideo.c")
    set(original_sha 86c292d336b7c9010cc16c0308e6c1ca7732dc6e3ea7ac9486ca27e94ff33bec)
    set(patched_sha 5d56fbb6d4e882e2ff163549d0d75949bb28818451b87f8bdaab670749d7bd0d)
    file(SHA256 "${path}" current_sha)
    if(NOT current_sha STREQUAL patched_sha)
        if(NOT current_sha STREQUAL original_sha)
            message(FATAL_ERROR "SDL display mapping patch: unexpected source ${path}")
        endif()
        file(READ "${path}" source)
        set(before [=[            internal->display = SDL_AddVideoDisplay(&internal->placeholder, true);
            SDL_free(internal->placeholder.name);
]=])
        set(after [=[            internal->display = SDL_AddVideoDisplay(&internal->placeholder, true);
            // Backport the public SDL Wayland output mapping property.
            SDL_SetPointerProperty(SDL_GetDisplayProperties(internal->display),
                                   "SDL.display.wayland.wl_output", internal->output);
            SDL_free(internal->placeholder.name);
]=])
        string(REPLACE "${before}" "${after}" source "${source}")
        set(before [=[        d->display = SDL_AddVideoDisplay(&d->placeholder, false);
        SDL_free(d->placeholder.name);
]=])
        set(after [=[        d->display = SDL_AddVideoDisplay(&d->placeholder, false);
        // Backport the public SDL Wayland output mapping property.
        SDL_SetPointerProperty(SDL_GetDisplayProperties(d->display),
                               "SDL.display.wayland.wl_output", d->output);
        SDL_free(d->placeholder.name);
]=])
        string(REPLACE "${before}" "${after}" source "${source}")
        string(SHA256 result_sha "${source}")
        if(NOT result_sha STREQUAL patched_sha)
            message(FATAL_ERROR "SDL display mapping patch produced unexpected source ${path}")
        endif()
        file(WRITE "${path}" "${source}")
    endif()
endfunction()
