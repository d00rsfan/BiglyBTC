# Saved RSS subscriptions

Implementation: `99f91cb3661544d47a3c9e79227315134b726bb8`; Native tree: `97f6c863cab284de2188d610f1f51d59cb744676`. Original reference: `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

## Behaviour

View → Subscriptions reads actual saved definitions and cached results. Normal sessions support Check now, Cancel, Mark read/unread and Delete results. Eligible due checks use the existing serialized worker. Preview reads history with zero fetch providers. Links remain plain text; this chapter adds no download action.

Checks bind the actual profile lease, session, creator worker and complete source identity. A matching catalog engine takes precedence over the embedded RSS engine. The concrete HTTP owner checks its source domain and receiver token before DNS and revalidates owned phases. Closing cancels and joins work before profile exclusion is released.

Session/page/request keys reject stale results. Selection changes clear old result keys; widget reentry cannot restore another subscription's rows. Explicit View reopening refreshes a retained page after a session switch. Cancellation after a durable update reports that the change was saved.

## Compatibility

Original-format subscriptions.config, signed local definitions, results, sibling records and unknown bencode/JSON values are preserved. Local loading verifies the metadata/details binding and retains signed bytes; import-time signature verification and signing are not implemented.

Reconciliation follows Original result keys, UID field u, first-scan read flags, UID matching, counters and tombstone retention thresholds. Read/delete operations preserve scan timestamps; unchanged edits are byte-preserving no-ops. A bounded Native intent supports interrupted two-file writes. It does not make both files atomic to Original Java after an unclean shutdown; Native recovery must finish first.

## Validation

GCC passed in 110.027s; ASan/UBSan with leak detection passed in 258.572s. Each lane freshly ran 25 cases across five groups. Another 31 HTTP cases retain their successful r3 results through exact unchanged executables, generated dependencies, libraries and a disjoint 309-file source/include closure. Earlier r1/r2 compile failures and r3 aggregate failures remain failed evidence.

The [verification](verification.json) and [independent C++ review](independent-review.json) bind both committed-source receipts, 1382 source files, eight executables, eleven archives, macro variants, private factory symbols and five final-link isolation checks. Full raw receipts remain at the recorded durable paths. No performance or default-distribution qualification is claimed.

The separate Java → Native → Java [file handback](original-result.json) passed six commands in 3.723s. Original public container, bencode/JSON, SHA1/CryptoECC and healthy FileUtil APIs created two genuine signed definitions, read Native read/delete changes and wrote an unread return that Native verified. Signed bodies and opaque values survived. Actual deletion days matched measured clock bounds. The [independent handback review](original-independent-review.json) distinguishes recorded ephemeral digests from bytes retained after cleanup; the private profile was removed.

The installed JDK required an anonymous Unix stream pair for file-channel initialization. A reviewed constraint permits that exact local pair while denying socket creation and external endpoint calls; actual controls passed. The original startup failure is retained. This handback covers public codecs/crypto and healthy files, not Original manager, scheduler, import, UI or recovery runtime.

The [GCC image](subscriptions-window-gcc.png) comes from the fresh passing UI test and is byte-identical to Root's reviewed image. It demonstrates readable classic layout using controlled presentation data. Actual serialized-worker tests also cover cancellation, durable changes, close/reopen and lease release.

## Remaining work

Automatic acquisition, notifications, active filters, unsupported library/archive auto-read effects, general metasearch, creation/import and full date/display parity remain refused or outside this chapter. Unsupported checks do not consume Java scan/read state. Explicit-zone dates retain correct offsets rather than copying the separately reproduced Original Atom offset defect.

Component states, coverage and integrated baselines are unchanged. Full P08/P07, the remaining migration plan and default distribution promotion remain open.
