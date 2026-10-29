# P07-01c Tor controller ownership setup

Status: implemented bounded wrapper; fresh GCC16/Clang24/ASan+UBSan+LSan pass.
Evidence: migration/evidence/P07-01c-tor-ownership/{matrix.json,review.md}. Original aznettor baseline chosen before drafting:
ae8efb8d40fb38246ac1790ab5cfb11584689c4a, TorPlugin.ControlConnection.connect,
getInfo, sendAndReceive and readLine(boolean). Exact file/method fingerprints
and independent source observations are retained. Preserve existing SAFECOOKIE
and ControlChannel APIs, runtime plugin identity and Java files.

Add a separate owner-thread wrapper that authenticates, sends TAKEOWNERSHIP,
RESETCONF __OwningControllerProcess, then GETINFO version. Source acknowledgment
prefixes, exact terminal250 OK, last-line substring4 projection and empty result
remain intact. Native limits: one fixed steady deadline across all stages,
32 version-reply lines,4096 trimmed reply bytes plus old1024 framing bounds.
Late connection/challenge transitions must enforce expiry even before the timer
notification. Caller signals may stop/delete the wrapper safely at every state.

This owns a socket only. Require an explicit newly owned loopback fake peer for
tests; no existing daemon discovery/adoption, Tor launch/signals, cookie/profile
access, process ownership claim, proxy activation, keepalive/events or app/plugin
activation. A future QProcess owner must retain its exact new child handle from
start, including failure before authentication; original existing-daemon HALT
behavior is excluded under the user's running-client constraint.

Independently freeze93 exact Java StringReader control-flow observations before
native comparison (81 source accepts,12 rejects; four explicit native bound
differences). Character-level Latin1 observation encoding does not prove the
original socket InputStreamReader's default-charset wire decoding. Generated
ASCII setup transcripts exercise actual owned sockets. Add source-driven Qt
rows plus lifecycle/failure/late-auth tests and unchanged channel/auth regressions.
Use fresh persistent project build roots for GCC16/Clang24/ASan+UBSan+LSan,
two compilers maximum at reduced priority. Retain failures and qualified input,
helper, runtime, binary and immutable commit provenance. Continue actual owned
child lifecycle and settings/proxy integration after this bounded component.


## 2026-10-29 bounded follow-up

Post-Ready control EOF/protocol loss now withdraws state/port/source authority immediately. Terminal callbacks are deferred until the Qt socket frame unwinds, preserving synchronous controller deletion and owned child join. GCC/SAN mock lifecycle and explicit extracted offline Tor checks passed; this does not activate complete app Tor routing.

Qualified source: `3228091a1e12f81b334e27e589da8605c0b76420`. See `migration/evidence/20261029-tracker-tiers-tor-console/REVIEW.md` for the initial failures, repairs and explicit reuse of identical tested executables.
