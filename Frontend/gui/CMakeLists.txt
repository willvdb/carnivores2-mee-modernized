# Optional RmlUi + SDL3 + OpenGL GUI evaluation slice.
#
# Everything that needs a display server, an OpenGL context, SDL3, FreeType or
# RmlUi is gated on C2_FRONTEND_GUI (default OFF). With the option off this
# directory adds nothing that fetches or links those dependencies, so the CLI
# configuration, engine builds and the legacy Windows menu are unchanged.
if(C2_FRONTEND_GUI)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake")
endif()
