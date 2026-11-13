cmake_minimum_required(VERSION 3.16)
project(PintSizePrix VERSION 0.1.0
        DESCRIPTION "Top-down miniature racing game"
        HOMEPAGE_URL "https://github.com/amirgeva/pintsize"
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Use an installed SDL2 when there is one; otherwise build it from source.
# Distribution packages must turn PSP_ALLOW_FETCH_SDL off: their builds have no
# network access and must link against the system library.
option(PSP_FORCE_FETCH_SDL "Always download and statically link SDL2" OFF)
option(PSP_ALLOW_FETCH_SDL "Download SDL2 when no installed copy is found" ON)
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
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz)
  FetchContent_MakeAvailable(SDL2)
  set(PSP_SDL_FETCHED ON)
endif()

# Game rules, physics and track generation: no SDL dependency, so it can be tested headlessly.
add_library(psp_core STATIC src/track.cpp src/tracks.cpp src/props.cpp src/race.cpp)
target_include_directories(psp_core PUBLIC src)

add_executable(pintsize WIN32 src/main.cpp src/game.cpp src/gfx.cpp src/audio.cpp)
if(WIN32)
  target_sources(pintsize PRIVATE res/pintsize.rc)  # the program icon
endif()
target_link_libraries(pintsize PRIVATE psp_core)
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

enable_testing()
add_executable(psp_simtest tests/simtest.cpp)
target_link_libraries(psp_simtest PRIVATE psp_core)
add_test(NAME simtest COMMAND psp_simtest)

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
