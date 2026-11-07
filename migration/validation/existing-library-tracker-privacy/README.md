# Private tracker credentials and public discovery

This focused regression links the same production archives used by the normal desktop Host. It does not replace the application, modify a production source, use a real profile or contact real trackers.

One generated library contains an active public torrent and an active private=1 torrent. The credentialed torrent is tested both with private=1 and with private=0 (public torrent, authenticated tracker). A path passkey and a different query passkey belong only to the private HTTP tracker. Owned HTTP/UDP trackers and a transparent Mainline KRPC capture check complete outgoing requests, genuine get_peers/announce_peer progress, public/private infohash separation and credential delivery to the intended private tracker. IPv4/IPv6 × public HTTP/UDP gives eight rows per lane. Negative assertions run after positive network barriers and joined close.

Run from this directory with configured caches available:

    make --jobserver-style=fifo -j3 -f batch.mk all

The recipe reuses configured normal-Host compiler/link inputs, builds only this test, and records the full commands, inputs, linked archive hashes and XML. It does not rebuild or copy Qt/provider/application binaries. For another attempt select --attempt with run.py under the same GNU Make jobserver; previous results are immutable.

Protocol references: [BEP 5](https://www.bittorrent.org/beps/bep_0005.html) defines DHT query fields; [BEP 27](https://www.bittorrent.org/beps/bep_0027.html) excludes private torrents from public discovery. Arbitrary account-bearing tracker redirects, peer metadata-extension sharing, logs and other plugins are outside this wire regression. A DHT write token is supplied by the DHT node and is separate from a tracker account passkey. This is bounded runtime regression evidence, not a claim about all possible plugins, malicious custom trackers or metadata sharing features.
