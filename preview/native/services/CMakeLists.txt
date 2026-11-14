find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Core Network GLOBAL)
find_package(OpenSSL 3.5.5 EXACT REQUIRED COMPONENTS Crypto)
# One original Bloom implementation, with separate chat/search input quotas.
add_library(biglybtc_original_bloom STATIC msgsync/bloom.cpp)
target_include_directories(biglybtc_original_bloom PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/msgsync")
target_link_libraries(biglybtc_original_bloom PUBLIC BiglyBTC::Core PRIVATE OpenSSL::Crypto)
target_compile_features(biglybtc_original_bloom PUBLIC cxx_std_23)
target_compile_options(biglybtc_original_bloom PRIVATE -Wall -Wextra -Wpedantic -Werror)

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

# Append after both existing service libraries are defined. No cyclic dependency:
# control remains a pure policy library; owned UDP now executes its NODE mode.
target_link_libraries(biglybtc_azureus_owned_udp PUBLIC biglybtc_azureus_control)

# Services: append once after the existing I2CP channel/datagram targets.
if(TARGET biglybtc_i2cp_initial_channel)
  add_library(biglybtc_i2p_streaming STATIC i2p_streaming/stream.cpp)
  target_include_directories(biglybtc_i2p_streaming PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/i2p_streaming")
  target_link_libraries(biglybtc_i2p_streaming PUBLIC biglybtc_i2p_client_identity PRIVATE ZLIB::ZLIB)
  target_compile_features(biglybtc_i2p_streaming PUBLIC cxx_std_23)
  target_compile_options(biglybtc_i2p_streaming PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_sources(biglybtc_i2cp_initial_channel PRIVATE i2cp_channel/streaming.inc)
  target_link_libraries(biglybtc_i2cp_initial_channel PUBLIC biglybtc_i2p_streaming)
endif()

target_link_libraries(biglybtc_azureus_control PRIVATE BiglyBTC::Core)

# Java's float Westwood filters round each operation; never contract to FMA.
if(TARGET biglybtc_i2p_streaming)
  target_compile_options(biglybtc_i2p_streaming PRIVATE -ffp-contract=off)
endif()

# Public endpoint predicate is declared in Core; UDP includes it directly.
target_link_libraries(biglybtc_azureus_owned_udp PUBLIC BiglyBTC::Core)

# Append once after the reviewed window services fragment.
if(TARGET biglybtc_i2p_streaming)
  target_sources(biglybtc_i2p_streaming PRIVATE i2p_streaming/manager.cpp)
  target_sources(biglybtc_i2cp_initial_channel PRIVATE i2cp_channel/managed_streams.inc)
endif()

# Append only after channel+manager targets. No default backend activation.
add_library(biglybtc_owned_sam_protocol STATIC i2p_provider/sam_protocol.cpp)
target_include_directories(biglybtc_owned_sam_protocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/i2p_provider")
target_link_libraries(biglybtc_owned_sam_protocol PUBLIC biglybtc_i2p_datagram PRIVATE OpenSSL::Crypto)
target_compile_features(biglybtc_owned_sam_protocol PUBLIC cxx_std_23)
target_compile_options(biglybtc_owned_sam_protocol PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(TARGET biglybtc_local_engine)
  add_library(biglybtc_owned_i2p_source STATIC i2p_provider/profile_source.cpp)
  target_include_directories(biglybtc_owned_i2p_source PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/i2p_provider")
  target_link_libraries(biglybtc_owned_i2p_source PUBLIC biglybtc_local_engine biglybtc_owned_sam_protocol
    biglybtc_i2p_client_identity biglybtc_i2cp_initial_channel LibtorrentRasterbar::torrent-rasterbar PRIVATE biglybtc_plugin_settings OpenSSL::Crypto)
  target_compile_features(biglybtc_owned_i2p_source PUBLIC cxx_std_23)
  target_compile_options(biglybtc_owned_i2p_source PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

option(BIGLYBTC_PRIVATE_I2P_PROVIDER_OWNER "Explicit privately qualified native I2P provider owner" OFF)
set(BIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL "" CACHE FILEPATH "Actual root-reviewed new private SDK/ELF approval")
set(BIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL_SHA256 "" CACHE STRING "Exact approval digest")
if(BIGLYBTC_PRIVATE_I2P_PROVIDER_OWNER)
  if(NOT TARGET biglybtc_owned_i2p_source OR NOT TARGET biglybtc_i2p_streaming)
    message(FATAL_ERROR "Owned I2P requires the separately qualified manager and engine")
  endif()
  get_target_property(_owned_i2p_dso LibtorrentRasterbar::torrent-rasterbar IMPORTED_LOCATION_RELEASE)
  if(NOT _owned_i2p_dso)
    get_target_property(_owned_i2p_dso LibtorrentRasterbar::torrent-rasterbar IMPORTED_LOCATION)
  endif()
  get_target_property(_owned_i2p_includes LibtorrentRasterbar::torrent-rasterbar INTERFACE_INCLUDE_DIRECTORIES)
  set(_owned_i2p_sdk "")
  foreach(_owned_i2p_dir IN LISTS _owned_i2p_includes)
    if(EXISTS "${_owned_i2p_dir}/libtorrent/biglybtc_private_i2p_owner.hpp")
      set(_owned_i2p_sdk "${_owned_i2p_dir}")
    endif()
  endforeach()
  if(NOT IS_ABSOLUTE "${BIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL}" OR NOT _owned_i2p_sdk OR NOT EXISTS "${_owned_i2p_dso}")
    message(FATAL_ERROR "Explicit approved private SDK/image required; stock/published provider remains unsupported")
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(_owned_i2p_source_capsule "63e9925c8f4f3e80168425d22b62ef87b23732eccff8fa1b7714f73f3f84e9c3")
  if(BIGLYBTC_PERSISTENT_MIXED_I2P_RUNTIME)
    # CLOSED: root replaces only after reviewed final source/artifact controls.
    set(_owned_i2p_source_capsule "f9afc758837d164662ae1ad4fdeec4b842b395ee1f09d30f5dd94d8e382ae5b6")
  endif()
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/i2p_provider/verify-owner-approval.py"
    "${BIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL}" "${BIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL_SHA256}"
    "${_owned_i2p_dso}" "${_owned_i2p_sdk}" "${_owned_i2p_source_capsule}"
    "${CMAKE_CURRENT_BINARY_DIR}/private_i2p_runtime_config.hpp" RESULT_VARIABLE _owned_i2p_verify)
  if(NOT _owned_i2p_verify EQUAL 0)
    message(FATAL_ERROR "Private owner approval/SDK/artifact closure failed")
  endif()
  add_library(biglybtc_i2p_provider_owner STATIC
    i2p_provider/owner.hpp i2p_provider/owner.cpp i2p_provider/profile_owner.hpp i2p_provider/profile_owner.cpp i2p_provider/bridge.hpp i2p_provider/bridge.cpp i2p_provider/owner_runtime_guard.cpp)
  set_target_properties(biglybtc_i2p_provider_owner PROPERTIES AUTOMOC ON)
  target_include_directories(biglybtc_i2p_provider_owner PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(biglybtc_i2p_provider_owner PUBLIC biglybtc_owned_i2p_source Qt6::Core Qt6::Network
    LibtorrentRasterbar::torrent-rasterbar PRIVATE OpenSSL::Crypto ${CMAKE_DL_LIBS})
  target_compile_features(biglybtc_i2p_provider_owner PUBLIC cxx_std_23)
  target_compile_options(biglybtc_i2p_provider_owner PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Append once; no default discovery activation or new dependency.
target_sources(biglybtc_i2cp_initial_channel PRIVATE i2cp_channel/dht.inc)
add_library(biglybtc_i2p_dht STATIC i2p_dht/dht.cpp i2p_dht/actor.cpp i2p_dht/actor.hpp)
set_target_properties(biglybtc_i2p_dht PROPERTIES AUTOMOC ON)
target_include_directories(biglybtc_i2p_dht PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/i2p_dht")
target_link_libraries(biglybtc_i2p_dht PUBLIC biglybtc_i2cp_initial_channel BiglyBTC::Core PRIVATE OpenSSL::Crypto)
target_compile_features(biglybtc_i2p_dht PUBLIC cxx_std_23)
target_compile_options(biglybtc_i2p_dht PRIVATE -Wall -Wextra -Wpedantic -Werror)

# Append after the existing owner and explicit discovery targets.
if(TARGET biglybtc_i2p_provider_owner AND TARGET biglybtc_i2p_dht)
  target_link_libraries(biglybtc_i2p_provider_owner PUBLIC biglybtc_i2p_dht)
endif()
# Root supplies BIGLYBTC_I2P_DISCOVERY_ABI_V1 only from the exact approved
# 42-export SDK feature probe. No old/default header or runtime gains effects.

# Original I2P inbound KRPC state and source-bound actor.
target_sources(biglybtc_i2p_dht PRIVATE i2p_dht/inbound_state.cpp i2p_dht/inbound_responder.cpp)
target_link_libraries(biglybtc_i2p_dht PRIVATE biglybtc_azureus_owned_udp)

# Original I2P contact persistence format only; no actor/profile activation.
target_sources(biglybtc_i2p_dht PRIVATE i2p_dht/saved_contacts.cpp)

# Original-profile I2P contacts are a separate filesystem worker library.
# Profile remains independent of the I2P wire/identity code; no owner activation.
add_library(biglybtc_i2p_contact_store STATIC i2p_provider/contact_store.cpp)
target_include_directories(biglybtc_i2p_contact_store PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/i2p_provider")
target_link_libraries(biglybtc_i2p_contact_store PUBLIC biglybtc_profile biglybtc_i2p_dht)
target_compile_features(biglybtc_i2p_contact_store PUBLIC cxx_std_23)
target_compile_options(biglybtc_i2p_contact_store PRIVATE -Wall -Wextra -Wpedantic -Werror)

# Private I2P routing-maintenance state; no new activation or dependencies.
target_sources(biglybtc_i2p_dht PRIVATE i2p_dht/routing_maintenance.hpp i2p_dht/routing_maintenance.inc)

# Original I2P contact export selection; no filesystem or transport authority.
target_sources(biglybtc_i2p_dht PRIVATE i2p_dht/saved_routing.cpp)

if(TARGET biglybtc_i2p_contact_store AND TARGET biglybtc_owned_i2p_source)
# Existing caller thread only; no additional filesystem/network worker.
add_library(biglybtc_i2p_contact_owner STATIC i2p_provider/contact_owner.cpp)
target_include_directories(biglybtc_i2p_contact_owner PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/i2p_provider")
target_link_libraries(biglybtc_i2p_contact_owner PUBLIC biglybtc_i2p_contact_store biglybtc_owned_i2p_source)
target_compile_features(biglybtc_i2p_contact_owner PUBLIC cxx_std_23)
target_compile_options(biglybtc_i2p_contact_owner PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(TARGET biglybtc_i2p_provider_owner)
  target_link_libraries(biglybtc_i2p_provider_owner PUBLIC biglybtc_i2p_contact_owner)
endif()
endif()
# SOURCE candidate only: review/qualify the frozen helper before this adapter.
# Apply only with the exact frozen helper source/header dependency. No fallback
# to a rekeyed allocator, counter wire ID, or retired-ID eviction is allowed.
if(NOT TARGET biglybtc_protocol_unique32)
  find_package(Threads REQUIRED)
  add_library(biglybtc_protocol_unique32 STATIC protocol_ids/unique32.cpp)
  target_include_directories(biglybtc_protocol_unique32 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/protocol_ids")
  target_link_libraries(biglybtc_protocol_unique32 PRIVATE OpenSSL::Crypto Threads::Threads)
  target_compile_features(biglybtc_protocol_unique32 PUBLIC cxx_std_23)
  target_compile_options(biglybtc_protocol_unique32 PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
target_link_libraries(biglybtc_i2cp_initial_channel PRIVATE biglybtc_protocol_unique32)

# Source-only additive target; no default startup/Host invocation or SDK change.
target_sources(biglybtc_i2p_client_identity PRIVATE i2p_client/generate_identity.cpp)
add_library(biglybtc_i2p_profile_bootstrap STATIC i2p_provider/profile_bootstrap.cpp)
target_include_directories(biglybtc_i2p_profile_bootstrap PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/i2p_provider")
target_link_libraries(biglybtc_i2p_profile_bootstrap PUBLIC biglybtc_profile biglybtc_i2p_client_identity biglybtc_i2p_dht PRIVATE biglybtc_plugin_settings OpenSSL::Crypto)
target_compile_features(biglybtc_i2p_profile_bootstrap PUBLIC cxx_std_23)
target_compile_options(biglybtc_i2p_profile_bootstrap PRIVATE -Wall -Wextra -Wpedantic -Werror)

# Owning untrusted RSS/Atom values only; no loader/fetch/engine activation.
add_library(biglybtc_rss_document STATIC search/rss_document.cpp)
target_include_directories(biglybtc_rss_document PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/search")
target_link_libraries(biglybtc_rss_document PUBLIC Qt6::Core)
target_compile_features(biglybtc_rss_document PUBLIC cxx_std_23)
target_compile_options(biglybtc_rss_document PRIVATE -Wall -Wextra -Wpedantic -Werror)

# Read-only saved RSS description only; no worker or network activation.
find_package(Threads REQUIRED)
add_library(biglybtc_saved_rss_source STATIC search/saved_rss_source.cpp)
target_include_directories(biglybtc_saved_rss_source PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/search" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../engine")
target_link_libraries(biglybtc_saved_rss_source PUBLIC biglybtc_profile biglybtc_rss_document Qt6::Core Qt6::Network PRIVATE Threads::Threads)
target_compile_features(biglybtc_saved_rss_source PUBLIC cxx_std_23)
target_compile_options(biglybtc_saved_rss_source PRIVATE -Wall -Wextra -Wpedantic -Werror)

# Concrete one-shot provider, callable by the existing serialized profile worker.
# No Host/UI/default invocation is registered here. The fixture macro is absent.
find_package(Threads REQUIRED)
add_library(biglybtc_rss_http_fetch STATIC search/rss_http_fetch.cpp)
target_include_directories(biglybtc_rss_http_fetch PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/search")
target_link_libraries(biglybtc_rss_http_fetch PUBLIC biglybtc_saved_rss_source biglybtc_rss_document Qt6::Core Qt6::Network PRIVATE biglybtc_http_response biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar Threads::Threads)
target_compile_features(biglybtc_rss_http_fetch PUBLIC cxx_std_23)
target_compile_options(biglybtc_rss_http_fetch PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(biglybtc_rss_http_fetch PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Concrete original saved-subscription source/store and bounded values. No
# network/Host effect is created by this target or by source descriptions.
if(TARGET biglybtc_saved_rss_source)
  target_sources(biglybtc_saved_rss_source PRIVATE
    search/rss_get_description.hpp search/rss_source_internal.inc)
  add_library(biglybtc_saved_subscription_source STATIC
    search/saved_subscription_source.hpp search/saved_subscription_source.cpp
    search/subscription_values.hpp search/subscription_values.cpp
    search/subscription_store_internal.hpp search/subscription_store_internal.inc)
  target_include_directories(biglybtc_saved_subscription_source PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/search" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../engine")
  target_link_libraries(biglybtc_saved_subscription_source PUBLIC biglybtc_saved_rss_source PRIVATE Threads::Threads)
  target_compile_features(biglybtc_saved_subscription_source PUBLIC cxx_std_23)
  target_compile_options(biglybtc_saved_subscription_source PRIVATE -Wall -Wextra -Wpedantic -Werror)
  set_target_properties(biglybtc_saved_subscription_source PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# The current production provider exists in the canonical services prefix.
# Compose this AFTER the closed-variant provider dependency append.
if(TARGET biglybtc_rss_http_fetch AND TARGET biglybtc_saved_subscription_source)
  add_library(biglybtc_saved_subscription_service STATIC search/saved_subscription_service.hpp search/saved_subscription_service.cpp)
  target_include_directories(biglybtc_saved_subscription_service PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/search")
  target_link_libraries(biglybtc_saved_subscription_service PUBLIC biglybtc_saved_subscription_source biglybtc_rss_http_fetch)
  target_compile_features(biglybtc_saved_subscription_service PUBLIC cxx_std_23)
  target_compile_options(biglybtc_saved_subscription_service PRIVATE -Wall -Wextra -Wpedantic -Werror)
  set_target_properties(biglybtc_saved_subscription_service PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(TARGET biglybtc_rss_http_fetch)
  if(NOT TARGET biglybtc_saved_subscription_source)
    message(FATAL_ERROR "Concrete subscription RSS transport requires the reviewed saved subscription reader")
  endif()
  target_link_libraries(biglybtc_rss_http_fetch PUBLIC biglybtc_saved_subscription_source)
endif()

# Integrate values in the existing reader archive; do not also append the
# provisional standalone numeric-library fragment (it would form a cycle).
if(TARGET biglybtc_saved_subscription_source)
  target_sources(biglybtc_saved_subscription_source PRIVATE
    search/subscription_numeric_filter.hpp search/subscription_numeric_filter.cpp search/subscription_numeric_json.inc)
endif()


if(TARGET biglybtc_saved_subscription_source)
  target_sources(biglybtc_saved_subscription_source PRIVATE search/saved_subscription_result_source.hpp search/saved_subscription_result_source.cpp)
endif()


if(TARGET biglybtc_saved_subscription_source)
  target_sources(biglybtc_saved_subscription_source PRIVATE search/subscription_association_values.hpp search/subscription_association_values.cpp)
endif()


if(TARGET biglybtc_rss_http_fetch)
  target_sources(biglybtc_rss_http_fetch PRIVATE search/subscription_metainfo_fetch.hpp)
endif()


# Replaces the frozen value-only services fragment during NEXT composition.
# Local-engine/headless builds may have already created this Core-only target.
if(NOT TARGET biglybtc_subscription_download_values)
  add_library(biglybtc_subscription_download_values STATIC search/subscription_download_values.cpp)
  target_include_directories(biglybtc_subscription_download_values PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/search")
  target_link_libraries(biglybtc_subscription_download_values PUBLIC BiglyBTC::Core)
  target_compile_features(biglybtc_subscription_download_values PUBLIC cxx_std_23)
  target_compile_options(biglybtc_subscription_download_values PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(TARGET biglybtc_saved_subscription_source)
  target_link_libraries(biglybtc_saved_subscription_source PUBLIC biglybtc_subscription_download_values)
endif()

if(TARGET biglybtc_saved_subscription_source)
  target_sources(biglybtc_saved_subscription_source PRIVATE search/subscription_literal_filter.cpp)
endif()


if(TARGET biglybtc_saved_subscription_source)
  target_link_libraries(biglybtc_saved_subscription_source PUBLIC biglybtc_subscription_automatic_values)
endif()


if(TARGET biglybtc_saved_rss_source AND TARGET biglybtc_rss_http_fetch)
  add_library(biglybtc_metasearch_values STATIC search/metasearch_values.cpp)
  target_include_directories(biglybtc_metasearch_values PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/search")
  target_link_libraries(biglybtc_metasearch_values PUBLIC BiglyBTC::Core)
  add_library(biglybtc_metasearch_document STATIC search/metasearch_document.cpp)
  target_include_directories(biglybtc_metasearch_document PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/search")
  target_link_libraries(biglybtc_metasearch_document PUBLIC biglybtc_metasearch_values biglybtc_rss_document Qt6::Core)
  add_library(biglybtc_saved_metasearch_source STATIC search/saved_metasearch_source.cpp search/saved_metasearch_result.cpp search/metasearch_source_internal.inc)
  target_include_directories(biglybtc_saved_metasearch_source PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/search" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../engine")
  target_link_libraries(biglybtc_saved_metasearch_source PUBLIC biglybtc_metasearch_values biglybtc_saved_rss_source PRIVATE Threads::Threads)
  add_library(biglybtc_saved_metasearch_service STATIC search/saved_metasearch_service.cpp)
  target_include_directories(biglybtc_saved_metasearch_service PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/search")
  target_link_libraries(biglybtc_saved_metasearch_service PUBLIC biglybtc_saved_metasearch_source biglybtc_metasearch_document biglybtc_rss_http_fetch)
  foreach(_metasearch_target biglybtc_metasearch_values biglybtc_metasearch_document biglybtc_saved_metasearch_source biglybtc_saved_metasearch_service)
    target_compile_features(${_metasearch_target} PUBLIC cxx_std_23)
    target_compile_options(${_metasearch_target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    set_target_properties(${_metasearch_target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  endforeach()
endif()

# Existing closed owned HTTP provider gains only the concrete metasearch reader.
if(TARGET biglybtc_rss_http_fetch)
  target_link_libraries(biglybtc_rss_http_fetch PUBLIC biglybtc_saved_metasearch_source)
endif()


if(TARGET biglybtc_saved_metasearch_source AND TARGET biglybtc_local_engine)
  add_library(biglybtc_metasearch_magnet_admission STATIC search/metasearch_magnet_admission.cpp)
  target_include_directories(biglybtc_metasearch_magnet_admission PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/search")
  target_link_libraries(biglybtc_metasearch_magnet_admission PUBLIC biglybtc_saved_metasearch_source biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar)
  target_compile_features(biglybtc_metasearch_magnet_admission PUBLIC cxx_std_23)
  target_compile_options(biglybtc_metasearch_magnet_admission PRIVATE -Wall -Wextra -Wpedantic -Werror)
  set_target_properties(biglybtc_metasearch_magnet_admission PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# Compose before the embedded-tracker fragment: no Source -> tracker cycle.
if(TARGET biglybtc_profile AND TARGET biglybtc_local_engine)
  add_library(biglybtc_sharing_values STATIC sharing/sharing_values.cpp)
  target_include_directories(biglybtc_sharing_values PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/sharing" "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(biglybtc_sharing_values PUBLIC BiglyBTC::Core PRIVATE OpenSSL::Crypto)
  add_library(biglybtc_saved_share_source STATIC sharing/saved_share.cpp)
  target_include_directories(biglybtc_saved_share_source PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/sharing" "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(biglybtc_saved_share_source PUBLIC biglybtc_sharing_values biglybtc_profile LibtorrentRasterbar::torrent-rasterbar PRIVATE biglybtc_local_engine Qt6::Core Qt6::Network OpenSSL::Crypto Threads::Threads)
  foreach(_sharing_target biglybtc_sharing_values biglybtc_saved_share_source)
    target_compile_features(${_sharing_target} PUBLIC cxx_std_23)
    target_compile_options(${_sharing_target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    set_target_properties(${_sharing_target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  endforeach()
endif()

if(TARGET biglybtc_saved_share_source AND TARGET biglybtc_local_engine)
  add_library(biglybtc_embedded_tracker STATIC tracker/embedded_tracker.cpp tracker/tracker_protocol.cpp)
  target_include_directories(biglybtc_embedded_tracker PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/tracker")
  target_link_libraries(biglybtc_embedded_tracker PUBLIC biglybtc_saved_share_source biglybtc_profile PRIVATE biglybtc_saved_service_view biglybtc_local_engine Qt6::Core Qt6::Network LibtorrentRasterbar::torrent-rasterbar Threads::Threads)
  target_compile_features(biglybtc_embedded_tracker PUBLIC cxx_std_23)
  target_compile_options(biglybtc_embedded_tracker PRIVATE -Wall -Wextra -Wpedantic -Werror)
  set_target_properties(biglybtc_embedded_tracker PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# Compose after the embedded-tracker fragment; explicit inactive service only.
if(TARGET biglybtc_saved_share_source AND TARGET biglybtc_embedded_tracker)
  add_library(biglybtc_saved_sharing STATIC sharing/saved_sharing_service.cpp)
  target_include_directories(biglybtc_saved_sharing PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/sharing" "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(biglybtc_saved_sharing PUBLIC biglybtc_saved_share_source biglybtc_local_engine PRIVATE biglybtc_embedded_tracker Threads::Threads)
  target_compile_features(biglybtc_saved_sharing PUBLIC cxx_std_23)
  target_compile_options(biglybtc_saved_sharing PRIVATE -Wall -Wextra -Wpedantic -Werror)
  set_target_properties(biglybtc_saved_sharing PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# Append after the existing owned UDP target; no new provider/Qt dependency.
target_sources(biglybtc_azureus_owned_udp PRIVATE
  azureus_transfer/data.cpp azureus_transfer/call.cpp azureus_udp/owned_transfer.inc)

# Explicit pure message core only. No room/Host/network activation.
option(BIGLYBTC_MSGSYNC_MESSAGE_CORE "Build original-compatible bounded msgsync core" OFF)
set(BIGLYBTC_MSGSYNC_SIGNER_SOURCE_DIR "" CACHE PATH "Explicit reviewed shared original STS/signer source")
if(BIGLYBTC_MSGSYNC_MESSAGE_CORE)
  if(NOT IS_ABSOLUTE "${BIGLYBTC_MSGSYNC_SIGNER_SOURCE_DIR}")
    message(FATAL_ERROR "Explicit reviewed msgsync signer sources required")
  endif()
  find_package(OpenSSL 3.5.5 REQUIRED COMPONENTS Crypto)
  if(NOT TARGET ZLIB::ZLIB)
    find_package(ZLIB REQUIRED)
  endif()
  if(TARGET biglybtc_msgsync_sts)
    get_target_property(_msgsync_target_dir biglybtc_msgsync_sts SOURCE_DIR)
    get_target_property(_msgsync_target_sources biglybtc_msgsync_sts SOURCES)
    get_filename_component(_msgsync_expected_dir "${BIGLYBTC_MSGSYNC_SIGNER_SOURCE_DIR}" REALPATH)
    get_filename_component(_msgsync_actual_dir "${_msgsync_target_dir}" REALPATH)
    if(NOT _msgsync_actual_dir STREQUAL _msgsync_expected_dir OR NOT _msgsync_target_sources STREQUAL "legacy_sts.cpp")
      message(FATAL_ERROR "Existing STS target must use the exact reviewed shared source")
    endif()
  else()
    add_library(biglybtc_msgsync_sts STATIC "${BIGLYBTC_MSGSYNC_SIGNER_SOURCE_DIR}/legacy_sts.cpp")
    target_include_directories(biglybtc_msgsync_sts PUBLIC "${BIGLYBTC_MSGSYNC_SIGNER_SOURCE_DIR}")
    target_link_libraries(biglybtc_msgsync_sts PRIVATE OpenSSL::Crypto)
    target_compile_features(biglybtc_msgsync_sts PUBLIC cxx_std_23)
    target_compile_options(biglybtc_msgsync_sts PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  if(TARGET biglybtc_msgsync_envelope)
    get_target_property(_msgsync_envelope_dir biglybtc_msgsync_envelope SOURCE_DIR)
    get_target_property(_msgsync_envelope_sources biglybtc_msgsync_envelope SOURCES)
    get_filename_component(_msgsync_expected_dir "${BIGLYBTC_MSGSYNC_SIGNER_SOURCE_DIR}" REALPATH)
    get_filename_component(_msgsync_actual_dir "${_msgsync_envelope_dir}" REALPATH)
    if(NOT _msgsync_actual_dir STREQUAL _msgsync_expected_dir OR NOT _msgsync_envelope_sources STREQUAL "envelope.cpp")
      message(FATAL_ERROR "Existing envelope target must use the exact qualified source")
    endif()
  else()
    add_library(biglybtc_msgsync_envelope STATIC "${BIGLYBTC_MSGSYNC_SIGNER_SOURCE_DIR}/envelope.cpp")
    target_include_directories(biglybtc_msgsync_envelope PUBLIC "${BIGLYBTC_MSGSYNC_SIGNER_SOURCE_DIR}")
    target_link_libraries(biglybtc_msgsync_envelope PRIVATE OpenSSL::Crypto)
    target_compile_features(biglybtc_msgsync_envelope PUBLIC cxx_std_23)
    target_compile_options(biglybtc_msgsync_envelope PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_library(biglybtc_msgsync_message_core STATIC msgsync/message.cpp msgsync/ledger.cpp msgsync/room.cpp msgsync/participant_name.cpp msgsync/ordering.cpp msgsync/discovery.cpp)
  target_include_directories(biglybtc_msgsync_message_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/msgsync")
  target_sources(biglybtc_msgsync_message_core PRIVATE msgsync/private_channel.cpp)
  target_link_libraries(biglybtc_msgsync_message_core PUBLIC BiglyBTC::Core biglybtc_original_bloom biglybtc_msgsync_sts biglybtc_msgsync_envelope PRIVATE OpenSSL::Crypto ZLIB::ZLIB)
  target_compile_features(biglybtc_msgsync_message_core PUBLIC cxx_std_23)
  target_compile_options(biglybtc_msgsync_message_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Shared SDK channel stays optional until explicit candidate selection.
add_subdirectory(shared_udp)

if(TARGET biglybtc_msgsync_message_core)
  add_library(biglybtc_msgsync_session STATIC msgsync/session.cpp)
  target_include_directories(biglybtc_msgsync_session PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/msgsync")
  target_link_libraries(biglybtc_msgsync_session PUBLIC biglybtc_msgsync_message_core biglybtc_azureus_owned_udp)
  target_compile_features(biglybtc_msgsync_session PUBLIC cxx_std_23)
  target_compile_options(biglybtc_msgsync_session PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Original Public related-content codec/cache policy, independent of Widgets.
if(TARGET Qt6::Core)
add_library(biglybtc_related_content STATIC related_content/related_content.cpp related_content/search.cpp related_content/search_bloom.cpp related_content/search_wire.cpp)
target_include_directories(biglybtc_related_content PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/related_content")
target_link_libraries(biglybtc_related_content PUBLIC biglybtc_core biglybtc_original_bloom PRIVATE Qt6::Core Qt6::Network ZLIB::ZLIB)
target_compile_options(biglybtc_related_content PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(TARGET Qt6::Core)
 add_library(biglybtc_network_identity STATIC network_identity/network_identity.cpp)
 target_include_directories(biglybtc_network_identity PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/network_identity")
 target_link_libraries(biglybtc_network_identity PUBLIC BiglyBTC::Core Qt6::Core Qt6::Network)
 target_compile_options(biglybtc_network_identity PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Exact previously qualified azrating queue; live executor integration is separate.
add_library(biglybtc_rating_publication_policy STATIC rating_publication/publication_policy.cpp rating_publication/read_schedule.cpp rating_publication/chat_notice.cpp)
target_include_directories(biglybtc_rating_publication_policy PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/rating_publication")
target_compile_features(biglybtc_rating_publication_policy PUBLIC cxx_std_23)
target_compile_options(biglybtc_rating_publication_policy PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(biglybtc_azureus_ddb_values STATIC azureus_ddb/value_envelope.cpp)
target_include_directories(biglybtc_azureus_ddb_values PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/azureus_ddb")
target_compile_features(biglybtc_azureus_ddb_values PUBLIC cxx_std_23)
target_compile_options(biglybtc_azureus_ddb_values PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(TARGET biglybtc_msgsync_message_core)
 target_sources(biglybtc_msgsync_message_core PRIVATE msgsync/peek.hpp msgsync/peek.cpp)
endif()

if(TARGET biglybtc_msgsync_session)
 target_sources(biglybtc_msgsync_session PRIVATE msgsync/peek_session.cpp msgsync/peek_session.hpp)
endif()

# Original NAT contact/message format, without sockets or a process-global owner.
add_library(biglybtc_azureus_nat_wire STATIC azureus_nat/wire.cpp)
target_include_directories(biglybtc_azureus_nat_wire PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/azureus_nat")
target_link_libraries(biglybtc_azureus_nat_wire PUBLIC BiglyBTC::Core biglybtc_azureus_owned_udp)
target_compile_features(biglybtc_azureus_nat_wire PUBLIC cxx_std_23)
target_compile_options(biglybtc_azureus_nat_wire PRIVATE -Wall -Wextra -Wpedantic -Werror)
