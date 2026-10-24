Adopt the four new `staging/preview/native` files after source review. Existing
native sources are unchanged and all four destinations were absent at freeze.
No new engine session/runtime approval is needed: only public LT metadata/hash
APIs are used, with exact2.0.12 compile pin and the existing qualified dependency.

Proposed CMake additions (root owns actual CMake):

```cmake
# engine/CMakeLists.txt
# Existing target already links the exact qualified LT dependency.
target_sources(biglybtc_local_engine PRIVATE torrent_creator.cpp)

# tests/CMakeLists.txt
add_executable(biglybtc-torrent-creator-tests torrent_creator_tests.cpp)
target_link_libraries(biglybtc-torrent-creator-tests PRIVATE
  biglybtc_local_engine LibtorrentRasterbar::torrent-rasterbar)
target_compile_options(biglybtc-torrent-creator-tests PRIVATE
  -Wall -Wextra -Wpedantic -Werror)
target_link_options(biglybtc-torrent-creator-tests PRIVATE "LINKER:--wrap=fsync")
add_test(NAME torrent-creator COMMAND biglybtc-torrent-creator-tests)
set_tests_properties(torrent-creator PROPERTIES TIMEOUT 120
  LABELS "unit;engine;torrent")

# Optional once-per-chapter original parser handoff driver, not every compiler.
add_executable(biglybtc-torrent-creator-driver torrent_creator_driver.cpp)
target_link_libraries(biglybtc-torrent-creator-driver PRIVATE biglybtc_local_engine)
target_compile_options(biglybtc-torrent-creator-driver PRIVATE
  -Wall -Wextra -Wpedantic -Werror)
```

Run the plain test in root's existing consolidated GCC batch. Then, once, run:

```sh
python3 build/fileops-20261024/creator/java/run.py \
  --driver /absolute/path/to/biglybtc-torrent-creator-driver \
  --output /home/yurii/Java/BiglyBT/build/fileops-20261024/creator/handoff-r1
```

The recipe invokes only two-helper javac, actual original parser and native
creator driver; it never rebuilds original core. It retains all logs/generated
files even on failure. This is decoder interoperability, not full v1/v2 or
transfer parity. No run has been performed by this worker.

UI must expose Auto plus exactly `CreatePieceSizes`, marshal callback values,
and treat final non-Durable publication as an already-created file with an IO
warning. No auto-import, auto-start, overwrite or tracker activity is implied.
