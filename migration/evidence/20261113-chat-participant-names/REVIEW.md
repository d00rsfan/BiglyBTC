# Original nameless chat participant names

Native `b0ec798ab331200e747b597228ece2031bff32e9`, tree
`310aa2e9b0ea7ce8084f9f469aaa4cbfcb0e1b46`; original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

BuddyPluginBeta.pkToString copies three bytes at offset8 and uses uppercase
ByteFormatter hexadecimal. ChatMessage.getNickName uses this when a normal
message has no nonempty payload nickname; participant replay updates the roster
name through the final sorted message. Native incorrectly displayed the first
six key bytes in lowercase and retained the first historical roster nickname.

The shared display helper now uses the original byte selection/case. A pure
presentation function retains historical names per message, the final current
name in the roster, and the existing timestamp cache/pruning. Distinct full
public keys remain separate when their short names collide. Empty/whitespace
nickname distinctions are retained. Null helper input matches000000; a non-null
key shorter than11bytes returns no value, matching the original method's error
outcome without adopting its exception API. Authenticated admission remains the
room service's responsibility. No short name becomes a cryptographic identity,
persistence key or participant deduplication key. Raw room history and nickname
payloads are not rewritten. Received mention text is preserved; its original
short identity now matches nameless sender display. This scope does not claim
all original system-message, FTUX, friend/pin, clash or mention-editor behavior.

A generated JDK17 oracle invokes the actual pinned private method on an Unsafe-
allocated instance without running a plugin constructor. Its temporary home is
isolated and network/listening are denied.300 generated null, short,11..96-byte
and equal-prefix keys match value/error outcomes. This is display-method
qualification, not cryptographic or network admission evidence. Projection
regressions cover changing/cleared nicknames, timestamp reuse/pruning and short-
name collisions across different full identities. Five projection Qt rows pass
with the Java differential executed, zero skipped. Existing chat page8 rows and
rating editor19 rows pass. The static desktop host compiled; three affected
CTests passed in1.66seconds. Local compilation logs are retained privately.

The presentation pass also replaces transport implementation counters in the
rating editor with plain publication status and a concise explanation of local
saving/Public settings. No new daily-use executable or SDK was produced, and
the running application/profile/preview manifest were not changed. The fix is
source-only until a user-requested build. Continue the remaining rating adapters.
