# Optional RmlUi + SDL3 + OpenGL GUI evaluation slice.
#
# Layering (dependency direction, top to bottom):
#   RML/RCSS screens -> RmlUi binding layer (app/) -> headless presentation
#   model (model/) -> c2_frontend_core
#
# The headless model always builds: it has no SDL, OpenGL or RmlUi types and
# its tests run in the ordinary CLI configuration. Everything that needs a
# display server, an OpenGL context, SDL3, FreeType or RmlUi is gated on
# C2_FRONTEND_GUI (default OFF), so the CLI configuration, engine builds and
# the legacy Windows menu are unchanged.

add_library(c2_frontend_gui_model STATIC
  model/src/source.cpp
  model/src/demo_store.cpp
  model/src/worker.cpp
  model/src/presentation.cpp)
target_include_directories(c2_frontend_gui_model PUBLIC model/include)
target_link_libraries(c2_frontend_gui_model PUBLIC c2_frontend_core PRIVATE Threads::Threads)
target_compile_features(c2_frontend_gui_model PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(c2_frontend_gui_model PRIVATE /W4 /utf-8)
else()
  target_compile_options(c2_frontend_gui_model PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  function(c2_frontend_gui_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE c2_frontend_gui_model Threads::Threads)
    if(MSVC)
      target_compile_options(${name} PRIVATE /W4 /utf-8)
    else()
      target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endfunction()
  c2_frontend_gui_test(c2-frontend-gui-model-tests tests/model_tests.cpp)
  add_test(NAME frontend-gui-model COMMAND c2-frontend-gui-model-tests)
  c2_frontend_gui_test(c2-frontend-gui-store-tests tests/store_tests.cpp)
  add_test(NAME frontend-gui-store COMMAND c2-frontend-gui-store-tests)
  c2_frontend_gui_test(c2-frontend-gui-worker-tests tests/worker_tests.cpp)
  add_test(NAME frontend-gui-worker COMMAND c2-frontend-gui-worker-tests)
endif()

if(C2_FRONTEND_GUI)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake")
endif()
