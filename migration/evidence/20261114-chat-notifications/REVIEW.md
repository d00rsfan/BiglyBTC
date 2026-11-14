# Original Public chat notification presentation and settings

Native 36c407cd5fcbc3f6cb046c5e6bb5ab5ec20a1b19, tree 80a364756a6216a2b474795c5ca06f935978ee48. Original BiglyBT baseline remains
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f; signed-room dependency azmsgsync remains
4209aa5029e3035cbb09dc3a83e9491d3f36fbc6. This completes the bounded notification
chapter. Whole buddy/plugin/client parity and real-profile/audio acceptance
remain incomplete. No running image, live profile, launcher, manifest or SDK was
changed, and no daily-use executable was built or delivered.

## Original behaviour retained

BuddyPluginView's pending timer runs every 2500ms. The first coloured phase is
followed by the grey-text phase on even ticks when flash is enabled. Red means
an unseen nickname reference; green means another pending chat. Flash is a
status-image change, not a request to raise the application window. Sound is
requested when the set of pending chat instances gains an instance, rather than
for every subsequent message/count refresh in the same unread room. Native uses
the real owner epoch/profile generation for that identity. A read and refill
before the next empty tick retains the previous set, as in the original.

Per-room disnot excludes a room from the active indicator/sound set without
clearing its actual unread ledger. The original has_mine calculation occurs
before this filter, so an unseen mention in a suppressed room can colour another
active notification red. This source detail is deliberately retained. General
and download views deduplicate the same actual room; status clicks choose an
unsuppressed unread room. Passive snapshots and count increases do not create
new sound edges. Pending presentation resets on profile change/close.

Unseen mentions use raw UTF-16 message text, ASCII nickname spaces replaced with
NBSP, case-sensitive non-overlapping hits and original char word boundaries.
Actual signed nonlocal/nonignored messages and the qualified read-marker fixup
supply the source. Seen classification uses one ledger-index pass, rather than a
new nested scan. The original per-message nickname-location cache is retained
across own-nickname changes and retired with the message/room epoch. Full SWT
mutable seen-state lifecycle equality and exhaustive later Unicode additions
are not implied; selected JDK17/Qt character coverage is explicit.

Original global keys azbuddy.chat.notif.sound.enable (default false),
azbuddy.chat.notif.sound.file (default empty) and azbuddy.chat.notif.flash.enable
(default true) are read/written through the held profile broker. An atomic
expected-settings CAS preserves unknown fields and unchanged scalar encodings.
The dialog also preserves untouched raw path bytes. Room disnot writes require
actual epoch, original identity, live source and expected value. Such a write
changes only disnot, so it cannot replay stale favourite/history settings from a
queued command. Ordinary options leave the freshest disnot untouched. Globals
return only their changed settings, preserving signed-participant preferences.
Notification timers perform no profile/catalogue/payload IO.

## Native presentation and sound adapter

General and property Chat share the display-menu suppression action and settings
dialog. The lower status button also exposes global notification settings. Old
profile dialogs/old epoch menus cannot submit edits or sound tests. Original
light/dark status images and the 6160-byte notification WAV are embedded without
alteration. The [2x dialog](notifications-2x.png) was visually checked; the path
field has usable width. Existing transcript, draft, selection and status routing
checks remain green. The main window's height/layout is unchanged.

Linux uses the installed paplay helper through QtCore QProcess, typed argv and
closed inherited descriptors. The original default WAV is streamed over stdin;
custom native local paths are data arguments. Java trim/default/nonexistent-path
selection is retained. No shell, temporary audio file, JVM, QtMultimedia or
embedded player is added. Parent image/profile descriptors are not inherited by
the helper. Playback is cancelled on profile reset and component destruction.
The adapter permits one helper and drops overlapping requests, while Java can
open overlapping Clips; exact overlap parity remains planned. Missing helper or
playback failure is reported locally. No real audio was played during checks.
The [helper version/hash](sound-backend.json) is qualification context for an
optional system utility, not a bundled or release-qualified audio stack.

## Qualification

GCC16/C++23/Qt6.12 Release affected service/profile/UI consumers, static desktop
Host and production main object compile. No broad sanitizer/network matrix is
claimed. Deterministic selected-original tests execute the actual pending-timer,
sound-switch and getNickLocations bodies with generated room/control/config
collaborators, observing effects rather than starting SWT/audio/network code.
1724 timer/nickname traces agree, including muted rooms, mentions, timer restart
and epoch/profile transitions. [Source hashes](source-scope.json) fix this scope.

Grouped policy/preferences/asynchronous-profile tests pass in 1.28s (three suites).
These cover original defaults, atomic/stale writes, unknown fields, room mute
preservation, live/identity refusal and unrelated-option preservation. The owned
sound-helper suite passes 0.67s: exact original WAV bytes through a pipe, literal
custom path arguments, no inherited non-CLOEXEC descriptor, overlap/cancel,
missing helper, reentrant destruction and original resources/dialog values.
The final widget set passes at 2x in 1.48s after a dialog-width/label-only refinement.
The retained sound/profile/actor functions did not change in that refinement.

The existing owned IPv4 signed-room/history scenario passes in 30.868s, with no
failures/skips. It now checks a real signed own-name mention, preserved participant
preferences after global changes, stale epoch refusal, correct suppression with
unread still present, unmute, nickname-cache behaviour and restored read-marker/
history with no false unread. Globals are restored before restart. All endpoints,
profiles, names, paths and messages are generated/owned. No external chat was
contacted and no real user's profile data or runtime traces are published.

## Remaining plan work

Private/friend conversations, notification-centre posting (notipost), floating
transfer-bar integration, offline configuration placement and full original
sound overlap/platform behaviour remain explicit work. Browser/player remain
last. Real audio-server and current-profile acceptance require a later requested
build/user observation. The next normal-priority chapter audits and implements
original private Public-network conversations and their policy/crypto/UI
prerequisites; unsupported private commands remain local until qualified.
Continue autonomously without replacing the running preview.
