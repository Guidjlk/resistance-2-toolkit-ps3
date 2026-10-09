# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Guidjlk
cmake_minimum_required(VERSION 3.20)
project(r2tk-unlock-manager C)
include(ps3-self)
add_executable(r2tk-unlock-manager source/main.c source/manager.c source/sha256.c)
target_include_directories(r2tk-unlock-manager PRIVATE include)
target_compile_features(r2tk-unlock-manager PRIVATE c_std_11)
target_compile_definitions(r2tk-unlock-manager PRIVATE R2TK_PS3)
target_compile_options(r2tk-unlock-manager PRIVATE -Wall -Wextra -O2)
target_link_libraries(r2tk-unlock-manager PRIVATE gcm_cmd gcm_sys io sysutil rt lv2 m)
ps3_add_self(r2tk-unlock-manager OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/output")
set(r2_package_files "${CMAKE_BINARY_DIR}/package-files")
file(MAKE_DIRECTORY "${r2_package_files}/USRDIR/licenses")
configure_file(LICENSE "${r2_package_files}/USRDIR/LICENSE" COPYONLY)
configure_file(CREDITS.md "${r2_package_files}/USRDIR/CREDITS.md" COPYONLY)
file(GLOB r2_license_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/licenses/*")
foreach(r2_license IN LISTS r2_license_files)
    get_filename_component(r2_license_name "${r2_license}" NAME)
    configure_file("${r2_license}" "${r2_package_files}/USRDIR/licenses/${r2_license_name}" COPYONLY)
endforeach()
ps3_add_pkg(r2tk-unlock-manager APPID "R2TK00001"
    CONTENTID "UP0001-R2TK00001_00-UNLOCKMANAGER001"
    SFOXML "${CMAKE_CURRENT_SOURCE_DIR}/assets/sfo.xml"
    TITLE "Resistance 2 Tool Kit"
    ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/ICON0.PNG"
    PKGFILES "${r2_package_files}")
# The SDK tracks the executable, icon and SFO; also rebuild for license changes.
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/output/r2tk-unlock-manager.pkg"
    APPEND DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/CREDITS.md" ${r2_license_files})
