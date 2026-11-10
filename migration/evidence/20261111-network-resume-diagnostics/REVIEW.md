# Network, resume and console diagnostics


Scope: critical regressions found during the first real-library previews; no new migration chapter.

Original core: cb8c6cb9bb97916cbc7e80338a017200ffc3e71f
Original azmsgsync: 4209aa5029e3035cbb09dc3a83e9491d3f36fbc6
Native signer (unchanged): f7cac97d1c396f904698f456d4fc8694da72a170
SDK upstream: 740a0b9aeabe00e762cc0efe4a0f27593db2550b
Native shared UDP SDK overlay: 4ff478031095830fcfb7531dd6bc0f21b30906af
Native implementation: 18ece176d8d36e6a35b284cc5511b7528d94ea35
Native tree: 89d39c5b984efcb89881737eb5f44d17113100c4

Implemented changes

Java's RDResumeHandler restores DONE and leaves NOT_DONE alone even when valid=0; STARTED and RECHECK have separate semantics. Native now accepts the qualified linear V1 partial map, proves physical extents covered by DONE, and does not require a missing, fully skipped file to invalidate other checked pieces. Possible native writes are still conservatively marked before I/O; fully skipped pieces are not turned into artificial recheck work. Sparse skipped-file preparation refreshes its identity before creating fast-resume/retirement proofs. Generated cold/warm cases cover ordinary and genuinely mapped paths; a modified DONE extent remains subject to verification. Pure V2, conversion and unsupported storage mappings are not newly qualified.

Shared Public chat policy accepts owner-held 0664 Java configuration while retaining named-file/CAS, ownership, symlink, network, identity and world-write checks. Standalone experimental PublicBound policy keeps its earlier permissions. The selected-room display now retains global network errors, rather than hiding them behind Connecting. Actual room exchange remains controlled by the existing independent owner and unchanged cryptographic signer.

Tracker status is aggregated by actual URL, listener and V1/V2 protocol. An ineligible IPv6 route or failing backup cannot erase a successful independent announce. A real failure on the previously working route does invalidate it. Normal HTTP/UDP configurations subscribe to successful tracker alerts even without DHT/queue observation; error alerts alone previously produced a misleading projection.

The requested stdout diagnostics report UTC timestamps, saved order/admission, source map counts and storage class, restore/refusal reasons, SDK resume rejection and resume-validation versus payload-hashing transitions. Network records distinguish eligible interfaces from actual socket-bind results and include the dedicated Mainline listener. Tracker records include complete saved URLs and error reasons. At the user's request they can include passkeys; actual runtime logs remain local and are not versioned. Chat routing counters and precise connection failures are visible without message or room-key logging. Optional launcher mirroring uses the existing private local log, capped at approximately4MiB; stdout remains visible.

A separately pinned Public-only SDK derivative retains at most4096 body bytes for HTTP400 diagnostics, escaping control bytes and marking truncation. It adds no request, redirect, routing, identity or network authority. The earlier SDK image is not overwritten. The complete1093-source-file manifest, unchanged286 headers,47 exports, derived patch and exact artifact pins identify the candidate; the six shared-UDP interface/source inputs remain identical to their original qualification. Existing mixed/private SDK roles are not enabled.

Validation

- runtime: 1/1 Test #130: public-chat-runtime-source-image ...   Passed    0.73 sec, 100% tests passed, 0 tests failed out of 1 (0.819s).
- resume: Totals: 5 passed, 0 failed, 0 skipped, 0 blacklisted, 1194ms (1.217s).
- trackers: Totals: 7 passed, 0 failed, 0 skipped, 0 blacklisted, 17942ms (17.958s).
- shared: Totals: 5 passed, 0 failed, 0 skipped, 0 blacklisted, 491ms (0.516s).
- chat-service: Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 20198ms (20.21s).
- chat-ui: Totals: 7 passed, 0 failed, 0 skipped, 0 blacklisted, 498ms (0.666s).
- host: 1/3 Test #370: ordinary-normal-daily-control-retirement ...   Passed    8.59 sec, 2/3 Test #386: general-chat-profile-publication ...........   Passed    2.46 sec, 3/3 Test #387: restored-library-startup-priority ..........   Passed    3.16 sec, 100% tests passed, 0 tests failed out of 3 (14.265s).
- Owned transfer IPv4-cold-warm: Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 30055ms (30.342s).
- Owned transfer IPv6-Cyrillic-ampersand: Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 30033ms (30.285s).
- Stdout and private-log mirroring verified; an oversized generated log was bounded successfully.
- GCC16 / Qt6.12 / C++23. No fresh sanitizer claim. Only affected targets/cases were built and run; no long unchanged full matrix was repeated.

Limits and next evidence

The fully completed real-seed report is NOT declared explained or fixed. The DND defect cannot be generalised to that case. Exact immediate-before-admission evidence is needed; older backups are not proof of that state and were never used to restore completion. The new timestamps/source map/SDK transitions distinguish delayed admission, source refusal and later engine rejection. Real-library CPU/startup parity and absence of unexpected full checks remain open. A bounded isolated Public NODE diagnostic received IPv4/IPv6 replies but did not complete the connected-state condition within its budget; that is not a live-room acceptance claim. No real room was joined or posted to by the agent.

A real HTTP400 still needs its actual captured response; this change provides that diagnostic and corrects the unrelated last-error projection. User real-profile/room retry is pending. Java and real payload/profile were not written by these tools. All reported subjects, addresses, private URLs, filenames, per-torrent maps and samples remain in ignored local diagnostics, outside this evidence.

Obsolete migration build products were removed at the user's request. Keep source, provenance, concise evidence and the current working build; do not accumulate rollback builds or unneeded historical test executables. Minimal negative-control inputs are test fixtures, not rollback applications.
