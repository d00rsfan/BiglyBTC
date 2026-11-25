cmake_minimum_required(VERSION 3.25)
project(BiglyBTCTorrentSdkProbe LANGUAGES CXX)
include(CTest)
find_package(LibtorrentRasterbar CONFIG REQUIRED)
add_executable(biglybtc-torrent-sdk-probe probe.cpp)
target_link_libraries(biglybtc-torrent-sdk-probe PRIVATE LibtorrentRasterbar::torrent-rasterbar)
if(WIN32)
  target_link_libraries(biglybtc-torrent-sdk-probe PRIVATE ws2_32 mswsock)
endif()
target_compile_features(biglybtc-torrent-sdk-probe PRIVATE cxx_std_20)
add_test(NAME native-torrent-sdk COMMAND biglybtc-torrent-sdk-probe)
set_tests_properties(native-torrent-sdk PROPERTIES TIMEOUT 20)

if(BIGLYBTC_RELEASE_SDK_SOURCE)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME windows-sdk-pe-exports
    COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/../../engine/runtime-binding/test_pe_exports.py")
  set(sdk_platform linux)
  if(APPLE)
    set(sdk_platform macos)
  elseif(WIN32)
    set(sdk_platform windows)
  endif()
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" sdk_processor)
  get_target_property(sdk_library LibtorrentRasterbar::torrent-rasterbar IMPORTED_LOCATION_RELEASE)
  get_target_property(sdk_includes LibtorrentRasterbar::torrent-rasterbar INTERFACE_INCLUDE_DIRECTORIES)
  set(sdk_include "")
  foreach(directory IN LISTS sdk_includes)
    if(EXISTS "${directory}/libtorrent/version.hpp")
      if(NOT sdk_include STREQUAL "")
        message(FATAL_ERROR "Ambiguous libtorrent SDK header roots")
      endif()
      set(sdk_include "${directory}")
    endif()
  endforeach()
  if(sdk_include STREQUAL "")
    message(FATAL_ERROR "Missing installed libtorrent SDK header root")
  endif()
  set(binding "${CMAKE_CURRENT_BINARY_DIR}/runtime-binding")
  file(MAKE_DIRECTORY "${binding}")
  execute_process(COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/../../engine/runtime-binding/verify_release_sdk.py"
    --source "${BIGLYBTC_RELEASE_SDK_SOURCE}" --platform "${sdk_platform}"
    --processor "${sdk_processor}" --library "${sdk_library}" --include "${sdk_include}"
    --header "${binding}/persistent_runtime_config.hpp"
    --private-header "${binding}/private_i2p_runtime_config.hpp"
    --receipt "${binding}/release-sdk.json" COMMAND_ERROR_IS_FATAL ANY)
endif()
if(NOT WIN32 AND BIGLYBTC_RELEASE_SDK_SOURCE)
  find_package(OpenSSL REQUIRED COMPONENTS Crypto)
  add_executable(biglybtc-runtime-binding-probe runtime_binding_probe.cpp
    ../../engine/persistent_runtime_guard.cpp
    ../../services/i2p_provider/owner_runtime_guard.cpp)
  target_compile_features(biglybtc-runtime-binding-probe PRIVATE cxx_std_23)
  target_include_directories(biglybtc-runtime-binding-probe PRIVATE "${binding}")
  target_compile_options(biglybtc-runtime-binding-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(biglybtc-runtime-binding-probe PRIVATE
    LibtorrentRasterbar::torrent-rasterbar OpenSSL::Crypto ${CMAKE_DL_LIBS})
  add_test(NAME native-torrent-runtime-binding COMMAND biglybtc-runtime-binding-probe)
  set_tests_properties(native-torrent-runtime-binding PROPERTIES TIMEOUT 20)
endif()
