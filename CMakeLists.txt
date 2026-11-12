cmake_minimum_required(VERSION 3.16)
project(PintSizePrix LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Turn this off on a machine that only needs the headless bot and tests: then SDL is not needed.
option(PSP_BUILD_GAME "Build the game itself, which needs SDL2" ON)

include(FetchContent)

# Use an installed SDL2 when there is one; otherwise build it from source.
option(PSP_FORCE_FETCH_SDL "Always download and statically link SDL2" OFF)
if(PSP_BUILD_GAME)
  if(NOT PSP_FORCE_FETCH_SDL)
    find_package(SDL2 CONFIG QUIET)
  endif()
  if(NOT SDL2_FOUND)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL2
      URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz)
    FetchContent_MakeAvailable(SDL2)
  endif()
endif()

# ENet (reliable UDP) carries LAN play. Use vcpkg's copy if there is one; otherwise download the
# source and compile it here. Its own build script is skipped (SOURCE_SUBDIR points nowhere)
# because it asks for a CMake version that CMake 4 no longer accepts.
find_package(unofficial-enet CONFIG QUIET)
if(TARGET unofficial::enet::enet)
  add_library(psp_enet INTERFACE)
  target_link_libraries(psp_enet INTERFACE unofficial::enet::enet)
  if(WIN32)
    target_link_libraries(psp_enet INTERFACE ws2_32 winmm)
  endif()
else()
  FetchContent_Declare(enet
    URL https://github.com/lsalzman/enet/archive/refs/tags/v1.3.18.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR not-used)
  FetchContent_MakeAvailable(enet)
  add_library(psp_enet STATIC
    ${enet_SOURCE_DIR}/callbacks.c ${enet_SOURCE_DIR}/compress.c ${enet_SOURCE_DIR}/host.c
    ${enet_SOURCE_DIR}/list.c ${enet_SOURCE_DIR}/packet.c ${enet_SOURCE_DIR}/peer.c
    ${enet_SOURCE_DIR}/protocol.c ${enet_SOURCE_DIR}/unix.c ${enet_SOURCE_DIR}/win32.c)
  target_include_directories(psp_enet PUBLIC ${enet_SOURCE_DIR}/include)
  if(WIN32)
    target_link_libraries(psp_enet PUBLIC ws2_32 winmm)
    target_compile_definitions(psp_enet PRIVATE _WINSOCK_DEPRECATED_NO_WARNINGS)
  else()
    # What ENet's build script would detect on any current Linux or macOS.
    target_compile_definitions(psp_enet PRIVATE HAS_SOCKLEN_T=1 HAS_POLL=1 HAS_FCNTL=1 HAS_MSGHDR_FLAGS=1
                               HAS_GETADDRINFO=1 HAS_GETNAMEINFO=1 HAS_INET_PTON=1 HAS_INET_NTOP=1)
  endif()
endif()

# Game rules, physics and track generation: no SDL dependency, so it can be tested headlessly.
add_library(psp_core STATIC src/track.cpp src/tracks.cpp src/props.cpp src/race.cpp src/link.cpp)
target_include_directories(psp_core PUBLIC src)

# LAN play: sessions and LAN search over ENet. No SDL either, so the headless bot shares it.
add_library(psp_net STATIC src/net.cpp)
target_link_libraries(psp_net PUBLIC psp_core PRIVATE psp_enet)

# A computer driver for LAN races, with no window or sound.
add_executable(psp_bot src/bot.cpp)
target_link_libraries(psp_bot PRIVATE psp_net)

if(PSP_BUILD_GAME)
  add_executable(pintsize WIN32 src/main.cpp src/game.cpp src/gfx.cpp src/audio.cpp)
  target_link_libraries(pintsize PRIVATE psp_core psp_net)
  if(TARGET SDL2::SDL2main)
    target_link_libraries(pintsize PRIVATE SDL2::SDL2main)
  endif()
  if(TARGET SDL2::SDL2-static)
    target_link_libraries(pintsize PRIVATE SDL2::SDL2-static)
  else()
    target_link_libraries(pintsize PRIVATE SDL2::SDL2)
  endif()
endif()

enable_testing()
add_executable(psp_simtest tests/simtest.cpp)
target_link_libraries(psp_simtest PRIVATE psp_core)
add_test(NAME simtest COMMAND psp_simtest)

# Real ENet sessions over the loopback interface, in one process. Takes about half a minute.
add_executable(psp_nettest tests/nettest.cpp)
target_link_libraries(psp_nettest PRIVATE psp_net)
add_test(NAME nettest COMMAND psp_nettest)

if(MSVC)
  target_compile_options(psp_core PRIVATE /W3 /fp:fast)
  target_compile_definitions(psp_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()
