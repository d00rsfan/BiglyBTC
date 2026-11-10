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
5. Implement/review/test each change and run affected cross-component regressions and performance workloads. Retain source, provenance and concise evidence. During migration keep the current working Native build and remove superseded binary caches; do not retain rollback application copies. Keep only the minimal negative-control inputs required by active tests.
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


## 2026-11-06 numeric filters for existing saved RSS subscriptions

Implementation `284cda11df71333d1f47724ffad45abc576ebb42`, Native subtree `611e08879c3af4229f07e48766a6cf9355ebb243`, Original `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`:12leaves accepted by one source-coherent GCC/SAN batch (550.306s/646.068s),80fresh Qt cases/23methods plus50explicit Original-value comparisons and8Malformed refusals per lane. [Bounded review](evidence/20261106-subscription-numeric-filter/REVIEW.md) binds actual saved-definition numeric filtering before history,source/body/bookkeeping fences,fixture isolation and clean sanitizer results. Original public58-value evidence (one javac/one JVM,1.753s) is retained without rerun; malformed/partial observations remain outside supported parity. Phase-specific getter projection,Original manager/history/UI runtime,acquisition/intake and wider filters/metasearch remain unproved. No component state,source coverage,global integrated baseline,whole P08/P07 or default-distribution advancement. Earlier chapter evidence/failures remain retained.


## 2026-11-06 explicit stopped subscription torrent acquisition

Implementation `ee20f6fa37b8cd228474f0acc560f6c3aba4a496`, native subtree `aa11e703dbc0aab6af3dddf96bb842a61bcb4fa9`, Original `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`:54 native leaves with source-owned metainfo, private stopped Public pure-v1 intake and recoverable subscription association/read. [Bounded review](evidence/20261106-subscription-stopped-acquisition/REVIEW.md) records actual GCC/SAN138Qt rows/52methods,55config/state/intake guards and8HTTP families per lane;10owned child crashes at9checkpoints plus conflict, second-startup stability, joins/locks/cleanup and layout-only PNG. Actual r2 builds passed; r2 report-marker FAILs and r1 compiler FAILs remain. R3 used exact unchanged source/images/dependencies, no-work builds and corrected report marker. Original value observations support source characterization only; no manager/scheduler/full-switching claim. Download-side SID callback, existing-target handling, automatic acquisition and wider preparation remain open. Component states, source coverage and global integrated baselines are unchanged; whole P08/P07/default distribution remain open.


## 2026-11-06 invalid-lease intake contract correction

Implementation `c34d691a7adc0386e4a653a3eb0a01e4a47e09ea`, native subtree `617b39df4457e57e662a6c1f8788f3e51fc8c37e`, Original `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`: a one-line lease-first check restores UnsafePath before pending-intent filesystem probes. [Evidence](evidence/20261106-intake-invalid-lease/REVIEW.md) preserves hosted241/242 failure and local reproduction; both existing GCC/SAN lanes pass full14-family/632-assertion ordinary intake plus8pending guards, one executable/four archives/three commands each (43.341s/77.962s). Source/image/Original/SDK closure independently reviewed. No other green suites, real profiles, whole-component or global baseline advancement. Hosted rerun follows publication; default development image is not promoted by this targeted test build.

## 2026-11-07 stopped subscription existing-target and SID state (independent review pending)

Implementation `b0493c710397236a0c915ed3f680b24d33719f13`, Native subtree `51bd2d26d3fd7e9b68fe127e3f3e4fb71e96577d`, Original `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`: explicit fresh/existing stopped Public pure-v1 acquisition computes raw64 cache and conditional download-side SID through private preserved-backup B/S0/S1 CAS and V2 recovery. [Bounded review](evidence/20261107-subscription-existing-stopped/REVIEW.md) binds both actual R5 lanes (42 commands, 159 Qt rows/54 methods, 24 owned process rows per lane; GCC 249.226 s/SAN 377.984 s) and the once-only current GCC legacy upgrade (2 preview startups, 0 factories, 14 files pinned before consumption, owned cleanup). Final independent review passed and rehashed the current source, images, dependencies, command logs and legacy result. Original getter/value and source evidence do not qualify real download-state handback or full manager/Scheduler parity. Prior failed aggregates and actual 127.0.0.1:6881 EADDRINUSE remain recorded; fixtures use isolated ports without changing production guards. Component states, source coverage and integrated baselines remain unchanged; whole P08/P07 and default distribution remain open.


## 2026-11-07 — Saved-subscription literal word filters

Implementation `c2d089eda4df59adeb86d8b5407429bf9f0672be`, Native subtree `f0301451eb0aa7ef5af891ffa41aec10cfa64430`, Original `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`: eleven native leaves apply supported ASCII include/exclude words to scan and saved-result selection before persisted history effects. [Evidence](evidence/20261107-subscription-literal-filters/REVIEW.md) records both actual GCC/SAN lanes (68 Qt rows/22 methods,15 commands; 230.827 s/567.893 s), six executables/15 archives and independent source/link/image checks. Original public value API20 cases passed in one javac/JVM with zero mismatches; earlier numeric and stopped-target recovery evidence is retained without replay. Unsupported regex/active Unicode/category/tag syntax refuses explicitly. Full P08, global baselines and default distribution remain open.


## 2026-11-07 — Real public Java download-state handback

Implementation `af45fbab82d3e4f7e3c438ae06aea4e5d0e2bf2f`, Native subtree `652fb06a7908f99aa528cb9bea09c80093d3c2af`, Original `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`: one test translation unit exports a genuinely created stopped pure-v1 state, checks Original public getters/map mutation/save, and reads it back with Native TorrentStateStore. [Evidence](evidence/20261107-public-download-state-handback/REVIEW.md) binds GCC/SAN normal fixture checks (six commands and one Qt row each; 44.983s/88.394s), the separate four-command GCC/Java handback (9.269s), exact source/image/link closure, actual 984-byte returned state and private cleanup. SID/cache/resume, raw info and previous-primary backup are checked. Prior managerless scrape-setter and dictionary-order failures remain preserved. This does not qualify a full DownloadManager, SubscriptionManager callback, scheduler or arbitrary-profile switching. Production code/application image and global/component baselines are unchanged.


## 2026-11-07 — Automatic saved-RSS acquisition and queue recovery

Implementation `2ac30ffbe665ff1b628ea7d044168b34f73690d8`, Native subtree `a017f8ac1a03b791746fbcb42c2ca7ac7304a0dd`, Original `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`: 38 native source leaves connect committed RSS events to a bounded worker queue, typed current-source metainfo acquisition, actual save/torrent-copy policy and durable queued or paused/stopped admission. [Evidence](evidence/20261107-automatic-rss-acquisition/REVIEW.md) records independent GCC/SAN acceptance: 44 fresh recovery rows per lane plus111 prior rows carried only with exact executable/archive/source closure, ordinary intake14/632 assertions and pending8, isolated core-only configuration, production/fixture separation, and one genuine old-V2 upgrade (14 files, two preview startups, zero providers or duplicate import/SID/history). Failed parent runs remain failed evidence. Owned inactive test leftovers were removed. Supported routes and explicit limits are recorded; this is not whole P08, arbitrary-profile or full Java-manager parity. The current automatic RSS chapter is complete within that scope. Next proposed chapter: metasearch and its classic results UI; not started. Stop after publication.


### 2026-11-07 — saved metasearch and classic results

Implementation `63376b561829d8205e540df41cc89ce06ae7cb7a`, Native subtree `8bf077b64fee57396e5098f5bf1eb2daa2bac138`, Original `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`:44 native leaves add actual saved RSS/plain-JSON search, classic results, retained directHTTP(S) stopped review and cancellation/lifetime fences. [Evidence](evidence/20261107-metasearch-classic-search/REVIEW.md) binds local GCC/SAN129Qtrows47methods each, actual18 Original comparisons, exact Java first-GET spelling, production/fixture role separation, owned cleanup and preserved failure lineage. Regex/advanced templates and source-bound magnet admission remain explicit; no whole P08, Original-manager, global-baseline or default-distribution advancement. Next chapter proposed only; not started. Stop after publication.


### 2026-11-07 — advanced search and reviewed magnet admission

Implementation `f458468ba86d30efa08786358a7f7ccd29e1c52b`, Native subtree `839e8231c0963c066298c7352772ea37446abd6f`, Original `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`:43 native leaves add bounded saved Regex/ordinary JSON mappings and actual source-bound Public magnet review, existing-format pending persistence and stopped payload admission. [Evidence](evidence/20261107-advanced-metasearch-magnet/REVIEW.md) binds local GCC/SAN 148 Qt rows/46 methods per lane plus legacy magnet-intake, actual26 Original observations with explicit corrections, bounded Qt probe, production/fixture separation and owned cleanup. Full manager/advanced ecosystem/whole P08/global baseline/default distribution claims remain open. Next proposed sharing/embedded-tracker chapter is not started; stop after publication.


### 2026-11-07 — persistent sharing and embedded HTTP tracker

Implementation `a291a4e8e1ceaadb8e0f31d52c57fc10e9c49619`, Native subtree `7a8de0d0e730c28d6166adf8251fb1620dfd6948`, Original `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`:46 leaves add actual persistent Public v1 sharing/cache/recovery, full verified seeding and selected post-host SDK announcement, known-only HTTP tracker and classic controls. [Evidence](evidence/20261107-owned-sharing-http-tracker/REVIEW.md) binds GCC r1/SAN r2 each95Qtrows28methods, real tracker-only file/directory transfer and Source/Controller/retry/stop/reopen tests. Actual16 Original fingerprint values, independent image/source/role audit, bounded layout review, default configure-only, scoped cleanup and separately resealed preview are retained. Earlier failures/SAN interruption remain failed; no Java-manager/global-P08/default-distribution claim. Next proposed existing-sharing compatibility is not started; stop.


### 2026-11-07 — existing-library daily use

`da80d02f9bd90004203f1f08e41c114ecb6ad582` / Native `2a78e756136f87b1688c90fbc90133ea4194ec07` / Original `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`: four requested priorities accepted within [explicit scope](evidence/20261107-existing-library-daily-use/REVIEW.md). GCC29 fresh+3 retained-origin Qt rows, SAN32 fresh, IO9, console37/61; historical failures preserved. Private metadata preflight is separate from generated runtime acceptance. No real-profile payload, global baseline or full-component completion claim. Stop; normal-use feedback/config compatibility is proposed, not started.


## 2026-11-07 existing-library display and checking progress

Original cb8c6cb9bb97916cbc7e80338a017200ffc3e71f, implementation c066523d0dfa75c3bb58b253ebc842df5a376b48, Native subtree 8515c321a7103e5013a53ddc81601dbe1be8b28c: both GCC/SAN lanes passed 10 classic suites and 18 Qt rows. [Bounded review](evidence/20261107-existing-library-live-fixes/REVIEW.md) records Order/Save Path/Tracker Name import, selected completion during checking, actual checking percentage and one cache per scan. Original-resume fast startup, configured per-filesystem checks, Waiting/startup and large-library discovery fairness remain open. No global/component integration baseline or chapter completion is advanced.


## 2026-11-07 original resume and checking policy, scoped SAN result

Original cb8c6cb9bb97916cbc7e80338a017200ffc3e71f, implementation 276d901c4cd353a35ecb21623d6be43e66c2778b, Native subtree bf94ddda0586c937cdd859e84b365764a996e7af. [Scoped review](evidence/20261107-existing-library-resume-checks/REVIEW.md) records imported primary v1 resume, checked partial-map retirement and configured checking limits, with18 actual-owner SAN rows and pure policy cases. GCC application/real-profile integration remains pending at the user's request to keep the old preview running. No component verified state, global integration baseline, launcher replacement or chapter completion is advanced.


## 2026-11-08 existing-library critical fixes

Original cb8c6cb9bb97916cbc7e80338a017200ffc3e71f, final implementation 6181803356a8bf208bd8cb4602ec0273af04f8dc, Native subtree ee6d4765f61ea70bea8cc7432d04a5ef5dfad047: [scoped review](evidence/20261108-existing-library-critical-fixes/REVIEW.md) binds final normal regressions, full-SAN main-package predecessor 18fcc1d6b77ed8d2c6b0ffde826bc070312a7305, final focused sanitizer checks and fresh8-case credential/DHT packet capture. GUI details/units/sort, primary-v1 resume/check limits and large-library dispatch are covered within explicit limits. Failed attempts remain failed; retained tests require unchanged executable bytes. The normal development launcher is refreshed without stopping the old preview; the historical san cache now holds Release/O2 with scoped local GCC16 template diagnostics. No global baseline or chapter completion is advanced. Stop after publication.

### 2026-11-08 — existing-library resume, complete configured-column audit and exit

Native implementation `69a401db0e6c261672ab64cac230c0f04774c8b4`, subtree `8360cb793fb26d5d6ec9542b23d6b08d73901516`,
Original `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`: scoped normal GCC16/Qt6.12 qualification passes118Qt data
cases and29private-codec cases (498assertions). Valid primary linear V1/selected
hybrid resume now avoids forced mapped-disk hashing; changed/invalid states and
explicit Recheck retain hashing. The33-column catalogue, independent original
pane layouts and modal close cancellation are covered. See
[evidence](evidence/20261108-existing-library-resume-columns/REVIEW.md).
No whole-component baseline, live-profile runtime or fresh full-SAN claim. CPU
tuning is deferred; next distributed chapter remains unstarted. Merging is now
immediately before the final embedded browser/media-player work.

## 2026-11-09 chat foundation and resume retirement repair

Original core baseline remains `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`;
azmsgsync baseline remains `4209aa5029e3035cbb09dc3a83e9491d3f36fbc6`.
Native foundation `2dc70bab91d7d5294adae2edd7c207861adee163` and signing support
`f7cac97d1c396f904698f456d4fc8694da72a170` are scoped foundations; operational Public rooms
remain in progress. See [foundation evidence](evidence/20261109-general-chat-foundation/REVIEW.md).

Resume-retirement implementation `01f8618a0d7fbf1bd11dfde536fc424db13d84ea`, native subtree
`85a51cf2b16d46ac1856b26e75b09785e3659326`, preserves actual DONE pieces and proof-bound unchanged input maps.
[Qualification and remaining acceptance](evidence/20261109-resume-retirement/REVIEW.md)
record final native storage/retirement checks and original Java recognition of
an actual generated Native checkpoint. This does not complete all resume
layouts or close the fresh real-profile unwanted-check investigation.

## 2026-11-10 chat shared-UDP boundary

Implementation `4ff478031095830fcfb7531dd6bc0f21b30906af`, provider-overlay tree `f638d44ec8f30f47a3bfdfa96dd91f51aa3ef338`; original core
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`, azmsgsync
`4209aa5029e3035cbb09dc3a83e9491d3f36fbc6`, SDK upstream
`740a0b9aeabe00e762cc0efe4a0f27593db2550b` plus the recorded parent provider.
[Evidence](evidence/20261110-chat-shared-udp/REVIEW.md) binds six source leaves,
seven final GCC16 owned-loopback test families and the exact new image. All42
parent function bindings are preserved; the five new functions resolve to the
same candidate image. The existing installed provider is unchanged. No app/runtime
approval, whole Public-chat completion, sanitizer/public-network result or
integrated baseline advance is claimed. Pause after this milestone as requested.

## 2026-11-10 shared actor and async chat profile

Implementation `e4071bb8d40eb57a6a38cd4c0095c8325c7a0dd0`, native tree `f84d5a360f9d70e26d217c8f661891b0cbfbd49e`.
[Evidence](evidence/20261110-chat-shared-actor/REVIEW.md) records real SDK-to-Qt
IPv4/IPv6 DATA/ping and bounded ingress/close tests, asynchronous profile tests
and retained existing DATA/broker acceptance. This is a lower-layer milestone;
operational chats, new app runtime cohort and complete component integration
remain open. The protected user build/provider are unchanged. Chat work continues.

## 2026-11-10 original room settings and managed receive policy

Implementation `39fa39a01b58406a0a7fa4f0a9fca787e2bd6981`, native tree `6dc2d547ac85569c03cf8bb50ef5bc99254b703b`.
[Evidence](evidence/20261110-chat-room-compatibility/REVIEW.md) records six affected
GCC16 suites, compatible nicknames/legacy options, owner-only read-only receive
and unsigned contact hints, plus Java's actual six-call concurrency edge.
Full room runtime/discovery/UI and writable moderator controls remain; no global
component baseline advances. Continue the authorised chat chapter.

## 2026-11-10 general-room owner

Original core `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; original azmsgsync `4209aa5029e3035cbb09dc3a83e9491d3f36fbc6`; SDK upstream `740a0b9aeabe00e762cc0efe4a0f27593db2550b`. Native implementation `54b56af9ca9788bbc6411c622a52c76bb7bc3caf`. [Evidence](evidence/20261110-chat-room-owner/REVIEW.md) qualifies actual owned room discovery/exchange/history and source-scoped ownership, not full original-handler/new app runtime or component completion. Continue this chapter.

## 2026-11-10 general Public chat preview

Original core `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; original azmsgsync `4209aa5029e3035cbb09dc3a83e9491d3f36fbc6`; Native signer `f7cac97d1c396f904698f456d4fc8694da72a170`; SDK upstream `740a0b9aeabe00e762cc0efe4a0f27593db2550b`; shared SDK overlay `4ff478031095830fcfb7531dd6bc0f21b30906af`; Native preview `a1096b5d1fab9988d137cb05d351d0fca6b4c369`. [Evidence](evidence/20261110-chat-operational-preview/REVIEW.md) records exact image/log hashes, nine targeted suites, original handler/order checks and actual Public IPv4/IPv6 NODE probes. Basic Public preview is delivered; whole-plugin parity and user real-profile/room acceptance remain open.

## 2026-11-10 chat publication repair and request-validation optimisation

Original core `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; azmsgsync `4209aa5029e3035cbb09dc3a83e9491d3f36fbc6`; Native signer `f7cac97d1c396f904698f456d4fc8694da72a170`; SDK upstream `740a0b9aeabe00e762cc0efe4a0f27593db2550b`; SDK overlay `4ff478031095830fcfb7531dd6bc0f21b30906af`. Native chat fix `5b5a56d0348c5664f3cc10240ae3841032d21557`; tracker validation/diagnostics `46df862b2a2ff98acccb55f96aa751feca5146c0`. [Evidence](evidence/20261110-chat-profile-publication/REVIEW.md) records full-profile Host/UI and request-authority checks. User chat acceptance remains pending. Unwanted checking, sustained CPU and absent payload transfer remain unresolved real-profile defects; the previous scoped resume repair is not accepted as solving them.

## 2026-11-10 original Java chat key format

Original core cb8c6cb9bb97916cbc7e80338a017200ffc3e71f; azmsgsync 4209aa5029e3035cbb09dc3a83e9491d3f36fbc6; unchanged Native signer f7cac97d1c396f904698f456d4fc8694da72a170; SDK upstream 740a0b9aeabe00e762cc0efe4a0f27593db2550b / overlay 4ff478031095830fcfb7531dd6bc0f21b30906af. Native b67c79cb1eaae7ab6c3f23142fb97549c8a28b0c; tree 6856492739cc7f4d52d0f769347c68bcab5421a7. [Evidence](evidence/20261110-original-chat-identity/REVIEW.md) records the original-generated identity regression failing before and passing after, four suites and exact image/log hashes. User retry remains open; Waiting/CPU are not repaired by this milestone.

## 2026-11-10 restored startup / tracker metadata scheduling

Original core cb8c6cb9bb97916cbc7e80338a017200ffc3e71f; azmsgsync4209aa5029e3035cbb09dc3a83e9491d3f36fbc6; unchanged signer f7cac97d1c396f904698f456d4fc8694da72a170; SDK upstream740a0b9aeabe00e762cc0efe4a0f27593db2550b and overlay4ff478031095830fcfb7531dd6bc0f21b30906af. Native e735c85585312101d2afa1da2a6e624f2040440b; tree 2c1a96c633e733ac00779cd2f147eeedb300a1be. [Evidence](evidence/20261110-startup-metadata/REVIEW.md) records live profiling, bounded/cache invalidation and fresh authority tests, interleaved queue groups, tracker observer capacity, UI/lifecycle and six owned transfer scenarios. No global startup/checking barrier or real-profile speedup claim; user retry remains pending.

## 2026-11-11 network / resume diagnostic candidate

Native 18ece176d8d36e6a35b284cc5511b7528d94ea35; tree 89d39c5b984efcb89881737eb5f44d17113100c4. [Evidence](evidence/20261111-network-resume-diagnostics/REVIEW.md) records the Java valid0/DND resume repair, shared-chat Java-mode compatibility, route-aware tracker status and successful-alert subscription, and the requested stdout admission/listener/URL/HTTP400 diagnostics. The separately pinned Public-only SDK derivative retains a bounded400 body without adding network authority. Generated cold/warm/changed-payload, owned dual-stack transfer/chat and exact-image controls passed. Fully completed real-seed Checking, actual HTTP400 cause, real-room connectivity and startup CPU acceptance remain open; do not generalise the DND repair to those reports. No real profile/payload was written by the tools and no completion was copied from old backups. Superseded build products were deleted; keep the current working build and source/evidence, not rollback application copies. No new chapter started.
