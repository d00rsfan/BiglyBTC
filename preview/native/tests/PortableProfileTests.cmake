function(biglybtc_profile_workflow_tests profile_target with_torrent_intake)
  add_executable(biglybtc-azureus-profile-storage-tests "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/azureus_profile_storage_tests.cpp")
  target_include_directories(biglybtc-azureus-profile-storage-tests PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../adapters")
  target_link_libraries(biglybtc-azureus-profile-storage-tests PRIVATE ${profile_target})
  add_test(NAME azureus-profile-storage COMMAND biglybtc-azureus-profile-storage-tests)
  set_tests_properties(azureus-profile-storage PROPERTIES TIMEOUT 30 LABELS "storage;profile;no-network")

  add_executable(biglybtc-portable-minute-history-tests "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/portable_minute_history_tests.cpp")
  target_include_directories(biglybtc-portable-minute-history-tests PRIVATE
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../adapters")
  target_link_libraries(biglybtc-portable-minute-history-tests PRIVATE ${profile_target})
  add_test(NAME portable-minute-history COMMAND biglybtc-portable-minute-history-tests)
  set_tests_properties(portable-minute-history PROPERTIES TIMEOUT 30 LABELS "storage;statistics;profile")

  add_executable(biglybtc-portable-mainline-checkpoint-tests
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/portable_mainline_checkpoint_tests.cpp")
  target_link_libraries(biglybtc-portable-mainline-checkpoint-tests PRIVATE biglybtc_mainline_checkpoint)
  if(TARGET biglybtc_mainline_identity)
    target_link_libraries(biglybtc-portable-mainline-checkpoint-tests PRIVATE biglybtc_mainline_identity)
    target_compile_definitions(biglybtc-portable-mainline-checkpoint-tests PRIVATE BIGLYBTC_TEST_MAINLINE_IDENTITY)
  endif()
  add_test(NAME portable-mainline-checkpoint COMMAND biglybtc-portable-mainline-checkpoint-tests)
  set_tests_properties(portable-mainline-checkpoint PROPERTIES TIMEOUT 30 LABELS "storage;profile;discovery")

  set(profile_tests biglybtc-azureus-profile-storage-tests biglybtc-portable-minute-history-tests
    biglybtc-portable-mainline-checkpoint-tests)
  if(with_torrent_intake)
    add_executable(biglybtc-torrent-intake-tests "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/torrent_intake_tests.cpp")
    target_include_directories(biglybtc-torrent-intake-tests PRIVATE
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../services/search")
    target_link_libraries(biglybtc-torrent-intake-tests PRIVATE ${profile_target} LibtorrentRasterbar::torrent-rasterbar)
    if(MINGW)
      target_link_options(biglybtc-torrent-intake-tests PRIVATE -municode)
    endif()
    add_test(NAME torrent-intake COMMAND biglybtc-torrent-intake-tests)
    set_tests_properties(torrent-intake PROPERTIES TIMEOUT 180 LABELS "unit;torrent-workflows")
    add_test(NAME subscription-acquisition-intake-pending-guards
      COMMAND biglybtc-torrent-intake-tests --acquisition-pending-guards)
    set_tests_properties(subscription-acquisition-intake-pending-guards PROPERTIES
      TIMEOUT 30 LABELS "integration;intake;subscriptions;owned-profile;no-network")
    list(APPEND profile_tests biglybtc-torrent-intake-tests)
  endif()

  foreach(test IN LISTS profile_tests)
    if(MSVC)
      target_compile_options(${test} PRIVATE /utf-8 /Zc:__cplusplus)
    else()
      target_compile_options(${test} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
  endforeach()
endfunction()
