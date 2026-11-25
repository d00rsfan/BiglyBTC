cmake_minimum_required(VERSION 3.25)
project(BiglyBTCStorageTests LANGUAGES CXX)
enable_testing()

find_package(LibtorrentRasterbar 2.0.12 EXACT CONFIG REQUIRED)
add_executable(biglybtc-sparse-storage-test sparse_storage_test.cpp)
target_compile_features(biglybtc-sparse-storage-test PRIVATE cxx_std_20)
target_link_libraries(biglybtc-sparse-storage-test PRIVATE LibtorrentRasterbar::torrent-rasterbar)
add_test(NAME sparse-storage COMMAND biglybtc-sparse-storage-test)
set_tests_properties(sparse-storage PROPERTIES TIMEOUT 195)

if(WIN32)
  add_subdirectory(../../core native-core EXCLUDE_FROM_ALL)
  add_subdirectory(.. native-platform EXCLUDE_FROM_ALL)
  target_sources(biglybtc-sparse-storage-test PRIVATE
    ../../engine/metadata_disk_router.cpp ../../engine/original_disk_io.cpp ../../engine/disk_latency.cpp
    ../../adapters/original_file_io.cpp ../../adapters/original_payload_io.cpp)
  target_include_directories(biglybtc-sparse-storage-test PRIVATE ../../engine ../../adapters ..)
  target_link_libraries(biglybtc-sparse-storage-test PRIVATE BiglyBTC::Core advapi32)
  if(MSVC)
    target_compile_options(biglybtc-sparse-storage-test PRIVATE /utf-8 /Zc:__cplusplus)
  endif()
  add_test(NAME original-disk-sparse-storage COMMAND biglybtc-sparse-storage-test --original-disk)
  set_tests_properties(original-disk-sparse-storage PROPERTIES TIMEOUT 90)

  find_package(Threads REQUIRED)
  find_package(ZLIB REQUIRED)
  add_library(biglybtc_windows_torrent_state STATIC
    ../../adapters/application_settings.cpp ../../adapters/encoded_document.cpp
    ../../adapters/library_view.cpp ../../adapters/profile_metadata.cpp
    ../../adapters/profile_config_read.cpp ../../adapters/profile_config_windows.cpp
    ../../adapters/state_file_io_windows.cpp ../../adapters/torrent_state.cpp)
  target_link_libraries(biglybtc_windows_torrent_state PUBLIC biglybtc_profile_lease BiglyBTC::Core
    PRIVATE ZLIB::ZLIB Threads::Threads)
  add_executable(biglybtc-torrent-state-tests ../torrent_state_tests.cpp)
  target_link_libraries(biglybtc-torrent-state-tests PRIVATE biglybtc_windows_torrent_state ZLIB::ZLIB)
  if(MSVC)
    target_compile_options(biglybtc_windows_torrent_state PRIVATE /utf-8 /Zc:__cplusplus)
    target_compile_options(biglybtc-torrent-state-tests PRIVATE /utf-8 /Zc:__cplusplus)
  endif()
  add_test(NAME torrent-state-transactions COMMAND biglybtc-torrent-state-tests)
  set_tests_properties(torrent-state-transactions PROPERTIES TIMEOUT 25)
endif()
