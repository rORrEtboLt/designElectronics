cmake_minimum_required(VERSION 3.20)
project(designElectronics VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# SDL3 + SDL3_ttf: use installed packages, or vendor them with
#   cmake -DDE_VENDOR_SDL=ON   (expects external/SDL and external/SDL_ttf checkouts)
option(DE_VENDOR_SDL "Build SDL3/SDL3_ttf from external/ sources" OFF)
if(DE_VENDOR_SDL OR ANDROID OR IOS)
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
  src/doc.cpp
  src/router.cpp
  src/drc.cpp
  src/sim.cpp
  src/export.cpp
  src/examples.cpp
)

if(ANDROID)
  add_library(main SHARED ${DE_SOURCES})
  set(DE_TARGET main)
else()
  add_executable(designElectronics MACOSX_BUNDLE WIN32 ${DE_SOURCES})
  set(DE_TARGET designElectronics)
endif()

target_include_directories(${DE_TARGET} PRIVATE src)
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
  set_target_properties(${DE_TARGET} PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/platform/apple/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER org.designelectronics.app
    MACOSX_BUNDLE_BUNDLE_NAME designElectronics
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
elseif(NOT ANDROID)
  add_custom_command(TARGET ${DE_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${DE_TARGET}>/assets/fonts
    COMMAND ${CMAKE_COMMAND} -E copy ${DE_FONTS} $<TARGET_FILE_DIR:${DE_TARGET}>/assets/fonts/)
  install(TARGETS ${DE_TARGET} RUNTIME DESTINATION bin)
  install(FILES ${DE_FONTS} DESTINATION bin/assets/fonts)
  if(UNIX)
    install(FILES platform/linux/designelectronics.desktop DESTINATION share/applications)
    install(FILES platform/linux/designelectronics-mime.xml DESTINATION share/mime/packages)
  endif()
endif()
