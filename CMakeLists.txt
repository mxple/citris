cmake_minimum_required(VERSION 3.16)
project(citris LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

find_package(SFML 3 COMPONENTS Graphics Window System REQUIRED)

# Embed font as C header via xxd
set(FONT_INPUT "${CMAKE_SOURCE_DIR}/assets/FreeMono.otf")
set(FONT_HEADER "${CMAKE_BINARY_DIR}/generated/FreeMono_otf.h")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
add_custom_command(
  OUTPUT "${FONT_HEADER}"
  COMMAND xxd -i FreeMono.otf "${FONT_HEADER}"
  COMMAND sed -i "s/^unsigned/inline unsigned/" "${FONT_HEADER}"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/assets"
  DEPENDS "${FONT_INPUT}"
  COMMENT "Embedding FreeMono.otf via xxd"
)
add_custom_target(embed_font DEPENDS "${FONT_HEADER}")

# Game library (all src/ except main.cc) — shared between main exe and tests
file(GLOB LIB_SOURCES
  "src/*.cc"
  "src/engine/*.cc"
  "src/controller/*.cc"
  "src/render/*.cc"
  "src/presets/*.cc"
)
list(FILTER LIB_SOURCES EXCLUDE REGEX "main\\.cc$")
add_library(tetris_lib STATIC ${LIB_SOURCES})
target_precompile_headers(tetris_lib PUBLIC src/pch.h)
target_include_directories(tetris_lib PUBLIC
  src
  src/engine
  src/controller
  src/render
  src/presets
  "${CMAKE_BINARY_DIR}/generated"
)
target_link_libraries(tetris_lib PUBLIC SFML::Graphics SFML::Window SFML::System)
add_dependencies(tetris_lib embed_font)

# Main executable
add_executable(citris src/main.cc)
target_link_libraries(citris PRIVATE tetris_lib)

# Install rules — exe + runtime data
install(TARGETS citris RUNTIME DESTINATION .)
install(DIRECTORY assets/ DESTINATION assets)
install(FILES assets/default_settings.ini DESTINATION . RENAME settings.ini)

# On Windows, bundle shared libs next to the exe
if(WIN32)
  # Collect SFML (and transitive) DLLs from vcpkg
  install(DIRECTORY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/"
    DESTINATION . FILES_MATCHING PATTERN "*.dll")

  # MinGW runtime DLLs — check compiler dir and sysroot bin
  get_filename_component(_compiler_dir ${CMAKE_CXX_COMPILER} DIRECTORY)
  file(GLOB _mingw_dlls
    "${_compiler_dir}/libstdc++-6.dll"
    "${_compiler_dir}/libgcc_s_seh-1.dll"
    "${_compiler_dir}/libwinpthread-1.dll"
    "${CMAKE_FIND_ROOT_PATH}/bin/libstdc++-6.dll"
    "${CMAKE_FIND_ROOT_PATH}/bin/libgcc_s_seh-1.dll"
    "${CMAKE_FIND_ROOT_PATH}/bin/libwinpthread-1.dll"
    "/usr/${CMAKE_SYSTEM_PROCESSOR}-w64-mingw32/bin/libstdc++-6.dll"
    "/usr/${CMAKE_SYSTEM_PROCESSOR}-w64-mingw32/bin/libgcc_s_seh-1.dll"
    "/usr/${CMAKE_SYSTEM_PROCESSOR}-w64-mingw32/bin/libwinpthread-1.dll")
  list(REMOVE_DUPLICATES _mingw_dlls)
  install(FILES ${_mingw_dlls} DESTINATION .)
endif()

# CPack — produces a zip on Windows, tar.gz on Linux
set(CPACK_PACKAGE_NAME "citris")
set(CPACK_PACKAGE_VERSION "0.1.0")
if(WIN32)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)

# Tests — each test/*.cc becomes its own executable
file(GLOB TEST_SOURCES "test/*.cc")
foreach(test_src ${TEST_SOURCES})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE tetris_lib)
endforeach()

