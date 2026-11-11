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

target_sources(biglybtc_local_engine PRIVATE network_policy.cpp managed_storage.cpp mainline_dht.cpp)
target_link_libraries(biglybtc_local_engine PRIVATE biglybtc_plugin_settings)

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
find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
target_link_libraries(biglybtc_local_engine PRIVATE OpenSSL::Crypto)
target_sources(biglybtc_profile PRIVATE ../adapters/file_operations.cpp ../adapters/file_operation_io.cpp)
target_link_libraries(biglybtc_profile PRIVATE OpenSSL::Crypto)

target_sources(biglybtc_local_engine PRIVATE original_disk_io.cpp original_control.cpp)

target_sources(biglybtc_profile PRIVATE ../adapters/linear_to_compact.cpp)

target_sources(biglybtc_local_engine PRIVATE network_binding.cpp)

# Verified optional SOCKS4 capability, retained for the current owner.
target_sources(biglybtc_local_engine PRIVATE network_proxy_capability.cpp)

target_sources(biglybtc_local_engine PRIVATE disk_latency.cpp)

# Original saved group-position normalization under profile authority.
target_sources(biglybtc_profile PRIVATE ../adapters/queue_positions.cpp)

# Original descriptor linkage >=100 leaves uses its source-defined gzip envelope.
find_package(ZLIB REQUIRED)
target_link_libraries(biglybtc_local_engine PRIVATE ZLIB::ZLIB)

target_link_libraries(biglybtc_local_engine PRIVATE biglybtc_mainline_checkpoint)

# Queued BEP33 uses the same frozen codec in desktop and headless owners.
add_library(biglybtc_mainline_scrape STATIC ../services/mainline_scrape.cpp)
target_include_directories(biglybtc_mainline_scrape PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../services")
target_link_libraries(biglybtc_mainline_scrape PUBLIC BiglyBTC::Core PRIVATE OpenSSL::Crypto)
target_compile_features(biglybtc_mainline_scrape PUBLIC cxx_std_23)
target_compile_options(biglybtc_mainline_scrape PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_sources(biglybtc_local_engine PRIVATE queued_mainline_scrape.cpp)
target_link_libraries(biglybtc_local_engine PRIVATE biglybtc_mainline_scrape)

# Explicit optional original-root source reader, absent on stock/default builds.
if(TARGET biglybtc_mainline_identity)
  target_link_libraries(biglybtc_local_engine PRIVATE biglybtc_mainline_identity)
  target_compile_definitions(biglybtc_local_engine PRIVATE BIGLYBTC_ORIGINAL_MLDHT_IDENTITY_READER=1)
else()
  target_compile_definitions(biglybtc_local_engine PRIVATE BIGLYBTC_ORIGINAL_MLDHT_IDENTITY_READER=0)
endif()

# Append under preview/native/engine after profile/plugin_settings/LT targets.
# Owner activation consumes this component; no provider build or policy permission change.
add_library(biglybtc_mainline_bind_plan STATIC mainline_bind_plan.hpp mainline_bind_plan.cpp)
target_include_directories(biglybtc_mainline_bind_plan PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(biglybtc_mainline_bind_plan PUBLIC biglybtc_profile PRIVATE biglybtc_plugin_settings LibtorrentRasterbar::torrent-rasterbar)
target_compile_features(biglybtc_mainline_bind_plan PUBLIC cxx_std_23)
target_compile_options(biglybtc_mainline_bind_plan PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(biglybtc_local_engine PRIVATE biglybtc_mainline_bind_plan)


# Worker-owned direct tracker stats; private Qt event loop, no GUI dependency.
find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Network)
target_sources(biglybtc_local_engine PRIVATE library_http_tracker.cpp library_http_protocol.cpp)
target_link_libraries(biglybtc_local_engine PRIVATE Qt6::Network)

# Separate explicit I2P-only preparation path; existing Public policy unchanged.
target_sources(biglybtc_local_engine PRIVATE owned_i2p_policy.cpp mixed_i2p_source.cpp mixed_i2p_transport.cpp)

# Optional discovery types are available only in the separately reviewed SDK.
# Probe the selected headers; old/default SDKs keep the code compiled out.
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_LIBRARIES LibtorrentRasterbar::torrent-rasterbar)
unset(BIGLYBTC_HAS_I2P_DISCOVERY_SDK CACHE)
check_cxx_source_compiles("#include <libtorrent/biglybtc_private_i2p_owner.hpp>
static_assert(libtorrent::BiglybtcI2pDiscoveryAbiV1 == 0x42494401);
static_assert(sizeof(libtorrent::BiglybtcI2pDiscoveredPeerV1) == 128);
int main() { return 0; }" BIGLYBTC_HAS_I2P_DISCOVERY_SDK)
unset(CMAKE_REQUIRED_LIBRARIES)
if(BIGLYBTC_HAS_I2P_DISCOVERY_SDK)
  target_compile_definitions(biglybtc_local_engine PUBLIC BIGLYBTC_I2P_DISCOVERY_ABI_V1=1)
endif()

# Shared owning HTTP values only. Socket/route/source authority stays in its owner.
add_library(biglybtc_http_response STATIC ../services/http/http_response.cpp)
target_include_directories(biglybtc_http_response PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../services/http")
target_link_libraries(biglybtc_http_response PUBLIC Qt6::Core PRIVATE ZLIB::ZLIB)
target_compile_features(biglybtc_http_response PUBLIC cxx_std_23)
target_compile_options(biglybtc_http_response PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(biglybtc_http_response PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(biglybtc_local_engine PRIVATE biglybtc_http_response)

# Intake.cpp is added to profile by this module only when LOCAL_ENGINE exists.
# The fixed codec is Qt-free and links only Core, including headless builds.
if(TARGET biglybtc_profile AND TARGET biglybtc_local_engine)
  if(NOT TARGET biglybtc_subscription_download_values)
    add_library(biglybtc_subscription_download_values STATIC ../services/search/subscription_download_values.cpp)
    target_include_directories(biglybtc_subscription_download_values PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../services/search")
    target_link_libraries(biglybtc_subscription_download_values PUBLIC BiglyBTC::Core)
    target_compile_features(biglybtc_subscription_download_values PUBLIC cxx_std_23)
    target_compile_options(biglybtc_subscription_download_values PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  target_link_libraries(biglybtc_profile PUBLIC biglybtc_subscription_download_values)
  target_sources(biglybtc_profile PRIVATE ../adapters/subscription_download_preparation.hpp
    ../adapters/subscription_state_images.inc ../adapters/existing_subscription_target.hpp ../adapters/existing_subscription_target.inc)
endif()

# SDK-public metadata/peer codec, independent of the engine/GUI owner.
add_library(biglybtc_azureus_torrent STATIC azureus_torrent.cpp azureus_torrent_sources.cpp)
target_include_directories(biglybtc_azureus_torrent PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(biglybtc_azureus_torrent PUBLIC biglybtc_profile PRIVATE LibtorrentRasterbar::torrent-rasterbar)
target_compile_options(biglybtc_azureus_torrent PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(biglybtc_local_engine PRIVATE biglybtc_azureus_torrent)
