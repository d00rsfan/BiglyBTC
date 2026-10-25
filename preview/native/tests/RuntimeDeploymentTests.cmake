# Root includes after the existing RuntimeBindingTests.cmake. Existing cohorts
# and fixtures remain byte-exact; unbound legacy-fallback tests remain required.
if(NOT BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST STREQUAL "")
  file(SHA256 "${BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST}" _biglybtc_deployment_manifest)
  if(_biglybtc_deployment_manifest STREQUAL "010c6094b62e5e2e597ce6267b0125f6560d1a043a792218389ef4b962e871ae"
      OR _biglybtc_deployment_manifest STREQUAL "aa5f1767640fc1ca5f978360ddb8eebd26fc29b2f0c34b44fdf7416299a1bb3c")
    set(_biglybtc_deployment_mode source)
  elseif(_biglybtc_deployment_manifest STREQUAL "e74def09b2257a266786b8ddb116382d300716687bf26bbe57072f5ff90e8737")
    set(_biglybtc_deployment_mode selected)
  elseif(_biglybtc_deployment_manifest STREQUAL "28374ad5900f04c533a13afe90e81a7332f8a62b52db30adb0ed4dfe2fbce8ee"
      OR _biglybtc_deployment_manifest STREQUAL "37ce74d783fe42e42f09415ffd4358f330d5849803ef1064cb5abab7f89c5abc")
    set(_biglybtc_deployment_mode strict)
  elseif(_biglybtc_deployment_manifest STREQUAL "3342ec8ae72ecafa4a1b887e664d0d066fffce4f7d864b0b130ca903be953a3a")
    # Exact private next-provider fixtures are registered below by the separate
    # provider-next-runtime-deployment block, including all rejection controls.
    # Configure-time manifest/source/image verification has already succeeded.
    return()
  else()
    message(FATAL_ERROR "Runtime deployment fixtures require an explicitly reviewed provider cohort")
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_executable(biglybtc-runtime-deployment-driver runtime_deployment_driver.cpp)
  target_link_libraries(biglybtc-runtime-deployment-driver PRIVATE
    biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar ${CMAKE_DL_LIBS})
  target_include_directories(biglybtc-runtime-deployment-driver PRIVATE
    "${PROJECT_SOURCE_DIR}/engine" "${PROJECT_BINARY_DIR}/engine/runtime-binding")
  target_compile_options(biglybtc-runtime-deployment-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_link_options(biglybtc-runtime-deployment-driver PRIVATE
    "LINKER:--wrap=open" "LINKER:--wrap=fstat" "LINKER:--wrap=read")
  set(_biglybtc_deployment_controls)
  if(_biglybtc_deployment_mode STREQUAL "source" OR _biglybtc_deployment_mode STREQUAL "selected")
    if(NOT TARGET biglybtc-runtime-interposition-driver OR "${_biglybtc_runtime_preload}" STREQUAL "")
      message(FATAL_ERROR "Copied runtime qualification needs the unchanged preload/interposition controls")
    endif()
    list(APPEND _biglybtc_deployment_controls
      --alternate "${BIGLYBTC_PERSISTENT_NEGATIVE_LT_LIBRARY}"
      --interposition-driver "$<TARGET_FILE:biglybtc-runtime-interposition-driver>"
      --preload "${_biglybtc_runtime_preload}")
  endif()
  add_test(NAME persistent-runtime-deployment-${_biglybtc_deployment_mode}
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/runtime_deployment_tests.py"
      --driver "$<TARGET_FILE:biglybtc-runtime-deployment-driver>"
      --provider "$<TARGET_FILE:LibtorrentRasterbar::torrent-rasterbar>"
      --mode "${_biglybtc_deployment_mode}" ${_biglybtc_deployment_controls})
  set_tests_properties(persistent-runtime-deployment-${_biglybtc_deployment_mode} PROPERTIES
    TIMEOUT 180 LABELS "unit;runtime-binding;deployment")
endif()
