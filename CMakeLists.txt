cmake_minimum_required(VERSION 3.16)

# Disable vcpkg's applocal DLL copy (requires PowerShell); we bundle DLLs via install()
set(VCPKG_APPLOCAL_DEPS OFF CACHE BOOL "")
set(X_VCPKG_APPLOCAL_DEPS_INSTALL OFF CACHE BOOL "")

project(citris LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EMSCRIPTEN)
  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads REQUIRED)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-O2 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

if(EMSCRIPTEN)
  # Enable pthreads globally so FetchContent deps (SDL3, etc.) are built with atomics
  add_compile_options(-pthread)

  # --- Emscripten: fetch dependencies instead of vcpkg ---
  include(FetchContent)

  FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.14/SDL3-3.2.14.tar.gz
  )
  FetchContent_Declare(SDL3_image
    URL https://github.com/libsdl-org/SDL_image/releases/download/release-3.2.4/SDL3_image-3.2.4.tar.gz
  )
  FetchContent_Declare(imgui_src
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.8.tar.gz
  )

  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL3IMAGE_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL3IMAGE_STATIC ON CACHE BOOL "" FORCE)
  set(SDL3IMAGE_JPG OFF CACHE BOOL "" FORCE)
  set(SDL3IMAGE_WEBP OFF CACHE BOOL "" FORCE)

  FetchContent_MakeAvailable(SDL3 SDL3_image)
  FetchContent_MakeAvailable(imgui_src)

  # Build ImGui as a static library from fetched sources
  add_library(imgui_lib STATIC
    ${imgui_src_SOURCE_DIR}/imgui.cpp
    ${imgui_src_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_src_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_src_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_src_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_src_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_src_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
  )
  target_include_directories(imgui_lib PUBLIC
    ${imgui_src_SOURCE_DIR}
    ${imgui_src_SOURCE_DIR}/backends
  )
  target_link_libraries(imgui_lib PUBLIC SDL3::SDL3)

  # Alias so the rest of the build can use imgui::imgui uniformly
  add_library(imgui::imgui ALIAS imgui_lib)
else()
  find_package(SDL3 REQUIRED)
  find_package(SDL3_image REQUIRED)
  find_package(imgui CONFIG REQUIRED)
endif()

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
  "src/ai/*.cc"
  "src/ai/eval/*.cc"
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
  src/ai
  src/ai/eval
  "${CMAKE_BINARY_DIR}/generated"
)
target_link_libraries(tetris_lib PUBLIC SDL3::SDL3 SDL3_image::SDL3_image imgui::imgui)
if(NOT EMSCRIPTEN)
  target_link_libraries(tetris_lib PUBLIC Threads::Threads)
endif()
add_dependencies(tetris_lib embed_font)

if(EMSCRIPTEN)
  # Main executable — no WIN32/MACOSX_BUNDLE for WASM
  add_executable(citris src/main.cc)
  target_link_libraries(citris PRIVATE tetris_lib)

  # Copy coi-serviceworker.js into build dir (needed for SharedArrayBuffer headers)
  add_custom_command(TARGET citris POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
      "${CMAKE_SOURCE_DIR}/web/coi-serviceworker.js"
      "$<TARGET_FILE_DIR:citris>/coi-serviceworker.js"
    COMMENT "Copying coi-serviceworker.js into build dir"
  )

  # Emscripten linker flags — use LINK_FLAGS to avoid CMake de-duplicating/merging args
  set_target_properties(citris PROPERTIES
    SUFFIX ".html"
    LINK_FLAGS "\
      -sASYNCIFY \
      -sASYNCIFY_STACK_SIZE=65536 \
      -sALLOW_MEMORY_GROWTH=1 \
      -sUSE_SDL=0 \
      -pthread \
      -sPTHREAD_POOL_SIZE=2 \
      --preload-file ${CMAKE_SOURCE_DIR}/assets@/assets \
      --preload-file ${CMAKE_SOURCE_DIR}/assets/default_settings.ini@/settings.ini \
      --shell-file ${CMAKE_SOURCE_DIR}/web/shell.html \
      --pre-js ${CMAKE_SOURCE_DIR}/web/pre.js \
    "
  )
else()
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
endif()
