# Exact private PORT child only; all older cohort registrations stay unchanged.
if(BIGLYBTC_LOCAL_ENGINE AND NOT BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST STREQUAL "")
  file(SHA256 "${BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST}" _biglybtc_mainline_manifest)
  if(_biglybtc_mainline_manifest STREQUAL "1267bf58a235dc6c32d9ffadab8bbd994ec81f2aa958f0cf621f8a71e4f6c84c")
    if(NOT TARGET biglybtc-runtime-interposition-driver OR NOT TARGET biglybtc-runtime-interposer
        OR NOT EXISTS "${BIGLYBTC_PERSISTENT_NEGATIVE_LT_LIBRARY}")
      message(FATAL_ERROR "Mainline PORT qualification requires existing runtime rejection fixtures")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME provider-mainline-runtime-inputs COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/provider_mainline_runtime_inputs_tests.py")
    set_tests_properties(provider-mainline-runtime-inputs PROPERTIES TIMEOUT 180
      LABELS "unit;qualified-provider;source-binding" ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    add_executable(biglybtc-provider-mainline-runtime-deployment-driver runtime_deployment_driver.cpp)
    target_link_libraries(biglybtc-provider-mainline-runtime-deployment-driver PRIVATE
      biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar ${CMAKE_DL_LIBS})
    target_include_directories(biglybtc-provider-mainline-runtime-deployment-driver PRIVATE
      "${PROJECT_SOURCE_DIR}/engine" "${PROJECT_BINARY_DIR}/engine/runtime-binding")
    target_compile_definitions(biglybtc-provider-mainline-runtime-deployment-driver PRIVATE BIGLYBTC_MAINLINE_PORT_DEPLOYMENT=1)
    target_compile_options(biglybtc-provider-mainline-runtime-deployment-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_options(biglybtc-provider-mainline-runtime-deployment-driver PRIVATE
      "LINKER:--wrap=open" "LINKER:--wrap=fstat" "LINKER:--wrap=read" "LINKER:--wrap=__read_chk")
    add_test(NAME provider-mainline-runtime-deployment COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/runtime_deployment_tests.py"
      --driver "$<TARGET_FILE:biglybtc-provider-mainline-runtime-deployment-driver>"
      --provider "$<TARGET_FILE:LibtorrentRasterbar::torrent-rasterbar>"
      --manifest "${BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST}" --mode mainline
      --alternate "${BIGLYBTC_PERSISTENT_NEGATIVE_LT_LIBRARY}"
      --interposition-driver "$<TARGET_FILE:biglybtc-runtime-interposition-driver>"
      --preload "${_biglybtc_runtime_preload}")
    set_tests_properties(provider-mainline-runtime-deployment PROPERTIES TIMEOUT 180
      LABELS "unit;qualified-provider;runtime-binding" ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    add_library(biglybtc-mainline-port-foreign-marker SHARED mainline_port_foreign_marker.cpp)
    target_compile_options(biglybtc-mainline-port-foreign-marker PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_executable(biglybtc-mainline-port-interposition-driver mainline_port_interposition_driver.cpp)
    add_dependencies(biglybtc-mainline-port-interposition-driver biglybtc-mainline-port-foreign-marker)
    target_link_libraries(biglybtc-mainline-port-interposition-driver PRIVATE
      biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar ${CMAKE_DL_LIBS})
    target_include_directories(biglybtc-mainline-port-interposition-driver PRIVATE "${PROJECT_SOURCE_DIR}/engine")
    target_compile_options(biglybtc-mainline-port-interposition-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
    set(_biglybtc_mainline_preload "$<TARGET_FILE:biglybtc-mainline-port-foreign-marker>")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND _biglybtc_runtime_address_sanitized)
      if(NOT IS_ABSOLUTE "${_biglybtc_runtime_asan}" OR NOT EXISTS "${_biglybtc_runtime_asan}")
        message(FATAL_ERROR "Mainline marker negative requires existing GCC ASan-first preload setup")
      endif()
      set(_biglybtc_mainline_preload "${_biglybtc_runtime_asan}:${_biglybtc_mainline_preload}")
    endif()
    add_test(NAME provider-mainline-reject-foreign-marker COMMAND "${CMAKE_COMMAND}" -E env
      --unset=LD_AUDIT "LD_PRELOAD=${_biglybtc_mainline_preload}"
      "$<TARGET_FILE:biglybtc-mainline-port-interposition-driver>")
    set_tests_properties(provider-mainline-reject-foreign-marker PROPERTIES TIMEOUT 15
      LABELS "unit;qualified-provider;runtime-binding")
  endif()
endif()
