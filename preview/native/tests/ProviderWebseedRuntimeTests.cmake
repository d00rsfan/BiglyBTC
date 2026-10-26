# Root adds one include after existing runtime rejection targets are registered.
# This exact branch never selects or weakens old/default/strict provider lanes.
if(BIGLYBTC_LOCAL_ENGINE AND NOT BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST STREQUAL "")
  file(SHA256 "${BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST}" _biglybtc_webseed_manifest)
  if(_biglybtc_webseed_manifest STREQUAL "ea82f78982a1428eb64067719cf30bbca7cfd09dd2abfcf8f76024f6e6ed41d5")
    if(NOT TARGET biglybtc-runtime-interposition-driver OR NOT TARGET biglybtc-runtime-interposer
        OR NOT EXISTS "${BIGLYBTC_PERSISTENT_NEGATIVE_LT_LIBRARY}")
      message(FATAL_ERROR "Webseed qualification requires every existing runtime rejection fixture")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME provider-webseed-runtime-inputs COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/provider_webseed_runtime_inputs_tests.py")
    set_tests_properties(provider-webseed-runtime-inputs PROPERTIES TIMEOUT 180
      LABELS "unit;qualified-provider;source-binding" ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    add_executable(biglybtc-provider-webseed-runtime-deployment-driver runtime_deployment_driver.cpp)
    target_link_libraries(biglybtc-provider-webseed-runtime-deployment-driver PRIVATE
      biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar ${CMAKE_DL_LIBS})
    target_include_directories(biglybtc-provider-webseed-runtime-deployment-driver PRIVATE
      "${PROJECT_BINARY_DIR}/engine/runtime-binding")
    target_compile_options(biglybtc-provider-webseed-runtime-deployment-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_options(biglybtc-provider-webseed-runtime-deployment-driver PRIVATE
      "LINKER:--wrap=open" "LINKER:--wrap=fstat" "LINKER:--wrap=read" "LINKER:--wrap=__read_chk")
    add_test(NAME provider-webseed-runtime-deployment COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/runtime_deployment_tests.py"
      --driver "$<TARGET_FILE:biglybtc-provider-webseed-runtime-deployment-driver>"
      --provider "/home/yurii/Java/BiglyBT/build/compatibility-20261026/provider-webseed/install/lib/libtorrent-rasterbar.so.2.0.12"
      --manifest "${BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST}" --mode webseed
      --alternate "${BIGLYBTC_PERSISTENT_NEGATIVE_LT_LIBRARY}"
      --interposition-driver "$<TARGET_FILE:biglybtc-runtime-interposition-driver>" --preload "${_biglybtc_runtime_preload}")
    set_tests_properties(provider-webseed-runtime-deployment PROPERTIES TIMEOUT 180
      LABELS "unit;qualified-provider;runtime-binding" ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
  endif()
endif()
