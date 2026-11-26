cmake_minimum_required(VERSION 3.25)
set(biglybtc_core_install_default OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(biglybtc_core_install_default ON)
  project(BiglyBTCCore VERSION 0.1.0 LANGUAGES CXX)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Toolchain.cmake")
    include(cmake/Toolchain.cmake)
  else()
    include(../cmake/Toolchain.cmake)
  endif()
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Threads REQUIRED)
add_library(biglybtc_core STATIC src/bencode.cpp src/executor.cpp src/library.cpp src/state_cache.cpp src/torrent_layout.cpp src/file_policy.cpp src/original_file_links.cpp src/saved_services.cpp src/saved_dnd.cpp src/column_layout.cpp src/classic_preferences.cpp src/saved_statistics.cpp src/overall_statistics.cpp src/overall_document.cpp src/engine_statistics.cpp src/saved_history.cpp src/saved_history_week.cpp src/saved_history_month.cpp src/saved_history_range.cpp)
add_library(BiglyBTC::Core ALIAS biglybtc_core)
target_include_directories(biglybtc_core PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_compile_features(biglybtc_core PUBLIC cxx_std_23)
set_target_properties(biglybtc_core PROPERTIES CXX_EXTENSIONS OFF EXPORT_NAME Core POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(biglybtc_core PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
else()
  target_compile_options(biglybtc_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
set(BIGLYBTC_CORE_NEEDS_BOOST OFF)
if(APPLE)
  find_package(Boost 1.89 CONFIG REQUIRED)
  target_link_libraries(biglybtc_core PUBLIC Boost::headers)
  set(BIGLYBTC_CORE_NEEDS_BOOST ON)
endif()
target_link_libraries(biglybtc_core PUBLIC Threads::Threads)
option(BIGLYBTC_INSTALL_CORE "Install experimental static core and development headers" "${biglybtc_core_install_default}")
if(BIGLYBTC_INSTALL_CORE)
install(TARGETS biglybtc_core EXPORT BiglyBTCCoreTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
configure_package_config_file(BiglyBTCCoreConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/BiglyBTCCoreConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BiglyBTCCore")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/BiglyBTCCoreConfigVersion.cmake"
  VERSION 0.1.0 COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/BiglyBTCCoreConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/BiglyBTCCoreConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BiglyBTCCore")
install(EXPORT BiglyBTCCoreTargets NAMESPACE BiglyBTC:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BiglyBTCCore")
install(FILES COPYING NOTICE.md DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/BiglyBTCCore")
endif()

target_sources(biglybtc_core PRIVATE src/source_rate_average.cpp src/minute_history.cpp)

target_sources(biglybtc_core PRIVATE src/saved_download_tags.cpp src/manual_tag_statistics.cpp)

target_sources(biglybtc_core PRIVATE src/peer_payload_ledger.cpp src/country_statistics.cpp)

target_sources(biglybtc_core PRIVATE src/tracker_statistics.cpp)

target_sources(biglybtc_core PRIVATE src/disk_statistics.cpp)

target_sources(biglybtc_core PRIVATE src/saved_speed_manager.cpp)

target_sources(biglybtc_core PRIVATE src/saved_speed_map.cpp src/passive_speed_mapper.cpp)
target_sources(biglybtc_core PRIVATE src/auto_speed_v1.cpp)

target_sources(biglybtc_core PRIVATE src/queue_policy.cpp src/queue_observations.cpp)

target_sources(biglybtc_core PRIVATE src/queue_slots.cpp)

target_sources(biglybtc_core PRIVATE src/queue_file_priority.cpp)

# Root merges into existing core/CMakeLists.txt.
target_sources(biglybtc_core PRIVATE src/queue_download_probe.cpp)

target_sources(biglybtc_core PRIVATE src/queue_rate_providers.cpp)

target_sources(biglybtc_core PRIVATE src/queue_target_statistics.cpp)

# Original saved completion-group queue ordering.
target_sources(biglybtc_core PRIVATE src/queue_positions.cpp)

# Value-only manual-download tag document edits; no profile or live activation.
target_sources(biglybtc_core PRIVATE src/saved_download_tag_edits.cpp)
# Pure original category definitions and selected-state attributes only.
target_sources(biglybtc_core PRIVATE src/saved_categories.cpp)

# Value-only source-defined automatic constraint compiler; no Host activation.
target_sources(biglybtc_core PRIVATE src/tag_constraints.cpp src/tag_constraint_catalog.inc)

# Owning original download-list history; distinct from Statistics saved_history.
target_sources(biglybtc_core PRIVATE src/saved_download_history.cpp)

# Inert original metasearch.config inventory; no engines or network activated.
target_sources(biglybtc_core PRIVATE src/saved_search_engines.cpp)

# Value-only original manual-tag/group ordering from measured shared JDK properties.
target_sources(biglybtc_core PRIVATE src/original_tag_order.cpp)

target_sources(biglybtc_core PRIVATE src/swarm_file_merge.cpp)

target_sources(biglybtc_core PRIVATE src/tag_runtime_registry.cpp)

target_sources(biglybtc_core PRIVATE src/download_state_tags.cpp)

target_sources(biglybtc_core PRIVATE src/download_state_names.cpp)

target_sources(biglybtc_core PRIVATE src/category_tags.cpp)

target_sources(biglybtc_core PRIVATE src/tag_dependency_apply.cpp)

target_sources(biglybtc_core PRIVATE src/stock_tag_names.cpp)

target_sources(biglybtc_core PRIVATE src/tracker_tag_rules.cpp)

target_sources(biglybtc_core PRIVATE src/untagged_tag_policy.cpp)

target_sources(biglybtc_core PRIVATE src/peer_set_cidrs.cpp)

target_sources(biglybtc_core PRIVATE src/saved_peer_sets.cpp)

target_sources(biglybtc_core PRIVATE src/peer_set_predicate.cpp)

target_sources(biglybtc_core PRIVATE src/tag_named_metrics.cpp)

target_sources(biglybtc_core PRIVATE src/internal_tag_owner.cpp)

target_sources(biglybtc_core PRIVATE src/tag_assignment_actions.cpp)

target_sources(biglybtc_core PRIVATE src/tag_rate_limits.cpp)

target_sources(biglybtc_core PRIVATE src/compact_file_requirement.cpp)

target_sources(biglybtc_core PRIVATE src/file_policy_restoration.cpp)

target_sources(biglybtc_core PRIVATE src/file_storage_selection.cpp)

target_sources(biglybtc_core PRIVATE src/file_selection_intent.cpp)

target_sources(biglybtc_core PRIVATE src/post_skip_existence.cpp)

option(BIGLYBTC_CORE_TESTS "Test the native core independently of the desktop and storage adapters" OFF)
if(BIGLYBTC_CORE_TESTS)
  enable_testing()
  foreach(test IN ITEMS document_edit executor library torrent_layout auto_speed_v1)
    add_executable(biglybtc-portable-${test}-tests "../tests/${test}_tests.cpp")
    target_link_libraries(biglybtc-portable-${test}-tests PRIVATE BiglyBTC::Core)
    if(MSVC)
      target_compile_options(biglybtc-portable-${test}-tests PRIVATE /utf-8)
    endif()
    add_test(NAME portable-${test} COMMAND biglybtc-portable-${test}-tests)
    set_tests_properties(portable-${test} PROPERTIES TIMEOUT 30)
  endforeach()
endif()
