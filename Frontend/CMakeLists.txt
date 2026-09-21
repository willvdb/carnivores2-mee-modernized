cmake_minimum_required(VERSION 3.20)
project(CarnivoresFrontend LANGUAGES CXX)
# Deliberately standalone: configure with -S Frontend, no engine dependencies.
set(c2_frontend_core_sources native/src/core.cpp native/src/json_compat.cpp native/src/schema_compat.cpp native/src/manifest_schema.cpp native/src/store_paths.cpp native/src/store.cpp native/src/capture.cpp native/src/profile.cpp native/src/profile_files.cpp native/src/content.cpp native/src/discovery.cpp native/src/catalog.cpp native/src/catalog_projection.cpp native/src/planning.cpp native/src/launch_planning.cpp native/src/store_write.cpp native/src/probe_process.cpp native/src/session_journal.cpp native/src/planning_store.cpp)
add_library(c2_frontend_core STATIC ${c2_frontend_core_sources})
target_compile_features(c2_frontend_core PUBLIC cxx_std_17)
target_include_directories(c2_frontend_core PUBLIC native/include PRIVATE native/src)
target_include_directories(c2_frontend_core SYSTEM PRIVATE native/third_party)
if(WIN32)
  # BCryptGenRandom backs new_id and temporary names.
  target_link_libraries(c2_frontend_core PRIVATE bcrypt)
endif()
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
  add_executable(c2-frontend-catalog-tests native/tests/catalog_tests.cpp)
  target_link_libraries(c2-frontend-catalog-tests PRIVATE c2_frontend_core)
  target_include_directories(c2-frontend-catalog-tests PRIVATE native/src)
  if(MSVC)
    target_compile_options(c2-frontend-catalog-tests PRIVATE /W4 /utf-8)
  else()
    target_compile_options(c2-frontend-catalog-tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME frontend-catalog-oracle COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_catalog_fixtures.py" --check)
  add_test(NAME frontend-native-catalog COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_catalog.py" "$<TARGET_FILE:c2-frontend-catalog-tests>")
  add_executable(c2-frontend-catalog-projection-tests native/tests/catalog_projection_tests.cpp)
  target_link_libraries(c2-frontend-catalog-projection-tests PRIVATE c2_frontend_core)
  target_include_directories(c2-frontend-catalog-projection-tests PRIVATE native/src)
  if(MSVC)
    target_compile_options(c2-frontend-catalog-projection-tests PRIVATE /W4 /utf-8)
  else()
    target_compile_options(c2-frontend-catalog-projection-tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME frontend-native-catalog-projection COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_catalog_projection.py" "$<TARGET_FILE:c2-frontend-catalog-projection-tests>")
  add_executable(c2-frontend-probe-process-tests native/tests/probe_process_tests.cpp)
  target_link_libraries(c2-frontend-probe-process-tests PRIVATE c2_frontend_core)
  target_include_directories(c2-frontend-probe-process-tests PRIVATE native/src)
  if(MSVC)
    target_compile_options(c2-frontend-probe-process-tests PRIVATE /W4 /utf-8)
  else()
    target_compile_options(c2-frontend-probe-process-tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME frontend-native-probe-process COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_probe_process.py" "$<TARGET_FILE:c2-frontend-probe-process-tests>" "$<TARGET_FILE:c2-profile-probe>")
  add_executable(c2-frontend-session-journal-tests native/tests/session_journal_tests.cpp)
  target_link_libraries(c2-frontend-session-journal-tests PRIVATE c2_frontend_core)
  target_include_directories(c2-frontend-session-journal-tests PRIVATE native/src)
  if(MSVC)
    target_compile_options(c2-frontend-session-journal-tests PRIVATE /W4 /utf-8)
  else()
    target_compile_options(c2-frontend-session-journal-tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME frontend-native-session-journal COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_session_journal.py" "$<TARGET_FILE:c2-frontend-session-journal-tests>")
  add_executable(c2-frontend-planning-store-tests native/tests/planning_store_tests.cpp)
  target_link_libraries(c2-frontend-planning-store-tests PRIVATE c2_frontend_core)
  target_include_directories(c2-frontend-planning-store-tests PRIVATE native/src)
  if(MSVC)
    target_compile_options(c2-frontend-planning-store-tests PRIVATE /W4 /utf-8)
  else()
    target_compile_options(c2-frontend-planning-store-tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME frontend-native-planning-store COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_planning_store.py" "$<TARGET_FILE:c2-frontend-planning-store-tests>" "$<TARGET_FILE:c2-profile-probe>")
  add_executable(c2-frontend-planning-tests native/tests/planning_tests.cpp)
  target_link_libraries(c2-frontend-planning-tests PRIVATE c2_frontend_core)
  target_include_directories(c2-frontend-planning-tests PRIVATE native/src)
  if(MSVC)
    target_compile_options(c2-frontend-planning-tests PRIVATE /W4 /utf-8)
  else()
    target_compile_options(c2-frontend-planning-tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME frontend-native-planning COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_planning.py" "$<TARGET_FILE:c2-frontend-planning-tests>" "$<TARGET_FILE:c2-profile-probe>")
  add_executable(c2-frontend-discovery-tests native/tests/discovery_tests.cpp)
  target_link_libraries(c2-frontend-discovery-tests PRIVATE c2_frontend_core)
  target_include_directories(c2-frontend-discovery-tests PRIVATE native/src)
  add_test(NAME frontend-native-discovery COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_discovery.py" "$<TARGET_FILE:c2-frontend-discovery-tests>")
  if(UNIX)
    add_test(NAME frontend-discovery-posix-permissions COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_discovery.py" "$<TARGET_FILE:c2-frontend-discovery-tests>" permissions)
    set_tests_properties(frontend-discovery-posix-permissions PROPERTIES SKIP_RETURN_CODE 77)
  endif()
  if(WIN32)
    foreach(capability IN ITEMS file-link dangling-link unpaired-utf16 junction hardlink cycle)
      add_test(NAME frontend-discovery-windows-${capability} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_discovery.py" "$<TARGET_FILE:c2-frontend-discovery-tests>" ${capability})
      set_tests_properties(frontend-discovery-windows-${capability} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()
  endif()
  add_executable(c2-frontend-content-tests native/tests/content_tests.cpp)
  target_link_libraries(c2-frontend-content-tests PRIVATE c2_frontend_core)
  target_include_directories(c2-frontend-content-tests PRIVATE native/src)
  add_test(NAME frontend-native-content COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_content.py" "$<TARGET_FILE:c2-frontend-content-tests>")
  if(UNIX)
    add_test(NAME frontend-content-posix-permissions COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_content.py" "$<TARGET_FILE:c2-frontend-content-tests>" permissions)
    set_tests_properties(frontend-content-posix-permissions PROPERTIES SKIP_RETURN_CODE 77)
  endif()
  if(WIN32)
    foreach(capability IN ITEMS file-link dangling-link unpaired-utf16 junction hardlink cycle)
      add_test(NAME frontend-content-windows-${capability} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_content.py" "$<TARGET_FILE:c2-frontend-content-tests>" ${capability})
      set_tests_properties(frontend-content-windows-${capability} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()
  endif()
  add_executable(c2-frontend-profile-files-tests native/tests/profile_files_tests.cpp)
  target_link_libraries(c2-frontend-profile-files-tests PRIVATE c2_frontend_core)
  target_include_directories(c2-frontend-profile-files-tests PRIVATE native/src)
  add_test(NAME frontend-profile-unicode-oracle COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_profile_unicode.py" --check)
  add_test(NAME frontend-native-profile-files COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_profile_files.py"
    "$<TARGET_FILE:c2-frontend-profile-files-tests>" "$<TARGET_FILE:c2-profile-probe>")
  if(UNIX)
    add_test(NAME frontend-profile-posix-permissions COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_profile_files.py"
      "$<TARGET_FILE:c2-frontend-profile-files-tests>" "$<TARGET_FILE:c2-profile-probe>" permissions)
    set_tests_properties(frontend-profile-posix-permissions PROPERTIES SKIP_RETURN_CODE 77)
  endif()
  if(WIN32)
    foreach(capability IN ITEMS file-link dangling-link unpaired-utf16 junction hardlink cycle)
      add_test(NAME frontend-profile-windows-${capability} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_profile_files.py"
        "$<TARGET_FILE:c2-frontend-profile-files-tests>" "$<TARGET_FILE:c2-profile-probe>" ${capability})
      set_tests_properties(frontend-profile-windows-${capability} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()
  endif()
  add_executable(c2-frontend-profile-tests native/tests/profile_tests.cpp)
  target_link_libraries(c2-frontend-profile-tests PRIVATE c2_frontend_core)
  add_test(NAME frontend-native-profile COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_profile_codec.py"
    "$<TARGET_FILE:c2-frontend-profile-tests>" "$<TARGET_FILE:c2-profile-probe>")
  add_executable(c2-frontend-generation-tests native/tests/generation_tests.cpp)
  target_link_libraries(c2-frontend-generation-tests PRIVATE c2_frontend_core)
  target_include_directories(c2-frontend-generation-tests PRIVATE native/src)
  add_test(NAME frontend-native-generation COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_generation_read.py"
    "$<TARGET_FILE:c2-frontend-native>" "$<TARGET_FILE:c2-frontend-generation-tests>")
  if(WIN32)
    foreach(capability IN ITEMS file-link dangling-link unpaired-utf16)
      add_test(NAME frontend-capture-windows-${capability} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_generation_read.py"
        "$<TARGET_FILE:c2-frontend-native>" "$<TARGET_FILE:c2-frontend-generation-tests>" ${capability})
      set_tests_properties(frontend-capture-windows-${capability} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()
  endif()
  add_executable(c2-frontend-store-tests native/tests/store_tests.cpp)
  target_link_libraries(c2-frontend-store-tests PRIVATE c2_frontend_core)
  target_include_directories(c2-frontend-store-tests PRIVATE native/src)
  add_test(NAME frontend-native-store COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_store_read.py"
    "$<TARGET_FILE:c2-frontend-native>" "$<TARGET_FILE:c2-frontend-store-tests>")
  add_executable(c2-frontend-store-write-tests native/tests/store_write_tests.cpp)
  target_link_libraries(c2-frontend-store-write-tests PRIVATE c2_frontend_core)
  target_include_directories(c2-frontend-store-write-tests PRIVATE native/src)
  if(MSVC)
    target_compile_options(c2-frontend-store-write-tests PRIVATE /W4 /utf-8)
  else()
    target_compile_options(c2-frontend-store-write-tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME frontend-native-store-write COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_store_write.py" "$<TARGET_FILE:c2-frontend-store-write-tests>")
  # The production handle owners must not depend on copy elision: the same
  # driver is rebuilt from the production sources with elision disabled.
  include(CheckCXXCompilerFlag)
  if(MSVC)
    check_cxx_compiler_flag("/Zc:nrvo-" c2_no_elision_supported)
    set(c2_no_elision_flag "/Zc:nrvo-")
  else()
    check_cxx_compiler_flag("-fno-elide-constructors" c2_no_elision_supported)
    set(c2_no_elision_flag "-fno-elide-constructors")
  endif()
  add_executable(c2-frontend-store-write-no-elision-tests native/tests/store_write_tests.cpp ${c2_frontend_core_sources})
  target_compile_features(c2-frontend-store-write-no-elision-tests PRIVATE cxx_std_17)
  target_include_directories(c2-frontend-store-write-no-elision-tests PRIVATE native/include native/src)
  target_include_directories(c2-frontend-store-write-no-elision-tests SYSTEM PRIVATE native/third_party)
  if(WIN32)
    target_link_libraries(c2-frontend-store-write-no-elision-tests PRIVATE bcrypt)
  endif()
  if(MSVC)
    target_compile_options(c2-frontend-store-write-no-elision-tests PRIVATE /W4 /utf-8)
  else()
    target_compile_options(c2-frontend-store-write-no-elision-tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  if(c2_no_elision_supported)
    target_compile_options(c2-frontend-store-write-no-elision-tests PRIVATE ${c2_no_elision_flag})
    add_test(NAME frontend-store-write-no-elision COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_store_write.py" "$<TARGET_FILE:c2-frontend-store-write-no-elision-tests>")
  else()
    message(STATUS "Compiler cannot disable copy elision; frontend-store-write-no-elision is registered as an explicit skip")
    add_test(NAME frontend-store-write-no-elision COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_store_write.py" "$<TARGET_FILE:c2-frontend-store-write-no-elision-tests>" elision-unavailable)
  endif()
  set_tests_properties(frontend-store-write-no-elision PROPERTIES SKIP_RETURN_CODE 77)
  add_test(NAME frontend-store-write-utilities COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_store_write.py" "$<TARGET_FILE:c2-frontend-store-write-tests>" utilities)
  # Named capabilities return CTest skip 77 when the host cannot exercise them.
  add_test(NAME frontend-store-write-file-link COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_store_write.py" "$<TARGET_FILE:c2-frontend-store-write-tests>" file-link)
  set_tests_properties(frontend-store-write-file-link PROPERTIES SKIP_RETURN_CODE 77)
  if(UNIX)
    add_test(NAME frontend-store-write-posix-durability COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_store_write.py" "$<TARGET_FILE:c2-frontend-store-write-tests>" posix)
    set_tests_properties(frontend-store-write-posix-durability PROPERTIES SKIP_RETURN_CODE 77)
  endif()
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
  set_tests_properties(frontend-native-help PROPERTIES PASS_REGULAR_EXPRESSION "Read-only commands")
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
