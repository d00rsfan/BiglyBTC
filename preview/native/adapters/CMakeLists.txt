if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "Profile locking adapter is currently qualified for Linux development only; Windows/macOS remain pending")
endif()
add_library(biglybtc_profile STATIC profile.cpp file_io.cpp torrent_state.cpp library_view.cpp column_layout_view.cpp statistics_view.cpp overall_statistics_view.cpp history_view.cpp history_week_view.cpp history_month_view.cpp history_range_view.cpp)
target_include_directories(biglybtc_profile PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(biglybtc_profile PUBLIC BiglyBTC::Core)
target_compile_options(biglybtc_profile PRIVATE -Wall -Wextra -Wpedantic -Werror)
find_package(ZLIB 1.3.1 EXACT REQUIRED)
add_library(biglybtc_state_codec STATIC encoded_document.cpp)
target_include_directories(biglybtc_state_codec PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(biglybtc_state_codec PUBLIC BiglyBTC::Core ZLIB::ZLIB)
target_compile_options(biglybtc_state_codec PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(biglybtc_profile PUBLIC biglybtc_state_codec)
add_library(biglybtc_mldht_cache STATIC mldht_cache.cpp)
target_include_directories(biglybtc_mldht_cache PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(biglybtc_mldht_cache PUBLIC BiglyBTC::Core)
target_compile_options(biglybtc_mldht_cache PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_library(biglybtc_plugin_settings STATIC plugin_settings.cpp)
target_include_directories(biglybtc_plugin_settings PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(biglybtc_plugin_settings PUBLIC BiglyBTC::Core)
target_compile_options(biglybtc_plugin_settings PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_library(biglybtc_plugin_keys STATIC plugin_keys.cpp)
target_include_directories(biglybtc_plugin_keys PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(biglybtc_plugin_keys PUBLIC BiglyBTC::Core)
target_compile_options(biglybtc_plugin_keys PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(biglybtc_plugin_settings PUBLIC biglybtc_plugin_keys)
add_library(biglybtc_plugin_attributes STATIC plugin_attributes.cpp)
target_include_directories(biglybtc_plugin_attributes PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(biglybtc_plugin_attributes PUBLIC biglybtc_plugin_keys)
target_compile_options(biglybtc_plugin_attributes PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_library(biglybtc_rpc_profile STATIC rpc_profile.cpp)
target_include_directories(biglybtc_rpc_profile PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(biglybtc_rpc_profile PUBLIC biglybtc_profile biglybtc_plugin_attributes biglybtc_plugin_settings)
target_compile_options(biglybtc_rpc_profile PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(biglybtc_saved_service_view STATIC saved_service_view.cpp)
target_include_directories(biglybtc_saved_service_view PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(biglybtc_saved_service_view PUBLIC biglybtc_profile)
target_compile_options(biglybtc_saved_service_view PRIVATE -Wall -Wextra -Wpedantic -Werror)

target_sources(biglybtc_profile PRIVATE minute_history_writer.cpp)

target_sources(biglybtc_profile PRIVATE saved_download_tags_view.cpp)

if(BIGLYBTC_DESKTOP)
  find_package(OpenSSL REQUIRED COMPONENTS Crypto)
  target_sources(biglybtc_profile PRIVATE country_provider.cpp country_provider_binding.cpp)
  target_link_libraries(biglybtc_profile PUBLIC BiglyBTC::PluginHost PRIVATE OpenSSL::Crypto)
endif()

target_sources(biglybtc_profile PRIVATE saved_speed_manager_view.cpp)

target_sources(biglybtc_profile PRIVATE library_commands.cpp)

target_sources(biglybtc_profile PRIVATE file_policy_commands.cpp)

target_sources(biglybtc_profile PRIVATE application_settings.cpp)

target_sources(biglybtc_profile PRIVATE original_file_io.cpp)

target_sources(biglybtc_profile PRIVATE original_storage_defaults.cpp)

target_sources(biglybtc_profile PRIVATE original_torrent_identity.cpp)

# Disposable native engine checkpoint; original plugin data remain read-only.
add_library(biglybtc_mainline_checkpoint STATIC mainline_checkpoint.cpp)
target_include_directories(biglybtc_mainline_checkpoint PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(biglybtc_mainline_checkpoint PUBLIC biglybtc_profile PRIVATE biglybtc_mldht_cache biglybtc_plugin_settings)
target_compile_options(biglybtc_mainline_checkpoint PRIVATE -Wall -Wextra -Wpedantic -Werror)
