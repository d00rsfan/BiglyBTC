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


## 2026-11-06 durable intake history and classic library view

Native `7b3e55f9f8ea5cbd43791e11c1870890875d63a8`, tree `fa98e0b8247185e976cf6a9371abe673248ee4f2`, original core `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`, original I2P helper `33d97eca67a9f332e3d4da7e7492ec54f4bec078`. [Review](evidence/20261106-history-library-view/REVIEW.md) records32 affected coverage cells/compiler, actual receipt/Window cases and remaining lifecycle/private gates. No global integration baseline advanced.


## 2026-11-06 original I2P inbound runtime

Implementation `7b3e55f9f8ea5cbd43791e11c1870890875d63a8`, launch `a95bc831a1a5ec639194e79241c889eb10a3a9cf`, Native subtree `fa98e0b8247185e976cf6a9371abe673248ee4f2`; core `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`, helper `33d97eca67a9f332e3d4da7e7492ec54f4bec078`. [Review](evidence/20261106-original-i2p-inbound/REVIEW.md) proves the actual6/14 original-manager exchange, stop/resume/source revocation, joins/lock reacquire and isolated cleanup. Existing magnets retained, no new SAN inbound/expiry/EOF credit. Whole P07 and integration baselines remain open.

## 2026-11-06 receive-TTL expiry and owned-router EOF acceptance

Implementation `00e2e70a58bf0a0d73f548c0cf785779b5792f55`, Native subtree `d85be38ad6bea02eb010f1f9cf7f741ae9f7dc6a`: actual fresh GCC/SAN receive-expiry-only5/4 each and independent GCC owned-router EOF3/3 passed, with final joins/locks/cleanup/fences and independent review. [Evidence](evidence/20261106-i2p-receive-lifecycle/REVIEW.md). Counter0→1 is actual per-scope TTL removal while input remains queued; source/session/scope and fresh ping recovery stay current. EOF is exact owned router A SIGKILL/-9 and first armed TransportClosed, with source authority current and surviving subjects joined. Prior6/14 and magnet proofs are retained separately; failed global-empty attempt remains FAIL. Core `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`, helper `33d97eca67a9f332e3d4da7e7492ec54f4bec078`, official I2P `9134f808337b401e8e53c73734c81fab04280c9d`, router SHA256 `b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5`, SDK SHA256 `dfe077249c82e550cd0fd82cd7bfb14841a303bba17c90b74e7d9025b7ce14d3`. Whole P07, dependent overlay/P06 work, P08 and global baselines remain open. The prepared P08 candidate is not adopted here.

## 2026-11-06 supported removal tags and current-source history handoff

Implementation `b77eb40adf8ed44ea2caeacb561428eb08311f09`, Native subtree `04e4a8b61071bedf85e529d7cc390fd59833d2e5`, original core `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`: GCC/SAN nine affected suites plus application builds passed;21 critical actual Host rows and64 value cases per lane. Current-source original history ten-command handoff passed in 46.152s with real timer write, return/prefix checks, joins, private cleanup and digest guards. [Evidence](evidence/20261106-history-removal-tags/REVIEW.md). Selected original identity h/v/UID and full32 ambiguity checks, preissue optional capture warnings and postissue essential CAS refusal are bounded by the recorded limits. Prior JVM value proof `4318d91f40ca1947d94dadbf97e7a2a9dfc901579b44710bef9b2ed2315689ec` is carried, not rerun; separate public torrent API child is two javac attempts/one run. This supersedes the older statement that this P08 candidate was only prepared:22 merged leaves are adopted/qualified at this implementation. Full P08/global baselines/default distribution remain open; prior P07 provenance `15012726f798ea813053eed665c12693a795fa0b` is retained.

## 2026-11-06 bounded RSS document service

Implementation `1cf7cb5ae92c0098bab2476a313c352b13e022a9`, Native subtree `266cf68746e58b172c798b07e42fd663a09c60a3`, original core `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`: reusable owning RSS/Atom parse service integrated at seven leaves, actual three-TU front-end checks and one GCC/SAN CTest per lane passed (eight current Native families; two ELF images plus archive dependency). [Evidence](evidence/20261106-rss-document-service/REVIEW.md). The child withholds conditional magnet-derived hashes until link selection settles, preserving independent explicit hashes. Prior r2 actual20-command/nine-group RSSFeed/RSSItem/WebResult value proof remains carried parent-only, not rerun. No app, fetch/network/HEAD/cache/search/subscription/date interpretation/cancellation-provider lifecycle or whole P08/global/default-distribution claim follows. Continue the authorized whole plan with separately reviewed source-owned fetch/consumer architecture; earlier tag/history and P07 milestones remain retained.

## 2026-11-06 bounded HTTP response and saved RSS source

Implementation `4fa9715c0a6f94b3668d3079bc92cee06229787e`, Native subtree `af5efce8af90ede28ae968e975f0d4c8f9ac68c0`, original `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`: bounded pure HTTP framing plus real-lease saved RSS description integrated at11 leaves. Corrected GCC/SAN each passed two new CTests (seven framing families/seven reader methods) plus ten selected existing HTTP Qt rows. [Evidence](evidence/20261106-rss-response-and-source/REVIEW.md). Initial compile-only failures remain failed evidence. This grants no RSS GET/transport authority and advances no whole P08/P07/global integration baseline or default distribution; source-owned fetch lifecycle remains open.

## 2026-11-06 source-owned one-shot RSS provider

Implementation `f4bd9300c8df391750579e468a520ae379d13e0a`, Native subtree `9e63927d57255803de933211f5e3a0d633962544`, Original `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`: local-owned GCC/SAN one-shot provider accepted at 6 source leaves,13 Qt methods/36 test-data rows per lane and isolated production/fixture variants. [Evidence](evidence/20261106-source-owned-rss-provider/REVIEW.md). Earlier configure failures remain failed; final default-core acceptance is configure-only. Actual Host/UI consumer wiring, public-network qualification, whole P08/P07/global baselines and distribution promotion remain open.

## 2026-11-06 classic RSS Sources consumer

Implementation `2417a35d3863ef5a44b997a0efcf57f406ba9e5c`, Native subtree `519aacb4fb6ead6ca701e09439bc0ca191271f9c`, Original `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`: explicit classic RSS Sources/manual one-shot consumer accepted at17 leaves with local-owned GCC/SAN, five current Bridge rows plus five carried r1 UI rows per lane; original aggregate failures retained, isolated Host/provider variants and owning UI capture. [Evidence](evidence/20261106-classic-rss-consumer/REVIEW.md). App built only; no automatic fetch/subscription/import or whole P08/P07/global baseline/distribution completion. Remaining RSS semantics, search and subscription chapters continue.


## 2026-11-06 empty offline development launcher

Qualified consumer `2417a35d3863ef5a44b997a0efcf57f406ba9e5c` / Native subtree `519aacb4fb6ead6ca701e09439bc0ca191271f9c` has a separate local empty offline development launcher. Exact consumer/provider/Qt and default-preservation checks plus one owned offscreen startup/lock-release/temp-cleanup test passed. [Evidence](evidence/20261106-rss-offline-development-preview/REVIEW.md). Existing normal distribution and global integration baselines remain open. This metadata records local development access, not a production runtime promotion.

## 2026-11-06 Original Atom timestamp offset defect

Public Original `RSSUtils.parseAtomDate` on JDK17/core `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f` reproduced four offset mismatches among six fixed values. [Evidence and GB-English message](evidence/20261106-original-atom-date/REVIEW.md). This records a value-API defect, not UI/subscription-runtime reproduction. Future Native explicit-offset handling must preserve the represented instant; this is a planned corrected divergence, not yet a general date-codec parity qualification. No integration baseline advances.


## 2026-11-06 existing saved RSS subscriptions

Implementation `99f91cb3661544d47a3c9e79227315134b726bb8`, Native subtree `97f6c863cab284de2188d610f1f51d59cb744676`, Original `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`:37 leaves accepted by local-owned GCC/SAN (110.027s/258.572s). Each lane has25 fresh Qt cases and31 explicitly carried unchanged HTTP cases; previous r1/r2/r3 aggregates remain FAIL. [Bounded review](evidence/20261106-saved-rss-subscriptions/REVIEW.md) binds source-owned saved-subscription checks, durable read/delete/history, classic UI, cancellation/recovery/close/OFD release, actual fixture isolation and the selected layout-only PNG. Separate Original healthy public-codec/crypto/FileUtil handback passed six commands in3.723s against the same actual GCC adapter (result SHA256 `682e3dc8804923ec9cd3440a0ebbab40a407773178750629acee3995daadb516`); two signed ordinary/singleton bodies remain byte-exact, state/opaque values return through public APIs, real deletion-day bounds and owned cleanup pass. The strict-wrapper startup r1 remains FAIL. This grants no Original subscription-manager/history/scheduler/UI or crash-recovery runtime claim. Component states/source coverage/global integrated baselines remain unchanged. Whole P08/P07, unsupported metasearch/acquisition effects and default distribution promotion remain open.
