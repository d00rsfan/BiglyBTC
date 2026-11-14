# Mainline startup with compatible large configuration

Native `b7da2c3226c3a1199e6a6df0052468abdb109cea`, tree `71647bfc010b08df8d20b34c77b518cb0b021db9`; original BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`;
original mlDHT `3b12515c5541ed75cfb4f795b672d4e786df9f45` (unchanged).

The Mainline bind-source reader reused a tracker helper limited to 1 MiB while
the normal profile reader accepted larger compatible configurations. A valid
profile could therefore open its library but fail before creating the DHT socket.
The shared helper now uses the existing compatibleConfigLimits. Pending writes,
primary/recovery selection, source revision and fresh tracker/proxy controls
retain their existing checks. This changes file parsing limits, not packet limits.
Unknown configuration data is preserved. No configured listen port is changed:
original mlDHT has its own UDP setting, separate from the torrent listener.

Unavailable Mainline failures now appear once per failure transition on stderr
and in the escaped statusbar tooltip. Recovery clears the reason; offline and
healthy tooltips do not retain stale failure text. The actual host test activates
the original Mainline identity from a generated configuration with more than
1 MiB of opaque data, then confirms unchanged configuration and identity data.

## Validation

GCC16, C++23, Qt6.12.0, retained pinned SDK. Focused QtTest invocations are recorded
in [qualification](qualification.json). New large-config owner plus changed-source
refusal passed in0.81s; new large main/tracker config with pending-write and fresh
disable checks passed in0.03s; tracker full-subject/proxy-authority checks passed
in0.08s. Three tooltip/status/settings UI cases passed in0.86s; malformed identity
reporting with payload owner preserved passed in2.61s. The pre-existing route/
saved-settings guard also passed. Only failed test fixtures were corrected and
repeated: close API/insert-missing-key setup, with no additional production change.

The fixed application built and the frozen preview manifest/hash validation
passed. Its no-profile --help path prints usage and exits2 by the existing CLI
contract; no profile/application session is opened by this smoke check. Real
profile restart/socket/public-peer acceptance remains pending user observation.
This review does not claim public DHT convergence, inbound NAT reachability or
whole-client parity. Local diagnostic inputs/logs are excluded from publication.
No full backup, payload copy or real-profile modification was made for diagnosis.
No separate reviewer was used. Other NAT/private-chat work continues separately.

Live restart follow-up: the user confirmed `mlDHT: active` in the fixed image. Its own Mainline UDP sockets were observed for both IP families, alongside the separate torrent listener. This qualifies successful real-profile startup; public convergence and incoming NAT reachability were not measured.
