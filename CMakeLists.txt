cmake_minimum_required(VERSION 3.16)
project(PintSizePrix LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Use an installed SDL2 when there is one; otherwise build it from source.
option(PSP_FORCE_FETCH_SDL "Always download and statically link SDL2" OFF)
if(NOT PSP_FORCE_FETCH_SDL)
  find_package(SDL2 CONFIG QUIET)
endif()
if(NOT SDL2_FOUND)
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz)
  FetchContent_MakeAvailable(SDL2)
endif()

# Game rules, physics and track generation: no SDL dependency, so it can be tested headlessly.
add_library(psp_core STATIC src/track.cpp src/tracks.cpp src/props.cpp src/race.cpp)
target_include_directories(psp_core PUBLIC src)

add_executable(pintsize WIN32 src/main.cpp src/game.cpp src/gfx.cpp src/audio.cpp)
target_link_libraries(pintsize PRIVATE psp_core)
if(TARGET SDL2::SDL2main)
  target_link_libraries(pintsize PRIVATE SDL2::SDL2main)
endif()
if(TARGET SDL2::SDL2-static)
  target_link_libraries(pintsize PRIVATE SDL2::SDL2-static)
else()
  target_link_libraries(pintsize PRIVATE SDL2::SDL2)
endif()

enable_testing()
add_executable(psp_simtest tests/simtest.cpp)
target_link_libraries(psp_simtest PRIVATE psp_core)
add_test(NAME simtest COMMAND psp_simtest)

if(MSVC)
  target_compile_options(psp_core PRIVATE /W3 /fp:fast)
  target_compile_definitions(psp_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()
