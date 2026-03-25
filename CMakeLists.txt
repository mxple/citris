cmake_minimum_required(VERSION 3.16)
project(tetris LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB SOURCES "src/*.cc")
add_executable(tetris ${SOURCES})

target_include_directories(tetris PRIVATE src)

find_package(SFML 3 COMPONENTS Graphics Window System REQUIRED)
target_link_libraries(tetris PRIVATE SFML::Graphics SFML::Window SFML::System)

