# Additional exact identity/tracker lane. All prior registrations stay scoped.
if(BIGLYBTC_LOCAL_ENGINE AND NOT BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST STREQUAL "")
  file(SHA256 "${BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST}" _biglybtc_original_identity_manifest)
  if(_biglybtc_original_identity_manifest STREQUAL "68c25c3ac2680513deef488f7f74dc0c6788b1802df43361ef00886c962bde58")
    if(NOT TARGET biglybtc-runtime-interposition-driver OR NOT TARGET biglybtc-runtime-interposer
        OR NOT EXISTS "${BIGLYBTC_PERSISTENT_NEGATIVE_LT_LIBRARY}")
      message(FATAL_ERROR "Identity/tracker qualification requires all existing rejection fixtures")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME provider-original-identity-runtime-inputs COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/provider_original_identity_runtime_inputs_tests.py")
    set_tests_properties(provider-original-identity-runtime-inputs PROPERTIES TIMEOUT 180
      LABELS "unit;qualified-provider;source-binding" ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    add_executable(biglybtc-provider-original-identity-runtime-deployment-driver runtime_deployment_driver.cpp)
    target_link_libraries(biglybtc-provider-original-identity-runtime-deployment-driver PRIVATE
      biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar ${CMAKE_DL_LIBS})
    target_include_directories(biglybtc-provider-original-identity-runtime-deployment-driver PRIVATE
      "${PROJECT_SOURCE_DIR}/engine" "${PROJECT_BINARY_DIR}/engine/runtime-binding")
    target_compile_definitions(biglybtc-provider-original-identity-runtime-deployment-driver PRIVATE BIGLYBTC_ORIGINAL_IDENTITY_DEPLOYMENT=1)
    target_compile_options(biglybtc-provider-original-identity-runtime-deployment-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_options(biglybtc-provider-original-identity-runtime-deployment-driver PRIVATE
      "LINKER:--wrap=open" "LINKER:--wrap=fstat" "LINKER:--wrap=read" "LINKER:--wrap=__read_chk")
    add_test(NAME provider-original-identity-runtime-deployment COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/runtime_deployment_tests.py"
      --driver "$<TARGET_FILE:biglybtc-provider-original-identity-runtime-deployment-driver>"
      --provider "$<TARGET_FILE:LibtorrentRasterbar::torrent-rasterbar>" --manifest "${BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST}" --mode original-identity
      --alternate "${BIGLYBTC_PERSISTENT_NEGATIVE_LT_LIBRARY}" --interposition-driver "$<TARGET_FILE:biglybtc-runtime-interposition-driver>"
      --preload "${_biglybtc_runtime_preload}")
    set_tests_properties(provider-original-identity-runtime-deployment PROPERTIES TIMEOUT 180
      LABELS "unit;qualified-provider;runtime-binding" ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    add_executable(biglybtc-original-identity-interposition-driver original_identity_interposition_driver.cpp)
    target_link_libraries(biglybtc-original-identity-interposition-driver PRIVATE biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar ${CMAKE_DL_LIBS})
    target_include_directories(biglybtc-original-identity-interposition-driver PRIVATE "${PROJECT_SOURCE_DIR}/engine")
    target_compile_options(biglybtc-original-identity-interposition-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(_kind identity tracker)
      add_library(biglybtc-original-${_kind}-foreign-marker SHARED original_identity_foreign_marker.cpp)
      if(_kind STREQUAL "identity")
        target_compile_definitions(biglybtc-original-${_kind}-foreign-marker PRIVATE BIGLYBTC_FOREIGN_ORIGINAL_IDENTITY=1)
      endif()
      target_compile_options(biglybtc-original-${_kind}-foreign-marker PRIVATE -Wall -Wextra -Wpedantic -Werror)
      add_dependencies(biglybtc-original-identity-interposition-driver biglybtc-original-${_kind}-foreign-marker)
      set(_preload "$<TARGET_FILE:biglybtc-original-${_kind}-foreign-marker>")
      if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND _biglybtc_runtime_address_sanitized)
        if(NOT IS_ABSOLUTE "${_biglybtc_runtime_asan}" OR NOT EXISTS "${_biglybtc_runtime_asan}")
          message(FATAL_ERROR "Identity/tracker marker controls require existing ASan-first setup")
        endif()
        set(_preload "${_biglybtc_runtime_asan}:${_preload}")
      endif()
      add_test(NAME provider-original-identity-reject-foreign-${_kind} COMMAND "${CMAKE_COMMAND}" -E env
        --unset=LD_AUDIT "LD_PRELOAD=${_preload}" "$<TARGET_FILE:biglybtc-original-identity-interposition-driver>" "${_kind}")
      set_tests_properties(provider-original-identity-reject-foreign-${_kind} PROPERTIES TIMEOUT 15 LABELS "unit;qualified-provider;runtime-binding")
    endforeach()
  endif()
endif()
