# Test-only same-build hidden implementation objects; never another DSO copy.
function(_biglybtc_private_owner_test_objects)
  if(NOT TARGET torrent-rasterbar OR NOT TARGET test_biglybtc_private_i2p_owner)
    message(FATAL_ERROR "Exact private-owner test/provider targets required")
  endif()
  if(NOT CMAKE_GENERATOR STREQUAL "Ninja" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "Reviewed Linux GCC/Ninja object layout required")
  endif()
  set(_i2p "${CMAKE_BINARY_DIR}/CMakeFiles/torrent-rasterbar.dir/src/i2p_stream.cpp.o")
  set(_proxy "${CMAKE_BINARY_DIR}/CMakeFiles/torrent-rasterbar.dir/src/proxy_base.cpp.o")
  # Fresh objects do not exist at configure. The runner measures both after
  # the successful build and requires those exact bytes through all tests.
  set_source_files_properties("${_i2p}" "${_proxy}"
    TARGET_DIRECTORY test_biglybtc_private_i2p_owner
    PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
  target_sources(test_biglybtc_private_i2p_owner PRIVATE "${_i2p}" "${_proxy}")
  add_dependencies(test_biglybtc_private_i2p_owner torrent-rasterbar)
endfunction()
cmake_language(DEFER CALL _biglybtc_private_owner_test_objects)
