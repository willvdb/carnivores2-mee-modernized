# Asset-free regression for the exact-source guards on both native newline forms.
include("${SOURCE_ROOT}/cmake/patches/SDL3X11ModeLeak.cmake")
include("${SOURCE_ROOT}/cmake/patches/SDL3WaylandDisconnect.cmake")
include("${SOURCE_ROOT}/cmake/patches/SDL3DisplayMapping.cmake")
include("${SOURCE_ROOT}/cmake/patches/SDL3X11DisabledOutputs.cmake")
if(APPLY_ONLY)
    carnivores_patch_sdl3_x11_mode_leak("${INPUT_ROOT}")
    carnivores_patch_sdl3_wayland_disconnect("${INPUT_ROOT}")
    carnivores_patch_sdl3_display_mapping("${INPUT_ROOT}")
    carnivores_patch_sdl3_x11_disabled_outputs("${INPUT_ROOT}")
    return()
endif()
foreach(newline IN ITEMS LF CRLF)
    set(fixture "${TEST_ROOT}/${newline}")
    foreach(relative IN ITEMS src/video/x11/SDL_x11modes.c
            src/video/wayland/SDL_waylandwindow.c src/video/wayland/SDL_waylandvideo.c)
        carnivores_read_sdl3_patch_source("${INPUT_ROOT}/${relative}" source ignored_sha)
        # file(CONFIGURE) can explicitly emit CRLF even on a Linux test runner.
        # Verify no accidental @variable@ expansion before using it as test I/O.
        string(CONFIGURE "${source}" configured @ONLY)
        if(NOT configured STREQUAL source)
            message(FATAL_ERROR "Fixture source contains configuration tokens")
        endif()
        get_filename_component(directory "${fixture}/${relative}" DIRECTORY)
        file(MAKE_DIRECTORY "${directory}")
        file(CONFIGURE OUTPUT "${fixture}/${relative}" CONTENT "${source}" @ONLY NEWLINE_STYLE ${newline})
    endforeach()
    foreach(pass RANGE 1 2)
        execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${SOURCE_ROOT}"
            "-DINPUT_ROOT=${fixture}" -DAPPLY_ONLY=ON -P "${CMAKE_CURRENT_LIST_FILE}"
            RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "${newline} patch pass ${pass} failed: ${output}${error}")
        endif()
    endforeach()
    file(APPEND "${fixture}/src/video/x11/SDL_x11modes.c" "\n// Deliberately unrecognized source alteration\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${SOURCE_ROOT}"
        "-DINPUT_ROOT=${fixture}" -DAPPLY_ONLY=ON -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(result EQUAL 0 OR NOT error MATCHES "unexpected")
        message(FATAL_ERROR "Modified ${newline} source was not rejected by exact-content guards")
    endif()
endforeach()
message(STATUS "SDL patch chain accepts LF/CRLF, is idempotent and rejects changed source")
