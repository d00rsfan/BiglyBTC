function(biglybtc_add_portable_network_tests network_target)
  add_executable(biglybtc-portable-network-binding-tests
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/portable_network_binding_tests.cpp")
  target_link_libraries(biglybtc-portable-network-binding-tests PRIVATE
    ${network_target} ${ARGN} biglybtc_network_interfaces LibtorrentRasterbar::torrent-rasterbar)
  if(MSVC)
    target_compile_options(biglybtc-portable-network-binding-tests PRIVATE /utf-8 /Zc:__cplusplus)
  else()
    target_compile_options(biglybtc-portable-network-binding-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME portable-network-binding COMMAND biglybtc-portable-network-binding-tests)
  set_tests_properties(portable-network-binding PROPERTIES TIMEOUT 30 LABELS "routing;platform;no-network")
endfunction()

function(biglybtc_add_portable_subscription_seed_tests engine_target source_target)
  find_package(Qt6 ${BIGLYBTC_QT_REQUIREMENT} REQUIRED COMPONENTS Core Network)
  set(seed_fixture "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/fixtures/subscription-seed-reference.dat")

  add_executable(biglybtc-subscription-seed-values-tests
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/subscription_seed_values_tests.cpp")
  target_link_libraries(biglybtc-subscription-seed-values-tests PRIVATE ${source_target} Qt6::Core)
  target_compile_options(biglybtc-subscription-seed-values-tests PRIVATE ${BIGLYBTC_WARNINGS})
  add_test(NAME subscription-seed-values COMMAND biglybtc-subscription-seed-values-tests "${seed_fixture}")
  set_tests_properties(subscription-seed-values PROPERTIES TIMEOUT 30 LABELS "unit;fixtures;subscriptions")

  add_executable(biglybtc-subscription-seed-owner-tests
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/subscription_seed_owner_tests.cpp")
  target_link_libraries(biglybtc-subscription-seed-owner-tests PRIVATE ${engine_target}
    LibtorrentRasterbar::torrent-rasterbar Qt6::Network)
  target_compile_options(biglybtc-subscription-seed-owner-tests PRIVATE ${BIGLYBTC_WARNINGS})
  add_test(NAME subscription-seed-owner COMMAND biglybtc-subscription-seed-owner-tests "${seed_fixture}")
  set_tests_properties(subscription-seed-owner PROPERTIES TIMEOUT 35 SKIP_RETURN_CODE 77
    LABELS "network;owned-loopback;subscriptions")
endfunction()

function(biglybtc_add_portable_magnet_test engine_target)
  add_executable(biglybtc-magnet-intake-tests
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/magnet_intake_tests.cpp")
  target_link_libraries(biglybtc-magnet-intake-tests PRIVATE ${engine_target} LibtorrentRasterbar::torrent-rasterbar)
  if(MSVC)
    target_compile_options(biglybtc-magnet-intake-tests PRIVATE /utf-8 /Zc:__cplusplus)
  else()
    target_compile_options(biglybtc-magnet-intake-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME magnet-intake COMMAND biglybtc-magnet-intake-tests)
  set_tests_properties(magnet-intake PROPERTIES TIMEOUT 240 LABELS "unit;torrent-workflows;storage;profile;no-network")
endfunction()
