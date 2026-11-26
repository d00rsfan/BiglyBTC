function(biglybtc_add_mainline_storage profile_target)
  set(sources "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
  add_library(biglybtc_mldht_cache STATIC "${sources}/mldht_cache.cpp")
  target_include_directories(biglybtc_mldht_cache PUBLIC "${sources}")
  target_link_libraries(biglybtc_mldht_cache PUBLIC BiglyBTC::Core)

  add_library(biglybtc_plugin_settings STATIC "${sources}/plugin_settings.cpp")
  target_include_directories(biglybtc_plugin_settings PUBLIC "${sources}")
  target_link_libraries(biglybtc_plugin_settings PUBLIC BiglyBTC::Core)

  add_library(biglybtc_plugin_keys STATIC "${sources}/plugin_keys.cpp")
  target_include_directories(biglybtc_plugin_keys PUBLIC "${sources}")
  target_link_libraries(biglybtc_plugin_keys PUBLIC BiglyBTC::Core)
  target_link_libraries(biglybtc_plugin_settings PUBLIC biglybtc_plugin_keys)

  # The native engine checkpoint is separate from the original plugin tables.
  add_library(biglybtc_mainline_checkpoint STATIC "${sources}/mainline_checkpoint.cpp")
  target_include_directories(biglybtc_mainline_checkpoint PUBLIC "${sources}")
  target_link_libraries(biglybtc_mainline_checkpoint PUBLIC ${profile_target}
    PRIVATE biglybtc_mldht_cache biglybtc_plugin_settings)
  if(WIN32)
    target_link_libraries(biglybtc_mainline_checkpoint PRIVATE ws2_32)
  endif()

  foreach(target IN ITEMS biglybtc_mldht_cache biglybtc_plugin_settings biglybtc_plugin_keys biglybtc_mainline_checkpoint)
    if(MSVC)
      target_compile_options(${target} PRIVATE /utf-8 /Zc:__cplusplus)
    else()
      target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
  endforeach()
endfunction()
