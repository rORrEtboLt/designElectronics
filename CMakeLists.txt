cmake_minimum_required(VERSION 3.20)
project(designElectronics VERSION 0.2.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# SDL3 + SDL3_ttf: use installed packages, or vendor them with
#   cmake -DDE_VENDOR_SDL=ON   (expects external/SDL and external/SDL_ttf checkouts)
option(DE_VENDOR_SDL "Build SDL3/SDL3_ttf from external/ sources" OFF)
if(DE_VENDOR_SDL OR ANDROID OR IOS OR EMSCRIPTEN)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  add_subdirectory(external/SDL EXCLUDE_FROM_ALL)
  set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF)
  add_subdirectory(external/SDL_ttf EXCLUDE_FROM_ALL)
else()
  find_package(SDL3 REQUIRED CONFIG)
  find_package(SDL3_ttf REQUIRED CONFIG)
endif()

set(DE_SOURCES
  src/main.cpp
  src/app.cpp
  src/canvas.cpp
  src/panels.cpp
  src/ui.cpp
  src/gfx.cpp
  src/json.cpp
  src/partlib.cpp
  src/catalog.cpp
  src/packages.cpp
  src/doc.cpp
  src/router.cpp
  src/drc.cpp
  src/sim.cpp
  src/arduino.cpp
  src/export.cpp
  src/examples.cpp
  src/mcp.cpp
  src/logo.cpp
  src/history.cpp
  src/app_history.cpp
  src/panels_layers.cpp
  src/web.cpp
  src/code_editor.cpp
  src/panels_code.cpp
  src/upload.cpp
  src/video.cpp
  src/panels_video.cpp
  src/codec_h264.c
  src/codec_mp4.c
  src/net.cpp
  src/agent.cpp
  src/panels_ai.cpp
  src/partdata.cpp
)

# Parts library data (parts/*.txt), compiled in.
file(GLOB DE_PART_DATA CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/parts/*.txt)
list(SORT DE_PART_DATA)
set(DE_PART_BLOB ${CMAKE_CURRENT_BINARY_DIR}/partdata_blob.c)
add_custom_command(
  OUTPUT ${DE_PART_BLOB}
  COMMAND ${CMAKE_COMMAND} "-DOUT=${DE_PART_BLOB}" "-DIN=${DE_PART_DATA}" -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_parts.cmake
  DEPENDS ${DE_PART_DATA} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_parts.cmake
  COMMENT "Embedding parts library"
  VERBATIM)
list(APPEND DE_SOURCES ${DE_PART_BLOB})

if(ANDROID)
  add_library(main SHARED ${DE_SOURCES})
  set(DE_TARGET main)
else()
  add_executable(designElectronics MACOSX_BUNDLE WIN32 ${DE_SOURCES})
  set(DE_TARGET designElectronics)
endif()

target_include_directories(${DE_TARGET} PRIVATE src)

# HTTPS for GitHub sync (desktop). Without it, local version history still works.
if(NOT ANDROID AND NOT IOS AND NOT EMSCRIPTEN)
  find_package(CURL QUIET)
  if(CURL_FOUND)
    target_link_libraries(${DE_TARGET} PRIVATE CURL::libcurl)
    target_compile_definitions(${DE_TARGET} PRIVATE DE_HAVE_CURL=1)
  else()
    message(STATUS "libcurl not found: GitHub sync disabled (local history still works)")
  endif()
endif()
target_link_libraries(${DE_TARGET} PRIVATE SDL3_ttf::SDL3_ttf SDL3::SDL3)

if(MSVC)
  target_compile_options(${DE_TARGET} PRIVATE /W3 /utf-8 /D_USE_MATH_DEFINES)
else()
  target_compile_options(${DE_TARGET} PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Copy fonts next to the binary (desktop) / into the bundle (macOS, iOS).
set(DE_FONTS
  ${CMAKE_SOURCE_DIR}/assets/fonts/Lato-Regular.ttf
  ${CMAKE_SOURCE_DIR}/assets/fonts/SourceCodePro-Regular.ttf
  ${CMAKE_SOURCE_DIR}/assets/fonts/OFL.txt)

if(APPLE)
  target_sources(${DE_TARGET} PRIVATE ${DE_FONTS})
  set_source_files_properties(${DE_FONTS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/assets/fonts)
  # App icon (regenerate with --export-logo + iconutil, see docs/BUILDING.md)
  target_sources(${DE_TARGET} PRIVATE ${CMAKE_SOURCE_DIR}/platform/apple/AppIcon.icns)
  set_source_files_properties(${CMAKE_SOURCE_DIR}/platform/apple/AppIcon.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(${DE_TARGET} PROPERTIES
    MACOSX_BUNDLE_ICON_FILE AppIcon
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/platform/apple/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER org.designelectronics.app
    MACOSX_BUNDLE_BUNDLE_NAME designElectronics
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
elseif(EMSCRIPTEN)
  # Web build: designElectronics.js/.wasm/.data, started by docs/app and docs/embed pages.
  # INVOKE_RUN=0: the page writes the design into the virtual FS first, then calls callMain(args).
  target_link_options(${DE_TARGET} PRIVATE
    -sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1 -sINVOKE_RUN=0 -sEXIT_RUNTIME=0
    "-sEXPORTED_RUNTIME_METHODS=['FS','ccall','callMain']"
    "-sEXPORTED_FUNCTIONS=['_main','_de_web_opened']"
    "--preload-file=${CMAKE_SOURCE_DIR}/assets/fonts@/assets/fonts")
elseif(NOT ANDROID)
  add_custom_command(TARGET ${DE_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${DE_TARGET}>/assets/fonts
    COMMAND ${CMAKE_COMMAND} -E copy ${DE_FONTS} $<TARGET_FILE_DIR:${DE_TARGET}>/assets/fonts/)
  install(TARGETS ${DE_TARGET} RUNTIME DESTINATION bin)
  install(FILES ${DE_FONTS} DESTINATION bin/assets/fonts)
  if(UNIX)
    install(FILES platform/linux/designelectronics.desktop DESTINATION share/applications)
    install(FILES platform/linux/designelectronics-mime.xml DESTINATION share/mime/packages)
    install(FILES assets/icon/icon_256.png DESTINATION share/icons/hicolor/256x256/apps RENAME designelectronics.png)
    install(FILES assets/icon/logo.svg DESTINATION share/icons/hicolor/scalable/apps RENAME designelectronics.svg)
  endif()
endif()
