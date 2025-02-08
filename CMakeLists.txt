cmake_minimum_required(VERSION 3.28.3)
project(cryptography_game C CXX)  # Add CXX here

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)  # Add C++ standard

# Add the cryptography_game_util subdirectory and link
add_subdirectory(/home/idokantor/CLionProjects/cryptography_game_util /home/idokantor/CLionProjects/cryptography_game_util/build)
target_include_directories(cryptography_game_util PUBLIC /home/idokantor/CLionProjects/cryptography_game_util)

# Find FLTK package
find_package(FLTK REQUIRED)

# Add GUI Library
add_library(gui_fltk STATIC gui_fltk.cpp)
set_target_properties(gui_fltk PROPERTIES
        LINKER_LANGUAGE CXX
        POSITION_INDEPENDENT_CODE ON
)
target_link_libraries(gui_fltk PRIVATE
        fltk
        fltk_images
        cryptography_game_util
)
target_include_directories(gui_fltk PUBLIC ${FLTK_INCLUDE_DIRS})

# Add Server Executable
add_executable(Server server.c)
target_include_directories(Server PUBLIC /home/idokantor/CLionProjects/cryptography_game_util)
target_link_libraries(Server cryptography_game_util)

# Add Client Executable
add_executable(Client client.c)
target_include_directories(Client PUBLIC /home/idokantor/CLionProjects/cryptography_game_util)
target_link_libraries(Client
        cryptography_game_util
        gui_fltk
        ${FLTK_LIBRARIES}
        pthread
        stdc++  # Add C++ standard library
)