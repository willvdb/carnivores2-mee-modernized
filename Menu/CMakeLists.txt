cmake_minimum_required(VERSION 3.15)

# Menu executable for Carnivores 2 Modder's Engine
set(MENU_SOURCES
    Hunt2.cpp
    Menu.cpp
    Audio.cpp
    Resources.cpp
    GUI.cpp
    Resolutions.cpp
    Targa.h
    Hunt.h
    resource.h
    Menu.rc
    Version.h
    ../Hunt/Audio/OpenAL_Loader.cpp
)

# Add the menu executable
add_executable(Carnivores2Menu WIN32 ${MENU_SOURCES})

# Include directories (self-contained, uses local Hunt.h)
target_include_directories(Carnivores2Menu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../Hunt
)

# Link libraries (same as main executable)
target_link_libraries(Carnivores2Menu PRIVATE
    LegacyPath
    kernel32
    user32
    gdi32
    winmm
)

# Output directory
set_target_properties(Carnivores2Menu PROPERTIES
    OUTPUT_NAME "Carnivores2Menu"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
