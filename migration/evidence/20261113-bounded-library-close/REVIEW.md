# Bounded graceful close for restored libraries

Native implementation `d1853d1f7b0e7887916d342fffd293f3ed07bb0a`, tree `18b53b2d5a2fd318c1e6748014ef91a0bd355c7d`.
BiglyBT baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`,
GlobalManagerImpl stop/save lifecycle. The native SDK lifetime/receipt protocol
is an adaptation, not a claim of whole Java shutdown parity. Codex reviewed this
scope; no independent reviewer/sub-agent participated.

A generated128-torrent fixture with8MiB of opaque original inventory data exposed
a real shutdown defect: the old close path capped the caller's60s budget at5s.
Filesystem checkpoints could complete, but final counter certification then
failed because the shared deadline had expired. Before: joined/released, no
final counter seal, accounting failure; diagnostic exit4. After: joined/released,
final counters certified, no accounting failure, all piece maps clean; exit0.
The diagnostic's sparse data are generated and automatically removed, not copies
of a real library. Real captures/identifiers/titles/paths remain private.

Graceful retirement now requests at most32 owners at a time. This bounds pause/
flush/removal bursts before the sole alert consumer and avoids sampling untouched
owners on every retirement pass. The caller budgets filesystem retirement; only
the final counter request retains its short certification limit. Per-owner close
waits share the actual close deadline without clearing failures. Host close budgets
scale with the retained graph and cap at1000s. Queue/check scheduling cannot resume
new work while closing. Existing lifetime joins, actual weak expiry, checkpoint
proofs and lost-event refusal remain required. No resume proof/classification
rule or historical-map restoration was weakened.

Actual checkpoint failure now produces a modal warning before the window closes,
instead of an ephemeral status line disappearing with it. Begin/end close output
records retained count, budget and actual receipt flags for future diagnosis.

Qualification:7 focused partial/mapped/invalid-resume QTest passes;12 existing
persistent-owner lifetime/cancellation/reentry/expired-deadline passes;3 owned
IPv4 tracker/DHT/payload/cold-warm passes. A short transfer fixture now uses a
lower download limit so it remains connected over several real2s peer snapshots;
this addresses a timing-only observer failure, not application rate behaviour.
Release GCC16/C++23/Qt6.12.0, same qualified SDK. Full compiler/test logs stay local.

A private post-Exit comparison also found some partial maps left at their initial
prepared MISSING-to-RECHECK stage, without loss of DONE bits/counter regression.
Their primary timestamps predated shutdown, demonstrating they were not finally
checkpointed. The old run did not retain a complete close-failure trace, so this
record does not assert its precise cause or claim real-profile acceptance. Bulk
retirement/deadline changes address demonstrated code risks; the next controlled
run and new warning/receipt output remain necessary. No real profile was modified,
no captured map restored and no application launched/promoted by these tools.
Slow restored-library initialisation remains open.

User now authorises swarm file merge in this same candidate, then STOP. That work
continues under P06-SWARM-FILE-MERGE; no browser/player work is included.
