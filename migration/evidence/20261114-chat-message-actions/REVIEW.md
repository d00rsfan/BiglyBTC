# Original Public chat commands and composer workflows

Native a863c5e72c19e17536fcc167415f251ea7b0bada, tree c94fbe4aa24be904953e6a094f827d37f12b300a. Original BiglyBT baseline remains
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f; azmsgsync remains
4209aa5029e3035cbb09dc3a83e9491d3f36fbc6. Bounded chapter complete; whole
chat/plugin/client parity and real-profile acceptance remain incomplete.

Original public /join, /pjoin, /nick, /ignore, /ignore -r, /me, /help and /clone
run through the existing owner/metadata/source gates. Full signed-roster keys
remain participant authority. Matching names is case-sensitive as in the
original; Native refuses ambiguous names. Routed operations retain the original
submission room/text for the GUI acknowledgement. Explicit navigation opens
the general Chat page without first consuming the previous room's unread state.
Instance nicknames change mode/value in one original config CAS, preserving
the shared nickname. Nickname writes check the live identity/room source.

Private/provider/unknown commands remain local errors, including after Unicode
escape decoding. The former ordinary-text broadcast of /msg-like input is
removed. Commands are classified before any character chunking, so a private
command's later text cannot become a public continuation. The raw Room transport
does not interpret slash commands. Explicit f.t1 action flags coexist with f.o,
survive signed exchange/history, and remain set on every native action part.

The original UI formatter and mention method agree in136 selected-source traces:
one-pass Unicode escapes, emphasis-protected500/400 UTF-16 chunking and caret/
ASCII-space-specific mention insertion. History keeps32 attempts and restores
the buffered draft; Shift/Ctrl/Alt+Enter insert newline, Ctrl+B/I toggle selected
emphasis and a Tab key is suppressed. Qt's plain-text getter was proved to
normalise NBSP to space; raw document text now preserves it, mapping paragraph
boundaries to newline. Trimming follows Java <=U+0020. Acknowledgement clears
only an unchanged raw/trimmed submission; failure keeps the draft.

The original2048 UTF-16 editor limit is enforced at submission. Native retains
an oversized pasted draft with a disabled Send/tooltip, rather than truncating
existing text. The internal user-text facade remains bounded at16KiB and128
parts. /clone selects the existing native general page instead of creating a
second Java window; help lists implemented commands locally. These are explicit
bounded UI differences. Private chats, friends/moderation, rich rendering/search
and the remaining original command family stay planned. Selection copying uses
the existing plain-text widget path; no implicit navigation or code execution
was added. Browser/player remain last.

Original character chunks can exceed azmsgsync's600-byte content cap, and can
split a UTF-16 pair. Native keeps valid pairs together before UTF-8 conversion,
then fits any oversized part against the actual prepared signed payload.
It never raises the600-byte wire limit. Parts are prepared/committed in sequence
under a fresh original room epoch and physical source permit each time. A
partial failure reports accepted parts, retains the draft and does not resend.

The original per-handler30-messages/60-seconds pacing delay agrees in256
selected-original clock traces, with integer narrowing/arithmetic preserved.
The actor waits on a Qt timer before obtaining a fresh source permit; it holds
no permit or blocked worker during the wait. Original flood-warning UI and full
moderation are not implied by this delay policy.

Validation: GCC16/C++23/Qt6.12 affected static Host/main object and source
consumers compile. Signed Room/history, profile/IO and original formatter checks
passed; the final widget/composer sets pass in1.73s, zero skips. Original pacing
uses deterministic supplied time, not real sleeps. The focused real owned
IPv4/DDB two-client command scenario passes in22.87s: private/escaped/long
commands leave no public ledger entry; positive signed traffic works; Unicode
including an emoji across the legacy boundary arrives complete as normal text
and as all-action parts; ignore/unignore, atomic nickname, source-key ACK and
new-room navigation are checked. A fixture initially used a spaced name with
normal /ignore, which original parsing takes as one word; the fixture was
corrected without relaxing production matching. No broad green network suite
was rerun. A GCC category-bridge maybe-uninitialised warning was observed on an
unrelated expected move; changed has an explicit false initialiser, with no
confirmed defect or category change in this chapter.

The [formatter/mention source hashes](composer-source-scope.json) and
[pacing source hashes](pace-source-scope.json) define exact selected reference
coverage and explicit in-memory collaborators. No original SWT lifecycle,
exhaustive command, full non-Public plugin or release parity is claimed. All
data/profiles are synthetic. No running application/profile, frozen launcher,
daily-use image, runtime manifest or SDK was changed.
