cmake_minimum_required(VERSION 3.28.3)
project(cryptography_game C)

set(CMAKE_C_STANDARD 11)

add_executable(Server server.c)

add_subdirectory(/home/idokantor/CLionProjects/cryptography_game_util /home/idokantor/CLionProjects/cryptography_game_util/build)

target_include_directories(Server PUBLIC /home/idokantor/CLionProjects/cryptography_game_util)

target_link_libraries(Server cryptography_game_util)

add_executable(Client client.c)

target_include_directories(Client PUBLIC /home/idokantor/CLionProjects/cryptography_game_util)

target_link_libraries(Client cryptography_game_util)

add_executable(GUI gui.c)

target_include_directories(GUI PUBLIC /home/idokantor/CLionProjects/cryptography_game_util)

target_link_libraries(GUI cryptography_game_util)

# Add at the top, after project()
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

# For the GUI target, add:
target_link_libraries(GUI ${GTK3_LIBRARIES})
target_include_directories(GUI PUBLIC ${GTK3_INCLUDE_DIRS})
target_compile_options(GUI PUBLIC ${GTK3_CFLAGS_OTHER})