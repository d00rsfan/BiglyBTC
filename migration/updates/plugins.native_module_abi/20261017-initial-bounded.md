# Initial bounded plugin SDK / module review

Self review, 2026-10-17; no independent reviewer. Original Java baseline
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; Location Provider
`8e8224030ac772fcc8fcb22e80954342d6a66be1`; SpeedScheduler
`1ee6a53dae017696d2f0002e4a0d74f5a9476af5`; mlDHT wrapper
`3b12515c5541ed75cfb4f795b672d4e786df9f45`.

Scope: independent build/ownership/lifecycle C boundary; bounded offline signed
native activation beside original JARs; actual location and scheduler module
behavior; internal mlDHT bind/effective-state policy. Full plugins/application,
automatic update transport, signing authority and release closure are excluded.

Final GCC16/Clang24/ASan+UBSan+LSan capsules are
`migration/evidence/P02-05-plugins/{gcc,clang,asan}.json`. Each records configure,
build, install/test commands/logs, all plugin source hashes and produced binaries.
Nine CTest rows per compiler cover2 SDK,1 location and6 scheduler suites; the
additional signed-store test runs actual child module probes, four process exits
at durable activation boundaries, retries/rollback, replay/wrong-ID/ABI/platform/
version/hash/signature rejection, lease exclusion and native uninstall preservation.
The original Java config/JAR selectors run before/after with the frozen core and
ReferenceFence. The retained core digest is checked before helper compilation.
`qualification-source-check.json` confirms current compiled/Python test inputs
match all three final capsules; later README/ignore/provenance edits are separate.

The first sanitizer run detected6956 bytes stranded by unloading ICU's locale
caches with the plugin. The replacement delegates display-name localization to a
host callback; ICU is linked into the test host for process lifetime and is not a
plugin runtime dependency. No LSan suppression, disabled leak detection, global
ICU cleanup or pin-all-modules workaround was introduced. Earlier failed runs
remain under `pre-locale-host`; an initial Clang harness failure after new update
fixtures were introduced remains under `pre-version-binding`. They are not passes.

Review findings retained as gates:

- A dlopen entry point executes native code before descriptor validation; only
  already-authenticated artifacts may reach it. This is not a code sandbox.
- Python store is an explicit generated-profile prototype with cooperative,
  owner-controlled directories. Production requires descriptor-relative traversal
  throughout and reuse of the application's existing held profile lease.
- No production signing root, catalogue, replay expiry, transport fallback or
  download has been enabled. Test private keys stay under generated `/tmp` roots.
- Original profile configuration/JAR/DB bytes/inodes and actual directory case
  survive. Disabled load-at-startup remains zero; explicit fixture install is not
  automatic enabling. Original uninstall/missing directory is not resurrected.
- Physical native uninstall deletes only signature-authenticated known files,
  preserving unknown neighboring files and Java artifacts. Tombstone/sequence and
  crash remnants stay available; general garbage collection is a later concern.
- Numeric original Java versions are supported; CVS/non-numeric versions,
  general Java properties grammar and original dependency/artifact closure need
  more coverage. Locale CLDR differences and absent native-only flag assets remain
  explicit location gaps. Location tag-example UI is not implemented.
- Scheduler returns commands, no real rate/tag/AutoSpeed/engine/file-writer actions.
  Its redistribution grant remains unresolved; implementation source is excluded
  from public exports. Host command acknowledgement is a separate integration gate.
- mlDHT's4860 observations use exact extracted method bodies with supplied host
  boundaries. No full wrapper, Java engine/cache compatibility or DHT socket
  operation is inferred. The native host policy has no network side effects.

Whole-plugin integrated/first-ported hashes remain null. Later provenance records
the actual immutable native implementation and clean public export commits rather
than inventing self-referential hashes. No feature family or phase is closed.
