# Worker-owned test registrations (workstream: sessions). Included from
# Frontend/CMakeLists.txt inside if(BUILD_TESTING). Use c2_frontend_test_driver().
c2_frontend_test_driver(c2-frontend-sessions-tests native/tests/sessions_tests.cpp)
# The compiled synthetic child must sit next to the driver, as it sits next to
# the production executable; every target is staged in the same binary directory.
add_dependencies(c2-frontend-sessions-tests c2-frontend-synthetic-child)
# One harness, three modes: preparation (A1), execution (A2) and
# reconciliation/recovery including the acceptance hand-off proof (A3).
foreach(mode IN ITEMS prepare run reconcile)
  add_test(NAME frontend-native-sessions-${mode} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_sessions_native.py"
    "$<TARGET_FILE:c2-frontend-sessions-tests>" "$<TARGET_FILE:c2-profile-probe>"
    "$<TARGET_FILE:c2-native-session-fixture>" "$<TARGET_FILE:c2-frontend-synthetic-child>"
    "$<TARGET_FILE:c2-frontend-probe-helper>" ${mode})
  set_tests_properties(frontend-native-sessions-${mode} PROPERTIES TIMEOUT 900)
endforeach()
