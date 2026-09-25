# Worker-owned test registrations (workstream: store_ops). Included from
# Frontend/CMakeLists.txt inside if(BUILD_TESTING). Use c2_frontend_test_driver().
c2_frontend_test_driver(c2-frontend-store-ops-tests native/tests/store_ops_tests.cpp)
add_test(NAME frontend-native-store-ops COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/test_store_ops.py"
  "$<TARGET_FILE:c2-frontend-store-ops-tests>" "$<TARGET_FILE:c2-profile-probe>")
