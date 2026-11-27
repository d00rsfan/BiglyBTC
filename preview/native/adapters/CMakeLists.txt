if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT APPLE AND NOT WIN32)
  message(FATAL_ERROR "The profile and payload adapters require Linux, macOS or Windows")
endif()
include(profile_storage.cmake)
include(mainline_storage.cmake)
biglybtc_add_mainline_storage(biglybtc_profile)
find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
target_link_libraries(biglybtc_profile PRIVATE OpenSSL::Crypto)
add_library(biglybtc_plugin_attributes STATIC plugin_attributes.cpp)
target_include_directories(biglybtc_plugin_attributes PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(biglybtc_plugin_attributes PUBLIC biglybtc_plugin_keys)
target_compile_options(biglybtc_plugin_attributes PRIVATE ${BIGLYBTC_WARNINGS})
add_library(biglybtc_rpc_profile STATIC rpc_profile.cpp rpc_listener_settings.cpp rpc_identity_store.cpp)
target_include_directories(biglybtc_rpc_profile PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(biglybtc_rpc_profile PUBLIC biglybtc_profile biglybtc_plugin_attributes biglybtc_plugin_settings)
target_compile_options(biglybtc_rpc_profile PRIVATE ${BIGLYBTC_WARNINGS})

add_library(biglybtc_saved_service_view STATIC saved_service_view.cpp)
target_include_directories(biglybtc_saved_service_view PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(biglybtc_saved_service_view PUBLIC biglybtc_profile)
target_compile_options(biglybtc_saved_service_view PRIVATE ${BIGLYBTC_WARNINGS})

target_sources(biglybtc_profile PRIVATE minute_history_writer.cpp)

target_sources(biglybtc_profile PRIVATE saved_download_tags_view.cpp)

if(BIGLYBTC_DESKTOP)
  target_sources(biglybtc_profile PRIVATE country_provider.cpp)
  if(WIN32)
    target_sources(biglybtc_profile PRIVATE country_provider_binding_windows.cpp)
  else()
    target_sources(biglybtc_profile PRIVATE country_provider_binding.cpp)
  endif()
  target_link_libraries(biglybtc_profile PUBLIC BiglyBTC::PluginHost)
endif()

target_sources(biglybtc_profile PRIVATE saved_speed_manager_view.cpp)

target_sources(biglybtc_profile PRIVATE library_commands.cpp)

target_sources(biglybtc_profile PRIVATE file_policy_commands.cpp)


target_sources(biglybtc_profile PRIVATE original_file_io.cpp original_payload_io.cpp)

target_sources(biglybtc_profile PRIVATE original_storage_defaults.cpp)

target_sources(biglybtc_profile PRIVATE original_torrent_identity.cpp original_identity_digest.cpp)
target_sources(biglybtc_profile PRIVATE existing_payload_scope.cpp file_operation_digest.cpp file_operation_io.cpp)
target_sources(biglybtc_profile PRIVATE profile_transaction_pending.cpp)

include(mainline_identity.cmake)
biglybtc_add_mainline_identity(biglybtc_profile)

# Worker-only original tag.config edit store; no live tag actions.
target_sources(biglybtc_profile PRIVATE manual_tag_store.cpp state_directory_listing.cpp)
# Owning filesystem worker only; reuses existing guarded state writer.
target_sources(biglybtc_profile PRIVATE category_store.cpp)
# Append after the original9489 category_store.cpp target binding.
set_source_files_properties(category_delete_journal.inc PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(biglybtc_profile PRIVATE category_delete_journal.inc)

# Existing profile/worker only, dependent on frozen pure download-history core.
target_sources(biglybtc_profile PRIVATE download_archive.cpp download_archive_export.cpp download_archive_restore.cpp
  download_archive_restore_transaction.cpp download_archive_payload.cpp download_archive_tags.cpp download_history_store.cpp)

# Receipt uses existing core/EVP/encoded-source adapters, never an SDK owner.
target_sources(biglybtc_profile PRIVATE durable_intake_receipt.cpp)

# One source-bound metadata receipt, no SDK dependency or active owner.
target_sources(biglybtc_profile PRIVATE durable_completion_receipt.cpp)

# Explicit owning Reset witness, no SDK or UI dependency in profile.
target_sources(biglybtc_profile PRIVATE history_reconstruction_receipt.cpp)


# Private one-shot fresh Keep Data history witness; no SDK/provider dependency.
target_sources(biglybtc_profile PRIVATE durable_removal_receipt.cpp)


if(NOT TARGET biglybtc_subscription_automatic_values)
  add_library(biglybtc_subscription_automatic_values STATIC ../services/search/subscription_automatic_values.cpp)
  target_include_directories(biglybtc_subscription_automatic_values PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../services/search")
  target_link_libraries(biglybtc_subscription_automatic_values PUBLIC BiglyBTC::Core)
  target_compile_features(biglybtc_subscription_automatic_values PUBLIC cxx_std_23)
  target_compile_options(biglybtc_subscription_automatic_values PRIVATE ${BIGLYBTC_WARNINGS})
  set_target_properties(biglybtc_subscription_automatic_values PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
target_link_libraries(biglybtc_profile PUBLIC biglybtc_subscription_automatic_values)

target_sources(biglybtc_profile PRIVATE chat_identity_stamp.cpp)

if(BIGLYBTC_DESKTOP)
 add_library(biglybtc_tag_subjects STATIC manual_tag_identity.cpp)
 target_include_directories(biglybtc_tag_subjects PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
 target_link_libraries(biglybtc_tag_subjects PUBLIC biglybtc_profile PRIVATE OpenSSL::Crypto)
 target_compile_options(biglybtc_tag_subjects PRIVATE ${BIGLYBTC_WARNINGS})
endif()

if(TARGET biglybtc_tag_subjects)
 add_library(biglybtc_tag_assignment_owner STATIC tag_assignment_owner.hpp tag_assignment_owner.cpp)
 target_include_directories(biglybtc_tag_assignment_owner PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
 target_link_libraries(biglybtc_tag_assignment_owner PUBLIC biglybtc_tag_subjects)
 target_compile_options(biglybtc_tag_assignment_owner PRIVATE ${BIGLYBTC_WARNINGS})
endif()

target_sources(biglybtc_profile PRIVATE prepared_tag_rate_source.hpp prepared_tag_rate_source.cpp)

# Shared fixed-name original control CAS;no engine or SDK dependency.
target_sources(biglybtc_profile PRIVATE original_control.cpp)
target_sources(biglybtc_profile PRIVATE original_storage_conversion.cpp)
