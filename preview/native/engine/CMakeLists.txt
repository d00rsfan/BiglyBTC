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

target_sources(biglybtc_local_engine PRIVATE network_policy.cpp managed_storage.cpp)

# Intake validates metadata against the same exact engine dependency. The
# profile library remains independent of the native engine owner and all UI.
target_sources(biglybtc_profile PRIVATE ../adapters/torrent_intake.cpp)
target_link_libraries(biglybtc_profile PRIVATE LibtorrentRasterbar::torrent-rasterbar)

target_sources(biglybtc_local_engine PRIVATE magnet_intake.cpp metadata_disk_router.cpp)

# The reviewed provider owns all private layout/stat reads. This TU uses only
# public handles and a fixed supported export: keep ALL sanitizer checks enabled.
target_sources(biglybtc_local_engine PRIVATE managed_payload_counters.cpp)
target_link_libraries(biglybtc_local_engine PRIVATE ${CMAKE_DL_LIBS})

target_sources(biglybtc_local_engine PRIVATE live_settings.cpp live_settings.hpp)

target_sources(biglybtc_local_engine PRIVATE torrent_creator.cpp)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
target_sources(biglybtc_profile PRIVATE ../adapters/file_operations.cpp ../adapters/file_operation_io.cpp)
target_link_libraries(biglybtc_profile PRIVATE OpenSSL::Crypto)

target_sources(biglybtc_local_engine PRIVATE original_disk_io.cpp original_control.cpp)

target_sources(biglybtc_profile PRIVATE ../adapters/linear_to_compact.cpp)

target_sources(biglybtc_local_engine PRIVATE network_binding.cpp)

# Verified optional SOCKS4 capability, retained for the current owner.
target_sources(biglybtc_local_engine PRIVATE network_proxy_capability.cpp)
