# Original chat transcript formatting and search

Native b101417b9f4ae3ea7a3f3edbdf0287871da6b80f, tree 4a75669da49353d707f8cbbfd430ebc1468a3b28; original BiglyBT baseline remains
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. This bounded chapter is complete.
The requested local test build is ready; work pauses at the user's boundary.
Whole chat/plugin/client parity and this candidate's real-profile acceptance
remain incomplete. No next chapter was started.

Emphasis and transcript search follow selected original methods in
BuddyPluginViewBetaChat. Bold, italic and explicit normal aliases, original
FEFF separators, percent encoding and the user-origin formatting gate preserve
raw signed history. Own-name highlighting uses the configured nickname or the
original public-key short name; the settings reader does not change nickname
mode. Rendering uses QTextCursor text and character formats, never received HTML.

The always-visible search field supports Ctrl+F, Enter and Escape. Enter advances
to overlapping matches and stops at the last occurrence, as in Java; it does not
wrap. ShortcutOverride resolves the existing main-window torrent-filter shortcut
only inside the chat page. Search/view state is retained separately per room and
profile generation. Message-ID/offset anchors preserve selection and scroll;
appended messages use an incremental path. Evicted selections clear safely.

HTTP(S) links and canonical original Public chat:?ENCODEDKEY links require
explicit user activation. Private opaque handles bind actions to the current
room, generation and retained message. Stale handles are discarded; append-only
updates retain existing handles. Copy Link copies the actual target. Dangerous
or unsupported schemes stay literal. The standalone UI tests observe navigation
signals without opening an external browser. General and property Chat share
the renderer and navigation adapters.

Qualification:304 generated selected-original Java emphasis/search cases and all
65536 JDK17 Character whitespace values pass (0.94s); participant-name and
preference fixtures pass (0.25s/0.36s). The final ChatPage suite passes at scale2
in1.46s with zero skips, including full-window Ctrl+F, NBSP nickname highlighting,
selection at EOF, history eviction, per-room search, signed-origin formatting,
allowed link actions and stale-handle rejection. One initial test incorrectly
used Qt document.find for NBSP, which normalises it; the final test checks the
raw document offset and actual character format. The production renderer did
not require a correction for that test. The complete GCC16/C++23/Qt6.12 desktop
application links. No unrelated long green network suite was repeated.

[Selected source hashes](source-scope.json) identify the exact original methods
and reference collaborators. The [2x UI image](transcript-2x.png) was visually
reviewed and uses only synthetic participants, text and room names.
[Build receipt](build.json) freezes the source, executable and unchanged provider.
The stable chat-preview/daily-use launcher selects the new image on the user's
next launch; preparation did not open the live profile or launch the application.
No full profile backups or rollback copies were made. The previous running image
is left intact and can be retired by the next launcher only after its manifest
lock is released and its exact hash matches. First user launch can retain one
compact private service-state diagnostic, without opening payload data.

Boundaries: full original URI/resource/font/locale behaviour, private/friend and
moderation commands, non-Public providers and exhaustive SWT lifecycle parity
remain planned. Search reference locale is English; exhaustive Unicode-version
case mapping is not claimed. Embedded browser and media player remain last.
Shared engine, torrent resume and SDK behaviour were not changed by this chapter.
