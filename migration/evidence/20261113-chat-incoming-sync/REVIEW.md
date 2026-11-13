# Original incoming chat synchronisation status

Original azmsgsync4209aa5029e3035cbb09dc3a83e9491d3f36fbc6;
native4dd0fc6b43b322f969f781d0708d725d0f138502, tree
edae42a84ed41382de3049d3073582e5576d500c. Source MsgSyncHandler.getMessageCounts
and inspected first-attempt/accepted-reply update sites. This is a dependency
of optional rating notices; the notice chapter remains active.

The pure incoming-status model preserves the original10-second cache, unknown
state before a first attempt, pending count, three consecutive no-more replies
and2-minute quiet interval. Cache invalidation deliberately follows the original
method rather than immediately changing status after each reply. Consecutive
quiet counts saturate at the only observed threshold3; a pending reply resets
the counter. Outgoing RoomSession calls record the first actual accepted ticket;
accepted authenticated replies update the real pending count. No fabricated
response or delivery state is introduced. Native attempts require an actual
ticket, which is a finite scope difference from an original pre-send timestamp.

The exact original getMessageCounts body executes with an explicit memory clock,
empty message list/zero outgoing count and inspected event-state assignments.
4288 observations compare64 generated traces, cache boundaries, pending replies
and quiet timeouts. Four Qt rows pass with zero skips in0.276seconds; CTest reports
0.30seconds. This is actual original-method comparison, not original transport
scheduler, wall-clock timer or whole room-session runtime equality. Source and
method digests are in[source-scope.json](source-scope.json).

The shared service static target builds successfully with the added field.
Automatic notice sending, source-bound background room integration and read-side
peek/autonotify remain further work. No live profile, frozen process/image,
SDK or user-facing build changed; no sanitizer/platform credit is claimed.
