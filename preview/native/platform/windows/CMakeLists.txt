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
  find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
  add_library(biglybtc_windows_torrent_state STATIC
    ../../adapters/application_settings.cpp ../../adapters/encoded_document.cpp
    ../../adapters/existing_payload_scope.cpp ../../adapters/file_operation_digest.cpp ../../adapters/file_operation_io.cpp
    ../../adapters/file_policy_commands.cpp ../../adapters/original_storage_conversion.cpp
    ../../adapters/file_operations.cpp ../../adapters/linear_to_compact.cpp ../../adapters/original_selection_group.cpp
    ../../adapters/durable_removal_receipt.cpp ../../adapters/profile_transaction_pending.cpp
    ../../adapters/download_history_store.cpp ../../adapters/profile_file_snapshot.cpp
    ../../adapters/category_store.cpp
    ../../adapters/history_view.cpp ../../adapters/history_week_view.cpp
    ../../adapters/history_month_view.cpp ../../adapters/history_range_view.cpp
    ../../adapters/minute_history_writer.cpp
    ../../adapters/azureus_contacts.cpp ../../adapters/azureus_value_versions.cpp
    ../../adapters/azureus_remote_diversification.cpp ../../adapters/azureus_public_policy.cpp
    ../../adapters/chat_identity_stamp.cpp
    ../../adapters/durable_completion_receipt.cpp ../../adapters/durable_intake_receipt.cpp
    ../../adapters/history_reconstruction_receipt.cpp
    ../../adapters/manual_tag_store.cpp ../../adapters/prepared_tag_rate_source.cpp ../../adapters/state_directory_listing.cpp
    ../../adapters/manual_tag_identity.cpp ../../adapters/original_identity_digest.cpp
    ../../adapters/library_view.cpp ../../adapters/original_control.cpp ../../adapters/profile_metadata.cpp
    ../../adapters/original_file_io.cpp ../../adapters/original_payload_io.cpp
    ../../adapters/original_storage_defaults.cpp ../../adapters/original_torrent_identity.cpp
    ../../adapters/profile_config_read.cpp ../../adapters/profile_config_windows.cpp
    ../../adapters/state_file_io_windows.cpp ../../adapters/torrent_state.cpp
    ../../adapters/torrent_intake.cpp ../../adapters/queue_positions.cpp
    ../../services/search/subscription_automatic_values.cpp ../../services/search/subscription_download_values.cpp)
  target_link_libraries(biglybtc_windows_torrent_state PUBLIC biglybtc_profile_lease BiglyBTC::Core
    PRIVATE ZLIB::ZLIB OpenSSL::Crypto Threads::Threads LibtorrentRasterbar::torrent-rasterbar biglybtc_network_interfaces advapi32)
  target_include_directories(biglybtc_windows_torrent_state PRIVATE ../../services/search)
  include(../../adapters/mainline_storage.cmake)
  biglybtc_add_mainline_storage(biglybtc_windows_torrent_state)
  add_library(biglybtc_windows_network_policy STATIC
    ../../engine/network_binding.cpp ../../engine/network_policy.cpp
    ../../engine/network_proxy_capability.cpp ../../engine/live_settings.cpp ../../engine/mainline_bind_plan.cpp)
  target_link_libraries(biglybtc_windows_network_policy PUBLIC biglybtc_windows_torrent_state
    PRIVATE biglybtc_network_interfaces biglybtc_plugin_settings LibtorrentRasterbar::torrent-rasterbar)
  include(../../tests/PortableNetworkTests.cmake)
  biglybtc_add_portable_network_tests(biglybtc_windows_network_policy)
  add_executable(biglybtc-network-policy-tests ../../tests/network_policy_tests.cpp)
  target_link_libraries(biglybtc-network-policy-tests PRIVATE biglybtc_windows_network_policy
    BiglyBTC::Core LibtorrentRasterbar::torrent-rasterbar)
  add_test(NAME network-policy COMMAND biglybtc-network-policy-tests)
  set_tests_properties(network-policy PROPERTIES TIMEOUT 30 LABELS "routing;no-network")
  if(MSVC)
    target_compile_options(biglybtc_windows_network_policy PRIVATE /utf-8 /Zc:__cplusplus)
    target_compile_options(biglybtc-network-policy-tests PRIVATE /utf-8 /Zc:__cplusplus)
  else()
    target_compile_options(biglybtc_windows_network_policy PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(biglybtc-network-policy-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  include(../../adapters/mainline_identity.cmake)
  biglybtc_add_mainline_identity(biglybtc_windows_torrent_state)
  include(../../tests/PortableProfileTests.cmake)
  biglybtc_profile_workflow_tests(biglybtc_windows_torrent_state TRUE)
  include(../../app/ProfilePreflight.cmake)
  biglybtc_add_profile_preflight(biglybtc_windows_torrent_state TRUE)
  add_executable(biglybtc-torrent-state-tests ../torrent_state_tests.cpp)
  target_link_libraries(biglybtc-torrent-state-tests PRIVATE biglybtc_windows_torrent_state ZLIB::ZLIB)
  add_executable(biglybtc-original-control-tests ../../tests/original_control_tests.cpp)
  target_link_libraries(biglybtc-original-control-tests PRIVATE biglybtc_windows_torrent_state Threads::Threads)
  add_executable(biglybtc-existing-payload-scope-tests ../existing_payload_scope_tests.cpp)
  target_link_libraries(biglybtc-existing-payload-scope-tests PRIVATE biglybtc_windows_torrent_state)
  add_executable(biglybtc-linear-to-compact-tests ../../tests/linear_to_compact_tests.cpp)
  target_link_libraries(biglybtc-linear-to-compact-tests PRIVATE biglybtc_windows_torrent_state OpenSSL::Crypto)
  add_executable(biglybtc-manual-tag-store-tests ../../tests/manual_tag_store_tests.cpp)
  target_link_libraries(biglybtc-manual-tag-store-tests PRIVATE biglybtc_windows_torrent_state)
  add_executable(biglybtc-download-history-store-tests ../../tests/download_history_store_tests.cpp)
  add_executable(biglybtc-category-store-tests ../../tests/category_store_tests.cpp)
  target_link_libraries(biglybtc-category-store-tests PRIVATE biglybtc_windows_torrent_state OpenSSL::Crypto ZLIB::ZLIB)
  target_link_libraries(biglybtc-download-history-store-tests PRIVATE biglybtc_windows_torrent_state)
  if(MINGW)
    target_link_options(biglybtc-download-history-store-tests PRIVATE -municode)
  endif()
  add_executable(biglybtc-manual-tag-identity-tests ../../tests/manual_tag_identity_tests.cpp)
  target_link_libraries(biglybtc-manual-tag-identity-tests PRIVATE biglybtc_windows_torrent_state)
  set(tag_fixtures "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures")
  target_compile_definitions(biglybtc-manual-tag-identity-tests PRIVATE
    TAG_CANONICAL_HASH_FIXTURE="${tag_fixtures}/canonical-tag-hashes-original.bencode"
    TAG_DOWNLOAD_IDENTITY_FIXTURE="${tag_fixtures}/download-identity-original.bencode")
  if(MSVC)
    target_compile_options(biglybtc_windows_torrent_state PRIVATE /utf-8 /Zc:__cplusplus)
    target_compile_options(biglybtc-torrent-state-tests PRIVATE /utf-8 /Zc:__cplusplus)
    target_compile_options(biglybtc-original-control-tests PRIVATE /utf-8 /Zc:__cplusplus)
    target_compile_options(biglybtc-existing-payload-scope-tests PRIVATE /utf-8 /Zc:__cplusplus)
    target_compile_options(biglybtc-linear-to-compact-tests PRIVATE /utf-8 /Zc:__cplusplus)
    target_compile_options(biglybtc-manual-tag-store-tests PRIVATE /utf-8 /Zc:__cplusplus)
    target_compile_options(biglybtc-download-history-store-tests PRIVATE /utf-8 /Zc:__cplusplus)
    target_compile_options(biglybtc-category-store-tests PRIVATE /utf-8 /Zc:__cplusplus)
    target_compile_options(biglybtc-manual-tag-identity-tests PRIVATE /utf-8 /Zc:__cplusplus)
  endif()
  add_test(NAME torrent-state-transactions COMMAND biglybtc-torrent-state-tests)
  set_tests_properties(torrent-state-transactions PROPERTIES TIMEOUT 25)
  add_test(NAME original-storage-controls COMMAND biglybtc-original-control-tests)
  set_tests_properties(original-storage-controls PROPERTIES TIMEOUT 120)
  add_test(NAME existing-payload-scope COMMAND biglybtc-existing-payload-scope-tests)
  set_tests_properties(existing-payload-scope PROPERTIES TIMEOUT 30)
  add_test(NAME original-linear-to-compact COMMAND biglybtc-linear-to-compact-tests)
  set_tests_properties(original-linear-to-compact PROPERTIES TIMEOUT 120)
  add_test(NAME manual-tag-store COMMAND biglybtc-manual-tag-store-tests)
  set_tests_properties(manual-tag-store PROPERTIES TIMEOUT 30)
  add_test(NAME original-download-history-store COMMAND biglybtc-download-history-store-tests)
  set_tests_properties(original-download-history-store PROPERTIES TIMEOUT 120)
  add_test(NAME original-category-worker-store COMMAND biglybtc-category-store-tests)
  set_tests_properties(original-category-worker-store PROPERTIES TIMEOUT 180)
  add_test(NAME actual-canonical-tag-subjects COMMAND biglybtc-manual-tag-identity-tests)
  set_tests_properties(actual-canonical-tag-subjects PROPERTIES TIMEOUT 60)
endif()
