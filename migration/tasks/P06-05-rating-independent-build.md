# P06-05 Independently buildable rating sources

Status: independent package qualified and sources published. Finished the missing build
dependency that has kept BiglyBTC-plugin-azrating README-only. Publish the
already qualified bounded data library with a pinned, independently installed
BiglyBTC core package. Keep the original GPL-2.0-only notice and COPYING file.

Allowed paths: native/core/CMakeLists.txt and package configuration/notices,
native/plugins/BiglyBTC-plugin-azrating/{CMakeLists.txt,fixture_test.py,README.md},
migration/publication/application, source export tool and task-local evidence.
No rating algorithm or original Java source changes are needed. The C ABI SDK's
default standalone build must continue to work; optional core builds use the
pinned GCC16/Clang24/libstdc++16 toolchains. No dependency is downloaded during
CMake configuration. Installation is into owned /tmp development prefixes.

Build core as a separate source project, install it, then configure azrating
with only that installed package visible through CMAKE_PREFIX_PATH. Relocate the
install prefix and remove access to the build tree to prove the package does not
retain checkout paths. Run 1819 original observations, bounds tests and 366
original-Java payload roundtrips across GCC/Clang/ASan+UBSan+LSan, serial two-job
builds. Check the SDK default build after changing its public root options.

Only after immutable local commits exist, export core/SDK and rating sources to
the already authorized public repositories. Verify every committed blob against
the export manifest, clean clones, unchanged remote heads and independent
builds. Keep CI recipes inactive in ci/ under the existing workflow-scope
constraint. Record all public/local source and artifact hashes. No binary or
catalogue release, active pipeline, runtime plugin registration, DDB publication
or complete rating plugin parity is qualified by these build tests.

Qualification passes GCC16, Clang24 and ASan/UBSan/LSan. Each consumer uses only
the relocated install prefix after its original prefix and core build path have
been moved away. All 1819 observations, bounds and366 original Java readbacks
pass per compiler. The normal application configure keeps BIGLYBTC_INSTALL_CORE
off and emits no core install commands, preserving the DEB contents. All source,
package and consumer hashes are in evidence/P06-05-rating-package/matrix.json.

Public core/SDK commit:499466856913d4d7d5e73d2a143c845864c7b44d.
Public azrating commit:940d3c15bfc2dc8085e6b544181e6f77a37854f8.
Local implementation commit:6b2c2d831cb3bedf4887f5f1493a29c97f264f85.
Every committed public blob matches its source manifest. Fresh exported core
build/install, both SDK contract tests and both rating tests pass independently.
All qualified input blobs match the immutable local commit. Publication and
independent build receipts are in the same evidence capsule. Continue P06-06.
