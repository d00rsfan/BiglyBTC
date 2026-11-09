# Preserve checked pieces across Native / Java handoffs

Status: scoped regression qualification passed; fresh real-library acceptance remains open.
Original reference: cb8c6cb9bb97916cbc7e80338a017200ffc3e71f.

An interrupted or not-yet-classified admission used to save every piece as
RECHECK_REQUIRED, discarding even actual verified pieces. A clean exit could
therefore make Java treat a previously completed torrent as incomplete and
start checking it again. The original RDResumeHandler preserves DONE pieces
when saving an interrupted check.

Native now saves actual verified pieces as DONE and leaves only unknown pieces
for checking. A paused admission that has not produced its first SDK checked
observation may retain its imported original classification. That retention
requires fresh named-file/descriptor identity, size and timestamp proofs for
every real file; SDK resume rejection, hash failure or admission failure disables
it. The checks run again before publication. Known-empty pieces and the original
partial block map survive an unchanged paused admission too.

An all-DONE original map with valid=0 is accepted for qualified linear V1 and
V1-selected hybrid layouts, matching the original startup code: valid=0 makes
unfinished pieces uncertain and does not invalidate DONE itself. Explicit
Recheck, unknown/RECHECK_REQUIRED pieces, changed files, recovered state, pure
V2 and unsupported storage layouts retain conservative verification.

Resume decisions are visible in the status tooltip and logged once per reason.
The log contains fixed diagnostic categories, without torrent identities or
paths. The initial trigger for every reported unwanted check has not yet been
proved on a fresh clean Java profile. Previously invalidated maps are not
repaired by asserting completion; Java's current recovery checks are left alone.

Final validation: seven focused repair cases, 61 storage families / 4,776
assertions, and the actual original Java isTorrentResumeDataComplete predicate
passed. The Java bridge consumed a checkpoint produced by an actual immediate
clean shutdown of the Native session on generated data. It used source-built
original classes and an in-memory configuration, without starting a Java client.
Existing resume and per-filesystem scheduling cases were also exercised. Two
old test assumptions were corrected: interrupted output formerly expected the
destructive all-RECHECK map, and an unchecked-map fixture depended on timestamp
clock granularity. Changed mapped data may refuse the entire save; its negative
test verifies that stronger refusal and unchanged saved bytes.

The build is normal GCC16 Release with Qt6.12.0 and the unchanged qualified
libtorrent2.0.12 provider. No fresh sanitizer or real-profile runtime acceptance
is claimed. Tests used generated profiles and data. No user payload, profile,
room or tracker information is included in this evidence.

Implementation: `01f8618a0d7fbf1bd11dfde536fc424db13d84ea`.
Native subtree: `85a51cf2b16d46ac1856b26e75b09785e3659326`.
