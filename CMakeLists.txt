cmake_minimum_required(VERSION 3.26)
project(TCSymbolicLinks VERSION 0.1.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(CTest)

# Portable, bounded parser of raw FSCTL_GET_REPARSE_POINT data.
# Tested on Linux with crafted NTFS symlinks and junctions before the
# same implementation is linked into the Windows WDX library.
add_library(tc_reparse STATIC src/ReparseData.cpp)
target_include_directories(tc_reparse PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(MSVC)
  target_compile_options(tc_reparse PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(tc_reparse PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(BUILD_TESTING)
  add_executable(reparse_tests tests/reparse_tests.cpp)
  target_link_libraries(reparse_tests PRIVATE tc_reparse)
  add_test(NAME ntfs_reparse_hebrew_bounds COMMAND reparse_tests)
endif()
add_library(tc_list STATIC src/ListParser.cpp)
target_include_directories(tc_list PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(BUILD_TESTING)
  add_executable(list_parser_tests tests/list_parser_tests.cpp)
  target_link_libraries(list_parser_tests PRIVATE tc_list)
  add_test(NAME tc_wide_list COMMAND list_parser_tests)
  if(MSVC)
    target_compile_options(list_parser_tests PRIVATE /W4 /utf-8)
  endif()
endif()

# Build the same WDX source as a Linux ELF shared object for Double Commander.
# These Linux tests exercise actual WDX exports with 16-bit WCHAR and real links.
if(UNIX AND NOT APPLE)
  add_library(SymlinkInspectorPosix SHARED src/SymlinkInspector.cpp)
  set_target_properties(SymlinkInspectorPosix PROPERTIES
    OUTPUT_NAME "SymlinkInspector" PREFIX "" SUFFIX ".wdx"
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
  target_compile_options(SymlinkInspectorPosix PRIVATE -Wall -Wextra -Wpedantic)
  if(BUILD_TESTING)
    add_executable(posix_wdx_tests tests/posix_wdx_tests.cpp)
    target_link_libraries(posix_wdx_tests PRIVATE ${CMAKE_DL_LIBS})
    target_compile_options(posix_wdx_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME linux_wdx_hebrew_plugin
      COMMAND posix_wdx_tests $<TARGET_FILE:SymlinkInspectorPosix>)
  endif()
endif()

if(WIN32)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "TC Symbolic Links currently supports Windows x64 only")
  endif()

  add_library(tc_link_core STATIC src/LinkCore.cpp)
  target_include_directories(tc_link_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(tc_link_core PUBLIC kernel32 tc_reparse)

  add_library(SymlinkInspector SHARED src/SymlinkInspector.cpp)
  target_link_libraries(SymlinkInspector PRIVATE tc_link_core)
  set_target_properties(SymlinkInspector PROPERTIES PREFIX "" SUFFIX ".wdx64")

  if(BUILD_TESTING)
    add_executable(windows_link_tests tests/windows_link_tests.cpp)
    target_link_libraries(windows_link_tests PRIVATE tc_link_core)
    add_test(NAME win32_hebrew_links COMMAND windows_link_tests)

    add_executable(wdx_abi_tests tests/wdx_abi_tests.cpp)
    target_link_libraries(wdx_abi_tests PRIVATE tc_link_core)
    add_test(NAME wdx_unicode_exports COMMAND wdx_abi_tests $<TARGET_FILE:SymlinkInspector>)

    if(MSVC)
      target_compile_options(windows_link_tests PRIVATE /W4 /utf-8)
      target_compile_options(wdx_abi_tests PRIVATE /W4 /utf-8)
    endif()
  endif()

  # MSVC's auto-generated manifest must be disabled because maker.rc embeds
  # an RT_MANIFEST resource with per-monitor-v2 DPI and long-path awareness.
  configure_file(src/maker.manifest "${CMAKE_CURRENT_BINARY_DIR}/maker.manifest" COPYONLY)
  add_executable(TCSymlinkMaker WIN32 src/TCSymlinkMaker.cpp src/maker.rc)
  set_source_files_properties(src/maker.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/maker.manifest")
  target_include_directories(TCSymlinkMaker PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(TCSymlinkMaker PRIVATE tc_link_core tc_list user32 gdi32 comctl32 shell32)
  if(MSVC)
    target_link_options(TCSymlinkMaker PRIVATE "/MANIFEST:NO")
  endif()

  foreach(t tc_link_core SymlinkInspector TCSymlinkMaker tc_list)
    target_compile_definitions(${t} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
    if(MSVC)
      target_compile_options(${t} PRIVATE /W4 /permissive- /utf-8 /std:c++latest)
    else()
      target_compile_options(${t} PRIVATE -Wall -Wextra)
    endif()
  endforeach()
endif()
