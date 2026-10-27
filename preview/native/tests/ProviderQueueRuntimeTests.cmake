# Additional exact queue observer cohort; previous registrations stay scoped.
if(BIGLYBTC_LOCAL_ENGINE AND NOT BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST STREQUAL "")
  file(SHA256 "${BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST}" _biglybtc_queue_manifest)
  if(_biglybtc_queue_manifest STREQUAL "f95542706a3b7324c6f2589d90d51a4374c92e7e245319b156f044aeec76a1d8")
    if(NOT TARGET biglybtc-runtime-interposition-driver OR NOT TARGET biglybtc-runtime-interposer
        OR NOT EXISTS "${BIGLYBTC_PERSISTENT_NEGATIVE_LT_LIBRARY}")
      message(FATAL_ERROR "Queue observer qualification requires existing runtime rejection fixtures")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME provider-queue-runtime-inputs COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/provider_queue_runtime_inputs_tests.py")
    set_tests_properties(provider-queue-runtime-inputs PROPERTIES TIMEOUT 180 LABELS "unit;qualified-provider;source-binding" ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    add_executable(biglybtc-provider-queue-runtime-deployment-driver runtime_deployment_driver.cpp)
    target_link_libraries(biglybtc-provider-queue-runtime-deployment-driver PRIVATE biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar ${CMAKE_DL_LIBS})
    target_include_directories(biglybtc-provider-queue-runtime-deployment-driver PRIVATE "${PROJECT_BINARY_DIR}/engine/runtime-binding")
    target_compile_options(biglybtc-provider-queue-runtime-deployment-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_options(biglybtc-provider-queue-runtime-deployment-driver PRIVATE "LINKER:--wrap=open" "LINKER:--wrap=fstat" "LINKER:--wrap=read" "LINKER:--wrap=__read_chk")
    add_test(NAME provider-queue-runtime-deployment COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/runtime_deployment_tests.py"
      --driver "$<TARGET_FILE:biglybtc-provider-queue-runtime-deployment-driver>"
      --provider "$<TARGET_FILE:LibtorrentRasterbar::torrent-rasterbar>" --manifest "${BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST}" --mode queue
      --alternate "${BIGLYBTC_PERSISTENT_NEGATIVE_LT_LIBRARY}" --interposition-driver "$<TARGET_FILE:biglybtc-runtime-interposition-driver>" --preload "${_biglybtc_runtime_preload}")
    set_tests_properties(provider-queue-runtime-deployment PROPERTIES TIMEOUT 180 LABELS "unit;qualified-provider;runtime-binding" ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
  endif()
endif()
