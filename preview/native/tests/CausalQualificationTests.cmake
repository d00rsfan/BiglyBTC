# Explicit test registration only; never bypasses ordinary runtime admission.
option(BIGLYBTC_CAUSAL_QUALIFICATION_TESTS "Build isolated test-only LT causal cohorts" OFF)
if(BIGLYBTC_CAUSAL_QUALIFICATION_TESTS)
  file(SHA256 "${BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST}" _biglybtc_causal_manifest_sha)
  if(NOT _biglybtc_causal_manifest_sha STREQUAL "37ce74d783fe42e42f09415ffd4358f330d5849803ef1064cb5abab7f89c5abc")
    message(FATAL_ERROR "Causal tests require the exact reviewed instrumented provider manifest")
  endif()
  add_executable(biglybtc-persistent-causal-tests persistent_causal_tests.cpp)
  set_target_properties(biglybtc-persistent-causal-tests PROPERTIES AUTOMOC ON)
  target_link_libraries(biglybtc-persistent-causal-tests PRIVATE biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar Qt6::Test)
  target_compile_definitions(biglybtc-persistent-causal-tests PRIVATE BIGLYBTC_LT_CAUSAL_TEST_ONLY=1)
  target_compile_options(biglybtc-persistent-causal-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME persistent-native-causal COMMAND biglybtc-persistent-causal-tests)
  set_tests_properties(persistent-native-causal PROPERTIES TIMEOUT 180 LABELS "qualification;statistics;loopback;causal")
# Source-only registration proposal, not an executed configure fragment.
# Root places these three staged inputs in its qualification-only test source
# directory and supplies the exact installed INSTRUMENTED LT include/library.
if(BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST STREQUAL "")
  message(FATAL_ERROR "Actual-alert policy tests require an approved instrumented provider binding")
endif()
add_executable(biglybtc-persistent-actual-alert-tests
  actual_alert_policy.cpp persistent_actual_alert_tests.cpp)
set_target_properties(biglybtc-persistent-actual-alert-tests PROPERTIES AUTOMOC ON)
target_link_libraries(biglybtc-persistent-actual-alert-tests PRIVATE
  biglybtc_local_engine Qt6::Test LibtorrentRasterbar::torrent-rasterbar)
target_include_directories(biglybtc-persistent-actual-alert-tests PRIVATE
  "${PROJECT_SOURCE_DIR}/engine" "${PROJECT_SOURCE_DIR}/tests"
  "${PROJECT_BINARY_DIR}/engine/runtime-binding")
target_compile_definitions(biglybtc-persistent-actual-alert-tests PRIVATE BIGLYBTC_LT_CAUSAL_TEST_ONLY=1)
target_compile_options(biglybtc-persistent-actual-alert-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(biglybtc-persistent-actual-alert-tests PRIVATE
  "LINKER:--wrap=_ZN10libtorrent14session_handle18post_session_statsEv"
  "LINKER:--wrap=_ZN10libtorrent14session_handle10pop_alertsEPSt6vectorIPNS_5alertESaIS3_EE")
add_test(NAME persistent-actual-alert-policy COMMAND biglybtc-persistent-actual-alert-tests)
set_tests_properties(persistent-actual-alert-policy PROPERTIES
  TIMEOUT 180 LABELS "qualification;runtime-binding;actual-alert-policy")
# No wrappers from persistent_api_faults.cpp, fake spans, remove hooks, preload,
# default-build skipping, extra fixture files or GUI platform are needed here.

endif()
