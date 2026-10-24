Metadata start correction; source-only, unexecuted. Replace the three native paths from staging/ after checking before_sha256. No CMake/API changes.

Pinned LT2.0.12 torrent_flags.hpp183–186 explicitly states that downloading metadata counts as a downloading state, and recommends dont_download priorities to stop after metadata. torrent.cpp134–151 confirms that classification;689 enters downloading_metadata, and11758–11790 triggers stop_when_ready. The previous metadataOnlyParameters therefore paused at metadata admission, preventing the actual BEP9 exchange.

Clear stop_when_ready explicitly and retain upload_mode, default_dont_download, registered disabled-IO routing, exact metadata identity validation and owner-driven retirement. The pure flag assertion now requires stop_when_ready absent. All no-payload/no-IO, peer, source, cancellation and join assertions remain unchanged.

Seven existing Qt wait assertions now attach passive bounded PendingMagnetStatus/owner/payload status diagnostics (at most2048 UTF-8-source characters, each failure truncated512). Their original conditions and deadlines remain exact; no new poll, alert consumer, sleep, retry or success criterion is added. The case count is unchanged. Root owns actual execution and qualification; author ran no C++ or Java.
