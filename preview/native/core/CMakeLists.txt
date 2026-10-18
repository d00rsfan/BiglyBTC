cmake_minimum_required(VERSION 3.25)
set(biglybtc_core_install_default OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(biglybtc_core_install_default ON)
  project(BiglyBTCCore VERSION 0.1.0 LANGUAGES CXX)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Toolchain.cmake")
    include(cmake/Toolchain.cmake)
  else()
    include(../cmake/Toolchain.cmake)
  endif()
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Threads REQUIRED)
add_library(biglybtc_core STATIC src/bencode.cpp src/executor.cpp src/library.cpp src/state_cache.cpp src/torrent_layout.cpp src/file_policy.cpp src/saved_services.cpp src/saved_dnd.cpp)
add_library(BiglyBTC::Core ALIAS biglybtc_core)
target_include_directories(biglybtc_core PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_compile_features(biglybtc_core PUBLIC cxx_std_23)
set_target_properties(biglybtc_core PROPERTIES CXX_EXTENSIONS OFF EXPORT_NAME Core POSITION_INDEPENDENT_CODE ON)
target_compile_options(biglybtc_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(biglybtc_core PUBLIC Threads::Threads)
option(BIGLYBTC_INSTALL_CORE "Install experimental static core and development headers" "${biglybtc_core_install_default}")
if(BIGLYBTC_INSTALL_CORE)
install(TARGETS biglybtc_core EXPORT BiglyBTCCoreTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
configure_package_config_file(BiglyBTCCoreConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/BiglyBTCCoreConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BiglyBTCCore")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/BiglyBTCCoreConfigVersion.cmake"
  VERSION 0.1.0 COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/BiglyBTCCoreConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/BiglyBTCCoreConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BiglyBTCCore")
install(EXPORT BiglyBTCCoreTargets NAMESPACE BiglyBTC:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BiglyBTCCore")
install(FILES COPYING NOTICE.md DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/BiglyBTCCore")
endif()
