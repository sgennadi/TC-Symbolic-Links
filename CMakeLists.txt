cmake_minimum_required(VERSION 3.26)
project(TCSymbolicLinks VERSION 0.2.16 LANGUAGES CXX)
# CI preview releases use the matching GitHub workflow run number.
set(TC_PREVIEW_ITERATION 0 CACHE STRING "Preview build serial; 0 for local builds")
option(TC_STABLE_RELEASE "Compile the official stable release version" OFF)
if(TC_STABLE_RELEASE)
  set(TC_STABLE_RELEASE_DEFINE 1)
else()
  set(TC_STABLE_RELEASE_DEFINE 0)
endif()
if(NOT TC_PREVIEW_ITERATION MATCHES "^[0-9]+$")
  message(FATAL_ERROR "TC_PREVIEW_ITERATION must be a decimal integer")
endif()
string(LENGTH "${TC_PREVIEW_ITERATION}" TC_ITERATION_DIGITS)
if(TC_ITERATION_DIGITS GREATER 8)
  message(FATAL_ERROR "TC_PREVIEW_ITERATION must contain at most eight decimal digits")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(CTest)

# Cross-platform regression: TC custom column sets have SpecialView=index+2.
# Also run on Linux to catch regressions before the first Windows CI build.
if(BUILD_TESTING)
  add_executable(tc_custom_view_tests tests/tc_custom_view_tests.cpp)
  target_include_directories(tc_custom_view_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  add_test(NAME tc_custom_view_indices COMMAND tc_custom_view_tests)
  add_executable(smb_path_tests tests/smb_path_tests.cpp)
  target_include_directories(smb_path_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  add_test(NAME smb_unc_share_boundaries COMMAND smb_path_tests)
endif()

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
  # Both x86 and x64 WDX modules are built from identical source.
  # Total Commander chooses the matching module by .wdx/.wdx64 suffix.
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(TC_WDX_SUFFIX ".wdx64")
  else()
    set(TC_WDX_SUFFIX ".wdx")
  endif()

  add_library(tc_link_core STATIC src/LinkCore.cpp)
  target_include_directories(tc_link_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(tc_link_core PUBLIC kernel32 tc_reparse mpr)

  # Both native Maker and startup registration diagnosis use UpdateManager.
  # Keep it in tc_integration so test executables link the same code as WDX.
  add_library(tc_integration STATIC src/TcIntegration.cpp src/UpdateManager.cpp)
  target_include_directories(tc_integration PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_definitions(tc_integration PRIVATE TC_PREVIEW_ITERATION=${TC_PREVIEW_ITERATION} TC_STABLE_RELEASE=${TC_STABLE_RELEASE_DEFINE})
  target_link_libraries(tc_integration PUBLIC user32 advapi32)
  add_library(SymlinkInspector SHARED src/SymlinkInspector.cpp src/TcMakerGui.cpp
    src/TcLauncher.cpp src/LinkMaker.rc)
  # The updater is raw data embedded in this same WDX, not a companion EXE.
  set_property(SOURCE src/LinkMaker.rc APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Update-TC-SymbolicLinks.ps1")
  target_compile_definitions(SymlinkInspector PRIVATE TC_PREVIEW_ITERATION=${TC_PREVIEW_ITERATION} TC_STABLE_RELEASE=${TC_STABLE_RELEASE_DEFINE})
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    # Undecorated WDX and rundll32 names for MSVC x86 __stdcall.
    target_sources(SymlinkInspector PRIVATE src/SymlinkInspector.x86.def)
  endif()
  target_include_directories(SymlinkInspector PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(SymlinkInspector PRIVATE tc_link_core tc_list tc_integration user32 gdi32 gdiplus dwmapi comctl32 shell32 ole32 uuid advapi32)
  set_target_properties(SymlinkInspector PROPERTIES PREFIX "" SUFFIX "${TC_WDX_SUFFIX}")

  if(BUILD_TESTING)
    # Offscreen GDI+ theme acceptance does not depend on a visible desktop.
    add_executable(windows_visual_theme_tests tests/windows_visual_theme_tests.cpp)
    target_include_directories(windows_visual_theme_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(windows_visual_theme_tests PRIVATE user32 gdi32 gdiplus)
    add_test(NAME tc_nipkg_theme_offscreen COMMAND windows_visual_theme_tests)
    if(MSVC)
      target_compile_options(windows_visual_theme_tests PRIVATE /W4 /utf-8)
    endif()

    add_executable(windows_link_tests tests/windows_link_tests.cpp)
    target_link_libraries(windows_link_tests PRIVATE tc_link_core)
    add_test(NAME win32_hebrew_links COMMAND windows_link_tests)

    # Optional real loopback SMB test; invoked by the dedicated CI script
    # after publishing a unique temporary Windows share. Never touches any
    # existing user SMB share, and is excluded from ordinary local CTest.
    add_executable(windows_smb_smoke_tests tests/windows_smb_smoke_tests.cpp)
    target_link_libraries(windows_smb_smoke_tests PRIVATE tc_link_core)
    if(MSVC)
      target_compile_options(windows_smb_smoke_tests PRIVATE /W4 /utf-8)
    endif()

    add_executable(wdx_abi_tests tests/wdx_abi_tests.cpp)
    target_link_libraries(wdx_abi_tests PRIVATE tc_link_core)
    add_test(NAME wdx_unicode_exports COMMAND wdx_abi_tests $<TARGET_FILE:SymlinkInspector>)

    add_executable(windows_maker_folder_tests tests/windows_maker_folder_tests.cpp)
    target_link_libraries(windows_maker_folder_tests PRIVATE tc_link_core)
    add_test(NAME tc_real_folder_selection_and_junction_gui
      COMMAND windows_maker_folder_tests $<TARGET_FILE:SymlinkInspector>)
    set_tests_properties(tc_real_folder_selection_and_junction_gui PROPERTIES TIMEOUT 45)

    if(MSVC)
      target_compile_options(windows_link_tests PRIVATE /W4 /utf-8)
      target_compile_options(wdx_abi_tests PRIVATE /W4 /utf-8)
      target_compile_options(windows_maker_folder_tests PRIVATE /W4 /utf-8)
    endif()
  endif()

  if(BUILD_TESTING)
    add_executable(windows_default_bar_tests tests/windows_default_bar_tests.cpp)
    target_link_libraries(windows_default_bar_tests PRIVATE tc_integration)
    add_test(NAME tc_implicit_default_toolbar_install
      COMMAND windows_default_bar_tests $<TARGET_FILE:SymlinkInspector>)
    set_tests_properties(tc_implicit_default_toolbar_install PROPERTIES TIMEOUT 40)
    if(MSVC)
      target_compile_options(windows_default_bar_tests PRIVATE /W4 /utf-8)
    endif()
    add_executable(windows_integration_tests tests/windows_integration_tests.cpp)
    target_link_libraries(windows_integration_tests PRIVATE tc_integration shell32)
    add_test(NAME tc_one_dll_installer_and_rundll32
      COMMAND windows_integration_tests $<TARGET_FILE:SymlinkInspector>)
    set_tests_properties(tc_one_dll_installer_and_rundll32 PROPERTIES TIMEOUT 70)
    # Regress the exact on-disk wincmd.ini damage seen with v0.2.1 without
    # modifying the real user profile or any Total Commander installation.
    find_program(TC_POWERSHELL NAMES pwsh powershell)
    if(TC_POWERSHELL)
      add_test(NAME tc_toolbar_only_recovery
        COMMAND "${TC_POWERSHELL}" -NoProfile -File
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_recovery.ps1")
    endif()
    if(MSVC)
      target_compile_options(windows_integration_tests PRIVATE /W4 /utf-8)
    endif()
  endif()

  foreach(t tc_link_core SymlinkInspector tc_list tc_integration)
    target_compile_definitions(${t} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
    if(MSVC)
      target_compile_options(${t} PRIVATE /W4 /permissive- /utf-8 /std:c++latest)
    else()
      target_compile_options(${t} PRIVATE -Wall -Wextra)
    endif()
  endforeach()
endif()
