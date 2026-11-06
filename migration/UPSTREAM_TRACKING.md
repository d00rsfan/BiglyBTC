# Upstream Commit Tracking

## Mandatory Rule

Every migrated behavior must be traceable to immutable upstream source and immutable native implementation commits. Record full Git object IDs, not branch names, release names, dates, or abbreviated hashes. Preserve the original baseline when later upstream updates are incorporated.

[upstreams.lock.json](upstreams.lock.json) is the machine-readable registry; [upstreams.schema.json](upstreams.schema.json) defines its structure. The registry currently contains inspected sources only. A repository may be partially ported across many phases, so integration coverage belongs to components, not a single “repository migrated” flag.

## Records and Their Meaning

### Source Registry

Each upstream entry records an ID, canonical URL, local checkout hint, Git object format, inspected commit/date, and whether the inspected working tree was dirty. Checkout paths are relative to the BiglyBT root and are convenience hints, not identity or permission grants. The commit's Git tree is the baseline; uncommitted files are not included by implication.

`audit_commit` identifies what was inspected. It never means that code has been ported, tested, or reviewed through the latest upstream HEAD. Updating a local checkout does not automatically update the registry or component baselines.

Track native dependencies separately in `external_dependencies`: exact version/source commit where available, artifact URL and SHA-256, build options/recipe, patches, and notice/license references. Resolve transitive dependencies and bundled JAR/native artifacts; their versions cannot be inferred from a plugin repository's commit alone. Empty dependency records at this planning stage mean the inventory is unfinished, not that the solution has no dependencies.

### Component Coverage

A component is a bounded behavior such as `scheduler.rules`, `scheduler.qt_editor`, `torrent.resume`, or `message_sync.public_transport`. Its record contains:

- `id`, feature IDs, state, task/evidence paths, and target paths.
- One or more `source_coverage` entries. Each identifies the upstream repository and relevant paths/symbols, the immutable `baseline_commit` chosen before implementation, and `first_ported_commit` once that coverage is verified.
- `integrated_through_commit`: the newest upstream state whose relevant behavior is fully accounted for by the verified implementation. A later reviewed commit may exist without advancing this value.
- `last_reviewed_commit`: the latest source snapshot explicitly examined for this coverage. It does not imply incorporation of every relevant change.
- `native_commits`: actual immutable implementation commits, including repository identity when the implementation spans repositories. Never insert the hash of a future commit or try to make a commit contain its own hash; a later provenance commit records the implementation commit.
- `review_records`: immutable-in-Git paths to the initial parity review and later upstream-update reviews. Each review describes fixtures, commands/results, known differences, and scope decisions.

If service and UI use different revisions, create separate coverage entries or components. If a feature uses several repositories, record each one. When replacing Java logic with a native library, record both the Java behavior baseline and the native dependency lock; “reused library” does not erase behavioral provenance.

State is `not_started`, `in_progress`, `implemented`, `verified`, `deferred`, or `retired`. Verified records require implementation commits and test evidence. Deferred/retired records require an explicit accepted decision reference. A partial component must be split or remain unverified; do not label a whole feature complete because its happy path works.

### Update Review Record

Store each review at a unique `migration/updates/<component>/<review-id>.md` path. A review records:

1. Component/feature scope and upstream repository URL.
2. Full old integrated hash A and candidate hash B, plus the comparison basis if history diverged.
3. Changed commits/files and classified effects: protocol, behavior, persistence, UI, performance, security, build/dependency, or out of scope.
4. A disposition for every relevant change: implemented, already satisfied by the native design, irrelevant with evidence, deferred with reason, or intentionally divergent with an accepted decision.
5. Native implementation commits, fixture changes, exact verification commands/results, and remaining work.
6. The resulting reviewed and integrated hashes, reviewer, and date. If a separate reviewer was unavailable, state that accurately.

Do not claim an integration baseline of B while a relevant change is deferred. Either keep A, choose a lower verified baseline with complete coverage, or split independent behavior into components with different baselines. A rejected upstream behavior needs an accepted divergence and tests defining the replacement behavior before B can be considered accounted for.

## Initial Port Workflow

1. Read the source registry, feature row, task card, and relevant architecture decisions. Check local repository status without modifying it.
2. Resolve the selected full commit with Git and inspect its committed tree. For a dirty source checkout, use a separate pristine worktree/clone or `git show`; do not restore another person's edits. Store any approved local source patch separately with a digest and clear provenance before using it as an oracle.
3. Create the component coverage record with the source `baseline_commit`, exact paths/symbols, and state `in_progress`. At this point `first_ported_commit` and `integrated_through_commit` stay null.
4. Characterize behavior with fixtures and implement the bounded native slice. Test against the pinned source/artifact, not whatever happens to be installed locally.
5. Once implementation commits and verification evidence exist, record `first_ported_commit`, `integrated_through_commit`, and `last_reviewed_commit` for each covered source, link the native commits and initial review, and mark the component verified. Uncommitted implementation remains `implemented`.
6. Update feature/task status and the integration handoff. Preserve the first-port baseline forever; update it only to correct a demonstrably erroneous record, with an explicit correction entry.

## Later Upstream Update Workflow

1. Fetch candidate source history without switching or merging the reference checkout. Network access and writes outside the workspace remain subject to the session's permissions. Deepen shallow clones when needed; a missing object is not “no changes.”
2. Select candidate B explicitly. Compare from the component's integrated A, not its audit hash or merely its last reviewed hash. Record both complete commit IDs.
3. Review commit messages and diffs, including surrounding dependencies, configuration/resources, renamed files, generated inputs, and protocol tests. A narrow path filter is a starting point, not proof that shared-service changes are irrelevant.
4. Build the disposition table and make task cards for relevant changes. Never cherry-pick Java source into C++ as a substitute for porting its behavior.
5. Implement/review/test each change and run affected cross-component regressions and performance workloads. Preserve the old native release and provenance for rollback.
6. Advance `last_reviewed_commit` after a completed review. Advance `integrated_through_commit` only after all relevant changes in that coverage are accounted for and verified. Append a review record; do not overwrite historical evidence.

Read-only comparison examples, after replacing the full-hash placeholders and choosing the correct repository:

```sh
git -C ../BiglyBT-plugin-SpeedScheduler rev-parse HEAD
git -C ../BiglyBT-plugin-SpeedScheduler status --short
git -C ../BiglyBT-plugin-SpeedScheduler cat-file -e '<A>^{commit}'
git -C ../BiglyBT-plugin-SpeedScheduler cat-file -e '<B>^{commit}'
git -C ../BiglyBT-plugin-SpeedScheduler merge-base --is-ancestor '<A>' '<B>'
git -C ../BiglyBT-plugin-SpeedScheduler log --reverse --format=fuller '<A>..<B>'
git -C ../BiglyBT-plugin-SpeedScheduler diff --find-renames --stat '<A>' '<B>'
git -C ../BiglyBT-plugin-SpeedScheduler diff --find-renames '<A>' '<B>' -- speedscheduler/
```

Angle-bracket values above are placeholders, not runnable hashes. For rewritten/divergent history, record the merge base and review both relevant histories and tree changes. Do not force-reset a clone or claim A→B ancestry that Git cannot establish.

## Required Validation

P00/P02 must automate these checks; JSON schema validation alone is not sufficient:

- Unique source/component IDs; resolvable references; full object IDs matching each repository's object format.
- Commits actually exist in the stated repository and can be retained/recovered; use archival mirrors/bundles for release baselines where necessary.
- No verified component without native commits, first-port/integration hashes, fixture/review evidence, and covered feature IDs.
- Relevant deferred changes do not advance the integrated baseline. Reviewed and integrated hashes may differ intentionally.
- Source paths/symbols exist at the recorded revision or have an explained rename/deletion. Dirty source overlays are explicitly accounted for.
- Native commits precede the provenance commit that records them; do not invent self-referential hashes. An uncommitted planning file is not an immutable release artifact.
- Replacement-library versions, build recipes, downloaded artifacts and local patches have independent provenance.
- Release manifests freeze a coherent set of component baselines, native commits, and dependency artifacts; per-component currency must not hide an incompatible combination.

One integrator owns the shared lock. Parallel workers propose task-local provenance changes; they do not race to rewrite global hashes. Preserve all prior integration review records in Git, including rejected/deferred update decisions and rollback history.

## 2026-10-31 bounded mixed Public and I2P one-owner integration

Native `90f1d3defd31acacb784cb8cda40d8d06ba6d76d` preserves one storage/checkpoint/queue owner while source-bound key0 and pure key1 identities remain separate. [Review](evidence/20261031-mixed-public-i2p/REVIEW.md) records full original/SDK hashes, measured qualification and unsupported discovery limits. Registry states/source coverage/null baselines remain unchanged; whole client/P07 in progress. STOP; next proposal only is I2P discovery/magnets beyond warmed-cache/tracker scope.


## 2026-11-05 held contacts and classic manual tags

Native `f859404f63394add41c898d40e486499d3a45178` (tree `342bb186f44e10531938c0f55ad403bdd35f6a86`) passed the same 32 affected GCC and ASan/UBSan suites. [Bounded review](evidence/20261105-held-contacts-manual-tags/REVIEW.md) records original core/helper identities, source and binary pins, worker ownership and remaining gates. Whole P07/P08 and registry integrated baselines remain in progress.


## 2026-11-06 protocol lifetime and library values

Native `3142bd167a1d139897e471c6aa488c97ea5b651c` (tree `f96e90bbcdeb0c628174091c66c9238e4f5700b0`) passed13 affected suites per compiler and four actual original-Java contact-file exchanges. [Review](evidence/20261106-protocol-library/REVIEW.md) separates measured protocol/value scope, original public-constraint API differences and required full-feature follow-ups. Whole P07/P08 and registry integrated baselines remain in progress.


## 2026-11-06 actual automatic-tag worker

Native `7ea75586f12a19365fa37a4a3a531d6569f43a36` (tree `5bdd5dfbc9ffe1c1e99be18146cd78f18bb0d953`) integrates source-bound supported constraints and guarded category storage. [Review](evidence/20261106-library-worker/REVIEW.md) distinguishes final GCC2/final sanitizer5 and retained GCC3 evidence. Complete P08/providers/actions and category live-owner/journal/UI integration remain open; registry integrated baselines are unchanged.


## 2026-11-06 classic category workflow

Native `a0b1debe1428c7c413a118c7e0fe2ef0aef2146d` (tree `21a7d021f33f33635523e94123171fc21e6d16ed`) integrates the worker/UI, guarded deletion and category-only owner state rebase. [Review](evidence/20261106-category-workflow/REVIEW.md) separates final GCC2/retained GCC10/final sanitizer12 and records limits/recovery/private-publication work. Whole P08 and registry integrated baselines remain open.


## 2026-11-06 library rules, recovery and identity bootstrap

Native `7335aba21d8d12c7dab9e2df04a51c4aa4673610`, tree `6b120fe2359d235a27f981775b454926078f0b51`, original core `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`, original I2P helper `33d97eca67a9f332e3d4da7e7492ec54f4bec078`. [Bounded review](evidence/20261106-library-policy-recovery/REVIEW.md) records24 passing coverage cells per compiler, unchanged-image retention, real Public omission drain and actual Java identity/category API checks. No full P07/P08 or global registry baseline advancement.


## 2026-11-06 original history public API handoff

Native helper `0c36fa9a1b170a812c2f2bebd5323aa219b37581`, tree `67fd16ff480ecfe0c4c3a8203a5ec97438d3cece`, original core `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. [Actual API/file/timer roundtrip](evidence/20261106-original-history-handoff/REVIEW.md) passed; lifecycle/GUI and whole P08 remain open.
