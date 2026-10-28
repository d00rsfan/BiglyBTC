# Independent rating/core package review

Core algorithms and rating data algorithms are unchanged. Core CMake can now
build/install as a standalone C++23 project, exporting BiglyBTC::Core through
BiglyBTCCore 0.1.0. Headers use separate build/install interfaces; Threads is found
through the package dependency helper. Static code is position independent and
not a stable public C++ ABI. Consumers must match the qualified compiler,
standard library and build/sanitizer options. No package download occurs during
configuration. Original core source baseline remains
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f.

The C ABI SDK root keeps its default build behavior; its optional core build
uses the pinned toolchain. Core development installation defaults on only for a
standalone core project. The existing application build defaults off, so static
archives/development headers do not enter the application's DEB automatically.
COPYING and a scoped NOTICE accompany installed headers/library.

The azrating standalone project finds the installed package instead of reaching
back into a native workspace. Portable fixture lookup finds fixtures in a public
export and falls back to workspace fixtures for monorepo builds. Data-derived
code retains GPL-2.0-only and original Aelitis notices; core is GPL-2.0-or-later.
Original plugin baseline remains1629340340d0bc391d9e582f9ee989a93d550581.

Across GCC16, Clang24 and ASan/UBSan/LSan, the install prefix and core build path
are moved away before configuring the consumer. Installed CMake files contain no
source/build/original-prefix paths and consumer includes point only at relocated
headers. Both rating CTests pass (1819 original observations plus bounds), and
366 native-produced payloads per compiler reopen identically in frozen Java.
Normal app configuration is checked for no development core installation.
Leak detection remains enabled; no global environment, live profile, Qt runtime
or Java application process was changed.

Publication must separately freeze actual local/public commit IDs, verify every
exported committed blob, rebuild the exported SDK/core/rating sources and retain
inactive CI recipes. No DDB transport, runtime module, settings/attributes/UI,
binary/catalogue release or full rating plugin parity is claimed. Review by the
implementing agent only; no independent reviewer was used.
