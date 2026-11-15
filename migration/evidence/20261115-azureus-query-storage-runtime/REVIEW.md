# Owned query-storage runtime

Native `60d1384b63fb6645dc44b6173be2d095177ccf7a`, tree `e2cb76c944e39a45fed12bdc945a5af3e479901d`; original BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

The ordinary actor now accepts request 1038 and replies with 1039 through the
existing endpoint/connection/transaction-bound UDP dispatcher. The sender uses
the same pending-deadline, receiver and remote-error lifecycle as other DHT RPCs.
Both directions use the preceding actual-original packet codec. A query grants
no STORE nonce, value publication, new endpoint or private torrent permission.

Inbound work is bounded to 32 simultaneous requests, 384 rows per packet, the
1400-byte physical envelope and five seconds per response. Claimed address and
normal exact endpoint are checked before use. A normal-profile source grant
precedes index observation; matched keys each acquire their current source
preflight, duplicates only once. Delayed callbacks carry one-shot and phase
identity. Cancellation, close, foreign threads and rejected registrations cannot
resume an old response. The final response requires current source authority
and the same actual full-key index matches, so local withdrawal or replacement
cannot return a frozen earlier tail or an unapproved newly appeared key. The
query does not repeat ordinary DB reads, counters or expiry scans. Timed-out
work and close release owned metadata independently of a held provider callback.

Owned real IPv4 and IPv6 actors verify ordered found/missing/duplicate rows,
source withdrawal, key mutation during preflight, stale callbacks after rejected
registration or close, foreign replies and finite request limits. Query-only
traffic cannot authorise STORE. The actual normal GeneralChatService, shared UDP
binding and profile broker also receive queries and respond from data admitted
through token-first STORE. A generated profile disabling DHT before the query
suppresses the response; stop/join prevents subsequent traffic.

All selected module and consumer regressions passed without skips; one full
application link passed. [qualification.json](qualification.json) records the
current groups. The original 34-packet/24-reply-parser/18-DB-transition proof is
retained from `90aa3c49107c6a6c9e8f5fc53a18195b8c31e31f`. No live-profile change,
new daily image or real-user acceptance is claimed. This is a one-packet query
primitive; splitting large surveys, neighbourhood discovery, per-contact survey
state/copy decisions and signed key blocks remain dependent unfinished work.
Query-storage source coverage remains in progress until those sender batching
and survey interactions are accounted for; broader F09 is not complete.
