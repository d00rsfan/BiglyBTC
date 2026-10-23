if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()
find_package(LibtorrentRasterbar 2.0.12 EXACT CONFIG REQUIRED GLOBAL)
add_library(biglybtc_local_engine STATIC local_session.cpp multifile_session.cpp engine_statistics_sampler.cpp persistent_session.cpp)
target_include_directories(biglybtc_local_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(biglybtc_local_engine PUBLIC biglybtc_profile PRIVATE LibtorrentRasterbar::torrent-rasterbar)
target_compile_options(biglybtc_local_engine PRIVATE -Wall -Wextra -Wpedantic -Werror)

include("${CMAKE_CURRENT_SOURCE_DIR}/runtime-binding/PersistentRuntimeBinding.cmake")

target_sources(biglybtc_local_engine PRIVATE runtime_metrics_sampler.cpp)

target_sources(biglybtc_local_engine PRIVATE effective_rate_limits.cpp)

target_sources(biglybtc_local_engine PRIVATE peer_payload_observer.cpp)

target_sources(biglybtc_local_engine PRIVATE tracker_statistics_sampler.cpp)
