# Lazy queued verification and unclassified retirement

Generated regressions reproduced and fixed; real-profile acceptance pending.
Native b30db36a63465553628c5fbd8d726b648a07028f, tree f4278285fd2a3fa2dd40021c24d9b451eaeb1d73. Prior startup transaction hardening
0942fd57146cd9421dda7e2ab17dcdd52655dc61 remains included. Original Java
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f: StartStopRulesDefaultPlugin waiting/queued admission and
RDResumeHandler save semantics. SDK unchanged: upstream
740a0b9aeabe00e762cc0efe4a0f27593db2550b, overlay parent
4ff478031095830fcfb7531dd6bc0f21b30906af, binary SHA-256
8aa740f2181b0295ca25154e680a623e7770832f19e37870add9f2d7d9a055b3.

## Reproduction and fixes

Java's Queued status does not imply that its saved piece map has already been
checked. Initialization may wait until an actual queue selection or manual
Force Start. Successful checking then supplies the map for subsequent restarts.
The local diagnostic baseline and direct Java observation confirmed the need to
distinguish pre-existing pending work from a new Native map transition. No real
profile identifiers or captures are included here.

Five generated failures were reproduced before the fix in less than a second.
Four linear/linked seed/partial cases retired a normal unclassified admission:
completedbytes became0 instead of49152/16384, and filedownloaded was reset.
Another ignored Queued seed hashed3pieces without queue permission. These are
specific Native defects, independent of the historical origin of pending maps.

Ordinary original-library Queued admissions now wait for Run from the existing
queue policy, or explicit Start/Resume/Force Start, before payload checking. Queue
ranking uses the saved no-DiskManager/DND classification while unchecked. Known
false and unavailable classifications remain distinct. This grants no verified
pieces, seed_mode or permission to send unchecked payload. Actual filesystem
check limits and stop_when_ready remain enforced. A deferred SDK check is logged
as awaiting-queue rather than active payload hashing.

Retirement with no imported map and no completed classification preserves the
historical completion/file counters and conservative resume. Actual accounting
remains separate. No Compact finishing or completion moves are derived from an
unclassified piece query. Real checked results, imported-proof failure and
observed hash failure retain their existing conservative paths. No historical
DONE map is restored automatically. Mixed/private-owner policy is unchanged.

## Final qualification

Linux amd64, Qt6.12.0, original Java17; existing Release build and unchanged SDK.
Full outputs remain local. No real payload, automatic client launch, public
network fixture or sanitiser acceptance is claimed.

- `original-check-scheduling-owner`: 30 QTest passes. Includes all five former
  failures, actual queued owner close/reopen with completion counters retained,
  Force Start real hashing, another checked restart with zero hash operations,
  positive ordinary queue selection of a pending download, existing scheduler
  filesystem/global bounds and resume checks.
- Managed selected V1/invalid-flag, skipped Compact and pure V2 restore/actual
  verified upload: 16 QTest passes.
- Ordinary Host owned IPv4 tracker/DHT/payload cold/warm: 3 QTest passes,
  about30s. No new stall of ordinary queued transfers.
- `general-chat-profile-publication`: 3 QTest passes.
- `original-resume-transaction`: 24 cases and24 original Java BDecoder reads.

Two old checking-owner rows still expected automatic checking after external
payload modification. They were aligned with the already-existing safe refusal
before invalidation, asserting byte-for-byte preservation. That production
refusal policy was not changed in this milestone; explicit read-only recheck has
its separate existing tests.

## Remaining gate

The user ran Java to inspect queued behaviour after the earlier baseline.
Require another confirmed Java Exit and refresh the small private baseline before
replacing the preview. Preserve only the two useful compact metadata captures;
no full-profile backup or rollback image. Let the user start and observe the
image. Previously pending maps and newly introduced Checking must be distinguished.
The two generated bugs are fixed, but this is not a claim that the complete
historical real-profile incident has been explained or whole resume parity
achieved. Do not start the next chapter.
