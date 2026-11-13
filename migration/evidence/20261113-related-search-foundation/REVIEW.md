# Public related search foundation

Source milestone `bd940d52896febb4ecab10532afb030950630381`, tree
`7690060ba065038066f0f16153c0ed9671b1b187`, uses original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. This is the query/Bloom/wire and DDB Read
foundation. Distributed scheduling, shared-owner permissions and GUI search
are the next work in the same active chapter. F12 remains partial; this does
not claim network search is already integrated or accepted on a real library.

Actual original JDK17 fixupTerm/matchContent0/getDHTWords/searchForeignBlooms,
BloomFilterFactory/AddOnly serialization, DDBaseHelpers migrated handler keys
and BEncoder ran on95 generated items/42 queries. Native agrees on exact
normalisation/hit sets, UTF16 words/tag prefixes, serialized Bloom map/count,
foreign membership and request/result bytes (5767 assertions). Result field
construction in the oracle is an explicit projection of original response
getters, not invocation of the complete original receiveRemoteRequest service.
The matcher has finite PCRE match/depth budgets, input/iteration/time quotas,
original evil-pattern/literal fallback controls and tested Unicode cases; full
Java Pattern dialect/Unicode-version parity is not claimed by this corpus.

The existing original Bloom implementation is compiled once. Chat creation/
decoding retains8193bits/count512. Separate related-search entry points accept
50001bits; original insertion cap is6250, with the original all-filter's one
extra DHT tail word allowing serialized count6251. Each all/non-DHT filter
sizes its own word set. Shuffle order is deliberately not a compatibility
identity; the original also shuffles. Membership is an optimisation and grants
no profile/contact/publication permission. Incoming Bloom structures stay
strictly bounded and duplicate keys are refused.

DDB Read is explicitly distinct from chat Call: the request is a1..255-byte
DATA key, and ReadReply returns on the same connection. Long request keys
reduce fragment payload to retain the1430-byte physical datagram bound. The
existing owner, peer, source revision, finite work, deadline and opaque receipt
checks remain. Defaults for chat registrations are unchanged. Actual original
DHTTransferHandler interoperates both ways with200-byte keys and6000-byte
fragmented replies on owned loopback sockets. The affected transport suite
passed21 QTests, including original Call, new Read, cancellation/revocation and
malformed/foreign inputs. The affected chat-room Bloom/history suite passed.
OriginalDataBody is a source-only DATA serializer projection from the pinned
Java implementation; the actual original transfer state machine executes.

Public search results encode hashes/title/counts/source fingerprints/tags, never
the Item tracker URL or credentials. A synthetic passkey-bearing URL regression
checks this boundary. Search responses cap30 hits/50 numeric contact hints and
16KiB; request maps cap255bytes. Unresolved/I2P contact export adapters remain
outside this Public numeric-contact slice and cannot enable another network.

Tests only used generated profiles/data and explicit owned loopback peers. No
user profile, running client, SDK binary, frozen preview or daily-use runtime
was changed. Compiler/test logs remain local; public fixtures are synthetic.
The latest user instruction authorises continuous source chapters and forbids
intermediate daily-use image promotions until explicitly requested.
