cmake_minimum_required(VERSION 3.20)
project(CarnivoresFrontend LANGUAGES CXX)
# Deliberately standalone: configure with -S Frontend, no engine dependencies.
add_library(c2_frontend_core STATIC native/src/core.cpp native/src/json_compat.cpp native/src/schema_compat.cpp native/src/manifest_schema.cpp native/src/store_paths.cpp native/src/store.cpp)
target_compile_features(c2_frontend_core PUBLIC cxx_std_17)
target_include_directories(c2_frontend_core PUBLIC native/include PRIVATE native/src)
target_include_directories(c2_frontend_core SYSTEM PRIVATE native/third_party)
add_executable(c2-frontend-native native/src/main.cpp)
target_link_libraries(c2-frontend-native PRIVATE c2_frontend_core)
foreach(target IN ITEMS c2_frontend_core c2-frontend-native)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endforeach()
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
  add_executable(c2-frontend-schema-tests native/tests/schema_tests.cpp)
  target_link_libraries(c2-frontend-schema-tests PRIVATE c2_frontend_core)
  target_include_directories(c2-frontend-schema-tests PRIVATE native/src)
  add_test(NAME frontend-schema-unicode-oracle COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_schema_unicode.py" --check)
  add_test(NAME frontend-schema-oracle COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_schema_fixtures.py" --check)
  add_test(NAME frontend-native-schema COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_schema.py" "$<TARGET_FILE:c2-frontend-schema-tests>")
  add_executable(c2-frontend-native-tests native/tests/compatibility_tests.cpp)
  target_link_libraries(c2-frontend-native-tests PRIVATE c2_frontend_core)
  target_include_directories(c2-frontend-native-tests PRIVATE native/src)
  if(MSVC)
    target_compile_options(c2-frontend-native-tests PRIVATE /W4 /utf-8)
  else()
    target_compile_options(c2-frontend-native-tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME frontend-compatibility-oracle
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_compatibility.py" --check)
  add_test(NAME frontend-native-compatibility COMMAND c2-frontend-native-tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/golden.json")
  add_test(NAME frontend-native-stack
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_native_stack.py"
      "$<TARGET_FILE:c2-frontend-native-tests>")
  add_test(NAME frontend-native-help COMMAND c2-frontend-native --help)
  set_tests_properties(frontend-native-help PROPERTIES PASS_REGULAR_EXPRESSION "Backend operations are not implemented")
  add_test(NAME frontend-native-version COMMAND c2-frontend-native --version)
  set_tests_properties(frontend-native-version PROPERTIES PASS_REGULAR_EXPRESSION "0.1.0-foundation")
  add_test(NAME frontend-native-unsupported COMMAND c2-frontend-native discover)
  set_tests_properties(frontend-native-unsupported PROPERTIES WILL_FAIL TRUE)
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
