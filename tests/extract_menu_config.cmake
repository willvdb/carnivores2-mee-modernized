# Compile the production Menu SaveConfig body with minimal state/path doubles.
# This characterizes preservation without changing or linking the Windows UI.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/Menu/Resources.cpp")
file(READ "${PROJECT_SOURCE_DIR}/Menu/Resources.cpp" menu_source)
string(FIND "${menu_source}" "void SaveConfig()" menu_save_start)
string(FIND "${menu_source}" "// Parse a single \"key value\" line." menu_save_end)
if(menu_save_start LESS 0 OR menu_save_end LESS menu_save_start)
    message(FATAL_ERROR "Cannot locate production Menu SaveConfig for preservation test")
endif()
math(EXPR menu_save_length "${menu_save_end} - ${menu_save_start}")
string(SUBSTRING "${menu_source}" ${menu_save_start} ${menu_save_length} menu_save_body)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/menu_save_config.inc" "${menu_save_body}")
