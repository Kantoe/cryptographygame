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
