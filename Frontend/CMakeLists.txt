cmake_minimum_required(VERSION 3.20)
project(CarnivoresFrontend LANGUAGES CXX)
# Deliberately standalone: configure with -S Frontend, no engine dependencies.
add_executable(c2-profile-probe profile_probe.cpp)
target_compile_features(c2-profile-probe PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(c2-profile-probe PRIVATE /W4)
else()
  target_compile_options(c2-profile-probe PRIVATE -Wall -Wextra -Wpedantic)
endif()
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
add_custom_target(c2-frontend
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/frontend.py" --help
  DEPENDS c2-profile-probe
  COMMENT "Show the optional frontend CLI")
include(CTest)
if(BUILD_TESTING)
  # Native test child, separate from the retained fixed Python fixture. No SDL,
  # game assets or gameplay dependencies; only the production engine I/O seam.
  add_executable(c2-native-session-fixture tests/native_session_fixture.cpp
    ../Hunt/Session/Session.cpp ../Hunt/Platform/Files.cpp)
  target_compile_features(c2-native-session-fixture PRIVATE cxx_std_17)
  # Compile the actual consumer, never a Python mirror of its mask formula.
  set(commandline "${CMAKE_CURRENT_SOURCE_DIR}/../Hunt/Game/CommandLine.cpp")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${commandline}")
  file(READ "${commandline}" commandline_source)
  string(FIND "${commandline_source}" "static bool equals_nocase" commandline_start)
  if(commandline_start LESS 0)
    message(FATAL_ERROR "Cannot locate production argument consumer")
  endif()
  string(SUBSTRING "${commandline_source}" ${commandline_start} -1 commandline_body)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/launch_commandline.inc" "${commandline_body}")
  add_executable(c2-launch-argument-probe tests/launch_argument_probe.cpp)
  target_compile_features(c2-launch-argument-probe PRIVATE cxx_std_17)
  target_include_directories(c2-launch-argument-probe PRIVATE ../Hunt "${CMAKE_CURRENT_BINARY_DIR}")
  add_test(NAME frontend-backend
    COMMAND "${CMAKE_COMMAND}" -E env
      "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}"
      "C2_PROFILE_PROBE=$<TARGET_FILE:c2-profile-probe>"
      "C2_LAUNCH_ARGUMENT_PROBE=$<TARGET_FILE:c2-launch-argument-probe>"
      "C2_NATIVE_TEST_ENGINE=$<TARGET_FILE:c2-native-session-fixture>"
      "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_tests.py")
endif()
