# Original save-root/default/link handoff extension

Separate source-only capsule. It does not change the active `oracle/` runner,
helpers, manifests, historical logs or compiled original classes. Root owns
inspection, fixture compilation, native build and actual execution. Nothing here
has been run as Java/native or claimed as passing.

Inputs reuse the retained Java17 original class/resource cohort from pinned
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. Only three small fixture helpers are
compiled with `--release 8`, empty sourcepath and implicit compilation disabled:
unchanged ReferenceFence/EngineOracle plus new StorageDefaultsOracle. Original
root/default/link decisions are made by the actual retained manager classes;
no copied decision algorithm or numeric expected-result corpus supplies them.

The twelve literal input cases cover simple and multifile legacy `path`,
save_dir without save_file, existing-directory/file and nonexistent roots,
the same-name multifile ambiguity, absent settings, missing/empty stores,
configured R and saved reordermb overriding global policy, and actual external
filelinks2 rows with signed−1 and INT_MIN indices. Data is bounded to original
v1 128 KiB single/three-file fixtures. Suffix renaming is disabled explicitly
within this family; its independent existing qualification remains intact.

For each case:

1. Actual original classes create metadata and a stopped profile. A separate
   raw copy retains that original export. The runner authors only the named
   legacy/default/link inputs, preserving hashed info bytes. Generated stale
   cache overlays are removed so they cannot mask the authored primary state.
2. Actual original load-only probe reports roots, type getters and actual links.
   Payload bytes and directory existence must remain unchanged. The complete
   original probe export is retained; exact authored input bytes are restored
   for the native phase so Java's own save cannot do the migration for native.
3. Actual native PersistentSession downloads from an explicitly owned loopback
   seed, stops, checkpoints and joins before original Java opens the profile.
   The thin driver is the existing storage-handoff driver with only a new name,
   complete-only admission and support for one or three files.
4. Original reload must reach READY, verify all eight pieces and read every
   logical byte through its actual file API. Roots/paths, source policy getters,
   cumulative counters, native config immutability and opaque-value retention
   are checked. Reordered physical-byte/map equality is never asserted.

Inspect/adopt only the NEW driver source at
`preview/native/tests/storage_defaults_handoff_driver.cpp` and `Targets.cmake`
registration. Do not replace the previous storage driver. After the common
source-qualified GCC cache builds that target, root can run:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 /home/yurii/Java/BiglyBT/build/storage-20261025/oracle-next/run.py \
  --reference-root /home/yurii/Java/BiglyBT \
  --driver /ABSOLUTE/APPROVED/BUILD/tests/biglybtc-storage-defaults-handoff-driver \
  --work /tmp/biglybtc-managed-handoff-defaults-r1
```

Use a fresh work path for each attempt. `--case NAME` is repeatable for focused
diagnosis. The runner requires its own frozen manifest, complete retained class
map/receipt and source/helper pins before any work; it records command argv,
binary digest, raw stdout/stderr, snapshots and per-case actual receipts. Long
children emit a 40-second heartbeat and are killed/joined on a deadline failure.
No public peer/bootstrap endpoint, original profile/process, original rebuild
or physical crash-certification credit is granted.

This extension does not newly cover malformed settings, symbolic paths,
ambiguous control families, 10 MiB threshold-boundary files or C/R cross-family
failure vectors. Focused native tests cover their authored rejection/projection
cases; existing actual format/managed C/R/X receipts remain separate. All runtime
counts and PASS status must come from root's actual output, never this plan.
