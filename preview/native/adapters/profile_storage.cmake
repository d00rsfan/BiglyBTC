set(profile_adapter_sources
  profile.cpp profile_config_read.cpp profile_config_posix.cpp file_io.cpp torrent_state.cpp library_view.cpp column_layout_view.cpp
  statistics_view.cpp overall_statistics_view.cpp history_view.cpp history_week_view.cpp
  history_month_view.cpp history_range_view.cpp
  azureus_contacts.cpp azureus_value_versions.cpp azureus_remote_diversification.cpp
  azureus_public_policy.cpp)
list(TRANSFORM profile_adapter_sources PREPEND "${CMAKE_CURRENT_LIST_DIR}/")

add_library(biglybtc_profile STATIC ${profile_adapter_sources})
target_include_directories(biglybtc_profile PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(biglybtc_profile PUBLIC BiglyBTC::Core biglybtc_profile_lease)
target_compile_options(biglybtc_profile PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(APPLE)
  find_package(ZLIB REQUIRED)
else()
  find_package(ZLIB 1.3.1 EXACT REQUIRED)
endif()
add_library(biglybtc_state_codec STATIC "${CMAKE_CURRENT_LIST_DIR}/encoded_document.cpp")
target_include_directories(biglybtc_state_codec PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(biglybtc_state_codec PUBLIC BiglyBTC::Core ZLIB::ZLIB)
target_compile_options(biglybtc_state_codec PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(biglybtc_profile PUBLIC biglybtc_state_codec)
