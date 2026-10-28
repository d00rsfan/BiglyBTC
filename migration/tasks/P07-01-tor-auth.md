# P07-01 Tor SAFECOOKIE controller authentication

Status: bounded authentication implementation qualified across GCC16/Clang24/
ASan+UBSan+LSan; full Tor controller/host integration pending. Original aznettor baseline
`ae8efb8d40fb38246ac1790ab5cfb11584689c4a`,
TorPlugin.ControlConnection.connect SAFECOOKIE/HmacSHA256 block.

Port server proof verification and controller proof generation to OpenSSL3.5.5.
Use the exact extracted original block with supplied transcript/cookie/client
nonce and no socket/file/process operations as a fenced Java reference. Compare
valid and tampered transcript fixtures, exact challenge/authentication commands,
uppercase/lowercase hex handling and rejection before exposing controller proof.
Native uses constant-time server-hash comparison and bounded input with exact
32-byte cookies/nonces. No password/null auth fallback or direct-network fallback.

This library does not start Tor, connect to a control/SOCKS socket, read cookies,
modify torrc, take ownership of a process, launch a timer or change proxy routing.
Host nonce generation, owned process lifecycle/cancellation/idle shutdown,
control framing/events, secrets file handling, original settings and onion-service
identities remain required before activating the full Tor plugin.

1024 original transcript cases match on all three compilers (384 accepted and
640 rejected), plus malformed/oversized/multiline input and every server-proof
hex-digit corruption check. Matrix pins native source/binary and OpenSSL header/
runtime hashes. No Tor executable is selected or launched.
