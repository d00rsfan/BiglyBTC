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
endif()
