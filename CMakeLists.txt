cmake_minimum_required(VERSION 3.16)

# Disable vcpkg's applocal DLL copy (requires PowerShell); we bundle DLLs via install()
set(VCPKG_APPLOCAL_DEPS OFF CACHE BOOL "")
set(X_VCPKG_APPLOCAL_DEPS_INSTALL OFF CACHE BOOL "")

project(citris LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

find_package(SDL3 REQUIRED)
find_package(SDL3_ttf REQUIRED)
find_package(SDL3_image REQUIRED)

# Embed font as C header via xxd
set(FONT_INPUT "${CMAKE_SOURCE_DIR}/assets/FreeMono.otf")
set(FONT_HEADER "${CMAKE_BINARY_DIR}/generated/FreeMono_otf.h")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
add_custom_command(
  OUTPUT "${FONT_HEADER}"
  COMMAND xxd -i FreeMono.otf "${FONT_HEADER}"
  COMMAND ${CMAKE_COMMAND} -DFILE=${FONT_HEADER}
    -P "${CMAKE_SOURCE_DIR}/cmake/inline_unsigned.cmake"
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
  "src/ui/*.cc"
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
target_link_libraries(tetris_lib PUBLIC SDL3::SDL3 SDL3_ttf::SDL3_ttf SDL3_image::SDL3_image)
add_dependencies(tetris_lib embed_font)

# Main executable — WIN32/MACOSX_BUNDLE suppress the console window
add_executable(citris WIN32 MACOSX_BUNDLE src/main.cc)
target_link_libraries(citris PRIVATE tetris_lib)

# Copy runtime data into build dir for local runs
add_custom_command(TARGET citris POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E create_symlink
    "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:citris>/assets"
  COMMAND ${CMAKE_COMMAND} -E create_symlink
    "${CMAKE_SOURCE_DIR}/settings.ini" "$<TARGET_FILE_DIR:citris>/settings.ini"
  COMMENT "Symlinking assets/ and settings.ini into build dir"
)

# Install rules — exe + runtime data
install(TARGETS citris RUNTIME DESTINATION . BUNDLE DESTINATION .)
install(DIRECTORY assets/ DESTINATION assets)
install(FILES assets/default_settings.ini DESTINATION . RENAME settings.ini)

# On Windows, bundle shared libs next to the exe
if(WIN32)
  # Collect SDL (and transitive) DLLs from vcpkg
  install(DIRECTORY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/"
    DESTINATION . FILES_MATCHING PATTERN "*.dll")

  # MinGW runtime DLLs — search compiler dir, sysroot, and common cross-compile paths
  get_filename_component(_compiler_dir ${CMAKE_CXX_COMPILER} DIRECTORY)
  file(GLOB _mingw_dlls
    "${_compiler_dir}/libstdc++-6.dll"
    "${_compiler_dir}/libgcc_s_seh-1.dll"
    "${_compiler_dir}/libwinpthread-1.dll"
    "${CMAKE_FIND_ROOT_PATH}/bin/libstdc++-6.dll"
    "${CMAKE_FIND_ROOT_PATH}/bin/libgcc_s_seh-1.dll"
    "${CMAKE_FIND_ROOT_PATH}/bin/libwinpthread-1.dll"
    "${CMAKE_FIND_ROOT_PATH}/lib/libstdc++-6.dll"
    "${CMAKE_FIND_ROOT_PATH}/lib/libgcc_s_seh-1.dll"
    "${CMAKE_FIND_ROOT_PATH}/lib/libwinpthread-1.dll"
    "/usr/x86_64-w64-mingw32/lib/libstdc++-6.dll"
    "/usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll"
    "/usr/x86_64-w64-mingw32/bin/libstdc++-6.dll"
    "/usr/x86_64-w64-mingw32/bin/libgcc_s_seh-1.dll"
    "/usr/x86_64-w64-mingw32/bin/libwinpthread-1.dll"
    "/usr/lib/gcc/x86_64-w64-mingw32/*/libgcc_s_seh-1.dll"
    "/usr/lib/gcc/x86_64-w64-mingw32/*/libstdc++-6.dll")
  list(REMOVE_DUPLICATES _mingw_dlls)
  install(FILES ${_mingw_dlls} DESTINATION .)
endif()

# CPack — produces a zip on Windows, tar.gz on Linux
set(CPACK_PACKAGE_NAME "citris")
if(NOT DEFINED CPACK_PACKAGE_VERSION)
  set(CPACK_PACKAGE_VERSION "0.1.0")
endif()
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
