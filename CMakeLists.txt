cmake_minimum_required(VERSION 3.16)
# The version is written only in VERSION; scripts/set-version.py copies it to the other files.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PSP_VERSION LIMIT_COUNT 1)
project(PintSizePrix VERSION ${PSP_VERSION}
        DESCRIPTION "Top-down miniature racing game"
        HOMEPAGE_URL "https://github.com/amirgeva/pintsize"
        LANGUAGES C CXX)

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
# Distribution packages must turn PSP_ALLOW_FETCH_SDL off: their builds have no
# network access and must link against the system library.
option(PSP_FORCE_FETCH_SDL "Always download and statically link SDL2" OFF)
option(PSP_ALLOW_FETCH_SDL "Download SDL2 when no installed copy is found" ON)
if(PSP_BUILD_GAME)
  if(NOT PSP_FORCE_FETCH_SDL)
    find_package(SDL2 CONFIG QUIET)
    if(SDL2_FOUND AND NOT TARGET SDL2::SDL2 AND NOT TARGET SDL2::SDL2-static)
      # Older sdl2-config.cmake files (SDL < 2.0.12) only set variables.
      find_package(PkgConfig QUIET)
      if(PKG_CONFIG_FOUND)
        pkg_check_modules(SDL2_PC QUIET IMPORTED_TARGET sdl2)
      endif()
      if(TARGET PkgConfig::SDL2_PC)
        add_library(SDL2::SDL2 INTERFACE IMPORTED)
        set_target_properties(SDL2::SDL2 PROPERTIES INTERFACE_LINK_LIBRARIES PkgConfig::SDL2_PC)
      else()
        set(SDL2_FOUND FALSE)
      endif()
    endif()
  endif()
  if(NOT SDL2_FOUND)
    if(NOT PSP_ALLOW_FETCH_SDL AND NOT PSP_FORCE_FETCH_SDL)
      message(FATAL_ERROR "SDL2 development files not found (install libsdl2-dev)")
    endif()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL2
      URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz)
    FetchContent_MakeAvailable(SDL2)
    set(PSP_SDL_FETCHED ON)
  endif()
endif()

# ENet (reliable UDP) carries LAN play. Use vcpkg's copy or the system's (libenet-dev, found
# through pkg-config) if there is one; otherwise download the source and compile it here. Its own
# build script is skipped (SOURCE_SUBDIR points nowhere) because it asks for a CMake version that
# CMake 4 no longer accepts.
find_package(unofficial-enet CONFIG QUIET)
if(NOT TARGET unofficial::enet::enet)
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(ENET_PC QUIET IMPORTED_TARGET libenet)
  endif()
endif()
if(TARGET unofficial::enet::enet)
  add_library(psp_enet INTERFACE)
  target_link_libraries(psp_enet INTERFACE unofficial::enet::enet)
  if(WIN32)
    target_link_libraries(psp_enet INTERFACE ws2_32 winmm)
  endif()
elseif(TARGET PkgConfig::ENET_PC)
  add_library(psp_enet INTERFACE)
  target_link_libraries(psp_enet INTERFACE PkgConfig::ENET_PC)
elseif(FETCHCONTENT_FULLY_DISCONNECTED)
  message(FATAL_ERROR "ENet development files not found and downloads are off (install libenet-dev)")
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
  if(WIN32)
    target_sources(pintsize PRIVATE res/pintsize.rc)  # the program icon
  endif()
  target_link_libraries(pintsize PRIVATE psp_core psp_net)
  if(TARGET SDL2::SDL2main)
    target_link_libraries(pintsize PRIVATE SDL2::SDL2main)
  endif()
  # Link the shared system SDL2 when there is one (distributions require it);
  # the downloaded copy is static.
  if(TARGET SDL2::SDL2 AND NOT PSP_SDL_FETCHED)
    target_link_libraries(pintsize PRIVATE SDL2::SDL2)
  else()
    target_link_libraries(pintsize PRIVATE SDL2::SDL2-static)
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

# Installation (used by the Debian package and by "cmake --install").
if(UNIX AND NOT APPLE)
  include(GNUInstallDirs)
  # Debian policy puts games in /usr/games; pass -DCMAKE_INSTALL_BINDIR=games for that layout.
  install(TARGETS pintsize RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  configure_file(data/pintsize.desktop.in pintsize.desktop @ONLY)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/pintsize.desktop
          DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES data/io.github.amirgeva.pintsize.metainfo.xml
          DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
  install(FILES data/pintsize.svg
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
  install(FILES data/pintsize.6 DESTINATION ${CMAKE_INSTALL_MANDIR}/man6)
endif()
