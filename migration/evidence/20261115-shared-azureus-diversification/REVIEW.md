# Shared original Azureus remote diversification

Selected chapter qualified on 2026-11-15. Source `cbdd8af1972ddbb985de0cb68f656ed9bb3d6570`, repository tree
`39821653ce1b2356a09f5e3d690752fc89975599`. The related classic-control follow-up is `8cbc94b7057d64526fc83e79c4ff635c3cb23198`
([review](../20261115-classic-controls/REVIEW.md)).

The source baseline is [BiglyBT](https://github.com/BiglySoftware/BiglyBT),
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`,
`DHTPluginStorageManager`: remote diversification serialisation/deserialisation,
lookup/expiry, constructor and key selection. The previous ordinary-torrent
runtime left these rules volatile across application restarts. This initial
selected port does not advance a whole DHT integration baseline.

Each actual source/network lane now owns one ledger shared by normal torrents,
room and download-chat discovery, related content, DDB ratings and NAT. Original
`dht/[netN]/diverse.dat` and `.saving` handling retains local, unrelated and unknown
fields. The last live original duplicate is selected; updates patch the last
recognised physical row and retain other rows. A malformed colliding key refuses
publication with original bytes preserved. Existing pending/unsafe files are
never repaired implicitly.

New entries use the original 48–72 hour expiry and fixed distinct size-PUT offsets.
Frequency PUT uses all ten fragments; GET randomisation and exhaustive selection
match the original selected policies. Warm lookups follow saved rules before a
root RPC. Original expiry distinctions at startup/runtime are retained. RCM keeps
its existing policy of avoiding publication for diversified discovery roots.

The IO broker coalesces at most64 updates, with256 bounded waiters and65536 live
records per ledger. Failures defer retries30 seconds and do not certify a PUT.
STORE replies may remember a job-derived key/type after the dispatch permit has
released; metadata grants no file, peer or endpoint authority. Every actual save
retains current network policy/value-version ownership, while outgoing torrent,
chat, RCM and rating work retains the existing per-target gates. Size PUT forks
wait for durable original-file publication. Late records cannot mark a different
record durable.

[Qualification](qualification.json) records raw QTest totals. The actual unchanged
original Java nested methods produced32 key-policy fixtures and checked expiry
removal, constructor TTL and distinct offsets. Native generated profiles cover
unknown/duplicate/expired rows, byte-preserving refusal and network separation.
Actual owned torrent restart preserves the frequency/size publication key set and
does not query the old root; room restart and read-only peek also pass. Original
Java UDP transport exchanges pass for DDB frequency/size publication, packed
continuations and depth bounds over IPv4/IPv6. No Internet/router acceptance is
inferred. Full application linking passed. Complete logs remain outside Git;
only summaries/root errors were read.

Disposition: selected remote protocol/behaviour/persistence is implemented and
qualified. Local storage diversification, forwarded-value republish and broader
DHT parity remain separate work. The obsolete destructive small-network repair
hack is not executed and no whole integration claim is made. This chapter changes
no saved torrent resume or payload layout. Implementation and source review were
performed by the same agent; no separate reviewer was used. The already delivered
classic daily remains immutable; no new image or real-profile run was performed.
