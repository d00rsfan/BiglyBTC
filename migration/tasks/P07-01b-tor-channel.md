# P07-01b Qt Tor control channel

Status: bounded channel implementation qualified across GCC16/Clang24/
ASan+UBSan+LSan; production host activation pending. Shared native service using the qualified offline Tor proof
library and Qt6.12 Core/Network; no Widgets dependency. Original aznettor baseline
`ae8efb8d40fb38246ac1790ab5cfb11584689c4a` ControlConnection connection/framing.

Owner-thread asynchronous numeric127.0.0.1 connection, explicitly NoProxy. A single
handshake deadline, bounded line/input/output buffering, verified server proof
before controller proof, authenticated-only commands, cancellation/EOF/timeout
and Qt callback reentrancy/destruction are required. Stop removes its own socket
only; no PID/process ownership or helper launch is part of this channel. Test
only newly created numeric loopback servers on ephemeral owned ports, no Tor or
original application profiles/processes. Do not log cookie/proof/raw transcripts.

Host nonce creation and protected original cookie files, process start/ownership/
idle-stop, settings/UI, command transaction/event demultiplexing and privacy/SOCKS
routing remain required. Production activation is not enabled in the app.

All three CTest rows pass per compiler, including original SAFECOOKIE fixtures.
QtTest covers fragmented/whole replies, authenticated-only commands, failed proof/
ack/EOF/framing/oversize/deadline, invalid parameters, callback cancellation and
destruction at every connection state and authenticated/reply notifications,
remote EOF after ready, deadline cancellation and fairness under reply flood.
Source/binary/Qt/OpenSSL hashes and full test logs are retained in P07-01b evidence.
