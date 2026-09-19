# Native read-only metadata adapters; SDL still owns display/window operations.
find_package(X11 REQUIRED COMPONENTS Xrandr)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
set(identity_protocol_sources)
foreach(protocol IN ITEMS wlr-output-management xdg-output)
    set(xml "${CMAKE_CURRENT_SOURCE_DIR}/third_party/protocols/${protocol}-unstable-v1.xml")
    set(header "${CMAKE_CURRENT_BINARY_DIR}/${protocol}-client.h")
    set(code "${CMAKE_CURRENT_BINARY_DIR}/${protocol}-protocol.c")
    add_custom_command(OUTPUT "${header}" "${code}"
        COMMAND "${WAYLAND_SCANNER}" client-header "${xml}" "${header}"
        COMMAND "${WAYLAND_SCANNER}" private-code "${xml}" "${code}"
        DEPENDS "${xml}" VERBATIM)
    list(APPEND identity_protocol_sources "${header}" "${code}")
endforeach()
add_library(CarnivoresLinuxDisplayIdentity STATIC
    Hunt/Platform/DisplayIdentityX11.cpp Hunt/Platform/DisplayIdentityWayland.cpp
    ${identity_protocol_sources})
target_include_directories(CarnivoresLinuxDisplayIdentity PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(CarnivoresLinuxDisplayIdentity PUBLIC SDL3::SDL3 X11::Xrandr PkgConfig::WAYLAND_CLIENT)
# Keep generated interface symbols distinct from SDL's bundled protocol code.
# Only C symbols change; the protocol wire/interface names remain unchanged.
foreach(interface IN ITEMS zxdg_output_manager_v1 zxdg_output_v1 zwlr_output_manager_v1
        zwlr_output_head_v1 zwlr_output_mode_v1 zwlr_output_configuration_v1 zwlr_output_configuration_head_v1)
    target_compile_definitions(CarnivoresLinuxDisplayIdentity PRIVATE
        "${interface}_interface=carnivores_${interface}_interface")
endforeach()

# Asset-free native protocol fixtures are enabled with the existing Linux tests.
pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
foreach(protocol IN ITEMS wlr-output-management xdg-output)
        add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${protocol}-server.h"
            COMMAND "${WAYLAND_SCANNER}" server-header
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/protocols/${protocol}-unstable-v1.xml"
                "${CMAKE_CURRENT_BINARY_DIR}/${protocol}-server.h"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/protocols/${protocol}-unstable-v1.xml" VERBATIM)
endforeach()
