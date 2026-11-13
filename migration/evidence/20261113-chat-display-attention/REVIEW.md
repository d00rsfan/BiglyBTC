# Original chat display, participant controls and read-marker handoff

Native b9659910a4519e7378542ac3d1e6f982147dcaf4, tree 18097f0d63ab2cb7a34c17ab7b722225f4c07874. Original BiglyBT baseline remains
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f; the existing azmsgsync baseline remains
4209aa5029e3035cbb09dc3a83e9491d3f36fbc6. This completes the bounded
P06-CHAT-DISPLAY-ATTENTION chapter. Whole chat/plugin/migration and real-profile
acceptance remain partial. Prior [profile primitives](../20261113-chat-profile-preferences/REVIEW.md)
and [signed flags/attention predicates](../20261113-chat-attention-policy/REVIEW.md)
retain their separate full source/native provenance.

Both general and property Chat pages expose original global hide-ratings and
hide-subscriptions settings plus Ignore/Pin participant controls. The owner
accepts participant edits only for full public keys in its actual signed room
roster; a nickname or peek hint is not that authority. The held IO broker
rechecks a live room epoch, primary original identity, expected field and full
configuration CAS. Self edits are refused. Only typed display settings reach
the UI; keys/private configuration are not exported as preference data.

Signed raw history/protocol records remain intact. The original visibility
predicate filters decoded transcript text; the roster still includes ignored
and hidden-message authors so controls can be reversed. Original per-message
names and final roster replay names are preserved. Unknown participant settings
are resolved before their text is projected. Original message type1 is
presented as an action. Attention uses the previously qualified local-flash,
remote-ignore and origin rules, including the original origin4 distinction.
Roster refreshes retain full-key item identity, selection and scrollbar
position, and defer changes while their context menu is open. Menu commands
capture room/session/key and the prior field value rather than a moving row.

The original per-room optsmap lmi field uses
written-seconds/message-seconds/Base32(message-ID). The new read predicates
preserve distinct original old-outstanding and seen-fixup behavior, including
ordered object identity, JDK long overflow and decimal narrowing/parsing,
UTF-16 decimal digits, String.split trailing fields and the original lenient
fixed-length Base32 decoder. [Source hashes](read-marker-source-scope.json)
cover exact selected original methods with real Base32/ByteArrayHashMap and
explicit generated message/clock/config collaborators. 768 comparison scenarios
and all65536 char digit values agree. This is not execution of original SWT
focus/window/tray code.

Foreground reading consumes actual outstanding attention and queues the last
raw message marker. Saves are coalesced, source/identity/expected-marker bound,
unknown-field preserving and separate from history-saving preferences. A new
room epoch resets transient read state; shutdown drains pending writes within
the existing close bound and reports failures. A restored history uses the
original classifier instead of announcing already-read records as new.

Config-only changes now revalidate the same factored original DHT/config
predicate without reloading an unchanged library/info subject. All inventory,
state/cache/recovery/permissions/journal observations remain fresh. Config
changes are not permission to reuse changed torrent metadata; uncertain proofs
fall back to the full checker. A2001-entry fixture plus500 warm calls and20
configuration display/LMI updates retains one full subject read, while DHT
disable and same-size/same-mtime network withdrawal still refuse use.

Validation: GCC16/C++23/Qt6.12 affected static Host and main_desktop object
compile; no new daily-use executable. Six focused profile/projection/IO/original
method/resource sets pass in2.11s, zero skips. The full Chat page set passes at
2x scale in1.39s, including stable selection/menu/draft/read-context cases.
The synthetic [screenshot](preferences-2x.png) was visually inspected.
Owned download preview25.84s, automatic notifications22.66s and focused actual
signed general-room/restart30.45s all pass (78.97s total). The latter checks
ignored messages/no attention, stale/foreign roster refusal, restored settings,
saved six-message history/read marker and zero false unread after a new owner.
The final fresh-download-epoch warning reset received an affected service compile;
the long network runs were not repeated for that default-state reset.
All profiles, names, messages and addresses are generated/owned fixtures.

Bounds carried forward: original private conversations/provider behavior,
spammer/IP-filter moderation, participant profile editing, richer transcript
formatting/navigation, sound/window flashing and remaining message/composer
operations are later plan work. Plain text creates no implicit navigation or
code execution. The chosen legacy-pin read-only migration difference remains
explicit in the profile review. Browser/player remain last. No running preview,
real profile, launcher, frozen manifest or SDK was modified.
