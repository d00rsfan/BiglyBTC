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

if((APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux") AND BIGLYBTC_RELEASE_SDK_SOURCE)
  find_package(OpenSSL REQUIRED COMPONENTS Crypto)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(sdk_platform linux)
  if(APPLE)
    set(sdk_platform macos)
  endif()
  get_target_property(sdk_library LibtorrentRasterbar::torrent-rasterbar IMPORTED_LOCATION_RELEASE)
  get_target_property(sdk_include LibtorrentRasterbar::torrent-rasterbar INTERFACE_INCLUDE_DIRECTORIES)
  set(binding "${CMAKE_CURRENT_BINARY_DIR}/runtime-binding")
  file(MAKE_DIRECTORY "${binding}")
  execute_process(COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/../../engine/runtime-binding/verify_release_sdk.py"
    --source "${BIGLYBTC_RELEASE_SDK_SOURCE}" --platform "${sdk_platform}"
    --processor "${CMAKE_SYSTEM_PROCESSOR}" --library "${sdk_library}" --include "${sdk_include}"
    --header "${binding}/persistent_runtime_config.hpp"
    --private-header "${binding}/private_i2p_runtime_config.hpp"
    --receipt "${binding}/release-sdk.json" COMMAND_ERROR_IS_FATAL ANY)
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
