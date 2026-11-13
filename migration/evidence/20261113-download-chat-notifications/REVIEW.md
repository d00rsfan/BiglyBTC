# Read-side automatic download chats and actual incoming notifications

Native1022c7b60958e7507ba075a76e5b02fe20e42986, tree
a2eb23cd43093ac3f9347652613879c018a42259. Original BiglyBT baseline remains
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f; azrating remains
1629340340d0bc391d9e582f9ee989a93d550581; azmsgsync remains
4209aa5029e3035cbb09dc3a83e9491d3f36fbc6. Earlier preview/property-room reviews
retain their exact interoperability scope. RatingsUpdater.readRating and
getChatForDownload, BuddyPluginBeta.setAutoNotify and original peek response
conditions were inspected. This is not complete buddy/plugin/UI parity.

A completed owned rating read checks the original automatic-chat and plugin/DHT
switches, then queues an identity-free canonical preview. Generic hash-only
reads and rating writes do not activate this path. A real valid preview with
m>0 enables the background room; n may be0, exactly as the original condition.
Missing/invalid replies, disabled settings and private targets create no room
identity. Pending automatic operations revalidate settings, source, membership
and Public permission before effects. Existing joined chats remain after the
automatic-creation preference is disabled, matching Java's cached chat reference;
manual access remains independent and reuses the same saved identity.

Only actual signed incoming messages increment unread state. Preview counts,
signature/key hints and network estimates never create participants or unread
messages. The general catalogue remains unchanged. A separate download
notification projection feeds the lower status bar, deduplicates a room also
visible in the general list and opens the matching torrent's Chat property tab.
A missing torrent falls back to an explicit general-room open. Room/message
and profile/hash scopes remain checked. Hidden property Chat pages unsubscribe
from transcript projection; returning resubscribes without reopening the network
session. A pending property open survives initial service attachment only when
its profile generation matches. Reentrant snapshot callbacks have life guards.

Actual IPv4/IPv6 tests start with two messages and a preview reporting n=0,
observe automatic joining and exactly two signed messages/unreads, preserve
keys after disabling automatic creation, and permit manual reuse. Disabled
creation and private-torrent negatives also pass. The final configured
three-test regression (automatic-download-chat-notifications, shared-owned-
rating-chat-notices, general-chat-room-service) passes in100.91seconds, zero
failures/skips. The full notification/visibility/general chat UI regression
passes in1.06seconds. New UI cases verify status-bar routing/deduplication and
hidden/returning subscriptions without an extra open request. The static desktop
host and production main object compile; no daily-use executable is replaced.

No real profile, running client, SDK artifact, external chat or user metadata
was used. The chapter's remaining resource check is avoiding repeated full
library/metadata parsing in fresh per-packet download permission checks. Original
chat display/attention preferences remain the next normal-priority follow-up;
non-Public providers and embedded browser/player are not implied here.
