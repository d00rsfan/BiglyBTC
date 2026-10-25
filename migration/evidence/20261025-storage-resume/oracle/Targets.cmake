# Root merges this fragment into its normal source-bound test registration.
# No CTest entry: run.py coordinates these actual original/native handoffs once.
if(BIGLYBTC_LOCAL_ENGINE AND BIGLYBTC_DESKTOP AND NOT BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST STREQUAL "")
  add_executable(biglybtc-storage-handoff-driver storage_handoff_driver.cpp)
  target_link_libraries(biglybtc-storage-handoff-driver PRIVATE
    biglybtc_local_engine biglybtc_profile LibtorrentRasterbar::torrent-rasterbar Qt6::Core)
  target_compile_options(biglybtc-storage-handoff-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
