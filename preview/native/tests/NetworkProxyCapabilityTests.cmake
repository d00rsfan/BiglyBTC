# Include after RuntimeBindingTests.cmake. This chooses TEST expectations only;
# the production capability always requires the full existing RuntimeGuard proof.
if(BIGLYBTC_LOCAL_ENGINE AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(biglybtc-network-proxy-capability-tests network_proxy_capability_driver.cpp)
  target_link_libraries(biglybtc-network-proxy-capability-tests PRIVATE
    biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar ${CMAKE_DL_LIBS})
  target_compile_options(biglybtc-network-proxy-capability-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  set(_biglybtc_socks4_expect absent)
  set(_biglybtc_webseed_expect FALSE)
  if(NOT BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST STREQUAL "")
    file(SHA256 "${BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST}" _biglybtc_socks4_manifest)
    # Root's already independently reviewed new source-image manifest. Matching
    # this test condition cannot bypass production source/catalog/loader checks.
    if(_biglybtc_socks4_manifest STREQUAL "3342ec8ae72ecafa4a1b887e664d0d066fffce4f7d864b0b130ca903be953a3a")
      set(_biglybtc_socks4_expect present)
    elseif(_biglybtc_socks4_manifest STREQUAL "ea82f78982a1428eb64067719cf30bbca7cfd09dd2abfcf8f76024f6e6ed41d5"
        OR _biglybtc_socks4_manifest STREQUAL "f95542706a3b7324c6f2589d90d51a4374c92e7e245319b156f044aeec76a1d8"
        OR _biglybtc_socks4_manifest STREQUAL "5901b34eeb25370106bdbf6185a80f0c86c62a837bf2e455f5619921db665760")
      set(_biglybtc_socks4_expect present-webseed)
      set(_biglybtc_webseed_expect TRUE)
    endif()
  endif()
  add_test(NAME network-proxy-capability-${_biglybtc_socks4_expect}
    COMMAND "${CMAKE_COMMAND}" -E env --unset=LD_PRELOAD --unset=LD_AUDIT
      "$<TARGET_FILE:biglybtc-network-proxy-capability-tests>" "${_biglybtc_socks4_expect}")
  set_tests_properties(network-proxy-capability-${_biglybtc_socks4_expect} PROPERTIES
    TIMEOUT 15 LABELS "unit;runtime-binding;network")
  if(_biglybtc_socks4_expect MATCHES "^present")
    if(BIGLYBTC_DESKTOP)
      if(NOT TARGET biglybtc-options-host-tests)
        message(FATAL_ERROR "Include NetworkProxyCapabilityTests.cmake after the Options Host target and RuntimeBindingTests.cmake")
      endif()
      target_compile_definitions(biglybtc-options-host-tests PRIVATE BIGLYBTC_SOCKS4_HOST_QUALIFIED_TESTS=1)
      target_link_libraries(biglybtc-options-host-tests PRIVATE LibtorrentRasterbar::torrent-rasterbar)
    endif()
    add_library(biglybtc-network-proxy-foreign-marker SHARED network_proxy_foreign_marker.cpp)
    target_compile_options(biglybtc-network-proxy-foreign-marker PRIVATE -Wall -Wextra -Wpedantic -Werror)
    set(_biglybtc_socks4_preload "$<TARGET_FILE:biglybtc-network-proxy-foreign-marker>")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND _biglybtc_runtime_address_sanitized)
      if(NOT IS_ABSOLUTE "${_biglybtc_runtime_asan}" OR NOT EXISTS "${_biglybtc_runtime_asan}")
        message(FATAL_ERROR "SOCKS marker negative requires the reviewed GCC ASan-first preload setup")
      endif()
      set(_biglybtc_socks4_preload "${_biglybtc_runtime_asan}:${_biglybtc_socks4_preload}")
    endif()
    add_test(NAME network-proxy-capability-foreign-marker
      COMMAND "${CMAKE_COMMAND}" -E env --unset=LD_AUDIT "LD_PRELOAD=${_biglybtc_socks4_preload}"
        "$<TARGET_FILE:biglybtc-network-proxy-capability-tests>" foreign)
    set_tests_properties(network-proxy-capability-foreign-marker PROPERTIES
      TIMEOUT 15 LABELS "unit;runtime-binding;network")
    if(_biglybtc_webseed_expect)
      add_library(biglybtc-network-proxy-foreign-webseed-marker SHARED network_proxy_foreign_webseed_marker.cpp)
      target_compile_options(biglybtc-network-proxy-foreign-webseed-marker PRIVATE -Wall -Wextra -Wpedantic -Werror)
      set(_biglybtc_webseed_preload "$<TARGET_FILE:biglybtc-network-proxy-foreign-webseed-marker>")
      if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND _biglybtc_runtime_address_sanitized)
        set(_biglybtc_webseed_preload "${_biglybtc_runtime_asan}:${_biglybtc_webseed_preload}")
      endif()
      add_test(NAME network-proxy-capability-foreign-webseed-marker
        COMMAND "${CMAKE_COMMAND}" -E env --unset=LD_AUDIT "LD_PRELOAD=${_biglybtc_webseed_preload}"
          "$<TARGET_FILE:biglybtc-network-proxy-capability-tests>" foreign-webseed)
      set_tests_properties(network-proxy-capability-foreign-webseed-marker PROPERTIES
        TIMEOUT 15 LABELS "unit;runtime-binding;network")
    endif()
  endif()
endif()
