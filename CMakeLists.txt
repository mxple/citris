cmake_minimum_required(VERSION 3.16)
project(tetris LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

find_package(SFML 3 COMPONENTS Graphics Window System REQUIRED)

# Game library (all src/ except main.cc) — shared between main exe and tests
file(GLOB LIB_SOURCES "src/*.cc")
list(FILTER LIB_SOURCES EXCLUDE REGEX "main\\.cc$")
add_library(tetris_lib STATIC ${LIB_SOURCES})
target_include_directories(tetris_lib PUBLIC src)
target_link_libraries(tetris_lib PUBLIC SFML::Graphics SFML::Window SFML::System)

# Main executable
add_executable(tetris src/main.cc)
target_link_libraries(tetris PRIVATE tetris_lib)

# Tests — each test/*.cc becomes its own executable
file(GLOB TEST_SOURCES "test/*.cc")
foreach(test_src ${TEST_SOURCES})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE tetris_lib)
endforeach()

