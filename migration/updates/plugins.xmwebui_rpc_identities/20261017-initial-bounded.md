# Original RPC torrent identity review

Self review, 2026-10-17. xmwebui
`c9ab8f7cd62b2874f7385772992c0288a224e85d`, exact getID method with supplied
ordered registered active/archive stubs and int64 attribute/counter state.

Native preserves the positive duplicate scan and list-order repair, initial
counter reset to max-positive-ID+1 even for allocate=false, zero-only allocation,
negative ID preservation, original setter order and signed-long overflow. A
preview owns its intentions and changes neither files nor ledger state. Explicit
apply checks ledger ownership and generation before any in-memory update; stale,
foreign and already-applied plans fail. Moving the ledger preserves valid plans;
moved-from ledgers cannot act. Future host must durably stage the original
attribute/counter changes before apply. This is not a cross-file journal.

The fenced helper runs exact getID with supplied in-memory setters; each setter
records its ordered invocation and value. 828 cases/40,824 lookups per compiler
match values, ordered writes, final IDs/counter/check state, including random
vectors, read-only queries, duplicates, Long boundaries and repeated allocation.
GCC 16, Clang 24 and Clang 24 ASan/UBSan/LSan pass these plus 804 existing wrapper
observations and native ownership/generation/move/100,000-torrent bounds. Matrix
capsules pin source/method/class/fixture/runtime/binary hashes. Leak detection
stays on. No real profile, download, config/state write, HTTP or network is used.

The bounded model contains at most 100,000 distinct registered stub instances;
list order is supplied, not discovered. An ordered set bounds duplicate-check
work without adversarial hash collisions. Active/archive registration/removal,
real original attribute/cache precedence, global folder-derived full64 counter,
cross-file journal/recovery and the actual RPC data provider remain required.
The original ID attribute is Plugin.xmwebui.xmui.dl.id; the global xmui.next.id
configuration prefix uses the original installation-directory basename. Do not
replace either with repository names or new native IDs. GPL2-or-later/Vuze notice.
