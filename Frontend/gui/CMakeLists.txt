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
  model/src/loop.cpp
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
  c2_frontend_gui_test(c2-frontend-gui-loop-tests tests/loop_tests.cpp)
  add_test(NAME frontend-gui-loop COMMAND c2-frontend-gui-loop-tests)
  c2_frontend_gui_test(c2-frontend-gui-model-tests tests/model_tests.cpp)
  add_test(NAME frontend-gui-model COMMAND c2-frontend-gui-model-tests)
  c2_frontend_gui_test(c2-frontend-gui-store-tests tests/store_tests.cpp)
  add_test(NAME frontend-gui-store COMMAND c2-frontend-gui-store-tests)
  c2_frontend_gui_test(c2-frontend-gui-worker-tests tests/worker_tests.cpp)
  add_test(NAME frontend-gui-worker COMMAND c2-frontend-gui-worker-tests)
endif()

if(C2_FRONTEND_GUI)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake")

  add_executable(c2-frontend-gui
    app/main.cpp app/app.cpp app/screens.cpp app/gamepad.cpp app/resources.cpp
    app/render_png.cpp app/capture.cpp app/self_test.cpp)
  target_link_libraries(c2-frontend-gui PRIVATE c2_frontend_gui_model c2_rmlui_backend_sdl_gl3)
  target_include_directories(c2-frontend-gui SYSTEM PRIVATE third_party/stb)
  target_compile_features(c2-frontend-gui PRIVATE cxx_std_17)
  # Development fallback only: an installed binary finds its assets beside
  # itself (../share/c2-frontend-gui) and never needs the source checkout.
  target_compile_definitions(c2-frontend-gui PRIVATE
    "C2_FRONTEND_GUI_SOURCE_ASSETS=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\"")
  if(MSVC)
    target_compile_options(c2-frontend-gui PRIVATE /W4 /utf-8)
    set_target_properties(c2-frontend-gui PROPERTIES WIN32_EXECUTABLE ON)
  else()
    target_compile_options(c2-frontend-gui PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  # Staged layout: bin/c2-frontend-gui finds share/c2-frontend-gui beside its
  # own directory, so the install runs from any working directory without the
  # source checkout. Notices for everything redistributed travel with it.
  install(TARGETS c2-frontend-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(DIRECTORY assets/ DESTINATION ${CMAKE_INSTALL_DATADIR}/c2-frontend-gui)
  set(c2_gui_licenses ${CMAKE_INSTALL_DATADIR}/c2-frontend-gui/licenses)
  install(FILES "${rmlui_SOURCE_DIR}/LICENSE.txt" DESTINATION ${c2_gui_licenses} RENAME LICENSE-RmlUi.txt)
  install(FILES third_party/stb/LICENSE.txt DESTINATION ${c2_gui_licenses} RENAME LICENSE-stb_image.txt)
  install(FILES third_party/README.md DESTINATION ${c2_gui_licenses} RENAME THIRD-PARTY-gui.md)
  if(NOT C2_FRONTEND_GUI_SYSTEM_SDL3)
    install(FILES "${sdl3_SOURCE_DIR}/LICENSE.txt" DESTINATION ${c2_gui_licenses} RENAME LICENSE-SDL3.txt)
  endif()
  if(c2_gui_freetype_license)
    install(FILES "${c2_gui_freetype_license}" DESTINATION ${c2_gui_licenses} RENAME LICENSE-FreeType-FTL.txt)
  endif()

  if(BUILD_TESTING)
    # Real RmlUi documents driven through the app's own dispatch path. The
    # offscreen SDL driver supplies an OpenGL context without a display.
    add_test(NAME frontend-gui-self-test COMMAND c2-frontend-gui --self-test)
    set_tests_properties(frontend-gui-self-test PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=offscreen")
    add_test(NAME frontend-gui-help COMMAND c2-frontend-gui --help)
    set_tests_properties(frontend-gui-help PROPERTIES PASS_REGULAR_EXPRESSION "--self-test")
  endif()
endif()
