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
  add_test(NAME frontend-backend
    COMMAND "${CMAKE_COMMAND}" -E env
      "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}"
      "C2_PROFILE_PROBE=$<TARGET_FILE:c2-profile-probe>"
      "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -v)
endif()
