cmake_minimum_required(VERSION 3.16)
project(citris LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT EMSCRIPTEN)
  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads REQUIRED)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-O2 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

if(EMSCRIPTEN)
  add_compile_options(-pthread)
endif()

# --- All platforms: use vendored submodules from dist/ ---
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDLIMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_JPG OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_WEBP OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_JXL OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)

add_subdirectory(dist/SDL EXCLUDE_FROM_ALL)
add_subdirectory(dist/SDL_image EXCLUDE_FROM_ALL)
add_subdirectory(dist/json EXCLUDE_FROM_ALL)
add_subdirectory(dist/spdlog EXCLUDE_FROM_ALL)

# OpenCV: minimal static build (core + imgproc only)
set(BUILD_LIST "core,imgproc" CACHE STRING "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_apps OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_python3 OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_python_bindings_generator OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_java OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_js OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_gapi OFF CACHE BOOL "" FORCE)
set(WITH_JPEG OFF CACHE BOOL "" FORCE)
set(WITH_PNG OFF CACHE BOOL "" FORCE)
set(WITH_TIFF OFF CACHE BOOL "" FORCE)
set(WITH_WEBP OFF CACHE BOOL "" FORCE)
set(WITH_OPENEXR OFF CACHE BOOL "" FORCE)
set(WITH_PROTOBUF OFF CACHE BOOL "" FORCE)
set(WITH_QUIRC OFF CACHE BOOL "" FORCE)
set(WITH_OPENCL OFF CACHE BOOL "" FORCE)
set(WITH_ITT OFF CACHE BOOL "" FORCE)
set(WITH_EIGEN OFF CACHE BOOL "" FORCE)
set(WITH_LAPACK OFF CACHE BOOL "" FORCE)
set(CV_TRACE OFF CACHE BOOL "" FORCE)
set(OPENCV_GENERATE_PKGCONFIG OFF CACHE BOOL "" FORCE)
set(WITH_IPP OFF CACHE BOOL "" FORCE)
if(EMSCRIPTEN)
  set(WITH_PTHREADS_PF OFF CACHE BOOL "" FORCE)
  set(CV_ENABLE_INTRINSICS OFF CACHE BOOL "" FORCE)
  set(CPU_BASELINE "" CACHE STRING "" FORCE)
  set(CPU_DISPATCH "" CACHE STRING "" FORCE)
endif()
add_subdirectory(dist/opencv EXCLUDE_FROM_ALL)

# ImGui — no CMakeLists.txt upstream, build as static lib from sources
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/dist/imgui")
add_library(imgui_lib STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(imgui_lib PUBLIC
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC SDL3::SDL3)
add_library(imgui::imgui ALIAS imgui_lib)

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

# Game library — all src/ subtree; shippable entry points live in bin/
file(GLOB LIB_SOURCES
  "src/*.cc"
  "src/engine/*.cc"
  "src/controller/*.cc"
  "src/render/*.cc"
  "src/presets/*.cc"
  "src/ui/*.cc"
  "src/ai/*.cc"
  "src/ai/pc/*.cc"
  "src/ai/eval/*.cc"
  "src/tbp/*.cc"
  "src/cv/*.cc"
)
add_library(tetris_lib STATIC ${LIB_SOURCES})
target_precompile_headers(tetris_lib PUBLIC src/pch.h)
target_compile_options(tetris_lib PUBLIC -fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=)
target_include_directories(tetris_lib PUBLIC
  src
  src/engine
  src/controller
  src/render
  src/presets
  src/ai
  src/ai/pc
  src/ai/eval
  src/tbp
  src/cv
  "${CMAKE_BINARY_DIR}/generated"
)
target_link_libraries(tetris_lib PUBLIC SDL3::SDL3 SDL3_image::SDL3_image imgui::imgui nlohmann_json::nlohmann_json spdlog::spdlog opencv_core opencv_imgproc)
target_include_directories(tetris_lib PUBLIC
  "${CMAKE_SOURCE_DIR}/dist/opencv/modules/core/include"
  "${CMAKE_SOURCE_DIR}/dist/opencv/modules/imgproc/include"
  "${CMAKE_BINARY_DIR}"
)
if(NOT EMSCRIPTEN)
  target_link_libraries(tetris_lib PUBLIC Threads::Threads)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # libstdc++'s std::stacktrace implementation lives in libstdc++exp (GCC 14+).
    target_link_libraries(tetris_lib PUBLIC stdc++exp)
  endif()
endif()
add_dependencies(tetris_lib embed_font)

if(EMSCRIPTEN)
  # Main executable — no WIN32/MACOSX_BUNDLE for WASM
  add_executable(citris bin/main_citris.cc)
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
      -sPTHREAD_POOL_SIZE=6 \
      --preload-file ${CMAKE_SOURCE_DIR}/assets@/assets \
      --preload-file ${CMAKE_SOURCE_DIR}/assets/default_settings.ini@/settings.ini \
      --shell-file ${CMAKE_SOURCE_DIR}/web/shell.html \
      --pre-js ${CMAKE_SOURCE_DIR}/web/pre.js \
    "
  )
else()
  # Main executable — WIN32/MACOSX_BUNDLE suppress the console window
  add_executable(citris WIN32 MACOSX_BUNDLE bin/main_citris.cc)
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

  # On Windows (MinGW cross-compile), bundle runtime DLLs
  if(WIN32)
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

  # Auxiliary CLI tools — every bin/main_*.cc (except main_citris.cc, the GUI
  # above) becomes an executable named citris-<stem>. Drop a new main_X.cc
  # into bin/ and the next build picks it up. CONFIGURE_DEPENDS makes CMake
  # re-glob on each build instead of requiring a manual reconfigure.
  file(GLOB BIN_AUX_SOURCES CONFIGURE_DEPENDS "bin/main_*.cc")
  list(FILTER BIN_AUX_SOURCES EXCLUDE REGEX "main_citris\\.cc$")
  foreach(bin_src ${BIN_AUX_SOURCES})
    get_filename_component(bin_stem ${bin_src} NAME_WE)        # main_tbp
    string(REGEX REPLACE "^main_" "citris-" bin_tgt ${bin_stem}) # citris-tbp
    add_executable(${bin_tgt} ${bin_src})
    target_link_libraries(${bin_tgt} PRIVATE tetris_lib)
  endforeach()

  # Explicit inter-target edges (not derivable from filenames):
  #   test_tbp_external_bot spawns citris-tbp at runtime.
  #   citris-headless dogfoods citris-tbp when you pass it as --bot2.
  if(TARGET test_tbp_external_bot AND TARGET citris-tbp)
    add_dependencies(test_tbp_external_bot citris-tbp)
  endif()
  if(TARGET citris-headless AND TARGET citris-tbp)
    add_dependencies(citris-headless citris-tbp)
  endif()
endif()
