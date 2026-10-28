# Proposed clarification: private Mainline PORT bridge

Status: proposed for bounded root review. Related: AD-001 and AD-004; existing privacy-routing requirements remain unchanged. Existing accepted decisions are unchanged.

Keep the payload/magnet torrent session’s internal DHT disabled. Its actual TCP/BEP5 peer connection may expose a typed observation to the sole owner, carrying the admission, actual numeric socket source, advertised UDP port and actual connected wire hash/protocol. A bounded queue with sticky loss and retirement withdrawal does not make the remote endpoint trustworthy. Only a current eligible original Public/direct policy may pass it to the dedicated Mainline service, which must obtain a real correlated response and actual live routing proof before rearming a lookup. Unknown caches, disabled/private/denied sources, unavailable providers and retired observations confer no authority or alternate route.

Outgoing advertisement uses only the same live eligible Mainline UDP listener port. Zero withdraws; already queued transport bytes require real retirement. Existing connections can be refreshed on an actual port transition without a new sampler or timer.

This is an exact private LT source/SDK C++ bridge, not a stable public plugin ABI or a relaxation of AD-008. The shared observer and fixed72-byte event require the frozen SDK/compiler ABI and the same fully checked20-export DSO mapping held through callback lifetime. A header, version, marker or different correct file on disk cannot grant it. Every older source/provider cohort and its trust scope stays separate.

This proposal authorises no shipping/parity claim. Original Java caches/IDs remain untouched, Mainline never substitutes for Azureus DHT, and the root must attach actual native results before advancing the bounded component.
