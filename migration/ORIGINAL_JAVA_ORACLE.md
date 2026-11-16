# Original Java oracle

Use the immutable original baseline recorded for the component. The current
baseline is `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. A checkout's existing
`core/target/classes` is not proof of its source version: an audit on2026-11-16
found a missing pinned API and different bundled translations there.

Build a dedicated, persistent reference cache with JDK17:

```sh
python3 migration/tools/build_original_java_oracle.py \
  --source-repo /path/to/original/BiglyBT \
  --out /path/to/owned/original-core-oracle \
  --javac /path/to/jdk17/bin/javac
```

The tool reads the selected Git object, including its two bundled compile jars;
it does not run Maven or change the reference working tree. It applies the
original JDK9+/resource exclusions, compiles2680 Core Java files with source/
target8, and copies the pinned resources. A nonempty unmarked output directory
is refused. Keep the cache outside any client profile,source directory or daily
image. Do not rebuild it while reference helpers use it.

Use `reference-environment.json` for the classpath and keep
`oracle-manifest.json` with the proof: original commit,compiler,input/dependency
hashes and derived artifact hashes. Compile each helper against this classpath;
run it with a fresh generated `-Dazureus.config.path` and any required JDK module
opens. Inert source/API boundaries and unqualified service lifecycle remain
explicit. The oracle is a test artifact, not an original-client launch.

When replacing an old oracle, preserve accepted fixtures and compare separate
fresh outputs before changing them. Reuse matching behavior evidence rather
than blindly rerunning all Native tests. Differences require source review and
focused Native checks. Keep output/logs private; publish only synthetic fixtures,
relative source identifiers,hashes and qualification summaries.

The initial audit replayed18 accepted2026-11-16 original groups. Fifteen outputs
matched exactly. Three localized fixtures differed (category,state,stock tag
names); corresponding tables were regenerated. See
[evidence](evidence/20261116-original-java-oracle/REVIEW.md). This supplements the
prior bounded reviews; it does not attest every older Java-based fixture in the
migration. Older helper classpaths must be rebuilt/checked as their components
are revisited, before advancing a whole-component integration baseline.
