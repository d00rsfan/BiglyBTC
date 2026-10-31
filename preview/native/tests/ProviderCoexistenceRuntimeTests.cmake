# Included only for the exact optional coexistence manifest, after the unchanged
# bound-runtime rejection targets exist. This does not approve a new provider.
if(NOT BIGLYBTC_PERSISTENT_COEXISTENCE_RUNTIME OR NOT BIGLYBTC_PRIVATE_I2P_PROVIDER_OWNER
    OR NOT BIGLYBTC_PERSISTENT_COEXISTENCE_APPROVAL_SHA256 STREQUAL "3407d518e13a86f2a64b3d748cbe0870ef7df57712fade48cb72c87d2962ad75"
    OR NOT TARGET biglybtc-runtime-interposition-driver OR NOT TARGET biglybtc-runtime-interposer
    OR NOT EXISTS "${BIGLYBTC_PERSISTENT_NEGATIVE_LT_LIBRARY}")
  message(FATAL_ERROR "Coexistence deployment requires its separate role approval and every existing rejection fixture")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_target_property(_biglybtc_coexistence_includes LibtorrentRasterbar::torrent-rasterbar INTERFACE_INCLUDE_DIRECTORIES)
set(_biglybtc_coexistence_headers "")
foreach(_include IN LISTS _biglybtc_coexistence_includes)
  if(EXISTS "${_include}/libtorrent/version.hpp")
    if(NOT _biglybtc_coexistence_headers STREQUAL "")
      message(FATAL_ERROR "Coexistence fixtures require one installed provider header root")
    endif()
    set(_biglybtc_coexistence_headers "${_include}")
  endif()
endforeach()
if(_biglybtc_coexistence_headers STREQUAL "")
  message(FATAL_ERROR "Coexistence fixtures require the actual installed provider headers")
endif()
add_test(NAME provider-coexistence-runtime-inputs COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/provider_coexistence_runtime_inputs_tests.py"
  --manifest "${BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST}"
  --provider "$<TARGET_FILE:LibtorrentRasterbar::torrent-rasterbar>"
  --include-root "${_biglybtc_coexistence_headers}"
  --approval "${BIGLYBTC_PERSISTENT_COEXISTENCE_APPROVAL}"
  --approval-sha256 "${BIGLYBTC_PERSISTENT_COEXISTENCE_APPROVAL_SHA256}"
  --private-approval "${BIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL}")
set_tests_properties(provider-coexistence-runtime-inputs PROPERTIES TIMEOUT 180
  LABELS "unit;qualified-provider;source-binding" ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
add_executable(biglybtc-provider-coexistence-runtime-deployment-driver runtime_deployment_driver.cpp)
target_link_libraries(biglybtc-provider-coexistence-runtime-deployment-driver PRIVATE
  biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar ${CMAKE_DL_LIBS})
target_include_directories(biglybtc-provider-coexistence-runtime-deployment-driver PRIVATE
  "${PROJECT_SOURCE_DIR}/engine" "${PROJECT_BINARY_DIR}/engine/runtime-binding")
target_compile_definitions(biglybtc-provider-coexistence-runtime-deployment-driver PRIVATE BIGLYBTC_COEXISTENCE_DEPLOYMENT=1)
target_compile_options(biglybtc-provider-coexistence-runtime-deployment-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(biglybtc-provider-coexistence-runtime-deployment-driver PRIVATE
  "LINKER:--wrap=open" "LINKER:--wrap=fstat" "LINKER:--wrap=read" "LINKER:--wrap=__read_chk")
add_dependencies(biglybtc-provider-coexistence-runtime-deployment-driver
  biglybtc-runtime-interposition-driver biglybtc-runtime-interposer)
add_test(NAME provider-coexistence-runtime-deployment COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_CURRENT_SOURCE_DIR}/runtime_deployment_tests.py"
  --driver "$<TARGET_FILE:biglybtc-provider-coexistence-runtime-deployment-driver>"
  --provider "$<TARGET_FILE:LibtorrentRasterbar::torrent-rasterbar>"
  --manifest "${BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST}" --mode coexistence
  --alternate "${BIGLYBTC_PERSISTENT_NEGATIVE_LT_LIBRARY}"
  --interposition-driver "$<TARGET_FILE:biglybtc-runtime-interposition-driver>"
  --preload "${_biglybtc_runtime_preload}")
set_tests_properties(provider-coexistence-runtime-deployment PROPERTIES TIMEOUT 180
  LABELS "unit;qualified-provider;runtime-binding" ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
add_executable(biglybtc-coexistence-interposition-driver coexistence_runtime_interposition_driver.cpp)
target_link_libraries(biglybtc-coexistence-interposition-driver PRIVATE
  biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar ${CMAKE_DL_LIBS})
target_include_directories(biglybtc-coexistence-interposition-driver PRIVATE "${PROJECT_SOURCE_DIR}/engine")
target_compile_options(biglybtc-coexistence-interposition-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
foreach(_kind owner torrents)
  add_library(biglybtc-coexistence-${_kind}-foreign-marker SHARED coexistence_runtime_foreign_marker.cpp)
  if(_kind STREQUAL "torrents")
    target_compile_definitions(biglybtc-coexistence-${_kind}-foreign-marker PRIVATE BIGLYBTC_FOREIGN_I2P_TORRENTS=1)
  endif()
  target_compile_options(biglybtc-coexistence-${_kind}-foreign-marker PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_dependencies(biglybtc-coexistence-interposition-driver biglybtc-coexistence-${_kind}-foreign-marker)
  set(_preload "$<TARGET_FILE:biglybtc-coexistence-${_kind}-foreign-marker>")
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND _biglybtc_runtime_address_sanitized)
    if(NOT IS_ABSOLUTE "${_biglybtc_runtime_asan}" OR NOT EXISTS "${_biglybtc_runtime_asan}")
      message(FATAL_ERROR "Coexistence marker controls require the existing ASan-first setup")
    endif()
    set(_preload "${_biglybtc_runtime_asan}:${_preload}")
  endif()
  add_test(NAME provider-coexistence-reject-foreign-${_kind} COMMAND "${CMAKE_COMMAND}" -E env
    --unset=LD_AUDIT "LD_PRELOAD=${_preload}" "$<TARGET_FILE:biglybtc-coexistence-interposition-driver>" "${_kind}")
  set_tests_properties(provider-coexistence-reject-foreign-${_kind} PROPERTIES TIMEOUT 15
    LABELS "unit;qualified-provider;runtime-binding")
endforeach()
