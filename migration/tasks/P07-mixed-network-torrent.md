# Mixed Public and I2P torrent

Bounded chapter complete; stopped. Native `90f1d3defd31acacb784cb8cda40d8d06ba6d76d` (preview tree `2210063859f4940974addd3543b6473c74dfae69`) qualifies the bounded mixed Public+I2P single-torrent chapter. A mixed row has one Public torrent handle, payload/piece/storage/checkpoint owner and queue slot. Key0 mixed transport borrows that owner; pure-I2P key1 remains separate. Explicit original network order/unknown metadata, source fences, independent lifecycle, shared queue/rates and cumulative payload counters remain preserved. Whole client/P07 and larger components remain in progress.

[Review](../evidence/20261031-mixed-public-i2p/REVIEW.md).

Next proposed only: Propose I2P peer discovery and magnets beyond tracker-only peers and explicitly warmed known local LS2 caches; do not begin it.
