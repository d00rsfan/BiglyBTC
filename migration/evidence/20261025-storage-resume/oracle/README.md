Root runs this once after adopting the mapped backend and the two native drivers.
The scripts remain source-only until those actual commands succeed.

```sh
python3 -B build/storage-20261025/oracle/run.py \
  --reference-root /home/yurii/Java/BiglyBT \
  --codec-driver /ABS/TO/biglybtc-original-file-io-driver \
  --managed-driver /ABS/TO/biglybtc-storage-handoff-driver \
  --work /tmp/biglybtc-managed-handoff-storage-UNIQUE
```

Use `--phase codec` or `--phase managed` only to separate batches; the result
records which phase actually ran. No omitted phase receives qualification credit.
The five authored/reused fixture helper classes are compiled together against
the retained5432 Java17 classes, under the existing ReferenceFence JVM recipe.
No original core/project rebuild. Runtime classes remain `/tmp` for that guard;
their full map must match the durable retention receipt before and after.

`tests/storage_handoff_driver.cpp` clones the existing qualified driver (before
image retained), changing only the real partial seed hole to piece5 and selecting
the three files at normal priority using the existing held-profile command before
admission. It uses the actual source-qualified runtime and inherited finite
deadlines. Root's ManagedStorage preparation must perform the explicit original
Compact→Linear / ReorderCompact→Reorder transition after the policy change.
`Targets.cmake` gives its exact target/link requirements. Root adopts the new test
source and registers the target; this capsule never edits shared CMake itself.

Raw driver CLI is `original_file_io_driver ROOT ROOT/request.tsv`, as agreed with
the codec owner. READ writes `read-LABEL.bin` and `read-LABEL.json` with absolute
logical missing ranges. Native output need not use original physical slot order
or unsafe swap normalization: cross-reader logical correctness and missing-data
handling are checked, with maps/physical products retained as actual observations.

Outputs: exact structured commands, stdout/stderr, helper class hashes, original
generated payload/control/config leaves, actual native receipts and final
`result.json` under the fresh private work directory. Failed output stays there.
Do not check large unchanged Java classes/binaries/build trees into Git.
