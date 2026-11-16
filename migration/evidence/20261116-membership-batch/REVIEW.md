# One-publication cross-tag membership batches

Native source `b975a1994a6928a68d0e3796cda9507df613421d`, tree `9a452fc12f204448fb4633fba9602b1944df06e6`; original baseline
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f` for tag membership/config persistence.
This is a Native persistence prerequisite; no fresh original runtime is claimed.

ManualTagStore accepts at most32 ordered AddMember/RemoveMember requests across
manual tags. The existing single-edit API delegates to the same private sequence
path. Every changed edit is prepared in memory from the prior value and repeats
the actual ordinary tag/group-effect policy. A late invalid ID, malformed source,
unsupported effect or cancellation cannot publish an earlier prefix. Empty or
byte-identical net results do not publish, advance generation or create a backup.

One final selected-type patch passes through the original owning loader/preimage
and actual file CAS. The held same-profile writer, exact candidate bytes/metadata,
subscription/recovery restrictions, source gate, cancellation checkpoint, allocator
ownership and uncertain-save poison protocol remain in force. Only an actual
reloaded Primary can produce a successful changed snapshot. The batch supplies
no automatic tag action, provider, network or membership authority of its own;
its caller still needs the existing store and current captured snapshot.

Fourteen actual isolated store families pass, including all eleven existing
families. New checks prove ordered cross-tag Add/Add/Remove, exactly one generation
advance and the original preimage as the single backup, opaque bytes/other types,
actual reopen, stale refusal, late missing/configured-tag failure without file/
backup/generation changes, cancellation, oversized input, empty and net-noop file
stamp preservation. Existing counter, recovery, uncertain-save poison, worker/
OFD, group, colour and no-event-noop cases remain green. Normal Host persistence,
category/state consumers, disabled startup mode and stale/foreign/cancelled/closed
worker checks pass through the refactored single-edit path. GCC16 Release/C++23/
Qt6.12.0 focused builds and one coherent application link pass. No sanitizer or
real-profile run is newly claimed. Review was by the implementing agent; no
independent reviewer is claimed.

The pure dependency protocol is qualified separately in
`92084d41f4326a9cde0fae4af2a85dc26ac0a1aa`. It is not yet connected to this batch
API. Source-bound transient membership plans/overlays and guarded recursive
execution remain current work; publishing this prerequisite does not enable
previously unknown constrained membership or bypass any dependency guard.

No live profile, payload, running client, protected SDK, daily launcher or rollback
image changed. No whole registry/F13/source integration baseline advances.
Continue the current source/dependency chapter autonomously without a new daily.
