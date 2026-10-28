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
