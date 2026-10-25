# Include from tests/CMakeLists after ordinary desktop targets exist. This adds
# targets only; root keeps existing persistent/causal suites in the BOUND cohort
# and all legacy operational suites in DEFAULT. No skip-as-pass policy.
add_executable(biglybtc-runtime-binding-driver persistent_runtime_binding_driver.cpp)
target_link_libraries(biglybtc-runtime-binding-driver PRIVATE biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar)
target_compile_options(biglybtc-runtime-binding-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST STREQUAL "")
  add_test(NAME persistent-runtime-default-unbound
    COMMAND biglybtc-runtime-binding-driver unbound)
  set_tests_properties(persistent-runtime-default-unbound PROPERTIES TIMEOUT 15 LABELS "unit;runtime-binding")
  if(TARGET biglybtc_desktop_host)
    add_executable(biglybtc-unbound-bridge-tests persistent_unbound_bridge_tests.cpp)
    set_target_properties(biglybtc-unbound-bridge-tests PROPERTIES AUTOMOC ON)
    target_link_libraries(biglybtc-unbound-bridge-tests PRIVATE biglybtc_desktop_host biglybtc_local_engine Qt6::Test
      LibtorrentRasterbar::torrent-rasterbar)
    target_compile_options(biglybtc-unbound-bridge-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME persistent-default-legacy-fallback COMMAND biglybtc-unbound-bridge-tests)
    set_tests_properties(persistent-default-legacy-fallback PROPERTIES TIMEOUT 120
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "unit")
  endif()
else()
  add_test(NAME persistent-runtime-source-bound COMMAND biglybtc-runtime-binding-driver bound)
  set_tests_properties(persistent-runtime-source-bound PROPERTIES TIMEOUT 15 LABELS "unit;runtime-binding")
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME persistent-runtime-manifest-negatives COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime_manifest_tests.py")
  set_tests_properties(persistent-runtime-manifest-negatives PROPERTIES TIMEOUT 60 LABELS "unit;runtime-binding")
  add_executable(biglybtc-runtime-negative-driver persistent_runtime_negative_driver.cpp)
  target_link_libraries(biglybtc-runtime-negative-driver PRIVATE biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar)
  target_include_directories(biglybtc-runtime-negative-driver PRIVATE
    "${PROJECT_BINARY_DIR}/engine/runtime-binding")
  target_compile_options(biglybtc-runtime-negative-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_link_options(biglybtc-runtime-negative-driver PRIVATE
    "LINKER:--wrap=open" "LINKER:--wrap=fstat" "LINKER:--wrap=read")
  foreach(_biglybtc_fault open inode content fifo)
    add_test(NAME persistent-runtime-reject-${_biglybtc_fault}
      COMMAND biglybtc-runtime-negative-driver "${_biglybtc_fault}")
    set_tests_properties(persistent-runtime-reject-${_biglybtc_fault} PROPERTIES TIMEOUT 15 LABELS "unit;runtime-binding")
  endforeach()
  add_library(biglybtc-runtime-interposer SHARED runtime_interposer.cpp)
  target_compile_options(biglybtc-runtime-interposer PRIVATE -Wall -Wextra -Wpedantic -Werror)

  # GCC ASan must initialize before the owned LD_PRELOAD interposer. This is
  # source-reviewed test setup for the existing sanitizer lane, not an observed
  # loader failure or a reason to disable ASan's link-order/leak checks.
  set(_biglybtc_runtime_preload "$<TARGET_FILE:biglybtc-runtime-interposer>")
  string(TOUPPER "${CMAKE_BUILD_TYPE}" _biglybtc_runtime_config)
  set(_biglybtc_runtime_flags
    "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${_biglybtc_runtime_config}} ${CMAKE_EXE_LINKER_FLAGS} ${CMAKE_EXE_LINKER_FLAGS_${_biglybtc_runtime_config}}")
  separate_arguments(_biglybtc_runtime_flags UNIX_COMMAND "${_biglybtc_runtime_flags}")
  set(_biglybtc_runtime_address_sanitized FALSE)
  foreach(_biglybtc_runtime_flag IN LISTS _biglybtc_runtime_flags)
    if(_biglybtc_runtime_flag MATCHES "^-fsanitize=(.*)$")
      string(REPLACE "," ";" _biglybtc_runtime_sanitizers "${CMAKE_MATCH_1}")
      if("address" IN_LIST _biglybtc_runtime_sanitizers)
        set(_biglybtc_runtime_address_sanitized TRUE)
      endif()
    elseif(_biglybtc_runtime_flag MATCHES "^-fno-sanitize=(.*)$")
      string(REPLACE "," ";" _biglybtc_runtime_sanitizers "${CMAKE_MATCH_1}")
      if("address" IN_LIST _biglybtc_runtime_sanitizers OR "all" IN_LIST _biglybtc_runtime_sanitizers)
        set(_biglybtc_runtime_address_sanitized FALSE)
      endif()
    endif()
  endforeach()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND _biglybtc_runtime_address_sanitized)
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=libasan.so
      RESULT_VARIABLE _biglybtc_runtime_asan_result
      OUTPUT_VARIABLE _biglybtc_runtime_asan
      ERROR_VARIABLE _biglybtc_runtime_asan_error
      OUTPUT_STRIP_TRAILING_WHITESPACE TIMEOUT 15)
    if(NOT _biglybtc_runtime_asan_result STREQUAL "0"
        OR NOT IS_ABSOLUTE "${_biglybtc_runtime_asan}"
        OR NOT EXISTS "${_biglybtc_runtime_asan}"
        OR IS_DIRECTORY "${_biglybtc_runtime_asan}"
        OR _biglybtc_runtime_asan MATCHES "[;: \t\r\n]")
      message(FATAL_ERROR "Selected GCC did not report an existing absolute libasan.so: ${_biglybtc_runtime_asan_error}")
    endif()
    file(REAL_PATH "${_biglybtc_runtime_asan}" _biglybtc_runtime_asan)
    if(NOT IS_ABSOLUTE "${_biglybtc_runtime_asan}"
        OR NOT EXISTS "${_biglybtc_runtime_asan}"
        OR IS_DIRECTORY "${_biglybtc_runtime_asan}"
        OR _biglybtc_runtime_asan MATCHES "[;: \t\r\n]")
      message(FATAL_ERROR "Selected GCC libasan target is not an unambiguous absolute preload path")
    endif()
    set(_biglybtc_runtime_preload
      "${_biglybtc_runtime_asan}:$<TARGET_FILE:biglybtc-runtime-interposer>")
    message(STATUS "Runtime binding preload tests: selected GCC libasan first: ${_biglybtc_runtime_asan}")
  endif()
  add_test(NAME persistent-runtime-reject-preload COMMAND "${CMAKE_COMMAND}" -E env
    "LD_PRELOAD=${_biglybtc_runtime_preload}"
    "$<TARGET_FILE:biglybtc-runtime-binding-driver>" unbound)
  set_tests_properties(persistent-runtime-reject-preload PROPERTIES TIMEOUT 15 LABELS "unit;runtime-binding")
  add_executable(biglybtc-runtime-interposition-driver runtime_interposition_driver.cpp)
  target_link_libraries(biglybtc-runtime-interposition-driver PRIVATE biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar ${CMAKE_DL_LIBS})
  target_compile_options(biglybtc-runtime-interposition-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME persistent-runtime-reject-mapped-interposition COMMAND "${CMAKE_COMMAND}" -E env
    "LD_PRELOAD=${_biglybtc_runtime_preload}"
    "$<TARGET_FILE:biglybtc-runtime-interposition-driver>")
  set_tests_properties(persistent-runtime-reject-mapped-interposition PROPERTIES TIMEOUT 15 LABELS "unit;runtime-binding")
  # Actual same-version provider replacement, not a synthetic version string.
  set(BIGLYBTC_PERSISTENT_NEGATIVE_LT_LIBRARY "" CACHE FILEPATH
    "Exact independently retained distro DSO for bound-mode loader rejection")
  if(NOT EXISTS "${BIGLYBTC_PERSISTENT_NEGATIVE_LT_LIBRARY}")
    message(FATAL_ERROR "Bound qualification requires the actual distro rejection fixture")
  endif()
  file(SHA256 "${BIGLYBTC_PERSISTENT_NEGATIVE_LT_LIBRARY}" _biglybtc_distro_sha)
  if(NOT _biglybtc_distro_sha STREQUAL "bbe6713f187140db190cc86e1140de36d006954d9b1ee1c581eeeb5cd98af427")
    message(FATAL_ERROR "Unexpected distro rejection fixture")
  endif()
  get_filename_component(_biglybtc_distro_dir "${BIGLYBTC_PERSISTENT_NEGATIVE_LT_LIBRARY}" DIRECTORY)
  add_test(NAME persistent-runtime-reject-same-version-distro COMMAND "${CMAKE_COMMAND}" -E env
    "LD_LIBRARY_PATH=${_biglybtc_distro_dir}"
    "$<TARGET_FILE:biglybtc-runtime-binding-driver>" unbound)
  set_tests_properties(persistent-runtime-reject-same-version-distro PROPERTIES TIMEOUT 15 LABELS "unit;runtime-binding")
endif()
