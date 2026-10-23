# Opt-in packaging, with temporary private Qt bundling selected by the user.
option(BIGLYBTC_DEB "Enable the experimental Linux DEB packaging target" OFF)
if(NOT BIGLYBTC_DEB)
  return()
endif()
option(BIGLYBTC_BUNDLE_QT "Bundle the configured Qt runtime privately in the DEB" ON)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_CROSSCOMPILING)
  message(FATAL_ERROR "DEB packaging currently requires a native Linux build")
endif()
if(NOT BIGLYBTC_DESKTOP OR NOT TARGET BiglyBTC OR NOT TARGET BiglyBTC-headless)
  message(FATAL_ERROR "DEB packaging requires the experimental desktop and headless targets")
endif()
set(BIGLYBTC_DEB_CONTACT "" CACHE STRING "Package maintainer name and email")
if(NOT BIGLYBTC_DEB_CONTACT MATCHES "^[^\n\r]+ <[^<>\n\r ]+@[^<>\n\r ]+>$")
  message(FATAL_ERROR "Set BIGLYBTC_DEB_CONTACT to the actual maintainer name and email")
endif()
if(NOT BIGLYBTC_QT_RUNTIME_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "DEB packaging needs the configured Qt Core runtime version")
endif()
find_program(BIGLYBTC_DPKG_SHLIBDEPS dpkg-shlibdeps REQUIRED)
find_program(BIGLYBTC_DPKG dpkg REQUIRED)
execute_process(COMMAND "${BIGLYBTC_DPKG}" --print-architecture
  OUTPUT_VARIABLE deb_arch OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)
if(NOT deb_arch STREQUAL "amd64")
  message(FATAL_ERROR "The initial DEB dependency profile is amd64; qualify other architectures separately")
endif()
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "System package install prefix" FORCE)
endif()
if(NOT CMAKE_INSTALL_PREFIX STREQUAL "/usr")
  message(FATAL_ERROR "The experimental DEB layout requires CMAKE_INSTALL_PREFIX=/usr")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(BIGLYBTC_DEB_RUNTIME_PACKAGE_ROOT "" CACHE PATH "Extracted Debian runtime package with original DEBIAN metadata (optional)")
if(BIGLYBTC_DEB_RUNTIME_PACKAGE_ROOT AND
   (NOT EXISTS "${BIGLYBTC_DEB_RUNTIME_PACKAGE_ROOT}/DEBIAN/control" OR
    NOT EXISTS "${BIGLYBTC_DEB_RUNTIME_PACKAGE_ROOT}/DEBIAN/shlibs"))
  message(FATAL_ERROR "The external runtime package requires its original Debian control and shlibs metadata")
endif()
if(BIGLYBTC_BUNDLE_QT)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/qt_bundle.py")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/qt_bundle.py"
    --sdk "${BIGLYBTC_QT_RUNTIME_ROOT}" --version "${BIGLYBTC_QT_RUNTIME_VERSION}"
    --notices "${CMAKE_CURRENT_LIST_DIR}/notices"
    --output "${CMAKE_CURRENT_BINARY_DIR}/qt-runtime.json"
    COMMAND_ERROR_IS_FATAL ANY)
  file(READ "${CMAKE_CURRENT_BINARY_DIR}/qt-runtime.json" qt_plan)
  string(JSON qt_file_count LENGTH "${qt_plan}" files)
  math(EXPR qt_last_file "${qt_file_count} - 1")
  foreach(index RANGE 0 ${qt_last_file})
    string(JSON source GET "${qt_plan}" files ${index} source)
    string(JSON destination GET "${qt_plan}" files ${index} destination)
    string(REGEX REPLACE "^usr/" "" destination "${destination}")
    get_filename_component(directory "${destination}" DIRECTORY)
    get_filename_component(name "${destination}" NAME)
    install(FILES "${source}" DESTINATION "${directory}" RENAME "${name}")
  endforeach()
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/qt-runtime.json"
    DESTINATION share/doc/biglybtc-experimental)
  set_target_properties(BiglyBTC BiglyBTC-headless PROPERTIES
    INSTALL_RPATH "$ORIGIN/../qt/lib" INSTALL_RPATH_USE_LINK_PATH FALSE)
  install(TARGETS BiglyBTC BiglyBTC-headless RUNTIME DESTINATION lib/biglybtc/bin)
  foreach(application IN ITEMS BiglyBTC BiglyBTC-headless)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${application}-launcher"
      "#!/bin/sh\nexec /usr/lib/biglybtc/bin/${application} \"\$@\"\n")
    install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/${application}-launcher"
      DESTINATION bin RENAME "${application}")
  endforeach()
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/biglybtc-qt.conf"
    "[Paths]\nPrefix=../qt\nLibraries=lib\nPlugins=plugins\nTranslations=translations\n")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/biglybtc-qt.conf" DESTINATION lib/biglybtc/bin RENAME qt.conf)
  set(biglybtc_qt_variant "bundled-qt")
else()
  set_target_properties(BiglyBTC BiglyBTC-headless PROPERTIES
    INSTALL_RPATH "" INSTALL_RPATH_USE_LINK_PATH FALSE)
  install(TARGETS BiglyBTC BiglyBTC-headless RUNTIME DESTINATION bin)
  set(biglybtc_qt_variant "system-qt")
endif()
install(FILES "${CMAKE_CURRENT_LIST_DIR}/../../LICENSE"
  DESTINATION share/doc/biglybtc-experimental RENAME copyright)
install(FILES "${CMAKE_CURRENT_LIST_DIR}/EXPERIMENTAL.txt"
  DESTINATION share/doc/biglybtc-experimental)
install(PROGRAMS "${CMAKE_CURRENT_LIST_DIR}/preview.py" DESTINATION bin RENAME BiglyBTC-preview)
install(FILES "${CMAKE_CURRENT_LIST_DIR}/BiglyBTC-preview.desktop" DESTINATION share/applications)
if(TARGET biglybtc_desktop_host)
  find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Test GLOBAL)
  add_executable(biglybtc-preview-smoke "${CMAKE_CURRENT_LIST_DIR}/preview_smoke.cpp")
  set_target_properties(biglybtc-preview-smoke PROPERTIES AUTOMOC ON)
  target_link_libraries(biglybtc-preview-smoke PRIVATE biglybtc_desktop_host Qt6::Test)
  target_compile_options(biglybtc-preview-smoke PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "biglybtc-experimental")
set(CPACK_PACKAGE_CONTACT "${BIGLYBTC_DEB_CONTACT}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}~dev")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "BiglyBTC experimental native client")
set(CPACK_PACKAGE_DESCRIPTION "Experimental C++23/Qt desktop and headless client.\n Only generated development profiles are supported. Full migration is unfinished.")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_SET_DESTDIR ON)
set(CPACK_DEBIAN_FILE_NAME "biglybtc-experimental_${PROJECT_VERSION}~dev_amd64_${biglybtc_qt_variant}.deb")
set(CPACK_DEBIAN_PACKAGE_SECTION net)
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${deb_arch}")
set(CPACK_DEBIAN_COMPRESSION_TYPE xz)
set(CPACK_THREADS 1)
set(CPACK_STRIP_FILES ON)
# CPack normally adds --ignore-missing-info itself. Override its tool with a
# strict wrapper, with local metadata solely for our package's private Qt/ICU.
configure_file("${CMAKE_CURRENT_LIST_DIR}/strict_shlibdeps.py.in"
  "${CMAKE_CURRENT_BINARY_DIR}/biglybtc-dpkg-shlibdeps" @ONLY)
file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/biglybtc-dpkg-shlibdeps"
  PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/BiglyBTCPack.cmake"
  "set(SHLIBDEPS_EXECUTABLE \"${CMAKE_CURRENT_BINARY_DIR}/biglybtc-dpkg-shlibdeps\")\n")
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_BINARY_DIR}/BiglyBTCPack.cmake")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# Ubuntu 26.04 amd64 is the first build baseline; qualify Mint/Debian ABIs separately.
# Qt platform plugins are loaded dynamically and cannot be found by shlibdeps.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "python3")
if(NOT BIGLYBTC_BUNDLE_QT)
  set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "python3, libqt6core6t64 (>= ${BIGLYBTC_QT_RUNTIME_VERSION}), libqt6gui6 (>= ${BIGLYBTC_QT_RUNTIME_VERSION}), libqt6widgets6 (>= ${BIGLYBTC_QT_RUNTIME_VERSION}), qt6-qpa-plugins (>= ${BIGLYBTC_QT_RUNTIME_VERSION})")
  set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-wayland (>= ${BIGLYBTC_QT_RUNTIME_VERSION})")
endif()
include(CPack)
