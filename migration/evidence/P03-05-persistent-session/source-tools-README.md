# Persistent-session source tool relocation

This is source-tool integration only. Phase1 retains its original 14,068 frozen export/pause observations, 67 source exceptions and 17 actual helper classes. Its immutable main fixture manifest is `6e60e51825488fce6e5717e90a4414c7eec908e383665be7a5f8dd5b0ce72b33`. The relocated builder has not run; every future fresh phase1 build must produce all 17 identical class hashes from that actual frozen reference. It does not invent a new first-class proof. Existing expected outputs and reference/class bytes remain untouched.

Phase2 completion has an independently reviewed 221-scenario/2,280-command source plan, four exact original method bodies (two subjects plus two scalar helpers), 22 original source files and complete harness `e9dca56d934a945313012d426425fe97ff3af12b21f3cc352b529b987d6f3f47`. Its first compiler/JVM/source-freeze operation is still pending. No phase2 class hashes or expected outputs are claimed. Main raw shared sources are copied byte for byte from cb8c6cb9bb97916cbc7e80338a017200ffc3e71f; the historical source drafts, plans, previews and reviews remain separate unchanged evidence.

Main tools are `migration/tools/session_export_pause_{oracle,fixtures}.py` and `migration/tools/session_completion_{oracle,fixtures}.py`. Their shared `session_source_scan.py` is the unchanged historical scanner under a noncolliding import name. Java templates retain `ExportPauseOracle.java.in` and `CompletionOracle.java.in` under `migration/fixtures/java/`. Both builders derive ROOT from their main tool path. Defaults only inspect or validate immutable historical plans; they do not rewrite plan identities, previews or inspection records.

Both explicit builders use a fresh physical owned /tmp root required by the unchanged ReferenceFence, fresh helper-only source/class directories, JDK17 --release8, nice5, two JVM CPUs and bounded timeouts. Exact compile and runtime argv are verified, including isolated profile/home/root and no peer ports. All preparation, input snapshot, compiler and verification operations after owned-root allocation are inside the failure capsule. Complete sources/input copies/logs/partial classes/reference/status are copied and hash-verified to `build/recovery/generated-references/<owned-work-name>` on normal success or exception. No ordinary core, manager, profile, timer or networking classes are reused. Persistent-filesystem failure itself remains outside this guarantee.

Collectors have no implicit builder. They preserve their own preparation inputs and failure status under this evidence directory’s development folder, stream commands/stdout/stderr/progress before each source JVM, and preserve helper before/after/output copies. They refuse any existing output directory and never write a failure record into one they did not create. Their after-failure helper capture accepts only the exact physical owned /tmp prefix, including when reference verification fails. The phase1 freeze switch therefore refuses the already-frozen main fixture directory; do not rerun it to replace original observations. The phase2 strict predeclared fault validator is unchanged.

These commands are read-only Python inspection/planning and are safe before a compiler grant:

```sh
python3 -B migration/tools/session_export_pause_oracle.py
python3 -B migration/tools/session_export_pause_fixtures.py
python3 -B migration/tools/session_completion_oracle.py
python3 -B migration/tools/session_completion_fixtures.py
```

After root grants and serializes the source Java slots, each explicit builder prints its exact new reference.json path:

```sh
python3 -B migration/tools/session_export_pause_oracle.py --build
python3 -B migration/tools/session_completion_oracle.py --build
```

The phase1 build itself checks all 17 historical actual classes. Only phase2 needs a first source freeze. Replace the placeholder below with its newly built exact reference path; do not use the phase1 reference:

```sh
python3 -B migration/tools/session_completion_fixtures.py --freeze --reference /tmp/biglybtc-completion-oracle-OWNED_SUFFIX/reference.json
```

No Java/compiler/native process was launched during relocation. No engine implementation, safe native completion/persistence behavior, performance result or equality credit follows from these source tools. Existing task phases and integration/native hashes remain in force. Source-only context and stronger native storage-barrier questions remain in the copied historical README/questions/reviews.

Actual root execution now supersedes the historical relocation-only status:
phase1 rebuilt with all17 frozen classes identical; phase2 compiled22 actual
classes and froze all2,280 rows with strict predeclared fault validation. See
phase1-class-relocation-proof.json, phase2-first-reference.json and
phase2-source-freeze-verification.json. No native equality claim follows.
