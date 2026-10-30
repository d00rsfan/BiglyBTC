# External fixture glue only. The reviewed provider/source/SDK are unchanged.
function(_biglybtc_private_owner_test_objects)
  if(NOT TARGET torrent-rasterbar OR NOT TARGET test_biglybtc_private_i2p_owner)
    message(FATAL_ERROR "Exact private-owner test/provider targets required")
  endif()
  if(NOT CMAKE_GENERATOR STREQUAL "Ninja" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "Reviewed Linux GCC/Ninja object layout required")
  endif()
  set(_i2p "${CMAKE_BINARY_DIR}/CMakeFiles/torrent-rasterbar.dir/src/i2p_stream.cpp.o")
  set(_proxy "${CMAKE_BINARY_DIR}/CMakeFiles/torrent-rasterbar.dir/src/proxy_base.cpp.o")
  file(SHA256 "${_i2p}" _i2p_sha)
  file(SHA256 "${_proxy}" _proxy_sha)
  if(NOT _i2p_sha STREQUAL "49352e2393199532205a20a03112a1b92b5483280e1d59c782e40b040ee6558a"
      OR NOT _proxy_sha STREQUAL "90250a39e6b8ace5ea1afb4604fec014fdda1a456491d410f63435f5b90ffa5d")
    message(FATAL_ERROR "Actual r1 hidden implementation objects changed")
  endif()
  set_source_files_properties("${_i2p}" "${_proxy}"
    TARGET_DIRECTORY test_biglybtc_private_i2p_owner
    PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
  target_sources(test_biglybtc_private_i2p_owner PRIVATE "${_i2p}" "${_proxy}")
  add_dependencies(test_biglybtc_private_i2p_owner torrent-rasterbar)
endfunction()

# CMAKE_PROJECT_INCLUDE runs before targets exist; defer to the root end.
cmake_language(DEFER CALL _biglybtc_private_owner_test_objects)
