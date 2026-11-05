cmake_minimum_required(VERSION 3.16)
project(HyperStack C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------- raylib ---
# Pinned raylib source release, extracted at configure time (no submodules).
set(RAYLIB_VER "5.5")
set(RAYLIB_DIR "${CMAKE_BINARY_DIR}/raylib-${RAYLIB_VER}")
set(RAYLIB_TGZ "${CMAKE_BINARY_DIR}/raylib-${RAYLIB_VER}.tar.gz")

if(NOT EXISTS "${RAYLIB_DIR}/src/raylib.h")
    message(STATUS "Downloading raylib ${RAYLIB_VER}...")
    file(DOWNLOAD
         "https://codeload.github.com/raysan5/raylib/tar.gz/refs/tags/${RAYLIB_VER}"
         "${RAYLIB_TGZ}" SHOW_PROGRESS)
    file(ARCHIVE_EXTRACT INPUT "${RAYLIB_TGZ}" DESTINATION "${CMAKE_BINARY_DIR}")
endif()

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
add_subdirectory("${RAYLIB_DIR}" "raylib-build")

add_executable(hyperstack src/main.cpp)
target_link_libraries(hyperstack PRIVATE raylib)

if(APPLE)
    target_link_libraries(hyperstack PRIVATE
        "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
elseif(UNIX)
    target_link_libraries(hyperstack PRIVATE GL m pthread dl rt X11)
endif()
