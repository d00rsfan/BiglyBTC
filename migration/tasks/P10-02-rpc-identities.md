# P10-02 Original remote-control torrent identities

Status: bounded staged identity model qualified across GCC 16, Clang 24 and
ASan/UBSan/LSan; original file binding and host activation pending. xmwebui baseline
`c9ab8f7cd62b2874f7385772992c0288a224e85d`, exact getID method with supplied
ordered active/archive stub list, int64 attributes and plugin counter.

Original torrent attribute is Plugin.xmwebui.xmui.dl.id (runtime ID prefix).
Global xmui.next.id uses the actual plugin-folder-derived config prefix, not the
repository name. First lookup, including allocate=false, repairs positive duplicate
IDs in list order and replaces the next counter with max-positive-ID+1. Negative
IDs persist; only zero IDs allocate. Java signed-long wrap must be characterized.

Native preview returns ordered original write intentions, including counter and
attribute changes, without touching files or current ledger state. Explicit apply
requires the same owner/generation; future host must durably stage writes before
applying. Model only; file binding, cross-file journal, active/archive lifecycle
and real RPC data-provider integration remain required. No live profile access.

828 original cases cover 40,824 lookups per compiler, ordered setter traces,
final state, duplicate/zero/negative IDs, one-time counter reset and signed-long
wrap. The 804 RPC envelope regressions and native stale/foreign/moved-plan and
100,000-torrent checks also pass. No real attributes/configuration are written.
