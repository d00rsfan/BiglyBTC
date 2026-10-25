# Root merges this into the existing source-bound fixture registration.
# No automatic CTest: run.py coordinates original/native/original once.
if(BIGLYBTC_LOCAL_ENGINE AND BIGLYBTC_DESKTOP AND NOT BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST STREQUAL "")
  add_executable(biglybtc-storage-defaults-handoff-driver storage_defaults_handoff_driver.cpp)
  target_link_libraries(biglybtc-storage-defaults-handoff-driver PRIVATE
    biglybtc_local_engine biglybtc_profile LibtorrentRasterbar::torrent-rasterbar Qt6::Core)
  target_compile_options(biglybtc-storage-defaults-handoff-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
