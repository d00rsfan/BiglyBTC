# Deferred: intermittent footer transfer-rate overestimate

Priority: low. User testing of the chat preview reported occasional upload rates
above the connection capacity and above the sum of the per-torrent Upload speed
column. The footer shows only the upload arrow and a kB/s or MB/s value; there is
no displayed configured-limit prefix. It is intermittent. Preserve this issue
while continuing the private chat-window chapter; no urgent image replacement.

Current source path: ClassicStatusBar::render -> nativeActivityRates Source10s ->
SourceRateProjection::observe -> SourceRateAverage::add/average. Table columns
show current payload rates; the footer may include protocol traffic according
to saved original display settings. This difference alone does not establish
that the reported strong overestimate is correct.

Hypothesis from source inspection, not yet a reproduced diagnosis: a delayed
owner observation delivers a cumulative byte delta spanning more than the
10-second source-style window. Charging the whole delta to its delivery bucket
could temporarily overestimate the actual rate. The existing code deliberately
does not reconstruct byte-arrival timing, so Java primitive bucket parity alone
does not qualify this observation adapter against long scheduling gaps.

Next: add a generated regression with constant known traffic and delayed samples
(including an observation interval longer than10s), compare displayed rates with
actual elapsed time, test boundary/duplicate/missing samples and recovery. Do
not hide the issue by clipping the rate to a configured limit. Choose an honest
unknown/interval fallback or another qualified adapter policy; retain original
formats/preferences and distinguish payload/protocol and current/windowed rates.
No user profile, personal endpoint, passkey or private trace belongs in evidence.
