# Worker-owned test registrations (workstream: acceptance). Included from
# Frontend/CMakeLists.txt inside if(BUILD_TESTING). Use c2_frontend_test_driver().
# Owned child supervisor (4A/4B): authored compiled child plus a driver over
# the private session_process API, compared against lodge.session_runner's
# unchanged BoundedLog/stop_owned by the Python harness.
add_executable(c2-frontend-session-process-child native/tests/session_process_child.cpp)
target_compile_features(c2-frontend-session-process-child PRIVATE cxx_std_17)
target_link_libraries(c2-frontend-session-process-child PRIVATE Threads::Threads)
c2_frontend_test_driver(c2-frontend-session-process-tests native/tests/session_process_tests.cpp)
add_test(NAME frontend-native-session-process COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_session_process.py"
  "$<TARGET_FILE:c2-frontend-session-process-tests>" "$<TARGET_FILE:c2-frontend-session-process-child>")
