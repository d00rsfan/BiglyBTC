# Fresh download chat subjects without repeated library scans

Native2b8e5f58c07bde1b442eafd9dd7c3a9f15848fcc, tree
c3b0ac376d574c936190eda78cdc66e8b03450a0. Original canonical download behavior
remains pinned to BiglyBTcb8c6cb9bb97916cbc7e80338a017200ffc3e71f; earlier
owner/UI/notification reviews preserve their exact original baselines. This is
a native resource/authority follow-up, using the descriptor/metadata observation
pattern already present in the Public torrent source owner. It introduces no
new wire format, torrent Start/resume authority or profile writes.

Repeated network permission checks previously called the full download subject
reader, including inventory parsing and original info hashing. One retained
profile OFD now owns a bounded64-entry read-only subject memo. Every lookup
first holds the same profile source gate, checks pending acquisition journals
and freshly observes anchored files. Observations cover the active directory
identity, original config/inventory primary/backup/saving candidates, selected
state/backup and original active/cache.dat overrides. Regular-file metadata
includes device/inode, ownership/mode/link count, size and nanosecond mtime/ctime.
A changed observation runs the existing full checker again, then reobserves
before memoising. No unrelated profile revision is substituted for actual file
freshness. The cache stores only the small typed subject, not whole torrent or
library trees. Diagnostic counters are atomic; actor code does not take the
memo mutex or perform file IO.

An uncertain memo observation falls back to the original full reader, preserving
its primary/recovery/safety contract. An unsafe but ignored backup therefore
does not impose a new refusal when a healthy primary is authoritative. Invalid
subjects are not cached. Existing network, profile, chat identity, lifetime and
commit permits still guard each actual effect. The memo is not a filesystem
security boundary or a substitute for those permits.

A generated2001-download library is validated once;500 subsequent subject reads
produce exactly500 memo hits and no further full inventory/info reads. A same-
size network edit with restored mtime is still detected through metadata freshness
and denied. Further tests cover original gzip cached-attribute overrides,
recovery-only config, unsafe ignored backups, primary symlink refusal and safe
restoration. Existing source/canonical/private/unknown-field regressions remain
green. The three configured CTests local-rating-host-ui, owned-download-chat-peek
and automatic-download-chat-notifications pass in48.09seconds (0.32,25.57,22.20),
zero failures/skips. Both actual IP families still exchange, reopen, notify and
refuse a send after Public withdrawal. The desktop host static library compiles.

This closes the bounded Public per-download chat adapter source chapter together
with its preceding reviews. Original display/attention/ignore/history preferences
remain the next normal chapter; non-Public providers, full plugin parity and
real-profile/production-display acceptance remain explicit later work. No new
daily-use image, backup, user profile access or SDK artifact was created. The
reusable build was retained; the workspace still has23GiB free. All profiles,
rooms, names and metadata in these tests are generated.
