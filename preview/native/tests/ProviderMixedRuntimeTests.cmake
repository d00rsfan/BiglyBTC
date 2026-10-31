# Included only for the exact optional mixed manifest, after the unchanged
# bound-runtime rejection targets exist. This does not approve a new provider.
if(NOT BIGLYBTC_PERSISTENT_MIXED_I2P_RUNTIME OR NOT BIGLYBTC_PRIVATE_I2P_PROVIDER_OWNER
    OR NOT IS_ABSOLUTE "${BIGLYBTC_PERSISTENT_MIXED_I2P_APPROVAL}"
    OR NOT TARGET biglybtc-runtime-interposition-driver OR NOT TARGET biglybtc-runtime-interposer
    OR NOT EXISTS "${BIGLYBTC_PERSISTENT_NEGATIVE_LT_LIBRARY}")
  message(FATAL_ERROR "Mixed deployment requires its separate role approval and every existing rejection fixture")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_target_property(_biglybtc_mixed_includes LibtorrentRasterbar::torrent-rasterbar INTERFACE_INCLUDE_DIRECTORIES)
set(_biglybtc_mixed_headers "")
foreach(_include IN LISTS _biglybtc_mixed_includes)
  if(EXISTS "${_include}/libtorrent/version.hpp")
    if(NOT _biglybtc_mixed_headers STREQUAL "")
      message(FATAL_ERROR "Mixed fixtures require one installed provider header root")
    endif()
    set(_biglybtc_mixed_headers "${_include}")
  endif()
endforeach()
if(_biglybtc_mixed_headers STREQUAL "")
  message(FATAL_ERROR "Mixed fixtures require the actual installed provider headers")
endif()
add_test(NAME provider-mixed-runtime-inputs COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/provider_mixed_runtime_inputs_tests.py"
  --manifest "${BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST}"
  --provider "$<TARGET_FILE:LibtorrentRasterbar::torrent-rasterbar>"
  --include-root "${_biglybtc_mixed_headers}"
  --approval "${BIGLYBTC_PERSISTENT_MIXED_I2P_APPROVAL}"
  --approval-sha256 "${BIGLYBTC_PERSISTENT_MIXED_I2P_APPROVAL_SHA256}"
)
set_tests_properties(provider-mixed-runtime-inputs PROPERTIES TIMEOUT 180
  LABELS "unit;qualified-provider;source-binding" ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
add_executable(biglybtc-provider-mixed-runtime-deployment-driver runtime_deployment_driver.cpp)
target_link_libraries(biglybtc-provider-mixed-runtime-deployment-driver PRIVATE
  biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar ${CMAKE_DL_LIBS})
target_include_directories(biglybtc-provider-mixed-runtime-deployment-driver PRIVATE
  "${PROJECT_SOURCE_DIR}/engine" "${PROJECT_BINARY_DIR}/engine/runtime-binding")
target_compile_definitions(biglybtc-provider-mixed-runtime-deployment-driver PRIVATE BIGLYBTC_MIXED_DEPLOYMENT=1)
target_compile_options(biglybtc-provider-mixed-runtime-deployment-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(biglybtc-provider-mixed-runtime-deployment-driver PRIVATE
  "LINKER:--wrap=open" "LINKER:--wrap=fstat" "LINKER:--wrap=read" "LINKER:--wrap=__read_chk")
add_dependencies(biglybtc-provider-mixed-runtime-deployment-driver
  biglybtc-runtime-interposition-driver biglybtc-runtime-interposer)
add_test(NAME provider-mixed-runtime-deployment COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/runtime_deployment_tests.py"
  --driver "$<TARGET_FILE:biglybtc-provider-mixed-runtime-deployment-driver>"
  --provider "$<TARGET_FILE:LibtorrentRasterbar::torrent-rasterbar>"
  --manifest "${BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST}" --mode mixed
  --approval "${BIGLYBTC_PERSISTENT_MIXED_I2P_APPROVAL}" --approval-sha256 "${BIGLYBTC_PERSISTENT_MIXED_I2P_APPROVAL_SHA256}"
  --include-root "${_biglybtc_mixed_headers}"
  --alternate "${BIGLYBTC_PERSISTENT_NEGATIVE_LT_LIBRARY}"
  --interposition-driver "$<TARGET_FILE:biglybtc-runtime-interposition-driver>"
  --preload "${_biglybtc_runtime_preload}")
set_tests_properties(provider-mixed-runtime-deployment PROPERTIES TIMEOUT 180
  LABELS "unit;qualified-provider;runtime-binding" ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
add_executable(biglybtc-mixed-interposition-driver mixed_runtime_interposition_driver.cpp)
target_link_libraries(biglybtc-mixed-interposition-driver PRIVATE
  biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar ${CMAKE_DL_LIBS})
target_include_directories(biglybtc-mixed-interposition-driver PRIVATE "${PROJECT_SOURCE_DIR}/engine")
target_compile_options(biglybtc-mixed-interposition-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
foreach(_kind owner torrents mixed)
  add_library(biglybtc-mixed-${_kind}-foreign-marker SHARED mixed_runtime_foreign_marker.cpp)
  if(_kind STREQUAL "mixed")
    target_compile_definitions(biglybtc-mixed-${_kind}-foreign-marker PRIVATE BIGLYBTC_FOREIGN_I2P_MIXED=1)
  elseif(_kind STREQUAL "torrents")
    target_compile_definitions(biglybtc-mixed-${_kind}-foreign-marker PRIVATE BIGLYBTC_FOREIGN_I2P_TORRENTS=1)
  endif()
  target_compile_options(biglybtc-mixed-${_kind}-foreign-marker PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_dependencies(biglybtc-mixed-interposition-driver biglybtc-mixed-${_kind}-foreign-marker)
  set(_preload "$<TARGET_FILE:biglybtc-mixed-${_kind}-foreign-marker>")
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND _biglybtc_runtime_address_sanitized)
    if(NOT IS_ABSOLUTE "${_biglybtc_runtime_asan}" OR NOT EXISTS "${_biglybtc_runtime_asan}")
      message(FATAL_ERROR "Mixed marker controls require the existing ASan-first setup")
    endif()
    set(_preload "${_biglybtc_runtime_asan}:${_preload}")
  endif()
  add_test(NAME provider-mixed-reject-foreign-${_kind} COMMAND "${CMAKE_COMMAND}" -E env
    --unset=LD_AUDIT "LD_PRELOAD=${_preload}" "$<TARGET_FILE:biglybtc-mixed-interposition-driver>" "${_kind}")
  set_tests_properties(provider-mixed-reject-foreign-${_kind} PROPERTIES TIMEOUT 15
    LABELS "unit;qualified-provider;runtime-binding")
endforeach()
