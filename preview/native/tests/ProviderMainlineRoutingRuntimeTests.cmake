# Additional exact identity/tracker lane. All prior registrations stay scoped.
if(BIGLYBTC_LOCAL_ENGINE AND NOT BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST STREQUAL "")
  file(SHA256 "${BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST}" _biglybtc_original_identity_manifest)
  if(_biglybtc_original_identity_manifest STREQUAL "f746fd279755ea90f31e20b0c9131c46e5f0871c4aab3665f232975f98feb56e")
    if(NOT TARGET biglybtc-runtime-interposition-driver OR NOT TARGET biglybtc-runtime-interposer
        OR NOT EXISTS "${BIGLYBTC_PERSISTENT_NEGATIVE_LT_LIBRARY}")
      message(FATAL_ERROR "Identity/tracker qualification requires all existing rejection fixtures")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME provider-mainline-routing-runtime-inputs COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/provider_mainline_routing_runtime_inputs_tests.py")
    set_tests_properties(provider-mainline-routing-runtime-inputs PROPERTIES TIMEOUT 180
      LABELS "unit;qualified-provider;source-binding" ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    add_executable(biglybtc-provider-mainline-routing-runtime-deployment-driver runtime_deployment_driver.cpp)
    target_link_libraries(biglybtc-provider-mainline-routing-runtime-deployment-driver PRIVATE
      biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar ${CMAKE_DL_LIBS})
    target_include_directories(biglybtc-provider-mainline-routing-runtime-deployment-driver PRIVATE
      "${PROJECT_SOURCE_DIR}/engine" "${PROJECT_BINARY_DIR}/engine/runtime-binding")
    target_compile_definitions(biglybtc-provider-mainline-routing-runtime-deployment-driver PRIVATE BIGLYBTC_MAINLINE_ROUTING_DEPLOYMENT=1)
    target_compile_options(biglybtc-provider-mainline-routing-runtime-deployment-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_options(biglybtc-provider-mainline-routing-runtime-deployment-driver PRIVATE
      "LINKER:--wrap=open" "LINKER:--wrap=fstat" "LINKER:--wrap=read" "LINKER:--wrap=__read_chk")
    add_test(NAME provider-mainline-routing-runtime-deployment COMMAND "${Python3_EXECUTABLE}" -B
      "${CMAKE_CURRENT_SOURCE_DIR}/runtime_deployment_tests.py"
      --driver "$<TARGET_FILE:biglybtc-provider-mainline-routing-runtime-deployment-driver>"
      --provider "$<TARGET_FILE:LibtorrentRasterbar::torrent-rasterbar>" --manifest "${BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST}" --mode mainline-routing
      --alternate "${BIGLYBTC_PERSISTENT_NEGATIVE_LT_LIBRARY}" --interposition-driver "$<TARGET_FILE:biglybtc-runtime-interposition-driver>"
      --preload "${_biglybtc_runtime_preload}")
    set_tests_properties(provider-mainline-routing-runtime-deployment PROPERTIES TIMEOUT 180
      LABELS "unit;qualified-provider;runtime-binding" ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    add_executable(biglybtc-mainline-routing-interposition-driver mainline_routing_interposition_driver.cpp)
    target_link_libraries(biglybtc-mainline-routing-interposition-driver PRIVATE biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar ${CMAKE_DL_LIBS})
    target_include_directories(biglybtc-mainline-routing-interposition-driver PRIVATE "${PROJECT_SOURCE_DIR}/engine")
    target_compile_options(biglybtc-mainline-routing-interposition-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(_kind identity tracker routing)
      add_library(biglybtc-routing-${_kind}-foreign-marker SHARED mainline_routing_foreign_marker.cpp)
      if(_kind STREQUAL "identity")
        target_compile_definitions(biglybtc-routing-${_kind}-foreign-marker PRIVATE BIGLYBTC_FOREIGN_ORIGINAL_IDENTITY=1)
      elseif(_kind STREQUAL "routing")
        target_compile_definitions(biglybtc-routing-${_kind}-foreign-marker PRIVATE BIGLYBTC_FOREIGN_MAINLINE_ROUTING=1)
      endif()
      target_compile_options(biglybtc-routing-${_kind}-foreign-marker PRIVATE -Wall -Wextra -Wpedantic -Werror)
      add_dependencies(biglybtc-mainline-routing-interposition-driver biglybtc-routing-${_kind}-foreign-marker)
      set(_preload "$<TARGET_FILE:biglybtc-routing-${_kind}-foreign-marker>")
      if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND _biglybtc_runtime_address_sanitized)
        if(NOT IS_ABSOLUTE "${_biglybtc_runtime_asan}" OR NOT EXISTS "${_biglybtc_runtime_asan}")
          message(FATAL_ERROR "Identity/tracker marker controls require existing ASan-first setup")
        endif()
        set(_preload "${_biglybtc_runtime_asan}:${_preload}")
      endif()
      add_test(NAME provider-mainline-routing-reject-foreign-${_kind} COMMAND "${CMAKE_COMMAND}" -E env
        --unset=LD_AUDIT "LD_PRELOAD=${_preload}" "$<TARGET_FILE:biglybtc-mainline-routing-interposition-driver>" "${_kind}")
      set_tests_properties(provider-mainline-routing-reject-foreign-${_kind} PROPERTIES TIMEOUT 15 LABELS "unit;qualified-provider;runtime-binding")
    endforeach()
  endif()
endif()
