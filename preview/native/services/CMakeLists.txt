find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Core Network GLOBAL)
find_package(OpenSSL 3.5.5 EXACT REQUIRED COMPONENTS Crypto)
add_library(biglybtc_azureus_owned_udp STATIC
  azureus_dht/headers.cpp
  azureus_ping/options.cpp
  azureus_positions/positions.cpp
  azureus_contacts/contacts.cpp
  azureus_nodes/nodes.cpp
  azureus_values/values.cpp
  azureus_packets/ping.cpp
  azureus_find_node/find_node.cpp
  azureus_find_value/find_value.cpp
  azureus_store/store.cpp
  azureus_error/error.cpp
  azureus_auth/authority.cpp
  azureus_store_policy/store_policy.cpp
  azureus_udp/owned_transport.cpp
  azureus_router/router.cpp
  azureus_db_value/db_value.cpp
  azureus_db_mapping/mapping.cpp
  azureus_db_runtime/database.cpp
  azureus_db_runtime/storage_stats.cpp
)
target_include_directories(biglybtc_azureus_owned_udp PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/azureus_dht"
  "${CMAKE_CURRENT_SOURCE_DIR}/azureus_ping"
  "${CMAKE_CURRENT_SOURCE_DIR}/azureus_positions"
  "${CMAKE_CURRENT_SOURCE_DIR}/azureus_contacts"
  "${CMAKE_CURRENT_SOURCE_DIR}/azureus_nodes"
  "${CMAKE_CURRENT_SOURCE_DIR}/azureus_values"
  "${CMAKE_CURRENT_SOURCE_DIR}/azureus_packets"
  "${CMAKE_CURRENT_SOURCE_DIR}/azureus_find_node"
  "${CMAKE_CURRENT_SOURCE_DIR}/azureus_find_value"
  "${CMAKE_CURRENT_SOURCE_DIR}/azureus_store"
  "${CMAKE_CURRENT_SOURCE_DIR}/azureus_error"
  "${CMAKE_CURRENT_SOURCE_DIR}/azureus_auth"
  "${CMAKE_CURRENT_SOURCE_DIR}/azureus_store_policy"
  "${CMAKE_CURRENT_SOURCE_DIR}/azureus_udp"
  "${CMAKE_CURRENT_SOURCE_DIR}/azureus_router"
  "${CMAKE_CURRENT_SOURCE_DIR}/azureus_db_value"
  "${CMAKE_CURRENT_SOURCE_DIR}/azureus_db_mapping"
  "${CMAKE_CURRENT_SOURCE_DIR}/azureus_db_runtime"
)
target_link_libraries(biglybtc_azureus_owned_udp PUBLIC Qt6::Core Qt6::Network PRIVATE OpenSSL::Crypto Threads::Threads)
target_compile_features(biglybtc_azureus_owned_udp PUBLIC cxx_std_23)
target_compile_options(biglybtc_azureus_owned_udp PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(biglybtc_azureus_owned_udp PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Explicit iterative GET policy has no Qt or I/O dependency.
add_library(biglybtc_azureus_control STATIC azureus_control/lookup.cpp)
target_include_directories(biglybtc_azureus_control PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/azureus_control"
  "${CMAKE_CURRENT_SOURCE_DIR}/azureus_nodes"
  "${CMAKE_CURRENT_SOURCE_DIR}/azureus_dht")
target_compile_features(biglybtc_azureus_control PUBLIC cxx_std_23)
target_compile_options(biglybtc_azureus_control PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(biglybtc_azureus_control PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Opt-in volatile local diversification; no persistence or default activation.
target_sources(biglybtc_azureus_owned_udp PRIVATE azureus_db_runtime/storage_diversification.cpp)

# Early host service export; explicit optional dependency, no daemon discovery.
option(BIGLYBTC_TOR_CONTROL "Build the owned Tor controller services" OFF)
set(BIGLYBTC_TOR_AUTH_SOURCE_DIR "" CACHE PATH "Explicit BiglyBTC-plugin-aznettor source checkout")
if(BIGLYBTC_TOR_CONTROL)
  if(NOT IS_ABSOLUTE "${BIGLYBTC_TOR_AUTH_SOURCE_DIR}" OR NOT EXISTS "${BIGLYBTC_TOR_AUTH_SOURCE_DIR}/safecookie.hpp")
    message(FATAL_ERROR "Tor control requires explicit qualified aznettor source")
  endif()
  add_subdirectory("${BIGLYBTC_TOR_AUTH_SOURCE_DIR}" "tor-auth")
  add_subdirectory(tor_control)
endif()

# Append to preview/native/services/CMakeLists.txt; pure, inactive codec only.
find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
add_library(biglybtc_i2p_datagram STATIC i2p_datagram/datagram.cpp)
target_include_directories(biglybtc_i2p_datagram PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/i2p_datagram")
target_link_libraries(biglybtc_i2p_datagram PRIVATE OpenSSL::Crypto)
target_compile_features(biglybtc_i2p_datagram PUBLIC cxx_std_23)
target_compile_options(biglybtc_i2p_datagram PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(biglybtc_i2p_client_identity STATIC i2p_client/identity.cpp)
target_include_directories(biglybtc_i2p_client_identity PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/i2p_client")
target_link_libraries(biglybtc_i2p_client_identity PUBLIC biglybtc_i2p_datagram PRIVATE OpenSSL::Crypto)
target_compile_features(biglybtc_i2p_client_identity PUBLIC cxx_std_23)
target_compile_options(biglybtc_i2p_client_identity PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(biglybtc_i2cp_protocol STATIC i2cp/protocol.cpp)
target_include_directories(biglybtc_i2cp_protocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/i2cp")
target_compile_features(biglybtc_i2cp_protocol PUBLIC cxx_std_23)
target_compile_options(biglybtc_i2cp_protocol PRIVATE -Wall -Wextra -Wpedantic -Werror)

# This explicit channel has no router selection/startup or automatic activation.
add_library(biglybtc_i2cp_initial_channel STATIC
  i2cp_channel/channel.hpp i2cp_channel/channel.cpp)
set_target_properties(biglybtc_i2cp_initial_channel PROPERTIES AUTOMOC ON)
target_include_directories(biglybtc_i2cp_initial_channel PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/i2cp_channel")
target_link_libraries(biglybtc_i2cp_initial_channel PUBLIC biglybtc_i2cp_protocol Qt6::Core Qt6::Network)
target_compile_features(biglybtc_i2cp_initial_channel PUBLIC cxx_std_23)
target_compile_options(biglybtc_i2cp_initial_channel PRIVATE -Wall -Wextra -Wpedantic -Werror)

# Append to services/CMakeLists after biglybtc_i2cp_initial_channel exists.
add_library(biglybtc_i2cp_session_config STATIC i2cp_session/session.cpp)
target_include_directories(biglybtc_i2cp_session_config PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/i2cp_session")
target_link_libraries(biglybtc_i2cp_session_config PUBLIC biglybtc_i2p_client_identity biglybtc_i2cp_protocol)
target_compile_features(biglybtc_i2cp_session_config PUBLIC cxx_std_23)
target_compile_options(biglybtc_i2cp_session_config PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(biglybtc_i2cp_initial_channel PUBLIC biglybtc_i2cp_session_config)

# Append to services/CMakeLists after session_config and initial_channel exist.
add_library(biglybtc_i2cp_leaseset2 STATIC i2cp_leaseset/leaseset.cpp)
target_include_directories(biglybtc_i2cp_leaseset2 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/i2cp_leaseset")
target_link_libraries(biglybtc_i2cp_leaseset2 PUBLIC biglybtc_i2cp_session_config)
target_compile_features(biglybtc_i2cp_leaseset2 PUBLIC cxx_std_23)
target_compile_options(biglybtc_i2cp_leaseset2 PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(biglybtc_i2cp_initial_channel PUBLIC biglybtc_i2cp_leaseset2)

# Existing exact dependency, already required by adapters. No new zlib backend.
if(NOT TARGET ZLIB::ZLIB)
  find_package(ZLIB 1.3.1 EXACT REQUIRED)
endif()
add_library(biglybtc_i2cp_datagram STATIC i2cp_datagram/messages.cpp)
target_include_directories(biglybtc_i2cp_datagram PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/i2cp_datagram")
target_link_libraries(biglybtc_i2cp_datagram PUBLIC biglybtc_i2cp_session_config ZLIB::ZLIB)
target_compile_features(biglybtc_i2cp_datagram PUBLIC cxx_std_23)
target_compile_options(biglybtc_i2cp_datagram PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(biglybtc_i2cp_initial_channel PUBLIC biglybtc_i2cp_datagram)

# Services section: include belongs to the existing actual channel target.
target_sources(biglybtc_i2cp_initial_channel PRIVATE i2cp_channel/receive.inc)

# Services: append after the existing channel/datagram targets. The shared
# BEP15 model is header-only core code, with no libtorrent/provider dependency.
add_library(biglybtc_i2cp_tracker_codec STATIC i2cp_tracker/tracker.cpp)
target_include_directories(biglybtc_i2cp_tracker_codec PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/i2cp_tracker")
target_link_libraries(biglybtc_i2cp_tracker_codec PUBLIC biglybtc_i2cp_datagram BiglyBTC::Core)
target_compile_features(biglybtc_i2cp_tracker_codec PUBLIC cxx_std_23)
target_compile_options(biglybtc_i2cp_tracker_codec PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_sources(biglybtc_i2cp_initial_channel PRIVATE i2cp_channel/tracker.inc)
target_link_libraries(biglybtc_i2cp_initial_channel PUBLIC biglybtc_i2cp_tracker_codec PRIVATE OpenSSL::Crypto)
