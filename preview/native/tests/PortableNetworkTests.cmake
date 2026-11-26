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
