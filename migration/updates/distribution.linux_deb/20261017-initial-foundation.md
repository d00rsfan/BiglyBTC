# Linux delivery foundation: bounded initial review

Component `distribution.linux_deb`; state in_progress. Reviewer Codex, self review
only, 2026-10-17. Original BiglyBT repository
https://github.com/BiglySoftware/BiglyBT.git at
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. There is no prior integrated native
distribution baseline and no source update A-to-B claim.

Native packaging implementation:
`f6e0f2e9fd4ae43c4f375dcf7ec19c9f9a40d534`.
The earlier C++ runtime remains
`5073f29e81752be5b6cef9ceabddd9e919eaae72`; all 44 production core/adapter/
application/engine/UI/plugin C++ source/header files match that commit exactly.

Original updater/catalogue, profile location, plugin state/discovery and uninstall
were source-inspected. AD-006 records their relevant behavior. The new Linux
package format is a native delivery design; the original Java updater and its
runtime/plugin lifecycle have NOT been ported by this change. Java's whole-folder
plugin uninstall and independent native/Java versions remain explicit coexistence
gates. No running Java profile or payload was opened.

Implemented: opt-in DEB packaging, private Qt/ICU or future system-Qt mode,
distinct variant filenames with one application package identity, strict external
dependency calculation, payload/relative-path audits, private notices/SPDX/input
records, hosted packaging checks and a manual two-variant build/artifact workflow.

Evidence under `migration/evidence/P02-01-linux-deb`: six-method synthetic
packaging contract PASS, including unpackaged dependency rejection; real Qt6.12
fixture package/relocation/private loader/plugin/offscreen probe PASS; actual
application bundled configuration PASS. No package was installed and no remote CI
was run. The Qt fixture's size is not the complete client's package size. CPack's
automatic missing-info flag is explicitly removed by the dependency wrapper.

Original source, test/evidence paths and implementation hash are recorded in
`upstreams.lock.json`. Qt/ICU binary hashes identify the installed SDK observations;
original archive/source/build and complete redistribution closure remain pending.
No invented SDK source correspondence is asserted from its SPDX declarations.

Remaining: qualified runner, actual application package/runtime/install/upgrade/
rollback and desktop qualification, supported distro/ABI, matching Debian Qt
packages for the small variant, plugin ABI/verification/installation/rollback,
repository ownership and release trust. No first_ported_commit or
integrated_through_commit is advanced, and no feature/phase is marked verified.
