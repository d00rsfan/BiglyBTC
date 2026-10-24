Source-only three-file amendment against native 49f02c870f70cb7a1aa42cf71c4e53a89eca07a0:

- managed_payload_counters.hpp retains the fixed scalar counter class byte-for-byte, uses libtorrent/fwd.hpp and declares attach only.
- New managed_payload_counters.cpp contains the identical callback/plugin/attach bodies, private LT headers and exact version/ABI gates. Only the two now-single definitions lose inline.
- Engine CMake compiles that CPP and appends -fno-sanitize=vptr to THIS SOURCE only for GNU/Clang/AppleClang. ASan, remaining UBSan checks, leak settings, flags/deadlines/assertions and every other translation unit's vptr checks remain unchanged.

Actual cause: chapter-sanitized-build.txt records persistent_session.cpp.o undefined references to hidden `typeinfo for libtorrent::torrent`. The private inline native-layout methods caused UBSan vptr instrumentation to require private RTTI that the already-approved shared provider does not export. This moves that ABI access out of callers and scopes the necessary exception to the one private shim. No LT source/build/artifact or runtime-approval hash changes; no program behavior, epoch arithmetic or callback lifetime change.

persistent_session.cpp and managed_download_tests.cpp are unchanged SHA guards in manifest.json. Static full-body reversal checks are recorded by prepare.py; no CMake/Cpp/test was executed. Root adopts staged files and resumes its existing caches. Do not rerun this authoring recipe after adoption.
